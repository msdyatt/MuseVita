#include "vita_media.h"

#include <stdlib.h>
#include <string.h>

#include <psp2/audioin.h>
#include <psp2/camera.h>
#include <psp2/jpegenc.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#define PHOTO_WIDTH 320
#define PHOTO_HEIGHT 240
#define PHOTO_JPEG_CAPACITY (256u * 1024u)
#define VOICE_SAMPLE_RATE 16000
#define VOICE_GRAIN 256
#define VOICE_SECONDS 2
#define VOICE_SAMPLE_COUNT (VOICE_SAMPLE_RATE * VOICE_SECONDS)
#define VOICE_DATA_BYTES (VOICE_SAMPLE_COUNT * 2u)

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

int muse_vita_capture_photo_jpeg(uint8_t *output, size_t capacity, size_t *output_size) {
    const size_t y_size = PHOTO_WIDTH * PHOTO_HEIGHT;
    const size_t chroma_size = ((PHOTO_WIDTH + 1u) / 2u) * ((PHOTO_HEIGHT + 1u) / 2u);
    const size_t camera_data_size = y_size + chroma_size + chroma_size;
    const size_t jpeg_offset = align_up(camera_data_size, 256u);
    const size_t allocation_size = align_up(jpeg_offset + PHOTO_JPEG_CAPACITY, 4096u);
    SceUID memblock = -1;
    void *base = NULL;
    void *encoder_memory = NULL;
    SceJpegEncoderContext encoder = NULL;
    int camera_open = 0;
    int camera_started = 0;
    int result = -1;
    int encoder_context_size;
    SceCameraInfo camera_info;
    SceCameraRead camera_read;
    int encoded_size;

    if (output == NULL || output_size == NULL || capacity < PHOTO_JPEG_CAPACITY) return -1;
    *output_size = 0;
    memblock = sceKernelAllocMemBlock("musevita-camera",
                                      SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
                                      allocation_size, NULL);
    if (memblock < 0 || sceKernelGetMemBlockBase(memblock, &base) < 0) goto cleanup;

    memset(&camera_info, 0, sizeof(camera_info));
    camera_info.size = sizeof(camera_info);
    camera_info.priority = SCE_CAMERA_PRIORITY_SHARE;
    camera_info.format = SCE_CAMERA_FORMAT_YUV420_PLANE;
    camera_info.resolution = SCE_CAMERA_RESOLUTION_320_240;
    camera_info.framerate = SCE_CAMERA_FRAMERATE_5_FPS;
    camera_info.width = PHOTO_WIDTH;
    camera_info.height = PHOTO_HEIGHT;
    camera_info.range = 1;
    camera_info.sizeIBase = y_size;
    camera_info.sizeUBase = chroma_size;
    camera_info.sizeVBase = chroma_size;
    camera_info.pIBase = base;
    camera_info.pUBase = (uint8_t *)base + y_size;
    camera_info.pVBase = (uint8_t *)base + y_size + chroma_size;
    camera_info.pitch = 0;
    camera_info.buffer = 0;

    if (sceCameraOpen(SCE_CAMERA_DEVICE_BACK, &camera_info) < 0) goto cleanup;
    camera_open = 1;
    if (sceCameraStart(SCE_CAMERA_DEVICE_BACK) < 0) goto cleanup;
    camera_started = 1;
    memset(&camera_read, 0, sizeof(camera_read));
    camera_read.size = sizeof(camera_read);
    camera_read.mode = 0;
    if (sceCameraRead(SCE_CAMERA_DEVICE_BACK, &camera_read) < 0) goto cleanup;

    encoder_context_size = sceJpegEncoderGetContextSize();
    if (encoder_context_size <= 0) goto cleanup;
    encoder_memory = malloc((size_t)encoder_context_size);
    if (encoder_memory == NULL) goto cleanup;
    encoder = encoder_memory;
    if (sceJpegEncoderInit(encoder, PHOTO_WIDTH, PHOTO_HEIGHT,
                           SCE_JPEGENC_PIXELFORMAT_YCBCR420,
                           (uint8_t *)base + jpeg_offset, PHOTO_JPEG_CAPACITY) < 0) goto cleanup;
    if (sceJpegEncoderSetCompressionRatio(encoder, 85) < 0) goto cleanup_encoder;
    encoded_size = sceJpegEncoderEncode(encoder, base);
    if (encoded_size <= 0 || (size_t)encoded_size > capacity) goto cleanup_encoder;
    memcpy(output, (uint8_t *)base + jpeg_offset, (size_t)encoded_size);
    *output_size = (size_t)encoded_size;
    result = 0;

cleanup_encoder:
    sceJpegEncoderEnd(encoder);
cleanup:
    free(encoder_memory);
    if (camera_started) sceCameraStop(SCE_CAMERA_DEVICE_BACK);
    if (camera_open) sceCameraClose(SCE_CAMERA_DEVICE_BACK);
    if (memblock >= 0) sceKernelFreeMemBlock(memblock);
    return result;
}

static void put_u16le(uint8_t *output, size_t offset, uint16_t value) {
    output[offset] = (uint8_t)(value & 0xffu);
    output[offset + 1] = (uint8_t)(value >> 8);
}

static void put_u32le(uint8_t *output, size_t offset, uint32_t value) {
    output[offset] = (uint8_t)(value & 0xffu);
    output[offset + 1] = (uint8_t)((value >> 8) & 0xffu);
    output[offset + 2] = (uint8_t)((value >> 16) & 0xffu);
    output[offset + 3] = (uint8_t)(value >> 24);
}

int muse_vita_capture_voice_wav(uint8_t *output, size_t capacity, size_t *output_size) {
    const size_t total_size = 44u + VOICE_DATA_BYTES;
    int port = -1;
    size_t offset = 44;
    if (output == NULL || output_size == NULL || capacity < total_size) return -1;
    *output_size = 0;
    port = sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_VOICE, VOICE_GRAIN,
                              VOICE_SAMPLE_RATE, SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
    if (port < 0) return -1;

    memset(output, 0, total_size);
    memcpy(output, "RIFF", 4);
    put_u32le(output, 4, (uint32_t)(total_size - 8u));
    memcpy(output + 8, "WAVEfmt ", 8);
    put_u32le(output, 16, 16);
    put_u16le(output, 20, 1);
    put_u16le(output, 22, 1);
    put_u32le(output, 24, VOICE_SAMPLE_RATE);
    put_u32le(output, 28, VOICE_SAMPLE_RATE * 2u);
    put_u16le(output, 32, 2);
    put_u16le(output, 34, 16);
    memcpy(output + 36, "data", 4);
    put_u32le(output, 40, VOICE_DATA_BYTES);

    for (size_t samples = 0; samples < VOICE_SAMPLE_COUNT; samples += VOICE_GRAIN) {
        if (sceAudioInInput(port, output + offset) != 0) {
            sceAudioInReleasePort(port);
            return -1;
        }
        offset += VOICE_GRAIN * 2u;
    }
    sceAudioInReleasePort(port);
    *output_size = total_size;
    return 0;
}
