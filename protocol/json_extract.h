#ifndef MUSE_JSON_EXTRACT_H
#define MUSE_JSON_EXTRACT_H

#include <stddef.h>
#include <stdint.h>

/* Small bounded extractor for the fixed Muse API/event fields used on Vita. */
int muse_json_find_string(const char *json, size_t json_size, const char *key,
                          char *output, size_t output_capacity);
/* Find a string member within [start_offset, end_offset). The optional
   offsets identify the member key and the byte after its decoded value. */
int muse_json_find_string_range(const char *json, size_t json_size,
                                size_t start_offset, size_t end_offset,
                                const char *key, char *output,
                                size_t output_capacity, size_t *key_offset,
                                size_t *next_offset);
int muse_json_find_bool(const char *json, size_t json_size, const char *key, int *value);
int muse_json_find_int64(const char *json, size_t json_size, const char *key, int64_t *value);
int muse_json_has_key(const char *json, size_t json_size, const char *key);

/* Find one object in an array value. When match_key is NULL, the first object
   is returned; otherwise the first object whose boolean field equals
   match_value is returned. The range is half-open and remains inside the
   original JSON buffer. */
int muse_json_find_array_object_range(const char *json, size_t json_size,
                                      const char *array_key, const char *match_key,
                                      int match_value, size_t *object_start,
                                      size_t *object_end);
/* Extract assistant text from the structured transcript/content forms used by
   Muse chat events. The result is bounded and NUL terminated. */
int muse_json_find_assistant_text(const char *json, size_t json_size,
                                  char *output, size_t output_capacity);

#endif
