#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "link_wire.h"

static void emit_hex(const char *name, const uint8_t *data, size_t size) {
    printf("%s ", name);
    for (size_t i = 0; i < size; i++) printf("%02x", data[i]);
    putchar('\n');
}

int main(void) {
    uint8_t output[70000];
    size_t output_size = 0;
    const MuseLinkHeader headers[] = {
        {"Content-Type", "application/json"},
        {"x-request-id", "req-1"},
        {"x-app-id", "musegadget"},
    };
    const uint8_t body[] = "{\"message\":\"hello\",\"output_modality\":\"text\","
                           "\"device_id\":\"homelink-000001\"}";

    if (muse_link_encode_open_stream_request(1, output, sizeof(output), &output_size) !=
        MUSE_LINK_WIRE_OK) return 1;
    emit_hex("open", output, output_size);
    if (muse_link_encode_http_request(3, "POST", "/chat/stream", headers,
                                      sizeof(headers) / sizeof(headers[0]), body,
                                      sizeof(body) - 1, 1, output, sizeof(output),
                                      &output_size) != MUSE_LINK_WIRE_OK) return 1;
    emit_hex("request", output, output_size);
    if (muse_link_encode_body_chunk(3, (const uint8_t *)"x", 1, 0, output,
                                    sizeof(output), &output_size) != MUSE_LINK_WIRE_OK) return 1;
    emit_hex("body", output, output_size);
    if (muse_link_encode_body_chunk(3, (const uint8_t *)"x", 1, 1, output,
                                    sizeof(output), &output_size) != MUSE_LINK_WIRE_OK) return 1;
    emit_hex("body_end", output, output_size);
    if (muse_link_encode_reset(7, 1, "request timeout", output, sizeof(output),
                               &output_size) != MUSE_LINK_WIRE_OK) return 1;
    emit_hex("reset", output, output_size);
    if (muse_link_encode_noise_frame(42, 0, 1, (const uint8_t *)"abc", 3,
                                     output, sizeof(output), &output_size) != MUSE_LINK_WIRE_OK) return 1;
    emit_hex("noise", output, output_size);
    return 0;
}
