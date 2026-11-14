#ifndef MUSE_JSON_EXTRACT_H
#define MUSE_JSON_EXTRACT_H

#include <stddef.h>

/* Small bounded extractor for the fixed Muse API/event fields used on Vita. */
int muse_json_find_string(const char *json, size_t json_size, const char *key,
                          char *output, size_t output_capacity);
int muse_json_find_bool(const char *json, size_t json_size, const char *key, int *value);

#endif
