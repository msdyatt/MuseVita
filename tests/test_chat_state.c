#include "chat_state.h"

#include <assert.h>
#include <string.h>

int main(void) {
    MuseVitaChatState state;
    muse_vita_chat_init(&state);
    muse_vita_chat_add_message(&state, MUSEVITA_CHAT_USER, "Ready\x1b[2J\n");
    assert(strcmp(state.messages[0].text, "Ready[2J") == 0);

    muse_vita_chat_start_assistant(&state);
    muse_vita_chat_append_assistant(&state, "caf\xc3\xa9\xc2\x9b\tOK");
    assert(strcmp(state.messages[1].text, "caféOK") == 0);

    muse_vita_chat_replace_last_assistant(&state, "Done\r\n");
    assert(strcmp(state.messages[1].text, "Done") == 0);
    return 0;
}
