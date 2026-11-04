#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <loader/loader.h>

#include "esp_at.h"

typedef enum {
    UpdaterViewMessage,
    UpdaterViewConnectSettings,
    UpdaterViewMenu,
    UpdaterViewBoard,
    UpdaterViewStatus,
    UpdaterViewProgress,
    UpdaterViewCheckProgress,
    UpdaterViewDownloadSettings,
} UpdaterView;

typedef enum {
    UpdaterEventMenuFw = 1,
    UpdaterEventMenuEsp32 = 2,
    UpdaterEventBoardGo = 3,
    UpdaterEventWorkerDone = 4,
    UpdaterEventStatusConfirm = 5,
    UpdaterEventStatusBack = 6,
    UpdaterEventProgressTick = 7,
    UpdaterEventProgressCancelled = 8,
    UpdaterEventCheckProgressNext = 9,
    UpdaterEventCachedOptionConfirm = 10,
    UpdaterEventMenuDownloadSettings = 11,
} UpdaterEvent;

typedef enum {
    UpdaterFlowNone = 0,
    UpdaterFlowFirmware,
    UpdaterFlowEsp32,
} UpdaterFlow;

typedef enum {
    UpdaterStageCheck,
    UpdaterStageDownload,
    UpdaterStageInstall,
} UpdaterStage;

typedef enum {
    ProgressPhaseDownload,
    ProgressPhaseVerify,
    ProgressPhaseInstall,
} ProgressPhase;

typedef enum {
    UpdaterResultNone = 0,
    UpdaterResultUpToDate,
    UpdaterResultUpdateAvailable,
    UpdaterResultDownloaded,
    UpdaterResultAlreadyDownloaded,
    UpdaterResultError,
} UpdaterResult;

typedef enum {
    UpdaterActionNone,
    UpdaterActionStartDownload,
    UpdaterActionInstall,
    UpdaterActionConfirmEsp32Install,
    UpdaterActionEsp32ResetAndInstall,
} UpdaterAction;

#define UPDATER_BOARD_COUNT 6

typedef struct {
    const char* label;
    const char* folder;
    const char* match;
} UpdaterBoard;

extern const UpdaterBoard k_updater_boards[UPDATER_BOARD_COUNT];

#define UPDATER_PATH_LEN  256
#define UPDATER_STR_LEN   96
#define UPDATER_URL_LEN   512
#define UPDATER_ASSET_MAX 24

#define UPDATER_DATA_DIR  EXT_PATH("apps_data/fox_update_downloader")
#define UPDATER_BAUD      115200U
#define UPDATER_FAST_BAUD 921600U

#define FOXFW_REPO   "FoxFW/flipperzero-firmware"
#define ESP32FW_REPO "FoxFW/Fox_ESP32_FW"

#define FOXFW_TAR_ASSET_NAME "flipper-z-f7-update-local.tar"

#define FOX_ESP32_FLASHER_FAP "/ext/apps/Fox/ESP32/fox_esp32_flasher.fap"

typedef struct {
    char name[UPDATER_STR_LEN];
    char url[UPDATER_URL_LEN];
    uint32_t size;
} ReleaseAsset;

typedef struct {
    bool ok;
    char tag[UPDATER_STR_LEN];
    char commit[16];
    ReleaseAsset assets[UPDATER_ASSET_MAX];
    uint8_t asset_count;
    char error[UPDATER_STR_LEN];
} ReleaseInfo;

typedef struct {
    uint32_t baud;
    uint8_t retry_attempts;
    uint16_t timeout_sec;
    bool auto_lower_baud;
} DownloaderSettings;

typedef struct UpdaterApp UpdaterApp;

struct UpdaterApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Storage* storage;
    DialogsApp* dialogs;
    Loader* loader;

    View* message_view;
    View* menu_view;
    View* board_view;
    View* status_view;
    View* progress_view;
    View* check_progress_view;

    EspAt* esp_at;
    bool message_view_detecting;
    bool message_view_not_detected_focus_left;
    size_t pin_option_index;
    size_t baud_option_index;
    View* connect_settings_view;
    uint8_t connect_settings_selected;

    View* download_settings_view;
    uint8_t download_settings_selected;
    DownloaderSettings settings;

    UpdaterView current_view;
    UpdaterFlow flow;
    UpdaterStage stage;
    UpdaterResult result;
    UpdaterAction pending_action;
    uint8_t board_index;

    bool verifying_cached;
    bool verifying_partial;
    uint8_t status_cycle_kind;
    char cached_tag[UPDATER_STR_LEN];
    char cached_commit[16];
    char cached_asset_path[UPDATER_PATH_LEN];
    char cached_board_folder[16];

    ReleaseInfo release;
    int matched_asset;
    int matched_boot;
    int matched_part;
    int matched_fw;

    bool esp32_assets_ready;
    ReleaseAsset esp32_boot_asset;
    ReleaseAsset esp32_part_asset;
    ReleaseAsset esp32_fw_asset;

    char compare_current[24];
    char compare_latest[24];

    char status_title[UPDATER_STR_LEN];
    char status_line1[UPDATER_STR_LEN];
    char status_line2[UPDATER_STR_LEN];
    char status_btn_left[16];
    char status_btn_right[16];
    bool status_has_left;
    bool status_has_right;
    uint8_t status_selected;

    bool status_cycle_mode;
    const char* status_cycle_options[5];
    uint8_t status_cycle_count;
    uint8_t status_cycle_selected;

    char download_path[UPDATER_PATH_LEN];
    char download_name[UPDATER_STR_LEN];
    char extract_current_name[UPDATER_STR_LEN];

    FuriThread* worker;
    volatile bool worker_running;
    volatile bool cancel_requested;

    FuriMutex* progress_mutex;
    volatile uint32_t progress_bytes;
    volatile uint32_t progress_total;
    volatile bool progress_done;
    volatile bool progress_ok;
    volatile uint8_t progress_phase;
    char progress_error[UPDATER_STR_LEN];

    FuriTimer* progress_timer;

    char check_stage_label[32];
    volatile uint8_t check_stage_target_pct;
    volatile bool check_stage_await_next;
    FuriTimer* check_progress_timer;
};

View* view_message_alloc(UpdaterApp* app);
void view_message_free(View* v);

View* connect_settings_view_alloc(UpdaterApp* app);
void connect_settings_view_free(View* v);
void connect_settings_view_reset(UpdaterApp* app);

View* download_settings_view_alloc(UpdaterApp* app);
void download_settings_view_free(View* v);
void download_settings_view_reset(UpdaterApp* app);
void updater_settings_load(UpdaterApp* app);
void updater_settings_save(UpdaterApp* app);

size_t app_pin_option_count(void);
const char* app_pin_option_label(size_t index);
size_t app_baud_option_count(void);
uint32_t app_baud_option_value(size_t index);
bool app_probe_uart_selected(UpdaterApp* app);

View* view_menu_alloc(UpdaterApp* app);
void view_menu_free(View* v);

View* view_board_alloc(UpdaterApp* app);
void view_board_free(View* v);
void view_board_refresh(View* v);

View* view_status_alloc(UpdaterApp* app);
void view_status_free(View* v);
void view_status_refresh(View* v);

View* view_progress_alloc(UpdaterApp* app);
void view_progress_free(View* v);
void view_progress_refresh(View* v);
void view_progress_reset(View* v);

View* view_check_progress_alloc(UpdaterApp* app);
void view_check_progress_free(View* v);
void view_check_progress_refresh(View* v);
void view_check_progress_reset(View* v);

void updater_switch_to_status(
    UpdaterApp* app,
    const char* title,
    const char* line1,
    const char* line2,
    const char* btn_left,
    const char* btn_right);

void updater_switch_to_status_cycle(
    UpdaterApp* app,
    const char* title,
    const char* line1,
    const char* line2,
    const char* const* options,
    uint8_t option_count);

void updater_handle_cached_option(UpdaterApp* app);

void updater_start_worker(UpdaterApp* app, UpdaterStage stage);

bool updater_has_partial_download(
    UpdaterApp* app,
    UpdaterFlow flow,
    char* tag_out,
    size_t tag_out_size,
    char* asset_path_out,
    size_t asset_path_out_size);

void updater_draw_ok_button(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t w,
    uint8_t h,
    uint8_t radius,
    const char* label);

void updater_draw_ok_button_centered(
    Canvas* canvas,
    uint8_t y,
    uint8_t h,
    uint8_t radius,
    const char* label);

void updater_handle_worker_done(UpdaterApp* app);
void updater_handle_status_confirm(UpdaterApp* app);
void updater_handle_status_back(UpdaterApp* app);
void updater_set_check_stage(UpdaterApp* app, const char* label, uint8_t target_pct);
void updater_retry_detection(UpdaterApp* app);
