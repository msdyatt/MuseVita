/* Shared protobuf/JSON wire builders for MuseVita and future MuseDS clients. */
#ifndef MUSE_LINK_WIRE_H
#define MUSE_LINK_WIRE_H

#include <stddef.h>
#include <stdint.h>

typedef enum MuseLinkWireError {
    MUSE_LINK_WIRE_OK = 0,
    MUSE_LINK_WIRE_INVALID_ARGUMENT = -1,
    MUSE_LINK_WIRE_OUTPUT_TOO_SMALL = -2,
    MUSE_LINK_WIRE_VALUE_TOO_LARGE = -3,
} MuseLinkWireError;

MuseLinkWireError muse_link_encode_open_stream_request(
    int64_t stream_id, uint8_t *output, size_t capacity, size_t *output_size);

MuseLinkWireError muse_link_encode_body_chunk(
    int64_t stream_id, const uint8_t *body, size_t body_size, int end_body,
    uint8_t *output, size_t capacity, size_t *output_size);

MuseLinkWireError muse_link_encode_noise_frame(
    int64_t chunk_id, uint32_t chunk_index, uint32_t total_chunks,
    const uint8_t *payload, size_t payload_size,
    uint8_t *output, size_t capacity, size_t *output_size);

MuseLinkWireError muse_link_build_register_message(
    const char *request_id, const char *node_id, const char *display_name,
    char *output, size_t capacity, size_t *output_size);

MuseLinkWireError muse_link_build_chat_request(
    const char *message, const char *node_id,
    char *output, size_t capacity, size_t *output_size);

#endif
