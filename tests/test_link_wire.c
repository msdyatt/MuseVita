#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "link_wire.h"

int main(void) {
    uint8_t proto[512];
    size_t proto_size = 0;
    char json[1024];
    size_t json_size = 0;

    assert(muse_link_encode_open_stream_request(1, proto, sizeof(proto), &proto_size) == MUSE_LINK_WIRE_OK);
    assert(proto_size > 2 && proto[0] == 0x12 && proto[1] == 0x19);

    assert(muse_link_build_register_message("req-1", "homelink-000001", "MuseVita",
                                           json, sizeof(json), &json_size) == MUSE_LINK_WIRE_OK);
    json[json_size] = '\0';
    puts(json);
    assert(strstr(json, "link.register") != NULL);
    assert(strstr(json, "homelink-000001") != NULL);
    assert(strstr(json, "musevita-psvita") != NULL);
    assert(strstr(json, "device.guide") != NULL);
    assert(strstr(json, "vita.camera_capture") != NULL);
    assert(strstr(json, "vita.microphone_record") != NULL);

    assert(muse_link_build_chat_request("hello", "homelink-000001",
                                       json, sizeof(json), &json_size) == MUSE_LINK_WIRE_OK);
    json[json_size] = '\0';
    puts(json);

    {
        static const uint8_t response[] = {
            0x0a, 0x0d, 0x08, 0x01, 0x1a, 0x09,
            0x08, 0xc8, 0x01, 0x1a, 0x02, 'o', 'k', 0x20, 0x01,
        };
        MuseLinkIncoming incoming;
        assert(muse_link_decode_service_response(response, sizeof(response), &incoming) == MUSE_LINK_WIRE_OK);
        assert(incoming.kind == MUSE_LINK_INCOMING_RESPONSE);
        assert(incoming.stream_id == 1);
        assert(incoming.status == 200);
        assert(incoming.end_body == 1);
        assert(incoming.body_size == 2 && memcmp(incoming.body, "ok", 2) == 0);
    }

    {
        static const uint8_t noise_frame[] = {0x08, 0x07, 0x18, 0x01, 0x22, 0x03, 'a', 'b', 'c'};
        MuseLinkNoiseFrame frame;
        assert(muse_link_decode_noise_frame(noise_frame, sizeof(noise_frame), &frame) == MUSE_LINK_WIRE_OK);
        assert(frame.chunk_id == 7 && frame.chunk_index == 0 && frame.total_chunks == 1);
        assert(frame.payload_size == 3 && memcmp(frame.payload, "abc", 3) == 0);
    }

    {
        const MuseLinkHeader headers[] = {
            {"Content-Type", "application/json"},
            {"x-request-id", "req-1"},
        };
        assert(muse_link_encode_http_request(2, "POST", "/chat/subscribe",
                                             headers, 2, (const uint8_t *)"{}", 2, 1,
                                             proto, sizeof(proto), &proto_size) == MUSE_LINK_WIRE_OK);
        assert(proto_size > 20);
    }
    {
        uint8_t large_body[60000];
        uint8_t large_output[70000];
        memset(large_body, 'x', sizeof(large_body));
        assert(muse_link_encode_http_request(3, "POST", "/chat/stream", NULL, 0,
                                             large_body, sizeof(large_body), 1,
                                             large_output, sizeof(large_output), &proto_size) == MUSE_LINK_WIRE_OK);
        assert(proto_size > sizeof(large_body));
    }
    {
        static const uint8_t media[] = {0x01, 0x02, 0x03, 0xfa};
        assert(muse_link_build_chat_media_request("look", "homelink-000001", "image",
                                                  "image/jpeg", "vita-camera.jpg",
                                                  media, sizeof(media), json, sizeof(json), &json_size) == MUSE_LINK_WIRE_OK);
        json[json_size] = '\0';
        assert(strstr(json, "\"items\":[{\"type\":\"text\"") != NULL);
        assert(strstr(json, "\"data_base64\":\"AQID+g==\"") != NULL);
    }
    {
        assert(muse_link_build_command_result("invoke-1", 1, NULL, "image", 1234,
                                              json, sizeof(json), &json_size) == MUSE_LINK_WIRE_OK);
        json[json_size] = '\0';
        assert(strstr(json, "\"method\":\"link.result\"") != NULL);
        assert(strstr(json, "\"bytes\":1234") != NULL);
    }
    {
        assert(muse_link_build_guide_result("invoke-guide", json, sizeof(json), &json_size) == MUSE_LINK_WIRE_OK);
        json[json_size] = '\0';
        assert(strstr(json, "\"format\":\"markdown\"") != NULL);
        assert(strstr(json, "MuseVita") != NULL);
    }
    return 0;
}
