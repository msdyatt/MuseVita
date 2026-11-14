#include "link_wire.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_LINK_FIELD_BYTES (4u * 1024u * 1024u)

static MuseLinkWireError append_bytes(uint8_t *output, size_t capacity, size_t *offset,
                                      const uint8_t *data, size_t data_size) {
    if (data_size > MAX_LINK_FIELD_BYTES || *offset > capacity || data_size > capacity - *offset) {
        return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
    }
    if (data_size != 0) memcpy(output + *offset, data, data_size);
    *offset += data_size;
    return MUSE_LINK_WIRE_OK;
}

static MuseLinkWireError append_varint(uint8_t *output, size_t capacity, size_t *offset,
                                       uint64_t value) {
    uint8_t encoded[10];
    size_t size = 0;
    do {
        encoded[size++] = (uint8_t)((value & 0x7fu) | (value >= 0x80u ? 0x80u : 0));
        value >>= 7;
    } while (value != 0 && size < sizeof(encoded));
    if (value != 0) return MUSE_LINK_WIRE_VALUE_TOO_LARGE;
    return append_bytes(output, capacity, offset, encoded, size);
}

static MuseLinkWireError append_key(uint8_t *output, size_t capacity, size_t *offset,
                                    unsigned field, unsigned wire_type) {
    return append_varint(output, capacity, offset, ((uint64_t)field << 3) | wire_type);
}

static MuseLinkWireError append_field(uint8_t *output, size_t capacity, size_t *offset,
                                      unsigned field, const uint8_t *data, size_t data_size) {
    MuseLinkWireError result = append_key(output, capacity, offset, field, 2);
    if (result == MUSE_LINK_WIRE_OK) result = append_varint(output, capacity, offset, data_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_bytes(output, capacity, offset, data, data_size);
    return result;
}

static MuseLinkWireError append_string(uint8_t *output, size_t capacity, size_t *offset,
                                       unsigned field, const char *value) {
    if (value == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    return append_field(output, capacity, offset, field,
                        (const uint8_t *)value, strlen(value));
}

static MuseLinkWireError append_bool(uint8_t *output, size_t capacity, size_t *offset,
                                     unsigned field, int value) {
    MuseLinkWireError result = append_key(output, capacity, offset, field, 0);
    if (result == MUSE_LINK_WIRE_OK) result = append_varint(output, capacity, offset, value ? 1 : 0);
    return result;
}

static MuseLinkWireError append_int64(uint8_t *output, size_t capacity, size_t *offset,
                                      unsigned field, int64_t value) {
    MuseLinkWireError result = append_key(output, capacity, offset, field, 0);
    if (result == MUSE_LINK_WIRE_OK) result = append_varint(output, capacity, offset, (uint64_t)value);
    return result;
}

MuseLinkWireError muse_link_encode_open_stream_request(
    int64_t stream_id, uint8_t *output, size_t capacity, size_t *output_size) {
    uint8_t request[256];
    uint8_t frame[320];
    uint8_t service_request[360];
    size_t request_size = 0;
    size_t frame_size = 0;
    size_t service_size = 0;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    *output_size = 0;
    result = append_string(request, sizeof(request), &request_size, 1, "POST");
    if (result == MUSE_LINK_WIRE_OK) result = append_string(request, sizeof(request), &request_size, 2, "/link-control");
    if (result == MUSE_LINK_WIRE_OK) result = append_int64(frame, sizeof(frame), &frame_size, 1, stream_id);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(frame, sizeof(frame), &frame_size, 2, request, request_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(service_request, sizeof(service_request), &service_size, 2, frame, frame_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_bytes(output, capacity, output_size, service_request, service_size);
    return result;
}

MuseLinkWireError muse_link_encode_body_chunk(
    int64_t stream_id, const uint8_t *body, size_t body_size, int end_body,
    uint8_t *output, size_t capacity, size_t *output_size) {
    uint8_t chunk[4u * 1024u + 32u];
    uint8_t frame[4u * 1024u + 64u];
    uint8_t service_request[4u * 1024u + 96u];
    size_t chunk_size = 0;
    size_t frame_size = 0;
    size_t service_size = 0;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL || (body == NULL && body_size != 0)) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    *output_size = 0;
    result = append_field(chunk, sizeof(chunk), &chunk_size, 1, body, body_size);
    if (result == MUSE_LINK_WIRE_OK && end_body) result = append_bool(chunk, sizeof(chunk), &chunk_size, 2, 1);
    if (result == MUSE_LINK_WIRE_OK) result = append_int64(frame, sizeof(frame), &frame_size, 1, stream_id);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(frame, sizeof(frame), &frame_size, 4, chunk, chunk_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(service_request, sizeof(service_request), &service_size, 2, frame, frame_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_bytes(output, capacity, output_size, service_request, service_size);
    return result;
}

MuseLinkWireError muse_link_encode_noise_frame(
    int64_t chunk_id, uint32_t chunk_index, uint32_t total_chunks,
    const uint8_t *payload, size_t payload_size,
    uint8_t *output, size_t capacity, size_t *output_size) {
    size_t offset = 0;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL || (payload == NULL && payload_size != 0)) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    result = append_int64(output, capacity, &offset, 1, chunk_id);
    if (result == MUSE_LINK_WIRE_OK && chunk_index != 0) result = append_key(output, capacity, &offset, 2, 0);
    if (result == MUSE_LINK_WIRE_OK && chunk_index != 0) result = append_varint(output, capacity, &offset, chunk_index);
    if (result == MUSE_LINK_WIRE_OK && total_chunks != 0) result = append_key(output, capacity, &offset, 3, 0);
    if (result == MUSE_LINK_WIRE_OK && total_chunks != 0) result = append_varint(output, capacity, &offset, total_chunks);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(output, capacity, &offset, 4, payload, payload_size);
    if (result == MUSE_LINK_WIRE_OK) *output_size = offset;
    return result;
}

static MuseLinkWireError json_string(char *output, size_t capacity, size_t *offset, const char *value) {
    size_t i;
    if (value == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    if (*offset >= capacity) return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
    output[(*offset)++] = '"';
    for (i = 0; value[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)value[i];
        const char *escaped = NULL;
        if (c == '"') escaped = "\\\"";
        else if (c == '\\') escaped = "\\\\";
        else if (c == '\n') escaped = "\\n";
        else if (c == '\r') escaped = "\\r";
        else if (c == '\t') escaped = "\\t";
        if (escaped != NULL) {
            size_t escaped_size = strlen(escaped);
            if (escaped_size > capacity - *offset - 1) return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
            memcpy(output + *offset, escaped, escaped_size);
            *offset += escaped_size;
        } else {
            if (c < 0x20 || *offset + 1 >= capacity) return MUSE_LINK_WIRE_VALUE_TOO_LARGE;
            output[(*offset)++] = (char)c;
        }
    }
    if (*offset >= capacity) return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
    output[(*offset)++] = '"';
    return MUSE_LINK_WIRE_OK;
}

static MuseLinkWireError json_pair(char *output, size_t capacity, size_t *offset,
                                   const char *key, const char *value, int *first) {
    MuseLinkWireError result;
    if (!*first) {
        if (*offset >= capacity) return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
        output[(*offset)++] = ',';
    }
    *first = 0;
    result = json_string(output, capacity, offset, key);
    if (result == MUSE_LINK_WIRE_OK) {
        if (*offset >= capacity) return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
        output[(*offset)++] = ':';
        result = json_string(output, capacity, offset, value);
    }
    return result;
}

static MuseLinkWireError json_format(char *output, size_t capacity, size_t *offset,
                                     const char *format, ...) {
    va_list args;
    int written;
    if (*offset >= capacity) return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
    va_start(args, format);
    written = vsnprintf(output + *offset, capacity - *offset, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= capacity - *offset) return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
    *offset += (size_t)written;
    return MUSE_LINK_WIRE_OK;
}

MuseLinkWireError muse_link_build_register_message(
    const char *request_id, const char *node_id, const char *display_name,
    char *output, size_t capacity, size_t *output_size) {
    size_t offset = 0;
    int first = 1;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    result = json_format(output, capacity, &offset, "{\"type\":\"req\",\"id\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, request_id);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, ",\"method\":\"link.register\",\"params\":{");
    if (result == MUSE_LINK_WIRE_OK) result = json_pair(output, capacity, &offset, "node_id", node_id, &first);
    if (result == MUSE_LINK_WIRE_OK) result = json_pair(output, capacity, &offset, "display_name", display_name, &first);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset,
        ",\"platform\":\"vita\",\"version\":\"0.1.0\",\"device_family\":\"gadget\",\"model_id\":\"muse-gadget-vita\",\"metadata\":{\"model_name\":\"PlayStation Vita\"},\"is_wakeup_supported\":false,\"commands_v2\":{}}");
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, "}");
    if (result == MUSE_LINK_WIRE_OK) *output_size = offset;
    return result;
}

MuseLinkWireError muse_link_build_chat_request(
    const char *message, const char *node_id,
    char *output, size_t capacity, size_t *output_size) {
    size_t offset = 0;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    result = json_format(output, capacity, &offset, "{\"message\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, message);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, ",\"output_modality\":\"text\",\"device_id\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, node_id);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, "}");
    if (result == MUSE_LINK_WIRE_OK) *output_size = offset;
    return result;
}
