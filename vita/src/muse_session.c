#include "muse_session.h"

#include <stdio.h>
#include <string.h>

#include <curl/websockets.h>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include "json_extract.h"
#include "link_wire.h"
#include "vita_media.h"

#define PAIRING_PATH "ux0:data/MuseVita/pairing.json"
#define SDK_TOKEN_PATH "ux0:data/MuseVita/sdk_token"
#define CREDENTIALS_PATH "ux0:data/MuseVita/credentials.bin"
#define IDENTITY_PATH "ux0:data/MuseVita/identity.json"
#define API_URL "https://api.muse.ai/fetch_vms"
#define USER_AGENT "MuseVita/0.1.0 (PlayStation Vita)"
#define HANDSHAKE_TIMEOUT_US (20u * 1000u * 1000u)
#define PING_INTERVAL_US (20u * 1000u * 1000u)
#define RETRY_DELAY_US (10u * 1000u * 1000u)
#define MAX_HTTP_JSON_BYTES (64u * 1024u)
#define MAX_CONTROL_OUTBOUND_MESSAGE_BYTES (256u * 1024u)
#define CREDENTIAL_MAGIC "MVC1"
#define CREDENTIAL_PLAINTEXT_BYTES 4096u

typedef struct ResponseBuffer {
    char *data;
    size_t size;
    size_t capacity;
} ResponseBuffer;

static void make_identity(MuseVitaSession *session, const char *json, size_t json_size);
static int read_file(const char *path, char *output, size_t capacity, size_t *output_size);
static int delete_file(const char *path);
static int consume_chat_bytes(MuseVitaSession *session, const uint8_t *bytes, size_t size);

static int is_hex_digit(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

static int valid_mac(const char *mac) {
    if (mac == NULL || strlen(mac) != 17) return 0;
    for (size_t i = 0; i < 17; i++) {
        if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
            if (mac[i] != ':') return 0;
        } else if (!is_hex_digit(mac[i])) {
            return 0;
        }
    }
    return 1;
}

static int valid_sdk_token(const char *token) {
    static const char last_alphabet[] = "AEIMQUYcgkosw048";
    size_t length;
    if (token == NULL) return 0;
    length = strlen(token);
    if (length != 48 || strncmp(token, "mgst_", 5) != 0) return 0;
    for (size_t i = 5; i < 47; i++) {
        char value = token[i];
        if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-')) return 0;
    }
    return strchr(last_alphabet, token[47]) != NULL;
}

static int load_sdk_token_file(MuseVitaSession *session) {
    char token[sizeof(session->sdk_token)];
    size_t token_size = 0;
    size_t start = 0;
    size_t end;
    if (read_file(SDK_TOKEN_PATH, token, sizeof(token), &token_size) != 0) return -1;
    while (start < token_size && (token[start] == ' ' || token[start] == '\t' ||
                                  token[start] == '\r' || token[start] == '\n')) start++;
    end = token_size;
    while (end > start && (token[end - 1] == ' ' || token[end - 1] == '\t' ||
                           token[end - 1] == '\r' || token[end - 1] == '\n')) end--;
    if (end == start || end - start >= sizeof(session->sdk_token)) return -1;
    memcpy(session->sdk_token, token + start, end - start);
    session->sdk_token[end - start] = '\0';
    return 0;
}

static void make_request_id(char output[40]) {
    uint64_t time = (uint64_t)sceKernelGetSystemTimeWide();
    snprintf(output, 40, "%08x-%04x-4%03x-8%03x-%012llx",
             (unsigned)(time >> 32), (unsigned)((time >> 16) & 0xffffu),
             (unsigned)(time & 0xfffu), (unsigned)((time >> 4) & 0x3ffu),
             (unsigned long long)(time & 0xffffffffffffULL));
}

static void close_transport(MuseVitaSession *session) {
    if (session->noise_ready) {
        muse_noise_xx_free(&session->noise);
        session->noise_ready = false;
    }
    if (session->curl != NULL) {
        curl_easy_cleanup(session->curl);
        session->curl = NULL;
    }
    if (session->headers != NULL) {
        curl_slist_free_all(session->headers);
        session->headers = NULL;
    }
    session->registered = false;
    session->subscribed = false;
    session->ws_message_active = false;
    session->ws_text_active = false;
    session->ws_message_size = 0;
    session->assembly_size = 0;
    session->assembly_count = 0;
    session->last_chat_seq = 0;
    session->last_chat_row_seq = 0;
    session->subscribe_buffer_size = 0;
    session->request_buffer_size = 0;
    session->request_active = false;
    session->request_done = false;
    session->request_failed = false;
    session->request_is_history = false;
    session->history_requested = false;
    session->awaiting_chat_ack = false;
    session->deferred_chat_overflow = false;
    session->deferred_chat_size = 0;
}

static void clear_deferred_chat(MuseVitaSession *session) {
    session->awaiting_chat_ack = false;
    session->deferred_chat_overflow = false;
    session->deferred_chat_size = 0;
}

static void set_transport_error(MuseVitaSession *session) {
    close_transport(session);
    session->state = MUSEVITA_SESSION_ERROR;
    session->retry_after_us = (uint64_t)sceKernelGetSystemTimeWide() + RETRY_DELAY_US;
    if (session->chat != NULL) session->chat->transport_connected = false;
}

static void forget_pairing(MuseVitaSession *session) {
    (void)delete_file(CREDENTIALS_PATH);
    (void)delete_file(PAIRING_PATH);
    (void)delete_file(SDK_TOKEN_PATH);
    memset(session->access_token, 0, sizeof(session->access_token));
    memset(session->refresh_token, 0, sizeof(session->refresh_token));
    memset(session->sdk_token, 0, sizeof(session->sdk_token));
    session->registered = false;
    session->subscribed = false;
    session->unpaired = true;
    session->state = MUSEVITA_SESSION_NO_PAIRING;
    if (session->chat != NULL) session->chat->transport_connected = false;
}

static size_t curl_write_response(char *data, size_t size, size_t count, void *context) {
    ResponseBuffer *response = context;
    size_t bytes = size * count;
    if (bytes > response->capacity - response->size) return 0;
    memcpy(response->data + response->size, data, bytes);
    response->size += bytes;
    return bytes;
}

static int read_file(const char *path, char *output, size_t capacity, size_t *output_size) {
    SceUID fd;
    size_t total = 0;
    int read_size;
    if (path == NULL || output == NULL || output_size == NULL || capacity == 0) return -1;
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return -1;
    while (total + 1 < capacity) {
        read_size = sceIoRead(fd, output + total, capacity - total - 1);
        if (read_size < 0) {
            sceIoClose(fd);
            return -1;
        }
        if (read_size == 0) break;
        total += (size_t)read_size;
    }
    sceIoClose(fd);
    output[total] = '\0';
    *output_size = total;
    return 0;
}

static int write_file(const char *path, const char *data, size_t size) {
    SceUID fd;
    const char *slash = strrchr(path, '/');
    if (slash != NULL) {
        char directory[128];
        size_t length = (size_t)(slash - path);
        if (length >= sizeof(directory)) return -1;
        memcpy(directory, path, length);
        directory[length] = '\0';
        (void)sceIoMkdir(directory, 0777);
    }
    fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0600);
    if (fd < 0) return -1;
    if (sceIoWrite(fd, data, size) != (int)size) {
        sceIoClose(fd);
        return -1;
    }
    sceIoClose(fd);
    return 0;
}

static int delete_file(const char *path) {
    int result = sceIoRemove(path);
    return result < 0 && result != (int)0x80010006u ? -1 : 0;
}

static void derive_credential_key(const MuseVitaSession *session, uint8_t key[32]) {
    static const char label[] = "MuseVita credential key v1|";
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts(&hash, 0);
    mbedtls_sha256_update(&hash, (const unsigned char *)label, sizeof(label) - 1);
    mbedtls_sha256_update(&hash, (const unsigned char *)session->mac, strlen(session->mac));
    mbedtls_sha256_finish(&hash, key);
    mbedtls_sha256_free(&hash);
}

static int save_encrypted_credentials(const MuseVitaSession *session) {
    uint8_t key[32];
    uint8_t nonce[12];
    uint8_t encrypted[CREDENTIAL_PLAINTEXT_BYTES];
    uint8_t tag[16];
    uint8_t record[4 + 12 + 4 + CREDENTIAL_PLAINTEXT_BYTES + 16];
    char plaintext[CREDENTIAL_PLAINTEXT_BYTES];
    mbedtls_gcm_context gcm;
    uint64_t time = (uint64_t)sceKernelGetSystemTimeWide();
    int plaintext_size;
    if (session->sdk_token[0] != '\0') {
        plaintext_size = snprintf(plaintext, sizeof(plaintext),
                                  "{\"access_token\":\"%s\",\"refresh_token\":\"%s\",\"sdk_token\":\"%s\"}",
                                  session->access_token, session->refresh_token, session->sdk_token);
    } else {
        plaintext_size = snprintf(plaintext, sizeof(plaintext),
                                  "{\"access_token\":\"%s\",\"refresh_token\":\"%s\"}",
                                  session->access_token, session->refresh_token);
    }
    if (plaintext_size < 0 || (size_t)plaintext_size >= sizeof(plaintext)) return -1;
    for (unsigned i = 0; i < sizeof(nonce); i++) {
        nonce[i] = (uint8_t)(time >> ((i % 8u) * 8u));
        if (i >= 8) nonce[i] ^= (uint8_t)(session->next_chunk_id >> ((i - 8u) * 8u));
    }
    derive_credential_key(session, key);
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0 ||
        mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, (size_t)plaintext_size,
                                  nonce, sizeof(nonce), (const unsigned char *)session->mac,
                                  strlen(session->mac), (const unsigned char *)plaintext,
                                  encrypted, sizeof(tag), tag) != 0) {
        mbedtls_gcm_free(&gcm);
        return -1;
    }
    mbedtls_gcm_free(&gcm);
    memcpy(record, CREDENTIAL_MAGIC, 4);
    memcpy(record + 4, nonce, sizeof(nonce));
    record[16] = (uint8_t)(plaintext_size & 0xff);
    record[17] = (uint8_t)((plaintext_size >> 8) & 0xff);
    record[18] = (uint8_t)((plaintext_size >> 16) & 0xff);
    record[19] = (uint8_t)((plaintext_size >> 24) & 0xff);
    memcpy(record + 20, encrypted, (size_t)plaintext_size);
    memcpy(record + 20 + plaintext_size, tag, sizeof(tag));
    return write_file(CREDENTIALS_PATH, (const char *)record, 20u + (size_t)plaintext_size + sizeof(tag));
}

static int load_encrypted_credentials(MuseVitaSession *session) {
    uint8_t record[4 + 12 + 4 + CREDENTIAL_PLAINTEXT_BYTES + 16];
    size_t record_size = 0;
    uint8_t key[32];
    uint8_t plaintext[CREDENTIAL_PLAINTEXT_BYTES];
    size_t plaintext_size;
    mbedtls_gcm_context gcm;
    if (read_file(CREDENTIALS_PATH, (char *)record, sizeof(record), &record_size) != 0 ||
        record_size < 36 || memcmp(record, CREDENTIAL_MAGIC, 4) != 0) return -1;
    plaintext_size = (size_t)record[16] | ((size_t)record[17] << 8) |
                     ((size_t)record[18] << 16) | ((size_t)record[19] << 24);
    if (plaintext_size == 0 || plaintext_size > CREDENTIAL_PLAINTEXT_BYTES ||
        record_size != 20u + plaintext_size + 16u) return -1;
    derive_credential_key(session, key);
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0 ||
        mbedtls_gcm_auth_decrypt(&gcm, plaintext_size, record + 4, 12,
                                 (const unsigned char *)session->mac, strlen(session->mac),
                                 record + 20 + plaintext_size, 16, record + 20,
                                 plaintext) != 0) {
        mbedtls_gcm_free(&gcm);
        return -1;
    }
    mbedtls_gcm_free(&gcm);
    if (!muse_json_find_string((const char *)plaintext, plaintext_size, "access_token",
                               session->access_token, sizeof(session->access_token)) ||
        !muse_json_find_string((const char *)plaintext, plaintext_size, "refresh_token",
                               session->refresh_token, sizeof(session->refresh_token))) return -1;
    (void)muse_json_find_string((const char *)plaintext, plaintext_size, "sdk_token",
                                session->sdk_token, sizeof(session->sdk_token));
    return 0;
}

static int ensure_identity(MuseVitaSession *session) {
    char identity[1024];
    size_t identity_size;
    if (read_file(IDENTITY_PATH, identity, sizeof(identity), &identity_size) == 0) {
        char mac[32];
        if (muse_json_find_string(identity, identity_size, "mac", mac, sizeof(mac)) &&
            valid_mac(mac)) {
            make_identity(session, identity, identity_size);
            return 0;
        }
    }
    make_identity(session, "{}", 2);
    {
        char record[128];
        int record_size = snprintf(record, sizeof(record), "{\"mac\":\"%s\"}", session->mac);
        if (record_size < 0 || (size_t)record_size >= sizeof(record)) return -1;
        return write_file(IDENTITY_PATH, record, (size_t)record_size);
    }
}

static void make_identity(MuseVitaSession *session, const char *json, size_t json_size) {
    char mac[32];
    uint64_t seed = (uint64_t)sceKernelGetSystemTimeWide();
    if (!muse_json_find_string(json, json_size, "mac", mac, sizeof(mac))) {
        for (unsigned i = 0; i < 6; i++) {
            seed = seed * 6364136223846793005ULL + 1;
            ((unsigned char *)mac)[i] = (unsigned char)(seed >> 32);
        }
        ((unsigned char *)mac)[0] = (unsigned char)((((unsigned char *)mac)[0] & 0xfcu) | 0x02u);
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                 (unsigned char)mac[0], (unsigned char)mac[1], (unsigned char)mac[2],
                 (unsigned char)mac[3], (unsigned char)mac[4], (unsigned char)mac[5]);
    }
    strncpy(session->mac, mac, sizeof(session->mac) - 1);
    session->mac[sizeof(session->mac) - 1] = '\0';
    size_t length = strlen(session->mac);
    const char *suffix = length > 8 ? session->mac + length - 8 : session->mac;
    char compact[16];
    size_t compact_size = 0;
    for (const char *cursor = suffix; *cursor != '\0' && compact_size + 1 < sizeof(compact); cursor++) {
        if (*cursor != ':') compact[compact_size++] = *cursor;
    }
    compact[compact_size] = '\0';
    if (compact_size > 6) memmove(compact, compact + compact_size - 6, 7);
    snprintf(session->node_id, sizeof(session->node_id), "homelink-%s", compact);
    snprintf(session->device_id, sizeof(session->device_id), "hatch-link:%s", session->mac);
}

int muse_vita_session_load_pairing(MuseVitaSession *session) {
    char pairing[16384];
    size_t pairing_size = 0;
    int encrypted_loaded;
    if (session == NULL) return -1;
    encrypted_loaded = load_encrypted_credentials(session) == 0;
    if (encrypted_loaded) {
        if (session->sdk_token[0] == '\0' && load_sdk_token_file(session) == 0) {
            if (save_encrypted_credentials(session) != 0) {
                session->state = MUSEVITA_SESSION_NO_PAIRING;
                return -1;
            }
            (void)delete_file(SDK_TOKEN_PATH);
        }
        session->state = MUSEVITA_SESSION_READY;
        return 0;
    }
    if (read_file(PAIRING_PATH, pairing, sizeof(pairing), &pairing_size) != 0) {
        session->state = MUSEVITA_SESSION_NO_PAIRING;
        return -1;
    }
    if (!muse_json_find_string(pairing, pairing_size, "access_token",
                               session->access_token, sizeof(session->access_token)) ||
        !muse_json_find_string(pairing, pairing_size, "refresh_token",
                               session->refresh_token, sizeof(session->refresh_token))) {
        session->state = MUSEVITA_SESSION_NO_PAIRING;
        return -1;
    }
    (void)muse_json_find_string(pairing, pairing_size, "sdk_token",
                                session->sdk_token, sizeof(session->sdk_token));
    if (session->sdk_token[0] == '\0') (void)load_sdk_token_file(session);
    if (save_encrypted_credentials(session) != 0) {
        session->state = MUSEVITA_SESSION_NO_PAIRING;
        return -1;
    }
    (void)delete_file(PAIRING_PATH);
    (void)delete_file(SDK_TOKEN_PATH);
    session->state = MUSEVITA_SESSION_READY;
    return 0;
}

int muse_vita_session_import_pairing(MuseVitaSession *session, const char *identity_json,
                                     size_t identity_size, const char *pairing_json,
                                     size_t pairing_size, const char *sdk_token) {
    char identity_mac[32];
    if (session == NULL || identity_json == NULL || pairing_json == NULL || sdk_token == NULL ||
        identity_size == 0 || pairing_size == 0 || strlen(sdk_token) == 0 ||
        strlen(sdk_token) >= sizeof(session->sdk_token) ||
        !muse_json_find_string(identity_json, identity_size, "mac", identity_mac, sizeof(identity_mac)) ||
        !valid_mac(identity_mac) || !valid_sdk_token(sdk_token)) return -1;
    if (!muse_json_find_string(pairing_json, pairing_size, "access_token",
                               session->access_token, sizeof(session->access_token)) ||
        !muse_json_find_string(pairing_json, pairing_size, "refresh_token",
                               session->refresh_token, sizeof(session->refresh_token))) return -1;
    if (write_file(IDENTITY_PATH, identity_json, identity_size) != 0) return -1;
    make_identity(session, identity_json, identity_size);
    strcpy(session->sdk_token, sdk_token);
    if (save_encrypted_credentials(session) != 0) return -1;
    session->state = MUSEVITA_SESSION_READY;
    return 0;
}

static int fetch_vm(MuseVitaSession *session) {
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    char response_data[MAX_HTTP_JSON_BYTES];
    char noise_host[sizeof(session->noise_host)];
    char vm_id[sizeof(session->vm_id)];
    char vm_name[sizeof(session->vm_id)];
    char vm_auth_token[sizeof(session->vm_auth_token)];
    ResponseBuffer response = {response_data, 0, sizeof(response_data) - 1};
    long status = 0;
    CURLcode result;
    if (curl == NULL) return -1;
    headers = curl_slist_append(headers, "X-API-Version: 1.0.0");
    {
        char authorization[2304];
        snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s", session->access_token);
        headers = curl_slist_append(headers, authorization);
    }
    curl_easy_setopt(curl, CURLOPT_URL, API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300) return status == 401 ? -2 : -1;
    memset(noise_host, 0, sizeof(noise_host));
    memset(vm_id, 0, sizeof(vm_id));
    memset(vm_name, 0, sizeof(vm_name));
    memset(vm_auth_token, 0, sizeof(vm_auth_token));
    if ((!muse_json_find_string(response_data, response.size, "vm_ws_url",
                                noise_host, sizeof(noise_host)) &&
         !muse_json_find_string(response_data, response.size, "vm_url",
                                noise_host, sizeof(noise_host))) ||
        !muse_json_find_string(response_data, response.size, "vm_auth_token",
                               vm_auth_token, sizeof(vm_auth_token)) ||
        (!muse_json_find_string(response_data, response.size, "vm_id",
                                vm_id, sizeof(vm_id)) &&
         !muse_json_find_string(response_data, response.size, "vm_name",
                                vm_name, sizeof(vm_name)))) return -1;
    if (vm_id[0] == '\0') {
        if (vm_name[0] == '\0') return -1;
        strncpy(vm_id, vm_name, sizeof(vm_id) - 1);
        vm_id[sizeof(vm_id) - 1] = '\0';
    }
    if (noise_host[0] == '\0' || vm_auth_token[0] == '\0') return -1;
    if (strncmp(noise_host, "wss://", 6) == 0) {
        char host[sizeof(noise_host)];
        const char *start = noise_host + 6;
        const char *end = strchr(start, '/');
        size_t length = end ? (size_t)(end - start) : strlen(start);
        if (length == 0 || length >= sizeof(host)) return -1;
        memcpy(host, start, length);
        host[length] = '\0';
        strncpy(noise_host, host, sizeof(noise_host) - 1);
        noise_host[sizeof(noise_host) - 1] = '\0';
    }
    strncpy(session->noise_host, noise_host, sizeof(session->noise_host) - 1);
    session->noise_host[sizeof(session->noise_host) - 1] = '\0';
    strncpy(session->vm_id, vm_id, sizeof(session->vm_id) - 1);
    session->vm_id[sizeof(session->vm_id) - 1] = '\0';
    strncpy(session->vm_auth_token, vm_auth_token, sizeof(session->vm_auth_token) - 1);
    session->vm_auth_token[sizeof(session->vm_auth_token) - 1] = '\0';
    return 0;
}

static int refresh_credentials(MuseVitaSession *session) {
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    char response_data[8192];
    char request_body[512];
    char raw_refresh[2048];
    char authorization[2304];
    ResponseBuffer response = {response_data, 0, sizeof(response_data) - 1};
    long status = 0;
    CURLcode result;
    const char *colon;
    if (curl == NULL) return -1;
    colon = strrchr(session->refresh_token, ':');
    colon = colon == NULL ? session->refresh_token : colon + 1;
    strncpy(raw_refresh, colon, sizeof(raw_refresh) - 1);
    raw_refresh[sizeof(raw_refresh) - 1] = '\0';
    if (session->sdk_token[0] != '\0') {
        snprintf(request_body, sizeof(request_body),
                 "{\"device_id\":\"%s\",\"sdk_token\":\"%s\"}",
                 session->device_id, session->sdk_token);
    } else {
        snprintf(request_body, sizeof(request_body), "{\"device_id\":\"%s\"}", session->device_id);
    }
    snprintf(authorization, sizeof(authorization), "Authorization: Bearer hatch_refresh:%s", raw_refresh);
    headers = curl_slist_append(headers, authorization);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.muse.ai/device_token/refresh");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(request_body));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK || status < 200 || status >= 300 ||
        !muse_json_find_string(response_data, response.size, "access_token",
                               session->access_token, sizeof(session->access_token)) ||
        !muse_json_find_string(response_data, response.size, "refresh_token",
                               session->refresh_token, sizeof(session->refresh_token))) {
        return status == 401 ? -2 : -1;
    }
    (void)delete_file(PAIRING_PATH);
    return save_encrypted_credentials(session);
}

static int websocket_send(CURL *curl, const uint8_t *data, size_t size) {
    size_t sent = 0;
    CURLcode result = curl_ws_send(curl, data, size, &sent, 0, CURLWS_BINARY);
    return result == CURLE_OK && sent == size ? 0 : -1;
}

static int append_url_component(char *output, size_t capacity, size_t *offset, const char *value) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; value[i] != '\0'; i++) {
        unsigned char c = (unsigned char)value[i];
        int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                   c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
        if (safe) {
            if (*offset + 1 >= capacity) return -1;
            output[(*offset)++] = (char)c;
        } else {
            if (*offset + 3 >= capacity) return -1;
            output[(*offset)++] = '%';
            output[(*offset)++] = hex[c >> 4];
            output[(*offset)++] = hex[c & 0x0f];
        }
    }
    output[*offset] = '\0';
    return 0;
}

static int websocket_receive(MuseVitaSession *session, int blocking, size_t *message_size) {
    const struct curl_ws_frame *meta = NULL;
    size_t received = 0;
    uint64_t deadline = (uint64_t)sceKernelGetSystemTimeWide() + HANDSHAKE_TIMEOUT_US;
    while (1) {
        size_t available = sizeof(session->ws_message) - session->ws_message_size;
        CURLcode result;
        if (available == 0) return -1;
        result = curl_ws_recv(session->curl, session->ws_message + session->ws_message_size,
                              available, &received, &meta);
        if (result == CURLE_AGAIN) {
            if (!blocking) return 0;
            if ((uint64_t)sceKernelGetSystemTimeWide() >= deadline) return -1;
            sceKernelDelayThread(10000);
            continue;
        }
        if (result != CURLE_OK) return -1;
        if (meta != NULL && (meta->flags & CURLWS_CLOSE) != 0) return -1;
        if (session->ws_text_active) {
            if (meta == NULL || meta->bytesleft == 0) session->ws_text_active = false;
            continue;
        }
        if (meta != NULL && (meta->flags & CURLWS_TEXT) != 0) {
            session->ws_message_size = 0;
            session->ws_text_active = meta->bytesleft != 0;
            continue;
        }
        session->ws_message_size += received;
        if (meta != NULL && (meta->flags & (CURLWS_PING | CURLWS_PONG)) != 0) {
            session->ws_message_size = 0;
            if (meta->bytesleft == 0) continue;
            return -1;
        }
        if (meta == NULL || meta->bytesleft == 0) {
            *message_size = session->ws_message_size;
            session->ws_message_size = 0;
            return 1;
        }
        session->ws_message_active = true;
        if (!blocking) return 0;
    }
}

static int send_noise_payload(MuseVitaSession *session, const uint8_t *payload, size_t payload_size) {
    size_t frame_size = 0;
    size_t ciphertext_size = 0;
    MuseLinkWireError wire_result;
    if (payload_size > MUSE_LINK_MAX_NOISE_CHUNK_BYTES) return -1;
    if (muse_noise_xx_encrypt(&session->noise, payload, payload_size,
                              session->crypto_buffer, sizeof(session->crypto_buffer),
                              &ciphertext_size) != MUSE_NOISE_OK) return -1;
    wire_result = muse_link_encode_noise_frame(session->next_chunk_id++, 0, 1,
                                               session->crypto_buffer, ciphertext_size,
                                               session->frame_buffer, sizeof(session->frame_buffer),
                                               &frame_size);
    if (wire_result != MUSE_LINK_WIRE_OK) return -1;
    return websocket_send(session->curl, session->frame_buffer, frame_size);
}

static int send_link_payload(MuseVitaSession *session, const uint8_t *payload, size_t payload_size) {
    return send_noise_payload(session, payload, payload_size);
}

static int send_control_json(MuseVitaSession *session, const char *message, size_t message_size) {
    enum { CONTROL_BODY_CHUNK_BYTES = 16 * 1024 };
    uint8_t framed[CONTROL_BODY_CHUNK_BYTES];
    size_t framed_offset = 0;
    size_t total_size;
    if (session == NULL || message == NULL || message_size > MAX_CONTROL_OUTBOUND_MESSAGE_BYTES ||
        message_size > SIZE_MAX - sizeof(uint32_t)) return -1;
    total_size = sizeof(uint32_t) + message_size;
    framed[0] = (uint8_t)(message_size & 0xffu);
    framed[1] = (uint8_t)((message_size >> 8) & 0xffu);
    framed[2] = (uint8_t)((message_size >> 16) & 0xffu);
    framed[3] = (uint8_t)((message_size >> 24) & 0xffu);
    /* /link-control is a long-lived request. Each length-prefixed JSON
       message is a body chunk, not the end of the HTTP request. Split the
       control frame only at the transport body-chunk boundary; the decoder
       reassembles the four-byte length prefix and JSON message. */
    while (framed_offset < total_size) {
        size_t chunk_size = total_size - framed_offset;
        size_t encoded_size = 0;
        if (chunk_size > sizeof(framed)) chunk_size = sizeof(framed);
        for (size_t i = 0; i < chunk_size; i++) {
            size_t source_offset = framed_offset + i;
            framed[i] = source_offset < sizeof(uint32_t)
                ? framed[source_offset]
                : (uint8_t)message[source_offset - sizeof(uint32_t)];
        }
        if (muse_link_encode_body_chunk(session->control_stream_id, framed, chunk_size, 0,
                                        session->frame_buffer, sizeof(session->frame_buffer),
                                        &encoded_size) != MUSE_LINK_WIRE_OK ||
            send_link_payload(session, session->frame_buffer, encoded_size) != 0) return -1;
        framed_offset += chunk_size;
    }
    return 0;
}

static int send_register(MuseVitaSession *session) {
    char message[4096];
    size_t message_size = 0;
    char request_id[40];
    make_request_id(request_id);
    strcpy(session->register_request_id, request_id);
    if (muse_link_build_register_message(request_id, session->node_id, "MuseVita",
                                          message, sizeof(message), &message_size) != MUSE_LINK_WIRE_OK) return -1;
    return send_control_json(session, message, message_size);
}

static int send_subscribe(MuseVitaSession *session) {
    char request_id[40];
    MuseLinkHeader headers[4];
    make_request_id(request_id);
    headers[0] = (MuseLinkHeader){"Content-Type", "application/json"};
    headers[1] = (MuseLinkHeader){"Accept", "application/x-ndjson"};
    headers[2] = (MuseLinkHeader){"x-request-id", request_id};
    headers[3] = (MuseLinkHeader){"x-app-id", "musegadget"};
    const uint8_t body[] = "{}";
    size_t encoded_size = 0;
    session->subscribe_stream_id = session->next_stream_id++;
    if (muse_link_encode_http_request(session->subscribe_stream_id, "POST", "/chat/subscribe",
                                      headers, sizeof(headers) / sizeof(headers[0]), body, 2, 1,
                                      session->frame_buffer, sizeof(session->frame_buffer), &encoded_size) != MUSE_LINK_WIRE_OK) return -1;
    return send_link_payload(session, session->frame_buffer, encoded_size);
}

static void begin_request(MuseVitaSession *session, int64_t stream_id) {
    session->request_stream_id = stream_id;
    session->request_status = 0;
    session->request_buffer_size = 0;
    session->request_active = true;
    session->request_done = false;
    session->request_failed = false;
    session->request_is_history = false;
}

static void finish_request(MuseVitaSession *session, int failed) {
    session->request_failed = failed != 0 || session->request_failed;
    session->request_done = true;
}

static int wait_for_request(MuseVitaSession *session) {
    const uint64_t deadline = (uint64_t)sceKernelGetSystemTimeWide() + 60000000ULL;
    while (session->request_active && !session->request_done) {
        if (muse_vita_session_poll(session) != 0) {
            session->request_active = false;
            clear_deferred_chat(session);
            return -1;
        }
        if ((uint64_t)sceKernelGetSystemTimeWide() >= deadline) {
            session->request_active = false;
            clear_deferred_chat(session);
            return -1;
        }
        sceKernelDelayThread(10000);
    }
    if (!session->request_done || session->request_failed ||
        session->request_status < 200 || session->request_status >= 300) {
        session->request_active = false;
        clear_deferred_chat(session);
        return -1;
    }
    session->request_active = false;
    if (session->awaiting_chat_ack) {
        char identifier[128];
        int has_identifier = muse_json_find_string(session->request_buffer,
                                                   session->request_buffer_size,
                                                   "message_id", identifier,
                                                   sizeof(identifier)) ||
                             muse_json_find_string(session->request_buffer,
                                                   session->request_buffer_size,
                                                   "reply_to_message_id", identifier,
                                                   sizeof(identifier));
        session->last_user_id[0] = '\0';
        session->active_assistant_id[0] = '\0';
        if (has_identifier) {
            strncpy(session->last_user_id, identifier, sizeof(session->last_user_id) - 1);
            session->last_user_id[sizeof(session->last_user_id) - 1] = '\0';
        }
        session->awaiting_chat_ack = false;
        if (session->deferred_chat_overflow ||
            consume_chat_bytes(session, (const uint8_t *)session->deferred_chat_buffer,
                               session->deferred_chat_size) != 0) {
            clear_deferred_chat(session);
            return -1;
        }
        clear_deferred_chat(session);
    }
    return 0;
}

static MuseControlError control_message(const uint8_t *message, size_t message_size, void *context) {
    MuseVitaSession *session = context;
    char type[32];
    char method[32];
    char command[64];
    char request_id[128];
    if (message == NULL || message_size == 0) return MUSE_CONTROL_OK;
    if (muse_json_find_string((const char *)message, message_size, "type", type, sizeof(type)) &&
        strcmp(type, "err") == 0) {
        session->state = MUSEVITA_SESSION_ERROR;
        return MUSE_CONTROL_INVALID_ARGUMENT;
    }
    if (muse_json_find_string((const char *)message, message_size, "method", method, sizeof(method)) &&
        strcmp(method, "link.invoke") == 0) {
        char result[512];
        const char *error = NULL;
        const char *media_type = NULL;
        size_t media_size = 0;
        size_t result_size = 0;
        int success = 0;
        if (!muse_json_find_string((const char *)message, message_size, "id",
                                   request_id, sizeof(request_id)) ||
            !muse_json_find_string((const char *)message, message_size, "command",
                                   command, sizeof(command))) return MUSE_CONTROL_OK;
        if (strcmp(command, "device.guide") == 0) {
            if (muse_link_build_guide_result(request_id, result, sizeof(result), &result_size) != MUSE_LINK_WIRE_OK ||
                send_control_json(session, result, result_size) != 0) {
                session->state = MUSEVITA_SESSION_ERROR;
                return MUSE_CONTROL_INVALID_ARGUMENT;
            }
            return MUSE_CONTROL_OK;
        } else if (strcmp(command, "camera.capture") == 0 ||
                   strcmp(command, "vita.camera_capture") == 0) {
            media_type = "image";
            if (muse_vita_capture_photo_jpeg(session->media_capture_buffer,
                                             sizeof(session->media_capture_buffer), &media_size) == 0 &&
                muse_link_build_camera_result(request_id, session->media_capture_buffer, media_size,
                                              session->media_buffer, sizeof(session->media_buffer),
                                              &result_size) == MUSE_LINK_WIRE_OK &&
                send_control_json(session, session->media_buffer, result_size) == 0) {
                return MUSE_CONTROL_OK;
            } else {
                error = "camera capture or result encoding failed";
            }
        } else if (strcmp(command, "vita.microphone_record") == 0) {
            media_type = "file";
            if (muse_vita_capture_voice_wav(session->media_capture_buffer,
                                            sizeof(session->media_capture_buffer), &media_size) == 0 &&
                muse_vita_session_send_media(session, "Please transcribe this voice note.", "file",
                                             "audio/wav", "vita-voice.wav",
                                             session->media_capture_buffer, media_size) == 0) {
                success = 1;
            } else {
                error = "microphone capture or send failed";
            }
        } else {
            error = "unsupported command";
        }
        if (muse_link_build_command_result(request_id, success, error, media_type, media_size,
                                           result, sizeof(result), &result_size) != MUSE_LINK_WIRE_OK ||
            send_control_json(session, result, result_size) != 0) {
            session->state = MUSEVITA_SESSION_ERROR;
            return MUSE_CONTROL_INVALID_ARGUMENT;
        }
        return MUSE_CONTROL_OK;
    }
    if (muse_json_find_string((const char *)message, message_size, "event", command, sizeof(command)) &&
        (strcmp(command, "link.unpaired") == 0 || strcmp(command, "node.unpaired") == 0)) {
        forget_pairing(session);
        return MUSE_CONTROL_OK;
    }
    if (muse_json_find_string((const char *)message, message_size, "id", request_id, sizeof(request_id)) &&
        strcmp(request_id, session->register_request_id) == 0) {
        int ok = 1;
        if (muse_json_has_key((const char *)message, message_size, "error") ||
            (muse_json_find_bool((const char *)message, message_size, "ok", &ok) && !ok)) {
            session->state = MUSEVITA_SESSION_ERROR;
            return MUSE_CONTROL_INVALID_ARGUMENT;
        }
        session->registered = true;
        session->state = MUSEVITA_SESSION_CONNECTED;
    }
    return MUSE_CONTROL_OK;
}

static void consume_chat_line(MuseVitaSession *session, const char *line, size_t line_size) {
    char event[64];
    char identifier[128];
    char parent_identifier[128];
    char text[1024];
    int64_t sequence = 0;
    int has_identifier = muse_json_find_string(line, line_size, "message_id",
                                                identifier, sizeof(identifier)) ||
                         muse_json_find_string(line, line_size, "id",
                                                identifier, sizeof(identifier));
    int has_parent = muse_json_find_string(line, line_size, "reply_to_message_id",
                                           parent_identifier, sizeof(parent_identifier)) ||
                     muse_json_find_string(line, line_size, "parent_message_id",
                                           parent_identifier, sizeof(parent_identifier));
    if (session->awaiting_chat_ack) {
        if (line_size + 1 > sizeof(session->deferred_chat_buffer) - session->deferred_chat_size) {
            session->deferred_chat_overflow = true;
            return;
        }
        memcpy(session->deferred_chat_buffer + session->deferred_chat_size, line, line_size);
        session->deferred_chat_size += line_size;
        session->deferred_chat_buffer[session->deferred_chat_size++] = '\n';
        return;
    }
    if (!muse_json_find_string(line, line_size, "event", event, sizeof(event))) return;
    if (muse_json_find_int64(line, line_size, "seq", &sequence)) {
        int is_message_row = strcmp(event, "message.user") == 0 ||
                             strcmp(event, "message.assistant") == 0;
        int64_t *last_sequence = is_message_row ? &session->last_chat_row_seq
                                                : &session->last_chat_seq;
        if (sequence <= *last_sequence) return;
        *last_sequence = sequence;
    }
    if (has_parent && session->last_user_id[0] != '\0' &&
        strcmp(parent_identifier, session->last_user_id) != 0 &&
        strcmp(parent_identifier, session->active_assistant_id) != 0) return;
    if (strcmp(event, "message.user") == 0) {
        if (has_identifier && session->last_user_id[0] != '\0' &&
            strcmp(identifier, session->last_user_id) != 0) return;
        if (has_identifier && strcmp(identifier, session->last_rendered_user_id) == 0) return;
        if (muse_json_find_string(line, line_size, "display_text", text, sizeof(text)) ||
            muse_json_find_string(line, line_size, "content", text, sizeof(text)) ||
            muse_json_find_string(line, line_size, "text", text, sizeof(text))) {
            muse_vita_chat_add_message(session->chat, MUSEVITA_CHAT_USER, text);
            if (has_identifier) {
                strncpy(session->last_rendered_user_id, identifier,
                        sizeof(session->last_rendered_user_id) - 1);
                session->last_rendered_user_id[sizeof(session->last_rendered_user_id) - 1] = '\0';
            }
        }
    } else if (strcmp(event, "delta.message_start") == 0) {
        if (!has_identifier || strcmp(identifier, session->active_assistant_id) != 0) {
            muse_vita_chat_start_assistant(session->chat);
            if (has_identifier) {
                strncpy(session->active_assistant_id, identifier, sizeof(session->active_assistant_id) - 1);
                session->active_assistant_id[sizeof(session->active_assistant_id) - 1] = '\0';
            } else {
                session->active_assistant_id[0] = '\0';
            }
        }
    } else if (strcmp(event, "delta.text_append") == 0) {
        if (has_identifier && strcmp(identifier, session->active_assistant_id) != 0) {
            muse_vita_chat_start_assistant(session->chat);
            strncpy(session->active_assistant_id, identifier, sizeof(session->active_assistant_id) - 1);
            session->active_assistant_id[sizeof(session->active_assistant_id) - 1] = '\0';
        }
        if (muse_json_find_string(line, line_size, "text", text, sizeof(text))) {
            muse_vita_chat_append_assistant(session->chat, text);
        }
    } else if (strcmp(event, "delta.message_done") == 0 || strcmp(event, "message.assistant") == 0) {
        if (strcmp(event, "message.assistant") == 0 && has_identifier &&
            strcmp(identifier, session->active_assistant_id) != 0) {
            muse_vita_chat_start_assistant(session->chat);
            strncpy(session->active_assistant_id, identifier, sizeof(session->active_assistant_id) - 1);
            session->active_assistant_id[sizeof(session->active_assistant_id) - 1] = '\0';
        }
        if ((muse_json_find_string(line, line_size, "display_text", text, sizeof(text)) ||
             muse_json_find_string(line, line_size, "transcript", text, sizeof(text)) ||
             muse_json_find_string(line, line_size, "content", text, sizeof(text)) ||
             muse_json_find_assistant_text(line, line_size, text, sizeof(text)) ||
             muse_json_find_string(line, line_size, "text", text, sizeof(text))) &&
            text[0] != '\0') {
            muse_vita_chat_replace_last_assistant(session->chat, text);
        }
    }
}

static int consume_chat_bytes(MuseVitaSession *session, const uint8_t *bytes, size_t size) {
    if (size > sizeof(session->subscribe_buffer) - session->subscribe_buffer_size) {
        session->subscribe_buffer_size = 0;
        return -1;
    }
    memcpy(session->subscribe_buffer + session->subscribe_buffer_size, bytes, size);
    session->subscribe_buffer_size += size;
    size_t start = 0;
    for (size_t i = 0; i < session->subscribe_buffer_size; i++) {
        if (session->subscribe_buffer[i] != '\n') continue;
        consume_chat_line(session, session->subscribe_buffer + start, i - start);
        start = i + 1;
    }
    if (start != 0) {
        memmove(session->subscribe_buffer, session->subscribe_buffer + start,
                session->subscribe_buffer_size - start);
        session->subscribe_buffer_size -= start;
    }
    return 0;
}

static void consume_request_bytes(MuseVitaSession *session, const uint8_t *bytes, size_t size,
                                  int end_body) {
    if (!session->request_active) return;
    if (size > sizeof(session->request_buffer) - session->request_buffer_size) {
        session->request_failed = true;
        finish_request(session, 1);
        return;
    }
    memcpy(session->request_buffer + session->request_buffer_size, bytes, size);
    session->request_buffer_size += size;
    if (end_body) finish_request(session, 0);
}

static MuseVitaChatRole history_role(const char *role) {
    if (strcmp(role, "user") == 0 || strcmp(role, "human") == 0) {
        return MUSEVITA_CHAT_USER;
    }
    return MUSEVITA_CHAT_ASSISTANT;
}

static int history_role_is_supported(const char *role) {
    return strcmp(role, "user") == 0 || strcmp(role, "human") == 0 ||
           strcmp(role, "assistant") == 0 || strcmp(role, "agent") == 0 ||
           strcmp(role, "muse") == 0;
}

static void consume_history_response(MuseVitaSession *session) {
    size_t cursor = 0;
    unsigned int added = 0;
    while (cursor < session->request_buffer_size &&
           added < MUSEVITA_CHAT_MAX_MESSAGES) {
        char role[32];
        char next_role[32];
        char text[MUSEVITA_CHAT_MAX_TEXT];
        size_t role_key = 0;
        size_t role_end = 0;
        size_t next_role_key = 0;
        size_t next_role_end = 0;
        size_t member_end = session->request_buffer_size;
        int found_text = 0;
        if (!muse_json_find_string_range(session->request_buffer,
                                         session->request_buffer_size, cursor,
                                         session->request_buffer_size, "role", role,
                                         sizeof(role), &role_key, &role_end)) break;
        if (muse_json_find_string_range(session->request_buffer,
                                        session->request_buffer_size, role_end,
                                        session->request_buffer_size, "role", next_role,
                                        sizeof(next_role), &next_role_key, &next_role_end)) {
            member_end = next_role_key;
        }
        if (history_role_is_supported(role)) {
            found_text = muse_json_find_string_range(session->request_buffer,
                                                     session->request_buffer_size,
                                                     role_end, member_end,
                                                     "display_text", text, sizeof(text),
                                                     NULL, NULL) ||
                         ((strcmp(role, "assistant") == 0 || strcmp(role, "agent") == 0 ||
                           strcmp(role, "muse") == 0) &&
                          muse_json_find_assistant_text(session->request_buffer + role_end,
                                                        member_end - role_end,
                                                        text, sizeof(text))) ||
                         muse_json_find_string_range(session->request_buffer,
                                                     session->request_buffer_size,
                                                     role_end, member_end,
                                                     "content", text, sizeof(text),
                                                     NULL, NULL) ||
                         muse_json_find_string_range(session->request_buffer,
                                                     session->request_buffer_size,
                                                     role_end, member_end,
                                                     "transcript", text, sizeof(text),
                                                     NULL, NULL) ||
                         muse_json_find_string_range(session->request_buffer,
                                                     session->request_buffer_size,
                                                     role_end, member_end,
                                                     "text", text, sizeof(text),
                                                     NULL, NULL);
            if (found_text && text[0] != '\0') {
                muse_vita_chat_add_message(session->chat, history_role(role), text);
                added++;
            }
        }
        cursor = next_role_key != 0 ? next_role_key : session->request_buffer_size;
    }
}

static int start_history_request(MuseVitaSession *session) {
    char request_id[40];
    MuseLinkHeader headers[3];
    size_t encoded_size = 0;
    int64_t stream_id;
    if (session == NULL || session->request_active || !session->subscribed) return -1;
    make_request_id(request_id);
    headers[0] = (MuseLinkHeader){"Accept", "application/json"};
    headers[1] = (MuseLinkHeader){"x-request-id", request_id};
    headers[2] = (MuseLinkHeader){"x-app-id", "musegadget"};
    session->history_requested = true;
    stream_id = session->next_stream_id++;
    begin_request(session, stream_id);
    session->request_is_history = true;
    if (muse_link_encode_http_request(stream_id, "GET", "/chat/history?limit=1",
                                      headers, sizeof(headers) / sizeof(headers[0]),
                                      NULL, 0, 1, session->frame_buffer,
                                      sizeof(session->frame_buffer), &encoded_size) != MUSE_LINK_WIRE_OK ||
        send_link_payload(session, session->frame_buffer, encoded_size) != 0) {
        session->request_active = false;
        session->request_done = false;
        session->request_is_history = false;
        return -1;
    }
    return 0;
}

static void finish_history_request(MuseVitaSession *session) {
    if (session == NULL || !session->request_active || !session->request_is_history ||
        !session->request_done) return;
    if (!session->request_failed && session->request_status >= 200 &&
        session->request_status < 300 && session->chat != NULL) {
        consume_history_response(session);
    }
    session->request_active = false;
    session->request_done = false;
    session->request_failed = false;
    session->request_is_history = false;
    session->request_buffer_size = 0;
}

static int process_service_response(MuseVitaSession *session, const uint8_t *payload, size_t payload_size) {
    MuseLinkIncoming incoming;
    if (muse_link_decode_service_response(payload, payload_size, &incoming) != MUSE_LINK_WIRE_OK) return -1;
    if (incoming.kind == MUSE_LINK_INCOMING_RESPONSE) {
        if (incoming.stream_id == session->control_stream_id &&
            (incoming.status < 200 || incoming.status >= 300)) {
            return -1;
        } else if (incoming.stream_id == session->control_stream_id && incoming.end_body) {
            return -1;
        } else if (incoming.stream_id == session->subscribe_stream_id && incoming.end_body) {
            return -1;
        } else if (incoming.stream_id == session->subscribe_stream_id && incoming.status >= 200 && incoming.status < 300) {
            session->subscribed = true;
        } else if (incoming.stream_id == session->subscribe_stream_id && incoming.status >= 300) {
            return -1;
        }
        if (incoming.stream_id == session->request_stream_id && session->request_active) {
            session->request_status = incoming.status;
            if (incoming.body_size != 0) {
                consume_request_bytes(session, incoming.body, incoming.body_size, 0);
            }
            if (incoming.end_body) {
                finish_request(session, incoming.status < 200 || incoming.status >= 300);
            }
        }
    }
    if (incoming.body_size != 0) {
        if (incoming.stream_id == session->control_stream_id) {
            if (muse_control_decoder_feed(&session->control_decoder, incoming.body, incoming.body_size,
                                          control_message, session) < 0) return -1;
        } else if (incoming.stream_id == session->subscribe_stream_id) {
            if (consume_chat_bytes(session, incoming.body, incoming.body_size) != 0) return -1;
        } else if (incoming.stream_id == session->request_stream_id &&
                   session->request_active && incoming.kind == MUSE_LINK_INCOMING_BODY_CHUNK) {
            consume_request_bytes(session, incoming.body, incoming.body_size, incoming.end_body);
        }
    }
    if (incoming.kind == MUSE_LINK_INCOMING_RESET && incoming.stream_id == session->request_stream_id &&
        session->request_active) finish_request(session, 1);
    if (incoming.kind == MUSE_LINK_INCOMING_RESET &&
        (incoming.stream_id == session->control_stream_id || incoming.stream_id == session->subscribe_stream_id)) return -1;
    if (incoming.kind == MUSE_LINK_INCOMING_BODY_CHUNK && incoming.end_body &&
        (incoming.stream_id == session->control_stream_id || incoming.stream_id == session->subscribe_stream_id)) return -1;
    return 0;
}

static int process_websocket_message(MuseVitaSession *session, const uint8_t *message, size_t message_size) {
    MuseLinkNoiseFrame frame;
    size_t clear_size = 0;
    if (muse_noise_xx_decrypt(&session->noise, message, message_size,
                              session->frame_buffer, sizeof(session->frame_buffer), &clear_size) != MUSE_NOISE_OK) return -1;
    if (muse_link_decode_noise_frame(session->frame_buffer, clear_size, &frame) != MUSE_LINK_WIRE_OK) return -1;
    if (frame.total_chunks == 0 || frame.total_chunks > MUSE_LINK_MAX_NOISE_CHUNKS ||
        frame.payload_size > MUSE_LINK_MAX_NOISE_CHUNK_BYTES) return -1;
    if (frame.total_chunks == 1) return process_service_response(session, frame.payload, frame.payload_size);
    if (frame.chunk_id != session->assembly_chunk_id || frame.chunk_index != session->assembly_count ||
        frame.total_chunks != session->assembly_total || frame.payload_size > sizeof(session->assembly_buffer) - session->assembly_size) {
        if (frame.chunk_index != 0) return -1;
        session->assembly_chunk_id = frame.chunk_id;
        session->assembly_total = frame.total_chunks;
        session->assembly_count = 0;
        session->assembly_size = 0;
    }
    memcpy(session->assembly_buffer + session->assembly_size, frame.payload, frame.payload_size);
    session->assembly_size += frame.payload_size;
    session->assembly_count++;
    if (session->assembly_count == session->assembly_total) {
        int result = process_service_response(session, session->assembly_buffer, session->assembly_size);
        session->assembly_count = 0;
        session->assembly_size = 0;
        return result;
    }
    return 0;
}

int muse_vita_session_init(MuseVitaSession *session) {
    if (session == NULL) return -1;
    memset(session, 0, sizeof(*session));
    session->state = MUSEVITA_SESSION_NO_PAIRING;
    session->next_stream_id = 1;
    session->next_chunk_id = 1;
    muse_control_decoder_init(&session->control_decoder);
    if (ensure_identity(session) != 0) return -1;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return -1;
    session->curl_initialized = true;
    return 0;
}

int muse_vita_session_connect(MuseVitaSession *session, MuseVitaChatState *chat) {
    char url[768];
    size_t url_size;
    size_t message_size = 0;
    uint8_t handshake[128];
    int connected;
    if (session == NULL || chat == NULL ||
        (session->state != MUSEVITA_SESSION_READY && session->state != MUSEVITA_SESSION_ERROR)) return -1;
    session->chat = chat;
    /* Preserve the in-memory transcript across reconnects. On a fresh app
       start, request a small recent page after the live subscription opens. */
    session->history_requested = chat->message_count != 0;
    session->state = MUSEVITA_SESSION_CONNECTING;
    {
        int fetch_result = fetch_vm(session);
        if (fetch_result == -2) {
            int refresh_result = refresh_credentials(session);
            if (refresh_result == 0) {
                fetch_result = fetch_vm(session);
            } else if (refresh_result == -2) {
                forget_pairing(session);
                return -1;
            }
        }
        if (fetch_result != 0) {
            set_transport_error(session);
            return -1;
        }
    }
    url_size = (size_t)snprintf(url, sizeof(url), "wss://%s/v1/noise?vm_id=", session->noise_host);
    if (url_size >= sizeof(url) || append_url_component(url, sizeof(url), &url_size, session->vm_id) != 0) goto fail;
    session->curl = curl_easy_init();
    if (session->curl == NULL) goto fail;
    {
        char authorization[2304];
        snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s", session->vm_auth_token);
        session->headers = curl_slist_append(session->headers, authorization);
    }
    curl_easy_setopt(session->curl, CURLOPT_URL, url);
    curl_easy_setopt(session->curl, CURLOPT_HTTPHEADER, session->headers);
    curl_easy_setopt(session->curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(session->curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(session->curl, CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(session->curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(session->curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (curl_easy_perform(session->curl) != CURLE_OK) goto fail;
    if (muse_noise_xx_init(&session->noise) != MUSE_NOISE_OK) goto fail;
    session->noise_ready = true;
    if (muse_noise_xx_write_message1(&session->noise, handshake) != MUSE_NOISE_OK ||
        websocket_send(session->curl, handshake, MUSE_NOISE_KEY_BYTES) != 0) goto fail;
    connected = websocket_receive(session, 1, &message_size);
    if (connected != 1 || muse_noise_xx_read_message2(&session->noise, session->ws_message, message_size) != MUSE_NOISE_OK) goto fail;
    if (muse_noise_xx_write_message3(&session->noise, handshake, sizeof(handshake), &message_size) != MUSE_NOISE_OK ||
        websocket_send(session->curl, handshake, message_size) != 0) goto fail;
    session->control_stream_id = session->next_stream_id++;
    if (muse_link_encode_open_stream_request(session->control_stream_id, session->frame_buffer,
                                              sizeof(session->frame_buffer), &message_size) != MUSE_LINK_WIRE_OK ||
        send_link_payload(session, session->frame_buffer, message_size) != 0 || send_register(session) != 0) goto fail;
    {
        const uint64_t registration_deadline =
            (uint64_t)sceKernelGetSystemTimeWide() + HANDSHAKE_TIMEOUT_US;
        while (!session->registered &&
               (uint64_t)sceKernelGetSystemTimeWide() < registration_deadline) {
            int result = muse_vita_session_poll(session);
            if (result < 0) goto fail;
            sceKernelDelayThread(10000);
        }
    }
    if (!session->registered || send_subscribe(session) != 0) goto fail;
    session->state = MUSEVITA_SESSION_CONNECTED;
    session->last_ping_us = (uint64_t)sceKernelGetSystemTimeWide();
    chat->transport_connected = true;
    return 0;
fail:
    set_transport_error(session);
    return -1;
}

int muse_vita_session_poll(MuseVitaSession *session) {
    size_t message_size = 0;
    int result;
    if (session == NULL || session->curl == NULL || !session->noise_ready) return -1;
    if (session->unpaired) {
        close_transport(session);
        session->unpaired = false;
        session->state = MUSEVITA_SESSION_NO_PAIRING;
        return 0;
    }
    if ((uint64_t)sceKernelGetSystemTimeWide() - session->last_ping_us >= PING_INTERVAL_US) {
        uint8_t ping = 0;
        size_t sent = 0;
        if (curl_ws_send(session->curl, &ping, 0, &sent, 0, CURLWS_PING) != CURLE_OK) {
            set_transport_error(session);
            return -1;
        }
        session->last_ping_us = (uint64_t)sceKernelGetSystemTimeWide();
    }
    for (unsigned i = 0; i < 8; i++) {
        result = websocket_receive(session, 0, &message_size);
        if (result < 0) {
            set_transport_error(session);
            return -1;
        }
        if (result == 0) break;
        if (process_websocket_message(session, session->ws_message, message_size) != 0) {
            set_transport_error(session);
            return -1;
        }
    }
    finish_history_request(session);
    if (session->subscribed && !session->history_requested && !session->request_active) {
        (void)start_history_request(session);
    }
    return 0;
}

int muse_vita_session_send_chat(MuseVitaSession *session, const char *message) {
    char body[4096];
    size_t body_size = 0;
    size_t encoded_size = 0;
    char request_id[64];
    MuseLinkHeader headers[4];
    int64_t stream_id;
    if (session == NULL || message == NULL || session->state != MUSEVITA_SESSION_CONNECTED ||
        !session->subscribed || session->request_active) return -1;
    if (muse_link_build_chat_request(message, session->node_id, body, sizeof(body), &body_size) != MUSE_LINK_WIRE_OK) return -1;
    make_request_id(request_id);
    headers[0] = (MuseLinkHeader){"Content-Type", "application/json"};
    headers[1] = (MuseLinkHeader){"x-request-id", request_id};
    headers[2] = (MuseLinkHeader){"x-app-id", "musegadget"};
    headers[3] = (MuseLinkHeader){"Accept", "application/json"};
    stream_id = session->next_stream_id++;
    begin_request(session, stream_id);
    session->awaiting_chat_ack = true;
    session->deferred_chat_size = 0;
    session->deferred_chat_overflow = false;
    session->last_user_id[0] = '\0';
    if (muse_link_encode_http_request(stream_id, "POST", "/chat/stream", headers, 4,
                                      (const uint8_t *)body, body_size, 1,
                                      session->frame_buffer, sizeof(session->frame_buffer), &encoded_size) != MUSE_LINK_WIRE_OK ||
        send_link_payload(session, session->frame_buffer, encoded_size) != 0) {
        session->request_active = false;
        clear_deferred_chat(session);
        return -1;
    }
    return wait_for_request(session);
}

int muse_vita_session_send_media(MuseVitaSession *session, const char *message,
                                 const char *item_type, const char *mime_type,
                                 const char *filename, const uint8_t *data,
                                 size_t data_size) {
    char request_id[40];
    MuseLinkHeader headers[4];
    size_t body_size = 0;
    size_t encoded_size = 0;
    int64_t stream_id;
    if (session == NULL || message == NULL || item_type == NULL || mime_type == NULL ||
        filename == NULL || (data == NULL && data_size != 0) ||
        data_size == 0 || session->state != MUSEVITA_SESSION_CONNECTED ||
        !session->subscribed || session->request_active) return -1;
    if (muse_link_build_chat_media_request(message, session->node_id, item_type, mime_type,
                                           filename, data, data_size, session->media_buffer,
                                           sizeof(session->media_buffer), &body_size) != MUSE_LINK_WIRE_OK) return -1;
    make_request_id(request_id);
    headers[0] = (MuseLinkHeader){"Content-Type", "application/json"};
    headers[1] = (MuseLinkHeader){"x-request-id", request_id};
    headers[2] = (MuseLinkHeader){"x-app-id", "musegadget"};
    headers[3] = (MuseLinkHeader){"Accept", "application/json"};
    stream_id = session->next_stream_id++;
    begin_request(session, stream_id);
    session->awaiting_chat_ack = true;
    session->deferred_chat_size = 0;
    session->deferred_chat_overflow = false;
    session->last_user_id[0] = '\0';
    if (muse_link_encode_http_request(stream_id, "POST", "/chat/stream", headers, 4,
                                      NULL, 0, 0, session->frame_buffer,
                                      sizeof(session->frame_buffer), &encoded_size) != MUSE_LINK_WIRE_OK ||
        send_link_payload(session, session->frame_buffer, encoded_size) != 0) {
        session->request_active = false;
        clear_deferred_chat(session);
        return -1;
    }
    for (size_t offset = 0; offset < body_size;) {
        size_t chunk_size = body_size - offset;
        int end_body;
        if (chunk_size > 16u * 1024u) chunk_size = 16u * 1024u;
        end_body = offset + chunk_size == body_size;
        if (muse_link_encode_body_chunk(stream_id, (const uint8_t *)session->media_buffer + offset,
                                        chunk_size, end_body, session->frame_buffer,
                                        sizeof(session->frame_buffer), &encoded_size) != MUSE_LINK_WIRE_OK ||
            send_link_payload(session, session->frame_buffer, encoded_size) != 0) {
            session->request_active = false;
            clear_deferred_chat(session);
            return -1;
        }
        offset += chunk_size;
    }
    return wait_for_request(session);
}

void muse_vita_session_term(MuseVitaSession *session) {
    if (session == NULL) return;
    close_transport(session);
    if (session->curl_initialized) curl_global_cleanup();
    memset(session, 0, sizeof(*session));
}

bool muse_vita_session_is_connected(const MuseVitaSession *session) {
    return session != NULL && session->state == MUSEVITA_SESSION_CONNECTED;
}

bool muse_vita_session_should_connect(const MuseVitaSession *session) {
    return session != NULL && session->state == MUSEVITA_SESSION_READY;
}

bool muse_vita_session_should_reconnect(const MuseVitaSession *session) {
    return session != NULL && session->state == MUSEVITA_SESSION_ERROR &&
           session->access_token[0] != '\0' &&
           (uint64_t)sceKernelGetSystemTimeWide() >= session->retry_after_us;
}

const char *muse_vita_session_label(const MuseVitaSession *session) {
    if (session == NULL) return "UNKNOWN";
    switch (session->state) {
        case MUSEVITA_SESSION_NO_PAIRING: return "NOT PAIRED";
        case MUSEVITA_SESSION_READY: return "PAIRING READY";
        case MUSEVITA_SESSION_CONNECTING: return "CONNECTING";
        case MUSEVITA_SESSION_CONNECTED: return "CONNECTED";
        default: return "ERROR";
    }
}
