#include "pairing_transfer.h"

#include <stdio.h>
#include <string.h>

#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/ecdh.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/io/fcntl.h>

#include "json_extract.h"

#define TRANSFER_PORT 8768
#define TRANSFER_BODY_MAX 65536u
#define TRANSFER_HEADER_MAX 8192u
#define TRANSFER_SESSION_BYTES 8u
#define TRANSFER_PUBLIC_KEY_BYTES 65u
#define TRANSFER_AES_TAG_BYTES 16u
#define TRANSFER_MAX_PLAINTEXT (MUSEVITA_PAIRING_IDENTITY_MAX + MUSEVITA_PAIRING_JSON_MAX + 512u)
#define TRANSFER_HTML_PATH "app0:/sce_sys/pairing.html"
#define TRANSFER_CRYPTO_PATH "app0:/sce_sys/pairing-crypto.js"
#define TRANSFER_ASSET_MAX 131072u
#define TRANSFER_TTL_US (10ULL * 60ULL * 1000000ULL)

static char transfer_request_buffer[TRANSFER_HEADER_MAX + TRANSFER_BODY_MAX];
static char transfer_file_buffer[TRANSFER_ASSET_MAX];
static char transfer_ct_b64[TRANSFER_BODY_MAX];
static uint8_t transfer_ciphertext[TRANSFER_BODY_MAX];
static uint8_t transfer_plaintext[TRANSFER_MAX_PLAINTEXT];
static uint8_t transfer_shared[32];
static uint8_t transfer_key[32];

static const char session_alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const char base64url_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static void set_status(MuseVitaPairingTransfer *transfer, const char *value) {
    strncpy(transfer->status, value, sizeof(transfer->status) - 1);
    transfer->status[sizeof(transfer->status) - 1] = '\0';
}

static void release_crypto(MuseVitaPairingTransfer *transfer) {
    mbedtls_ctr_drbg_free(&transfer->drbg);
    mbedtls_entropy_free(&transfer->entropy);
    mbedtls_mpi_free(&transfer->private_key);
    mbedtls_ecp_group_free(&transfer->group);
}

static int session_live(MuseVitaPairingTransfer *transfer) {
    if (transfer->expires_us != 0 && (uint64_t)sceKernelGetSystemTimeWide() >= transfer->expires_us) {
        transfer->ready = false;
        set_status(transfer, "QR expired; reopen setup to regenerate it");
        return 0;
    }
    return 1;
}

static size_t base64_encode(const uint8_t *input, size_t input_size, char *output,
                            size_t output_capacity) {
    size_t at = 0;
    size_t i = 0;
    while (i < input_size) {
        uint32_t value = (uint32_t)input[i++] << 16;
        size_t count = 1;
        if (i < input_size) { value |= (uint32_t)input[i++] << 8; count++; }
        if (i < input_size) { value |= input[i++]; count++; }
        if (at + 4 >= output_capacity) return 0;
        output[at++] = base64url_alphabet[(value >> 18) & 63];
        output[at++] = base64url_alphabet[(value >> 12) & 63];
        if (count > 1) output[at++] = base64url_alphabet[(value >> 6) & 63];
        if (count > 2) output[at++] = base64url_alphabet[value & 63];
    }
    output[at] = '\0';
    return at;
}

static int base64_value(char c) {
    const char *found = strchr(base64url_alphabet, c);
    return found == NULL ? -1 : (int)(found - base64url_alphabet);
}

static size_t base64_decode(const char *input, uint8_t *output, size_t capacity) {
    size_t input_size = strlen(input);
    size_t at = 0;
    uint32_t value = 0;
    unsigned int bits = 0;
    for (size_t i = 0; i < input_size; i++) {
        int part = base64_value(input[i]);
        if (part < 0) return 0;
        value = (value << 6) | (uint32_t)part;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (at >= capacity) return 0;
            output[at++] = (uint8_t)(value >> bits);
            value &= (1u << bits) - 1u;
        }
    }
    return at;
}

static int receive_all(int socket, char *buffer, size_t capacity, size_t *size) {
    while (*size + 1 < capacity) {
        int received = sceNetRecv(socket, buffer + *size, (unsigned int)(capacity - *size - 1), 0);
        if (received <= 0) return -1;
        *size += (size_t)received;
        buffer[*size] = '\0';
        if (strstr(buffer, "\r\n\r\n") != NULL) return 0;
    }
    return -1;
}

static int send_bytes(int socket, const void *data, size_t size) {
    const char *bytes = data;
    size_t offset = 0;
    while (offset < size) {
        int sent = sceNetSend(socket, bytes + offset, (unsigned int)(size - offset), 0);
        if (sent <= 0) return -1;
        offset += (size_t)sent;
    }
    return 0;
}

static const char *http_reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "Error";
    }
}

static int send_redirect(int socket, const char *location) {
    char header[512];
    int header_size = snprintf(header, sizeof(header),
                               "HTTP/1.1 302 Found\r\nLocation: %s\r\n"
                               "Content-Length: 0\r\nCache-Control: no-store\r\n"
                               "Connection: close\r\n\r\n", location);
    if (header_size < 0 || (size_t)header_size >= sizeof(header)) return -1;
    return send_bytes(socket, header, (size_t)header_size);
}

static int send_text(int socket, int status, const char *content_type, const char *body) {
    char header[256];
    size_t body_size = strlen(body);
    int header_size = snprintf(header, sizeof(header),
                               "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                               "Content-Length: %lu\r\nCache-Control: no-store\r\n"
                               "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                               status, http_reason(status), content_type, (unsigned long)body_size);
    if (header_size < 0 || (size_t)header_size >= sizeof(header)) return -1;
    if (send_bytes(socket, header, (size_t)header_size) != 0) return -1;
    return send_bytes(socket, body, body_size);
}

static int send_file(int socket, const char *path, const char *content_type) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    int read_size;
    size_t total = 0;
    char header[256];
    int header_size;
    if (fd < 0) return send_text(socket, 404, "text/plain", "Not found");
    while (total < sizeof(transfer_file_buffer)) {
        read_size = sceIoRead(fd, transfer_file_buffer + total,
                              sizeof(transfer_file_buffer) - total);
        if (read_size <= 0) break;
        total += (size_t)read_size;
    }
    sceIoClose(fd);
    if (total == sizeof(transfer_file_buffer)) return send_text(socket, 500, "text/plain", "Page too large");
    header_size = snprintf(header, sizeof(header),
                           "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
                           "Content-Length: %lu\r\nCache-Control: no-store\r\n"
                           "X-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                           content_type, (unsigned long)total);
    if (header_size < 0 || (size_t)header_size >= sizeof(header)) return -1;
    if (send_bytes(socket, header, (size_t)header_size) != 0) return -1;
    return send_bytes(socket, transfer_file_buffer, total);
}

static int decrypt_bundle(MuseVitaPairingTransfer *transfer, const char *body) {
    char sid[32], epk_b64[128], iv_b64[32];
    uint8_t epk[65], iv[16];
    size_t epk_size, iv_size, ciphertext_size, plaintext_size;
    mbedtls_ecp_point peer;
    mbedtls_mpi shared_mpi;
    mbedtls_gcm_context gcm;
    const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    static const char info[] = "MuseVita pairing transfer v1";
    int result = -1;
    if (!muse_json_find_string(body, strlen(body), "sid", sid, sizeof(sid)) ||
        strcmp(sid, transfer->session_id) != 0 ||
        !muse_json_find_string(body, strlen(body), "epk", epk_b64, sizeof(epk_b64)) ||
        !muse_json_find_string(body, strlen(body), "iv", iv_b64, sizeof(iv_b64)) ||
        !muse_json_find_string(body, strlen(body), "ct", transfer_ct_b64, sizeof(transfer_ct_b64))) return -1;
    epk_size = base64_decode(epk_b64, epk, sizeof(epk));
    iv_size = base64_decode(iv_b64, iv, sizeof(iv));
    ciphertext_size = base64_decode(transfer_ct_b64, transfer_ciphertext, sizeof(transfer_ciphertext));
    if (epk_size != sizeof(epk) || iv_size != 12 || ciphertext_size < TRANSFER_AES_TAG_BYTES ||
        ciphertext_size > sizeof(transfer_plaintext)) return -1;
    mbedtls_ecp_point_init(&peer);
    mbedtls_mpi_init(&shared_mpi);
    if (mbedtls_ecp_point_read_binary(&transfer->group, &peer, epk, sizeof(epk)) != 0 ||
        mbedtls_ecdh_compute_shared(&transfer->group, &shared_mpi, &peer,
                                    &transfer->private_key, mbedtls_ctr_drbg_random,
                                    &transfer->drbg) != 0 ||
        mbedtls_mpi_write_binary(&shared_mpi, transfer_shared, sizeof(transfer_shared)) != 0 ||
        mbedtls_hkdf(sha256, (const unsigned char *)sid, strlen(sid), transfer_shared,
                     sizeof(transfer_shared), (const unsigned char *)info, sizeof(info) - 1,
                     transfer_key, sizeof(transfer_key)) != 0) {
        goto done;
    }
    plaintext_size = ciphertext_size - TRANSFER_AES_TAG_BYTES;
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, transfer_key, 256) == 0 &&
        mbedtls_gcm_auth_decrypt(&gcm, plaintext_size, iv, 12,
                                 (const unsigned char *)sid, strlen(sid),
                                 transfer_ciphertext + plaintext_size, TRANSFER_AES_TAG_BYTES,
                                 transfer_ciphertext, transfer_plaintext) == 0) {
        transfer_plaintext[plaintext_size] = '\0';
        if (muse_json_find_string((const char *)transfer_plaintext, plaintext_size, "identity",
                                  transfer->pending_identity, sizeof(transfer->pending_identity)) &&
            muse_json_find_string((const char *)transfer_plaintext, plaintext_size, "pairing",
                                  transfer->pending_pairing, sizeof(transfer->pending_pairing)) &&
            muse_json_find_string((const char *)transfer_plaintext, plaintext_size, "sdk_token",
                                  transfer->pending_token, sizeof(transfer->pending_token))) {
            transfer->pending_bundle = true;
            transfer->consumed = true;
            set_status(transfer, "Encrypted bundle received; press SELECT");
            result = 0;
        }
    }
    mbedtls_gcm_free(&gcm);
done:
    mbedtls_mpi_free(&shared_mpi);
    mbedtls_ecp_point_free(&peer);
    memset(transfer_ct_b64, 0, sizeof(transfer_ct_b64));
    memset(transfer_ciphertext, 0, sizeof(transfer_ciphertext));
    memset(transfer_shared, 0, sizeof(transfer_shared));
    memset(transfer_key, 0, sizeof(transfer_key));
    memset(transfer_plaintext, 0, sizeof(transfer_plaintext));
    return result;
}

static int handle_request(MuseVitaPairingTransfer *transfer, int socket) {
    size_t request_size = 0;
    const char *headers_end;
    char method[8], path[256];
    unsigned long content_length = 0;
    if (receive_all(socket, transfer_request_buffer, sizeof(transfer_request_buffer), &request_size) != 0) return -1;
    headers_end = strstr(transfer_request_buffer, "\r\n\r\n");
    if (headers_end == NULL || sscanf(transfer_request_buffer, "%7s %255s", method, path) != 2) return -1;
    {
        const char *length = strstr(transfer_request_buffer, "Content-Length:");
        if (length != NULL) (void)sscanf(length, "Content-Length: %lu", &content_length);
    }
    if (content_length > TRANSFER_BODY_MAX) return -1;
    size_t header_size = (size_t)(headers_end - transfer_request_buffer) + 4u;
    while (request_size < header_size + content_length && request_size + 1 < sizeof(transfer_request_buffer)) {
        int received = sceNetRecv(socket, transfer_request_buffer + request_size,
                                  (unsigned int)(sizeof(transfer_request_buffer) - request_size - 1), 0);
        if (received <= 0) return -1;
        request_size += (size_t)received;
        transfer_request_buffer[request_size] = '\0';
    }
    if (request_size < header_size + content_length) return -1;
    if (strncmp(method, "GET", 4) == 0 && strcmp(path, "/") == 0) {
        char location[256];
        snprintf(location, sizeof(location), "/pair?s=%s&k=%s",
                 transfer->session_id, transfer->public_key_b64);
        return session_live(transfer) ? send_redirect(socket, location) :
                                        send_text(socket, 403, "text/plain", "Invalid or expired session");
    }
    if (strncmp(method, "GET", 4) == 0 && strncmp(path, "/pair/config", 12) == 0) {
        char response[256];
        if (!session_live(transfer) || !strstr(path, transfer->session_id))
            return send_text(socket, 403, "text/plain", "Invalid or expired session");
        snprintf(response, sizeof(response), "{\"sid\":\"%s\",\"pub\":\"%s\"}",
                 transfer->session_id, transfer->public_key_b64);
        return send_text(socket, 200, "application/json", response);
    }
    if (strncmp(method, "GET", 4) == 0 && strncmp(path, "/pairing-crypto.js", 18) == 0)
        return send_file(socket, TRANSFER_CRYPTO_PATH, "text/javascript; charset=utf-8");
    if (strncmp(method, "GET", 4) == 0 && strncmp(path, "/pair", 5) == 0)
        return send_file(socket, TRANSFER_HTML_PATH, "text/html; charset=utf-8");
    if (strncmp(method, "POST", 4) == 0 && strncmp(path, "/pair/upload", 12) == 0) {
        const char *body = transfer_request_buffer + header_size;
        if (!session_live(transfer) || transfer->consumed || !strstr(path, transfer->session_id) ||
            decrypt_bundle(transfer, body) != 0)
            return send_text(socket, 400, "text/plain", "Transfer rejected or expired");
        return send_text(socket, 200, "text/plain", "Pairing bundle received");
    }
    return send_text(socket, 404, "text/plain", "Not found");
}

static int transfer_thread(unsigned int args, void *arg) {
    MuseVitaPairingTransfer *transfer;
    SceNetSockaddrIn address;
    if (arg == NULL || args != sizeof(transfer)) return -1;
    transfer = *(MuseVitaPairingTransfer **)arg;
    if (transfer == NULL) return -1;
    transfer->listen_socket = sceNetSocket("MuseVitaPair", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (transfer->listen_socket < 0) {
        transfer->ready = false;
        set_status(transfer, "Phone transfer unavailable");
        return 0;
    }
    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = SCE_NET_AF_INET;
    address.sin_port = sceNetHtons(TRANSFER_PORT);
    address.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
    if (sceNetBind(transfer->listen_socket, (SceNetSockaddr *)&address, sizeof(address)) < 0 ||
        sceNetListen(transfer->listen_socket, 1) < 0) {
        sceNetSocketClose(transfer->listen_socket);
        transfer->listen_socket = -1;
        transfer->ready = false;
        set_status(transfer, "Phone transfer unavailable");
        return 0;
    }
    set_status(transfer, "Scan QR with your phone");
    transfer->listener_bound = true;
    while (!transfer->stop) {
        SceNetSockaddr peer;
        unsigned int peer_size = sizeof(peer);
        int client = sceNetAccept(transfer->listen_socket, &peer, &peer_size);
        if (client < 0) continue;
        (void)handle_request(transfer, client);
        sceNetSocketClose(client);
    }
    sceNetSocketClose(transfer->listen_socket);
    transfer->listen_socket = -1;
    transfer->listener_bound = false;
    return 0;
}

int muse_vita_pairing_transfer_start(MuseVitaPairingTransfer *transfer) {
    SceNetCtlInfo info;
    mbedtls_ecp_point public_point;
    size_t public_size = 0;
    if (transfer == NULL) return -1;
    memset(transfer, 0, sizeof(*transfer));
    transfer->listen_socket = -1;
    transfer->thread = -1;
    transfer->listener_bound = false;
    mbedtls_ecp_group_init(&transfer->group);
    mbedtls_mpi_init(&transfer->private_key);
    mbedtls_entropy_init(&transfer->entropy);
    mbedtls_ctr_drbg_init(&transfer->drbg);
    mbedtls_ecp_point_init(&public_point);
    if (mbedtls_ecp_group_load(&transfer->group, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
        mbedtls_ctr_drbg_seed(&transfer->drbg, mbedtls_entropy_func, &transfer->entropy,
                              (const unsigned char *)"MuseVita pairing transfer", 25) != 0 ||
        mbedtls_ecdh_gen_public(&transfer->group, &transfer->private_key, &public_point,
                                mbedtls_ctr_drbg_random, &transfer->drbg) != 0 ||
        mbedtls_ecp_point_write_binary(&transfer->group, &public_point,
                                       MBEDTLS_ECP_PF_UNCOMPRESSED, &public_size,
                                       transfer->public_key, sizeof(transfer->public_key)) != 0 ||
        public_size != sizeof(transfer->public_key)) {
        mbedtls_ecp_point_free(&public_point);
        release_crypto(transfer);
        return -1;
    }
    mbedtls_ecp_point_free(&public_point);
    uint8_t random[TRANSFER_SESSION_BYTES];
    if (mbedtls_ctr_drbg_random(&transfer->drbg, random, sizeof(random)) != 0) {
        release_crypto(transfer);
        return -1;
    }
    for (size_t i = 0; i < sizeof(random); i++)
        transfer->session_id[i * 2] = session_alphabet[random[i] >> 3];
    for (size_t i = 0; i < sizeof(random); i++)
        transfer->session_id[i * 2 + 1] = session_alphabet[random[i] & 31];
    transfer->session_id[16] = '\0';
    transfer->expires_us = (uint64_t)sceKernelGetSystemTimeWide() + TRANSFER_TTL_US;
    if (base64_encode(transfer->public_key, sizeof(transfer->public_key),
                      transfer->public_key_b64, sizeof(transfer->public_key_b64)) == 0) {
        release_crypto(transfer);
        return -1;
    }
    memset(&info, 0, sizeof(info));
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0) {
        set_status(transfer, "Connect Vita to Wi-Fi for QR setup");
        release_crypto(transfer);
        return -1;
    }
    snprintf(transfer->manual_url, sizeof(transfer->manual_url),
             "http://%s:%d/", info.ip_address, TRANSFER_PORT);
    snprintf(transfer->qr_payload, sizeof(transfer->qr_payload),
             "http://%s:%d/pair?s=%s&k=%s", info.ip_address, TRANSFER_PORT,
             transfer->session_id, transfer->public_key_b64);
    transfer->thread = sceKernelCreateThread("MuseVitaPairing", transfer_thread,
                                             0x10000100, 0x10000, 0, 0, NULL);
    if (transfer->thread < 0 ||
        sceKernelStartThread(transfer->thread, sizeof(transfer), &transfer) < 0) {
        if (transfer->thread >= 0) {
            sceKernelDeleteThread(transfer->thread);
            transfer->thread = -1;
        }
        release_crypto(transfer);
        return -1;
    }
    for (unsigned int attempt = 0; attempt < 100; attempt++) {
        if (transfer->listener_bound) {
            transfer->ready = true;
            return 0;
        }
        sceKernelDelayThread(10000);
    }
    muse_vita_pairing_transfer_stop(transfer);
    return -1;
}

void muse_vita_pairing_transfer_stop(MuseVitaPairingTransfer *transfer) {
    if (transfer == NULL) return;
    transfer->stop = true;
    if (transfer->listen_socket >= 0) (void)sceNetSocketAbort(transfer->listen_socket, 0);
    if (transfer->thread >= 0) {
        int status;
        (void)sceKernelWaitThreadEnd(transfer->thread, &status, NULL);
        sceKernelDeleteThread(transfer->thread);
    }
    release_crypto(transfer);
    memset(transfer, 0, sizeof(*transfer));
    transfer->listen_socket = -1;
}

const char *muse_vita_pairing_transfer_qr(const MuseVitaPairingTransfer *transfer) {
    return transfer == NULL ? "" : transfer->qr_payload;
}

const char *muse_vita_pairing_transfer_manual_url(const MuseVitaPairingTransfer *transfer) {
    return transfer == NULL ? "" : transfer->manual_url;
}

const char *muse_vita_pairing_transfer_status(const MuseVitaPairingTransfer *transfer) {
    return transfer == NULL ? "" : transfer->status;
}

int muse_vita_pairing_transfer_take(MuseVitaPairingTransfer *transfer,
                                    char *identity, size_t identity_capacity,
                                    char *pairing, size_t pairing_capacity,
                                    char *token, size_t token_capacity) {
    if (transfer == NULL || !transfer->pending_bundle || identity == NULL || pairing == NULL || token == NULL ||
        strlen(transfer->pending_identity) + 1 > identity_capacity ||
        strlen(transfer->pending_pairing) + 1 > pairing_capacity ||
        strlen(transfer->pending_token) + 1 > token_capacity) return -1;
    strcpy(identity, transfer->pending_identity);
    strcpy(pairing, transfer->pending_pairing);
    strcpy(token, transfer->pending_token);
    memset(transfer->pending_identity, 0, sizeof(transfer->pending_identity));
    memset(transfer->pending_pairing, 0, sizeof(transfer->pending_pairing));
    memset(transfer->pending_token, 0, sizeof(transfer->pending_token));
    transfer->pending_bundle = false;
    return 0;
}
