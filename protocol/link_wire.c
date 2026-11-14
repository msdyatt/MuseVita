#include "link_wire.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINK_FIELD_BYTES (4u * 1024u * 1024u)
#define MAX_HTTP_REQUEST_BYTES (64u * 1024u + 4096u)
#define MAX_BODY_CHUNK_BYTES (16u * 1024u)

static MuseLinkWireError append_string(uint8_t *output, size_t capacity, size_t *offset,
                                       unsigned field, const char *value);
static MuseLinkWireError json_base64(char *output, size_t capacity, size_t *offset,
                                     const uint8_t *data, size_t data_size);

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

static MuseLinkWireError append_header(uint8_t *output, size_t capacity, size_t *offset,
                                       const MuseLinkHeader *header) {
    uint8_t encoded[512];
    size_t encoded_size = 0;
    MuseLinkWireError result;
    if (header == NULL || header->key == NULL || header->value == NULL) {
        return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    }
    result = append_string(encoded, sizeof(encoded), &encoded_size, 1, header->key);
    if (result == MUSE_LINK_WIRE_OK) {
        result = append_string(encoded, sizeof(encoded), &encoded_size, 2, header->value);
    }
    if (result == MUSE_LINK_WIRE_OK) {
        result = append_field(output, capacity, offset, 3, encoded, encoded_size);
    }
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
    uint8_t chunk[MAX_BODY_CHUNK_BYTES + 32u];
    uint8_t frame[MAX_BODY_CHUNK_BYTES + 64u];
    uint8_t service_request[MAX_BODY_CHUNK_BYTES + 96u];
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

MuseLinkWireError muse_link_encode_reset(
    int64_t stream_id, int reset_code, const char *reason,
    uint8_t *output, size_t capacity, size_t *output_size) {
    uint8_t reset[256];
    uint8_t frame[320];
    uint8_t service_request[360];
    size_t reset_size = 0;
    size_t frame_size = 0;
    size_t service_size = 0;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL || reason == NULL ||
        reset_code < 0 || reset_code > INT32_MAX) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    *output_size = 0;
    result = append_int64(reset, sizeof(reset), &reset_size, 1, reset_code);
    if (result == MUSE_LINK_WIRE_OK && reason[0] != '\0') {
        result = append_string(reset, sizeof(reset), &reset_size, 2, reason);
    }
    if (result == MUSE_LINK_WIRE_OK) result = append_int64(frame, sizeof(frame), &frame_size, 1, stream_id);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(frame, sizeof(frame), &frame_size, 5,
                                                              reset, reset_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(service_request, sizeof(service_request),
                                                            &service_size, 2, frame, frame_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_bytes(output, capacity, output_size,
                                                            service_request, service_size);
    return result;
}

MuseLinkWireError muse_link_encode_http_request(
    int64_t stream_id, const char *method, const char *path,
    const MuseLinkHeader *headers, size_t header_count,
    const uint8_t *body, size_t body_size, int end_body,
    uint8_t *output, size_t capacity, size_t *output_size) {
    uint8_t *request = NULL;
    uint8_t *frame = NULL;
    uint8_t *service_request = NULL;
    size_t request_capacity;
    size_t frame_capacity;
    size_t service_capacity;
    size_t request_size = 0;
    size_t frame_size = 0;
    size_t service_size = 0;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL || method == NULL || path == NULL ||
        (body == NULL && body_size != 0) || header_count > 32) {
        return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    }
    if (body_size > MAX_HTTP_REQUEST_BYTES) return MUSE_LINK_WIRE_VALUE_TOO_LARGE;
    *output_size = 0;
    request_capacity = body_size + 256u;
    for (size_t i = 0; i < header_count; i++) {
        if (headers[i].key == NULL || headers[i].value == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
        request_capacity += strlen(headers[i].key) + strlen(headers[i].value) + 16u;
    }
    if (request_capacity < body_size || request_capacity > MAX_HTTP_REQUEST_BYTES + 4096u) {
        return MUSE_LINK_WIRE_VALUE_TOO_LARGE;
    }
    frame_capacity = request_capacity + 64u;
    service_capacity = request_capacity + 128u;
    request = malloc(request_capacity);
    frame = malloc(frame_capacity);
    service_request = malloc(service_capacity);
    if (request == NULL || frame == NULL || service_request == NULL) {
        free(request);
        free(frame);
        free(service_request);
        return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
    }
    result = append_string(request, request_capacity, &request_size, 1, method);
    if (result == MUSE_LINK_WIRE_OK) result = append_string(request, request_capacity, &request_size, 2, path);
    for (size_t i = 0; result == MUSE_LINK_WIRE_OK && i < header_count; i++) {
        result = append_header(request, request_capacity, &request_size, &headers[i]);
    }
    if (result == MUSE_LINK_WIRE_OK && body_size != 0) {
        result = append_field(request, request_capacity, &request_size, 4, body, body_size);
    }
    if (result == MUSE_LINK_WIRE_OK && end_body) {
        result = append_bool(request, request_capacity, &request_size, 5, 1);
    }
    if (result == MUSE_LINK_WIRE_OK) result = append_int64(frame, frame_capacity, &frame_size, 1, stream_id);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(frame, frame_capacity, &frame_size, 2, request, request_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_field(service_request, service_capacity, &service_size, 2, frame, frame_size);
    if (result == MUSE_LINK_WIRE_OK) result = append_bytes(output, capacity, output_size, service_request, service_size);
    free(request);
    free(frame);
    free(service_request);
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
        ",\"platform\":\"vita\",\"version\":\"0.1.0\",\"device_family\":\"gadget\",\"model_id\":\"musevita-psvita\",\"metadata\":{\"model_name\":\"PlayStation Vita\"},\"is_wakeup_supported\":false,\"commands_v2\":{\"device.guide\":{\"description\":\"Describe MuseVita capabilities and controls.\",\"required\":{},\"optional\":{},\"timeout_ms\":5000},\"camera.capture\":{\"description\":\"Capture one still 320x240 JPEG frame and return it as base64.\",\"required\":{},\"optional\":{},\"timeout_ms\":30000},\"vita.microphone_record\":{\"description\":\"Record a two-second voice note and send it to this Muse conversation.\",\"required\":{},\"optional\":{},\"timeout_ms\":30000}}}");
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, "}");
    if (result == MUSE_LINK_WIRE_OK) *output_size = offset;
    return result;
}

MuseLinkWireError muse_link_build_command_result(
    const char *request_id, int ok, const char *error,
    const char *media_type, size_t media_size,
    char *output, size_t capacity, size_t *output_size) {
    size_t offset = 0;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL || request_id == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    *output_size = 0;
    result = json_format(output, capacity, &offset, "{\"method\":\"link.result\",\"id\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, request_id);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset,
                                                           ok ? ",\"ok\":true" : ",\"ok\":false");
    if (result == MUSE_LINK_WIRE_OK && ok) {
        result = json_format(output, capacity, &offset, ",\"payload\":{\"sent\":true,\"media_type\":");
        if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset,
                                                               media_type == NULL ? "unknown" : media_type);
        if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, ",\"bytes\":%zu}", media_size);
    } else if (result == MUSE_LINK_WIRE_OK) {
        result = json_format(output, capacity, &offset, ",\"error\":");
        if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset,
                                                               error == NULL ? "command failed" : error);
    }
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, "}");
    if (result == MUSE_LINK_WIRE_OK) *output_size = offset;
    return result;
}

MuseLinkWireError muse_link_build_camera_result(
    const char *request_id, const uint8_t *data, size_t data_size,
    char *output, size_t capacity, size_t *output_size) {
    size_t offset = 0;
    MuseLinkWireError result;
    if (request_id == NULL || output == NULL || output_size == NULL ||
        (data == NULL && data_size != 0)) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    *output_size = 0;
    result = json_format(output, capacity, &offset, "{\"method\":\"link.result\",\"id\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, request_id);
    if (result == MUSE_LINK_WIRE_OK) {
        result = json_format(output, capacity, &offset,
                             ",\"ok\":true,\"payload\":{\"format\":\"jpeg-base64\",\"data_base64\":\"");
    }
    if (result == MUSE_LINK_WIRE_OK) result = json_base64(output, capacity, &offset, data, data_size);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, "\"}}");
    if (result == MUSE_LINK_WIRE_OK) *output_size = offset;
    return result;
}

MuseLinkWireError muse_link_build_guide_result(
    const char *request_id, char *output, size_t capacity, size_t *output_size) {
    static const char guide[] =
        "# MuseVita\\n\\n"
        "MuseVita is a native PlayStation Vita client for Muse. It can send and receive text chats over the Muse link, capture a photo with the Vita camera, and record a short voice note with the microphone.\\n\\n"
        "Controls: X changes pages or opens the composer, SQUARE sends a draft, L captures a photo, R records a voice note, and TRIANGLE opens Muse Web. The status and setup pages show connection state and local device controls.";
    size_t offset = 0;
    MuseLinkWireError result;
    if (request_id == NULL || output == NULL || output_size == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    *output_size = 0;
    result = json_format(output, capacity, &offset, "{\"method\":\"link.result\",\"id\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, request_id);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset,
                                                           ",\"ok\":true,\"payload\":{\"format\":\"markdown\",\"guide\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, guide);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset,
                                                           ",\"capabilities\":{\"chat\":true,\"camera_capture\":true,\"microphone_record\":true}}}");
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

static MuseLinkWireError json_base64(char *output, size_t capacity, size_t *offset,
                                     const uint8_t *data, size_t data_size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t index = 0;
    if (data == NULL && data_size != 0) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    while (index < data_size) {
        uint32_t value = (uint32_t)data[index++] << 16;
        int have_second = index < data_size;
        int have_third;
        if (have_second) value |= (uint32_t)data[index++] << 8;
        have_third = index < data_size;
        if (have_third) value |= data[index++];
        if (*offset + 4 >= capacity) return MUSE_LINK_WIRE_OUTPUT_TOO_SMALL;
        output[(*offset)++] = alphabet[(value >> 18) & 0x3f];
        output[(*offset)++] = alphabet[(value >> 12) & 0x3f];
        output[(*offset)++] = have_second ? alphabet[(value >> 6) & 0x3f] : '=';
        output[(*offset)++] = have_third ? alphabet[value & 0x3f] : '=';
    }
    return MUSE_LINK_WIRE_OK;
}

MuseLinkWireError muse_link_build_chat_media_request(
    const char *message, const char *node_id, const char *item_type,
    const char *mime_type, const char *filename,
    const uint8_t *data, size_t data_size,
    char *output, size_t capacity, size_t *output_size) {
    size_t offset = 0;
    int first_item = 1;
    MuseLinkWireError result;
    if (output == NULL || output_size == NULL || message == NULL || node_id == NULL ||
        item_type == NULL || mime_type == NULL || filename == NULL ||
        (data == NULL && data_size != 0)) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    *output_size = 0;
    result = json_format(output, capacity, &offset, "{\"message\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, message);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset,
                                                            ",\"output_modality\":\"text\",\"device_id\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, node_id);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, ",\"items\":[");
    if (result == MUSE_LINK_WIRE_OK && message[0] != '\0') {
        result = json_format(output, capacity, &offset, "{\"type\":\"text\",\"text\":");
        if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, message);
        if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, "}");
        first_item = 0;
    }
    if (result == MUSE_LINK_WIRE_OK && !first_item) {
        result = json_format(output, capacity, &offset, ",");
    }
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset,
                                                            "{\"type\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, item_type);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, ",\"mime_type\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, mime_type);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, ",\"filename\":");
    if (result == MUSE_LINK_WIRE_OK) result = json_string(output, capacity, &offset, filename);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, ",\"data_base64\":\"");
    if (result == MUSE_LINK_WIRE_OK) result = json_base64(output, capacity, &offset, data, data_size);
    if (result == MUSE_LINK_WIRE_OK) result = json_format(output, capacity, &offset, "\"}]}" );
    if (result == MUSE_LINK_WIRE_OK) *output_size = offset;
    return result;
}

static MuseLinkWireError read_varint(const uint8_t *input, size_t input_size, size_t *offset,
                                     uint64_t *value) {
    uint64_t result = 0;
    for (unsigned i = 0; i < 10; i++) {
        if (*offset >= input_size) return MUSE_LINK_WIRE_INVALID_MESSAGE;
        uint8_t byte = input[(*offset)++];
        if (i == 9 && (byte & 0xfeu) != 0) return MUSE_LINK_WIRE_INVALID_MESSAGE;
        result |= ((uint64_t)(byte & 0x7fu)) << (i * 7u);
        if ((byte & 0x80u) == 0) {
            *value = result;
            return MUSE_LINK_WIRE_OK;
        }
    }
    return MUSE_LINK_WIRE_INVALID_MESSAGE;
}

static MuseLinkWireError read_key(const uint8_t *input, size_t input_size, size_t *offset,
                                  unsigned *field, unsigned *wire_type) {
    uint64_t key;
    MuseLinkWireError result = read_varint(input, input_size, offset, &key);
    if (result != MUSE_LINK_WIRE_OK || key == 0 || key > 0xffffffffu) return MUSE_LINK_WIRE_INVALID_MESSAGE;
    *field = (unsigned)(key >> 3);
    *wire_type = (unsigned)(key & 7u);
    if (*field == 0 || (*wire_type != 0 && *wire_type != 1 && *wire_type != 2 && *wire_type != 5)) {
        return MUSE_LINK_WIRE_INVALID_MESSAGE;
    }
    return MUSE_LINK_WIRE_OK;
}

static MuseLinkWireError read_delimited(const uint8_t *input, size_t input_size, size_t *offset,
                                        const uint8_t **value, size_t *value_size) {
    uint64_t length;
    MuseLinkWireError result = read_varint(input, input_size, offset, &length);
    if (result != MUSE_LINK_WIRE_OK || length > input_size - *offset) return MUSE_LINK_WIRE_INVALID_MESSAGE;
    *value = input + *offset;
    *value_size = (size_t)length;
    *offset += (size_t)length;
    return MUSE_LINK_WIRE_OK;
}

static MuseLinkWireError skip_value(const uint8_t *input, size_t input_size, size_t *offset,
                                    unsigned wire_type) {
    uint64_t ignored;
    if (wire_type == 0) return read_varint(input, input_size, offset, &ignored);
    if (wire_type == 1) {
        if (input_size - *offset < 8) return MUSE_LINK_WIRE_INVALID_MESSAGE;
        *offset += 8;
        return MUSE_LINK_WIRE_OK;
    }
    if (wire_type == 2) {
        const uint8_t *ignored_bytes;
        size_t ignored_size;
        return read_delimited(input, input_size, offset, &ignored_bytes, &ignored_size);
    }
    if (wire_type == 5) {
        if (input_size - *offset < 4) return MUSE_LINK_WIRE_INVALID_MESSAGE;
        *offset += 4;
        return MUSE_LINK_WIRE_OK;
    }
    return MUSE_LINK_WIRE_INVALID_MESSAGE;
}

MuseLinkWireError muse_link_decode_noise_frame(
    const uint8_t *input, size_t input_size, MuseLinkNoiseFrame *frame) {
    size_t offset = 0;
    if (input == NULL || frame == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    frame->total_chunks = 1;
    while (offset < input_size) {
        unsigned field, wire_type;
        MuseLinkWireError result = read_key(input, input_size, &offset, &field, &wire_type);
        if (result != MUSE_LINK_WIRE_OK) return result;
        if (field == 1 || field == 2 || field == 3) {
            uint64_t value;
            if (wire_type != 0 || read_varint(input, input_size, &offset, &value) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
            if (field == 1) frame->chunk_id = (int64_t)value;
            else if (field == 2) {
                if (value > UINT32_MAX) return MUSE_LINK_WIRE_INVALID_MESSAGE;
                frame->chunk_index = (uint32_t)value;
            } else {
                if (value > UINT32_MAX) return MUSE_LINK_WIRE_INVALID_MESSAGE;
                frame->total_chunks = (uint32_t)value;
            }
        } else if (field == 4) {
            if (wire_type != 2 || read_delimited(input, input_size, &offset,
                                                  &frame->payload, &frame->payload_size) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
        } else if (skip_value(input, input_size, &offset, wire_type) != MUSE_LINK_WIRE_OK) {
            return MUSE_LINK_WIRE_INVALID_MESSAGE;
        }
    }
    if (frame->total_chunks == 0 || frame->total_chunks > MUSE_LINK_MAX_NOISE_CHUNKS ||
        frame->chunk_index >= frame->total_chunks || frame->payload_size > MUSE_LINK_MAX_NOISE_CHUNK_BYTES) {
        return MUSE_LINK_WIRE_INVALID_MESSAGE;
    }
    return MUSE_LINK_WIRE_OK;
}

static MuseLinkWireError decode_response_body(const uint8_t *input, size_t input_size,
                                              MuseLinkIncoming *incoming) {
    size_t offset = 0;
    while (offset < input_size) {
        unsigned field, wire_type;
        uint64_t value;
        MuseLinkWireError result = read_key(input, input_size, &offset, &field, &wire_type);
        if (result != MUSE_LINK_WIRE_OK) return result;
        if (field == 1) {
            if (wire_type != 0 || read_varint(input, input_size, &offset, &value) != MUSE_LINK_WIRE_OK || value > INT32_MAX) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
            incoming->status = (int)value;
        } else if (field == 3) {
            if (wire_type != 2 || read_delimited(input, input_size, &offset,
                                                  &incoming->body, &incoming->body_size) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
        } else if (field == 4) {
            if (wire_type != 0 || read_varint(input, input_size, &offset, &value) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
            incoming->end_body = value != 0;
        } else if (skip_value(input, input_size, &offset, wire_type) != MUSE_LINK_WIRE_OK) {
            return MUSE_LINK_WIRE_INVALID_MESSAGE;
        }
    }
    return MUSE_LINK_WIRE_OK;
}

static MuseLinkWireError decode_body_chunk(const uint8_t *input, size_t input_size,
                                           MuseLinkIncoming *incoming) {
    size_t offset = 0;
    while (offset < input_size) {
        unsigned field, wire_type;
        uint64_t value;
        MuseLinkWireError result = read_key(input, input_size, &offset, &field, &wire_type);
        if (result != MUSE_LINK_WIRE_OK) return result;
        if (field == 1) {
            if (wire_type != 2 || read_delimited(input, input_size, &offset,
                                                  &incoming->body, &incoming->body_size) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
        } else if (field == 2) {
            if (wire_type != 0 || read_varint(input, input_size, &offset, &value) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
            incoming->end_body = value != 0;
        } else if (skip_value(input, input_size, &offset, wire_type) != MUSE_LINK_WIRE_OK) {
            return MUSE_LINK_WIRE_INVALID_MESSAGE;
        }
    }
    return MUSE_LINK_WIRE_OK;
}

static MuseLinkWireError decode_reset(const uint8_t *input, size_t input_size,
                                      MuseLinkIncoming *incoming) {
    size_t offset = 0;
    while (offset < input_size) {
        unsigned field, wire_type;
        uint64_t value;
        MuseLinkWireError result = read_key(input, input_size, &offset, &field, &wire_type);
        if (result != MUSE_LINK_WIRE_OK) return result;
        if (field == 1) {
            if (wire_type != 0 || read_varint(input, input_size, &offset, &value) != MUSE_LINK_WIRE_OK || value > INT32_MAX) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
            incoming->reset_code = (int)value;
        } else if (skip_value(input, input_size, &offset, wire_type) != MUSE_LINK_WIRE_OK) {
            return MUSE_LINK_WIRE_INVALID_MESSAGE;
        }
    }
    return MUSE_LINK_WIRE_OK;
}

static MuseLinkWireError decode_service_envelope(const uint8_t *input, size_t input_size,
                                                 unsigned envelope_field,
                                                 MuseLinkIncoming *incoming) {
    const uint8_t *service_payload = NULL;
    size_t service_payload_size = 0;
    size_t offset = 0;
    if (input == NULL || incoming == NULL) return MUSE_LINK_WIRE_INVALID_ARGUMENT;
    memset(incoming, 0, sizeof(*incoming));
    while (offset < input_size) {
        unsigned field, wire_type;
        MuseLinkWireError result = read_key(input, input_size, &offset, &field, &wire_type);
        if (result != MUSE_LINK_WIRE_OK) return result;
        if (field == envelope_field) {
            if (wire_type != 2 || read_delimited(input, input_size, &offset,
                                                  &service_payload, &service_payload_size) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
        } else if (skip_value(input, input_size, &offset, wire_type) != MUSE_LINK_WIRE_OK) {
            return MUSE_LINK_WIRE_INVALID_MESSAGE;
        }
    }
    if (service_payload == NULL) return MUSE_LINK_WIRE_INVALID_MESSAGE;
    offset = 0;
    while (offset < service_payload_size) {
        unsigned field, wire_type;
        const uint8_t *value_bytes;
        size_t value_size;
        MuseLinkWireError result = read_key(service_payload, service_payload_size, &offset, &field, &wire_type);
        if (result != MUSE_LINK_WIRE_OK) return result;
        if (field == 1) {
            uint64_t stream_id;
            if (wire_type != 0 || read_varint(service_payload, service_payload_size, &offset, &stream_id) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
            incoming->stream_id = (int64_t)stream_id;
        } else if (field >= 3 && field <= 5) {
            if (wire_type != 2 || read_delimited(service_payload, service_payload_size, &offset,
                                                  &value_bytes, &value_size) != MUSE_LINK_WIRE_OK) {
                return MUSE_LINK_WIRE_INVALID_MESSAGE;
            }
            if (field == 3) {
                incoming->kind = MUSE_LINK_INCOMING_RESPONSE;
                result = decode_response_body(value_bytes, value_size, incoming);
            } else if (field == 4) {
                incoming->kind = MUSE_LINK_INCOMING_BODY_CHUNK;
                result = decode_body_chunk(value_bytes, value_size, incoming);
            } else {
                incoming->kind = MUSE_LINK_INCOMING_RESET;
                result = decode_reset(value_bytes, value_size, incoming);
            }
            if (result != MUSE_LINK_WIRE_OK) return result;
        } else if (skip_value(service_payload, service_payload_size, &offset, wire_type) != MUSE_LINK_WIRE_OK) {
            return MUSE_LINK_WIRE_INVALID_MESSAGE;
        }
    }
    return MUSE_LINK_WIRE_OK;
}

MuseLinkWireError muse_link_decode_service_response(
    const uint8_t *input, size_t input_size, MuseLinkIncoming *incoming) {
    return decode_service_envelope(input, input_size, 1, incoming);
}

MuseLinkWireError muse_link_decode_service_request(
    const uint8_t *input, size_t input_size, MuseLinkIncoming *incoming) {
    return decode_service_envelope(input, input_size, 2, incoming);
}
