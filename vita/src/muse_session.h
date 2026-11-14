#ifndef MUSEVITA_MUSE_SESSION_H
#define MUSEVITA_MUSE_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include <curl/curl.h>

#include "chat_state.h"
#include "control_framing.h"
#include "link_wire.h"
#include "noise_xx.h"

#define MUSEVITA_MAX_WS_MESSAGE_BYTES (512u * 1024u)
#define MUSEVITA_MAX_ASSEMBLY_BYTES (2u * 1024u * 1024u)
#define MUSEVITA_MAX_FRAME_BYTES (512u * 1024u)
#define MUSEVITA_MEDIA_BUFFER_BYTES (384u * 1024u)

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
    int64_t request_stream_id;
    int64_t next_stream_id;
    int64_t next_chunk_id;
    uint32_t assembly_total;
    uint32_t assembly_count;
    int64_t assembly_chunk_id;
    size_t assembly_size;
    size_t ws_message_size;
    uint64_t last_ping_us;
    uint64_t retry_after_us;
    size_t subscribe_buffer_size;
    size_t request_buffer_size;
    int request_status;
    bool request_active;
    bool request_done;
    bool request_failed;
    char active_assistant_id[128];
    char last_user_id[128];
    char register_request_id[40];
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
    /* A body chunk may use the official 16 KiB link chunk size. Keep the
       encrypted payload buffer at the full link limit so future media sends
       cannot fail just because the transport scratch buffer is smaller than
       the protobuf frame. */
    uint8_t crypto_buffer[MUSE_LINK_MAX_NOISE_CHUNK_BYTES + 64u];
    uint8_t media_capture_buffer[MUSEVITA_MEDIA_BUFFER_BYTES];
    char media_buffer[MUSEVITA_MEDIA_BUFFER_BYTES];
} MuseVitaSession;

int muse_vita_session_init(MuseVitaSession *session);
void muse_vita_session_term(MuseVitaSession *session);
int muse_vita_session_load_pairing(MuseVitaSession *session);
int muse_vita_session_import_pairing(MuseVitaSession *session, const char *identity_json,
                                     size_t identity_size, const char *pairing_json,
                                     size_t pairing_size, const char *sdk_token);
int muse_vita_session_connect(MuseVitaSession *session, MuseVitaChatState *chat);
int muse_vita_session_poll(MuseVitaSession *session);
int muse_vita_session_send_chat(MuseVitaSession *session, const char *message);
int muse_vita_session_send_media(MuseVitaSession *session, const char *message,
                                 const char *item_type, const char *mime_type,
                                 const char *filename, const uint8_t *data,
                                 size_t data_size);
bool muse_vita_session_should_connect(const MuseVitaSession *session);
bool muse_vita_session_should_reconnect(const MuseVitaSession *session);
bool muse_vita_session_is_connected(const MuseVitaSession *session);
const char *muse_vita_session_label(const MuseVitaSession *session);

#endif
