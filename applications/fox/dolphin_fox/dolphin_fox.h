#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>

#define DF_TAG "DolphinFox"

#define DF_ANIM_DIR  EXT_PATH("apps_data/dolphin_fox")
#define DF_DATA_DIR  EXT_PATH("apps_data/dolphin_fox")
#define DF_CONF_PATH DF_DATA_DIR "/dolphin_fox.conf"

#define DF_NAME_MAX       64
#define DF_LEVEL_MIN      1
#define DF_LEVEL_MAX      30
#define DF_ANIM_LEVEL_MAX 3
#define DF_MOOD_COUNT     3
#define DF_BUTTHURT_MAX   14

typedef enum {
    DfScreenAnim,
    DfScreenCard,
} DfScreen;

typedef enum {
    DfSelNone,
    DfSelMood,
    DfSelLevel,
} DfSel;

typedef enum {
    DfEventInput,
    DfEventTick,
} DfEventType;

typedef struct {
    DfEventType type;
    InputEvent input;
} DfEvent;

typedef struct {
    char name[DF_NAME_MAX];
    uint8_t min_butthurt;
    uint8_t max_butthurt;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t weight;
} DfAnimInfo;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriTimer* frame_timer;
    FuriMutex* mutex;
    Storage* storage;

    DfAnimInfo* anims;
    size_t anim_count;
    int current;

    uint8_t** frames;
    size_t frame_slots;
    uint8_t* seq;
    size_t seq_len;
    size_t pos;
    uint8_t width;
    uint8_t height;
    uint32_t frame_period_ms;

    DfScreen screen;
    DfSel sel;
    uint8_t mood;
    uint8_t level;

    bool running;
} DfApp;

void df_library_build(DfApp* app);
void df_library_free(DfApp* app);
int df_library_pick(DfApp* app);

bool df_player_load(DfApp* app, int index);
void df_player_unload(DfApp* app);
void df_player_draw(DfApp* app, Canvas* canvas);

void df_card_draw(DfApp* app, Canvas* canvas);

uint8_t df_mood_butthurt(uint8_t mood);
