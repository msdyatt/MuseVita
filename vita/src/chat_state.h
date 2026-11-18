#ifndef MUSEVITA_CHAT_STATE_H
#define MUSEVITA_CHAT_STATE_H

#include <stdbool.h>

#define MUSEVITA_CHAT_MAX_MESSAGES 16
#define MUSEVITA_CHAT_MAX_TEXT 256

typedef enum MuseVitaChatRole {
    MUSEVITA_CHAT_USER = 0,
    MUSEVITA_CHAT_ASSISTANT = 1,
} MuseVitaChatRole;

typedef struct MuseVitaChatMessage {
    MuseVitaChatRole role;
    char text[MUSEVITA_CHAT_MAX_TEXT];
} MuseVitaChatMessage;

typedef struct MuseVitaChatState {
    MuseVitaChatMessage messages[MUSEVITA_CHAT_MAX_MESSAGES];
    unsigned int message_count;
    char draft[MUSEVITA_CHAT_MAX_TEXT];
    bool draft_ready;
    bool transport_connected;
} MuseVitaChatState;

void muse_vita_chat_init(MuseVitaChatState *state);
void muse_vita_chat_add_message(MuseVitaChatState *state, MuseVitaChatRole role, const char *text);
void muse_vita_chat_start_assistant(MuseVitaChatState *state);
void muse_vita_chat_append_assistant(MuseVitaChatState *state, const char *text);
void muse_vita_chat_replace_last_assistant(MuseVitaChatState *state, const char *text);
void muse_vita_chat_set_draft(MuseVitaChatState *state, const char *text);
void muse_vita_chat_clear_draft(MuseVitaChatState *state);

#endif
