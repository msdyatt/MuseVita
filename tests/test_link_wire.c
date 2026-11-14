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

    assert(muse_link_build_chat_request("hello", "homelink-000001",
                                       json, sizeof(json), &json_size) == MUSE_LINK_WIRE_OK);
    json[json_size] = '\0';
    puts(json);
    return 0;
}
