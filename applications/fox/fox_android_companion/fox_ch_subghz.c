#include "fox_ch_subghz.h"

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <subghz/devices/devices.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/protocols/raw.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/registry.h>
#include <lib/subghz/subghz_protocol_registry.h>

#include "helpers/radio_device_loader.h"
#include "fox_progress.h"
#include "fox_watchdog.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "FoxSubghzIO"

#define FOX_RX_TMP_NAME "fox_io_rx"
#define FOX_RX_TMP_PATH EXT_PATH("subghz/fox_io_rx.sub")
// "recording is live now" marker — the app polls for it to start its capture timer aligned with the
// real record window (hiding the FAP-boot lag between appStart and the first sample).
#define FOX_RX_MARK EXT_PATH("apps_data/fox_companion/rx_live.txt")
// App drops this file (its on-screen Stop button) to end the record early; we finish + save now.
#define FOX_RX_STOP EXT_PATH("apps_data/fox_companion/rx_stop.txt")

static FuriHalSubGhzPreset fox_preset_enum(int idx) {
    switch(idx) {
    case 0:
        return FuriHalSubGhzPresetOok270Async;
    case 2:
        return FuriHalSubGhzPreset2FSKDev238Async;
    case 3:
        return FuriHalSubGhzPreset2FSKDev476Async;
    default:
        return FuriHalSubGhzPresetOok650Async;
    }
}

static const uint8_t FOX_PRESET_PANDORA_2FSK[] = {
    0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x04, 0x14, 0x00, 0x13, 0x02, 0x12, 0x04,
    0x11, 0x83, 0x10, 0x67, 0x15, 0x47, 0x18, 0x18, 0x19, 0x16, 0x1D, 0x91, 0x1C, 0x00,
    0x1B, 0x07, 0x20, 0xFB, 0x22, 0x10, 0x21, 0x56, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

static void fox_load_preset(const SubGhzDevice* device, int idx) {
    if(idx == 4) {
        subghz_devices_load_preset(
            device, FuriHalSubGhzPresetCustom, (uint8_t*)FOX_PRESET_PANDORA_2FSK);
    } else {
        subghz_devices_load_preset(device, fox_preset_enum(idx), NULL);
    }
}

static const char* fox_preset_name(int idx) {
    switch(idx) {
    case 0:
        return "AM270";
    case 2:
        return "FM238";
    case 3:
        return "FM476";
    default:
        return "AM650";
    }
}

static const SubGhzDevice* fox_device_begin(int dev) {
    const SubGhzDevice* device = NULL;
    if(dev == 1 || dev == 2) {
        device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);
        if(device && radio_device_loader_is_external(device)) {
            return device;
        }
    } else {
        device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeInternal);
    }
    if(!device) device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(device) subghz_devices_begin(device);
    return device;
}

static void fox_device_end(const SubGhzDevice* device) {
    if(!device) return;
    subghz_devices_idle(device);
    subghz_devices_sleep(device);
    radio_device_loader_end(device);
}

static bool fox_copy_file(const char* src, const char* dst) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = false;
    File* in = storage_file_alloc(storage);
    File* out = storage_file_alloc(storage);
    do {
        if(!storage_file_open(in, src, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        if(!storage_file_open(out, dst, FSAM_WRITE, FSOM_CREATE_ALWAYS)) break;
        uint8_t buf[512];
        bool io_ok = true;
        while(true) {
            uint16_t rd = storage_file_read(in, buf, sizeof(buf));
            if(rd == 0) break;
            if(storage_file_write(out, buf, rd) != rd) {
                io_ok = false;
                break;
            }
            if(rd < sizeof(buf)) break;
        }
        ok = io_ok;
    } while(false);
    storage_file_close(in);
    storage_file_close(out);
    storage_file_free(in);
    storage_file_free(out);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static bool fox_do_rx(
    const SubGhzDevice* device,
    uint32_t freq,
    int preset_idx,
    uint32_t ms,
    const char* outpath,
    float rssi_threshold) {
    SubGhzEnvironment* env = subghz_environment_alloc();
    void* raw = subghz_protocol_decoder_raw_alloc(env);

    SubGhzWorker* worker = subghz_worker_alloc();
    subghz_worker_set_overrun_callback(
        worker, (SubGhzWorkerOverrunCallback)subghz_protocol_decoder_raw_reset);
    subghz_worker_set_pair_callback(
        worker, (SubGhzWorkerPairCallback)subghz_protocol_decoder_raw_feed);
    subghz_worker_set_context(worker, raw);

    SubGhzRadioPreset preset;
    preset.name = furi_string_alloc_set(fox_preset_name(preset_idx));
    preset.frequency = freq;
    preset.data = NULL;
    preset.data_size = 0;

    subghz_devices_reset(device);
    subghz_devices_idle(device);
    fox_load_preset(device, preset_idx);
    subghz_devices_set_frequency(device, freq);

    bool ok = subghz_protocol_raw_save_to_file_init((SubGhzProtocolDecoderRAW*)raw, FOX_RX_TMP_NAME, &preset);
    if(ok) {
        subghz_devices_start_async_rx(device, subghz_worker_rx_callback, worker);
        subghz_worker_start(worker);
        // Tell the app the radio is live and recording NOW (see FOX_RX_MARK).
        {
            Storage* mstg = furi_record_open(RECORD_STORAGE);
            File* mf = storage_file_alloc(mstg);
            if(storage_file_open(mf, FOX_RX_MARK, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                storage_file_write(mf, "1", 1);
                storage_file_close(mf);
            }
            storage_file_free(mf);
            furi_record_close(RECORD_STORAGE);
        }
        // Determinate progress page on the Flipper for the known record window; Back stops early.
        FoxProgress* prog = fox_progress_begin("SubGHz\nOperation\nin Progress!");
        // The app can end the record early via its Stop button by dropping FOX_RX_STOP; poll it here
        // and finish now, saving whatever's been captured so far — the same shorter-capture path as an
        // on-device Back/cancel. Open storage once for the poll; clear any stale flag first.
        Storage* stop_stg = furi_record_open(RECORD_STORAGE);
        storage_simply_remove(stop_stg, FOX_RX_STOP);
        uint32_t start = furi_get_tick();
        // RSSI squelch: only record while the signal is stronger than the threshold, so the capture
        // isn't filled with background noise. Same pause/resume mechanism the Flipper's Read RAW uses.
        bool paused = false;
        bool stop_req = false;
        uint8_t stop_check = 0;
        while((furi_get_tick() - start) < furi_ms_to_ticks(ms) && !fox_progress_cancelled(prog) &&
              !stop_req) {
            float rssi = subghz_devices_get_rssi(device);
            bool want_pause = rssi < rssi_threshold;
            if(want_pause != paused) {
                subghz_protocol_raw_save_to_file_pause((SubGhzProtocolDecoderRAW*)raw, want_pause);
                paused = want_pause;
            }
            if(++stop_check >= 5) { // ~every 100ms; no need to stat the SD every 20ms tick
                stop_check = 0;
                if(storage_file_exists(stop_stg, FOX_RX_STOP)) stop_req = true;
            }
            fox_progress_set(prog, (float)(furi_get_tick() - start) / (float)ms);
            fox_watchdog_kick(); // progress -> the watchdog won't reset us mid-capture
            furi_delay_ms(20);
        }
        storage_simply_remove(stop_stg, FOX_RX_STOP); // don't let the flag abort the next capture
        furi_record_close(RECORD_STORAGE);
        subghz_worker_stop(worker);
        subghz_devices_stop_async_rx(device);
        subghz_protocol_raw_save_to_file_stop((SubGhzProtocolDecoderRAW*)raw);
        ok = fox_copy_file(FOX_RX_TMP_PATH, outpath);
        Storage* tmp_storage = furi_record_open(RECORD_STORAGE);
        storage_simply_remove(tmp_storage, FOX_RX_TMP_PATH);
        furi_record_close(RECORD_STORAGE);
        fox_progress_end(prog); // page disappears; a cancel just means a shorter capture
    }

    furi_string_free(preset.name);
    subghz_worker_free(worker);
    subghz_protocol_decoder_raw_free(raw);
    subghz_environment_free(env);
    return ok;
}

static bool
    fox_do_tx(const SubGhzDevice* device, const char* path, uint32_t freq, int preset_idx) {
    subghz_devices_reset(device);
    subghz_devices_idle(device);
    fox_load_preset(device, preset_idx);
    subghz_devices_set_frequency(device, freq);

    SubGhzFileEncoderWorker* enc = subghz_file_encoder_worker_alloc();
    bool ok = subghz_file_encoder_worker_start(enc, path, subghz_devices_get_name(device));
    if(ok) {
        furi_delay_ms(100);
        if(subghz_devices_start_async_tx(
               device, subghz_file_encoder_worker_get_level_duration, enc)) {
            while(!subghz_devices_is_async_complete_tx(device)) {
                furi_delay_ms(20);
            }
            subghz_devices_stop_async_tx(device);
        } else {
            ok = false;
        }
        if(subghz_file_encoder_worker_is_running(enc)) subghz_file_encoder_worker_stop(enc);
    }
    subghz_file_encoder_worker_free(enc);
    subghz_devices_idle(device);
    return ok;
}

static bool fox_do_sweep(
    const SubGhzDevice* device,
    uint32_t start_hz,
    uint32_t stop_hz,
    uint32_t step_hz,
    const char* outpath,
    int passes) {
    if(step_hz == 0) step_hz = 1000000;
    if(stop_hz < start_hz) return false;

    subghz_devices_reset(device);
    subghz_devices_idle(device);
    fox_load_preset(device, 1); // AM650 — wideband enough for an RSSI sweep

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* spec = furi_string_alloc();
    FuriString* stop_path = furi_string_alloc_printf("%s.stop", outpath);

    int pass = 0;
    while(true) {
        furi_string_reset(spec);
        for(uint32_t f = start_hz; f <= stop_hz; f += step_hz) {
            if(!subghz_devices_is_frequency_valid(device, f)) continue;
            subghz_devices_idle(device);
            subghz_devices_set_frequency(device, f);
            subghz_devices_set_rx(device);
            furi_delay_ms(3);
            float rssi = subghz_devices_get_rssi(device);
            subghz_devices_idle(device);
            furi_string_cat_printf(spec, "%lu %d\n", (unsigned long)f, (int)rssi);
            fox_watchdog_kick(); // a long/continuous sweep is legit progress, not a hang
        }
        File* out = storage_file_alloc(storage);
        if(storage_file_open(out, outpath, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_write(out, furi_string_get_cstr(spec), furi_string_size(spec));
            storage_file_close(out);
        }
        storage_file_free(out);

        pass++;
        if(passes != 0 && pass >= passes) break; // finite passes done
        if(storage_file_exists(storage, furi_string_get_cstr(stop_path))) break; // app asked to stop
        furi_delay_ms(10);
    }

    storage_simply_remove(storage, furi_string_get_cstr(stop_path));
    subghz_devices_idle(device);
    furi_string_free(spec);
    furi_string_free(stop_path);
    furi_record_close(RECORD_STORAGE);
    return true;
}

// Detect whether an external CC1101 module is attached (used by the DEVICE receiver status).
bool fox_subghz_external_present(void) {
    subghz_devices_init();
    const SubGhzDevice* d = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);
    bool ext = (d != NULL) && radio_device_loader_is_external(d);
    if(d) radio_device_loader_end(d);
    subghz_devices_deinit();
    return ext;
}

int32_t fox_subghz_channel(const char* args) {
    if(!args || args[0] == '\0') return -1;

    char mode[8] = {0};
    if(sscanf(args, "%7s", mode) != 1) {
        FURI_LOG_E(TAG, "bad args: %s", args);
        return -1;
    }

    subghz_devices_init();
    int32_t rc = 0;

    if(strcmp(mode, "RX") == 0) {
        unsigned long freq = 0, ms = 0;
        int dev = 0, preset = 1;
        int rssi_thr = -65; // dBm squelch; ignore anything weaker (noise). Optional 6th arg overrides.
        char outpath[256] = {0};
        int n = sscanf(
            args, "%*s %lu %d %lu %255s %d %d", &freq, &dev, &ms, outpath, &preset, &rssi_thr);
        if(n >= 4) {
            if(ms == 0) ms = 4000;
            const SubGhzDevice* device = fox_device_begin(dev);
            if(device) {
                bool ok =
                    fox_do_rx(device, (uint32_t)freq, preset, (uint32_t)ms, outpath, (float)rssi_thr);
                fox_device_end(device);
                rc = ok ? 0 : -2;
            } else {
                rc = -3;
            }
        } else {
            FURI_LOG_E(TAG, "RX parse fail (%d): %s", n, args);
            rc = -1;
        }
    } else if(strcmp(mode, "TX") == 0) {
        unsigned long freq = 0;
        int dev = 0, preset = 1;
        char path[256] = {0};
        int n = sscanf(args, "%*s %255s %lu %d %d", path, &freq, &dev, &preset);
        if(n >= 3) {
            const SubGhzDevice* device = fox_device_begin(dev);
            if(device) {
                bool ok = fox_do_tx(device, path, (uint32_t)freq, preset);
                fox_device_end(device);
                rc = ok ? 0 : -2;
            } else {
                rc = -3;
            }
        } else {
            FURI_LOG_E(TAG, "TX parse fail (%d): %s", n, args);
            rc = -1;
        }
    } else if(strcmp(mode, "SWEEP") == 0) {
        unsigned long start_hz = 0, stop_hz = 0, step_hz = 0;
        int dev = 0, passes = 1;
        char outpath[256] = {0};
        int n = sscanf(
            args, "%*s %lu %lu %lu %d %255s %d", &start_hz, &stop_hz, &step_hz, &dev, outpath,
            &passes);
        if(n >= 5) {
            const SubGhzDevice* device = fox_device_begin(dev);
            if(device) {
                bool ok = fox_do_sweep(
                    device, (uint32_t)start_hz, (uint32_t)stop_hz, (uint32_t)step_hz, outpath,
                    passes);
                fox_device_end(device);
                rc = ok ? 0 : -2;
            } else {
                rc = -3;
            }
        } else {
            FURI_LOG_E(TAG, "SWEEP parse fail (%d): %s", n, args);
            rc = -1;
        }
    } else {
        FURI_LOG_E(TAG, "unknown mode: %s", mode);
        rc = -1;
    }

    subghz_devices_deinit();
    return rc;
}

// Live count of sub-GHz protocols in this firmware build (grows as protocols are added).
size_t fox_subghz_protocol_count(void) {
    return subghz_protocol_registry_count(&subghz_protocol_registry);
}
