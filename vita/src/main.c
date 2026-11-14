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
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/ime_dialog.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>

#include "chat_state.h"
#include "muse_session.h"
#include "network_state.h"
#include "pairing_transfer.h"
#include "qrcodegen.h"
#include "vita_media.h"

#define WHITE RGBA8(255, 255, 255, 255)
#define MUTED RGBA8(211, 217, 232, 255)
#define BG RGBA8(10, 13, 24, 255)
#define PANEL RGBA8(24, 30, 49, 255)
#define CARD RGBA8(36, 45, 70, 255)
#define ACCENT RGBA8(157, 139, 255, 255)
#define BORDER RGBA8(83, 94, 126, 255)
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
static char setup_status[96];
static char chat_status[96];
static uint8_t qr_temp[qrcodegen_BUFFER_LEN_FOR_VERSION(20)];
static uint8_t qr_code[qrcodegen_BUFFER_LEN_FOR_VERSION(20)];
static const SceWChar16 chat_ime_title[] = {
    'M', 'e', 's', 's', 'a', 'g', 'e', ' ', 't', 'o', ' ', 'M', 'u', 's', 'e', 0
};
static MuseVitaSession muse_session;

static void boot_log(const char *stage) {
    static bool started;
    SceUID fd;
    char line[96];
    int size;
    if (stage == NULL) return;
    (void)sceIoMkdir("ux0:data/MuseVita", 0777);
    fd = sceIoOpen("ux0:data/MuseVita/boot.log",
                   SCE_O_WRONLY | SCE_O_CREAT | (started ? SCE_O_APPEND : SCE_O_TRUNC), 0600);
    if (fd < 0) return;
    size = snprintf(line, sizeof(line), "%s\n", stage);
    if (size > 0) (void)sceIoWrite(fd, line, size);
    sceIoClose(fd);
    started = true;
}

static void draw_pairing_qr(const char *payload) {
    int size;
    const int module_size = 2;
    const int quiet_zone = 4;
    if (payload == NULL || payload[0] == '\0' ||
        !qrcodegen_encodeText(payload, qr_temp, qr_code, qrcodegen_Ecc_LOW,
                              1, 20, qrcodegen_Mask_AUTO, true)) return;
    size = qrcodegen_getSize(qr_code);
    vita2d_draw_rectangle(700, 226, (size + quiet_zone * 2) * module_size,
                          (size + quiet_zone * 2) * module_size, WHITE);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcodegen_getModule(qr_code, x, y))
                vita2d_draw_rectangle(700 + (x + quiet_zone) * module_size,
                                      226 + (y + quiet_zone) * module_size,
                                      module_size, module_size, BG);
        }
    }
}

static void text(vita2d_pgf *font, int x, int y, float size, uint32_t color, const char *value) {
    /* vita2d PGF takes a scale multiplier, not a pixel size. */
    float scale = size / MUSEVITA_PGF_BASE_SIZE;
    if (scale < MUSEVITA_PGF_MIN_SCALE) scale = MUSEVITA_PGF_MIN_SCALE;
    vita2d_pgf_draw_text(font, x, y, color, scale, value);
}

static void draw_header(vita2d_pgf *font, MuseVitaNetworkState network,
                        const MuseVitaSession *session) {
    text(font, 42, 74, 38, WHITE, "MUSEVITA");
    text(font, 44, 106, 18, MUTED, "Native Muse client  •  direct Wi-Fi");
    text(font, 720, 74, 18, network == MUSEVITA_NETWORK_ONLINE ? SUCCESS : WARNING,
         muse_vita_network_label(network));
    text(font, 720, 106, 16, muse_vita_session_is_connected(session) ? SUCCESS : MUTED,
         muse_vita_session_label(session));
}

static void draw_status_page(vita2d_pgf *font, MuseVitaNetworkState network,
                             const MuseVitaSession *session) {
    text(font, 70, 192, 31, WHITE, "Connection status");
    text(font, 70, 234, 22, network == MUSEVITA_NETWORK_ONLINE ? SUCCESS : WARNING,
         muse_vita_network_label(network));
    text(font, 70, 280, 22, WHITE, muse_vita_session_label(session));
    text(font, 70, 319, 20, MUTED, muse_vita_session_is_connected(session)
                                      ? "Direct Muse transport is active."
                                      : "Import official pairing state to connect directly.");
    vita2d_draw_rectangle(70, 352, 820, 62, CARD);
    text(font, 94, 391, 20, WHITE, "Use SETUP for the one-time pairing steps");
    text(font, 70, 438, 18, MUTED, "TRIANGLE  Open Muse Web");
}

static void draw_setup_page(vita2d_pgf *font, const MuseVitaPairingTransfer *transfer) {
    text(font, 70, 184, 31, WHITE, "Pair your Vita");
    text(font, 70, 214, 18, MUTED, "Scan. Pick 3 files. Press SELECT.");

    vita2d_draw_rectangle(70, 238, 570, 55, CARD);
    text(font, 94, 272, 17, ACCENT, "1");
    text(font, 142, 262, 19, WHITE, "Scan QR");
    text(font, 142, 283, 15, MUTED, "Use your phone camera");

    vita2d_draw_rectangle(70, 304, 570, 70, CARD);
    text(font, 94, 337, 17, ACCENT, "2");
    text(font, 142, 328, 19, WHITE, "Pick 3 files");
    text(font, 142, 350, 15, MUTED, "identity.json  •  pairing.json  •  sdk_token");

    vita2d_draw_rectangle(70, 385, 570, 43, CARD);
    text(font, 94, 413, 17, ACCENT, "3");
    text(font, 142, 413, 19, WHITE, "Press SELECT");

    draw_pairing_qr(muse_vita_pairing_transfer_qr(transfer));
    if (setup_status[0] != '\0') text(font, 70, 466, 16, MUTED, setup_status);
    else text(font, 70, 466, 16, MUTED, muse_vita_pairing_transfer_status(transfer));
    text(font, 70, 488, 14, MUTED, "Can't scan? Open:");
    text(font, 240, 488, 14, ACCENT,
         muse_vita_pairing_transfer_manual_url(transfer));
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
    text(font, 70, 192, 31, WHITE, "Main chat");
    text(font, 690, 192, 18, chat->transport_connected ? SUCCESS : WARNING,
         chat->transport_connected ? "CONNECTED" : "OFFLINE");
    vita2d_draw_rectangle(70, 210, 820, 216, CARD);

    if (chat->message_count == 0) {
        text(font, 95, 260, 21, MUTED, "No conversation loaded.");
        text(font, 95, 300, 19, MUTED, "Messages from Muse will appear here.");
    } else {
        unsigned int first = chat->message_count > 4 ? chat->message_count - 4 : 0;
        int y = 240;
        for (unsigned int i = first; i < chat->message_count; i++) {
            const MuseVitaChatMessage *message = &chat->messages[i];
            text(font, 95, y, 18, message->role == MUSEVITA_CHAT_USER ? ACCENT : SUCCESS,
                 message->role == MUSEVITA_CHAT_USER ? "YOU" : "MUSE");
            y += 21;
            y += draw_wrapped(font, 95, y, 62, 2, WHITE, message->text) * 24 + 10;
            if (y > 408) break;
        }
    }

    if (chat->draft_ready) {
        text(font, 70, 452, 18, WARNING, "DRAFT READY — not sent");
        draw_wrapped(font, 280, 452, 50, 1, MUTED, chat->draft);
    } else {
        text(font, 70, 452, 18, MUTED, "X  Type a message");
    }
    if (chat_status[0] != '\0') text(font, 70, 480, 17, WARNING, chat_status);
    else if (media_status[0] != '\0') text(font, 70, 480, 17, MUTED, media_status);
}

static void draw(vita2d_pgf *font, int page, MuseVitaNetworkState network,
                 const MuseVitaChatState *chat, const MuseVitaSession *session,
                 const MuseVitaPairingTransfer *transfer) {
    vita2d_start_drawing();
    vita2d_clear_screen();
    vita2d_draw_rectangle(0, 0, 960, 544, BG);
    vita2d_draw_rectangle(0, 0, 960, 8, ACCENT);
    draw_header(font, network, session);
    vita2d_draw_rectangle(42, 138, 876, 315, PANEL);
    vita2d_draw_rectangle(42, 138, 6, 315, ACCENT);
    if (page == PAGE_STATUS) draw_status_page(font, network, session);
    else if (page == PAGE_SETUP) draw_setup_page(font, transfer);
    else draw_chat_page(font, chat);
    text(font, 44, 506, 16, MUTED, page == PAGE_CHAT ? "X Type  □ Send  L Photo  R Voice" : "X Next page");
    text(font, 230, 506, 16, MUTED, "○ Previous");
    text(font, 430, 506, 16, MUTED, "△ Muse Web");
    text(font, 690, page == PAGE_SETUP ? 522 : 506, 16, MUTED,
         page == PAGE_SETUP ? "SELECT Reload" : "START Exit");
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
    bool rendered_once = false;
    bool runtime_initialized = false;
    boot_log("main:start");
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    vita2d_init();
    boot_log("vita2d:initialized");
    vita2d_set_clear_color(BG);
    vita2d_pgf *font = vita2d_load_default_pgf();
    if (!font) {
        boot_log("vita2d:font-failed");
        vita2d_fini();
        sceKernelExitProcess(1);
        return 1;
    }

    bool network_ready = false;
    MuseVitaNetworkProvider network_provider = muse_vita_default_network_provider();
    MuseVitaChatState chat;
    bool session_ready = false;
    muse_vita_chat_init(&chat);
    media_status[0] = '\0';
    setup_status[0] = '\0';
    chat_status[0] = '\0';
    static MuseVitaPairingTransfer pairing_transfer;
    memset(&pairing_transfer, 0, sizeof(pairing_transfer));
    pairing_transfer.listen_socket = -1;
    bool pairing_transfer_started = false;
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
        if (session_ready && (just_pressed & SCE_CTRL_SELECT) && page == PAGE_SETUP) {
            char identity[MUSEVITA_PAIRING_IDENTITY_MAX];
            char pairing[MUSEVITA_PAIRING_JSON_MAX];
            char sdk_token[MUSEVITA_PAIRING_TOKEN_MAX];
            if (muse_vita_pairing_transfer_take(&pairing_transfer, identity, sizeof(identity),
                                                pairing, sizeof(pairing), sdk_token, sizeof(sdk_token)) == 0 &&
                muse_vita_session_import_pairing(&muse_session, identity, strlen(identity),
                                                 pairing, strlen(pairing), sdk_token) == 0) {
                strcpy(setup_status, "Encrypted pairing imported; connecting...");
            } else if (muse_vita_session_load_pairing(&muse_session) == 0) {
                strcpy(setup_status, "Pairing imported; connecting...");
            } else {
                strcpy(setup_status, "No received or local pairing bundle found.");
            }
        }
        if ((just_pressed & SCE_CTRL_SQUARE) && page == PAGE_CHAT && chat.draft_ready) {
            if (muse_vita_session_send_chat(&muse_session, chat.draft) == 0) {
                muse_vita_chat_clear_draft(&chat);
                strcpy(chat_status, "Message sent.");
            } else {
                strcpy(chat_status, "Message not sent — check the connection.");
            }
        }
        MuseVitaNetworkState network = network_ready
                                           ? network_provider.probe(network_provider.context)
                                           : MUSEVITA_NETWORK_UNKNOWN;
        if (pairing_transfer_started &&
            (page != PAGE_SETUP || (network == MUSEVITA_NETWORK_ONLINE && !pairing_transfer.ready))) {
            muse_vita_pairing_transfer_stop(&pairing_transfer);
            pairing_transfer_started = false;
        }
        if (page == PAGE_SETUP && network == MUSEVITA_NETWORK_ONLINE && !pairing_transfer_started) {
            pairing_transfer_started = muse_vita_pairing_transfer_start(&pairing_transfer) == 0;
        }
        if (rendered_once && network == MUSEVITA_NETWORK_ONLINE &&
            (muse_vita_session_should_connect(&muse_session) ||
             muse_vita_session_should_reconnect(&muse_session))) {
            boot_log("session:connect-start");
            (void)muse_vita_session_connect(&muse_session, &chat);
            boot_log(muse_vita_session_is_connected(&muse_session)
                         ? "session:connected" : "session:connect-failed");
        }
        draw(font, page, network, &chat, &muse_session, &pairing_transfer);
        rendered_once = true;

        if (!runtime_initialized) {
            boot_log("runtime:start");
            network_ready = muse_vita_network_init() == 0;
            boot_log(network_ready ? "network:initialized" : "network:unavailable");
            session_ready = muse_vita_session_init(&muse_session) == 0;
            boot_log(session_ready ? "session:initialized" : "session:init-failed");
            if (session_ready && muse_vita_session_load_pairing(&muse_session) == 0) {
                boot_log("pairing:loaded");
            } else {
                boot_log("pairing:not-loaded");
            }
            init_ime();
            boot_log("ime:initialized");
            runtime_initialized = true;
        }
        if (ime_open) (void)vita2d_common_dialog_update();
        sceKernelDelayThread(16000);
    }
    term_ime();
    if (pairing_transfer_started) muse_vita_pairing_transfer_stop(&pairing_transfer);
    if (runtime_initialized && session_ready) muse_vita_session_term(&muse_session);
    if (network_ready) muse_vita_network_term();
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
