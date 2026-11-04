#include "desktop_i.h"

#include <furi/core/memmgr.h>
#include <furi/core/memmgr_heap.h>
#include <cli/cli_vcp.h>
#include <bt/bt_service/bt.h>
#include <furi_hal_serial_control.h>
#include <furi_hal.h>
#include <furi_hal_usb_cdc.h> /* CdcCtrlLineDTR: defrag USB-connected gate */
#include <expansion/expansion.h>
#include <gui/gui_i.h>
#include <locale/locale.h>
#include <storage/storage.h>
#include <assets_icons.h>
#include <version.h>
#include <lib/subghz/devices/devices.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>

#include "scenes/desktop_scene.h"
#include "scenes/desktop_scene_locked.h"
#include "helpers/pin_code.h"
#include "furi_hal_power.h"
#include <power/power_service/power.h>
#include <gui/modules/fox_theme.h>
#include <namechanger/namechanger.h>
#include <flipper_format/flipper_format.h>

#define TAG "Desktop"

static FuriHalSerialHandle* s_locked_gpio_usart  = NULL;
static FuriHalSerialHandle* s_locked_gpio_lpuart = NULL;
static FuriHalUsbInterface* s_locked_usb_config  = NULL;

static bool s_locked_usb_disconnected = false;

static FuriHalUsbInterface* s_ram_watchdog_usb_config = NULL;

static bool s_ram_watchdog_usb_disconnected = false;

static uint8_t s_cli_vcp_session_lock_refcount = 0;

static void desktop_cli_vcp_session_lock_acquire(void) {
    if(s_cli_vcp_session_lock_refcount++ == 0) {
        CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
        cli_vcp_session_lock(cli_vcp);
        furi_record_close(RECORD_CLI_VCP);
    }
}

static void desktop_cli_vcp_session_lock_release(void) {
    furi_assert(s_cli_vcp_session_lock_refcount > 0);
    if(--s_cli_vcp_session_lock_refcount == 0) {
        CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
        cli_vcp_session_unlock(cli_vcp);
        furi_record_close(RECORD_CLI_VCP);
    }
}

#define WALLPAPER_DIR              EXT_PATH("wallpapers")
#define WALLPAPER_ACTIVATE_MARKER  EXT_PATH("wallpapers/.activate")
#define WALLPAPER_CURRENT_MARKER   EXT_PATH("wallpapers/.current")
#define DEFAULT_WALLPAPER_NAME     "Default.xbm"
#define WALLPAPER_SIZE 1024

#define ALARM_CHECK_POLL_MS 15000

#define RAM_WATCHDOG_POLL_MS 1000

#define RAM_WATCHDOG_TRIP_HEAP_PERCENT      1
#define RAM_WATCHDOG_RECOVER_HEAP_PERCENT   2

#define RAM_DEFRAG_WEDGE_GAP 20480u
#define RAM_DEFRAG_HEALTHY_BLOCK 102400u
/* AfterAppFinished can fire before the just-exited app's heap has finished freeing/
 * coalescing, so a reading taken right then can look fragmented when it isn't (it
 * recovers within <0.5s). When the first reading looks non-healthy, re-measure a few
 * times to let it settle before acting, so neither the low-RAM banner nor (critically)
 * the USB reseat fires on a transient false reading. */
#define RAM_DEFRAG_SETTLE_STEP_MS 100u
#define RAM_DEFRAG_SETTLE_TRIES   5u
/* USB-off gap in the reseat: teardown slack so the RPC/CLI session frees before USB
 * comes back. (COM stability on renamed units is handled at boot by namechanger
 * rebuilding the CDC serial with the custom name -- see namechanger.c -- not here.) */
#define RAM_DEFRAG_RESEAT_GAP_MS 300u

#define DESKTOP_STORAGE_WATCHDOG_TIMEOUT_MS 4000

#define DESKTOP_STORAGE_WATCHDOG_MAX_AUTO_RESETS 3

#define FOX_SETUP_FLAG_PATH      "/int/fox_setup.done"
#define FOX_SETUP_FLAG_EXT_PATH  "/ext/System/.fox_setup.done"
#define FOX_SETUP_AUTO_ARG   "auto"

#define FOX_UPDATE_2011_FLAG_PATH        "/ext/System/.fox_update_2011.done"
#define FOX_UPDATE_SUBGHZ_SETTINGS_PATH  EXT_PATH("subghz/assets/last_subghz.settings")
#define FOX_UPDATE_SUBGHZ_RSSI_KEY       "RSSI"
#define FOX_UPDATE_SUBGHZ_RSSI           (-65.0f)
#define FOX_UPDATE_WALLPAPER_BACKUP_MAX  100

#define FOX_ESP32_WIFI_STATUS_PATH EXT_PATH("apps_data/fox_esp32/wifi_status.txt")


#define FOX_ESP32_SEEN_PATH EXT_PATH("apps_data/fox_esp32/esp32_seen")

static void desktop_auto_lock_arm(Desktop*);
static void desktop_auto_lock_inhibit(Desktop*);
static void desktop_start_auto_lock_timer(Desktop*);
static void desktop_apply_settings(Desktop*);
static void desktop_load_wallpaper(Desktop*);
static void desktop_check_wallpaper_updates(Desktop*);
static void desktop_init_settings(Desktop*);

static void fox_no_sd_draw_callback(Canvas* canvas, void* context);

static void fox_lockout_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_icon(canvas, 2, 4, &I_fox_32x32);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 38, 14, "LOCKED!");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 38, 26, "Wrong PIN entered");
    canvas_draw_str(canvas, 38, 35, "too many times!");
    canvas_draw_str(canvas, 38, 45, "DFU > Repair >");
    canvas_draw_str(canvas, 38, 54, "Erase.");
}

static void fox_lockout_input_callback(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
}

static volatile bool s_sd_ejected_during_lockout = false;

static void fox_sd_eject_pubsub_callback(const void* message, void* context) {
    UNUSED(context);
    const StorageEvent* evt = message;
    if(evt->type == StorageEventTypeCardUnmount) {
        s_sd_ejected_during_lockout = true;
    } else if(evt->type == StorageEventTypeCardMount) {
        furi_hal_power_reset();
    }
}

static void fox_desktop_show_lockout_blocking(Desktop* desktop) {
    s_sd_ejected_during_lockout = false;
    FuriPubSubSubscription* sub = furi_pubsub_subscribe(
        storage_get_pubsub(desktop->storage), fox_sd_eject_pubsub_callback, NULL);

    ViewPort* lock_vp = view_port_alloc();
    view_port_draw_callback_set(lock_vp, fox_lockout_draw_callback, NULL);
    view_port_input_callback_set(lock_vp, fox_lockout_input_callback, NULL);
    gui_add_view_port(desktop->gui, lock_vp, GuiLayerFullscreen);

    if(furi_hal_usb_get_config() != NULL) {
        furi_hal_usb_set_config(NULL, NULL);
    }

    ViewPort* sd_overlay = NULL;

    while(true) {
        furi_delay_ms(1200);

        bool sd_gone = s_sd_ejected_during_lockout ||
                       (storage_sd_status(desktop->storage) != FSE_OK);

        if(sd_gone && sd_overlay == NULL) {
            sd_overlay = view_port_alloc();
            view_port_draw_callback_set(sd_overlay, fox_no_sd_draw_callback, NULL);
            view_port_input_callback_set(sd_overlay, fox_lockout_input_callback, NULL);
            gui_add_view_port(desktop->gui, sd_overlay, GuiLayerFullscreen);
        } else if(!sd_gone && sd_overlay != NULL) {
            furi_pubsub_unsubscribe(storage_get_pubsub(desktop->storage), sub);
            furi_hal_power_reset();
        }

        if(fox_recovery_check_and_reset()) {
            desktop_pin_code_reset();
            storage_common_remove(desktop->storage, FOX_LOCKOUT_FLAG_PATH);
            furi_pubsub_unsubscribe(storage_get_pubsub(desktop->storage), sub);
            furi_hal_power_reset();
        }
    }
}

static void fox_format_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_icon(canvas, 2, 0, &I_fox_32x32);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 38, 10, "SD Formatted!");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 38, 22, "PIN limit exceeded.");
    canvas_draw_str(canvas, 2,  38, "All data deleted.");
    canvas_draw_str(canvas, 2,  48, "DFU to recover.");
    canvas_draw_str(canvas, 2,  58, "See Help Files!");
}

static void fox_desktop_show_format_blocking(Desktop* desktop) {
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, fox_format_draw_callback, NULL);
    view_port_input_callback_set(vp, fox_lockout_input_callback, NULL);
    gui_add_view_port(desktop->gui, vp, GuiLayerFullscreen);

    if(furi_hal_usb_get_config() != NULL) {
        furi_hal_usb_set_config(NULL, NULL);
    }

    while(true) {
        furi_delay_ms(10000);
    }
}

static void fox_corrupt_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignTop, "Firmware Corrupt");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignTop, "Fox.data not found.");
    canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignTop, "Please re-install");
    canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignTop, "FoxFW firmware.");
}

static void fox_desktop_show_corrupt_blocking(Desktop* desktop) {
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, fox_corrupt_draw_callback, NULL);
    view_port_input_callback_set(vp, fox_lockout_input_callback, NULL);
    gui_add_view_port(desktop->gui, vp, GuiLayerFullscreen);
    if(furi_hal_usb_get_config() != NULL) {
        furi_hal_usb_set_config(NULL, NULL);
    }
    while(true) {
        furi_delay_ms(60000);
    }
}

static const uint8_t s_sample_wallpaper_xbm[WALLPAPER_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xc0, 0x0f, 0x00, 0x00, 0x80, 0xff, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xc0, 0x7f, 0x00, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x80, 0xff, 0x0f, 0x00, 0xe0, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xf9, 0xff, 0x00, 0xf0, 0xff, 0xdf, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xf2, 0xff, 0xff, 0x7f, 0xff, 0xbf, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x62, 0xff, 0xff, 0x6f, 0xfe, 0x7f, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xc4, 0xfc, 0xff, 0x37, 0xfe, 0x7f, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0xf3, 0x1f, 0x18, 0xfe, 0x7f, 0x76, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x20, 0xcf, 0x01, 0x9c, 0xff, 0xff, 0xae, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x60, 0xce, 0x00, 0x1e, 0xfe, 0xff, 0x36, 0x3f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x60, 0x0e, 0x00, 0x3f, 0xfd, 0x3f, 0x37, 0x7c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x06, 0x00, 0xfe, 0xff, 0x0e, 0x77, 0xf8, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0x00, 0xc0, 0x7f, 0x8c, 0x77, 0xe0, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x7f, 0xc8, 0x5f, 0xc0, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xf8, 0xc0, 0x47, 0x30, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x80, 0xc0, 0x63, 0xc0, 0x1f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0xf0, 0x07, 0x70, 0x00, 0xc1, 0x00, 0xfe, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x10, 0x04, 0x00, 0x3c, 0xc0, 0x01, 0x00, 0x01, 0xf9, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x58, 0xc4, 0x00, 0xe0, 0x00, 0x0f, 0x00, 0x60, 0xfe, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x70, 0x74, 0x00, 0x00, 0x07, 0x3c, 0x00, 0x80, 0xf1, 0x07, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x60, 0xf4, 0xff, 0x7f, 0xfc, 0xf7, 0x03, 0x1c, 0xc7, 0x05, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0xe6, 0x3f, 0x80, 0xff, 0xff, 0x03, 0xf0, 0x0f, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x20, 0xe6, 0x1f, 0xe0, 0x0f, 0xe0, 0x01, 0x8c, 0x1f, 0x06, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x7e, 0x80, 0x7f, 0x00, 0xc0, 0x03, 0xc0, 0x33, 0x0d, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0x3f, 0x00, 0xb0, 0x3f, 0x80, 0x7f, 0x0e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xc0, 0x00, 0xff, 0x01, 0x00, 0x00, 0xc0, 0xff, 0x07, 0x7b, 0x0e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x30, 0xc0, 0x07, 0x00, 0x00, 0xc0, 0x03, 0xff, 0x1f, 0x7a, 0x0e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08, 0xf8, 0x03, 0x0c, 0x00, 0x00, 0x3e, 0xfc, 0x7f, 0x7a, 0x0e, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0f, 0xff, 0xc3, 0x03, 0x80, 0x0f, 0xc0, 0xff, 0x7f, 0x79, 0x0c, 0x00, 0x00,
    0x00, 0x00, 0xc0, 0x3f, 0x00, 0x7e, 0xe0, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7d, 0x0c, 0x00, 0x00,
    0x00, 0x00, 0xc0, 0x3f, 0x00, 0x06, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x0d, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xf1, 0xff, 0xff, 0xff, 0x7f, 0xfe, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0xfc, 0xff, 0xff, 0xff, 0x9f, 0xff, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0xff, 0xff, 0xff, 0xff, 0xe7, 0xff, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xfe, 0xff, 0xfb, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xc1, 0x3f, 0xfe, 0xff, 0xff, 0x3f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0xf8, 0x07, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xff, 0x80, 0xff, 0xff, 0xef, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x1f, 0xe0, 0xc7, 0xff, 0xe7, 0x07, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x01, 0xfc, 0xe0, 0xff, 0xf1, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0xf0, 0x7f, 0x78, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0xfc, 0x0f, 0x1c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0xff, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void desktop_write_xbm_text(File* f, const uint8_t* raw, size_t len) {
    static const char header[] =
        "#define wallpaper_width 128\n"
        "#define wallpaper_height 64\n"
        "static unsigned char wallpaper_bits[] = {\n";
    storage_file_write(f, header, strlen(header));

    char line[8];
    for(size_t i = 0; i < len; i++) {
        bool last = (i + 1 == len);
        bool eol  = ((i + 1) % 12 == 0) || last;
        int n = snprintf(line, sizeof(line), last ? "0x%02x" : "0x%02x,", raw[i]);
        storage_file_write(f, line, n);
        storage_file_write(f, eol ? "\n" : " ", 1);
    }

    static const char footer[] = "};\n";
    storage_file_write(f, footer, strlen(footer));
}

static void desktop_write_current_wallpaper_marker(Desktop* desktop, const uint8_t* bits) {
    File* f = storage_file_alloc(desktop->storage);
    if(storage_file_open(f, WALLPAPER_CURRENT_MARKER, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        desktop_write_xbm_text(f, bits, WALLPAPER_SIZE);
        storage_file_close(f);
    }
    storage_file_free(f);
}

static void desktop_ensure_wallpaper(Desktop* desktop) {
    if(storage_sd_status(desktop->storage) != FSE_OK) return;

    storage_simply_mkdir(desktop->storage, WALLPAPER_DIR);

    char default_path[96];
    snprintf(default_path, sizeof(default_path), "%s/%s", WALLPAPER_DIR, DEFAULT_WALLPAPER_NAME);

    if(!storage_file_exists(desktop->storage, default_path)) {
        File* f = storage_file_alloc(desktop->storage);
        if(storage_file_open(f, default_path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            desktop_write_xbm_text(f, s_sample_wallpaper_xbm, WALLPAPER_SIZE);
            storage_file_close(f);
            FURI_LOG_I("Desktop", "Default wallpaper created at %s", default_path);
        }
        storage_file_free(f);
    }

}

static volatile bool s_sd_mounted_event = false;

static void fox_sd_pubsub_callback(const void* message, void* context) {
    UNUSED(context);
    const StorageEvent* evt = message;
    if(evt->type == StorageEventTypeCardMount) {
        s_sd_mounted_event = true;
    }
}

static void fox_sd_mount_eventflag_callback(const void* message, void* context) {
    const StorageEvent* evt = message;
    FuriEventFlag* ev = context;
    if(evt->type == StorageEventTypeCardMount) furi_event_flag_set(ev, 1);
}

static volatile bool s_desktop_boot_ready = true;

bool desktop_boot_is_ready(void) {
    return s_desktop_boot_ready;
}

static void fox_no_sd_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_icon(canvas, 2, 6, &I_fox_32x32);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 38, 16, "SD Card Missing!");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 38, 28, "FoxFW requires an");
    canvas_draw_str(canvas, 38, 37, "SD Card to function.");
    canvas_draw_str(canvas, 38, 52, "Insert SD Card");
    canvas_draw_str(canvas, 38, 61, "to continue...");
}

static void fox_desktop_show_no_sd_blocking(Desktop* desktop) {
    s_sd_mounted_event = false;
    FuriPubSubSubscription* sub = furi_pubsub_subscribe(
        storage_get_pubsub(desktop->storage), fox_sd_pubsub_callback, NULL);

    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, fox_no_sd_draw_callback, NULL);
    view_port_input_callback_set(vp, fox_lockout_input_callback, NULL);
    gui_add_view_port(desktop->gui, vp, GuiLayerFullscreen);

    while(true) {
        furi_delay_ms(200);
        if(s_sd_mounted_event || storage_sd_status(desktop->storage) == FSE_OK) {
            furi_pubsub_unsubscribe(storage_get_pubsub(desktop->storage), sub);
            gui_remove_view_port(desktop->gui, vp);
            view_port_free(vp);
            return;
        }
    }
}

static int32_t desktop_boot_init_settings_thread(void* context) {
    Desktop* desktop = context;
    desktop_init_settings(desktop);
    return 0;
}

#define STORAGE_WATCHDOG_TEXT_CENTER_X 81

static void fox_storage_watchdog_draw_recovering(Canvas* canvas, void* context) {
    uint32_t attempt = (uint32_t)(uintptr_t)context;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_icon(canvas, 2, 6, &I_fox_32x32);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, STORAGE_WATCHDOG_TEXT_CENTER_X, 6, AlignCenter, AlignTop, "SD Card Stuck");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, STORAGE_WATCHDOG_TEXT_CENTER_X, 18, AlignCenter, AlignTop, "Recovering...");

    char buf[24];
    snprintf(
        buf,
        sizeof(buf),
        "Attempt %lu of %d",
        (unsigned long)attempt,
        DESKTOP_STORAGE_WATCHDOG_MAX_AUTO_RESETS);
    canvas_draw_str_aligned(
        canvas, STORAGE_WATCHDOG_TEXT_CENTER_X, 29, AlignCenter, AlignTop, buf);
}

static void fox_storage_watchdog_draw_stuck(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_icon(canvas, 2, 6, &I_fox_32x32);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, STORAGE_WATCHDOG_TEXT_CENTER_X, 6, AlignCenter, AlignTop, "SD Card Problem");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, STORAGE_WATCHDOG_TEXT_CENTER_X, 18, AlignCenter, AlignTop, "Recovery gave up");
    canvas_draw_str_aligned(
        canvas, STORAGE_WATCHDOG_TEXT_CENTER_X, 29, AlignCenter, AlignTop, "Remove/reseat SD,");
    canvas_draw_str_aligned(
        canvas, STORAGE_WATCHDOG_TEXT_CENTER_X, 40, AlignCenter, AlignTop, "reformat it, then");
    canvas_draw_str_aligned(
        canvas, STORAGE_WATCHDOG_TEXT_CENTER_X, 51, AlignCenter, AlignTop, "resetting Flipper");
}

static void desktop_storage_watchdog_usb_disconnect_and_reset(void) {
    furi_hal_usb_unlock();
    furi_hal_usb_disable();
    furi_delay_ms(500);
    furi_hal_power_reset();
}

static void desktop_storage_watchdog_trigger(Desktop* desktop) {
    uint32_t trips = furi_hal_rtc_get_register(FuriHalRtcRegisterStorageWatchdogTrips) + 1;
    furi_hal_rtc_set_register(FuriHalRtcRegisterStorageWatchdogTrips, trips);

    FURI_LOG_E(
        TAG,
        "Boot-time storage watchdog tripped (attempt %lu/%d): wallpaper/settings load "
        "didn't return within %dms",
        (unsigned long)trips,
        DESKTOP_STORAGE_WATCHDOG_MAX_AUTO_RESETS,
        DESKTOP_STORAGE_WATCHDOG_TIMEOUT_MS);

    ViewPort* vp = view_port_alloc();
    view_port_input_callback_set(vp, fox_lockout_input_callback, NULL);

    if(trips <= DESKTOP_STORAGE_WATCHDOG_MAX_AUTO_RESETS) {
        view_port_draw_callback_set(
            vp, fox_storage_watchdog_draw_recovering, (void*)(uintptr_t)trips);
        gui_add_view_port(desktop->gui, vp, GuiLayerFullscreen);
        furi_delay_ms(1500);
        desktop_storage_watchdog_usb_disconnect_and_reset();
    } else {
        view_port_draw_callback_set(vp, fox_storage_watchdog_draw_stuck, NULL);
        gui_add_view_port(desktop->gui, vp, GuiLayerFullscreen);
        while(true) {
            furi_delay_ms(1000);
        }
    }
}

static void desktop_init_settings_with_watchdog(Desktop* desktop) {
    FuriThread* thread = furi_thread_alloc();
    furi_thread_set_name(thread, "DesktopBootInit");
    furi_thread_set_stack_size(thread, 2048);
    furi_thread_set_context(thread, desktop);
    furi_thread_set_callback(thread, desktop_boot_init_settings_thread);
    furi_thread_start(thread);

    uint32_t waited_ms = 0;
    while(furi_thread_get_state(thread) != FuriThreadStateStopped) {
        if(waited_ms >= DESKTOP_STORAGE_WATCHDOG_TIMEOUT_MS) {
            desktop_storage_watchdog_trigger(desktop);
        }
        furi_delay_ms(50);
        waited_ms += 50;
    }

    furi_thread_join(thread);
    furi_thread_free(thread);

    furi_hal_rtc_set_register(FuriHalRtcRegisterStorageWatchdogTrips, 0);
}

#define FOX_SETUP_FAP_PATH EXT_PATH("apps/Fox/fox_setup.fap")

/* Launch the fox_setup wizard (first boot only -- gated on the internal
 * /int/fox_setup.done flag). Runs from a one-shot timer, NOT a dedicated thread:
 * the old thread was fire-and-forget and never joined/freed, so its 1024-byte
 * stack leaked every boot, and on first boot it stranded high (created after
 * qFlipper's CLI/RPC had filled the low heap). The timer is a normal permanent
 * desktop resource allocated low in desktop_alloc -- no stack, no leak. */
static void desktop_fox_setup_launch_timer_cb(void* context) {
    Desktop* desktop = context;

    if(storage_file_exists(desktop->storage, FOX_SETUP_FLAG_PATH)) {
        return;
    }

    if(!storage_file_exists(desktop->storage, FOX_SETUP_FAP_PATH)) {
        FURI_LOG_E("FoxSetup", "fox_setup.fap not found at %s — wizard cannot launch",
                   FOX_SETUP_FAP_PATH);

        if(desktop->pending_slideshow) {
            view_dispatcher_send_custom_event(
                desktop->view_dispatcher, DesktopGlobalAfterAppFinished);
        }
        return;
    }

    FURI_LOG_I("FoxSetup", "Flag /int/fox_setup.done absent — launching %s", FOX_SETUP_FAP_PATH);

    FuriString* err = furi_string_alloc();
    LoaderStatus status = loader_start(desktop->loader, FOX_SETUP_FAP_PATH, FOX_SETUP_AUTO_ARG, err);
    if(status != LoaderStatusOk) {
        FURI_LOG_E("FoxSetup", "loader_start failed for fox_setup: %s (status=%d)",
                   furi_string_get_cstr(err), (int)status);
    }
    furi_string_free(err);
}

#define FOX_IMPORT_FAP_PATH  EXT_PATH("apps/System/fox_android_companion.fap")
#define FOX_IMPORT_FLAG_PATH EXT_PATH("apps_data/fox_companion/request.flag")
#define FOX_IMPORT_POLL_MS   2000

// Launch thread: runs loader_start off the timer thread so a busy loader can
// never stall the shared timer service.
static FuriThread* s_fox_import_launch_thread = NULL;

static int32_t desktop_fox_import_launch_thread_fn(void* context) {
    Desktop* desktop = context;
    FuriString* err = furi_string_alloc();
    loader_start(desktop->loader, FOX_IMPORT_FAP_PATH, "SETTINGS", err);
    furi_string_free(err);
    return 0;
}

// Periodic flag-watch: when the companion app (over BLE/USB) drops the request
// flag, launch the external Import Companion Settings app to apply it.
static void desktop_fox_import_timer_callback(void* context) {
    Desktop* desktop = context;
    if(s_fox_import_launch_thread &&
       furi_thread_get_state(s_fox_import_launch_thread) == FuriThreadStateStopped) {
        furi_thread_join(s_fox_import_launch_thread);
        furi_thread_free(s_fox_import_launch_thread);
        s_fox_import_launch_thread = NULL;
    }
    if(s_fox_import_launch_thread != NULL) return;
    if(desktop->app_running || desktop->locked || loader_is_locked(desktop->loader)) return;
    if(!storage_file_exists(desktop->storage, FOX_IMPORT_FLAG_PATH)) return;
    if(!storage_file_exists(desktop->storage, FOX_IMPORT_FAP_PATH)) return;

    s_fox_import_launch_thread = furi_thread_alloc();
    furi_thread_set_name(s_fox_import_launch_thread, "FoxImportLaunch");
    furi_thread_set_stack_size(s_fox_import_launch_thread, 1024);
    furi_thread_set_callback(s_fox_import_launch_thread, desktop_fox_import_launch_thread_fn);
    furi_thread_set_context(s_fox_import_launch_thread, desktop);
    furi_thread_start(s_fox_import_launch_thread);
}

static void desktop_loader_callback(const void* message, void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    const LoaderEvent* event = message;

    if(event->type == LoaderEventTypeApplicationBeforeLoad) {
        view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalBeforeAppStarted);
    } else if(event->type == LoaderEventTypeNoMoreAppsInQueue) {
        view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalAfterAppFinished);
    }
}

static void desktop_storage_callback(const void* message, void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    const StorageEvent* event = message;

    if(desktop->usb_msc_active) {
        return;
    }

    if(event->type == StorageEventTypeCardMount) {

        if(desktop->no_sd_viewport != NULL) {
            view_dispatcher_send_custom_event(
                desktop->view_dispatcher, DesktopGlobalSdCardMounted);
        }
        view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalReloadSettings);
    } else if(event->type == StorageEventTypeCardUnmount) {

        view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalSdCardRemoved);
    }
}

static void desktop_lock_icon_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    furi_assert(canvas);
    canvas_draw_icon(canvas, 0, 0, &I_Lock_7x8);
}

static void desktop_clock_update(Desktop* desktop) {
    furi_assert(desktop);

    DateTime curr_dt;
    furi_hal_rtc_get_datetime(&curr_dt);
    bool time_format_12 = locale_get_time_format() == LocaleTimeFormat12h;

    if(desktop->clock.hour != curr_dt.hour || desktop->clock.minute != curr_dt.minute ||
       desktop->clock.format_12 != time_format_12) {
        desktop->clock.format_12 = time_format_12;
        desktop->clock.hour = curr_dt.hour;
        desktop->clock.minute = curr_dt.minute;
        view_port_update(desktop->clock_viewport);
    }
}

static void desktop_clock_reconfigure(Desktop* desktop) {
    furi_assert(desktop);

    desktop_clock_update(desktop);

    if(desktop->settings.display_clock) {
        furi_timer_start(desktop->update_clock_timer, furi_ms_to_ticks(1000));
    } else {
        furi_timer_stop(desktop->update_clock_timer);
    }

    view_port_enabled_set(desktop->clock_viewport, desktop->settings.display_clock);
}

static void desktop_clock_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    furi_assert(canvas);

    Desktop* desktop = context;

    canvas_set_font(canvas, FontPrimary);

    uint8_t hour = desktop->clock.hour;
    if(desktop->clock.format_12) {
        if(hour > 12) hour -= 12;
        if(hour == 0 && !desktop->settings.clock_midnight_zero) hour = 12;
    }

    char buffer[20];
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        snprintf(buffer, sizeof(buffer), "D %02u:%02u", hour, desktop->clock.minute);
    } else {
        snprintf(buffer, sizeof(buffer), "%02u:%02u", hour, desktop->clock.minute);
    }

    canvas_draw_str_aligned(
        canvas, canvas_width(canvas) / 2, 8, AlignCenter, AlignBottom, buffer);
}

static void desktop_stealth_mode_icon_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    furi_assert(canvas);
    canvas_draw_icon(canvas, 0, 0, &I_Muted_8x8);
}

static void desktop_wifi_icon_draw_callback(Canvas* canvas, void* context) {
    Desktop* desktop = context;
    furi_assert(canvas);
    furi_assert(desktop);

    const Icon* icon;
    if(desktop->wifi_connected) {
        icon = &I_WiFi_Connected_9x8;
    } else if(desktop->cc1101_connected) {
        icon = &I_CC1101_Connected_9x8;
    } else {
        icon = &I_WiFi_Disconnected_9x8;
    }
    canvas_draw_icon(canvas, 2, 0, icon);
}

static void desktop_refresh_wifi_icon(Desktop* desktop) {
    furi_assert(desktop);

    bool wifi_connected = false;
    File* f = storage_file_alloc(desktop->storage);
    if(storage_file_open(f, FOX_ESP32_WIFI_STATUS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[1] = {0};
        if(storage_file_read(f, buf, 1) == 1) {
            wifi_connected = (buf[0] == '1');
        }
    }
    storage_file_close(f);
    storage_file_free(f);

    if(wifi_connected != desktop->wifi_connected) {
        desktop->wifi_connected = wifi_connected;
        view_port_update(desktop->wifi_icon_viewport);
    }

    gui_view_port_send_to_front(desktop->gui, desktop->wifi_icon_viewport);
}

#define FOX_ESP32_WIFI_RECHECK_MS        15000
#define FOX_ESP32_WIFI_DISCOVERY_MS      (1 * 60 * 1000)
#define FOX_ESP32_WIFI_PROBE_TIMEOUT_MS  500
#define FOX_ESP32_WIFI_PROBE_BAUD        115200

#define FOX_CC1101_BOOT_SETTLE_MS        5000

typedef struct {
    FuriStreamBuffer* stream;
} FoxWifiProbeCtx;

static void fox_wifi_probe_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    FoxWifiProbeCtx* ctx = context;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(ctx->stream, &byte, 1, 0);
    }
}

static int fox_wifi_probe_pins(FuriHalSerialId serial_id) {
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    FuriHalSerialHandle* handle = furi_hal_serial_control_acquire(serial_id);
    if(handle == NULL) {

        furi_record_close(RECORD_EXPANSION);
        return -1;
    }

    FuriHalBus bus = (serial_id == FuriHalSerialIdUsart) ? FuriHalBusUSART1 : FuriHalBusLPUART1;
    bool serial_owned = !furi_hal_bus_is_enabled(bus);
    if(serial_owned) {
        furi_hal_serial_init(handle, FOX_ESP32_WIFI_PROBE_BAUD);
    }
    furi_hal_serial_set_br(handle, FOX_ESP32_WIFI_PROBE_BAUD);

    FoxWifiProbeCtx ctx;
    ctx.stream = furi_stream_buffer_alloc(256, 1);
    furi_hal_serial_async_rx_start(handle, fox_wifi_probe_rx_callback, &ctx, false);

    static const char cmd[] = "[WIFI/STATUS]\r\n";
    furi_hal_serial_tx(handle, (const uint8_t*)cmd, strlen(cmd));

    char line[32];
    size_t line_len = 0;
    int result = -2;
    uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(FOX_ESP32_WIFI_PROBE_TIMEOUT_MS);
    while(furi_get_tick() < deadline) {
        uint8_t byte;
        uint32_t remaining = deadline - furi_get_tick();
        if(furi_stream_buffer_receive(ctx.stream, &byte, 1, remaining) == 0) break;
        if(byte == '\n') {
            if(line_len > 0 && line[line_len - 1] == '\r') line_len--;
            line[line_len] = '\0';

            const char* val = line;
            if(val[0] == '[') {
                const char* close = strchr(val, ']');
                if(close != NULL) val = close + 1;
            }
            if(strcmp(val, "true") == 0) {
                result = 1;
                break;
            }
            if(strcmp(val, "false") == 0) {
                result = 0;
                break;
            }

            line_len = 0;
        } else if(line_len < sizeof(line) - 1) {
            line[line_len++] = (char)byte;
        } else {
            line_len = 0;
        }
    }

    furi_hal_serial_async_rx_stop(handle);
    if(serial_owned) {
        furi_hal_serial_deinit(handle);
    }
    furi_hal_serial_control_release(handle);
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
    furi_stream_buffer_free(ctx.stream);

    return result;
}

static void fox_tz_refresh_send(FuriHalSerialId serial_id) {
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    FuriHalSerialHandle* handle = furi_hal_serial_control_acquire(serial_id);
    if(handle == NULL) {
        furi_record_close(RECORD_EXPANSION);
        return;
    }

    FuriHalBus bus = (serial_id == FuriHalSerialIdUsart) ? FuriHalBusUSART1 : FuriHalBusLPUART1;
    bool serial_owned = !furi_hal_bus_is_enabled(bus);
    if(serial_owned) {
        furi_hal_serial_init(handle, FOX_ESP32_WIFI_PROBE_BAUD);
    }
    furi_hal_serial_set_br(handle, FOX_ESP32_WIFI_PROBE_BAUD);

    static const char cmd[] = "[TZ/REFRESH]\r\n";
    furi_hal_serial_tx(handle, (const uint8_t*)cmd, strlen(cmd));
    furi_hal_serial_tx_wait_complete(handle);

    if(serial_owned) {
        furi_hal_serial_deinit(handle);
    }
    furi_hal_serial_control_release(handle);
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
}

static void fox_wifi_status_write_raw(Storage* storage, bool connected) {
    storage_simply_mkdir(storage, "/ext/apps_data");
    storage_simply_mkdir(storage, "/ext/apps_data/fox_esp32");

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, FOX_ESP32_WIFI_STATUS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const char* v = connected ? "1" : "0";
        storage_file_write(file, v, 1);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void fox_wifi_mark_esp32_seen(Storage* storage) {
    storage_simply_mkdir(storage, "/ext/apps_data");
    storage_simply_mkdir(storage, "/ext/apps_data/fox_esp32");

    File* file = storage_file_alloc(storage);
    storage_file_open(file, FOX_ESP32_SEEN_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    storage_file_close(file);
    storage_file_free(file);
}

bool subghz_devices_try_init_internal_only(void);

static void desktop_cc1101_ext_check(Desktop* desktop) {

    if(desktop->app_running || desktop->locked || loader_is_locked(desktop->loader)) {
        return;
    }

    bool connected = false;

    if(!subghz_devices_try_init_internal_only()) {
        return;
    }
    if(subghz_devices_load_external()) {
        const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        if(device) {
            connected = subghz_devices_is_connect(device);
        }
    }
    subghz_devices_deinit();

    if(connected != desktop->cc1101_connected) {
        desktop->cc1101_connected = connected;
        view_port_update(desktop->wifi_icon_viewport);
    }
}

static int32_t desktop_wifi_recheck_thread(void* context) {
    Desktop* desktop = context;

    bool esp32_seen = storage_file_exists(desktop->storage, FOX_ESP32_SEEN_PATH);

    bool first_pass = true;

    int last_tz_refresh_slot = -1;
    uint32_t thread_start_tick = furi_get_tick();
    while(true) {
        if(!first_pass) {
            furi_delay_ms(esp32_seen ? FOX_ESP32_WIFI_RECHECK_MS : FOX_ESP32_WIFI_DISCOVERY_MS);
        }
        first_pass = false;

        if(desktop->app_running || desktop->locked || loader_is_locked(desktop->loader)) {
            continue;
        }

        FuriHalSerialId working_id = FuriHalSerialIdUsart;
        int result_usart = fox_wifi_probe_pins(FuriHalSerialIdUsart);
        int result = result_usart;
        if(result_usart == -2) {

            result = fox_wifi_probe_pins(FuriHalSerialIdLpuart);
            working_id = FuriHalSerialIdLpuart;
        }

        if(result != -1) {
            bool connected = (result == 1);
            fox_wifi_status_write_raw(desktop->storage, connected);
            if(connected != desktop->wifi_connected) {
                desktop->wifi_connected = connected;
                view_port_update(desktop->wifi_icon_viewport);
            }

            if(!esp32_seen && (result == 0 || result == 1)) {
                esp32_seen = true;
                fox_wifi_mark_esp32_seen(desktop->storage);
            }

            if(connected) {
                DateTime now;
                furi_hal_rtc_get_datetime(&now);
                if((now.minute == 0 || now.minute == 30) && now.second < 15) {
                    int slot = now.hour * 2 + (now.minute >= 30 ? 1 : 0);
                    if(slot != last_tz_refresh_slot) {
                        last_tz_refresh_slot = slot;
                        fox_tz_refresh_send(working_id);
                    }
                }
            }
        }

        if(furi_get_tick() - thread_start_tick >= furi_ms_to_ticks(FOX_CC1101_BOOT_SETTLE_MS)) {

            furi_delay_ms(300);

            if(desktop->app_running || desktop->locked || loader_is_locked(desktop->loader)) {
                continue;
            }

            desktop_cc1101_ext_check(desktop);
        }
    }

    return 0;
}

static void desktop_wallpaper_draw_callback(Canvas* canvas, void* model) {
    if(!model) return;
    Desktop* desktop = *(Desktop**)model;

    furi_mutex_acquire(desktop->wallpaper_mutex, FuriWaitForever);
    if(desktop->wallpaper_data) {
        canvas_clear(canvas);
        canvas_draw_xbm(canvas, 0, 0, 128, 64, desktop->wallpaper_data);
    }
    furi_mutex_release(desktop->wallpaper_mutex);
}

#define RAM_DEFRAG_BANNER_ROWS 12 /* full status-bar strip (rows 1..12) */
#define RAM_DEFRAG_SLIDE_MS     15

#define RAM_FREE_BANNER_MAX 92160u /* 90 KB: only warn "Free RAM" below this */

/* Desktop notification priority, filtered by the Fox "Notification Text" setting
 * (All / Critical / None): Critical banners show unless None; Info banners show only
 * on All. Give any future banner a priority and the filtering is automatic. */
typedef enum {
    FoxBannerPrioCritical, /* warnings, failures, in-progress */
    FoxBannerPrioInfo,     /* success / FYI */
} FoxBannerPrio;

/* Last "RAM FRAGMENTED x%" and "Free RAM Block xxKB" values shown. RAM-only (.data,
 * reset on every boot -- never persisted) so each re-shows once after a fresh boot
 * and then only when its value changes. */
static uint8_t s_ram_frag_last_pct = 0xFF;
static uint8_t s_ram_free_last_kb  = 0xFF;

/* Should a banner of this priority show, per the Fox "Notification Text" setting?
 * Read lazily -- only when a banner is about to show (rare + deduped) -- so there is
 * no per-app-exit settings-file read. */
static bool desktop_banner_allowed(FoxBannerPrio prio) {
    uint8_t mode = fox_settings_get_notification_text();
    if(mode == FOX_NOTIFY_TEXT_NONE) return false;
    if(mode == FOX_NOTIFY_TEXT_CRITICAL && prio == FoxBannerPrioInfo) return false;
    return true;
}

/* Async, cancellable desktop banner (Fox). On request we wait a non-blocking
 * pre-delay, then drop at full height, hold, slide up, clear. Driven by
 * desktop->banner_timer (one-shot, restarted per phase); the timer callback only
 * posts DesktopGlobalBannerTick so every GUI call + state change runs on the
 * desktop thread. Opening an app cancels it (desktop_banner_cancel): during the
 * pre-delay it never shows; while visible it is cleared. Nothing is persisted --
 * each banner is recomputed fresh on the next app exit, so a condition that has
 * resolved by then simply produces no banner. Gated by the Fox "Notification
 * Text" setting. No heap alloc (text -> fixed buffer). */
#define FOX_BANNER_PREDELAY_MS 750u  /* non-blocking wait before a banner appears */
#define FOX_BANNER_HOLD_MS     2000u /* full-height dwell (visible time) */

typedef enum {
    FoxBannerPhaseIdle = 0,
    FoxBannerPhasePredelay,
    FoxBannerPhaseHold,
    FoxBannerPhaseSlide,
} FoxBannerPhase;

static FoxBannerPhase s_banner_phase = FoxBannerPhaseIdle;
static uint8_t s_banner_slide_row = 0;
static GuiDesktopNotifyIcon s_banner_icon = GuiDesktopNotifyIconInfo;
static char s_banner_text[28];

static void desktop_banner_timer_callback(void* context) {
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalBannerTick);
}

/* Cancel any in-flight banner. If already visible, clear it. Desktop-thread only. */
static void desktop_banner_cancel(Desktop* desktop) {
    if(s_banner_phase == FoxBannerPhaseIdle) return;
    furi_timer_stop(desktop->banner_timer);
    if(s_banner_phase == FoxBannerPhaseHold || s_banner_phase == FoxBannerPhaseSlide) {
        gui_desktop_notification_clear(desktop->gui);
    }
    s_banner_phase = FoxBannerPhaseIdle;
}

/* Queue a banner (non-blocking). Replaces any in-flight one. Desktop-thread only. */
static void desktop_banner_request(
    Desktop* desktop, const char* text, GuiDesktopNotifyIcon icon, FoxBannerPrio prio) {
    if(!desktop_banner_allowed(prio)) return;
    desktop_banner_cancel(desktop);
    snprintf(s_banner_text, sizeof(s_banner_text), "%s", text);
    s_banner_icon = icon;
    s_banner_phase = FoxBannerPhasePredelay;
    furi_timer_start(desktop->banner_timer, furi_ms_to_ticks(FOX_BANNER_PREDELAY_MS));
}

/* Advance the banner state machine one step. Desktop-thread only (event loop). */
static void desktop_banner_tick(Desktop* desktop) {
    switch(s_banner_phase) {
    case FoxBannerPhasePredelay:
        gui_desktop_notification_set(
            desktop->gui, s_banner_text, (int8_t)RAM_DEFRAG_BANNER_ROWS, s_banner_icon);
        s_banner_phase = FoxBannerPhaseHold;
        furi_timer_start(desktop->banner_timer, furi_ms_to_ticks(FOX_BANNER_HOLD_MS));
        break;
    case FoxBannerPhaseHold:
        s_banner_slide_row = RAM_DEFRAG_BANNER_ROWS;
        s_banner_phase = FoxBannerPhaseSlide;
        furi_timer_start(desktop->banner_timer, furi_ms_to_ticks(RAM_DEFRAG_SLIDE_MS));
        break;
    case FoxBannerPhaseSlide:
        if(s_banner_slide_row >= 1) {
            gui_desktop_notification_set(
                desktop->gui, s_banner_text, (int8_t)s_banner_slide_row, s_banner_icon);
            s_banner_slide_row--;
            furi_timer_start(desktop->banner_timer, furi_ms_to_ticks(RAM_DEFRAG_SLIDE_MS));
        } else {
            gui_desktop_notification_clear(desktop->gui);
            s_banner_phase = FoxBannerPhaseIdle;
        }
        break;
    default:
        break; /* stale tick after cancel -- ignore */
    }
}


void desktop_ram_defrag_if_wedged(Desktop* desktop) {
    if(!desktop_boot_is_ready()) return;
    if(desktop->ram_watchdog_tripped) return;
    if(desktop->pending_slideshow || desktop->alarm_ringing || desktop->locked) return;

    size_t max_block = memmgr_heap_get_max_free_block();

    /* Let the heap settle: the exited app's memory may not be fully freed/coalesced yet
     * at this instant. Re-measure until healthy or the short budget elapses. Only waits
     * on a non-healthy-looking exit (breaks out early as soon as it recovers), never on a
     * clean one -- so a false "wedged" reading no longer triggers the USB reseat. */
    if(max_block < RAM_DEFRAG_HEALTHY_BLOCK) {
        for(uint8_t i = 0; i < RAM_DEFRAG_SETTLE_TRIES; i++) {
            furi_delay_ms(RAM_DEFRAG_SETTLE_STEP_MS);
            max_block = memmgr_heap_get_max_free_block();
            if(max_block >= RAM_DEFRAG_HEALTHY_BLOCK) break;
        }
    }

    if(max_block >= RAM_DEFRAG_HEALTHY_BLOCK) {
        s_ram_free_last_kb = 0xFF; /* healthy: reset so low-RAM banner re-shows on next drop */
        return;
    }
    size_t free_heap = memmgr_get_free_heap();

    if((free_heap - max_block) < RAM_DEFRAG_WEDGE_GAP) {
        /* Under HEALTHY but NOT fragmented (max ~= free) -- just low total free. Only
         * warn when GENUINELY low (< RAM_FREE_BANNER_MAX, 90 KB): with the trace stripped
         * a connected host sits ~104 KB, so this is a true low-RAM heads-up, not a
         * qFlipper indicator. Deduped by rounded KB (reset when healthy); RAM-only. */
        if(max_block < RAM_FREE_BANNER_MAX) {
            uint8_t kb = (uint8_t)((max_block + 512U) / 1024U);
            if(kb != s_ram_free_last_kb) {
                s_ram_free_last_kb = kb;
                char buf[28];
                snprintf(buf, sizeof(buf), "Free RAM: %uk", (unsigned)kb);
                desktop_banner_request(desktop, buf, GuiDesktopNotifyIconInfo, FoxBannerPrioInfo);
            }
        }
        return;
    }

    if(furi_hal_cdc_get_ctrl_line_state(0 /*VCP_IF_NUM*/) & CdcCtrlLineDTR) {
        /* Host (qFlipper) on the VCP and the heap is fragmented: reseat USB so the
         * allocator can coalesce. Tear the RPC/CLI session down (frees its ~15-20 KB),
         * wait the gap, then restore. The footprint returns on reconnect, but freeing +
         * re-allocating contiguously raises the largest free block (the defrag). Same
         * furi_hal_usb sequence as the ram-watchdog / lock-screen reseats; blocks the
         * desktop thread for the gap (USB stays down the whole time, so no RAM churn).
         * COM stability on renamed units is handled at boot by namechanger, not here. */
        desktop_banner_request(
            desktop, "Defragmenting RAM", GuiDesktopNotifyIconInfo, FoxBannerPrioInfo);
        desktop_cli_vcp_session_lock_acquire();
        FuriHalUsbInterface* saved_usb = furi_hal_usb_get_config();
        furi_hal_usb_unlock();
        furi_hal_usb_set_config(NULL, NULL);
        furi_delay_ms(RAM_DEFRAG_RESEAT_GAP_MS);
        furi_hal_usb_set_config(saved_usb, NULL);
        desktop_cli_vcp_session_lock_release();
        return;
    } else {
        /* Wedged, but no host on the VCP -- nothing for the reseat to free. Report it
         * instead of pretending to defrag with a plain "Fragmentation" banner (no numbers).
         * pct (= 100 - largest free block as a %% of total free; higher = worse) is kept only
         * as the de-dupe key so the desktop shows the banner once per fragmentation level and
         * doesn't nag on every app exit. No heap alloc, and this runs on the bare desktop with
         * USB off, so it can't re-fragment the heap. */
        uint8_t pct = (uint8_t)(100U - ((max_block * 100U) / free_heap));
        if(pct != s_ram_frag_last_pct) {
            s_ram_frag_last_pct = pct;
            desktop_banner_request(
                desktop, "Fragmentation", GuiDesktopNotifyIconWarning, FoxBannerPrioCritical);
        }
    }
}

static bool desktop_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    Desktop* desktop = (Desktop*)context;

    if(event == DesktopGlobalBeforeAppStarted) {
        desktop_auto_lock_inhibit(desktop);
        desktop->app_running = true;
        desktop_banner_cancel(desktop);

    } else if(event == DesktopGlobalBannerTick) {
        desktop_banner_tick(desktop);

    } else if(event == DesktopGlobalAfterAppFinished) {

        desktop_auto_lock_arm(desktop);
        desktop->app_running = false;
        desktop_check_wallpaper_updates(desktop);
        desktop_refresh_wifi_icon(desktop);

        desktop_ram_defrag_if_wedged(desktop);

        if(desktop->pending_slideshow) {
            desktop->pending_slideshow = false;
            if(storage_file_exists(desktop->storage, SLIDESHOW_FS_PATH)) {
                scene_manager_next_scene(desktop->scene_manager, DesktopSceneSlideshow);
            }
        }

        if(desktop->alarm_ringing) {
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneClockLock);
        }

    } else if(event == DesktopGlobalAutoLock) {
        if(!desktop->app_running && !desktop->locked) {
            if((desktop->settings.usb_inhibit_auto_lock) && (furi_hal_usb_is_locked())) {
                return (0);
            }
            desktop_lock(desktop);
        }
    } else if(event == DesktopGlobalSaveSettings) {
        desktop_settings_save(&desktop->settings);
        desktop_apply_settings(desktop);

    } else if(event == DesktopGlobalReloadSettings) {
        desktop_settings_load(&desktop->settings);
        desktop_apply_settings(desktop);
    } else if(event == DesktopGlobalSdCardRemoved) {
        if(desktop->no_sd_viewport == NULL) {
            desktop->no_sd_viewport = view_port_alloc();
            view_port_draw_callback_set(desktop->no_sd_viewport, fox_no_sd_draw_callback, NULL);
            view_port_input_callback_set(desktop->no_sd_viewport, fox_lockout_input_callback, NULL);
            gui_add_view_port(desktop->gui, desktop->no_sd_viewport, GuiLayerFullscreen);
        }
    } else if(event == DesktopGlobalSdCardMounted) {
        fox_settings_sync_int_to_sd();
        fox_settings_sync_sd_to_int();
        if(desktop->no_sd_viewport != NULL) {
            furi_delay_ms(400);
            furi_hal_power_reset();
        }

    } else {
        return scene_manager_handle_custom_event(desktop->scene_manager, event);
    }

    return true;
}

static bool desktop_back_event_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = (Desktop*)context;
    return scene_manager_handle_back_event(desktop->scene_manager);
}

static void desktop_tick_event_callback(void* context) {
    furi_assert(context);
    Desktop* app = context;
    scene_manager_handle_tick_event(app->scene_manager);

    static uint8_t wifi_icon_position_reasserts = 6;
    if(wifi_icon_position_reasserts > 0) {
        wifi_icon_position_reasserts--;
        gui_view_port_send_to_front(app->gui, app->wifi_icon_viewport);
    }

    static uint32_t s_last_integrity_ms = 0;
    uint32_t now = furi_get_tick();
    if(now - s_last_integrity_ms < furi_ms_to_ticks(2000)) return;
    s_last_integrity_ms = now;

    {
        Storage* s = app->storage;
        bool sd_present = (storage_sd_status(s) == FSE_OK);

        bool int_ok = storage_file_exists(s, FOX_SETTINGS_INT_PATH);
        bool ext_ok = sd_present && storage_file_exists(s, FOX_SETTINGS_EXT_PATH);

        if(!int_ok && !ext_ok) {
            if(storage_file_exists(s, FOX_SETUP_FLAG_PATH)) {
                furi_hal_power_reset();
            }
        } else if(!int_ok && ext_ok) {
            fox_settings_sync_sd_to_int();
        } else if(int_ok && sd_present && !ext_ok) {
            fox_settings_sync_int_to_sd();
        }

        if(sd_present) {
            bool flag_int = storage_file_exists(s, FOX_SETUP_FLAG_PATH);
            bool flag_ext = storage_file_exists(s, FOX_SETUP_FLAG_EXT_PATH);
            if(flag_int && !flag_ext) {
                storage_common_copy(s, FOX_SETUP_FLAG_PATH, FOX_SETUP_FLAG_EXT_PATH);
            } else if(!flag_int && flag_ext) {
                storage_common_copy(s, FOX_SETUP_FLAG_EXT_PATH, FOX_SETUP_FLAG_PATH);
            }

            const char* pin_pending = EXT_PATH("apps_data/fox_setup/fox_pend.tmp");
            if(storage_file_exists(s, pin_pending)) {
                File* pf = storage_file_alloc(s);
                if(storage_file_open(pf, pin_pending, FSAM_READ, FSOM_OPEN_EXISTING)) {
                    DesktopPinCode pin = {0};
                    storage_file_read(pf, &pin.length, sizeof(uint8_t));
                    if(pin.length > 0 && pin.length <= (uint8_t)(sizeof(pin.data) - 1)) {
                        storage_file_read(pf, pin.data, pin.length);
                        pin.data[pin.length] = '\0';
                    } else {
                        pin.length = 0;
                    }
                    storage_file_close(pf);
                    if(pin.length > 0) desktop_pin_code_set(&pin);
                }
                storage_file_free(pf);
                storage_common_remove(s, pin_pending);
            }
        }
    }
}

static void desktop_input_event_callback(const void* value, void* context) {
    furi_assert(value);
    furi_assert(context);
    const InputEvent* event = value;
    Desktop* desktop = context;
    if(event->type == InputTypePress) {
        desktop_start_auto_lock_timer(desktop);
    }
}

static void desktop_auto_lock_timer_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopGlobalAutoLock);
}

static void desktop_start_auto_lock_timer(Desktop* desktop) {
    furi_timer_start(
        desktop->auto_lock_timer, furi_ms_to_ticks(desktop->settings.auto_lock_delay_ms));
}

static void desktop_stop_auto_lock_timer(Desktop* desktop) {
    furi_timer_stop(desktop->auto_lock_timer);
}

static void desktop_auto_lock_arm(Desktop* desktop) {
    if(desktop->settings.auto_lock_delay_ms) {
        if(!desktop->input_events_subscription) {
            desktop->input_events_subscription = furi_pubsub_subscribe(
                desktop->input_events_pubsub, desktop_input_event_callback, desktop);
        }
        desktop_start_auto_lock_timer(desktop);
    }
}

static void desktop_auto_lock_inhibit(Desktop* desktop) {
    desktop_stop_auto_lock_timer(desktop);
    if(desktop->input_events_subscription) {
        furi_pubsub_unsubscribe(desktop->input_events_pubsub, desktop->input_events_subscription);
        desktop->input_events_subscription = NULL;
    }
}

static void desktop_clock_timer_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    if(desktop->app_running) return;
    desktop_clock_update(desktop);
}

static uint8_t desktop_hex2byte(char hi, char lo) {
    uint8_t h = (hi >= 'a') ? (uint8_t)(hi - 'a' + 10) :
                (hi >= 'A') ? (uint8_t)(hi - 'A' + 10) : (uint8_t)(hi - '0');
    uint8_t l = (lo >= 'a') ? (uint8_t)(lo - 'a' + 10) :
                (lo >= 'A') ? (uint8_t)(lo - 'A' + 10) : (uint8_t)(lo - '0');
    return (h << 4) | l;
}

static bool desktop_parse_xbm_file(Storage* storage, const char* path, uint8_t* out) {
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return false;
    }

    size_t count = 0;

    uint8_t state = 0;
    char    hi_digit = 0;
    bool    finished = false;

    uint8_t  chunk[256];
    uint16_t n;

    while(!finished && count < WALLPAPER_SIZE &&
          (n = storage_file_read(file, chunk, sizeof(chunk))) > 0) {
        for(uint16_t i = 0; i < n && count < WALLPAPER_SIZE && !finished; i++) {
            char c = (char)chunk[i];
            switch(state) {
            case 0:
                if(c == '{') state = 1;
                break;
            case 1:
                if(c == '}') { finished = true; break; }
                if(c == '0') state = 2;
                break;
            case 2:
                state = (c == 'x' || c == 'X') ? 3 : 1;
                break;
            case 3:
                hi_digit = c;
                state = 4;
                break;
            case 4:
                out[count++] = desktop_hex2byte(hi_digit, c);
                state = 1;
                break;
            }
        }
    }

    storage_file_close(file);
    storage_file_free(file);

    return count == WALLPAPER_SIZE;
}

static bool desktop_wallpaper_header_is_128x64(Storage* storage, const char* path) {
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char header[220];
        uint16_t n = storage_file_read(file, header, sizeof(header) - 1);
        header[n] = '\0';
        storage_file_close(file);

        char* w = strstr(header, "_width ");
        char* h = strstr(header, "_height ");
        if(w && h) {
            ok = (atoi(w + 7) == 128) && (atoi(h + 8) == 64);
        }
    }
    storage_file_free(file);
    return ok;
}

static void desktop_apply_one_time_update(Desktop* desktop) {
    if(storage_sd_status(desktop->storage) != FSE_OK) return;
    if(storage_file_exists(desktop->storage, FOX_UPDATE_2011_FLAG_PATH)) return;

    char default_path[96];
    snprintf(default_path, sizeof(default_path), "%s/%s", WALLPAPER_DIR, DEFAULT_WALLPAPER_NAME);

    if(storage_file_exists(desktop->storage, default_path)) {
        uint8_t* bits = malloc(WALLPAPER_SIZE);
        bool is_stock = desktop_parse_xbm_file(desktop->storage, default_path, bits) &&
                        memcmp(bits, s_sample_wallpaper_xbm, WALLPAPER_SIZE) == 0;
        free(bits);

        if(!is_stock) {
            char backup_path[96];
            for(uint8_t n = 2; n < FOX_UPDATE_WALLPAPER_BACKUP_MAX; n++) {
                snprintf(
                    backup_path, sizeof(backup_path), "%s/Default%u.xbm", WALLPAPER_DIR, (unsigned)n);
                if(!storage_file_exists(desktop->storage, backup_path)) {
                    FS_Error error =
                        storage_common_rename(desktop->storage, default_path, backup_path);
                    FURI_LOG_I(
                        TAG,
                        "One-time update: old default wallpaper kept as %s (%d)",
                        backup_path,
                        (int)error);
                    break;
                }
            }
        }
    }

    FlipperFormat* settings = flipper_format_file_alloc(desktop->storage);
    if(flipper_format_file_open_existing(settings, FOX_UPDATE_SUBGHZ_SETTINGS_PATH)) {
        float rssi = FOX_UPDATE_SUBGHZ_RSSI;
        bool updated =
            flipper_format_update_float(settings, FOX_UPDATE_SUBGHZ_RSSI_KEY, &rssi, 1);
        FURI_LOG_I(TAG, "One-time update: Sub-GHz RSSI threshold reset (%d)", (int)updated);
    }
    flipper_format_free(settings);

    storage_simply_mkdir(desktop->storage, "/ext/System");
    File* flag = storage_file_alloc(desktop->storage);
    if(storage_file_open(flag, FOX_UPDATE_2011_FLAG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_close(flag);
    }
    storage_file_free(flag);
}

#define WALLPAPER_CYCLE_NAME_MAX 64
#define WALLPAPER_CYCLE_LIST_MAX 64

static uint8_t desktop_list_wallpapers(
    Desktop* desktop,
    char (*names)[WALLPAPER_CYCLE_NAME_MAX],
    uint8_t max_count) {
    uint8_t count = 0;
    File* dir = storage_file_alloc(desktop->storage);
    if(storage_dir_open(dir, WALLPAPER_DIR)) {
        FileInfo info;
        char name[128];
        while(count < max_count && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            if(name[0] == '.') continue;

            size_t len = strlen(name);
            if(len < 4 || strcasecmp(name + len - 4, ".xbm") != 0) continue;

            char full[160];
            snprintf(full, sizeof(full), "%s/%s", WALLPAPER_DIR, name);
            if(!desktop_wallpaper_header_is_128x64(desktop->storage, full)) continue;

            strlcpy(names[count], name, WALLPAPER_CYCLE_NAME_MAX);
            count++;
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);

    for(uint8_t i = 0; (uint8_t)(i + 1) < count; i++) {
        uint8_t smallest = i;
        for(uint8_t j = (uint8_t)(i + 1); j < count; j++) {
            if(strcasecmp(names[j], names[smallest]) < 0) smallest = j;
        }
        if(smallest != i) {
            char tmp[WALLPAPER_CYCLE_NAME_MAX];
            memcpy(tmp, names[i], WALLPAPER_CYCLE_NAME_MAX);
            memcpy(names[i], names[smallest], WALLPAPER_CYCLE_NAME_MAX);
            memcpy(names[smallest], tmp, WALLPAPER_CYCLE_NAME_MAX);
        }
    }
    return count;
}

void desktop_cycle_wallpaper(Desktop* desktop) {
    furi_assert(desktop);

    char(*names)[WALLPAPER_CYCLE_NAME_MAX] =
        malloc(WALLPAPER_CYCLE_LIST_MAX * WALLPAPER_CYCLE_NAME_MAX);
    uint8_t count = desktop_list_wallpapers(desktop, names, WALLPAPER_CYCLE_LIST_MAX);

    if(count == 0) {
        free(names);
        return;
    }

    if(!desktop->settings.wallpaper_enabled) {
        bool found = false;
        for(uint8_t i = 0; i < count; i++) {
            if(strcmp(names[i], desktop->settings.wallpaper_filename) == 0) {
                found = true;
                break;
            }
        }
        if(!found) {
            strlcpy(
                desktop->settings.wallpaper_filename,
                names[0],
                sizeof(desktop->settings.wallpaper_filename));
        }
        desktop->settings.wallpaper_enabled = 1;
    } else if(count == 1) {
        desktop->settings.wallpaper_enabled = 0;
    } else {
        uint8_t current = 0;
        for(uint8_t i = 0; i < count; i++) {
            if(strcmp(names[i], desktop->settings.wallpaper_filename) == 0) {
                current = i;
                break;
            }
        }
        uint8_t next = (uint8_t)((current + 1) % count);
        strlcpy(
            desktop->settings.wallpaper_filename,
            names[next],
            sizeof(desktop->settings.wallpaper_filename));
    }

    free(names);
    desktop_settings_save(&desktop->settings);
    desktop_load_wallpaper(desktop);

    // Repaint the wallpaper view right away so the new image shows instantly — without this the
    // change only appeared after the desktop was left and reloaded. The draw callback reads
    // desktop->wallpaper_data (already swapped above); committing the model with update=true asks
    // the GUI to redraw the view now.
    with_view_model(
        desktop->wallpaper_view, Desktop** model, { *model = desktop; }, true);
}

static void desktop_load_wallpaper(Desktop* desktop) {
    furi_assert(desktop);

    const char* filename = (desktop->settings.wallpaper_enabled &&
                             desktop->settings.wallpaper_filename[0] != '\0') ?
                                desktop->settings.wallpaper_filename :
                                DEFAULT_WALLPAPER_NAME;

    char path[96];
    snprintf(path, sizeof(path), "%s/%s", WALLPAPER_DIR, filename);

    uint8_t* out = desktop->wallpaper_scratch;
    bool ok = desktop_parse_xbm_file(desktop->storage, path, out);

    if(!ok && strcmp(filename, DEFAULT_WALLPAPER_NAME) != 0) {
        char default_path[96];
        snprintf(
            default_path, sizeof(default_path), "%s/%s", WALLPAPER_DIR, DEFAULT_WALLPAPER_NAME);
        ok = desktop_parse_xbm_file(desktop->storage, default_path, out);
    }

    if(!ok) {
        furi_mutex_acquire(desktop->wallpaper_mutex, FuriWaitForever);
        desktop->wallpaper_data = (uint8_t*)s_sample_wallpaper_xbm;
        furi_mutex_release(desktop->wallpaper_mutex);
        storage_simply_remove(desktop->storage, WALLPAPER_CURRENT_MARKER);
        return;
    }

    furi_mutex_acquire(desktop->wallpaper_mutex, FuriWaitForever);
    if(desktop->wallpaper_data && memcmp(desktop->wallpaper_data, out, WALLPAPER_SIZE) == 0) {
        furi_mutex_release(desktop->wallpaper_mutex);
        return;
    }
    desktop->wallpaper_data = out;
    desktop->wallpaper_scratch =
        (out == desktop->wallpaper_buf[0]) ? desktop->wallpaper_buf[1] : desktop->wallpaper_buf[0];
    furi_mutex_release(desktop->wallpaper_mutex);
    desktop_write_current_wallpaper_marker(desktop, out);
}

static void desktop_check_wallpaper_updates(Desktop* desktop) {
    if(storage_sd_status(desktop->storage) != FSE_OK) return;

    if(storage_file_exists(desktop->storage, WALLPAPER_ACTIVATE_MARKER)) {
        char name[64] = {0};
        File* f = storage_file_alloc(desktop->storage);
        if(storage_file_open(f, WALLPAPER_ACTIVATE_MARKER, FSAM_READ, FSOM_OPEN_EXISTING)) {
            uint16_t n = storage_file_read(f, name, sizeof(name) - 1);
            name[n] = '\0';
            storage_file_close(f);
        }
        storage_file_free(f);

        size_t len = strlen(name);
        while(len > 0 && (name[len - 1] == '\n' || name[len - 1] == '\r' || name[len - 1] == ' ')) {
            name[--len] = '\0';
        }

        if(len > 0 && len < sizeof(desktop->settings.wallpaper_filename)) {
            char candidate_path[96];
            snprintf(candidate_path, sizeof(candidate_path), "%s/%s", WALLPAPER_DIR, name);
            if(desktop_wallpaper_header_is_128x64(desktop->storage, candidate_path)) {
                strlcpy(
                    desktop->settings.wallpaper_filename,
                    name,
                    sizeof(desktop->settings.wallpaper_filename));
                desktop->settings.wallpaper_enabled = 1;
                desktop_settings_save(&desktop->settings);
            }
        }

        storage_simply_remove(desktop->storage, WALLPAPER_ACTIVATE_MARKER);
    }

    desktop_load_wallpaper(desktop);
}

static uint8_t desktop_alarm_weekday_mask(uint8_t weekday) {
    switch(weekday) {
    case 1: return FOX_ALARM_DAY_MON;
    case 2: return FOX_ALARM_DAY_TUE;
    case 3: return FOX_ALARM_DAY_WED;
    case 4: return FOX_ALARM_DAY_THU;
    case 5: return FOX_ALARM_DAY_FRI;
    case 6: return FOX_ALARM_DAY_SAT;
    case 7: return FOX_ALARM_DAY_SUN;
    default: return 0;
    }
}

static void desktop_trigger_alarm_ring(Desktop* desktop, uint8_t alarm_index) {
    desktop->alarm_ringing = true;
    desktop->alarm_ringing_index = alarm_index;

    notification_message(desktop->notification, &sequence_display_backlight_force_on);
    notification_alarm_start(
        desktop->notification,
        desktop->settings.alarm_beep_enabled,
        desktop->settings.alarm_vibrate_enabled);

    desktop_clock_lock_set_ringing(desktop->clock_lock_view, true);

    if(!desktop->on_clock_lock_scene && !desktop->app_running && !desktop->locked) {
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneClockLock);
    }
}

void desktop_alarm_dismiss(Desktop* desktop) {
    if(!desktop->alarm_ringing) return;
    desktop->alarm_ringing = false;
    notification_alarm_stop(desktop->notification);
    desktop_clock_lock_set_ringing(desktop->clock_lock_view, false);
}

static void desktop_check_alarms(Desktop* desktop) {
    if(desktop->settings.alarm_count == 0) return;

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    uint16_t stamp = (uint16_t)dt.hour * 60 + dt.minute;
    if(stamp == desktop->alarm_last_checked_stamp) return;
    desktop->alarm_last_checked_stamp = stamp;

    uint8_t today_mask = desktop_alarm_weekday_mask(dt.weekday);
    bool settings_changed = false;

    for(uint8_t i = 0; i < desktop->settings.alarm_count; i++) {
        FoxAlarm* alarm = &desktop->settings.alarms[i];
        if(!alarm->active) continue;
        if(alarm->hour != dt.hour || alarm->minute != dt.minute) continue;
        if(alarm->recurring && !(alarm->days_mask & today_mask)) continue;

        if(!alarm->recurring) {

            alarm->active = 0;
            settings_changed = true;
        }
        desktop_trigger_alarm_ring(desktop, i);
    }

    if(settings_changed) {
        desktop_settings_save(&desktop->settings);
    }
}

static void desktop_alarm_check_timer_callback(void* context) {
    Desktop* desktop = context;
    furi_assert(desktop);
    desktop_check_alarms(desktop);
}

static void desktop_ram_watchdog_trigger(Desktop* desktop) {
    FURI_LOG_W(
        TAG,
        "Low RAM watchdog tripped: free heap %zu, total %zu",
        memmgr_get_free_heap(),
        memmgr_get_total_heap());

    loader_signal(desktop->loader, FuriSignalExit, NULL);

    desktop_cli_vcp_session_lock_acquire();

    s_ram_watchdog_usb_config = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(NULL, NULL);
    s_ram_watchdog_usb_disconnected = true;

    for(size_t i = 0; i < gpio_pins_count; i++) {
        if(gpio_pins[i].debug) continue;
        furi_hal_gpio_write(gpio_pins[i].pin, false);
        furi_hal_gpio_init(gpio_pins[i].pin, GpioModeAnalog, GpioPullNo, GpioSpeedVeryHigh);
    }

    desktop->ram_watchdog_tripped = true;
    scene_manager_next_scene(desktop->scene_manager, DesktopSceneLowRam);
}

static void desktop_ram_watchdog_timer_callback(void* context) {
    Desktop* desktop = context;
    furi_assert(desktop);

    size_t total = memmgr_get_total_heap();
    size_t free_heap = memmgr_get_free_heap();

    if(desktop->ram_watchdog_tripped) {

        if(free_heap > (total * RAM_WATCHDOG_RECOVER_HEAP_PERCENT) / 100 &&
           s_ram_watchdog_usb_disconnected) {

            furi_hal_usb_set_config(s_ram_watchdog_usb_config, NULL);
            s_ram_watchdog_usb_config = NULL;

            desktop_cli_vcp_session_lock_release();
            s_ram_watchdog_usb_disconnected = false;

            desktop->ram_watchdog_tripped = false;
            FURI_LOG_I(TAG, "Low RAM watchdog: USB/CLI restored, free heap %zu", free_heap);
        }
        return;
    }

    if(free_heap < (total * RAM_WATCHDOG_TRIP_HEAP_PERCENT) / 100) {
        desktop_ram_watchdog_trigger(desktop);
    }
}

static void desktop_apply_settings(Desktop* desktop) {
    desktop->in_transition = true;

    desktop_clock_reconfigure(desktop);
    desktop_load_wallpaper(desktop);

    view_port_enabled_set(desktop->wifi_icon_viewport, !desktop->settings.wifi_icon_hidden);

    gui_set_statusbar_show_icons(desktop->gui, desktop->settings.statusbar_show_icons);

    {
        Power* power = furi_record_open(RECORD_POWER);
        power_trigger_ui_update(power);
        furi_record_close(RECORD_POWER);
    }

    if(!desktop->app_running && !desktop->locked) {
        desktop_auto_lock_arm(desktop);
    }

    desktop->in_transition = false;
}

static void desktop_init_settings(Desktop* desktop) {
    furi_pubsub_subscribe(storage_get_pubsub(desktop->storage), desktop_storage_callback, desktop);

    if(storage_sd_status(desktop->storage) != FSE_OK) {
        FURI_LOG_D(TAG, "SD Card not ready, skipping settings");
        return;
    }

    desktop_settings_load(&desktop->settings);

    if(desktop->settings.wallpaper_filename[0] == '\0') {
        strlcpy(
            desktop->settings.wallpaper_filename,
            DEFAULT_WALLPAPER_NAME,
            sizeof(desktop->settings.wallpaper_filename));

        desktop->settings.wallpaper_enabled = 0;
        desktop_settings_save(&desktop->settings);
    }

    fox_theme_set_style(desktop->settings.menu_theme);
    desktop_apply_settings(desktop);
}

static Desktop* desktop_alloc(void) {
    Desktop* desktop = malloc(sizeof(Desktop));

    desktop->wallpaper_data  = (uint8_t*)s_sample_wallpaper_xbm;
    desktop->wallpaper_buf[0] = malloc(WALLPAPER_SIZE);
    desktop->wallpaper_buf[1] = malloc(WALLPAPER_SIZE);
    desktop->wallpaper_scratch = desktop->wallpaper_buf[0];
    desktop->wallpaper_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    desktop->no_sd_viewport  = NULL;
    desktop->pending_slideshow = false;
    desktop->ram_watchdog_tripped = false;

    desktop->locked = false;

    desktop->alarm_ringing = false;
    desktop->alarm_ringing_index = 0;
    desktop->on_clock_lock_scene = false;
    desktop->clock_lock_backlight_manually_off = false;
    desktop->alarm_last_checked_stamp = 0xFFFF;

    desktop->gui = furi_record_open(RECORD_GUI);
    desktop->scene_thread = furi_thread_alloc();
    desktop->view_dispatcher = view_dispatcher_alloc();
    desktop->scene_manager = scene_manager_alloc(&desktop_scene_handlers, desktop);

    view_dispatcher_attach_to_gui(
        desktop->view_dispatcher, desktop->gui, ViewDispatcherTypeDesktop);
    view_dispatcher_set_tick_event_callback(
        desktop->view_dispatcher, desktop_tick_event_callback, 500);

    view_dispatcher_set_event_callback_context(desktop->view_dispatcher, desktop);
    view_dispatcher_set_custom_event_callback(
        desktop->view_dispatcher, desktop_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        desktop->view_dispatcher, desktop_back_event_callback);

    desktop->lock_menu = desktop_lock_menu_alloc();
#ifdef FURI_DEBUG
    desktop->debug_view = desktop_debug_alloc();
#endif
    desktop->popup = popup_alloc();
    desktop->locked_view = desktop_view_locked_alloc();
    desktop->pin_input_view = desktop_view_pin_input_alloc();
    desktop->pin_timeout_view = desktop_view_pin_timeout_alloc();
    desktop->slideshow_view = desktop_view_slideshow_alloc();
    desktop->clock_lock_view = desktop_clock_lock_alloc();

    desktop->main_view_stack = view_stack_alloc();
    desktop->main_view = desktop_main_alloc();

    desktop->wallpaper_view = view_alloc();
    view_allocate_model(desktop->wallpaper_view, ViewModelTypeLocking, sizeof(Desktop*));
    with_view_model(
        desktop->wallpaper_view,
        Desktop** model,
        { *model = desktop; },
        false);
    view_set_draw_callback(desktop->wallpaper_view, desktop_wallpaper_draw_callback);

    view_stack_add_view(desktop->main_view_stack, desktop_main_get_view(desktop->main_view));
    view_stack_add_view(desktop->main_view_stack, desktop->wallpaper_view);

    desktop->locked_view_stack = view_stack_alloc();
    view_stack_add_view(
        desktop->locked_view_stack, desktop_view_locked_get_view(desktop->locked_view));

    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdMain,
        view_stack_get_view(desktop->main_view_stack));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdLocked,
        view_stack_get_view(desktop->locked_view_stack));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdLockMenu,
        desktop_lock_menu_get_view(desktop->lock_menu));
#ifdef FURI_DEBUG
    view_dispatcher_add_view(
        desktop->view_dispatcher, DesktopViewIdDebug, desktop_debug_get_view(desktop->debug_view));
#endif
    view_dispatcher_add_view(
        desktop->view_dispatcher, DesktopViewIdPopup, popup_get_view(desktop->popup));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdPinTimeout,
        desktop_view_pin_timeout_get_view(desktop->pin_timeout_view));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdPinInput,
        desktop_view_pin_input_get_view(desktop->pin_input_view));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdSlideshow,
        desktop_view_slideshow_get_view(desktop->slideshow_view));
    view_dispatcher_add_view(
        desktop->view_dispatcher,
        DesktopViewIdClockLock,
        desktop_clock_lock_get_view(desktop->clock_lock_view));

    desktop->lock_icon_viewport = view_port_alloc();
    view_port_set_width(desktop->lock_icon_viewport, icon_get_width(&I_Lock_7x8));
    view_port_draw_callback_set(
        desktop->lock_icon_viewport, desktop_lock_icon_draw_callback, desktop);
    view_port_enabled_set(desktop->lock_icon_viewport, false);
    gui_add_view_port(desktop->gui, desktop->lock_icon_viewport, GuiLayerStatusBarLeft);

    desktop->clock_viewport = view_port_alloc();
    view_port_set_width(desktop->clock_viewport, 50);
    view_port_draw_callback_set(desktop->clock_viewport, desktop_clock_draw_callback, desktop);
    view_port_enabled_set(desktop->clock_viewport, false);
    gui_add_view_port(desktop->gui, desktop->clock_viewport, GuiLayerStatusBarCenter);

    desktop->stealth_mode_icon_viewport = view_port_alloc();
    view_port_set_width(desktop->stealth_mode_icon_viewport, icon_get_width(&I_Muted_8x8));
    view_port_draw_callback_set(
        desktop->stealth_mode_icon_viewport, desktop_stealth_mode_icon_draw_callback, desktop);
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode)) {
        view_port_enabled_set(desktop->stealth_mode_icon_viewport, true);
    } else {
        view_port_enabled_set(desktop->stealth_mode_icon_viewport, false);
    }
    gui_add_view_port(desktop->gui, desktop->stealth_mode_icon_viewport, GuiLayerStatusBarLeft);

    desktop->wifi_icon_viewport = view_port_alloc();
    view_port_set_width(desktop->wifi_icon_viewport, icon_get_width(&I_WiFi_Connected_9x8) + 2);
    view_port_draw_callback_set(
        desktop->wifi_icon_viewport, desktop_wifi_icon_draw_callback, desktop);
    view_port_enabled_set(desktop->wifi_icon_viewport, true);
    gui_add_view_port(desktop->gui, desktop->wifi_icon_viewport, GuiLayerStatusBarRight);

    desktop->loader = furi_record_open(RECORD_LOADER);
    furi_pubsub_subscribe(loader_get_pubsub(desktop->loader), desktop_loader_callback, desktop);

    desktop->storage = furi_record_open(RECORD_STORAGE);
    desktop->notification = furi_record_open(RECORD_NOTIFICATION);
    desktop->input_events_pubsub = furi_record_open(RECORD_INPUT_EVENTS);

    desktop->auto_lock_timer =
        furi_timer_alloc(desktop_auto_lock_timer_callback, FuriTimerTypeOnce, desktop);

    desktop->status_pubsub = furi_pubsub_alloc();

    desktop->update_clock_timer =
        furi_timer_alloc(desktop_clock_timer_callback, FuriTimerTypePeriodic, desktop);

    desktop_refresh_wifi_icon(desktop);

    desktop->alarm_check_timer =
        furi_timer_alloc(desktop_alarm_check_timer_callback, FuriTimerTypePeriodic, desktop);
    furi_timer_start(desktop->alarm_check_timer, furi_ms_to_ticks(ALARM_CHECK_POLL_MS));

    desktop->ram_watchdog_timer =
        furi_timer_alloc(desktop_ram_watchdog_timer_callback, FuriTimerTypePeriodic, desktop);

    desktop->fox_import_timer =
        furi_timer_alloc(desktop_fox_import_timer_callback, FuriTimerTypePeriodic, desktop);
    desktop->fox_setup_timer =
        furi_timer_alloc(desktop_fox_setup_launch_timer_cb, FuriTimerTypeOnce, desktop);
    desktop->banner_timer =
        furi_timer_alloc(desktop_banner_timer_callback, FuriTimerTypeOnce, desktop);

    desktop->app_running = loader_is_locked(desktop->loader);

    desktop->wifi_recheck_thread =
        furi_thread_alloc_ex("FoxWifiRecheck", 2048, desktop_wifi_recheck_thread, desktop);
    furi_thread_start(desktop->wifi_recheck_thread);

    furi_record_create(RECORD_DESKTOP, desktop);

    return desktop;
}

void desktop_lock(Desktop* desktop) {
    furi_assert(!desktop->locked);

    furi_hal_rtc_set_flag(FuriHalRtcFlagLock);

    if(desktop->settings.lock_on_lock_enabled) {
        if(desktop->settings.lock_disconnect_ble) {
            Bt* bt = furi_record_open(RECORD_BT);
            bt_disconnect(bt);
            furi_record_close(RECORD_BT);
        }

        if(desktop->settings.lock_disconnect_gpio) {
            s_locked_gpio_usart  = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
            s_locked_gpio_lpuart = furi_hal_serial_control_acquire(FuriHalSerialIdLpuart);
        }

    }

    {
        bool should_disconnect_usb =
            desktop_pin_code_is_set() ||
            (desktop->settings.lock_usb_level >= LockUsbLevelSessionBlock);

        if(should_disconnect_usb) {

            desktop_cli_vcp_session_lock_acquire();

            s_locked_usb_config = furi_hal_usb_get_config();
            furi_hal_usb_unlock();
            furi_hal_usb_set_config(NULL, NULL);
            s_locked_usb_disconnected = true;
        }
    }

    if(!desktop->settings.lock_show_statusbar) {
        view_port_enabled_set(desktop->clock_viewport, false);
        view_port_enabled_set(desktop->wifi_icon_viewport, false);
        view_port_enabled_set(desktop->stealth_mode_icon_viewport, false);
    }

    desktop_auto_lock_inhibit(desktop);
    scene_manager_set_scene_state(
        desktop->scene_manager, DesktopSceneLocked, DesktopSceneLockedStateFirstEnter);
    scene_manager_next_scene(desktop->scene_manager, DesktopSceneLocked);

    DesktopStatus status = {.locked = true};
    furi_pubsub_publish(desktop->status_pubsub, &status);

    desktop->locked = true;
}

void desktop_unlock(Desktop* desktop) {
    furi_assert(desktop->locked);

    view_port_enabled_set(desktop->lock_icon_viewport, false);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_set_lockdown(gui, false);
    furi_record_close(RECORD_GUI);

    if(!desktop->settings.lock_show_statusbar) {
        view_port_enabled_set(desktop->clock_viewport, desktop->settings.display_clock);
        view_port_enabled_set(desktop->wifi_icon_viewport, !desktop->settings.wifi_icon_hidden);
        view_port_enabled_set(
            desktop->stealth_mode_icon_viewport,
            furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode));
    }

    desktop_view_locked_unlock(desktop->locked_view);
    scene_manager_search_and_switch_to_previous_scene(desktop->scene_manager, DesktopSceneMain);
    desktop_auto_lock_arm(desktop);
    furi_hal_rtc_reset_flag(FuriHalRtcFlagLock);
    furi_hal_rtc_set_pin_fails(0);

    if(desktop->settings.lock_on_lock_enabled) {
        if(desktop->settings.lock_disconnect_gpio) {
            if(s_locked_gpio_usart) {
                furi_hal_serial_control_release(s_locked_gpio_usart);
                s_locked_gpio_usart = NULL;
            }
            if(s_locked_gpio_lpuart) {
                furi_hal_serial_control_release(s_locked_gpio_lpuart);
                s_locked_gpio_lpuart = NULL;
            }
        }

    }

    if(s_locked_usb_disconnected) {

        furi_hal_usb_set_config(s_locked_usb_config, NULL);
        s_locked_usb_config = NULL;

        desktop_cli_vcp_session_lock_release();
        s_locked_usb_disconnected = false;
    }

    DesktopStatus status = {.locked = false};
    furi_pubsub_publish(desktop->status_pubsub, &status);

    desktop->locked = false;
}

void desktop_set_stealth_mode_state(Desktop* desktop, bool enabled) {
    desktop->in_transition = true;

    if(enabled) {
        furi_hal_rtc_set_flag(FuriHalRtcFlagStealthMode);
    } else {
        furi_hal_rtc_reset_flag(FuriHalRtcFlagStealthMode);
    }

    view_port_enabled_set(desktop->stealth_mode_icon_viewport, enabled);

    desktop->in_transition = false;
}

bool desktop_api_is_locked(Desktop* instance) {
    furi_assert(instance);
    return furi_hal_rtc_is_flag_set(FuriHalRtcFlagLock);
}

void desktop_api_unlock(Desktop* instance) {
    furi_assert(instance);
    view_dispatcher_send_custom_event(instance->view_dispatcher, DesktopGlobalApiUnlock);
}

FuriPubSub* desktop_api_get_status_pubsub(Desktop* instance) {
    furi_assert(instance);
    return instance->status_pubsub;
}

void desktop_api_reload_settings(Desktop* instance) {
    furi_assert(instance);
    view_dispatcher_send_custom_event(instance->view_dispatcher, DesktopGlobalReloadSettings);
}

void desktop_api_get_settings(Desktop* instance, DesktopSettings* settings) {
    furi_assert(instance);
    furi_assert(settings);
    *settings = instance->settings;
}

void desktop_api_set_settings(Desktop* instance, const DesktopSettings* settings) {
    furi_assert(instance);
    furi_assert(settings);
    instance->settings = *settings;
    view_dispatcher_send_custom_event(instance->view_dispatcher, DesktopGlobalSaveSettings);
}

void desktop_api_set_pin(Desktop* instance, const DesktopPinCode* pin_code) {
    furi_assert(instance);
    furi_assert(pin_code);
    desktop_pin_code_set(pin_code);
}

void desktop_api_clear_pin(Desktop* instance) {
    furi_assert(instance);
    desktop_pin_code_reset();
}

DesktopUsbMode desktop_api_get_usb_mode(Desktop* instance) {
    furi_assert(instance);
    return instance->usb_msc_active ? DesktopUsbModeMassStorage : DesktopUsbModeQflipper;
}

void desktop_api_set_usb_mode(Desktop* instance, DesktopUsbMode mode) {
    furi_assert(instance);
    instance->usb_msc_active = (mode == DesktopUsbModeMassStorage);
}

int32_t desktop_srv(void* p) {
    UNUSED(p);

    if(furi_hal_rtc_get_boot_mode() != FuriHalRtcBootModeNormal) {
        FURI_LOG_W(TAG, "Skipping start in special boot mode");
        furi_thread_suspend(furi_thread_get_current_id());
        return 0;
    }

    s_desktop_boot_ready = false;

    Desktop* desktop = desktop_alloc();

    {
        if(storage_sd_status(desktop->storage) != FSE_OK) {
            FuriEventFlag* sd_ev = furi_event_flag_alloc();
            FuriPubSubSubscription* sub = furi_pubsub_subscribe(
                storage_get_pubsub(desktop->storage), fox_sd_mount_eventflag_callback, sd_ev);
            if(storage_sd_status(desktop->storage) != FSE_OK) {
                furi_event_flag_wait(sd_ev, 1, FuriFlagWaitAny, 5000);
            }
            furi_pubsub_unsubscribe(storage_get_pubsub(desktop->storage), sub);
            furi_event_flag_free(sd_ev);
        }
        if(storage_sd_status(desktop->storage) != FSE_OK) {
            fox_desktop_show_no_sd_blocking(desktop);
        }
    }

    {
        FileInfo file_info;
        bool sd_settled = false;
        for(uint8_t i = 0; i < 50 && !sd_settled; i++) {
            sd_settled =
                (storage_common_stat(desktop->storage, STORAGE_EXT_PATH_PREFIX, &file_info) ==
                 FSE_OK);
            if(!sd_settled) furi_delay_ms(20);
        }
        if(!sd_settled) {
            FURI_LOG_W(
                TAG,
                "SD card mounted but not answering a real stat() yet - "
                "proceeding anyway, boot-time storage watchdog will catch a real hang");
        }
    }

    desktop_apply_one_time_update(desktop);
    desktop_ensure_wallpaper(desktop);

    {
        bool format_flagged = storage_file_exists(desktop->storage, FOX_FORMAT_FLAG_PATH);
        bool lock_flagged   = storage_file_exists(desktop->storage, FOX_LOCKOUT_FLAG_PATH);
        if(format_flagged) {
            fox_desktop_show_format_blocking(desktop);

        }
        if(lock_flagged) {
            fox_desktop_show_lockout_blocking(desktop);

        }
    }

    desktop_init_settings_with_watchdog(desktop);

    s_desktop_boot_ready = true;

    desktop_pin_code_load_from_storage();

    if(!desktop_pin_code_is_set() && desktop->settings.auto_lock_delay_ms != 0) {
        desktop->settings.auto_lock_delay_ms = 0;
        desktop_settings_save(&desktop->settings);
    }

    if(fox_settings_import_override()) {
        FURI_LOG_I("Desktop", "Fox.Settings override applied");
        furi_hal_power_reset();
    }

    if(fox_recovery_check_and_reset()) {

        desktop_pin_code_reset();
        storage_common_remove(desktop->storage, FOX_LOCKOUT_FLAG_PATH);
        FoxEscrowData recovery_escrow;
        memset(&recovery_escrow, 0, sizeof(FoxEscrowData));
        if(fox_escrow_load_and_verify(&recovery_escrow)) {
            recovery_escrow.active_fail_count = 0;
            fox_escrow_save_state(&recovery_escrow);
        }
        furi_hal_power_reset();
    }

    if(storage_file_exists(desktop->storage, SLIDESHOW_FS_PATH)) {
        storage_common_remove(desktop->storage, FOX_LOCKOUT_FLAG_PATH);
        storage_common_remove(desktop->storage, FOX_FORMAT_FLAG_PATH);
        desktop_pin_code_reset();
        storage_common_remove(desktop->storage, FOX_SETUP_FLAG_PATH);
        storage_common_remove(desktop->storage, FOX_SETUP_FLAG_EXT_PATH);
        storage_common_remove(desktop->storage,
                              EXT_PATH("apps_data/fox_setup/completed.flag"));
    }

    {
        bool setup_done = storage_file_exists(desktop->storage, FOX_SETUP_FLAG_PATH);
        bool int_ok = storage_file_exists(desktop->storage, FOX_SETTINGS_INT_PATH);
        bool ext_ok = storage_file_exists(desktop->storage, FOX_SETTINGS_EXT_PATH);

        if(!int_ok && !ext_ok && setup_done) {

            fox_desktop_show_corrupt_blocking(desktop);

        } else if(!int_ok && ext_ok) {

            fox_settings_sync_sd_to_int();
        } else if(int_ok && !ext_ok) {

            fox_settings_sync_int_to_sd();
        }

    }

    {
        bool flag_int = storage_file_exists(desktop->storage, FOX_SETUP_FLAG_PATH);
        bool flag_ext = storage_file_exists(desktop->storage, FOX_SETUP_FLAG_EXT_PATH);

        if(flag_int && !flag_ext) {

            storage_common_copy(desktop->storage, FOX_SETUP_FLAG_PATH, FOX_SETUP_FLAG_EXT_PATH);
        } else if(!flag_int && flag_ext) {

            storage_common_copy(desktop->storage, FOX_SETUP_FLAG_EXT_PATH, FOX_SETUP_FLAG_PATH);
        }

    }

    scene_manager_next_scene(desktop->scene_manager, DesktopSceneMain);

    furi_timer_start(desktop->ram_watchdog_timer, furi_ms_to_ticks(RAM_WATCHDOG_POLL_MS));
    furi_timer_start(desktop->fox_import_timer, furi_ms_to_ticks(FOX_IMPORT_POLL_MS));

    bool wiper_screen_active = false;
    if(desktop_pin_code_is_set()) {
        FoxEscrowData hcheck;
        memset(&hcheck, 0, sizeof(FoxEscrowData));
        wiper_screen_active = fox_escrow_load_and_verify(&hcheck) &&
                          (hcheck.active_fail_count == 0xFF);
        desktop_lock(desktop);
    }

    if(!wiper_screen_active && storage_file_exists(desktop->storage, SLIDESHOW_FS_PATH)) {
        bool fox_setup_pending = !storage_file_exists(desktop->storage, FOX_SETUP_FLAG_PATH);
        if(fox_setup_pending) {
            desktop->pending_slideshow = true;
        } else {
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneSlideshow);
        }
    }

    /* Fire the one-shot wizard-launch timer (200 ms, so the desktop UI settles
     * first). The callback re-checks the internal setup flag, so starting it
     * unconditionally is harmless on an already-configured device. */
    furi_timer_start(desktop->fox_setup_timer, furi_ms_to_ticks(200));

    if(!furi_hal_version_do_i_belong_here()) {
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneHwMismatch);
    }

    if(furi_hal_rtc_get_fault_data()) {
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneFault);
    }

    uint8_t keys_total, keys_valid;
    if(!furi_hal_crypto_enclave_verify(&keys_total, &keys_valid)) {
        FURI_LOG_E(
            TAG,
            "Secure Enclave verification failed: total %hhu, valid %hhu",
            keys_total,
            keys_valid);
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneSecureEnclave);
    }

    /* FoxFW: all permanent desktop allocations are now placed; let the VCP
     * service enumerate so qFlipper's RPC lands above them (high/transient)
     * instead of wedging the low heap on first boot. */
    furi_hal_usb_boot_gate_release();

    view_dispatcher_run(desktop->view_dispatcher);

    return 0;
}
