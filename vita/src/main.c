/*
 * MuseVita native UI.
 * Copyright (c) 2026 Sam Dyatt and MuseVita contributors. MIT license.
 *
 * The chat screen is a real native shell with Vita IME input and bounded
 * message history. It deliberately reports the transport state honestly:
 * messages are not fabricated and text is not sent until the authenticated
 * Muse session is connected.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>

#include "chat_state.h"
#include "muse_session.h"
#include "network_state.h"
#include "vita_media.h"

#define WHITE RGBA8(246, 246, 248, 255)
#define MUTED RGBA8(166, 172, 196, 255)
#define BG RGBA8(11, 14, 27, 255)
#define PANEL RGBA8(25, 31, 51, 255)
#define ACCENT RGBA8(130, 110, 245, 255)
#define BORDER RGBA8(54, 60, 82, 255)
#define WARNING RGBA8(240, 184, 100, 255)
#define SUCCESS RGBA8(111, 214, 160, 255)
#define MUSEVITA_PGF_BASE_SIZE 32.0f
#define MUSEVITA_PGF_MIN_SCALE 0.78f
#define MUSEVITA_WEB_URL "https://muse.ai/"

enum {
    PAGE_STATUS = 0,
    PAGE_SETUP = 1,
    PAGE_CHAT = 2,
    PAGE_COUNT = 3,
};

static uint16_t ime_buffer[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static bool ime_open;
static bool app_util_ready;
static char media_status[96];
static const SceWChar16 chat_ime_title[] = {
    'M', 'e', 's', 's', 'a', 'g', 'e', ' ', 't', 'o', ' ', 'M', 'u', 's', 'e', 0
};
static MuseVitaSession muse_session;

static void text(vita2d_pgf *font, int x, int y, float size, uint32_t color, const char *value) {
    /* vita2d PGF takes a scale multiplier, not a pixel size. */
    float scale = size / MUSEVITA_PGF_BASE_SIZE;
    if (scale < MUSEVITA_PGF_MIN_SCALE) scale = MUSEVITA_PGF_MIN_SCALE;
    vita2d_pgf_draw_text(font, x, y, color, scale, value);
}

static void draw_header(vita2d_pgf *font, MuseVitaNetworkState network,
                        const MuseVitaSession *session) {
    text(font, 42, 74, 38, WHITE, "MUSEVITA");
    text(font, 44, 106, 18, MUTED, "Unofficial PS Vita client for Meta Muse");
    text(font, 720, 74, 18, network == MUSEVITA_NETWORK_ONLINE ? SUCCESS : WARNING,
         muse_vita_network_label(network));
    text(font, 720, 106, 16, muse_vita_session_is_connected(session) ? SUCCESS : MUTED,
         muse_vita_session_label(session));
}

static void draw_status_page(vita2d_pgf *font, MuseVitaNetworkState network,
                             const MuseVitaSession *session) {
    text(font, 70, 192, 29, WHITE, "Connection status");
    text(font, 70, 233, 20, network == MUSEVITA_NETWORK_ONLINE ? SUCCESS : WARNING,
         muse_vita_network_label(network));
    text(font, 70, 278, 19, WHITE, muse_vita_session_label(session));
    text(font, 70, 314, 19, MUTED, muse_vita_session_is_connected(session)
                                      ? "Direct Muse transport is active."
                                      : "Import official pairing state to connect directly.");
    vita2d_draw_rectangle(70, 347, 820, 60, BORDER);
    text(font, 93, 386, 20, WHITE, "Use SETUP to review one-time pairing requirements");
    text(font, 70, 438, 18, MUTED, "TRIANGLE opens Muse Web in the Vita browser");
}

static void draw_setup_page(vita2d_pgf *font) {
    text(font, 70, 192, 29, WHITE, "One-time Muse setup");
    text(font, 70, 233, 19, WHITE, "1. Pair the Vita identity with an official BLE host");
    text(font, 70, 270, 19, WHITE, "2. Copy the approved pairing state with VitaShell");
    text(font, 70, 307, 19, WHITE, "3. Use direct Wi-Fi after setup");
    text(font, 70, 344, 19, MUTED, "Bluetooth is needed only during provisioning.");
    vita2d_draw_rectangle(70, 370, 820, 44, BORDER);
    text(font, 93, 399, 18, WARNING, "Pairing transport is not active yet.");
}

static int draw_wrapped(vita2d_pgf *font, int x, int y, int max_chars, int max_lines,
                        uint32_t color, const char *value) {
    char line[72];
    int line_length = 0;
    int lines = 0;
    const char *cursor = value;
    while (*cursor && lines < max_lines) {
        if (*cursor == '\n') {
            line[line_length] = '\0';
            text(font, x, y + lines * 22, 17, color, line);
            line_length = 0;
            cursor++;
            lines++;
            continue;
        }
        if (line_length >= max_chars || line_length >= (int)sizeof(line) - 1) {
            line[line_length] = '\0';
            text(font, x, y + lines * 22, 17, color, line);
            line_length = 0;
            lines++;
            if (lines >= max_lines) break;
        }
        line[line_length++] = *cursor++;
    }
    if (lines < max_lines && line_length > 0) {
        line[line_length] = '\0';
        text(font, x, y + lines * 22, 17, color, line);
        lines++;
    }
    return lines;
}

static void draw_chat_page(vita2d_pgf *font, const MuseVitaChatState *chat) {
    text(font, 70, 192, 29, WHITE, "Main chat");
    text(font, 690, 192, 16, chat->transport_connected ? SUCCESS : WARNING,
         chat->transport_connected ? "CONNECTED" : "OFFLINE");
    vita2d_draw_rectangle(70, 210, 820, 216, BORDER);

    if (chat->message_count == 0) {
        text(font, 95, 257, 20, MUTED, "No conversation loaded.");
        text(font, 95, 293, 17, MUTED, "Messages from Muse will appear here.");
    } else {
        unsigned int first = chat->message_count > 4 ? chat->message_count - 4 : 0;
        int y = 240;
        for (unsigned int i = first; i < chat->message_count; i++) {
            const MuseVitaChatMessage *message = &chat->messages[i];
            text(font, 95, y, 16, message->role == MUSEVITA_CHAT_USER ? ACCENT : SUCCESS,
                 message->role == MUSEVITA_CHAT_USER ? "YOU" : "MUSE");
            y += 21;
            y += draw_wrapped(font, 95, y, 70, 2, WHITE, message->text) * 22 + 10;
            if (y > 408) break;
        }
    }

    if (chat->draft_ready) {
        text(font, 70, 452, 16, WARNING, "DRAFT READY — not sent");
        draw_wrapped(font, 260, 452, 56, 1, MUTED, chat->draft);
    } else {
        text(font, 70, 452, 16, MUTED, "X  Type a message");
    }
    if (media_status[0] != '\0') text(font, 70, 480, 15, MUTED, media_status);
}

static void draw(vita2d_pgf *font, int page, MuseVitaNetworkState network,
                 const MuseVitaChatState *chat, const MuseVitaSession *session) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_draw_rectangle(0, 0, 960, 544, BG);
    vita2d_draw_rectangle(0, 0, 960, 8, ACCENT);
    draw_header(font, network, session);
    vita2d_draw_rectangle(42, 138, 876, 315, PANEL);
    vita2d_draw_rectangle(42, 138, 6, 315, ACCENT);
    if (page == PAGE_STATUS) draw_status_page(font, network, session);
    else if (page == PAGE_SETUP) draw_setup_page(font);
    else draw_chat_page(font, chat);
    text(font, 44, 506, 18, MUTED, page == PAGE_CHAT ? "X Type  SQUARE Send  L Photo  R Voice" : "X  Next page");
    text(font, 230, 506, 18, MUTED, "CIRCLE  Previous");
    text(font, 470, 506, 18, MUTED, "TRIANGLE  Muse Web");
    text(font, 770, 506, 18, MUTED, "START  Exit");
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

static void copy_ime_text(char *destination, size_t capacity) {
    size_t output = 0;
    for (size_t i = 0; ime_buffer[i] != 0 && output + 1 < capacity; i++) {
        uint16_t codepoint = ime_buffer[i];
        destination[output++] = codepoint < 0x80 ? (char)codepoint : '?';
    }
    destination[output] = '\0';
}

static void open_chat_ime(void) {
    if (!app_util_ready || ime_open) return;
    memset(ime_buffer, 0, sizeof(ime_buffer));
    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
    param.languagesForced = SCE_TRUE;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.option = SCE_IME_OPTION_MULTILINE;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    param.title = chat_ime_title;
    param.maxTextLength = MUSEVITA_CHAT_MAX_TEXT - 1;
    param.inputTextBuffer = (SceWChar16 *)ime_buffer;
    param.enterLabel = SCE_IME_ENTER_LABEL_SEND;
    ime_open = sceImeDialogInit(&param) >= 0;
}

static void finish_chat_ime(MuseVitaChatState *chat) {
    if (!ime_open || sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return;
    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);
    if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
        char draft[MUSEVITA_CHAT_MAX_TEXT];
        copy_ime_text(draft, sizeof(draft));
        muse_vita_chat_set_draft(chat, draft);
    }
    sceImeDialogTerm();
    ime_open = false;
}

static void capture_and_send_photo(void) {
    size_t media_size = 0;
    strcpy(media_status, "Capturing photo...");
    if (muse_vita_capture_photo_jpeg(muse_session.media_capture_buffer, sizeof(muse_session.media_capture_buffer), &media_size) != 0) {
        strcpy(media_status, "Camera capture failed.");
        return;
    }
    if (muse_vita_session_send_media(&muse_session, "Please analyze this image.", "image",
                                     "image/jpeg", "vita-camera.jpg", muse_session.media_capture_buffer,
                                     media_size) != 0) {
        strcpy(media_status, "Photo captured, but send failed.");
        return;
    }
    strcpy(media_status, "Photo sent to Muse.");
}

static void capture_and_send_voice(void) {
    size_t media_size = 0;
    strcpy(media_status, "Recording 2-second voice note...");
    if (muse_vita_capture_voice_wav(muse_session.media_capture_buffer, sizeof(muse_session.media_capture_buffer), &media_size) != 0) {
        strcpy(media_status, "Microphone capture failed.");
        return;
    }
    if (muse_vita_session_send_media(&muse_session, "Please transcribe this voice note.", "file",
                                     "audio/wav", "vita-voice.wav", muse_session.media_capture_buffer,
                                     media_size) != 0) {
        strcpy(media_status, "Voice note captured, but send failed.");
        return;
    }
    strcpy(media_status, "Voice note sent to Muse.");
}

static void init_ime(void) {
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_IME) < 0) return;
    SceAppUtilInitParam app_param = {0};
    SceAppUtilBootParam boot_param = {0};
    if (sceAppUtilInit(&app_param, &boot_param) < 0) return;
    SceCommonDialogConfigParam config;
    sceCommonDialogConfigParamInit(&config);
    if (sceCommonDialogSetConfigParam(&config) < 0) {
        sceAppUtilShutdown();
        return;
    }
    app_util_ready = true;
}

static void term_ime(void) {
    if (ime_open) {
        sceImeDialogAbort();
        sceImeDialogTerm();
        ime_open = false;
    }
    if (app_util_ready) {
        sceAppUtilShutdown();
        app_util_ready = false;
    }
    if (sceSysmoduleIsLoaded(SCE_SYSMODULE_IME) > 0) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
    }
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
    MuseVitaChatState chat;
    muse_vita_chat_init(&chat);
    media_status[0] = '\0';
    muse_vita_session_init(&muse_session);
    if (muse_vita_session_load_pairing(&muse_session) == 0 && network_ready) {
        (void)muse_vita_session_connect(&muse_session, &chat);
    }
    init_ime();
    SceCtrlData pad = {0};
    uint32_t previous = 0;
    int page = PAGE_STATUS;
    while (true) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t just_pressed = pad.buttons & ~previous;
        previous = pad.buttons;
        finish_chat_ime(&chat);
        (void)muse_vita_session_poll(&muse_session);
        if (just_pressed & SCE_CTRL_START) break;
        if ((just_pressed & SCE_CTRL_CROSS) && page == PAGE_CHAT) open_chat_ime();
        else if (just_pressed & SCE_CTRL_CROSS) page = (page + 1) % PAGE_COUNT;
        if (just_pressed & SCE_CTRL_CIRCLE) page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
        if ((just_pressed & SCE_CTRL_LTRIGGER) && page == PAGE_CHAT &&
            muse_vita_session_is_connected(&muse_session)) capture_and_send_photo();
        if ((just_pressed & SCE_CTRL_RTRIGGER) && page == PAGE_CHAT &&
            muse_vita_session_is_connected(&muse_session)) capture_and_send_voice();
        if (just_pressed & SCE_CTRL_TRIANGLE) {
            (void)sceAppMgrLaunchAppByUri(0x20000, MUSEVITA_WEB_URL);
        }
        if ((just_pressed & SCE_CTRL_SQUARE) && page == PAGE_CHAT && chat.draft_ready &&
            muse_vita_session_send_chat(&muse_session, chat.draft) == 0) {
            muse_vita_chat_clear_draft(&chat);
        }
        MuseVitaNetworkState network = network_ready
                                           ? network_provider.probe(network_provider.context)
                                           : MUSEVITA_NETWORK_UNKNOWN;
        if (network == MUSEVITA_NETWORK_ONLINE && muse_vita_session_should_reconnect(&muse_session)) {
            (void)muse_vita_session_connect(&muse_session, &chat);
        }
        draw(font, page, network, &chat, &muse_session);
        if (ime_open) (void)vita2d_common_dialog_update();
        sceKernelDelayThread(16000);
    }
    term_ime();
    muse_vita_session_term(&muse_session);
    if (network_ready) muse_vita_network_term();
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
