#include <applications.h>
#include <lib/toolbox/value_index.h>
#include <gui/modules/fox_theme.h>
#include <cli/cli_settings.h>
#include <gpio_remap/gpio_remap_settings.h>

#include "../desktop_settings_app.h"
#include "desktop_settings_scene.h"
#include "desktop_settings_scene_i.h"
#include "desktop_settings_icons.h"

typedef enum {
    DesktopSettingsPinSetup           = 0,
    DesktopSettingsUsbMode            = 1,
    DesktopSettingsMenuStyle          = 2,
    DesktopSettingsWallpaper          = 3,
    DesktopSettingsChangeName         = 4,
    DesktopSettingsMainMenu           = 5,
    DesktopSettingsAlarmClock         = 6,
    DesktopSettingsRgbBacklight       = 7,
    DesktopSettingsVgmOptions         = 8,

    DesktopSettingsFavoriteLeftShort  = 17,
    DesktopSettingsFavoriteLeftLong   = 18,
    DesktopSettingsFavoriteRightShort = 19,
    DesktopSettingsFavoriteOkLong     = 20,
    DesktopSettingsFavoriteDownShort  = 21,

} DesktopSettingsEntry;

#define CLOCK_ENABLE_COUNT 2
static const char* const clock_enable_text[CLOCK_ENABLE_COUNT]  = {"OFF", "ON"};
static const uint32_t    clock_enable_value[CLOCK_ENABLE_COUNT] = {0, 1};

#define WIFI_ICON_COUNT 2
static const char* const wifi_icon_text[WIFI_ICON_COUNT] = {"ON", "OFF"};

#define STATUSBAR_ICONS_COUNT 2
static const char* const statusbar_icons_text[STATUSBAR_ICONS_COUNT] = {"OFF", "ON"};

#define MIDNIGHT_FORMAT_COUNT 2
static const char* const midnight_format_text[MIDNIGHT_FORMAT_COUNT] = {"12", "0"};

#define SHELL_COLOR_COUNT 8
static const char* const shell_color_text[SHELL_COLOR_COUNT] = {
    "Orange", "Red", "Green", "Yellow", "Blue", "Magenta", "Cyan", "White"};
static CliSettings s_cli_settings;

#define GPIO_PINS_COUNT 2
static const char* const gpio_pins_text[GPIO_PINS_COUNT] = {"13/14", "15/16"};
static GpioRemapSettings s_gpio_remap;

#define NOTIFY_TEXT_COUNT 3
static const char* const notify_text_text[NOTIFY_TEXT_COUNT] = {"All", "Critical", "None"};

#define BATTERY_VIEW_COUNT 6
static const char* const battery_view_text[BATTERY_VIEW_COUNT] =
    {"Bar", "%", "Inv. %", "Retro 3", "Retro 5", "Bar %"};
static const uint32_t battery_view_value[BATTERY_VIEW_COUNT] = {
    DISPLAY_BATTERY_BAR,
    DISPLAY_BATTERY_PERCENT,
    DISPLAY_BATTERY_INVERTED_PERCENT,
    DISPLAY_BATTERY_RETRO_3,
    DISPLAY_BATTERY_RETRO_5,
    DISPLAY_BATTERY_BAR_PERCENT};

static void desktop_settings_scene_start_var_list_enter_callback(void* context, uint32_t index) {
    DesktopSettingsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void desktop_settings_scene_start_battery_view_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, battery_view_text[index]);
    app->settings.displayBatteryPercentage = index;

}

static void desktop_settings_scene_start_clock_enable_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, clock_enable_text[index]);
    app->settings.display_clock = index;
}

static void desktop_settings_scene_start_wifi_icon_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, wifi_icon_text[index]);
    app->settings.wifi_icon_hidden = index;
}

static void desktop_settings_scene_start_midnight_format_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, midnight_format_text[index]);
    app->settings.clock_midnight_zero = index;
}

static void desktop_settings_scene_start_shell_color_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, shell_color_text[index]);
    s_cli_settings.shell_color_index = index;
    cli_settings_save(&s_cli_settings);
}

static void desktop_settings_scene_start_statusbar_icons_changed(VariableItem* item) {
    DesktopSettingsApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, statusbar_icons_text[index]);
    app->settings.statusbar_show_icons = index;
}

static void desktop_settings_scene_start_gpio_pins_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, gpio_pins_text[index]);
    s_gpio_remap.esp32_uart_channel = index;
    gpio_remap_settings_save(&s_gpio_remap);
}

static void desktop_settings_scene_start_notify_text_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, notify_text_text[index]);
    fox_settings_set_notification_text(index);
}

void desktop_settings_scene_start_on_enter(void* context) {
    DesktopSettingsApp* app = context;
    VariableItemList* list = app->variable_item_list;
    VariableItem* item;
    uint8_t value_index;

    {
        uint8_t ms_idx = fox_theme_get_style();
        if(ms_idx > 4) ms_idx = 0;
        app->settings.menu_theme = ms_idx;
    }

    variable_item_list_add(list, "Security & Privacy", 0, NULL, NULL);
    variable_item_list_add(list, "USB Mode", 0, NULL, NULL);
    variable_item_list_add(list, "Menu Style", 0, NULL, NULL);
    variable_item_list_add(list, "Custom Wallpaper", 0, NULL, NULL);
    variable_item_list_add(list, "Change Flipper Name", 0, NULL, app);
    variable_item_list_add(list, "Main Menu Apps", 0, NULL, NULL);
    variable_item_list_add(list, "Alarm Clock", 0, NULL, NULL);
    variable_item_list_add(list, "RGB Backlight", 0, NULL, NULL);
    variable_item_list_add(list, "VGM Options", 0, NULL, NULL);

    item = variable_item_list_add(
        list, "Notification Text", NOTIFY_TEXT_COUNT,
        desktop_settings_scene_start_notify_text_changed, app);
    {
        uint8_t ni = fox_settings_get_notification_text();
        if(ni >= NOTIFY_TEXT_COUNT) ni = 0;
        variable_item_set_current_value_index(item, ni);
        variable_item_set_current_value_text(item, notify_text_text[ni]);
    }

    item = variable_item_list_add(
        list, "Battery View", BATTERY_VIEW_COUNT,
        desktop_settings_scene_start_battery_view_changed, app);
    value_index = value_index_uint32(
        app->settings.displayBatteryPercentage, battery_view_value, BATTERY_VIEW_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, battery_view_text[value_index]);

    item = variable_item_list_add(
        list, "Show Clock", CLOCK_ENABLE_COUNT,
        desktop_settings_scene_start_clock_enable_changed, app);
    value_index =
        value_index_uint32(app->settings.display_clock, clock_enable_value, CLOCK_ENABLE_COUNT);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, clock_enable_text[value_index]);

    item = variable_item_list_add(
        list, "Midnight Format", MIDNIGHT_FORMAT_COUNT,
        desktop_settings_scene_start_midnight_format_changed, app);
    variable_item_set_current_value_index(item, app->settings.clock_midnight_zero);
    variable_item_set_current_value_text(
        item, midnight_format_text[app->settings.clock_midnight_zero]);

    item = variable_item_list_add(
        list, "WiFi/CC1101 Icon", WIFI_ICON_COUNT,
        desktop_settings_scene_start_wifi_icon_changed, app);
    variable_item_set_current_value_index(item, app->settings.wifi_icon_hidden);
    variable_item_set_current_value_text(item, wifi_icon_text[app->settings.wifi_icon_hidden]);

    item = variable_item_list_add(
        list, "Battery & SD Icons", STATUSBAR_ICONS_COUNT,
        desktop_settings_scene_start_statusbar_icons_changed, app);
    variable_item_set_current_value_index(item, app->settings.statusbar_show_icons);
    variable_item_set_current_value_text(
        item, statusbar_icons_text[app->settings.statusbar_show_icons]);

    cli_settings_load(&s_cli_settings);
    item = variable_item_list_add(
        list, "Shell Color", SHELL_COLOR_COUNT,
        desktop_settings_scene_start_shell_color_changed, app);
    variable_item_set_current_value_index(item, s_cli_settings.shell_color_index);
    variable_item_set_current_value_text(item, shell_color_text[s_cli_settings.shell_color_index]);

    gpio_remap_settings_load(&s_gpio_remap);
    item = variable_item_list_add(
        list, "ESP32 UART", GPIO_PINS_COUNT,
        desktop_settings_scene_start_gpio_pins_changed, app);
    if(s_gpio_remap.esp32_uart_channel >= GPIO_PINS_COUNT) s_gpio_remap.esp32_uart_channel = 0;
    variable_item_set_current_value_index(item, s_gpio_remap.esp32_uart_channel);
    variable_item_set_current_value_text(item, gpio_pins_text[s_gpio_remap.esp32_uart_channel]);

    variable_item_list_add(list, "Favorite - Left Short",  0, NULL, NULL);
    variable_item_list_add(list, "Favorite - Left Long",   0, NULL, NULL);
    variable_item_list_add(list, "Favorite - Right Short", 0, NULL, NULL);
    variable_item_list_add(list, "Favorite - Ok Long",     0, NULL, NULL);
    variable_item_list_add(list, "Favorite - Down Short",  0, NULL, NULL);

    variable_item_list_set_enter_callback(
        list, desktop_settings_scene_start_var_list_enter_callback, app);

    uint32_t saved_pos = scene_manager_get_scene_state(
        app->scene_manager, DesktopSettingsAppSceneStart);
    variable_item_list_set_selected_item(list, (uint8_t)saved_pos);

    view_dispatcher_switch_to_view(app->view_dispatcher, DesktopSettingsAppViewVarItemList);
}

bool desktop_settings_scene_start_on_event(void* context, SceneManagerEvent event) {
    DesktopSettingsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {

        scene_manager_set_scene_state(
            app->scene_manager, DesktopSettingsAppSceneStart, event.event);

        switch(event.event) {
        case DesktopSettingsPinSetup:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppScenePinMenu);
            break;
        case DesktopSettingsUsbMode:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneUsbMode);
            break;
        case DesktopSettingsMenuStyle:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneMenuStyle);
            break;
        case DesktopSettingsWallpaper:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneWallpaperSetup);
            break;
        case DesktopSettingsChangeName:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneChangeName);
            break;
        case DesktopSettingsMainMenu:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneMainMenu);
            break;
        case DesktopSettingsAlarmClock:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneAlarmClock);
            break;
        case DesktopSettingsRgbBacklight:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneRgbSettings);
            break;
        case DesktopSettingsVgmOptions:
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneVgmOptions);
            break;
        case DesktopSettingsFavoriteLeftShort:
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppLeftShort);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsFavoriteLeftLong:
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppLeftLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsFavoriteRightShort:
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppRightShort);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsFavoriteOkLong:
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppOkLong);
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        case DesktopSettingsFavoriteDownShort:
            scene_manager_set_scene_state(
                app->scene_manager, DesktopSettingsAppSceneFavorite,
                SCENE_STATE_SET_FAVORITE_APP | FavoriteAppRightLong); /* Down-short -> RightLong slot */
            scene_manager_next_scene(app->scene_manager, DesktopSettingsAppSceneFavorite);
            break;
        default:
            break;
        }
        consumed = true;
    }
    return consumed;
}

void desktop_settings_scene_start_on_exit(void* context) {
    DesktopSettingsApp* app = context;
    variable_item_list_reset(app->variable_item_list);
    desktop_settings_save(&app->settings);
    fox_theme_set_style(app->settings.menu_theme);
}
