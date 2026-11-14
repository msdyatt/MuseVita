#include "noise_xx.h"

#include <string.h>

#include <mbedtls/gcm.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#define NOISE_PROTOCOL_NAME "Noise_XX_25519_AESGCM_SHA256"
#define NOISE_PHASE_CREATED 0u
#define NOISE_PHASE_INITIALIZED 1u
#define NOISE_PHASE_MESSAGE1_SENT 2u
#define NOISE_PHASE_MESSAGE2_READ 3u
#define NOISE_PHASE_MESSAGE3_SENT 4u
#define NOISE_PHASE_SPLIT 5u
#define NOISE_MIN_MESSAGE2 (32u + 48u + 16u)
#define NOISE_MIN_MESSAGE3 (48u + 16u)
#define NOISE_MAX_SAFE_NONCE ((UINT64_C(1) << 53) - 1)

static const uint8_t low_order_points[][MUSE_NOISE_KEY_BYTES] = {
    {0},
    {1},
    {0xe0, 0xeb, 0x7a, 0x7c, 0x3b, 0x41, 0xb8, 0xae, 0x16, 0x56, 0xe3, 0xfa,
     0xf1, 0x9f, 0xc4, 0x6a, 0xda, 0x09, 0x8d, 0xeb, 0x9c, 0x32, 0xb1, 0xfd,
     0x86, 0x62, 0x05, 0x16, 0x5f, 0x49, 0xb8, 0x00},
    {0x5f, 0x9c, 0x95, 0xbc, 0xa3, 0x50, 0x8c, 0x24, 0xb1, 0xd0, 0xb1, 0x55,
     0x9c, 0x83, 0xef, 0x5b, 0x04, 0x44, 0x5c, 0xc4, 0x58, 0x1c, 0x8e, 0x86,
     0xd8, 0x22, 0x4e, 0xdd, 0xd0, 0x9f, 0x11, 0x57},
    {0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
    {0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
    {0xee, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
};

static int is_low_order_point(const uint8_t point[MUSE_NOISE_KEY_BYTES]) {
    for (size_t i = 0; i < sizeof(low_order_points) / sizeof(low_order_points[0]); i++) {
        if (memcmp(point, low_order_points[i], MUSE_NOISE_KEY_BYTES) == 0) return 1;
    }
    return 0;
}

static int next_nonce(uint64_t *nonce, uint64_t *current) {
    if (*nonce >= NOISE_MAX_SAFE_NONCE) return -1;
    *current = (*nonce)++;
    return 0;
}

static const mbedtls_md_info_t *sha256_info(void) {
    return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
}

static int hmac_sha256(const uint8_t *key, size_t key_size,
                       const uint8_t *data, size_t data_size,
                       uint8_t output[MUSE_NOISE_KEY_BYTES]) {
    return mbedtls_md_hmac(sha256_info(), key, key_size, data, data_size, output);
}

static int hash_concat(const uint8_t *left, size_t left_size,
                       const uint8_t *right, size_t right_size,
                       uint8_t output[MUSE_NOISE_KEY_BYTES]) {
    mbedtls_sha256_context context;
    int result;
    mbedtls_sha256_init(&context);
    result = mbedtls_sha256_starts(&context, 0);
    if (result == 0) result = mbedtls_sha256_update(&context, left, left_size);
    if (result == 0) result = mbedtls_sha256_update(&context, right, right_size);
    if (result == 0) result = mbedtls_sha256_finish(&context, output);
    mbedtls_sha256_free(&context);
    return result;
}

static int hkdf2(const uint8_t chaining_key[MUSE_NOISE_KEY_BYTES],
                 const uint8_t *input, size_t input_size,
                 uint8_t output1[MUSE_NOISE_KEY_BYTES],
                 uint8_t output2[MUSE_NOISE_KEY_BYTES]) {
    uint8_t temp[MUSE_NOISE_KEY_BYTES];
    static const uint8_t one[] = {1};
    uint8_t second_input[MUSE_NOISE_KEY_BYTES + 1];
    int result = hmac_sha256(chaining_key, MUSE_NOISE_KEY_BYTES, input, input_size, temp);
    if (result != 0) return result;
    result = hmac_sha256(temp, sizeof(temp), one, sizeof(one), output1);
    if (result != 0) return result;
    memcpy(second_input, output1, MUSE_NOISE_KEY_BYTES);
    second_input[MUSE_NOISE_KEY_BYTES] = 2;
    return hmac_sha256(temp, sizeof(temp), second_input, sizeof(second_input), output2);
}

static int mix_hash(MuseNoiseXX *noise, const uint8_t *data, size_t data_size) {
    uint8_t next[MUSE_NOISE_KEY_BYTES];
    int result = hash_concat(noise->handshake_hash, sizeof(noise->handshake_hash),
                             data, data_size, next);
    if (result == 0) memcpy(noise->handshake_hash, next, sizeof(next));
    return result;
}

static int mix_key(MuseNoiseXX *noise, const uint8_t input[MUSE_NOISE_KEY_BYTES]) {
    uint8_t next[MUSE_NOISE_KEY_BYTES];
    int result = hkdf2(noise->chaining_key, input, MUSE_NOISE_KEY_BYTES,
                       next, noise->cipher_key);
    if (result == 0) {
        memcpy(noise->chaining_key, next, sizeof(next));
        noise->cipher_nonce = 0;
        noise->cipher_keyed = 1;
    }
    return result;
}

static void build_nonce(uint64_t nonce, uint8_t output[12]) {
    memset(output, 0, 4);
    output[4] = (uint8_t)(nonce >> 56);
    output[5] = (uint8_t)(nonce >> 48);
    output[6] = (uint8_t)(nonce >> 40);
    output[7] = (uint8_t)(nonce >> 32);
    output[8] = (uint8_t)(nonce >> 24);
    output[9] = (uint8_t)(nonce >> 16);
    output[10] = (uint8_t)(nonce >> 8);
    output[11] = (uint8_t)nonce;
}

static int aead_encrypt(const uint8_t key[MUSE_NOISE_KEY_BYTES], uint64_t nonce,
                        const uint8_t *ad, size_t ad_size,
                        const uint8_t *plaintext, size_t plaintext_size,
                        uint8_t *ciphertext) {
    uint8_t iv[12];
    uint8_t tag[MUSE_NOISE_TAG_BYTES];
    mbedtls_gcm_context context;
    int result;
    build_nonce(nonce, iv);
    mbedtls_gcm_init(&context);
    result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (result == 0) {
        result = mbedtls_gcm_crypt_and_tag(&context, MBEDTLS_GCM_ENCRYPT,
                                           plaintext_size, iv, sizeof(iv), ad, ad_size,
                                           plaintext, ciphertext, sizeof(tag), tag);
    }
    if (result == 0) memcpy(ciphertext + plaintext_size, tag, sizeof(tag));
    mbedtls_gcm_free(&context);
    return result;
}

static int aead_decrypt(const uint8_t key[MUSE_NOISE_KEY_BYTES], uint64_t nonce,
                        const uint8_t *ad, size_t ad_size,
                        const uint8_t *ciphertext, size_t ciphertext_size,
                        uint8_t *plaintext) {
    uint8_t iv[12];
    mbedtls_gcm_context context;
    int result;
    if (ciphertext_size < MUSE_NOISE_TAG_BYTES) return -1;
    build_nonce(nonce, iv);
    mbedtls_gcm_init(&context);
    result = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (result == 0) {
        result = mbedtls_gcm_auth_decrypt(&context,
                                          ciphertext_size - MUSE_NOISE_TAG_BYTES,
                                          iv, sizeof(iv), ad, ad_size,
                                          ciphertext + ciphertext_size - MUSE_NOISE_TAG_BYTES,
                                          MUSE_NOISE_TAG_BYTES, ciphertext, plaintext);
    }
    mbedtls_gcm_free(&context);
    return result;
}

static int encrypt_and_hash(MuseNoiseXX *noise, const uint8_t *plaintext,
                            size_t plaintext_size, uint8_t *output) {
    size_t output_size = plaintext_size;
    int result;
    if (noise->cipher_keyed) {
        uint64_t nonce;
        if (next_nonce(&noise->cipher_nonce, &nonce) != 0) return -1;
        result = aead_encrypt(noise->cipher_key, nonce,
                              noise->handshake_hash, sizeof(noise->handshake_hash),
                              plaintext, plaintext_size, output);
        output_size += MUSE_NOISE_TAG_BYTES;
    } else {
        result = (plaintext_size == 0) ? 0 : -1;
        if (result == 0 && plaintext_size != 0) memcpy(output, plaintext, plaintext_size);
    }
    if (result == 0) result = mix_hash(noise, output, output_size);
    return result;
}

static int decrypt_and_hash(MuseNoiseXX *noise, const uint8_t *ciphertext,
                            size_t ciphertext_size, uint8_t *plaintext,
                            size_t plaintext_capacity, size_t *plaintext_size) {
    size_t clear_size = ciphertext_size;
    int result;
    if (noise->cipher_keyed) {
        if (ciphertext_size < MUSE_NOISE_TAG_BYTES) return -1;
        clear_size -= MUSE_NOISE_TAG_BYTES;
        if (clear_size > plaintext_capacity) return -1;
        uint64_t nonce;
        if (next_nonce(&noise->cipher_nonce, &nonce) != 0) return -1;
        result = aead_decrypt(noise->cipher_key, nonce,
                              noise->handshake_hash, sizeof(noise->handshake_hash),
                              ciphertext, ciphertext_size, plaintext);
    } else {
        if (ciphertext_size > plaintext_capacity) return -1;
        if (ciphertext_size != 0) memcpy(plaintext, ciphertext, ciphertext_size);
        result = 0;
    }
    if (result == 0) result = mix_hash(noise, ciphertext, ciphertext_size);
    if (result == 0 && plaintext_size != NULL) *plaintext_size = clear_size;
    return result;
}

static int generate_key_pair(MuseNoiseXX *noise, mbedtls_mpi *private_key,
                             uint8_t public_key[MUSE_NOISE_KEY_BYTES]) {
    mbedtls_ecp_point point;
    size_t public_size = 0;
    int result;
    mbedtls_ecp_point_init(&point);
    result = mbedtls_ecdh_gen_public(&noise->group, private_key, &point,
                                     mbedtls_ctr_drbg_random, &noise->drbg);
    if (result == 0) result = mbedtls_ecp_point_write_binary(
        &noise->group, &point, MBEDTLS_ECP_PF_UNCOMPRESSED, &public_size,
        public_key, MUSE_NOISE_KEY_BYTES);
    if (result == 0 && public_size != MUSE_NOISE_KEY_BYTES) result = -1;
    mbedtls_ecp_point_free(&point);
    return result;
}

static int dh(MuseNoiseXX *noise, const mbedtls_mpi *private_key,
              const uint8_t peer_public[MUSE_NOISE_KEY_BYTES],
              uint8_t output[MUSE_NOISE_KEY_BYTES]) {
    mbedtls_ecp_point point;
    mbedtls_mpi shared;
    int result;
    if (is_low_order_point(peer_public)) return -1;
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&shared);
    result = mbedtls_ecp_point_read_binary(&noise->group, &point,
                                           peer_public, MUSE_NOISE_KEY_BYTES);
    if (result == 0) result = mbedtls_ecdh_compute_shared(&noise->group, &shared, &point,
                                                           private_key, NULL, NULL);
    if (result == 0) result = mbedtls_mpi_write_binary_le(&shared, output, MUSE_NOISE_KEY_BYTES);
    mbedtls_mpi_free(&shared);
    mbedtls_ecp_point_free(&point);
    if (result == 0) {
        uint8_t zero[MUSE_NOISE_KEY_BYTES] = {0};
        if (memcmp(output, zero, sizeof(zero)) == 0) return -1;
    }
    return result;
}

MuseNoiseError muse_noise_xx_init(MuseNoiseXX *noise) {
    static const uint8_t empty = 0;
    static const uint8_t prologue[32] = NOISE_PROTOCOL_NAME;
    const uint8_t *protocol = prologue;
    size_t protocol_size = sizeof(NOISE_PROTOCOL_NAME) - 1;
    int result;
    if (noise == NULL) return MUSE_NOISE_INVALID_MESSAGE;
    memset(noise, 0, sizeof(*noise));
    mbedtls_ecp_group_init(&noise->group);
    mbedtls_entropy_init(&noise->entropy);
    mbedtls_ctr_drbg_init(&noise->drbg);
    mbedtls_mpi_init(&noise->ephemeral_private);
    mbedtls_mpi_init(&noise->static_private);
    result = mbedtls_ecp_group_load(&noise->group, MBEDTLS_ECP_DP_CURVE25519);
    if (result == 0) result = mbedtls_ctr_drbg_seed(&noise->drbg, mbedtls_entropy_func,
                                                    &noise->entropy, (const unsigned char *)"MuseVita Noise XX", 17);
    if (result == 0) {
        memset(noise->handshake_hash, 0, sizeof(noise->handshake_hash));
        memcpy(noise->handshake_hash, protocol, protocol_size);
        memcpy(noise->chaining_key, noise->handshake_hash, sizeof(noise->chaining_key));
        result = mix_hash(noise, &empty, 0);
    }
    if (result != 0) {
        muse_noise_xx_free(noise);
        return MUSE_NOISE_CRYPTO;
    }
    noise->phase = NOISE_PHASE_INITIALIZED;
    noise->initialized = 1;
    return MUSE_NOISE_OK;
}

void muse_noise_xx_free(MuseNoiseXX *noise) {
    if (noise == NULL) return;
    mbedtls_mpi_free(&noise->ephemeral_private);
    mbedtls_mpi_free(&noise->static_private);
    mbedtls_ctr_drbg_free(&noise->drbg);
    mbedtls_entropy_free(&noise->entropy);
    mbedtls_ecp_group_free(&noise->group);
    memset(noise, 0, sizeof(*noise));
}

MuseNoiseError muse_noise_xx_write_message1(MuseNoiseXX *noise, uint8_t output[32]) {
    uint8_t empty = 0;
    if (noise == NULL || !noise->initialized || noise->phase != NOISE_PHASE_INITIALIZED) {
        return MUSE_NOISE_INVALID_STATE;
    }
    if (generate_key_pair(noise, &noise->ephemeral_private, noise->ephemeral_public) != 0 ||
        mix_hash(noise, noise->ephemeral_public, sizeof(noise->ephemeral_public)) != 0 ||
        encrypt_and_hash(noise, &empty, 0, NULL) != 0) {
        noise->phase = NOISE_PHASE_CREATED;
        return MUSE_NOISE_CRYPTO;
    }
    memcpy(output, noise->ephemeral_public, 32);
    noise->phase = NOISE_PHASE_MESSAGE1_SENT;
    return MUSE_NOISE_OK;
}

MuseNoiseError muse_noise_xx_read_message2(MuseNoiseXX *noise, const uint8_t *message, size_t message_size) {
    uint8_t shared[32];
    uint8_t clear[32];
    size_t clear_size = 0;
    if (noise == NULL || message == NULL || noise->phase != NOISE_PHASE_MESSAGE1_SENT) {
        return MUSE_NOISE_INVALID_STATE;
    }
    if (message_size < NOISE_MIN_MESSAGE2) return MUSE_NOISE_INVALID_MESSAGE;
    memcpy(noise->remote_ephemeral, message, 32);
    if (mix_hash(noise, noise->remote_ephemeral, 32) != 0 ||
        dh(noise, &noise->ephemeral_private, noise->remote_ephemeral, shared) != 0 ||
        mix_key(noise, shared) != 0 ||
        decrypt_and_hash(noise, message + 32, 48, clear, sizeof(clear), &clear_size) != 0 ||
        clear_size != 32) return MUSE_NOISE_CRYPTO;
    memcpy(noise->remote_static, clear, 32);
    if (dh(noise, &noise->ephemeral_private, noise->remote_static, shared) != 0 ||
        mix_key(noise, shared) != 0 ||
        decrypt_and_hash(noise, message + 80, message_size - 80, clear, sizeof(clear), &clear_size) != 0) {
        return MUSE_NOISE_CRYPTO;
    }
    noise->phase = NOISE_PHASE_MESSAGE2_READ;
    return MUSE_NOISE_OK;
}

MuseNoiseError muse_noise_xx_write_message3(MuseNoiseXX *noise, uint8_t *output,
                                            size_t output_capacity, size_t *output_size) {
    uint8_t shared[32];
    size_t first_size = 48;
    size_t second_size = 16;
    if (noise == NULL || output == NULL || output_size == NULL ||
        noise->phase != NOISE_PHASE_MESSAGE2_READ) return MUSE_NOISE_INVALID_STATE;
    if (output_capacity < NOISE_MIN_MESSAGE3) return MUSE_NOISE_OUTPUT_TOO_SMALL;
    if (generate_key_pair(noise, &noise->static_private, noise->static_public) != 0 ||
        encrypt_and_hash(noise, noise->static_public, 32, output) != 0 ||
        dh(noise, &noise->static_private, noise->remote_ephemeral, shared) != 0 ||
        mix_key(noise, shared) != 0 ||
        encrypt_and_hash(noise, NULL, 0, output + first_size) != 0) return MUSE_NOISE_CRYPTO;
    if (hkdf2(noise->chaining_key, NULL, 0, noise->send_key, noise->receive_key) != 0) {
        return MUSE_NOISE_CRYPTO;
    }
    noise->send_nonce = 0;
    noise->receive_nonce = 0;
    noise->phase = NOISE_PHASE_SPLIT;
    *output_size = first_size + second_size;
    return MUSE_NOISE_OK;
}

MuseNoiseError muse_noise_xx_encrypt(MuseNoiseXX *noise, const uint8_t *plaintext,
                                     size_t plaintext_size, uint8_t *ciphertext,
                                     size_t ciphertext_capacity, size_t *ciphertext_size) {
    if (noise == NULL || ciphertext == NULL || ciphertext_size == NULL ||
        (plaintext == NULL && plaintext_size != 0) || noise->phase != NOISE_PHASE_SPLIT) {
        return MUSE_NOISE_INVALID_STATE;
    }
    if (ciphertext_capacity < plaintext_size + MUSE_NOISE_TAG_BYTES) return MUSE_NOISE_OUTPUT_TOO_SMALL;
    uint64_t nonce;
    if (next_nonce(&noise->send_nonce, &nonce) != 0) return MUSE_NOISE_CRYPTO;
    if (aead_encrypt(noise->send_key, nonce, NULL, 0,
                     plaintext, plaintext_size, ciphertext) != 0) return MUSE_NOISE_CRYPTO;
    *ciphertext_size = plaintext_size + MUSE_NOISE_TAG_BYTES;
    return MUSE_NOISE_OK;
}

MuseNoiseError muse_noise_xx_decrypt(MuseNoiseXX *noise, const uint8_t *ciphertext,
                                     size_t ciphertext_size, uint8_t *plaintext,
                                     size_t plaintext_capacity, size_t *plaintext_size) {
    if (noise == NULL || ciphertext == NULL || plaintext == NULL || plaintext_size == NULL ||
        noise->phase != NOISE_PHASE_SPLIT || ciphertext_size < MUSE_NOISE_TAG_BYTES) {
        return MUSE_NOISE_INVALID_STATE;
    }
    if (plaintext_capacity < ciphertext_size - MUSE_NOISE_TAG_BYTES) return MUSE_NOISE_OUTPUT_TOO_SMALL;
    uint64_t nonce;
    if (next_nonce(&noise->receive_nonce, &nonce) != 0) return MUSE_NOISE_CRYPTO;
    if (aead_decrypt(noise->receive_key, nonce, NULL, 0,
                     ciphertext, ciphertext_size, plaintext) != 0) return MUSE_NOISE_CRYPTO;
    *plaintext_size = ciphertext_size - MUSE_NOISE_TAG_BYTES;
    return MUSE_NOISE_OK;
}
