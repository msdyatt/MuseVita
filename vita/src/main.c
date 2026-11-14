/*
 * Muse Vita - initial native UI scaffold (not yet connected to Muse).
 * Copyright (c) 2026 Sam Dyatt and Muse Vita contributors. MIT license.
 */
#include <stdint.h>
#include <stdbool.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#define WHITE RGBA8(246, 246, 248, 255)
#define MUTED RGBA8(166, 172, 196, 255)
#define BG RGBA8(11, 14, 27, 255)
#define PANEL RGBA8(25, 31, 51, 255)
#define ACCENT RGBA8(130, 110, 245, 255)
#define BORDER RGBA8(54, 60, 82, 255)

static void text(vita2d_pgf *font, int x, int y, float size, uint32_t color, const char *s) {
    vita2d_pgf_draw_text(font, x, y, color, size, s);
}

static void draw(vita2d_pgf *font, int page) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_draw_rectangle(0, 0, 960, 544, BG);
    vita2d_draw_rectangle(0, 0, 960, 8, ACCENT);
    text(font, 42, 74, 38, WHITE, "MUSE VITA");
    text(font, 44, 106, 18, MUTED, "An unofficial PS Vita client for Muse");

    vita2d_draw_rectangle(42, 138, 876, 315, PANEL);
    vita2d_draw_rectangle(42, 138, 6, 315, ACCENT);
    if (page == 0) {
        text(font, 70, 192, 29, WHITE, "Welcome");
        text(font, 70, 233, 19, MUTED, "Native Vita application scaffold: ready for protocol work.");
        text(font, 70, 271, 19, WHITE, "Next: implement Noise XX and direct Muse networking.");
        text(font, 70, 307, 19, WHITE, "Pairing via QR is planned, not active in this build.");
        vita2d_draw_rectangle(70, 347, 820, 60, BORDER);
        text(font, 93, 386, 22, WHITE, "Press X to view setup requirements");
    } else {
        text(font, 70, 192, 29, WHITE, "Setup requirements");
        text(font, 70, 239, 20, WHITE, "1. A homebrew-capable PS Vita with Wi-Fi");
        text(font, 70, 280, 20, WHITE, "2. Your own Meta Muse Gadget SDK token");
        text(font, 70, 321, 20, WHITE, "3. One-time approved Muse gadget pairing");
        text(font, 70, 376, 18, MUTED, "No access tokens are included in this application.");
    }
    text(font, 44, 506, 18, MUTED, "X  Next / Back");
    text(font, 766, 506, 18, MUTED, "START  Exit");
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

int main(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    vita2d_init();
    vita2d_set_clear_color(BG);
    vita2d_pgf *font = vita2d_load_default_pgf();
    if (!font) {
        vita2d_fini();
        sceKernelExitProcess(1);
        return 1;
    }
    SceCtrlData pad = {0};
    uint32_t previous = 0;
    int page = 0;
    while (true) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t just_pressed = pad.buttons & ~previous;
        previous = pad.buttons;
        if (just_pressed & SCE_CTRL_START) break;
        if (just_pressed & SCE_CTRL_CROSS) page = 1 - page;
        draw(font, page);
        sceKernelDelayThread(16000);
    }
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
