#pragma once

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>

#include "scenes/fmh_scene.h"
#include "views/fmh_box_list.h"
#include "helpers/fmh_vehicle_data.h"

#define FMH_NAME_MAX    48
#define FMH_RESULT_MAX  8
#define FMH_SUB_MAX     40
#define FMH_HEADER_MAX  96

typedef enum {
    FmhViewSubmenu,
    FmhViewBoxList,
} FmhViewId;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    FmhBoxList* box_list;

    FmhStrList makes;
    FmhStrList models;
    FmhYearList years;
    FmhResultList results;

    char make[FMH_NAME_MAX];
    char model[FMH_NAME_MAX];
    bool make_set;
    bool make_direct;
    bool model_set;
    int32_t year_from;
    int32_t year_to;
    uint16_t year_index;

    FmhBoxListOption start_options[2];
    char start_make_sub[FMH_NAME_MAX];

    FmhBoxListOption result_options[FMH_RESULT_MAX];
    char result_titles[FMH_RESULT_MAX][FMH_REGION_LEN];
    char result_subs[FMH_RESULT_MAX][FMH_SUB_MAX];
    char result_header[FMH_HEADER_MAX];

    bool launch_requested;
    uint32_t launch_freq;
    uint8_t launch_preset;
    char launch_mode;
} FmhApp;
