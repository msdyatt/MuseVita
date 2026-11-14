#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "noise_xx.h"

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static size_t parse_hex(const char *value, uint8_t *output, size_t capacity) {
    size_t length = 0;
    while (value[length] != '\0' && value[length] != '\n' && value[length] != '\r' &&
           value[length] != ' ' && value[length] != '\t') length++;
    if ((length & 1u) != 0 || length / 2u > capacity) return 0;
    for (size_t i = 0; i < length / 2u; i++) {
        int high = hex_value(value[i * 2u]);
        int low = hex_value(value[i * 2u + 1u]);
        if (high < 0 || low < 0) return 0;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return length / 2u;
}

static void print_hex(const char *label, const uint8_t *value, size_t size) {
    printf("%s ", label);
    for (size_t i = 0; i < size; i++) printf("%02x", value[i]);
    putchar('\n');
    fflush(stdout);
}

int main(void) {
    MuseNoiseXX noise;
    uint8_t message1[MUSE_NOISE_KEY_BYTES];
    uint8_t message2[512];
    uint8_t message3[512];
    uint8_t ciphertext[512];
    uint8_t plaintext[512];
    size_t message3_size = 0;
    size_t ciphertext_size = 0;
    size_t plaintext_size = 0;
    char line[2048];

    if (muse_noise_xx_init(&noise) != MUSE_NOISE_OK ||
        muse_noise_xx_write_message1(&noise, message1) != MUSE_NOISE_OK) return 1;
    print_hex("msg1", message1, sizeof(message1));

    size_t message2_size = 0;
    if (fgets(line, sizeof(line), stdin) == NULL || strncmp(line, "msg2 ", 5) != 0 ||
        (message2_size = parse_hex(line + 5, message2, sizeof(message2))) == 0 ||
        muse_noise_xx_read_message2(&noise, message2, message2_size) != MUSE_NOISE_OK) {
        fprintf(stderr, "read_message2 failed (size=%lu)\n", (unsigned long)message2_size);
        muse_noise_xx_free(&noise);
        return 2;
    }
    if (muse_noise_xx_write_message3(&noise, message3, sizeof(message3), &message3_size) != MUSE_NOISE_OK) {
        muse_noise_xx_free(&noise);
        return 3;
    }
    print_hex("msg3", message3, message3_size);

    if (fgets(line, sizeof(line), stdin) == NULL || strncmp(line, "encrypt ", 8) != 0) {
        muse_noise_xx_free(&noise);
        return 4;
    }
    size_t message_size = parse_hex(line + 8, plaintext, sizeof(plaintext));
    if (muse_noise_xx_encrypt(&noise, plaintext, message_size, ciphertext,
                              sizeof(ciphertext), &ciphertext_size) != MUSE_NOISE_OK) {
        muse_noise_xx_free(&noise);
        return 5;
    }
    print_hex("client_ct", ciphertext, ciphertext_size);

    if (fgets(line, sizeof(line), stdin) == NULL || strncmp(line, "server ", 7) != 0) {
        muse_noise_xx_free(&noise);
        return 6;
    }
    ciphertext_size = parse_hex(line + 7, ciphertext, sizeof(ciphertext));
    if (muse_noise_xx_decrypt(&noise, ciphertext, ciphertext_size, plaintext,
                              sizeof(plaintext), &plaintext_size) != MUSE_NOISE_OK) {
        muse_noise_xx_free(&noise);
        return 7;
    }
    print_hex("server_pt", plaintext, plaintext_size);
    muse_noise_xx_free(&noise);
    return 0;
}
