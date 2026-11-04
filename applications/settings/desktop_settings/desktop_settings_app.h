#pragma once

#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/popup.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/text_input.h>

#include <desktop/desktop.h>
#include <desktop/helpers/pin_code.h>
#include <dialogs/dialogs.h>
#include <notification/notification_app.h>

#include "views/desktop_settings_view_pin_setup_howto.h"
#include "views/desktop_settings_view_pin_setup_howto2.h"
#include "views/desktop_settings_view_numeric_pin.h"
#include "views/desktop_settings_view_wallpaper.h"
#include "views/desktop_settings_view_alarm_edit.h"
#include "views/desktop_settings_view_menu_style.h"
#include "views/desktop_settings_view_usb_mode.h"

#include "scenes/desktop_settings_scene.h"

#define desktop_pin_lock_error_notify()   (void)0

#define desktop_set_pin(service, pin_code) desktop_api_set_pin(service, pin_code)

#define uxTopUsedPriority 0

typedef struct {
    Gui* gui;
    DialogsApp* dialogs;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    Popup* popup;
    Submenu* submenu;
    VariableItemList* variable_item_list;

    DesktopSettingsViewPinSetupHowto* pin_setup_howto_view;
    void* pin_setup_howto2_view;
    DesktopSettingsViewNumericPin* numeric_pin_view;
    DesktopSettingsViewWallpaper* wallpaper_view;
    DesktopSettingsViewAlarmEdit* alarm_edit_view;
    DesktopSettingsViewMenuStyle* menu_style_view;
    DesktopSettingsViewUsbMode* usb_mode_view;
    DialogEx* dialog_ex;
    TextInput* text_input;

    NotificationApp* notification;

    char device_name[64];
    bool save_name;
    /* Must match MM_LAYOUT_LABEL_LEN in helpers/main_menu_layout_ui.h. */
    char main_menu_rename_buffer[32];
    uint32_t pin_menu_idx;
    DesktopSettings settings;
    DesktopPinCode pincode_buffer;
} DesktopSettingsApp;
