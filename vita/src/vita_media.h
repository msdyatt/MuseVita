#ifndef MUSEVITA_VITA_MEDIA_H
#define MUSEVITA_VITA_MEDIA_H

#include <stddef.h>
#include <stdint.h>

/* The functions are deliberately one-shot and bounded. They do not retain
   camera or microphone handles between calls, which keeps the transport/UI
   lifecycle simple on a Vita with limited memory. */
int muse_vita_capture_photo_jpeg(uint8_t *output, size_t capacity, size_t *output_size);
int muse_vita_capture_voice_wav(uint8_t *output, size_t capacity, size_t *output_size);

#endif
