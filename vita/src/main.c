/*
 * MuseVita native UI.
 * Copyright (c) 2026 Sam Dyatt and MuseVita contributors. MIT license.
 *
 * This binary deliberately does not pretend that a local UI state is a live
 * Muse session. The protocol transport is kept behind a separate interface;
 * until pairing, credentials, TLS, WebSocket, and Noise XX are verified, the
 * app reports the real offline/unpaired state.
 */
#include <stdbool.h>
#include <stdint.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include "network_state.h"

#define WHITE RGBA8(246, 246, 248, 255)
#define MUTED RGBA8(166, 172, 196, 255)
#define BG RGBA8(11, 14, 27, 255)
#define PANEL RGBA8(25, 31, 51, 255)
#define ACCENT RGBA8(130, 110, 245, 255)
#define BORDER RGBA8(54, 60, 82, 255)
#define WARNING RGBA8(240, 184, 100, 255)
#define SUCCESS RGBA8(111, 214, 160, 255)

enum {
    PAGE_STATUS = 0,
    PAGE_SETUP = 1,
    PAGE_CHAT = 2,
    PAGE_COUNT = 3,
};

static void text(vita2d_pgf *font, int x, int y, float size, uint32_t color, const char *value) {
    vita2d_pgf_draw_text(font, x, y, color, size, value);
}

static void draw_header(vita2d_pgf *font, MuseVitaNetworkState network) {
    text(font, 42, 74, 38, WHITE, "MUSEVITA");
    text(font, 44, 106, 18, MUTED, "Unofficial PS Vita client for Meta Muse");
    text(font, 720, 74, 18, network == MUSEVITA_NETWORK_ONLINE ? SUCCESS : WARNING,
         muse_vita_network_label(network));
}

static void draw_status_page(vita2d_pgf *font, MuseVitaNetworkState network) {
    text(font, 70, 192, 29, WHITE, "Connection status");
    text(font, 70, 233, 20, network == MUSEVITA_NETWORK_ONLINE ? SUCCESS : WARNING,
         muse_vita_network_label(network));
    text(font, 70, 278, 19, WHITE, "Muse account: not paired");
    text(font, 70, 314, 19, MUTED, "This build will not simulate a chat session.");
    vita2d_draw_rectangle(70, 347, 820, 60, BORDER);
    text(font, 93, 386, 20, WHITE, "Use SETUP to review one-time pairing requirements");
}

static void draw_setup_page(vita2d_pgf *font) {
    text(font, 70, 192, 29, WHITE, "One-time setup");
    text(font, 70, 233, 19, WHITE, "1. Connect the Vita to Wi-Fi");
    text(font, 70, 270, 19, WHITE, "2. Approve Muse Gadget pairing");
    text(font, 70, 307, 19, WHITE, "3. Redeem a short-lived QR session");
    text(font, 70, 344, 19, MUTED, "QR data must never contain account tokens.");
    vita2d_draw_rectangle(70, 370, 820, 44, BORDER);
    text(font, 93, 399, 18, WARNING, "Pairing transport is not active in this build.");
}

static void draw_chat_page(vita2d_pgf *font) {
    text(font, 70, 192, 29, WHITE, "Main chat");
    vita2d_draw_rectangle(70, 220, 820, 150, BORDER);
    text(font, 95, 264, 20, MUTED, "No conversation loaded.");
    text(font, 95, 302, 18, MUTED, "Pair the Vita before sending a message.");
    text(font, 70, 412, 18, WARNING, "IME text entry and live replies are pending.");
}

static void draw(vita2d_pgf *font, int page, MuseVitaNetworkState network) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_draw_rectangle(0, 0, 960, 544, BG);
    vita2d_draw_rectangle(0, 0, 960, 8, ACCENT);
    draw_header(font, network);
    vita2d_draw_rectangle(42, 138, 876, 315, PANEL);
    vita2d_draw_rectangle(42, 138, 6, 315, ACCENT);
    if (page == PAGE_STATUS) draw_status_page(font, network);
    else if (page == PAGE_SETUP) draw_setup_page(font);
    else draw_chat_page(font);
    text(font, 44, 506, 18, MUTED, "X  Next page");
    text(font, 500, 506, 18, MUTED, "CIRCLE  Previous");
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

    bool network_ready = muse_vita_network_init() == 0;
    MuseVitaNetworkProvider network_provider = muse_vita_default_network_provider();
    SceCtrlData pad = {0};
    uint32_t previous = 0;
    int page = PAGE_STATUS;
    while (true) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t just_pressed = pad.buttons & ~previous;
        previous = pad.buttons;
        if (just_pressed & SCE_CTRL_START) break;
        if (just_pressed & SCE_CTRL_CROSS) page = (page + 1) % PAGE_COUNT;
        if (just_pressed & SCE_CTRL_CIRCLE) page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
        MuseVitaNetworkState network = network_ready
                                           ? network_provider.probe(network_provider.context)
                                           : MUSEVITA_NETWORK_UNKNOWN;
        draw(font, page, network);
        sceKernelDelayThread(16000);
    }
    if (network_ready) muse_vita_network_term();
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
