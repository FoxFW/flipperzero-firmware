#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <cli/cli_vcp.h>

typedef enum {
    RamMonitorFooterDefault,
    RamMonitorFooterSaved,
    RamMonitorFooterSaveFailed,
} RamMonitorFooterState;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriTimer* refresh_timer;
    RamMonitorFooterState footer_state;
    bool usb_locked;
    FuriHalUsbInterface* saved_usb;
} RamMonitorApp;

static void fox_ram_monitor_heap_map_write_callback(void* context, const char* data, size_t length) {
    File* file = context;
    storage_file_write(file, data, length);
}

static bool fox_ram_monitor_save_heap_map(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, "/ext/apps_data");
    storage_simply_mkdir(storage, "/ext/apps_data/heap_map");

    char path[128];
    snprintf(
        path,
        sizeof(path),
        "/ext/apps_data/heap_map/heap_map_%lu_rm.txt",
        (unsigned long)furi_get_tick());

    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, path, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(opened) {
        memmgr_heap_write_fragmentation_map(
            fox_ram_monitor_heap_map_write_callback, file, "ram_monitor_button");
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return opened;
}

static void fox_ram_monitor_usb_lock(RamMonitorApp* app) {
    if(app->usb_locked) return;
    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_session_lock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);
    app->saved_usb = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(NULL, NULL);
    app->usb_locked = true;
}

static void fox_ram_monitor_usb_unlock(RamMonitorApp* app) {
    if(!app->usb_locked) return;
    furi_hal_usb_set_config(app->saved_usb, NULL);
    app->saved_usb = NULL;
    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_session_unlock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);
    app->usb_locked = false;
}

static void fox_ram_monitor_draw_callback(Canvas* canvas, void* context) {
    RamMonitorApp* app = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    char line[40];
    uint8_t y = 8;
    const uint8_t step = 9;

    snprintf(line, sizeof(line), "Free heap:    %zu", memmgr_get_free_heap());
    canvas_draw_str(canvas, 2, y, line);
    y += step;

    snprintf(line, sizeof(line), "Total heap:   %zu", memmgr_get_total_heap());
    canvas_draw_str(canvas, 2, y, line);
    y += step;

    snprintf(line, sizeof(line), "Minimum heap: %zu", memmgr_get_minimum_free_heap());
    canvas_draw_str(canvas, 2, y, line);
    y += step;

    snprintf(line, sizeof(line), "Max heap blk: %zu", memmgr_heap_get_max_free_block());
    canvas_draw_str(canvas, 2, y, line);
    y += step;

    snprintf(line, sizeof(line), "Pool free:    %zu", memmgr_pool_get_free());
    canvas_draw_str(canvas, 2, y, line);
    y += step;

    snprintf(line, sizeof(line), "Max pool blk: %zu", memmgr_pool_get_max_block());
    canvas_draw_str(canvas, 2, y, line);

    const char* footer;
    if(app->footer_state == RamMonitorFooterSaved) {
        footer = "Snapshot saved";
    } else if(app->footer_state == RamMonitorFooterSaveFailed) {
        footer = "Snapshot FAILED";
    } else if(app->usb_locked) {
        footer = "USB OFF  v:on ^:snap";
    } else {
        footer = "USB on  v:off ^:snap";
    }
    canvas_draw_str(canvas, 2, 63, footer);
}

static void fox_ram_monitor_input_callback(InputEvent* event, void* context) {
    RamMonitorApp* app = context;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

static void fox_ram_monitor_refresh_timer_callback(void* context) {
    RamMonitorApp* app = context;
    view_port_update(app->view_port);
}

int32_t fox_ram_monitor_app(void* p) {
    UNUSED(p);

    RamMonitorApp* app = malloc(sizeof(RamMonitorApp));
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->footer_state = RamMonitorFooterDefault;
    app->usb_locked = false;
    app->saved_usb = NULL;

    app->refresh_timer =
        furi_timer_alloc(fox_ram_monitor_refresh_timer_callback, FuriTimerTypePeriodic, app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, fox_ram_monitor_draw_callback, app);
    view_port_input_callback_set(app->view_port, fox_ram_monitor_input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    furi_timer_start(app->refresh_timer, 500);
    view_port_update(app->view_port);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->input_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.key == InputKeyBack) {
                running = false;
            } else if(event.key == InputKeyOk && event.type == InputTypeShort) {
                app->footer_state = RamMonitorFooterDefault;
                view_port_update(app->view_port);
            } else if(event.key == InputKeyUp && event.type == InputTypeShort) {
                bool saved = fox_ram_monitor_save_heap_map();
                app->footer_state = saved ? RamMonitorFooterSaved : RamMonitorFooterSaveFailed;
                view_port_update(app->view_port);
            } else if(event.key == InputKeyDown && event.type == InputTypeShort) {
                if(app->usb_locked) {
                    fox_ram_monitor_usb_unlock(app);
                } else {
                    fox_ram_monitor_usb_lock(app);
                    furi_delay_ms(300);
                }
                app->footer_state = RamMonitorFooterDefault;
                view_port_update(app->view_port);
            }
        }
    }

    furi_timer_stop(app->refresh_timer);
    furi_timer_free(app->refresh_timer);

    if(app->usb_locked) {
        fox_ram_monitor_usb_unlock(app);
    }

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);
    free(app);

    return 0;
}
