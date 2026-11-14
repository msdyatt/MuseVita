#include "control_framing.h"

#include <string.h>

static uint32_t read_u32_le(const uint8_t *bytes) {
    return ((uint32_t)bytes[0]) |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void write_u32_le(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
}

void muse_control_decoder_init(MuseControlDecoder *decoder) {
    if (decoder == NULL) return;
    memset(decoder, 0, sizeof(*decoder));
}

MuseControlError muse_control_decoder_feed(
    MuseControlDecoder *decoder,
    const uint8_t *data,
    size_t data_size,
    MuseControlMessageCallback callback,
    void *context) {
    size_t offset = 0;

    if (decoder == NULL || callback == NULL || (data == NULL && data_size != 0)) {
        return MUSE_CONTROL_INVALID_ARGUMENT;
    }

    while (offset < data_size) {
        if (decoder->length_size < sizeof(decoder->length_bytes)) {
            size_t needed = sizeof(decoder->length_bytes) - decoder->length_size;
            size_t available = data_size - offset;
            size_t copied = needed < available ? needed : available;
            memcpy(decoder->length_bytes + decoder->length_size, data + offset, copied);
            decoder->length_size += copied;
            offset += copied;
            if (decoder->length_size < sizeof(decoder->length_bytes)) return MUSE_CONTROL_NEED_MORE;
            decoder->message_length = read_u32_le(decoder->length_bytes);
            decoder->message_size = 0;
            if (decoder->message_length > MUSE_CONTROL_MAX_MESSAGE_BYTES) {
                return MUSE_CONTROL_MESSAGE_TOO_LARGE;
            }
        }

        if (decoder->message_length != 0) {
            size_t needed = (size_t)decoder->message_length - decoder->message_size;
            size_t available = data_size - offset;
            size_t copied = needed < available ? needed : available;
            memcpy(decoder->message + decoder->message_size, data + offset, copied);
            decoder->message_size += copied;
            offset += copied;
            if (decoder->message_size < decoder->message_length) return MUSE_CONTROL_NEED_MORE;
            MuseControlError result = callback(decoder->message, decoder->message_size, context);
            if (result != MUSE_CONTROL_OK) return result;
        } else {
            MuseControlError result = callback(NULL, 0, context);
            if (result != MUSE_CONTROL_OK) return result;
        }

        decoder->length_size = 0;
        decoder->message_length = 0;
        decoder->message_size = 0;
    }
    return MUSE_CONTROL_OK;
}

MuseControlError muse_control_encode(
    const uint8_t *message,
    size_t message_size,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size) {
    if (output_size == NULL || output == NULL || (message == NULL && message_size != 0)) {
        return MUSE_CONTROL_INVALID_ARGUMENT;
    }
    if (message_size > MUSE_CONTROL_MAX_MESSAGE_BYTES) return MUSE_CONTROL_MESSAGE_TOO_LARGE;
    if (output_capacity < message_size + sizeof(uint32_t)) return MUSE_CONTROL_OUTPUT_TOO_SMALL;
    write_u32_le(output, (uint32_t)message_size);
    if (message_size != 0) memcpy(output + sizeof(uint32_t), message, message_size);
    *output_size = message_size + sizeof(uint32_t);
    return MUSE_CONTROL_OK;
}
