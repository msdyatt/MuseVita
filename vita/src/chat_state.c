#include "chat_state.h"

#include <string.h>

static void copy_text(char *destination, const char *source) {
    if (!source) source = "";
    strncpy(destination, source, MUSEVITA_CHAT_MAX_TEXT - 1);
    destination[MUSEVITA_CHAT_MAX_TEXT - 1] = '\0';
}

void muse_vita_chat_init(MuseVitaChatState *state) {
    memset(state, 0, sizeof(*state));
}

void muse_vita_chat_add_message(MuseVitaChatState *state, MuseVitaChatRole role, const char *text) {
    unsigned int index;
    if (state->message_count < MUSEVITA_CHAT_MAX_MESSAGES) {
        index = state->message_count++;
    } else {
        memmove(&state->messages[0], &state->messages[1],
                sizeof(state->messages[0]) * (MUSEVITA_CHAT_MAX_MESSAGES - 1));
        index = MUSEVITA_CHAT_MAX_MESSAGES - 1;
    }
    state->messages[index].role = role;
    copy_text(state->messages[index].text, text);
}

void muse_vita_chat_set_draft(MuseVitaChatState *state, const char *text) {
    copy_text(state->draft, text);
    state->draft_ready = state->draft[0] != '\0';
}

void muse_vita_chat_clear_draft(MuseVitaChatState *state) {
    state->draft[0] = '\0';
    state->draft_ready = false;
}
