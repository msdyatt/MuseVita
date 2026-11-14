#include "chat_state.h"

#include <string.h>

static size_t append_clean_text(char *destination, size_t offset, const char *source) {
    if (source == NULL) return offset;
    while (*source != '\0' && offset + 1 < MUSEVITA_CHAT_MAX_TEXT) {
        unsigned char value = (unsigned char)*source++;
        /* Match the official client: remove C0/DEL controls while preserving
           UTF-8 text, including encoded C1 controls. */
        if (value < 0x20u || value == 0x7fu) continue;
        if (value == 0xc2u && (unsigned char)*source >= 0x80u &&
            (unsigned char)*source <= 0x9fu) {
            source++;
            continue;
        }
        destination[offset++] = (char)value;
    }
    destination[offset] = '\0';
    return offset;
}

static void copy_text(char *destination, const char *source) {
    destination[0] = '\0';
    (void)append_clean_text(destination, 0, source);
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

void muse_vita_chat_start_assistant(MuseVitaChatState *state) {
    muse_vita_chat_add_message(state, MUSEVITA_CHAT_ASSISTANT, "");
}

void muse_vita_chat_append_assistant(MuseVitaChatState *state, const char *text) {
    MuseVitaChatMessage *message;
    size_t current_size;
    size_t remaining;
    if (state->message_count == 0) muse_vita_chat_start_assistant(state);
    message = &state->messages[state->message_count - 1];
    if (message->role != MUSEVITA_CHAT_ASSISTANT) {
        muse_vita_chat_start_assistant(state);
        message = &state->messages[state->message_count - 1];
    }
    current_size = strlen(message->text);
    remaining = MUSEVITA_CHAT_MAX_TEXT - 1 - current_size;
    if (remaining == 0 || text == NULL) return;
    (void)append_clean_text(message->text, current_size, text);
}

void muse_vita_chat_replace_last_assistant(MuseVitaChatState *state, const char *text) {
    if (state->message_count == 0 ||
        state->messages[state->message_count - 1].role != MUSEVITA_CHAT_ASSISTANT) {
        muse_vita_chat_start_assistant(state);
    }
    copy_text(state->messages[state->message_count - 1].text, text);
}

void muse_vita_chat_set_draft(MuseVitaChatState *state, const char *text) {
    copy_text(state->draft, text);
    state->draft_ready = state->draft[0] != '\0';
}

void muse_vita_chat_clear_draft(MuseVitaChatState *state) {
    state->draft[0] = '\0';
    state->draft_ready = false;
}
