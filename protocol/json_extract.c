#include "json_extract.h"

#include <string.h>

static size_t skip_space(const char *json, size_t size, size_t offset) {
    while (offset < size && (json[offset] == ' ' || json[offset] == '\n' ||
                             json[offset] == '\r' || json[offset] == '\t')) {
        offset++;
    }
    return offset;
}

static int key_at(const char *json, size_t size, size_t offset, const char *key, size_t key_size) {
    return offset + key_size + 1 < size && json[offset] == '"' &&
           memcmp(json + offset + 1, key, key_size) == 0 &&
           json[offset + key_size + 1] == '"';
}

static int copy_json_string(const char *json, size_t size, size_t *offset,
                            char *output, size_t capacity) {
    size_t out = 0;
    if (*offset >= size || json[*offset] != '"' || capacity == 0) return 0;
    (*offset)++;
    while (*offset < size) {
        unsigned char c = (unsigned char)json[(*offset)++];
        int escaped = 0;
        if (c == '"') {
            output[out] = '\0';
            return 1;
        }
        if (c == '\\') {
            escaped = 1;
            if (*offset >= size) return 0;
            c = (unsigned char)json[(*offset)++];
            if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' ||
                c == 'n' || c == 'r' || c == 't') {
                if (c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') {
                    c = (c == 'n') ? '\n' : (c == 'r') ? '\r' : (c == 't') ? '\t' : ' ';
                }
            } else {
                return 0;
            }
        }
        if ((!escaped && c < 0x20) || out + 1 >= capacity) return 0;
        output[out++] = (char)c;
    }
    return 0;
}

int muse_json_find_string(const char *json, size_t json_size, const char *key,
                          char *output, size_t output_capacity) {
    size_t key_size;
    if (json == NULL || key == NULL || output == NULL || output_capacity == 0) return 0;
    key_size = strlen(key);
    for (size_t i = 0; i + key_size + 2 < json_size; i++) {
        size_t value_offset;
        if (!key_at(json, json_size, i, key, key_size)) continue;
        value_offset = skip_space(json, json_size, i + key_size + 2);
        if (value_offset >= json_size || json[value_offset] != ':') continue;
        value_offset = skip_space(json, json_size, value_offset + 1);
        if (copy_json_string(json, json_size, &value_offset, output, output_capacity)) return 1;
    }
    return 0;
}

int muse_json_find_bool(const char *json, size_t json_size, const char *key, int *value) {
    size_t key_size;
    if (json == NULL || key == NULL || value == NULL) return 0;
    key_size = strlen(key);
    for (size_t i = 0; i + key_size + 2 < json_size; i++) {
        size_t value_offset;
        if (!key_at(json, json_size, i, key, key_size)) continue;
        value_offset = skip_space(json, json_size, i + key_size + 2);
        if (value_offset >= json_size || json[value_offset] != ':') continue;
        value_offset = skip_space(json, json_size, value_offset + 1);
        if (value_offset + 4 <= json_size && memcmp(json + value_offset, "true", 4) == 0) {
            *value = 1;
            return 1;
        }
        if (value_offset + 5 <= json_size && memcmp(json + value_offset, "false", 5) == 0) {
            *value = 0;
            return 1;
        }
    }
    return 0;
}
