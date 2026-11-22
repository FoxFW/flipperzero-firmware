#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/number_input.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <loader/loader.h>
#include <string.h>
#include <furi_hal_random.h>

#define TAG "SubGhzAddManually"
#define HANDOFF_PATH EXT_PATH("subghz/assets/.add_manual")
#define HANDOFF_TYPE "Fox SubGhz Add Manual"
#define HANDOFF_VER  1

typedef enum {
    AmViewSubmenu,
    AmViewByteInput,
    AmViewNumInput,
} AmView;

typedef enum {
    AmEventFieldDone = 100,
    AmEventNumDone,
} AmEvent;

/* Field-entry categories. Mirrors the per-GenType keypad flow of the resident
 * set_serial/set_button/set_counter/set_seed/set_key scenes. */
typedef enum {
    CAT_NONE = 0,            /* Security+1.0: no fields, generate immediately */
    CAT_KEY,                 /* GenData: key(8) */
    CAT_SER_CNT2,            /* CameAtomo, PhoenixV2: serial(4), cnt(2) */
    CAT_SER_BTN_CNT2,        /* Keeloq & friends: serial(4), btn(1), cnt(2) */
    CAT_SER_BTN_CNT4,        /* SecPlus2, BenincaARC: serial(4), btn(1), cnt(4) */
    CAT_SER_BTN_CNT2_SEED,   /* KeeloqBFT: serial(4), btn(1), cnt(2), seed(4) */
    CAT_SER_BTN_CNT4_SEED,   /* FaacSLH: serial(4), btn(1), cnt(4), seed(4) */
    CAT_MAX,
} FieldCat;

typedef enum { F_SERIAL, F_BTN, F_CNT2, F_CNT4, F_SEED, F_KEY } FieldKind;
typedef enum { NUM_TE, NUM_FREQ } NumStage;

#define MAX_FIELDS 4

static const FieldKind cat_fields[CAT_MAX][MAX_FIELDS] = {
    [CAT_NONE] = {0},
    [CAT_KEY] = {F_KEY},
    [CAT_SER_CNT2] = {F_SERIAL, F_CNT2},
    [CAT_SER_BTN_CNT2] = {F_SERIAL, F_BTN, F_CNT2},
    [CAT_SER_BTN_CNT4] = {F_SERIAL, F_BTN, F_CNT4},
    [CAT_SER_BTN_CNT2_SEED] = {F_SERIAL, F_BTN, F_CNT2, F_SEED},
    [CAT_SER_BTN_CNT4_SEED] = {F_SERIAL, F_BTN, F_CNT4, F_SEED},
};
static const uint8_t cat_field_count[CAT_MAX] = {
    [CAT_NONE] = 0,
    [CAT_KEY] = 1,
    [CAT_SER_CNT2] = 2,
    [CAT_SER_BTN_CNT2] = 3,
    [CAT_SER_BTN_CNT4] = 3,
    [CAT_SER_BTN_CNT2_SEED] = 4,
    [CAT_SER_BTN_CNT4_SEED] = 4,
};
static const char* const field_header[] = {
    [F_SERIAL] = "Enter SERIAL in hex",
    [F_BTN] = "Enter BUTTON in hex",
    [F_CNT2] = "Enter COUNTER in hex",
    [F_CNT4] = "Enter COUNTER in hex",
    [F_SEED] = "Enter SEED in hex",
    [F_KEY] = "Enter KEY in hex",
};
static const uint8_t field_width[] = {
    [F_SERIAL] = 4, [F_BTN] = 1, [F_CNT2] = 2, [F_CNT4] = 4, [F_SEED] = 4, [F_KEY] = 8,
};

/* Protocol table (index == subghz SetType). Generated from
 * subghz_custom_event.h / subghz_scene_set_type.c / subghz_gen_info.c. */
static const char* const proto_names[] = {
    "FAAC SLH 868MHz",
    "FAAC SLH 433MHz",
    "BFT Mitto 433MHz",
    "Somfy Telis 433MHz",
    "KingGates Stylo4k 433M.",
    "Beninca ARC 433MHz",
    "Jarolift 433MHz",
    "AN-Motors AT4 433MHz",
    "Alutech AT4N 433MHz",
    "V2 Phoenix 433MHz",
    "KL: HCS101 433MHz",
    "KL: DoorHan 315MHz",
    "KL: DoorHan 433MHz",
    "KL: Beninca 433MHz",
    "KL: Beninca 868MHz",
    "KL: Comunello 433MHz",
    "KL: Comunello 868MHz",
    "KL: Allmatic 433MHz",
    "KL: Allmatic 868MHz",
    "KL: Centurion 433MHz",
    "KL: Monarch 433MHz",
    "KL: Phantom/Hiland 433M",
    "KL: Jolly Mot. 433MHz",
    "KL: Motorline 433MHz",
    "KL: Sommer 434MHz",
    "KL: Sommer 868MHz",
    "KL: Sommer fm2 434Mhz",
    "KL: Sommer fm2 868Mhz",
    "KL: Stilmatic 433MHz",
    "KL: IronLogic 433MHz",
    "KL: IronLogic SM 433MHz",
    "KL: DEA Mio 433MHz",
    "KL: DTM Neo 433MHz",
    "KL: Gibidi 433MHz",
    "KL: GSN 433MHz",
    "KL: Aprimatic 433MHz",
    "KL: Erreka 433MHz",
    "KL: Erreka 868MHz",
    "KL: Elmes (PL) 433MHz",
    "KL: Normstahl 433MHz",
    "KL: JCM Tech 433MHz",
    "KL: Novoferm 433MHz",
    "KL: Hor. EcoStar 433MHz",
    "KL: Cardin S449 433MHz",
    "KL: FAAC RC,XT 433MHz",
    "KL: FAAC RC,XT 868MHz",
    "KL: Genius Bravo 433MHz",
    "KL: Mhouse 433MHz",
    "KL: Nice Smilo 433MHz",
    "Nice FloR-S 433MHz",
    "Nice One 433MHz",
    "Nice Flo 12bit 433MHz",
    "Nice Flo 24bit 433MHz",
    "CAME 12bit 433MHz",
    "CAME 24bit 433MHz",
    "CAME 12bit 868MHz",
    "CAME 24bit 868MHz",
    "CAME TWEE 433MHz",
    "CAME Atomo 433MHz",
    "CAME Atomo 868MHz",
    "KL: CAME Space 433MHz",
    "Princeton 315MHz",
    "Princeton 433MHz",
    "ZKTeco 430MHz",
    "BETT 433MHz",
    "GangQi 433MHz",
    "Hollarm 433MHz",
    "Revers RB2 433MHz",
    "Marantec24 868MHz",
    "Marantec 433MHz",
    "Marantec 868MHz",
    "Roger 433MHz",
    "Linear 300MHz",
    "Gate TX 433MHz",
    "Security+1.0 315MHz",
    "Security+1.0 390MHz",
    "Security+1.0 433MHz",
    "Security+2.0 310MHz",
    "Security+2.0 315MHz",
    "Security+2.0 390MHz",
    "Security+2.0 433MHz",
};
static const uint8_t proto_cats[] = {
    CAT_SER_BTN_CNT4_SEED,
    CAT_SER_BTN_CNT4_SEED,
    CAT_SER_BTN_CNT2_SEED,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT4,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_SER_CNT2,
    CAT_SER_CNT2,
    CAT_SER_BTN_CNT2,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_KEY,
    CAT_NONE,
    CAT_NONE,
    CAT_NONE,
    CAT_SER_BTN_CNT4,
    CAT_SER_BTN_CNT4,
    CAT_SER_BTN_CNT4,
    CAT_SER_BTN_CNT4
};
#define PROTO_COUNT (sizeof(proto_names) / sizeof(proto_names[0]))

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    ByteInput* byte_input;
    NumberInput* number_input;

    AmView view;
    uint32_t set_type;
    FieldCat cat;
    uint8_t field_idx;
    uint8_t field_count;

    uint8_t buf[8];

    uint32_t serial;
    uint32_t btn;
    uint32_t cnt;
    uint32_t seed;
    uint8_t key[8];
    uint32_t frequency;
    uint32_t te;
    uint8_t num_stage;
    int32_t num_result;
} AmApp;

static void am_byte_input_done(void* context);

static void am_show_field(AmApp* app, uint8_t idx) {
    FieldKind kind = cat_fields[app->cat][idx];
    uint8_t w = field_width[kind];
    memset(app->buf, 0, sizeof(app->buf));
    /* Pre-fill with the seeded default (big-endian) so the user can accept it with OK. */
    if(kind == F_KEY) {
        memcpy(app->buf, app->key, sizeof(app->key));
    } else {
        uint32_t dv = (kind == F_SERIAL) ? app->serial :
                      (kind == F_BTN)    ? app->btn :
                      (kind == F_SEED)   ? app->seed :
                                           app->cnt;
        for(uint8_t i = 0; i < w; i++) app->buf[i] = (uint8_t)(dv >> (8 * (w - 1 - i)));
    }
    byte_input_set_header_text(app->byte_input, field_header[kind]);
    byte_input_set_result_callback(
        app->byte_input,
        am_byte_input_done,
        NULL,
        app,
        app->buf,
        w);
    app->field_idx = idx;
    app->view = AmViewByteInput;
    view_dispatcher_switch_to_view(app->view_dispatcher, AmViewByteInput);
}

static void am_write_handoff(AmApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_always(ff, HANDOFF_PATH)) {
        do {
            if(!flipper_format_write_header_cstr(ff, HANDOFF_TYPE, HANDOFF_VER)) break;
            if(!flipper_format_write_uint32(ff, "SetType", &app->set_type, 1)) break;
            for(uint8_t i = 0; i < app->field_count; i++) {
                FieldKind kind = cat_fields[app->cat][i];
                if(kind == F_SERIAL) {
                    flipper_format_write_uint32(ff, "Serial", &app->serial, 1);
                } else if(kind == F_BTN) {
                    flipper_format_write_uint32(ff, "Btn", &app->btn, 1);
                } else if(kind == F_CNT2 || kind == F_CNT4) {
                    flipper_format_write_uint32(ff, "Cnt", &app->cnt, 1);
                } else if(kind == F_SEED) {
                    flipper_format_write_uint32(ff, "Seed", &app->seed, 1);
                } else if(kind == F_KEY) {
                    flipper_format_write_hex(ff, "Key", app->key, 8);
                }
            }
            flipper_format_write_uint32(ff, "Frequency", &app->frequency, 1);
            if(app->cat == CAT_KEY && app->te) {
                flipper_format_write_uint32(ff, "TE", &app->te, 1);
            }
        } while(0);
    }
    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void am_launch_subghz(void) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(loader, "subghz", NULL, LoaderDeferredLaunchFlagNone);
    furi_record_close(RECORD_LOADER);
}

/* Forward path: a protocol is fully specified -> write handoff + launch subghz,
 * which detects the handoff and generates the key, then goes to Save. */
static void am_commit_and_launch(AmApp* app) {
    am_write_handoff(app);
    am_launch_subghz();
    view_dispatcher_stop(app->view_dispatcher);
}

/* Back path from the protocol list: just return to SubGHz (its Start menu).
 * Stage 3 pre-writes the .focus_menu marker so Add Manually stays selected. */
static void am_back_to_subghz(AmApp* app) {
    am_launch_subghz();
    view_dispatcher_stop(app->view_dispatcher);
}

static void am_num_cb(void* context, int32_t number) {
    AmApp* app = context;
    app->num_result = number;
    view_dispatcher_send_custom_event(app->view_dispatcher, AmEventNumDone);
}

static void am_show_num(AmApp* app) {
    if(app->num_stage == NUM_TE) {
        number_input_set_header_text(app->number_input, "TE (us), 0 = protocol default");
        number_input_set_result_callback(
            app->number_input, am_num_cb, app, (int32_t)app->te, 0, 65535);
    } else {
        number_input_set_header_text(app->number_input, "Frequency (Hz)");
        number_input_set_result_callback(
            app->number_input, am_num_cb, app, (int32_t)app->frequency, 300000000, 928000000);
    }
    app->view = AmViewNumInput;
    view_dispatcher_switch_to_view(app->view_dispatcher, AmViewNumInput);
}

static void am_start_num(AmApp* app) {
    /* GenData (CAT_KEY) protocols get an optional TE step first (0 = protocol
     * default); every protocol then gets the frequency step. */
    app->num_stage = (app->cat == CAT_KEY) ? NUM_TE : NUM_FREQ;
    am_show_num(app);
}

static void am_num_done(AmApp* app) {
    if(app->num_stage == NUM_TE) {
        app->te = (uint32_t)app->num_result;
        app->num_stage = NUM_FREQ;
        am_show_num(app);
    } else {
        app->frequency = (uint32_t)app->num_result;
        am_commit_and_launch(app);
    }
}

static void am_submenu_cb(void* context, uint32_t index) {
    AmApp* app = context;
    if(index >= PROTO_COUNT) return;
    app->set_type = index;
    app->cat = (FieldCat)proto_cats[index];
    app->field_count = cat_field_count[app->cat];
    /* Seed sensible defaults (Unleashed-style): Add Manually -> <proto> -> OK/OK/OK -> Save makes a
     * working random-serial remote with no hand-typed hex. Pre-filled in am_show_field; still editable. */
    app->serial = furi_hal_random_get() & 0x0FFFFFFF;
    app->btn = 0x01;
    app->cnt = 0x01;
    app->seed = furi_hal_random_get();
    furi_hal_random_fill_buf(app->key, sizeof(app->key));
    app->frequency = 433920000;
    app->te = 0;
    if(app->field_count == 0) {
        am_start_num(app);
        return;
    }
    am_show_field(app, 0);
}

static void am_byte_input_done(void* context) {
    AmApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, AmEventFieldDone);
}

static void am_field_done(AmApp* app) {
    FieldKind kind = cat_fields[app->cat][app->field_idx];
    uint8_t w = field_width[kind];
    if(kind == F_KEY) {
        memcpy(app->key, app->buf, 8);
    } else {
        uint32_t v = 0;
        for(uint8_t i = 0; i < w; i++) v = (v << 8) | app->buf[i];
        if(kind == F_SERIAL)
            app->serial = v;
        else if(kind == F_BTN)
            app->btn = v;
        else if(kind == F_CNT2 || kind == F_CNT4)
            app->cnt = v;
        else if(kind == F_SEED)
            app->seed = v;
    }
    uint8_t next = app->field_idx + 1;
    if(next < app->field_count) {
        am_show_field(app, next);
    } else {
        am_start_num(app);
    }
}

static bool am_custom_event_cb(void* context, uint32_t event) {
    AmApp* app = context;
    if(event == AmEventFieldDone) {
        am_field_done(app);
        return true;
    }
    if(event == AmEventNumDone) {
        am_num_done(app);
        return true;
    }
    return false;
}

static bool am_nav_cb(void* context) {
    AmApp* app = context;
    if(app->view == AmViewNumInput) {
        if(app->num_stage == NUM_FREQ && app->cat == CAT_KEY) {
            app->num_stage = NUM_TE;
            am_show_num(app);
        } else {
            app->view = AmViewSubmenu;
            submenu_set_selected_item(app->submenu, app->set_type);
            view_dispatcher_switch_to_view(app->view_dispatcher, AmViewSubmenu);
        }
        return true;
    }
    if(app->view == AmViewByteInput) {
        if(app->field_idx == 0) {
            app->view = AmViewSubmenu;
            /* Restore the cursor to the protocol just backed out of (set_type == the
             * submenu item index) instead of resetting to the top of the list. */
            submenu_set_selected_item(app->submenu, app->set_type);
            view_dispatcher_switch_to_view(app->view_dispatcher, AmViewSubmenu);
        } else {
            am_show_field(app, app->field_idx - 1);
        }
        return true;
    }
    /* On the protocol list: Back returns to SubGHz. */
    am_back_to_subghz(app);
    return true;
}

int32_t subghz_add_manually_app(void* p) {
    UNUSED(p);
    AmApp* app = malloc(sizeof(AmApp));
    memset(app, 0, sizeof(AmApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->byte_input = byte_input_alloc();
    app->number_input = number_input_alloc();

    submenu_set_header(app->submenu, "Add Manually");
    /* Display protocols alphabetically (Batch 2 Item A). Each item's VALUE stays
     * its ORIGINAL proto_names[] index, so the handoff (set_type == value) and the
     * focus-restore (submenu_set_selected_item matches by value, not position) stay
     * correct regardless of display order. */
    size_t order[PROTO_COUNT];
    for(size_t i = 0; i < PROTO_COUNT; i++) order[i] = i;
    for(size_t a = 1; a < PROTO_COUNT; a++) {
        size_t key = order[a], b = a;
        while(b > 0 && strcmp(proto_names[order[b - 1]], proto_names[key]) > 0) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = key;
    }
    for(size_t s = 0; s < PROTO_COUNT; s++) {
        submenu_add_item(app->submenu, proto_names[order[s]], order[s], am_submenu_cb, app);
    }

    view_dispatcher_add_view(app->view_dispatcher, AmViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, AmViewByteInput, byte_input_get_view(app->byte_input));
    view_dispatcher_add_view(
        app->view_dispatcher, AmViewNumInput, number_input_get_view(app->number_input));

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, am_custom_event_cb);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, am_nav_cb);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->view = AmViewSubmenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, AmViewSubmenu);
    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(app->view_dispatcher, AmViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, AmViewByteInput);
    view_dispatcher_remove_view(app->view_dispatcher, AmViewNumInput);
    submenu_free(app->submenu);
    byte_input_free(app->byte_input);
    number_input_free(app->number_input);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
