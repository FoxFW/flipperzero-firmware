#include <furi.h>
#include <gui/gui.h>
#include <dialogs/dialogs.h>
#include <input/input.h>
#include <stdlib.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>
#include <furi_hal_spi.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_resources.h>
#include <nrf24.h>
#include <notification/notification_messages.h>
#include "nrf24_jammer_icons.h"

#include <stringp.h>

#define TAG "jammer"

typedef struct {
    FuriMutex* mutex;
    bool is_thread_running;
    bool is_nrf24_connected;
    bool close_thread_please;
    uint8_t jam_type; // 0:narrow, 1:wide, 2:all
    FuriThread* mjthread;
} PluginState;

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} PluginEvent;

static void render_callback(Canvas* const canvas, void* ctx) {
    furi_assert(ctx);
    const PluginState* plugin_state = ctx;
    furi_mutex_acquire(plugin_state->mutex, FuriWaitForever);

    // border around the edge of the screen
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontSecondary);
    if(!plugin_state->is_thread_running) {
	canvas_set_font(canvas, FontPrimary);

	char tmp[128];
	char *jam_types[] = {"narrow", "wide", "full"};
	snprintf(tmp, 128, "^ type:%s", jam_types[plugin_state->jam_type]);
       	canvas_draw_str_aligned(canvas, 10, 3, AlignLeft, AlignTop, tmp);
	canvas_set_font(canvas, FontSecondary);
	canvas_draw_str_aligned(canvas, 10, 40, AlignLeft, AlignBottom, "Press Ok button to start");
        if(!plugin_state->is_nrf24_connected) {
            canvas_draw_str_aligned(
                canvas, 10, 60, AlignLeft, AlignBottom, "Connect NRF24 to GPIO!");
        }
    } else if(plugin_state->is_thread_running) {
	canvas_set_font(canvas, FontPrimary);

        char tmp[128];
	char *jam_types[] = {"narrow", "wide", "full"};
	snprintf(tmp, 128, "^ type:%s", jam_types[plugin_state->jam_type]);
       	canvas_draw_str_aligned(canvas, 10, 3, AlignLeft, AlignTop, tmp);
	canvas_set_font(canvas, FontSecondary);

	    
	canvas_draw_str_aligned(canvas, 3, 30, AlignLeft, AlignBottom, "Causing mayhem...");
        canvas_draw_str_aligned(canvas, 3, 40, AlignLeft, AlignBottom, "Please wait!");
        canvas_draw_str_aligned(
            canvas, 3, 50, AlignLeft, AlignBottom, "Press back to exit.");
    } else {
        canvas_draw_str_aligned(canvas, 3, 10, AlignLeft, AlignBottom, "Unknown Error");
        canvas_draw_str_aligned(canvas, 3, 20, AlignLeft, AlignBottom, "press back");
        canvas_draw_str_aligned(canvas, 3, 30, AlignLeft, AlignBottom, "to exit");
    }

    furi_mutex_release(plugin_state->mutex);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void jammer_state_init(PluginState* const plugin_state) {
    plugin_state->is_thread_running = false;
}

// entrypoint for worker
static int32_t mj_worker_thread(void* ctx) {
    PluginState* plugin_state = ctx;
    FURI_LOG_D(TAG, "starting to jam");
    char tmp[128];
    // make sure the NRF24 is powered down so we can do all the initial setup
    nrf24_set_idle(nrf24_HANDLE);
    uint8_t mac[] = { 0xDE, 0xAD}; // DEAD BEEF FEED
    uint8_t ping_packet[] = {0xDE, 0xAD, 0xBE, 0xEF,0xDE, 0xAD, 0xBE, 0xEF,0xDE, 0xAD, 0xBE, 0xEF,0xDE, 0xAD, 0xBE, 0xEF,0xDE, 0xAD, 0xBE, 0xEF,0xDE, 0xAD, 0xBE, 0xEF,0xDE, 0xAD, 0xBE, 0xEF,0xDE, 0xAD, 0xBE, 0xEF}; // 32 bytes, in case we ever need to experiment with bigger packets
    plugin_state->is_thread_running = true;
       
    uint8_t conf = 0;

    nrf24_configure(nrf24_HANDLE, 2, mac, mac, 2, 1, true, true);
    // set PA level to maximum
    uint8_t setup; 
    nrf24_read_reg(nrf24_HANDLE, REG_RF_SETUP, &setup,1);
    
    setup &= 0xF8;
    setup |= 7;
    
    snprintf(tmp, 128, "NRF24 SETUP REGISTER: %d", setup);
    FURI_LOG_D(TAG, tmp);
    
    nrf24_read_reg(nrf24_HANDLE, REG_CONFIG, &conf,1);
    snprintf(tmp, 128, "NRF24 CONFIG REGISTER: %d", conf);
    FURI_LOG_D(TAG, tmp);
    nrf24_write_reg(nrf24_HANDLE, REG_RF_SETUP, setup);

    #define size 32
    uint8_t status = 0;
    uint8_t tx[size + 1];
    uint8_t rx[size + 1];
    memset(tx, 0, size + 1);
    memset(rx, 0, size + 1);

    tx[0] = W_TX_PAYLOAD_NOACK;

    memcpy(&tx[1], ping_packet, size);

    #define nrf24_TIMEOUT 500
    // push data to the TX register
    nrf24_spi_trx(nrf24_HANDLE, tx, 0, size + 1, nrf24_TIMEOUT);
    // put the module in TX mode
    nrf24_set_tx_mode(nrf24_HANDLE);
    // send one test packet (for debug reasons)
    while(!(status & (TX_DS | MAX_RT))) 
    {
        status = nrf24_status(nrf24_HANDLE);
        snprintf(tmp, 128, "NRF24 STATUS REGISTER: %d", status);
        
        FURI_LOG_D(TAG, tmp);
    }
    // various types of hopping I empirically found
    uint8_t hopping_channels_2[128];
    for(int i = 0; i < 128; i++) hopping_channels_2[i] = i;
    uint8_t hopping_channels_1[] = {32,34, 46,48, 50, 52, 0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 74, 76, 78, 80, 82, 84,86 };
    uint8_t hopping_channels_0[] = {2, 26, 80};
    uint8_t hopping_channels_len[] = {3, 24, 124};

    uint8_t chan = 0;
    uint8_t limit = 0;
    do {
	limit = hopping_channels_len[plugin_state->jam_type];
        for(int ch = 0;ch < limit; ch++) {
	    switch(plugin_state->jam_type) {
		case 0: chan = hopping_channels_0[ch]; break;
		case 1: chan = hopping_channels_1[ch]; break;
		case 2: chan = hopping_channels_2[ch]; break;
		default: break;
	    }
	    // change channel
            nrf24_write_reg(nrf24_HANDLE, REG_RF_CH, chan);
            // push new data to the TX register
            nrf24_spi_trx(nrf24_HANDLE, tx, 0, 3, nrf24_TIMEOUT);
        }
    } while(!plugin_state->close_thread_please);
    
    plugin_state->is_thread_running = false;
    nrf24_set_idle(nrf24_HANDLE);
    return 0;
}

int32_t jammer_app(void* p) {
    UNUSED(p);
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(PluginEvent));

    PluginState* plugin_state = malloc(sizeof(PluginState));
    jammer_state_init(plugin_state);
    plugin_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!plugin_state->mutex) {
        FURI_LOG_E("jammer", "cannot create mutex\r\n");
        furi_message_queue_free(event_queue);
        free(plugin_state);
        return 255;
    }

    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);

    // Set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, plugin_state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    plugin_state->mjthread = furi_thread_alloc();
    furi_thread_set_name(plugin_state->mjthread, "MJ Worker");
    furi_thread_set_stack_size(plugin_state->mjthread, 2048);
    furi_thread_set_context(plugin_state->mjthread, plugin_state);
    furi_thread_set_callback(plugin_state->mjthread, mj_worker_thread);
    FURI_LOG_D(TAG, "nrf24 init...");
    nrf24_init();
    FURI_LOG_D(TAG, "nrf24 init done!");
    PluginEvent event;
    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);
        furi_mutex_acquire(plugin_state->mutex, FuriWaitForever);

        if(event_status == FuriStatusOk) {
            // press events
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyUp:
			plugin_state->jam_type = (plugin_state->jam_type + 1) % 3;
                        break;
                    case InputKeyDown:
                        break;
                    case InputKeyRight:
                        break;
                    case InputKeyLeft:
                        break;
                    case InputKeyOk:
                        if(!nrf24_check_connected(nrf24_HANDLE)) {
                            plugin_state->is_nrf24_connected = false;
                            view_port_update(view_port);
                            notification_message(notification, &sequence_error);
                        } else if(!plugin_state->is_thread_running) {
                            furi_thread_start(plugin_state->mjthread);
                            view_port_update(view_port);
                        }
                        break;
                    case InputKeyBack:
                        FURI_LOG_D(TAG, "CLOSE_PLZ");
			if(!plugin_state->is_thread_running) processing = false;

                        plugin_state->close_thread_please = true;
                        if(plugin_state->is_thread_running && plugin_state->mjthread) {
                            furi_thread_join(
                                plugin_state->mjthread); // wait until thread is finished
                        }
                        plugin_state->close_thread_please = false;
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        view_port_update(view_port);
        furi_mutex_release(plugin_state->mutex);
    }

    furi_thread_free(plugin_state->mjthread);
    FURI_LOG_D(TAG, "nrf24 deinit...");
    nrf24_deinit();
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(plugin_state->mutex);
    free(plugin_state);

    return 0;
}
