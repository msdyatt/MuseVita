/*
 * Shared Muse control-stream framing contract for MuseVita and MuseDS.
 *
 * The official Muse Gadget SDK carries each JSON control message as a
 * little-endian uint32 byte length followed by UTF-8 JSON. A zero-length
 * frame is a keepalive. This file intentionally does not implement TLS,
 * WebSockets, Noise, or credential handling.
 */
#ifndef MUSE_CONTROL_FRAMING_H
#define MUSE_CONTROL_FRAMING_H

#include <stddef.h>
#include <stdint.h>

#define MUSE_CONTROL_MAX_MESSAGE_BYTES (4u * 1024u * 1024u)

typedef enum MuseControlError {
    MUSE_CONTROL_OK = 0,
    MUSE_CONTROL_NEED_MORE = 1,
    MUSE_CONTROL_KEEPALIVE = 2,
    MUSE_CONTROL_INVALID_ARGUMENT = -1,
    MUSE_CONTROL_MESSAGE_TOO_LARGE = -2,
    MUSE_CONTROL_OUTPUT_TOO_SMALL = -3,
} MuseControlError;

typedef struct MuseControlDecoder {
    uint8_t length_bytes[4];
    size_t length_size;
    uint32_t message_length;
    uint8_t message[MUSE_CONTROL_MAX_MESSAGE_BYTES];
    size_t message_size;
} MuseControlDecoder;

typedef MuseControlError (*MuseControlMessageCallback)(
    const uint8_t *message, size_t message_size, void *context);

void muse_control_decoder_init(MuseControlDecoder *decoder);

/* Feed any portion of a byte stream. The callback can be invoked multiple times. */
MuseControlError muse_control_decoder_feed(
    MuseControlDecoder *decoder,
    const uint8_t *data,
    size_t data_size,
    MuseControlMessageCallback callback,
    void *context);

/* Encode one JSON message, including its four-byte little-endian length. */
MuseControlError muse_control_encode(
    const uint8_t *message,
    size_t message_size,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size);

#endif
