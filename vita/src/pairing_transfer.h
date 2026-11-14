#ifndef MUSEVITA_PAIRING_TRANSFER_H
#define MUSEVITA_PAIRING_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <psp2/kernel/threadmgr.h>

#define MUSEVITA_PAIRING_QR_MAX 256
#define MUSEVITA_PAIRING_IDENTITY_MAX 4096
#define MUSEVITA_PAIRING_JSON_MAX 16384
#define MUSEVITA_PAIRING_TOKEN_MAX 256

typedef struct MuseVitaPairingTransfer {
    SceUID thread;
    int listen_socket;
    volatile bool stop;
    volatile bool listener_bound;
    bool ready;
    bool consumed;
    uint64_t expires_us;
    char session_id[17];
    char public_key_b64[96];
    char qr_payload[MUSEVITA_PAIRING_QR_MAX];
    char manual_url[64];
    char status[96];
    uint8_t public_key[65];
    mbedtls_ecp_group group;
    mbedtls_mpi private_key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    char pending_identity[MUSEVITA_PAIRING_IDENTITY_MAX];
    char pending_pairing[MUSEVITA_PAIRING_JSON_MAX];
    char pending_token[MUSEVITA_PAIRING_TOKEN_MAX];
    bool pending_bundle;
} MuseVitaPairingTransfer;

int muse_vita_pairing_transfer_start(MuseVitaPairingTransfer *transfer);
void muse_vita_pairing_transfer_stop(MuseVitaPairingTransfer *transfer);
const char *muse_vita_pairing_transfer_qr(const MuseVitaPairingTransfer *transfer);
const char *muse_vita_pairing_transfer_manual_url(const MuseVitaPairingTransfer *transfer);
const char *muse_vita_pairing_transfer_status(const MuseVitaPairingTransfer *transfer);
int muse_vita_pairing_transfer_take(MuseVitaPairingTransfer *transfer,
                                    char *identity, size_t identity_capacity,
                                    char *pairing, size_t pairing_capacity,
                                    char *token, size_t token_capacity);

#endif
