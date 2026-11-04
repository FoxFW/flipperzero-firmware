// NFC Apdu Spy v1.0.0  (formerly "Badge Spy")
// The Flipper pretends to be an NFC tag (ISO14443-4A / ISO-DEP) and logs
// every APDU a phone sends, together with the Flipper's answer.
//
// Controls (main screen):
//   Left / Right  switch graphics mode (1-BIT <-> GRAY)
//   OK            open the Settings menu
//   Down          open Info / About
//   Back          stop; if something was captured -> "Save log?" (NO / YES)
//
// Settings menu (OK): Tag Setting, Graphics, Info/About. Back saves & returns.

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include <nfc/nfc.h>
#include <nfc/nfc_listener.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_listener.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller_sync.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "spy_gfx.h"

#define TAG           "NfcApduSpy"
#define APP_VERSION   "1.0.0"
#define SPY_DIR       EXT_PATH("apps_data/nfc_apdu_spy")
#define TMP_PATH      EXT_PATH("apps_data/nfc_apdu_spy/spy_tmp.txt")
#define SETTINGS_PATH EXT_PATH("apps_data/nfc_apdu_spy/settings.txt")
#define OLD_ANSWERS   EXT_PATH("apps_data/badge_spy/answers.txt") // from Badge Spy 1.0
#define OLD_ANSWERS2  EXT_PATH("apps_data/nfc_apdu_spy/answers.txt") // from v1.0..1.2

#define MAX_ANSWERS    40
#define MAX_PREFIX     16
#define MAX_ANSWER     200
#define MAX_APDU       600
#define LOG_DATA_MAX   280
#define LOG_RESP_MAX   80
#define LOG_QUEUE_SIZE 24

// subframe rates (Hz). Pictures per second = rate / gray depth (s).
static const uint16_t spy_rates[] = {60, 75, 90, 100, 120, 150};
#define RATE_COUNT   (sizeof(spy_rates) / sizeof(spy_rates[0]))
#define RATE_DEFAULT 0 // 60 Hz

#define HOLD_SHOW_MS 1500 // "HOLD STILL" after the first contact
#define ACTIVE_MS    2000 // "SPYING" as long as the phone talked within this time
#define DONE_SHOW_MS 3500 // "Data SAVED" + path before returning to the menu
#define SCAN_SHOW_MS 1000 // SUCCESS / ERROR after a scan
#define SCAN_BUSY_MS 500  // "SCANNING..." beat

// ---------- scan feedback: sounds + vibration ----------
// success: two rising notes in harmony (C5 -> G5, a fifth), short buzz
// error: two falling, dissonant notes (Bb4 -> E4, a tritone), long buzz
static const NotificationMessage m_c5 = {
    .type = NotificationMessageTypeSoundOn, .data.sound = {.frequency = 523.25f, .volume = 1.0f}};
static const NotificationMessage m_g5 = {
    .type = NotificationMessageTypeSoundOn, .data.sound = {.frequency = 783.99f, .volume = 1.0f}};
static const NotificationMessage m_bb4 = {
    .type = NotificationMessageTypeSoundOn, .data.sound = {.frequency = 466.16f, .volume = 1.0f}};
static const NotificationMessage m_e4 = {
    .type = NotificationMessageTypeSoundOn, .data.sound = {.frequency = 329.63f, .volume = 1.0f}};
// sound-only sequences (vibration is fired separately so both can be toggled)
static const NotificationSequence seq_ok_sound = {
    &m_c5,
    &message_delay_100,
    &m_g5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};
static const NotificationSequence seq_fail_sound = {
    &m_bb4,
    &message_delay_100,
    &m_e4,
    &message_delay_250,
    &message_sound_off,
    NULL,
};
// a longer buzz for errors (short buzz uses the built-in sequence_single_vibro)
static const NotificationSequence seq_vibro_long = {
    &message_vibro_on,
    &message_delay_250,
    &message_delay_250,
    &message_vibro_off,
    NULL,
};

// ---------- answer table ----------
typedef struct {
    uint8_t prefix[MAX_PREFIX];
    uint8_t prefix_len;
    uint8_t answer[MAX_ANSWER];
    uint8_t answer_len;
} SpyAnswer;

// ---------- tag profiles (files in profiles/, each = one product/device) ----------
// A profile file: optional identity header (UID/ATQA/SAK/ATS/RANDOM_UID) + answer
// lines (<command start hex> <answer hex>, * = all others). First '#' line = name.
#define PROFILE_DIR  EXT_PATH("apps_data/nfc_apdu_spy/profiles")
#define MAX_PROFILES 24
#define NAME_LEN     26
#define FNAME_LEN    42

typedef struct {
    char name[NAME_LEN]; // shown in the menu
    char fname[FNAME_LEN]; // file name in profiles/
} ProfileItem;

// starter library written on first run
typedef struct {
    const char* fname;
    const char* text;
} DefaultProfile;

static const DefaultProfile default_profiles[] = {
    {"good_display_dmn036ew.txt",
     "# Good Display DMN036EW\n"
     "UID E2916F52\n"
     "ATQA 0400\n"
     "SAK 20\n"
     "ATS 0B7833A0029090E2916F5200\n"
     "# --- answers (command start -> answer), * = all others ---\n"
     "00D1 A007F0362002580190A10701FFFFFFFFFFFFB10108B20114B30100C004201EA001C104E2916F52D10701200000000000E00E46E00080018102830382058607859000\n"
     "F0D801FE 6A86\n"
     "F0D80000 FFFFFFFFFFFFFFFFFFFFFFFFFFFF9000\n"
     "F0DE 019000\n"
     "F0D2 9000\n"
     "F0D3 9000\n"
     "F0D4 9000\n"
     "F0DA 9000\n"
     "F0DB 9000\n"
     "0020 9000\n"
     "00A4 6A82\n"
     "* 6D00\n"},
    {"simple_tag.txt",
     "# Simple tag (4A)\n"
     "# No identity header -> generic ISO14443-4A card.\n"
     "ATS 057833A002\n"
     "00A4 9000\n"
     "* 6D00\n"},
    {"random_uid.txt",
     "# Random UID (4A)\n"
     "RANDOM_UID 1\n"
     "ATS 057833A002\n"
     "00A4 9000\n"
     "* 6D00\n"},
};
#define DEFAULT_PROFILE_COUNT (sizeof(default_profiles) / sizeof(default_profiles[0]))

// ---------- log entries (NFC thread -> main thread) ----------
typedef enum {
    LogKindRats,
    LogKindPps,
    LogKindApdu,
    LogKindDeselect,
    LogKindRBlock,
    LogKindFieldOff,
    LogKindHalt,
    LogKindRaw,
} LogKind;

typedef struct {
    uint8_t kind;
    uint32_t tick;
    uint16_t len;
    uint8_t data[LOG_DATA_MAX];
    uint8_t resp_len;
    uint8_t resp[LOG_RESP_MAX];
} LogItem;

typedef enum {
    ScreenWait, // GET NEAR TARGET
    ScreenHold, // HOLD STILL (first contact)
    ScreenLog, // live log window
    ScreenAsk, // "Save log?" NO / YES
    ScreenDone, // "Data SAVED" + path
    ScreenScan, // Mission: SCAN TAG, WAITING... (reader mode)
    ScreenScanBusy, // tag detected, SCANNING...
    ScreenScanOk, // scan success (check, SUCCESS)
    ScreenScanFail, // scan failed (verbot, ERROR)
    ScreenNoTag, // tried to Spy without choosing a tag first
} SpyScreen;

typedef enum {
    MenuNone,
    MenuHome, // start screen: SELECT TARGET / SCAN TAG
    MenuMain, // Graphics / Info
    MenuGfx, // Mode / Grays / Rate
    MenuTag, // list of profiles
    MenuAbout,
} MenuState;

typedef struct {
    // NFC
    Nfc* nfc;
    NfcListener* listener;
    Iso14443_3aData* data;
    BitBuffer* tx;
    uint8_t ats[16]; // active tag's ATS
    uint8_t ats_len;

    // ISO-DEP state (NFC thread only)
    bool active;
    uint8_t apdu[MAX_APDU];
    uint16_t apdu_len;
    uint8_t last_frame[MAX_ANSWER + 4];
    uint8_t last_frame_len;
    uint8_t frame[MAX_ANSWER + 4];

    SpyAnswer answers[MAX_ANSWERS];
    uint8_t answer_count;
    uint8_t default_answer[4];
    uint8_t default_answer_len;

    // log
    FuriMessageQueue* log_queue;
    LogItem item_nfc;
    LogItem item_main;
    volatile uint32_t dropped;
    uint32_t start_tick;
    File* file;
    Storage* storage;
    char saved_name[48]; // final log file name (for the save screen)
    uint32_t cnt_field;
    uint32_t cnt_rats;
    uint32_t cnt_apdu;

    // screen (main thread only)
    SpyScreen screen;
    SpyScreen prev_screen; // to return to when the save prompt is cancelled
    uint32_t screen_since;
    uint32_t last_activity;
    char box[BOX_LINES][BOX_CHARS + 1];
    int box_n;
    int typed;
    bool hold_fire; // trigger vibration once on first contact

    // tag profiles
    ProfileItem profiles[MAX_PROFILES];
    int profile_count;
    int tag_idx; // active profile index
    char active_file[FNAME_LEN]; // active profile file name (saved in settings)

    // settings
    bool gray; // false = 1-bit (default)
    bool sound_on; // scan feedback tones
    bool vibro_on; // scan feedback / contact vibration
    int rate_idx;
    MenuState menu;
    int sel_main, sel_gfx, sel_tag, tag_top, sel_home;
    int about_top; // scroll position in the Info/About text
    bool about_from_menu;
    bool settings_from_home; // Settings opened from the start menu (vs. during spying)
    bool had_saved_tag; // a tag was already chosen in a previous run -> preselect Spying
    bool first_run; // no settings file yet -> seed the starter profiles once

    // tag edit/delete popup + name keyboard
    bool tag_popup; // small Edit/Delete window open in the select list
    int popup_sel; // 0 = Edit, 1 = Delete
    bool want_keyboard; // open the name keyboard on the next loop turn
    bool kb_rename; // keyboard is renaming an existing tag (not naming a scan)
    char kb_buf[NAME_LEN]; // keyboard text buffer
    // freshly scanned identity (kept until the name keyboard confirms)
    char scan_uhex[15];
    uint8_t scan_atqa[2];
    uint8_t scan_sak;

    // scan
    uint32_t scan_deadline;
    uint32_t scan_last_try;

    FuriMessageQueue* input_queue;
} NfcApduSpy;

// ---------- hex helpers ----------
static int hexval(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t parse_hex(const char* s, uint8_t* out, size_t max) {
    size_t n = 0;
    int hi = -1;
    for(; *s && *s != '\n' && *s != '\r' && *s != '#'; s++) {
        int v = hexval(*s);
        if(v < 0) continue;
        if(hi < 0) {
            hi = v;
        } else {
            if(n < max) out[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    return n;
}

// word at *line equals kw (case-insensitive, ends at space/tab)? returns arg start or NULL
static const char* kw_match(const char* line, const char* kw) {
    size_t n = strlen(kw);
    for(size_t i = 0; i < n; i++) {
        char c = line[i];
        if(c >= 'a' && c <= 'z') c -= 32;
        if(c != kw[i]) return NULL;
    }
    if(line[n] != ' ' && line[n] != '\t') return NULL;
    return line + n;
}

// parse a whole profile: identity header (optional) + answer lines
static void spy_parse_profile(NfcApduSpy* app, const char* text) {
    // defaults (generic ISO14443-4A)
    uint8_t uid[7] = {0x04, 0x11, 0x22, 0x33}, uid_len = 4;
    uint8_t atqa[2] = {0x04, 0x00}, sak = 0x20;
    uint8_t ats[16] = {0x05, 0x78, 0x33, 0xA0, 0x02}, ats_len = 5;
    bool rnd = false;
    app->answer_count = 0;
    app->default_answer[0] = 0x6D;
    app->default_answer[1] = 0x00;
    app->default_answer_len = 2;

    const char* line = text;
    while(line && *line) {
        const char* end = strchr(line, '\n');
        while(*line == ' ' || *line == '\t') line++;
        if(*line && *line != '#' && *line != '\n' && *line != '\r') {
            const char* arg;
            if((arg = kw_match(line, "UID"))) {
                uint8_t t[7];
                size_t n = parse_hex(arg, t, 7);
                if(n >= 4) {
                    uid_len = (uint8_t)(n == 7 ? 7 : 4);
                    memcpy(uid, t, uid_len);
                }
            } else if((arg = kw_match(line, "ATQA"))) {
                parse_hex(arg, atqa, 2);
            } else if((arg = kw_match(line, "SAK"))) {
                parse_hex(arg, &sak, 1);
            } else if((arg = kw_match(line, "ATS"))) {
                uint8_t t[16];
                size_t n = parse_hex(arg, t, 16);
                if(n) {
                    ats_len = (uint8_t)n;
                    memcpy(ats, t, n);
                }
            } else if((arg = kw_match(line, "RANDOM_UID"))) {
                while(*arg == ' ' || *arg == '\t') arg++;
                rnd = (*arg != '0');
            } else {
                // answer line: <prefix hex> <answer hex>, or * <hex>
                const char* sp = line;
                while(*sp && *sp != ' ' && *sp != '\t' && *sp != '\n' && *sp != '\r') sp++;
                if(*sp == ' ' || *sp == '\t') {
                    if(*line == '*') {
                        app->default_answer_len = (uint8_t)parse_hex(
                            sp, app->default_answer, sizeof(app->default_answer));
                    } else if(app->answer_count < MAX_ANSWERS) {
                        SpyAnswer* a = &app->answers[app->answer_count];
                        char word[2 * MAX_PREFIX + 2];
                        size_t wl = (size_t)(sp - line);
                        if(wl > sizeof(word) - 1) wl = sizeof(word) - 1;
                        memcpy(word, line, wl);
                        word[wl] = '\0';
                        a->prefix_len = (uint8_t)parse_hex(word, a->prefix, MAX_PREFIX);
                        a->answer_len = (uint8_t)parse_hex(sp, a->answer, MAX_ANSWER);
                        if(a->prefix_len > 0 && a->answer_len > 0) app->answer_count++;
                    }
                }
            }
        }
        line = end ? end + 1 : NULL;
    }

    if(rnd)
        for(int i = 0; i < uid_len; i++) uid[i] = (uint8_t)furi_hal_random_get();
    iso14443_3a_set_uid(app->data, uid, uid_len);
    iso14443_3a_set_atqa(app->data, atqa);
    iso14443_3a_set_sak(app->data, sak);
    memcpy(app->ats, ats, ats_len);
    app->ats_len = ats_len;
    FURI_LOG_I(TAG, "profile: %u answers, ats %u", app->answer_count, app->ats_len);
}

static const SpyAnswer* spy_find_answer(NfcApduSpy* app, const uint8_t* cmd, size_t len) {
    const SpyAnswer* best = NULL;
    for(uint8_t i = 0; i < app->answer_count; i++) {
        const SpyAnswer* a = &app->answers[i];
        if(a->prefix_len > len) continue;
        if(memcmp(a->prefix, cmd, a->prefix_len) != 0) continue;
        if(!best || a->prefix_len > best->prefix_len) best = a;
    }
    return best;
}

// write the starter library on first run (only files that are missing)
static void spy_write_defaults(NfcApduSpy* app) {
    storage_simply_mkdir(app->storage, SPY_DIR);
    storage_simply_mkdir(app->storage, PROFILE_DIR);
    for(unsigned i = 0; i < DEFAULT_PROFILE_COUNT; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", PROFILE_DIR, default_profiles[i].fname);
        if(storage_file_exists(app->storage, path)) continue;
        File* f = storage_file_alloc(app->storage);
        if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS))
            storage_file_write(f, default_profiles[i].text, strlen(default_profiles[i].text));
        storage_file_close(f);
        storage_file_free(f);
    }
}

// read the display name from a profile file (first '#' line, else the file name)
static void spy_read_name(NfcApduSpy* app, const char* fname, char* out, size_t out_len) {
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", PROFILE_DIR, fname);
    out[0] = '\0';
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[96] = {0};
        size_t rd = storage_file_read(f, buf, sizeof(buf) - 1);
        buf[rd] = '\0';
        char* h = strchr(buf, '#');
        if(h) {
            h++;
            while(*h == ' ' || *h == '\t') h++;
            size_t k = 0;
            while(h[k] && h[k] != '\n' && h[k] != '\r' && k < out_len - 1) {
                out[k] = h[k];
                k++;
            }
            out[k] = '\0';
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    if(!out[0]) {
        // fall back to the file name without .txt
        strncpy(out, fname, out_len - 1);
        out[out_len - 1] = '\0';
        char* dot = strrchr(out, '.');
        if(dot) *dot = '\0';
    }
}

// scan profiles/ and fill app->profiles (alphabetical-ish, folder order)
static void spy_scan_profiles(NfcApduSpy* app) {
    app->profile_count = 0;
    File* dir = storage_file_alloc(app->storage);
    if(storage_dir_open(dir, PROFILE_DIR)) {
        FileInfo info;
        char fname[FNAME_LEN];
        while(app->profile_count < MAX_PROFILES &&
              storage_dir_read(dir, &info, fname, sizeof(fname))) {
            if(info.flags & FSF_DIRECTORY) continue;
            size_t l = strlen(fname);
            if(l < 5 || strcmp(&fname[l - 4], ".txt") != 0) continue;
            ProfileItem* p = &app->profiles[app->profile_count];
            strncpy(p->fname, fname, FNAME_LEN - 1);
            p->fname[FNAME_LEN - 1] = '\0';
            spy_read_name(app, fname, p->name, NAME_LEN);
            app->profile_count++;
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
}

// load + apply a profile by index (identity + ATS + answers)
static void spy_apply_profile(NfcApduSpy* app, int idx) {
    if(idx < 0 || idx >= app->profile_count) {
        spy_parse_profile(app, default_profiles[1].text); // generic fallback
        return;
    }
    app->tag_idx = idx;
    strncpy(app->active_file, app->profiles[idx].fname, FNAME_LEN - 1);
    app->active_file[FNAME_LEN - 1] = '\0';
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", PROFILE_DIR, app->profiles[idx].fname);
    bool loaded = false;
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(f);
        if(size > 0 && size < 16000) {
            char* buf = malloc((size_t)size + 1);
            size_t rd = storage_file_read(f, buf, (size_t)size);
            buf[rd] = '\0';
            spy_parse_profile(app, buf);
            free(buf);
            loaded = true;
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    if(!loaded) spy_parse_profile(app, default_profiles[1].text);
}

// ---------- log (NFC thread) ----------
static void spy_log(
    NfcApduSpy* app,
    LogKind kind,
    const uint8_t* data,
    size_t len,
    const uint8_t* resp,
    size_t resp_len) {
    LogItem* it = &app->item_nfc;
    it->kind = (uint8_t)kind;
    it->tick = furi_get_tick();
    it->len = (uint16_t)len;
    size_t n = len > LOG_DATA_MAX ? LOG_DATA_MAX : len;
    if(n && data) memcpy(it->data, data, n);
    size_t r = resp_len > LOG_RESP_MAX ? LOG_RESP_MAX : resp_len;
    it->resp_len = (uint8_t)r;
    if(r && resp) memcpy(it->resp, resp, r);
    if(furi_message_queue_put(app->log_queue, it, 0) != FuriStatusOk) app->dropped++;
}

static void spy_send(NfcApduSpy* app, Iso14443_3aListener* l, const uint8_t* frame, size_t len) {
    bit_buffer_reset(app->tx);
    bit_buffer_append_bytes(app->tx, frame, len);
    if(len <= sizeof(app->last_frame)) {
        memcpy(app->last_frame, frame, len);
        app->last_frame_len = (uint8_t)len;
    }
    iso14443_3a_listener_send_standard_frame(l, app->tx);
}

// ---------- ISO-DEP by hand (card side) ----------
static NfcCommand spy_nfc_callback(NfcGenericEvent event, void* context) {
    NfcApduSpy* app = context;
    if(event.protocol != NfcProtocolIso14443_3a) return NfcCommandContinue;
    const Iso14443_3aListenerEvent* ev = event.event_data;
    Iso14443_3aListener* l = (Iso14443_3aListener*)event.instance;

    if(ev->type == Iso14443_3aListenerEventTypeFieldOff) {
        app->active = false;
        app->apdu_len = 0;
        spy_log(app, LogKindFieldOff, NULL, 0, NULL, 0);
        return NfcCommandContinue;
    }
    if(ev->type == Iso14443_3aListenerEventTypeHalted) {
        app->active = false;
        app->apdu_len = 0;
        spy_log(app, LogKindHalt, NULL, 0, NULL, 0);
        return NfcCommandContinue;
    }

    const BitBuffer* rx = ev->data->buffer;
    size_t n = bit_buffer_get_size_bytes(rx);
    if(n == 0) return NfcCommandContinue;
    const uint8_t* b = bit_buffer_get_data(rx);

    if(ev->type != Iso14443_3aListenerEventTypeReceivedStandardFrame) {
        spy_log(app, LogKindRaw, b, n, NULL, 0);
        return NfcCommandContinue;
    }

    uint8_t pcb = b[0];

    // RATS
    if(n == 2 && pcb == 0xE0) {
        app->active = true;
        app->apdu_len = 0;
        spy_send(app, l, app->ats, app->ats_len);
        spy_log(app, LogKindRats, b, n, app->ats, app->ats_len);
        return NfcCommandContinue;
    }
    if(!app->active) {
        spy_log(app, LogKindRaw, b, n, NULL, 0);
        return NfcCommandContinue;
    }

    // PPS
    if((pcb & 0xF0) == 0xD0) {
        uint8_t r = pcb;
        spy_send(app, l, &r, 1);
        spy_log(app, LogKindPps, b, n, &r, 1);
        return NfcCommandContinue;
    }

    bool has_cid = (pcb & 0x08) != 0;
    uint8_t cid = (has_cid && n > 1) ? b[1] : 0;
    size_t hdr = 1 + (has_cid ? 1 : 0);

    // I-block
    if((pcb & 0xE2) == 0x02) {
        bool has_nad = (pcb & 0x04) != 0;
        size_t off = hdr + (has_nad ? 1 : 0);
        size_t inf = n > off ? n - off : 0;
        if(app->apdu_len + inf > MAX_APDU) inf = MAX_APDU - app->apdu_len;
        memcpy(&app->apdu[app->apdu_len], &b[off], inf);
        app->apdu_len += inf;

        if(pcb & 0x10) {
            uint8_t ack[2];
            size_t al = 0;
            ack[al++] = 0xA2 | (has_cid ? 0x08 : 0) | (pcb & 0x01);
            if(has_cid) ack[al++] = cid;
            spy_send(app, l, ack, al);
            return NfcCommandContinue;
        }

        const SpyAnswer* a = spy_find_answer(app, app->apdu, app->apdu_len);
        const uint8_t* resp = a ? a->answer : app->default_answer;
        size_t resp_len = a ? a->answer_len : app->default_answer_len;

        uint8_t* frame = app->frame;
        size_t fl = 0;
        frame[fl++] = 0x02 | (has_cid ? 0x08 : 0) | (pcb & 0x01);
        if(has_cid) frame[fl++] = cid;
        memcpy(&frame[fl], resp, resp_len);
        fl += resp_len;
        spy_send(app, l, frame, fl);

        spy_log(app, LogKindApdu, app->apdu, app->apdu_len, resp, resp_len);
        app->apdu_len = 0;
        return NfcCommandContinue;
    }

    // R-block
    if((pcb & 0xE6) == 0xA2) {
        if(app->last_frame_len) {
            bit_buffer_reset(app->tx);
            bit_buffer_append_bytes(app->tx, app->last_frame, app->last_frame_len);
            iso14443_3a_listener_send_standard_frame(l, app->tx);
        }
        spy_log(app, LogKindRBlock, b, n, NULL, 0);
        return NfcCommandContinue;
    }

    // S-block DESELECT
    if((pcb & 0xF7) == 0xC2) {
        uint8_t r[2];
        size_t rl = 0;
        r[rl++] = pcb;
        if(has_cid) r[rl++] = cid;
        spy_send(app, l, r, rl);
        app->active = false;
        app->apdu_len = 0;
        spy_log(app, LogKindDeselect, b, n, r, rl);
        return NfcCommandContinue;
    }

    spy_log(app, LogKindRaw, b, n, NULL, 0);
    return NfcCommandContinue;
}

// ---------- file (main thread) ----------
static void spy_file_puts(NfcApduSpy* app, const char* s) {
    if(app->file) storage_file_write(app->file, s, strlen(s));
}

static void spy_hex_append(FuriString* s, const uint8_t* d, size_t n) {
    for(size_t i = 0; i < n; i++) furi_string_cat_printf(s, "%02X", d[i]);
}

// opens a temporary log file. It is only renamed to a real name if the user saves.
static void spy_open_tmp(NfcApduSpy* app) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    storage_simply_mkdir(app->storage, SPY_DIR);
    storage_simply_remove(app->storage, TMP_PATH);
    app->file = storage_file_alloc(app->storage);
    if(!storage_file_open(app->file, TMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(app->file);
        app->file = NULL;
        return;
    }
    char head[160];
    snprintf(
        head,
        sizeof(head),
        "NFC Apdu Spy v%s\nStarted: %04u-%02u-%02u %02u:%02u:%02u\nTag: %s\n"
        "Format: [ms] PHONE> command  /  TAG< answer\n\n",
        APP_VERSION,
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second,
        (app->tag_idx >= 0 && app->tag_idx < app->profile_count) ?
            app->profiles[app->tag_idx].name :
            "?");
    spy_file_puts(app, head);
}

// closes the temp file (writes the end line first)
static void spy_close_tmp(NfcApduSpy* app) {
    if(!app->file) return;
    char tail[80];
    snprintf(
        tail,
        sizeof(tail),
        "\nEnd. Connections %lu, APDUs %lu, lost log entries %lu\n",
        (unsigned long)app->cnt_rats,
        (unsigned long)app->cnt_apdu,
        (unsigned long)app->dropped);
    spy_file_puts(app, tail);
    storage_file_close(app->file);
    storage_file_free(app->file);
    app->file = NULL;
}

// renames the temp file to spy_YYYY-MM-DD_HHMMSS.txt (newest is last in the folder)
static bool spy_save_log(NfcApduSpy* app) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    snprintf(
        app->saved_name,
        sizeof(app->saved_name),
        "spy_%04u-%02u-%02u_%02u%02u%02u.txt",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
    char dst[128];
    snprintf(dst, sizeof(dst), "%s/%s", SPY_DIR, app->saved_name);
    storage_simply_remove(app->storage, dst);
    FS_Error e = storage_common_rename(app->storage, TMP_PATH, dst);
    return e == FSE_OK || e == FSE_EXIST;
}

// ---------- NFC listener lifecycle (card side) ----------
static void spy_listener_start(NfcApduSpy* app) {
    if(app->listener) return;
    app->listener = nfc_listener_alloc(app->nfc, NfcProtocolIso14443_3a, app->data);
    nfc_listener_start(app->listener, spy_nfc_callback, app);
    furi_hal_light_set(LightBlue, 0xFF);
}
static void spy_listener_stop_now(NfcApduSpy* app) {
    if(!app->listener) return;
    nfc_listener_stop(app->listener);
    nfc_listener_free(app->listener);
    app->listener = NULL;
    furi_hal_light_set(LightBlue, 0x00);
}

// ---------- scan a real tag (reader mode) -> write a profile file ----------
// returns true and fills out_file on success. The listener must be stopped.
// read a real tag's identity (UID/ATQA/SAK) into app->scan_* ; no file yet
static bool spy_scan_read(NfcApduSpy* app) {
    Iso14443_3aData* d = iso14443_3a_alloc();
    Iso14443_3aError e = iso14443_3a_poller_sync_read(app->nfc, d);
    bool ok = false;
    if(e == Iso14443_3aErrorNone) {
        size_t ul = 0;
        const uint8_t* uid = iso14443_3a_get_uid(d, &ul);
        app->scan_sak = iso14443_3a_get_sak(d);
        iso14443_3a_get_atqa(d, app->scan_atqa);
        app->scan_uhex[0] = '\0';
        for(size_t i = 0; i < ul && i < 7; i++) snprintf(&app->scan_uhex[i * 2], 3, "%02X", uid[i]);
        ok = app->scan_uhex[0] != '\0';
    }
    iso14443_3a_free(d);
    return ok;
}

// write the scanned identity to a profile file with the given display name
// returns the file name in out_file; the answers are generic (user can refine)
static bool spy_write_scanned(NfcApduSpy* app, const char* name, char* out_file, size_t out_len) {
    char text[256];
    int n = snprintf(
        text,
        sizeof(text),
        "# %s\n"
        "UID %s\n"
        "ATQA %02X%02X\n"
        "SAK %02X\n"
        "ATS 057833A002\n"
        "# edit the answers for a full conversation:\n"
        "00A4 9000\n"
        "* 6D00\n",
        name[0] ? name : app->scan_uhex,
        app->scan_uhex,
        app->scan_atqa[0],
        app->scan_atqa[1],
        app->scan_sak);
    snprintf(out_file, out_len, "scan_%s.txt", app->scan_uhex);
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", PROFILE_DIR, out_file);
    storage_simply_mkdir(app->storage, PROFILE_DIR);
    File* f = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, text, (size_t)n);
        ok = true;
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

static void spy_settings_save(NfcApduSpy* app); // defined below

// rename a profile's display name = rewrite its first "# ..." header line (keep answers)
static void spy_rename_profile(NfcApduSpy* app, int idx, const char* newname) {
    if(idx < 0 || idx >= app->profile_count || !newname[0]) return;
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", PROFILE_DIR, app->profiles[idx].fname);
    char buf[1024];
    size_t rd = 0;
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        rd = storage_file_read(f, buf, sizeof(buf) - 1);
        buf[rd] = '\0';
    }
    storage_file_close(f);
    // body = everything after the first line if that line is a "# ..." header, else whole file
    const char* body = buf;
    if(buf[0] == '#') {
        const char* nl = strchr(buf, '\n');
        body = nl ? nl + 1 : "";
    }
    char out[1100];
    int n = snprintf(out, sizeof(out), "# %s\n%s", newname, body);
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS))
        storage_file_write(f, out, (size_t)n);
    storage_file_close(f);
    storage_file_free(f);
    spy_scan_profiles(app);
    // keep tag_idx pointing at the active file
    for(int i = 0; i < app->profile_count; i++)
        if(strcmp(app->profiles[i].fname, app->active_file) == 0) app->tag_idx = i;
}

// delete a profile file and refresh the list (recreate defaults if none remain)
static void spy_delete_profile(NfcApduSpy* app, int idx) {
    if(idx < 0 || idx >= app->profile_count) return;
    bool was_active = (idx == app->tag_idx);
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", PROFILE_DIR, app->profiles[idx].fname);
    storage_simply_remove(app->storage, path);
    spy_scan_profiles(app);
    if(app->sel_tag >= app->profile_count) app->sel_tag = app->profile_count - 1;
    if(app->sel_tag < 0) app->sel_tag = 0;
    if(app->profile_count == 0) {
        // list is empty now; deletions stay deleted (no auto re-seed)
        app->had_saved_tag = false;
        app->active_file[0] = '\0';
        app->tag_idx = 0;
        spy_apply_profile(app, -1); // generic fallback identity
        spy_settings_save(app);
    } else if(was_active) {
        spy_apply_profile(app, app->sel_tag); // adopt a new active tag
        spy_settings_save(app);
    } else {
        for(int i = 0; i < app->profile_count; i++)
            if(strcmp(app->profiles[i].fname, app->active_file) == 0) app->tag_idx = i;
    }
}

// start a fresh spy session (fresh log, counters, listener on)
static void spy_enter(NfcApduSpy* app) {
    app->cnt_rats = app->cnt_apdu = app->cnt_field = 0;
    app->dropped = 0;
    app->box_n = 0;
    app->typed = 0;
    app->last_activity = 0;
    app->menu = MenuNone;
    app->screen = ScreenWait;
    spy_open_tmp(app);
    app->start_tick = furi_get_tick();
    spy_listener_start(app);
}

// leave the spy and return to the home menu (drop an unsaved temp log)
static void spy_goto_home(NfcApduSpy* app) {
    spy_listener_stop_now(app);
    if(app->file) {
        spy_close_tmp(app);
        storage_simply_remove(app->storage, TMP_PATH);
    }
    app->menu = MenuHome;
}

// ---------- monitor window lines ----------
static void spy_box_add(NfcApduSpy* app, const char* txt) {
    if(app->box_n == BOX_LINES) {
        memmove(app->box[0], app->box[1], sizeof(app->box[0]) * (BOX_LINES - 1));
        app->box_n--;
    }
    strncpy(app->box[app->box_n], txt, BOX_CHARS);
    app->box[app->box_n][BOX_CHARS] = '\0';
    app->box_n++;
    app->typed = 0;
}

static void spy_box_hex(NfcApduSpy* app, char dir, const uint8_t* d, size_t n) {
    char t[BOX_CHARS + 1];
    size_t p = 0;
    t[p++] = dir;
    t[p++] = ' ';
    size_t show = n <= 7 ? n : 6;
    for(size_t i = 0; i < show; i++) {
        snprintf(&t[p], 3, "%02X", d[i]);
        p += 2;
    }
    if(show < n) {
        t[p++] = '.';
        t[p++] = '.';
    }
    t[p] = '\0';
    spy_box_add(app, t);
}

static void spy_process_item(NfcApduSpy* app, const LogItem* it) {
    FuriString* s = furi_string_alloc();
    uint32_t ms = furi_kernel_get_tick_frequency() ?
                      (uint32_t)((uint64_t)(it->tick - app->start_tick) * 1000 /
                                 furi_kernel_get_tick_frequency()) :
                      0;
    furi_string_printf(s, "[%7lu] ", (unsigned long)ms);
    size_t shown = it->len > LOG_DATA_MAX ? LOG_DATA_MAX : it->len;
    bool contact = false;

    switch(it->kind) {
    case LogKindRats:
        app->cnt_rats++;
        contact = true;
        furi_string_cat_str(s, "PHONE> RATS ");
        spy_hex_append(s, it->data, shown);
        furi_string_cat_str(s, "\n          TAG< ATS ");
        spy_hex_append(s, it->resp, it->resp_len);
        spy_box_add(app, "> RATS");
        spy_box_add(app, "< ATS");
        break;
    case LogKindPps:
        contact = true;
        furi_string_cat_str(s, "PHONE> PPS ");
        spy_hex_append(s, it->data, shown);
        spy_box_add(app, "> PPS (speed)");
        break;
    case LogKindApdu: {
        app->cnt_apdu++;
        contact = true;
        bool image = it->len >= 2 && it->data[0] == 0xF0 &&
                     (it->data[1] == 0xD2 || it->data[1] == 0xD3);
        furi_string_cat_str(s, "PHONE> ");
        if(image && it->len > 5) {
            spy_hex_append(s, it->data, 5);
            furi_string_cat_printf(s, " ...(%u bytes)", (unsigned int)it->len);
        } else {
            spy_hex_append(s, it->data, shown);
            if(shown < it->len)
                furi_string_cat_printf(s, " ...(total %u bytes)", (unsigned int)it->len);
        }
        furi_string_cat_str(s, "\n          TAG< ");
        spy_hex_append(s, it->resp, it->resp_len);
        spy_box_hex(app, '>', it->data, shown);
        spy_box_hex(app, '<', it->resp, it->resp_len);
        break;
    }
    case LogKindDeselect:
        contact = true;
        furi_string_cat_str(s, "PHONE> DESELECT (phone ends session)");
        spy_box_add(app, "> DESELECT");
        break;
    case LogKindRBlock:
        contact = true;
        furi_string_cat_str(s, "PHONE> R-block ");
        spy_hex_append(s, it->data, shown);
        furi_string_cat_str(s, " (repeat request)");
        spy_box_add(app, "> R-BLOCK");
        break;
    case LogKindFieldOff:
        app->cnt_field++;
        furi_string_cat_str(s, "--- field off (phone gone / app stopped) ---");
        if(app->cnt_rats || app->cnt_apdu) spy_box_add(app, "-- FIELD OFF --");
        break;
    case LogKindHalt:
        furi_string_cat_str(s, "PHONE> HALT");
        spy_box_add(app, "> HALT");
        break;
    default: {
        char t[BOX_CHARS + 1];
        furi_string_cat_str(s, "PHONE> raw frame ");
        spy_hex_append(s, it->data, shown);
        snprintf(t, sizeof(t), "> RAW %u B", (unsigned int)it->len);
        spy_box_add(app, t);
        break;
    }
    }

    if(contact) {
        app->last_activity = furi_get_tick();
        if(app->screen == ScreenWait) {
            app->screen = ScreenHold;
            app->screen_since = furi_get_tick();
            app->hold_fire = true; // vibrate once
        }
    }

    furi_string_cat_str(s, "\n");
    spy_file_puts(app, furi_string_get_cstr(s));
    furi_string_free(s);
}

// ---------- lamp flicker (gray mode) ----------
static int spy_flicker(void) {
    static const uint8_t patterns[4][6] = {
        {1, 0},
        {3, 0, 6, 0},
        {5, 3, 0, 0, 0, 7},
        {5, 0, 10, 0, 10, 0},
    };
    static uint8_t queue[5];
    static uint8_t qn = 0, qi = 0;
    if(qi < qn) return queue[qi++];
    uint32_t r = furi_hal_random_get();
    if(r % 100 < 5) {
        const uint8_t* p = patterns[(r >> 8) % 4];
        qn = p[0];
        memcpy(queue, &p[1], qn);
        qi = 1;
        return queue[0];
    }
    return ((r >> 12) % 100) < 85 ? 10 : 8;
}

// ---------- bottom bar + buttons ----------
#define SEP_ROW 51 // the picture ends here (same line as the start screen)
static void bar_base(void) {
    gfx_fill(0, 52, 127, 63, 0); // orange bar
    for(int x = 0; x < 128; x++) lev[SEP_ROW][x] = GRAY_P; // separator at the picture edge
}
// back button at the right (sprite)
static void bar_back(void) {
    gfx_button(&btn_back, 128 - btn_back.w, 64 - btn_back.h);
}
static void bar_title(const char* t) {
    gfx_text(&font_status, 2, STATUS_TOP, t, GRAY_P, 63);
}

// ---------- menu list (Flipper-style: white page, black text, inverted row) ----------
#define ROW_Y0 2
#define ROW_H  14
static void menu_page(const char* title) {
    gfx_fill(0, 0, 127, 63, 0);
    bar_base();
    bar_title(title);
    bar_back();
}
static void menu_row_y(int y, const char* left, const char* right, bool sel, bool active, int rw) {
    uint8_t ink = GRAY_P;
    if(sel) {
        gfx_fill(0, y, rw, y + 12, GRAY_P); // bar fully covers the text
        ink = 0;
    }
    if(active) gfx_fill(3, y + 4, 6, y + 8, ink); // marker dot for the active item
    gfx_text(&font_status, 11, y, left, ink, SEP_ROW - 1);
    if(right && right[0])
        gfx_text(&font_status, rw - 3 - gfx_text_width(&font_status, right), y, right, ink, SEP_ROW - 1);
}
static void menu_row(int idx, const char* left, const char* right, bool sel, bool active) {
    menu_row_y(ROW_Y0 + idx * ROW_H, left, right, sel, active, 127);
}

// thin vertical scrollbar on the right (track + thumb), items of a list, top index shown
static void menu_scrollbar(int count, int top, int visible) {
    if(count <= visible) return;
    const int x = 125, y0 = 1, y1 = SEP_ROW - 2, h = y1 - y0;
    for(int y = y0; y <= y1; y++) gfx_px(x, y, GRAY_P, 63); // track
    int th = h * visible / count;
    if(th < 6) th = 6;
    int ty = y0 + (h - th) * top / (count - visible);
    gfx_fill(x - 1, ty, x + 1, ty + th, GRAY_P); // thumb
}

// small Edit/Delete window, opens top-right like the stock Flipper context menu
static void menu_tag_popup(NfcApduSpy* app) {
    const int x0 = 70, y0 = 2, x1 = 124, y1 = 32;
    gfx_fill(x0, y0, x1, y1, 0); // white inside
    for(int x = x0; x <= x1; x++) {
        gfx_px(x, y0, GRAY_P, 63);
        gfx_px(x, y1, GRAY_P, 63);
    }
    for(int y = y0; y <= y1; y++) {
        gfx_px(x0, y, GRAY_P, 63);
        gfx_px(x1, y, GRAY_P, 63);
    }
    static const char* const it[2] = {"Rename", "Delete"};
    for(int i = 0; i < 2; i++) {
        int y = y0 + 3 + i * 14;
        uint8_t ink = GRAY_P;
        if(app->popup_sel == i) {
            gfx_fill(x0 + 1, y - 1, x1 - 1, y + 12, GRAY_P);
            ink = 0;
        }
        gfx_text(&font_status, x0 + 5, y, it[i], ink, y1 - 1);
    }
}

// Info / About: title + version on top, then a scrollable how-to text
static const char* const about_lines[] = {
    "NFC Apdu Spy", // drawn big, with the version small next to it
    "",
    "HOW TO USE",
    "",
    "1. Select a tag from",
    "   the list, or scan",
    "   a new real tag.",
    "2. Start spying and",
    "   hold the phone or",
    "   reader to it.",
    "3. It logs every",
    "   command the sender",
    "   sends + the reply.",
    "4. On exit, save the",
    "   log to review it.",
    "",
    "Only use on devices",
    "you own.",
    "Use responsible.",
    "",
    "App by Menrix",
    "(Don_Key_Moustache)",
};
#define ABOUT_COUNT ((int)(sizeof(about_lines) / sizeof(about_lines[0])))
#define ABOUT_VIS   5 // visible lines (font_name, 8px rows, above the separator)

static void spy_draw_about(NfcApduSpy* app) {
    gfx_fill(0, 0, 127, 63, 0);
    bar_base();
    bar_title("INFO");
    bar_back();
    if(app->about_top < 0) app->about_top = 0;
    if(app->about_top > ABOUT_COUNT - ABOUT_VIS) app->about_top = ABOUT_COUNT - ABOUT_VIS;
    if(app->about_top < 0) app->about_top = 0;
    int y = 1;
    for(int r = 0; r < ABOUT_VIS; r++, y += 9) {
        int i = app->about_top + r;
        if(i >= ABOUT_COUNT) break;
        if(i == 0) {
            // title: big bold name + small version beside it
            gfx_text_bold(&font_status, 2, y, "NFC Apdu Spy", GRAY_P, SEP_ROW - 1);
            int vx = 2 + gfx_text_bold_width(&font_status, "NFC Apdu Spy") + 5;
            gfx_text(&font_name, vx, y + 5, "v" APP_VERSION, GRAY_P, SEP_ROW - 1);
        } else if(strcmp(about_lines[i], "HOW TO USE") == 0) {
            gfx_text_bold(&font_status, 2, y, about_lines[i], GRAY_P, SEP_ROW - 1); // big headline
        } else {
            gfx_text(&font_name, 2, y, about_lines[i], GRAY_P, SEP_ROW - 1);
        }
    }
    menu_scrollbar(ABOUT_COUNT, app->about_top, ABOUT_VIS);
}

static void spy_draw_menu(NfcApduSpy* app) {
    if(app->menu == MenuAbout) {
        spy_draw_about(app);
        return;
    }
    if(app->menu == MenuMain) {
        menu_page("SETTINGS");
        menu_row_y(2, "Graphics", ">", app->sel_main == 0, false, 127);
        menu_row_y(14, "Info / About", ">", app->sel_main == 1, false, 127);
        menu_row_y(26, "Sound", app->sound_on ? "< ON >" : "< OFF >", app->sel_main == 2, false, 127);
        menu_row_y(
            38, "Vibration", app->vibro_on ? "< ON >" : "< OFF >", app->sel_main == 3, false, 127);
    } else if(app->menu == MenuGfx) {
        char s[16];
        menu_page("GRAPHICS");
        snprintf(s, sizeof(s), "< %s >", app->gray ? "GRAY" : "1-BIT");
        menu_row(0, "Mode", s, app->sel_gfx == 0, false);
        snprintf(s, sizeof(s), "< S%d >", g_p);
        menu_row(1, "Grays", s, app->sel_gfx == 1, false);
        snprintf(s, sizeof(s), "< %uHZ >", spy_rates[app->rate_idx]);
        menu_row(2, "Rate", s, app->sel_gfx == 2, false);
    } else if(app->menu == MenuTag) {
        menu_page("TAGS");
        if(app->profile_count == 0) {
            gfx_text(&font_name, 6, 14, "No tags yet.", GRAY_P, SEP_ROW - 1);
            gfx_text(&font_name, 6, 26, "Scan one to add it.", GRAY_P, SEP_ROW - 1);
            return;
        }
        // keep the selected row inside the 3-row window
        if(app->sel_tag < app->tag_top) app->tag_top = app->sel_tag;
        if(app->sel_tag > app->tag_top + 2) app->tag_top = app->sel_tag - 2;
        for(int row = 0; row < 3; row++) {
            int i = app->tag_top + row;
            if(i >= app->profile_count) break;
            menu_row_y(
                ROW_Y0 + row * ROW_H,
                app->profiles[i].name,
                "",
                app->sel_tag == i,
                i == app->tag_idx,
                123); // leave room for the scrollbar
        }
        menu_scrollbar(app->profile_count, app->tag_top, 3);
        if(app->tag_popup) menu_tag_popup(app);
    }
}

// save prompt: NO (left) / YES (right) sprites, "SAVE LOG?" centred
static void spy_draw_save_prompt(void) {
    bar_base();
    gfx_button(&btn_no, 0, 64 - btn_no.h);
    gfx_button(&btn_yes, 128 - btn_yes.w, 64 - btn_yes.h);
    int w = gfx_text_width(&font_status, "SAVE LOG?");
    gfx_text(&font_status, 64 - w / 2, STATUS_TOP, "SAVE LOG?", GRAY_P, 63);
}

#define HOME_ITEMS 4
static const char* const home_items[HOME_ITEMS] = {"Spying", "Select tag", "Scan tag", "Settings"};
// one-line explanation shown in the bottom bar for the highlighted start item
static const char* const home_hints[HOME_ITEMS] = {
    "Spy on sender",
    "Select from list",
    "Scan new tag",
    "Finetune tool",
};

// bottom bar on the start screen:
//   - no tag chosen yet  -> short info for the highlighted item
//   - a tag selected/scanned -> the active tag name (scrolls when too long)
static void spy_draw_hint_bar(NfcApduSpy* app) {
    const char* s;
    if(app->had_saved_tag && app->tag_idx >= 0 && app->tag_idx < app->profile_count)
        s = app->profiles[app->tag_idx].name;
    else
        s = home_hints[app->sel_home];
    const int x0 = 2, win = 92, ny = 54; // small font, inside the bar, no cut descenders
    int w = gfx_text_width(&font_name, s);
    if(w <= win) {
        gfx_text(&font_name, x0, ny, s, GRAY_P, 63);
        return;
    }
    const uint32_t PAUSE = 2000, STEP = 70;
    int total = w - win;
    uint32_t span = (uint32_t)total * STEP, cyc = PAUSE + span + PAUSE;
    uint32_t t = furi_get_tick() % cyc;
    int off = (t < PAUSE) ? 0 : (t < PAUSE + span) ? (int)((t - PAUSE) / STEP) : total;
    gfx_text_clip(&font_name, x0 - off, ny, s, GRAY_P, 63, x0, x0 + win);
}
static void spy_draw_home(NfcApduSpy* app, bool bw) {
    if(bw)
        gfx_bw_base();
    else
        gfx_scene(spy_flicker());
    for(int i = 0; i < HOME_ITEMS; i++)
        gfx_text_outlined(&font_status, 64, 1 + i * 12, home_items[i], 51);
    int ay = 1 + app->sel_home * 12 + 2; // selection cursor, centred on the item
    gfx_tri(53, ay - 1, 11, GRAY_P); // black outline
    gfx_tri(54, ay, 9, 0); // white triangle
    bar_base();
    spy_draw_hint_bar(app);
    gfx_button(&btn_exit, 128 - btn_exit.w, 64 - btn_exit.h);
}

// ---------- paint one picture ----------
static void spy_paint(NfcApduSpy* app, uint32_t pic) {
    uint32_t now = furi_get_tick();
    char status[20];
    const char* word = "";
    int dots = (int)((pic / 8) % 4);

    bool bw = !app->gray;

    if(app->menu == MenuHome) {
        spy_draw_home(app, bw);
        return;
    }
    if(app->menu != MenuNone) {
        spy_draw_menu(app);
        return;
    }

    if(bw)
        gfx_bw_base();
    else
        gfx_scene(spy_flicker());

    switch(app->screen) {
    case ScreenScan:
        gfx_overlay(&ov_scan_tag);
        word = "WAITING";
        break;
    case ScreenScanBusy:
        gfx_overlay(&ov_scan_tag);
        word = "SCANNING";
        break;
    case ScreenScanOk:
        gfx_overlay(&ov_check);
        word = "SUCCESS";
        break;
    case ScreenScanFail:
        gfx_overlay(&ov_fail);
        word = "ERROR";
        break;
    case ScreenWait:
        gfx_overlay(&ov_get_near_target);
        word = app->file ? "WAITING" : "NO LOG FILE";
        break;
    case ScreenHold:
        // blink a few times to say "attention, hold still"
        if((pic / 5) % 2 == 0) gfx_overlay(&ov_hold_still);
        word = "SPYING";
        break;
    case ScreenDone:
        gfx_overlay(&ov_data_saved);
        break;
    case ScreenLog:
    case ScreenAsk: {
        bool busy = (now - app->last_activity) < ACTIVE_MS;
        if(app->screen == ScreenLog && app->typed < (app->box_n ? (int)strlen(app->box[app->box_n - 1]) : 0)) {
            app->typed += 2;
            gfx_log_box(app->box, app->box_n, app->typed, true, 0, bw);
        } else {
            char tmp[BOX_LINES][BOX_CHARS + 1];
            int n = 0;
            int from = app->box_n >= BOX_LINES ? app->box_n - (BOX_LINES - 1) : 0;
            for(int i = from; i < app->box_n; i++) strcpy(tmp[n++], app->box[i]);
            if(n < BOX_LINES) tmp[n++][0] = '\0';
            gfx_log_box(tmp, n, -1, (pic / 6) % 2 == 0, 0, bw);
        }
        word = busy ? "SPYING" : "SCANNING";
        break;
    }
    default:
        break;
    }

    if(app->screen == ScreenAsk) {
        spy_draw_save_prompt();
        return;
    }

    if(app->screen == ScreenDone) {
        // no buttons any more: show the saved file name centred in the bar
        bar_base();
        int w = gfx_text_width(&font_log, app->saved_name);
        gfx_text(&font_log, 64 - w / 2, 56, app->saved_name, GRAY_P, 63);
        return;
    }

    if(app->screen == ScreenNoTag) {
        // notice shown inside the same monitor frame used for the captured commands
        if(bw) {
            gfx_fill(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, GRAY_P);
            gfx_overlay(&ov_frame);
        } else {
            gfx_box_background();
        }
        const int bcx = (BOX_X0 + BOX_X1) / 2; // centre of the window
        static const char* const l[3] = {"Select or", "scan a tag", "first!"};
        int ty = (BOX_Y0 + BOX_Y1) / 2 - 11;
        for(int i = 0; i < 3; i++, ty += 8)
            gfx_text(&font_name, bcx - gfx_text_width(&font_name, l[i]) / 2, ty, l[i], 0, BOX_Y1 - 1);
        bar_base();
        gfx_button(&btn_back, 128 - btn_back.w, 64 - btn_back.h);
        return;
    }

    bool steady = app->screen == ScreenScanOk || app->screen == ScreenScanFail;
    if(steady)
        snprintf(status, sizeof(status), "%s", word);
    else
        snprintf(status, sizeof(status), "%s%.*s", word, dots, "...");
    if(bw)
        gfx_status_text(status);
    else
        gfx_status_bar(status);
}

// ---------- direct screen access ----------
#define RAM_START 0x20000000u
#define RAM_END   0x20040000u
static uint8_t* find_screen_buffer(Canvas* canvas) {
    uintptr_t c = (uintptr_t)canvas;
    if(c < RAM_START || c + 256 * 4 > RAM_END || (c & 3)) return NULL;
    const uint32_t* words = (const uint32_t*)canvas;
    for(int i = 0; i < 256; i++) {
        uint32_t w = words[i];
        if(w < RAM_START || w + 1024 > RAM_END) continue;
        const uint8_t* buf = (const uint8_t*)(uintptr_t)w;
        canvas_clear(canvas);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_dot(canvas, 5, 0);
        canvas_draw_dot(canvas, 3, 9);
        bool ok = buf[5] == 0x01 && buf[128 + 3] == 0x02 && buf[4] == 0 && buf[6] == 0 &&
                  buf[128 + 2] == 0 && buf[128 + 4] == 0;
        canvas_clear(canvas);
        if(ok && buf[5] == 0 && buf[128 + 3] == 0) return (uint8_t*)buf;
    }
    return NULL;
}

static void draw_slow(Canvas* canvas, const uint8_t* picture) {
    static uint8_t xbm[1024];
    memset(xbm, 0, sizeof(xbm));
    for(int y = 0; y < 64; y++)
        for(int x = 0; x < 128; x++)
            if(picture[PX_IDX(x, y)] & PX_BIT(y)) xbm[y * 16 + (x >> 3)] |= (uint8_t)(1 << (x & 7));
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_xbm(canvas, 0, 0, 128, 64, xbm);
}

static void spy_input_cb(const void* value, void* ctx) {
    NfcApduSpy* app = ctx;
    furi_message_queue_put(app->input_queue, value, 0);
}

static void spy_drain_log(NfcApduSpy* app) {
    while(furi_message_queue_get(app->log_queue, &app->item_main, 0) == FuriStatusOk)
        spy_process_item(app, &app->item_main);
}

// ---------- settings file ----------
static void spy_settings_load(NfcApduSpy* app) {
    app->rate_idx = RATE_DEFAULT;
    g_p = 4;
    app->gray = false;
    app->sound_on = true;
    app->vibro_on = true;
    app->active_file[0] = '\0';
    app->first_run = true;
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        app->first_run = false; // settings exist -> not the first run, don't re-seed defaults
        char buf[128] = {0};
        storage_file_read(f, buf, sizeof(buf) - 1);
        int m = 0, s = 0, r = 0, snd = 1, vib = 1;
        char tg[FNAME_LEN] = {0};
        if(sscanf(buf, "v4 gray=%d s=%d rate=%d tag=%41s snd=%d vib=%d", &m, &s, &r, tg, &snd, &vib) >=
           3) {
            app->gray = m != 0;
            if(s >= 3 && s <= GRAY_PMAX) g_p = s;
            for(size_t i = 0; i < RATE_COUNT; i++)
                if(spy_rates[i] == r) app->rate_idx = (int)i;
            strncpy(app->active_file, tg, FNAME_LEN - 1);
            app->had_saved_tag = (tg[0] && tg[0] != '-');
            app->sound_on = snd != 0;
            app->vibro_on = vib != 0;
        }
    }
    storage_file_close(f);
    storage_file_free(f);
}

static void spy_settings_save(NfcApduSpy* app) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[128];
        snprintf(
            buf,
            sizeof(buf),
            "v4 gray=%d s=%d rate=%u tag=%s snd=%d vib=%d\n",
            app->gray,
            g_p,
            spy_rates[app->rate_idx],
            app->active_file[0] ? app->active_file : "-",
            app->sound_on,
            app->vibro_on);
        storage_file_write(f, buf, strlen(buf));
    }
    storage_file_close(f);
    storage_file_free(f);
}

// change a value in the Graphics submenu (dir = -1 / +1)
static void spy_gfx_change(NfcApduSpy* app, int dir) {
    switch(app->sel_gfx) {
    case 0:
        app->gray = !app->gray;
        break;
    case 1:
        g_p += dir;
        if(g_p < 3) g_p = 3;
        if(g_p > GRAY_PMAX) g_p = GRAY_PMAX;
        gfx_build_lut();
        break;
    default:
        app->rate_idx += dir;
        if(app->rate_idx < 0) app->rate_idx = 0;
        if(app->rate_idx >= (int)RATE_COUNT) app->rate_idx = (int)RATE_COUNT - 1;
        break;
    }
}

// ---------- name keyboard (stock TextInput) callbacks ----------
typedef struct {
    ViewDispatcher* vd;
    bool confirmed;
} KbCtx;
static void spy_kb_done(void* ctx) {
    KbCtx* c = ctx;
    c->confirmed = true;
    view_dispatcher_stop(c->vd);
}
static bool spy_kb_back(void* ctx) {
    KbCtx* c = ctx;
    c->confirmed = false;
    view_dispatcher_stop(c->vd);
    return true;
}

// ---------- start ----------
int32_t nfc_apdu_spy_app(void* p) {
    UNUSED(p);
    NfcApduSpy* app = malloc(sizeof(NfcApduSpy));
    memset(app, 0, sizeof(NfcApduSpy));
    app->screen = ScreenWait;

    app->log_queue = furi_message_queue_alloc(LOG_QUEUE_SIZE, sizeof(LogItem));
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->tx = bit_buffer_alloc(MAX_ANSWER + 8);

    spy_settings_load(app);
    storage_simply_mkdir(app->storage, SPY_DIR);
    storage_simply_mkdir(app->storage, PROFILE_DIR);
    if(app->first_run) spy_write_defaults(app); // starter library only on the very first run
    spy_scan_profiles(app); // read profiles/ into the list
    // pick the active profile saved in settings, else the first
    app->tag_idx = 0;
    for(int i = 0; i < app->profile_count; i++)
        if(strcmp(app->profiles[i].fname, app->active_file) == 0) app->tag_idx = i;

    NotificationApp* notif = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notif, &sequence_display_backlight_enforce_on);
    FuriPubSub* input = furi_record_open(RECORD_INPUT_EVENTS);
    FuriPubSubSubscription* sub = furi_pubsub_subscribe(input, spy_input_cb, app);
    Gui* gui = furi_record_open(RECORD_GUI);
    Canvas* canvas = gui_direct_draw_acquire(gui);
    uint8_t* screen = find_screen_buffer(canvas);
    gfx_build_lut();

    app->data = iso14443_3a_alloc();
    spy_apply_profile(app, app->tag_idx); // identity + ATS + answers for the active profile
    app->nfc = nfc_alloc();
    app->listener = NULL; // started only while spying

    // open on the start menu; if a tag was already chosen before, go straight to SPYING
    app->menu = MenuHome;
    app->sel_home = app->had_saved_tag ? 0 : 2; // Spying if a tag is set, else Scan tag
    if(app->first_run) spy_settings_save(app); // mark as run before, so defaults aren't re-seeded

    static uint8_t frame[1024];
    uint32_t cpu_us = furi_hal_cortex_instructions_per_microsecond();
    uint32_t next = DWT->CYCCNT;
    uint32_t pic = 0;
    bool quit = false;

    while(!quit) {
        InputEvent ev;
        while(furi_message_queue_get(app->input_queue, &ev, 0) == FuriStatusOk) {
            bool press = ev.type == InputTypeShort || ev.type == InputTypeRepeat;

            // long-press OK toggles GRAY <-> 1-BIT everywhere, except in the tag
            // list (there a long OK opens the Rename/Delete window)
            if(ev.key == InputKeyOk && ev.type == InputTypeLong && app->menu != MenuTag) {
                app->gray = !app->gray;
                spy_settings_save(app);
                continue;
            }

            // ----- start screen -----
            if(app->menu == MenuHome) {
                if(!press) continue;
                if(ev.key == InputKeyUp)
                    app->sel_home = (app->sel_home + HOME_ITEMS - 1) % HOME_ITEMS;
                else if(ev.key == InputKeyDown)
                    app->sel_home = (app->sel_home + 1) % HOME_ITEMS;
                else if(ev.key == InputKeyOk && ev.type == InputTypeShort) {
                    if(app->sel_home == 0) { // Spying with the active target
                        if(app->had_saved_tag) {
                            spy_enter(app);
                        } else { // no tag chosen yet -> tell the user what to do first
                            app->menu = MenuNone;
                            app->screen = ScreenNoTag;
                            app->screen_since = furi_get_tick();
                        }
                    } else if(app->sel_home == 1) { // Select a saved target
                        app->menu = MenuTag;
                        app->sel_tag = app->tag_idx;
                        app->tag_top = 0;
                    } else if(app->sel_home == 2) { // Scan a real tag
                        app->menu = MenuNone;
                        app->screen = ScreenScan;
                        app->screen_since = furi_get_tick();
                        app->scan_deadline = furi_get_tick() + 12000;
                        app->scan_last_try = 0;
                    } else { // Settings
                        app->menu = MenuMain;
                        app->sel_main = 0;
                        app->settings_from_home = true;
                    }
                } else if(ev.key == InputKeyBack && ev.type == InputTypeShort) {
                    quit = true; // leave the app
                }
                continue;
            }
            // ----- settings: Graphics / Info / Sound / Vibration -----
            if(app->menu == MenuMain) {
                if(!press) continue;
                if(ev.key == InputKeyUp)
                    app->sel_main = (app->sel_main + 3) % 4;
                else if(ev.key == InputKeyDown)
                    app->sel_main = (app->sel_main + 1) % 4;
                else if(ev.key == InputKeyLeft || ev.key == InputKeyRight) {
                    if(app->sel_main == 2) app->sound_on = !app->sound_on;
                    if(app->sel_main == 3) app->vibro_on = !app->vibro_on;
                } else if(ev.key == InputKeyOk && ev.type == InputTypeShort) {
                    if(app->sel_main == 0) {
                        app->menu = MenuGfx;
                        app->sel_gfx = 0;
                    } else if(app->sel_main == 1) {
                        app->menu = MenuAbout;
                        app->about_from_menu = true;
                        app->about_top = 0;
                    } else if(app->sel_main == 2) {
                        app->sound_on = !app->sound_on;
                    } else {
                        app->vibro_on = !app->vibro_on;
                    }
                } else if(ev.key == InputKeyBack && ev.type == InputTypeShort) {
                    spy_settings_save(app);
                    app->menu = app->settings_from_home ? MenuHome : MenuNone;
                    if(app->menu == MenuHome) app->sel_home = 3; // keep Settings highlighted
                }
                continue;
            }
            if(app->menu == MenuGfx) {
                if(!press) continue;
                if(ev.key == InputKeyUp)
                    app->sel_gfx = (app->sel_gfx + 2) % 3;
                else if(ev.key == InputKeyDown)
                    app->sel_gfx = (app->sel_gfx + 1) % 3;
                else if(ev.key == InputKeyLeft)
                    spy_gfx_change(app, -1);
                else if(ev.key == InputKeyRight || ev.key == InputKeyOk)
                    spy_gfx_change(app, +1);
                else if(ev.key == InputKeyBack && ev.type == InputTypeShort) {
                    app->menu = MenuMain;
                    spy_settings_save(app);
                }
                continue;
            }
            if(app->menu == MenuTag) {
                // Edit/Delete popup has priority while it is open
                if(app->tag_popup) {
                    if(!press) continue;
                    if(ev.key == InputKeyUp || ev.key == InputKeyDown)
                        app->popup_sel ^= 1;
                    else if(ev.key == InputKeyOk && ev.type == InputTypeShort) {
                        app->tag_popup = false;
                        if(app->popup_sel == 0) { // Edit: rename via keyboard
                            app->kb_rename = true;
                            strncpy(app->kb_buf, app->profiles[app->sel_tag].name, NAME_LEN - 1);
                            app->kb_buf[NAME_LEN - 1] = '\0';
                            app->want_keyboard = true;
                        } else { // Delete: remove the profile file
                            spy_delete_profile(app, app->sel_tag);
                        }
                    } else if(ev.key == InputKeyBack && ev.type == InputTypeShort) {
                        app->tag_popup = false;
                    }
                    continue;
                }
                // long-press OK opens the Edit/Delete window
                if(ev.key == InputKeyOk && ev.type == InputTypeLong && app->profile_count > 0) {
                    app->tag_popup = true;
                    app->popup_sel = 0;
                    continue;
                }
                if(!press) continue;
                int n = app->profile_count > 0 ? app->profile_count : 1;
                if(ev.key == InputKeyUp)
                    app->sel_tag = (app->sel_tag + n - 1) % n;
                else if(ev.key == InputKeyDown)
                    app->sel_tag = (app->sel_tag + 1) % n;
                else if(
                    ev.key == InputKeyOk && ev.type == InputTypeShort && app->profile_count > 0) {
                    // choose this profile as the active target, back to the menu
                    spy_apply_profile(app, app->sel_tag);
                    app->had_saved_tag = true;
                    spy_settings_save(app);
                    app->menu = MenuHome;
                    app->sel_home = 0; // highlight Spying next
                } else if(ev.key == InputKeyBack && ev.type == InputTypeShort) {
                    app->menu = MenuHome; // back to the start screen
                }
                continue;
            }
            if(app->menu == MenuAbout) {
                if(!press) continue;
                if(ev.key == InputKeyUp)
                    app->about_top--;
                else if(ev.key == InputKeyDown)
                    app->about_top++;
                else if(ev.key == InputKeyBack)
                    app->menu = app->about_from_menu ? MenuMain : MenuHome;
                continue;
            }

            // ----- "choose a tag first" notice -----
            if(app->screen == ScreenNoTag) {
                if(press) app->menu = MenuHome; // any key returns to the start menu
                continue;
            }

            // ----- scan screens -----
            if(app->screen == ScreenScan || app->screen == ScreenScanBusy) {
                if(press && ev.key == InputKeyBack) spy_goto_home(app);
                continue;
            }
            if(app->screen == ScreenScanOk || app->screen == ScreenScanFail) {
                continue; // wait out the result
            }

            // ----- save prompt -----
            if(app->screen == ScreenAsk) {
                if(!press) continue;
                if(ev.key == InputKeyLeft) { // NO: discard, back to home
                    spy_goto_home(app);
                } else if(ev.key == InputKeyRight) { // YES: save now, show path
                    spy_drain_log(app);
                    spy_close_tmp(app);
                    spy_save_log(app);
                    spy_listener_stop_now(app);
                    app->screen = ScreenDone;
                    app->screen_since = furi_get_tick();
                } else if(ev.key == InputKeyBack) { // cancel
                    app->screen = app->prev_screen;
                }
                continue;
            }

            // ----- spy screen -----
            if(app->screen == ScreenDone) continue;
            if(!press) continue;
            if(ev.key == InputKeyOk && ev.type == InputTypeShort) {
                app->menu = MenuMain;
                app->sel_main = 0;
                app->settings_from_home = false;
            } else if(ev.key == InputKeyLeft || ev.key == InputKeyRight) {
                app->gray = !app->gray; // switch graphics mode
                spy_settings_save(app);
            } else if(ev.key == InputKeyBack && ev.type == InputTypeShort) {
                if(app->cnt_rats || app->cnt_apdu) {
                    app->prev_screen = app->screen;
                    app->screen = ScreenAsk; // ask to save
                } else {
                    spy_goto_home(app); // nothing captured -> back to home
                }
            }
        }
        if(quit) break;

        spy_drain_log(app);

        // vibrate once on the first contact
        if(app->hold_fire) {
            app->hold_fire = false;
            if(app->vibro_on) notification_message(notif, &sequence_single_vibro);
        }

        uint32_t now = furi_get_tick();
        if(app->screen == ScreenHold && now - app->screen_since > HOLD_SHOW_MS)
            app->screen = ScreenLog;

        // scan: poll the real tag every ~250 ms (reader mode; listener is off)
        if(app->menu == MenuNone && app->screen == ScreenScan) {
            if(now - app->scan_last_try > 250) {
                app->scan_last_try = now;
                if(spy_scan_read(app)) { // got the identity; name + save come after the keyboard
                    app->screen = ScreenScanBusy; // show "SCANNING..." briefly
                    app->screen_since = now;
                } else if(now > app->scan_deadline) {
                    app->screen = ScreenScanFail;
                    app->screen_since = now;
                    if(app->sound_on) notification_message(notif, &seq_fail_sound);
                    if(app->vibro_on) notification_message(notif, &seq_vibro_long);
                }
            }
        }
        if(app->screen == ScreenScanBusy && now - app->screen_since > SCAN_BUSY_MS) {
            app->screen = ScreenScanOk;
            app->screen_since = now;
            if(app->sound_on) notification_message(notif, &seq_ok_sound);
            if(app->vibro_on) notification_message(notif, &sequence_single_vibro);
        }
        if(app->screen == ScreenScanOk && now - app->screen_since > SCAN_SHOW_MS) {
            // let the user name the freshly scanned tag (Flipper keyboard)
            snprintf(app->kb_buf, NAME_LEN, "Scan %s", app->scan_uhex);
            app->kb_rename = false;
            app->want_keyboard = true;
            app->screen = ScreenWait; // consume this state so the keyboard is armed only once
        }
        if(app->screen == ScreenScanFail && now - app->screen_since > SCAN_SHOW_MS)
            spy_goto_home(app);
        if(app->screen == ScreenNoTag && now - app->screen_since > 4000)
            app->menu = MenuHome; // auto-dismiss the notice
        if(app->screen == ScreenDone && now - app->screen_since > DONE_SHOW_MS)
            spy_goto_home(app);

        // ----- name keyboard (stock Flipper text input) -----
        // Runs its own event loop, so we hand the GUI/input back for the duration
        // and re-acquire direct draw afterwards.
        if(app->want_keyboard) {
            app->want_keyboard = false;
            gui_direct_draw_release(gui);
            furi_pubsub_unsubscribe(input, sub);

            ViewDispatcher* vd = view_dispatcher_alloc();
            TextInput* ti = text_input_alloc();
            KbCtx kc = {.vd = vd, .confirmed = false};
            text_input_set_header_text(ti, app->kb_rename ? "Rename tag" : "Name this tag");
            text_input_set_result_callback(
                ti, spy_kb_done, &kc, app->kb_buf, NAME_LEN, !app->kb_rename);
            view_dispatcher_set_event_callback_context(vd, &kc);
            view_dispatcher_set_navigation_event_callback(vd, spy_kb_back);
            view_dispatcher_add_view(vd, 0, text_input_get_view(ti));
            view_dispatcher_attach_to_gui(vd, gui, ViewDispatcherTypeFullscreen);
            view_dispatcher_switch_to_view(vd, 0);
            view_dispatcher_run(vd);
            view_dispatcher_remove_view(vd, 0);
            text_input_free(ti);
            view_dispatcher_free(vd);

            // re-acquire the direct framebuffer and input
            canvas = gui_direct_draw_acquire(gui);
            screen = find_screen_buffer(canvas);
            sub = furi_pubsub_subscribe(input, spy_input_cb, app);

            if(app->kb_rename) {
                if(kc.confirmed) spy_rename_profile(app, app->sel_tag, app->kb_buf);
                app->menu = MenuTag; // stay in the list
            } else {
                char file[FNAME_LEN];
                if(spy_write_scanned(app, app->kb_buf, file, sizeof(file))) {
                    spy_scan_profiles(app);
                    for(int i = 0; i < app->profile_count; i++)
                        if(strcmp(app->profiles[i].fname, file) == 0) app->tag_idx = i;
                    strncpy(app->active_file, file, FNAME_LEN - 1);
                    app->active_file[FNAME_LEN - 1] = '\0';
                    spy_apply_profile(app, app->tag_idx);
                    app->had_saved_tag = true;
                    spy_settings_save(app);
                }
                spy_goto_home(app);
                app->sel_home = 0; // highlight Spying
            }
            // drop any input queued while the keyboard was up; reset the frame clock
            InputEvent drop;
            while(furi_message_queue_get(app->input_queue, &drop, 0) == FuriStatusOk) {
            }
            next = DWT->CYCCNT;
            continue;
        }

        // draw one picture (gray = several subframes; the white menus are always 1-bit)
        bool gmode = app->gray && (app->menu == MenuNone || app->menu == MenuHome);
        spy_paint(app, pic++);
        uint32_t sub_us = 1000000U / spy_rates[app->rate_idx];
        // 1-bit screens/menus refresh at a steady ~30 fps (like the Flipper GUI) so
        // scrolling text stays crisp; the rate setting only drives gray temporal dithering.
        uint32_t flat_us = 1000000U / 30;
        int subframes = gmode ? GRAY_P : 1;
        for(int sf = 0; sf < subframes; sf++) {
            next += cpu_us * (gmode ? sub_us : flat_us);
            int32_t wait = (int32_t)(next - DWT->CYCCNT) / (int32_t)cpu_us;
            if(wait < -50000) {
                next = DWT->CYCCNT;
                wait = 0;
            }
            if(wait > 1500)
                furi_delay_ms((uint32_t)(wait - 500) / 1000);
            else
                furi_delay_tick(1);
            while((int32_t)(next - DWT->CYCCNT) > 0) furi_thread_yield();

            gfx_render_subframe(sf, frame, gmode);
            if(screen)
                memcpy(screen, frame, 1024);
            else
                draw_slow(canvas, frame);
            canvas_commit(canvas);
        }
    }

    spy_listener_stop_now(app);
    nfc_free(app->nfc);

    // if the temp file is still open (unexpected exit), drop it
    if(app->file) {
        spy_close_tmp(app);
        storage_simply_remove(app->storage, TMP_PATH);
    }

    gui_direct_draw_release(gui);
    furi_record_close(RECORD_GUI);
    furi_pubsub_unsubscribe(input, sub);
    furi_record_close(RECORD_INPUT_EVENTS);
    notification_message(notif, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    iso14443_3a_free(app->data);
    bit_buffer_free(app->tx);
    furi_message_queue_free(app->input_queue);
    furi_message_queue_free(app->log_queue);
    free(app);
    return 0;
}
