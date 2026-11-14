/* Shared protobuf/JSON wire builders for MuseVita and future MuseDS clients. */
#ifndef MUSE_LINK_WIRE_H
#define MUSE_LINK_WIRE_H

#include <stddef.h>
#include <stdint.h>

#define MUSE_LINK_MAX_NOISE_CHUNK_BYTES 65489u
#define MUSE_LINK_MAX_NOISE_CHUNKS 256u

typedef enum MuseLinkWireError {
    MUSE_LINK_WIRE_OK = 0,
    MUSE_LINK_WIRE_INVALID_ARGUMENT = -1,
    MUSE_LINK_WIRE_OUTPUT_TOO_SMALL = -2,
    MUSE_LINK_WIRE_VALUE_TOO_LARGE = -3,
    MUSE_LINK_WIRE_INVALID_MESSAGE = -4,
} MuseLinkWireError;

typedef struct MuseLinkHeader {
    const char *key;
    const char *value;
} MuseLinkHeader;

typedef enum MuseLinkIncomingKind {
    MUSE_LINK_INCOMING_RESPONSE = 0,
    MUSE_LINK_INCOMING_BODY_CHUNK = 1,
    MUSE_LINK_INCOMING_RESET = 2,
} MuseLinkIncomingKind;

typedef struct MuseLinkIncoming {
    MuseLinkIncomingKind kind;
    int64_t stream_id;
    int status;
    int end_body;
    const uint8_t *body;
    size_t body_size;
    int reset_code;
} MuseLinkIncoming;

typedef struct MuseLinkNoiseFrame {
    int64_t chunk_id;
    uint32_t chunk_index;
    uint32_t total_chunks;
    const uint8_t *payload;
    size_t payload_size;
} MuseLinkNoiseFrame;

MuseLinkWireError muse_link_encode_open_stream_request(
    int64_t stream_id, uint8_t *output, size_t capacity, size_t *output_size);

MuseLinkWireError muse_link_encode_body_chunk(
    int64_t stream_id, const uint8_t *body, size_t body_size, int end_body,
    uint8_t *output, size_t capacity, size_t *output_size);

MuseLinkWireError muse_link_encode_http_request(
    int64_t stream_id, const char *method, const char *path,
    const MuseLinkHeader *headers, size_t header_count,
    const uint8_t *body, size_t body_size, int end_body,
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

MuseLinkWireError muse_link_build_chat_media_request(
    const char *message, const char *node_id, const char *item_type,
    const char *mime_type, const char *filename,
    const uint8_t *data, size_t data_size,
    char *output, size_t capacity, size_t *output_size);

MuseLinkWireError muse_link_build_command_result(
    const char *request_id, int ok, const char *error,
    const char *media_type, size_t media_size,
    char *output, size_t capacity, size_t *output_size);

MuseLinkWireError muse_link_decode_noise_frame(
    const uint8_t *input, size_t input_size, MuseLinkNoiseFrame *frame);

MuseLinkWireError muse_link_decode_service_response(
    const uint8_t *input, size_t input_size, MuseLinkIncoming *incoming);

#endif
