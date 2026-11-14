#include <assert.h>
#include <stdint.h>
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
    int64_t sequence = 0;
    assert(muse_json_find_string(json, strlen(json), "vm_ws_url", value, sizeof(value)));
    assert(strcmp(value, "wss://vm.example") == 0);
    assert(muse_json_find_string(json, strlen(json), "event", value, sizeof(value)));
    assert(strcmp(value, "delta.text_append") == 0);
    assert(muse_json_find_string(json, strlen(json), "text", value, sizeof(value)));
    assert(strcmp(value, "hello\nworld") == 0);
    {
        size_t key_offset = 0;
        size_t next_offset = 0;
        assert(muse_json_find_string_range(json, strlen(json), 0, strlen(json),
                                           "vm_id", value, sizeof(value),
                                           &key_offset, &next_offset));
        assert(strcmp(value, "vm-1") == 0);
        assert(next_offset > key_offset);
    }
    assert(muse_json_find_bool(json, strlen(json), "default", &boolean) && boolean == 1);
    assert(muse_json_find_int64("{\"seq\":42}", 10, "seq", &sequence) && sequence == 42);
    assert(muse_json_find_int64("{\"seq\":-7}", 10, "seq", &sequence) && sequence == -7);
    assert(muse_json_has_key(json, strlen(json), "payload"));
    assert(!muse_json_has_key(json, strlen(json), "error"));
    {
        const char structured[] =
            "{\"event\":\"message.assistant\",\"transcript\":{"
            "\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"ignore\"}]},"
            "{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"first\"},"
            "{\"type\":\"text\",\"text\":\"second\"}]}]}}";
        assert(muse_json_find_assistant_text(structured, strlen(structured),
                                             value, sizeof(value)));
        assert(strcmp(value, "first\nsecond") == 0);
    }
    {
        const char structured[] =
            "{\"event\":\"message.assistant\",\"content\":["
            "{\"type\":\"text\",\"text\":\"direct\"}]}";
        assert(muse_json_find_assistant_text(structured, strlen(structured),
                                             value, sizeof(value)));
        assert(strcmp(value, "direct") == 0);
    }
    {
        const char multiple_vms[] =
            "{\"vm_list\":["
            "{\"vm_id\":\"first\",\"vm_auth_token\":\"a\",\"default\":false},"
            "{\"vm_id\":\"selected\",\"vm_auth_token\":\"b\",\"default\":true}]}";
        size_t object_start = 0;
        size_t object_end = 0;
        assert(muse_json_find_array_object_range(multiple_vms, strlen(multiple_vms),
                                                 "vm_list", "default", 1,
                                                 &object_start, &object_end));
        assert(muse_json_find_string_range(multiple_vms, strlen(multiple_vms),
                                           object_start, object_end, "vm_id", value,
                                           sizeof(value), NULL, NULL));
        assert(strcmp(value, "selected") == 0);
        assert(muse_json_find_array_object_range(multiple_vms, strlen(multiple_vms),
                                                 "vm_list", NULL, 0,
                                                 &object_start, &object_end));
        assert(muse_json_find_string_range(multiple_vms, strlen(multiple_vms),
                                           object_start, object_end, "vm_id", value,
                                           sizeof(value), NULL, NULL));
        assert(strcmp(value, "first") == 0);
    }
    {
        const char escaped[] = "{\"text\":\"caf\\u00e9 \\ud83d\\ude00\"}";
        const char expected[] = "caf\xc3\xa9 \xf0\x9f\x98\x80";
        assert(muse_json_find_string(escaped, strlen(escaped), "text", value, sizeof(value)));
        assert(strcmp(value, expected) == 0);
    }
    return 0;
}
