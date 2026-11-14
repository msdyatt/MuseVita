#ifndef MUSE_JSON_EXTRACT_H
#define MUSE_JSON_EXTRACT_H

#include <stddef.h>

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
int muse_json_has_key(const char *json, size_t json_size, const char *key);

#endif
