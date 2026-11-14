#include "json_extract.h"

#include <stdint.h>
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
    return muse_json_find_string_range(json, json_size, 0, json_size, key,
                                       output, output_capacity, NULL, NULL);
}

int muse_json_find_string_range(const char *json, size_t json_size,
                                size_t start_offset, size_t end_offset,
                                const char *key, char *output,
                                size_t output_capacity, size_t *key_offset,
                                size_t *next_offset) {
    size_t key_size;
    if (json == NULL || key == NULL || output == NULL || output_capacity == 0 ||
        start_offset > json_size) return 0;
    if (end_offset > json_size) end_offset = json_size;
    if (start_offset >= end_offset) return 0;
    key_size = strlen(key);
    for (size_t i = start_offset; i + key_size + 2 < end_offset; i++) {
        size_t value_offset;
        if (!key_at(json, json_size, i, key, key_size)) continue;
        value_offset = skip_space(json, end_offset, i + key_size + 2);
        if (value_offset >= end_offset || json[value_offset] != ':') continue;
        value_offset = skip_space(json, end_offset, value_offset + 1);
        if (copy_json_string(json, end_offset, &value_offset, output, output_capacity)) {
            if (key_offset != NULL) *key_offset = i;
            if (next_offset != NULL) *next_offset = value_offset;
            return 1;
        }
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

int muse_json_find_int64(const char *json, size_t json_size, const char *key, int64_t *value) {
    size_t key_size;
    if (json == NULL || key == NULL || value == NULL) return 0;
    key_size = strlen(key);
    for (size_t i = 0; i + key_size + 2 < json_size; i++) {
        size_t offset;
        int negative = 0;
        uint64_t magnitude = 0;
        if (!key_at(json, json_size, i, key, key_size)) continue;
        offset = skip_space(json, json_size, i + key_size + 2);
        if (offset >= json_size || json[offset] != ':') continue;
        offset = skip_space(json, json_size, offset + 1);
        if (offset < json_size && json[offset] == '-') {
            negative = 1;
            offset++;
        }
        if (offset >= json_size || json[offset] < '0' || json[offset] > '9') continue;
        while (offset < json_size && json[offset] >= '0' && json[offset] <= '9') {
            unsigned digit = (unsigned)(json[offset++] - '0');
            if (magnitude > (UINT64_MAX - digit) / 10u) {
                magnitude = UINT64_MAX;
                break;
            }
            magnitude = magnitude * 10u + digit;
        }
        if (negative) {
            if (magnitude > (uint64_t)INT64_MAX + 1u) continue;
            *value = magnitude == (uint64_t)INT64_MAX + 1u
                         ? INT64_MIN : -(int64_t)magnitude;
        } else {
            if (magnitude > (uint64_t)INT64_MAX) continue;
            *value = (int64_t)magnitude;
        }
        return 1;
    }
    return 0;
}

int muse_json_has_key(const char *json, size_t json_size, const char *key) {
    size_t key_size;
    if (json == NULL || key == NULL) return 0;
    key_size = strlen(key);
    for (size_t i = 0; i + key_size + 2 < json_size; i++) {
        size_t value_offset;
        if (!key_at(json, json_size, i, key, key_size)) continue;
        value_offset = skip_space(json, json_size, i + key_size + 2);
        if (value_offset < json_size && json[value_offset] == ':') return 1;
    }
    return 0;
}

static size_t json_string_end(const char *json, size_t size, size_t offset) {
    int escaped = 0;
    if (offset >= size || json[offset] != '"') return 0;
    for (offset++; offset < size; offset++) {
        unsigned char c = (unsigned char)json[offset];
        if (escaped) {
            escaped = 0;
        } else if (c == '\\') {
            escaped = 1;
        } else if (c == '"') {
            return offset + 1;
        }
    }
    return 0;
}

static size_t json_value_start(const char *json, size_t size, size_t start,
                               size_t end, const char *key) {
    size_t key_size = strlen(key);
    for (size_t i = start; i + key_size + 2 < end; i++) {
        size_t value;
        if (!key_at(json, size, i, key, key_size)) continue;
        value = skip_space(json, end, i + key_size + 2);
        if (value < end && json[value] == ':') {
            value = skip_space(json, end, value + 1);
            if (value < end) return value;
        }
    }
    return 0;
}

static size_t json_container_end(const char *json, size_t size, size_t offset,
                                 char opening, char closing) {
    size_t depth = 0;
    int in_string = 0;
    int escaped = 0;
    if (offset >= size || json[offset] != opening) return 0;
    for (size_t i = offset; i < size; i++) {
        unsigned char c = (unsigned char)json[i];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
            continue;
        }
        if (c == '"') {
            in_string = 1;
        } else if (c == opening) {
            depth++;
        } else if (c == closing) {
            if (--depth == 0) return i + 1;
        }
    }
    return 0;
}

static int append_json_text(const char *json, size_t size, size_t start,
                            size_t end, char *output, size_t capacity,
                            int *has_text) {
    char text[512];
    size_t text_size;
    if (!muse_json_find_string_range(json, size, start, end, "text", text,
                                     sizeof(text), NULL, NULL) || text[0] == '\0') {
        return 0;
    }
    text_size = strlen(text);
    if (*has_text) {
        if (strlen(output) + 1 >= capacity) return 0;
        strcat(output, "\n");
    }
    if (strlen(output) + text_size + 1 > capacity) return 0;
    strcat(output, text);
    *has_text = 1;
    return 1;
}

static int append_text_items(const char *json, size_t size, size_t array_start,
                             size_t array_end, char *output, size_t capacity,
                             int *has_text) {
    size_t cursor = array_start + 1;
    while (cursor + 1 < array_end) {
        size_t item_end;
        cursor = skip_space(json, array_end - 1, cursor);
        if (cursor >= array_end - 1 || json[cursor] == ']') break;
        if (json[cursor] == '{') {
            item_end = json_container_end(json, array_end, cursor, '{', '}');
            if (item_end == 0 || item_end > array_end) return 0;
            append_json_text(json, size, cursor, item_end, output, capacity, has_text);
            cursor = item_end;
        } else if (json[cursor] == '"') {
            item_end = json_string_end(json, array_end, cursor);
            if (item_end == 0) return 0;
            cursor = item_end;
        } else {
            cursor++;
        }
        cursor = skip_space(json, array_end - 1, cursor);
        if (cursor < array_end - 1 && json[cursor] == ',') cursor++;
    }
    return *has_text;
}

static int append_assistant_messages(const char *json, size_t size,
                                     size_t messages_start, size_t messages_end,
                                     char *output, size_t capacity, int *has_text) {
    size_t cursor = messages_start + 1;
    while (cursor + 1 < messages_end) {
        size_t message_end;
        char role[32];
        cursor = skip_space(json, messages_end - 1, cursor);
        if (cursor >= messages_end - 1 || json[cursor] == ']') break;
        if (json[cursor] != '{') {
            cursor++;
            continue;
        }
        message_end = json_container_end(json, messages_end, cursor, '{', '}');
        if (message_end == 0 || message_end > messages_end) return 0;
        if (muse_json_find_string_range(json, size, cursor, message_end, "role",
                                        role, sizeof(role), NULL, NULL) &&
            strcmp(role, "assistant") == 0) {
            size_t content = json_value_start(json, size, cursor, message_end, "content");
            if (content < message_end && json[content] == '[') {
                size_t content_end = json_container_end(json, message_end, content, '[', ']');
                if (content_end == 0 || !append_text_items(json, size, content,
                                                           content_end, output,
                                                           capacity, has_text)) {
                    return 0;
                }
            }
        }
        cursor = message_end;
        cursor = skip_space(json, messages_end - 1, cursor);
        if (cursor < messages_end - 1 && json[cursor] == ',') cursor++;
    }
    return *has_text;
}

int muse_json_find_assistant_text(const char *json, size_t json_size,
                                  char *output, size_t output_capacity) {
    size_t cursor = 0;
    int has_text = 0;
    if (json == NULL || output == NULL || output_capacity == 0) return 0;
    output[0] = '\0';
    while (cursor < json_size) {
        size_t object_start = json_value_start(json, json_size, cursor, json_size, "transcript");
        if (object_start == 0) break;
        if (json[object_start] == '{') {
            size_t object_end = json_container_end(json, json_size, object_start, '{', '}');
            size_t messages = object_end == 0 ? 0 : json_value_start(json, json_size,
                                                                       object_start,
                                                                       object_end,
                                                                       "messages");
            if (messages != 0 && json[messages] == '[') {
                size_t messages_end = json_container_end(json, object_end, messages, '[', ']');
                if (messages_end != 0) {
                    append_assistant_messages(json, json_size, messages, messages_end,
                                              output, output_capacity, &has_text);
                }
            }
        }
        cursor = object_start + 1;
    }
    if (has_text) return 1;

    cursor = json_value_start(json, json_size, 0, json_size, "content");
    if (cursor < json_size && json[cursor] == '[') {
        size_t content_end = json_container_end(json, json_size, cursor, '[', ']');
        if (content_end != 0) {
            append_text_items(json, json_size, cursor, content_end, output,
                              output_capacity, &has_text);
        }
    }
    return has_text;
}
