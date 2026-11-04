#include "dolphin_fox.h"
#include "dolphin_fox_icons.h"

#include <furi_hal_version.h>

static const Icon* const df_portrait_happy[DF_ANIM_LEVEL_MAX] = {
    &I_passport_happy1_46x49,
    &I_passport_happy2_46x49,
    &I_passport_happy3_46x49};
static const Icon* const df_portrait_ok[DF_ANIM_LEVEL_MAX] = {
    &I_passport_okay1_46x49,
    &I_passport_okay2_46x49,
    &I_passport_okay3_46x49};
static const Icon* const df_portrait_bad[DF_ANIM_LEVEL_MAX] = {
    &I_passport_bad1_46x49,
    &I_passport_bad2_46x49,
    &I_passport_bad3_46x49};
static const Icon* const* df_portraits[DF_MOOD_COUNT] =
    {df_portrait_happy, df_portrait_ok, df_portrait_bad};

static const char* df_mood_name(uint8_t mood) {
    switch(mood) {
    case 0:
        return "Happy";
    case 1:
        return "Ok";
    default:
        return "Angry";
    }
}

void df_card_draw(DfApp* app, Canvas* canvas) {
    char level_str[24];
    char mood_str[24];

    uint8_t eff_level = app->level;
    if(eff_level > DF_ANIM_LEVEL_MAX) eff_level = DF_ANIM_LEVEL_MAX;
    if(eff_level < 1) eff_level = 1;
    uint8_t mood = (app->mood < DF_MOOD_COUNT) ? app->mood : 0;

    snprintf(mood_str, sizeof(mood_str), "Mood: %s", df_mood_name(mood));
    snprintf(level_str, sizeof(level_str), "Level: %u", (unsigned)app->level);

    canvas_draw_icon(canvas, 0, 0, &I_passport_left_6x46);
    canvas_draw_icon(canvas, 0, 46, &I_passport_bottom_128x18);
    canvas_draw_line(canvas, 6, 0, 125, 0);
    canvas_draw_line(canvas, 127, 2, 127, 47);
    canvas_draw_dot(canvas, 126, 1);

    canvas_draw_icon(canvas, 9, 5, df_portraits[mood][eff_level - 1]);

    canvas_draw_line(canvas, 58, 16, 123, 16);
    canvas_draw_line(canvas, 58, 30, 123, 30);
    canvas_draw_line(canvas, 58, 44, 123, 44);

    canvas_set_font(canvas, FontPrimary);

    const char* my_name = furi_hal_version_get_name_ptr();
    canvas_draw_str(canvas, 58, 12, my_name ? my_name : "Unknown");

    canvas_set_color(canvas, ColorBlack);
    if(app->sel == DfSelMood) {
        canvas_draw_box(canvas, 56, 17, 71, 12);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 58, 26, mood_str);
    canvas_set_color(canvas, ColorBlack);

    if(app->sel == DfSelLevel) {
        canvas_draw_box(canvas, 56, 31, 71, 12);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 58, 40, level_str);
    canvas_set_color(canvas, ColorBlack);

    uint32_t span = DF_LEVEL_MAX - DF_LEVEL_MIN;
    uint32_t prog = (uint32_t)app->level - DF_LEVEL_MIN;
    uint8_t xp_progress = (uint8_t)(64 - (prog * 64 / span));
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 123 - xp_progress, 47, xp_progress + 1, 6);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_line(canvas, 123, 47, 123, 52);
}
