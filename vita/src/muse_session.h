#ifndef MUSEVITA_MUSE_SESSION_H
#define MUSEVITA_MUSE_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include <curl/curl.h>

#include "chat_state.h"
#include "control_framing.h"
#include "noise_xx.h"

#define MUSEVITA_MAX_WS_MESSAGE_BYTES (512u * 1024u)
#define MUSEVITA_MAX_ASSEMBLY_BYTES (2u * 1024u * 1024u)
#define MUSEVITA_MAX_FRAME_BYTES (512u * 1024u)

typedef enum MuseVitaSessionState {
    MUSEVITA_SESSION_NO_PAIRING = 0,
    MUSEVITA_SESSION_READY = 1,
    MUSEVITA_SESSION_CONNECTING = 2,
    MUSEVITA_SESSION_CONNECTED = 3,
    MUSEVITA_SESSION_ERROR = 4,
} MuseVitaSessionState;

typedef struct MuseVitaSession {
    CURL *curl;
    struct curl_slist *headers;
    MuseNoiseXX noise;
    MuseControlDecoder control_decoder;
    MuseVitaSessionState state;
    MuseVitaChatState *chat;
    bool curl_initialized;
    bool noise_ready;
    bool registered;
    bool subscribed;
    bool ws_message_active;
    int64_t control_stream_id;
    int64_t subscribe_stream_id;
    int64_t next_stream_id;
    int64_t next_chunk_id;
    uint32_t assembly_total;
    uint32_t assembly_count;
    int64_t assembly_chunk_id;
    size_t assembly_size;
    size_t ws_message_size;
    uint64_t last_ping_us;
    size_t subscribe_buffer_size;
    size_t request_buffer_size;
    char active_assistant_id[128];
    char last_user_id[128];
    char access_token[2048];
    char refresh_token[2048];
    char sdk_token[256];
    char mac[32];
    char node_id[64];
    char device_id[96];
    char vm_id[256];
    char noise_host[256];
    char vm_auth_token[2048];
    char subscribe_buffer[262144];
    char request_buffer[65536];
    uint8_t ws_message[MUSEVITA_MAX_WS_MESSAGE_BYTES];
    uint8_t assembly_buffer[MUSEVITA_MAX_ASSEMBLY_BYTES];
    uint8_t frame_buffer[MUSEVITA_MAX_FRAME_BYTES];
    uint8_t crypto_buffer[4u * 1024u + 64u];
} MuseVitaSession;

int muse_vita_session_init(MuseVitaSession *session);
void muse_vita_session_term(MuseVitaSession *session);
int muse_vita_session_load_pairing(MuseVitaSession *session);
int muse_vita_session_connect(MuseVitaSession *session, MuseVitaChatState *chat);
int muse_vita_session_poll(MuseVitaSession *session);
int muse_vita_session_send_chat(MuseVitaSession *session, const char *message);
bool muse_vita_session_is_connected(const MuseVitaSession *session);
const char *muse_vita_session_label(const MuseVitaSession *session);

#endif
