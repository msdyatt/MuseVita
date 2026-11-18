#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "control_framing.h"

typedef struct SeenMessage {
    int count;
    int keepalive_count;
    char value[64];
} SeenMessage;

static MuseControlError collect(const uint8_t *message, size_t size, void *context) {
    SeenMessage *seen = context;
    if (size == 0) {
        seen->keepalive_count += 1;
        return MUSE_CONTROL_OK;
    }
    assert(size < sizeof(seen->value));
    memcpy(seen->value, message, size);
    seen->value[size] = '\0';
    seen->count += 1;
    return MUSE_CONTROL_OK;
}

int main(void) {
    const uint8_t json[] = "{\"type\":\"req\"}";
    uint8_t encoded[64];
    size_t encoded_size = 0;
    static MuseControlDecoder decoder;
    SeenMessage seen = {0};

    assert(muse_control_encode(json, sizeof(json) - 1, encoded, sizeof(encoded), &encoded_size) == MUSE_CONTROL_OK);
    assert(encoded_size == sizeof(json) - 1 + 4);
    assert(encoded[0] == sizeof(json) - 1);
    assert(encoded[1] == 0 && encoded[2] == 0 && encoded[3] == 0);

    muse_control_decoder_init(&decoder);
    for (size_t i = 0; i < encoded_size; ++i) {
        MuseControlError result = muse_control_decoder_feed(&decoder, encoded + i, 1, collect, &seen);
        assert(result == MUSE_CONTROL_NEED_MORE || result == MUSE_CONTROL_OK);
    }
    assert(seen.count == 1);
    assert(strcmp(seen.value, "{\"type\":\"req\"}") == 0);

    const uint8_t keepalive[] = {0, 0, 0, 0};
    assert(muse_control_decoder_feed(&decoder, keepalive, sizeof(keepalive), collect, &seen) == MUSE_CONTROL_OK);
    assert(seen.keepalive_count == 1);
    return 0;
}
