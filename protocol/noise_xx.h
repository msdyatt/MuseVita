/*
 * Noise XX initiator used by the Muse Gadget SDK transport.
 *
 * The state machine follows the official SDK's Noise_XX_25519_AESGCM_SHA256
 * implementation reviewed at upstream commit
 * f2c702b123e97f4fecdaefb29ded17bb03fab0b0. It is deliberately independent
 * of Vita networking and does not contain account credentials.
 */
#ifndef MUSE_NOISE_XX_H
#define MUSE_NOISE_XX_H

#include <stddef.h>
#include <stdint.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/bignum.h>

#define MUSE_NOISE_KEY_BYTES 32u
#define MUSE_NOISE_TAG_BYTES 16u

typedef enum MuseNoiseError {
    MUSE_NOISE_OK = 0,
    MUSE_NOISE_INVALID_STATE = -1,
    MUSE_NOISE_INVALID_MESSAGE = -2,
    MUSE_NOISE_CRYPTO = -3,
    MUSE_NOISE_OUTPUT_TOO_SMALL = -4,
} MuseNoiseError;

typedef struct MuseNoiseXX {
    mbedtls_ecp_group group;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_mpi ephemeral_private;
    mbedtls_mpi static_private;
    uint8_t ephemeral_public[MUSE_NOISE_KEY_BYTES];
    uint8_t static_public[MUSE_NOISE_KEY_BYTES];
    uint8_t remote_ephemeral[MUSE_NOISE_KEY_BYTES];
    uint8_t remote_static[MUSE_NOISE_KEY_BYTES];
    uint8_t chaining_key[MUSE_NOISE_KEY_BYTES];
    uint8_t handshake_hash[MUSE_NOISE_KEY_BYTES];
    uint8_t cipher_key[MUSE_NOISE_KEY_BYTES];
    uint8_t send_key[MUSE_NOISE_KEY_BYTES];
    uint8_t receive_key[MUSE_NOISE_KEY_BYTES];
    uint64_t cipher_nonce;
    uint64_t send_nonce;
    uint64_t receive_nonce;
    unsigned phase;
    int cipher_keyed;
    int initialized;
} MuseNoiseXX;

MuseNoiseError muse_noise_xx_init(MuseNoiseXX *noise);
void muse_noise_xx_free(MuseNoiseXX *noise);

MuseNoiseError muse_noise_xx_write_message1(
    MuseNoiseXX *noise, uint8_t output[MUSE_NOISE_KEY_BYTES]);

MuseNoiseError muse_noise_xx_read_message2(
    MuseNoiseXX *noise, const uint8_t *message, size_t message_size);

MuseNoiseError muse_noise_xx_write_message3(
    MuseNoiseXX *noise, uint8_t *output, size_t output_capacity, size_t *output_size);

MuseNoiseError muse_noise_xx_encrypt(
    MuseNoiseXX *noise, const uint8_t *plaintext, size_t plaintext_size,
    uint8_t *ciphertext, size_t ciphertext_capacity, size_t *ciphertext_size);

MuseNoiseError muse_noise_xx_decrypt(
    MuseNoiseXX *noise, const uint8_t *ciphertext, size_t ciphertext_size,
    uint8_t *plaintext, size_t plaintext_capacity, size_t *plaintext_size);

#endif
