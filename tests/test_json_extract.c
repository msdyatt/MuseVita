#include <assert.h>
#include <string.h>

#include "json_extract.h"

int main(void) {
    const char json[] =
        "{\"vm_list\":[{\"vm_ws_url\":\"wss://vm.example\","
        "\"vm_auth_token\":\"secret\",\"vm_id\":\"vm-1\",\"default\":true}],"
        "\"type\":\"event\",\"event\":\"delta.text_append\","
        "\"payload\":{\"text\":\"hello\\nworld\"}}";
    char value[128];
    int boolean = 0;
    assert(muse_json_find_string(json, strlen(json), "vm_ws_url", value, sizeof(value)));
    assert(strcmp(value, "wss://vm.example") == 0);
    assert(muse_json_find_string(json, strlen(json), "event", value, sizeof(value)));
    assert(strcmp(value, "delta.text_append") == 0);
    assert(muse_json_find_string(json, strlen(json), "text", value, sizeof(value)));
    assert(strcmp(value, "hello\nworld") == 0);
    assert(muse_json_find_bool(json, strlen(json), "default", &boolean) && boolean == 1);
    assert(muse_json_has_key(json, strlen(json), "payload"));
    assert(!muse_json_has_key(json, strlen(json), "error"));
    return 0;
}
