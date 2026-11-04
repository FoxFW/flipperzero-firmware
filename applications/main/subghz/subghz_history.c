#include "subghz_history.h"
#include <lib/subghz/receiver.h>
#include <flipper_format/flipper_format_i.h>
#include <storage/storage.h>
#include <toolbox/stream/stream.h>

#include <furi.h>

#define SUBGHZ_HISTORY_FREE_HEAP      12288
#define SUBGHZ_HISTORY_PING_NOTIFY_MS 400U
#define SUBGHZ_HISTORY_BLOCK          16U
#define SUBGHZ_HISTORY_BLOCKS \
    ((SUBGHZ_HISTORY_MAX + SUBGHZ_HISTORY_BLOCK - 1U) / SUBGHZ_HISTORY_BLOCK)
#define SUBGHZ_HISTORY_NO_NAME        0xFFFFU
#define SUBGHZ_HISTORY_FLUSH_PER_TICK 4U
#define SUBGHZ_HISTORY_DIR            EXT_PATH("subghz/.history")
#define TAG                           "SubGhzHistory"

typedef struct {
    FlipperFormat* ff;
    uint8_t* preset_data;
    size_t preset_data_size;
    uint32_t frequency;
    uint32_t last_ping_tick;
    uint32_t serial;
    uint32_t file_id;
    DateTime datetime;
    uint16_t proto_idx;
    uint16_t preset_idx;
    uint8_t key[sizeof(uint64_t)];
    char label[32];
    uint8_t type;
    uint8_t source;
    bool has_key;
    bool auto_save_pending;
    bool on_sd;
} SubGhzHistoryItem;

ARRAY_DEF(SubGhzHistoryNameArray, FuriString*, M_POD_OPLIST)

struct SubGhzHistory {
    uint32_t last_update_timestamp;
    uint32_t next_file_id;
    uint32_t cache_id;
    uint16_t count;
    uint8_t code_last_hash_data;
    bool sd_ok;
    bool dir_ready;
    FuriString* tmp_string;
    SubGhzHistoryItem* blocks[SUBGHZ_HISTORY_BLOCKS];
    SubGhzHistoryNameArray_t names;
    FlipperFormat* cache;
    SubGhzRadioPreset preset_tmp;
    FuriMutex* mutex;
};

static void subghz_history_path(FuriString* path, uint32_t file_id) {
    furi_string_printf(path, "%s/%lu.sub", SUBGHZ_HISTORY_DIR, (unsigned long)file_id);
}

static void subghz_history_wipe_dir(SubGhzHistory* instance) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove_recursive(storage, SUBGHZ_HISTORY_DIR);
    furi_record_close(RECORD_STORAGE);
    instance->dir_ready = false;
    instance->sd_ok = true;
}

static SubGhzHistoryItem* subghz_history_at(SubGhzHistory* instance, uint16_t idx) {
    if(idx >= instance->count) return NULL;
    return &instance->blocks[idx / SUBGHZ_HISTORY_BLOCK][idx % SUBGHZ_HISTORY_BLOCK];
}

static SubGhzHistoryItem* subghz_history_find_id(SubGhzHistory* instance, uint32_t file_id) {
    for(uint16_t i = 0; i < instance->count; i++) {
        SubGhzHistoryItem* item = subghz_history_at(instance, i);
        if(item->file_id == file_id) return item;
    }
    return NULL;
}

static SubGhzHistoryItem* subghz_history_push(SubGhzHistory* instance) {
    if(instance->count >= SUBGHZ_HISTORY_MAX) return NULL;
    uint16_t block = instance->count / SUBGHZ_HISTORY_BLOCK;
    if(!instance->blocks[block]) {
        instance->blocks[block] = malloc(sizeof(SubGhzHistoryItem) * SUBGHZ_HISTORY_BLOCK);
    }
    SubGhzHistoryItem* item =
        &instance->blocks[block][instance->count % SUBGHZ_HISTORY_BLOCK];
    memset(item, 0, sizeof(SubGhzHistoryItem));
    item->file_id = ++instance->next_file_id;
    item->proto_idx = SUBGHZ_HISTORY_NO_NAME;
    item->preset_idx = SUBGHZ_HISTORY_NO_NAME;
    instance->count++;
    return item;
}

static void subghz_history_release_blocks(SubGhzHistory* instance, uint16_t keep) {
    for(uint16_t b = keep; b < SUBGHZ_HISTORY_BLOCKS; b++) {
        if(instance->blocks[b]) {
            free(instance->blocks[b]);
            instance->blocks[b] = NULL;
        }
    }
}

static void subghz_history_remove_at(SubGhzHistory* instance, uint16_t idx) {
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    if(!item) return;
    if(item->ff) flipper_format_free(item->ff);
    for(uint16_t i = idx; i + 1 < instance->count; i++) {
        *subghz_history_at(instance, i) = *subghz_history_at(instance, i + 1);
    }
    instance->count--;
    uint16_t used = (instance->count + SUBGHZ_HISTORY_BLOCK - 1U) / SUBGHZ_HISTORY_BLOCK;
    subghz_history_release_blocks(instance, used + 1U);
}

static void subghz_history_clear_items(SubGhzHistory* instance) {
    for(uint16_t i = 0; i < instance->count; i++) {
        SubGhzHistoryItem* item = subghz_history_at(instance, i);
        if(item->ff) flipper_format_free(item->ff);
    }
    instance->count = 0;
    subghz_history_release_blocks(instance, 0);
    size_t names = SubGhzHistoryNameArray_size(instance->names);
    for(size_t i = 0; i < names; i++) {
        furi_string_free(*SubGhzHistoryNameArray_get(instance->names, i));
    }
    SubGhzHistoryNameArray_reset(instance->names);
}

static uint16_t subghz_history_intern(SubGhzHistory* instance, const char* str) {
    size_t size = SubGhzHistoryNameArray_size(instance->names);
    for(size_t i = 0; i < size; i++) {
        if(furi_string_equal_str(*SubGhzHistoryNameArray_get(instance->names, i), str)) {
            return (uint16_t)i;
        }
    }
    if(size >= SUBGHZ_HISTORY_NO_NAME) return SUBGHZ_HISTORY_NO_NAME;
    SubGhzHistoryNameArray_push_back(instance->names, furi_string_alloc_set_str(str));
    return (uint16_t)size;
}

static const char* subghz_history_name(SubGhzHistory* instance, uint16_t name_idx) {
    if(name_idx >= SubGhzHistoryNameArray_size(instance->names)) return "";
    return furi_string_get_cstr(*SubGhzHistoryNameArray_get(instance->names, name_idx));
}

static void subghz_history_fill_common(
    SubGhzHistory* instance,
    SubGhzHistoryItem* item,
    SubGhzRadioPreset* preset) {
    item->auto_save_pending = false;
    item->last_ping_tick = furi_get_tick();
    item->frequency = preset->frequency;
    item->preset_idx = subghz_history_intern(instance, furi_string_get_cstr(preset->name));
    item->preset_data = preset->data;
    item->preset_data_size = preset->data_size;
    furi_hal_rtc_get_datetime(&item->datetime);
}

static void subghz_history_fill_match_fields(
    SubGhzHistory* instance,
    SubGhzHistoryItem* item,
    FlipperFormat* ff) {
    flipper_format_rewind(ff);
    if(flipper_format_read_string(ff, "Protocol", instance->tmp_string)) {
        item->proto_idx = subghz_history_intern(instance, furi_string_get_cstr(instance->tmp_string));
    }
    flipper_format_rewind(ff);
    item->has_key = flipper_format_read_hex(ff, "Key", item->key, sizeof(item->key));
    uint8_t serial[4] = {0};
    flipper_format_rewind(ff);
    if(flipper_format_read_hex(ff, "Key", serial, sizeof(serial))) {
        item->serial = ((uint32_t)serial[0] << 24) | ((uint32_t)serial[1] << 16) |
                       ((uint32_t)serial[2] << 8) | (uint32_t)serial[3];
    }
    flipper_format_rewind(ff);
}

SubGhzHistory* subghz_history_alloc(void) {
    SubGhzHistory* instance = malloc(sizeof(SubGhzHistory));
    memset(instance, 0, sizeof(SubGhzHistory));
    instance->tmp_string = furi_string_alloc();
    instance->preset_tmp.name = furi_string_alloc();
    SubGhzHistoryNameArray_init(instance->names);
    instance->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    subghz_history_wipe_dir(instance);
    return instance;
}

void subghz_history_free(SubGhzHistory* instance) {
    furi_assert(instance);
    subghz_history_clear_items(instance);
    SubGhzHistoryNameArray_clear(instance->names);
    subghz_history_release_cache(instance);
    subghz_history_wipe_dir(instance);
    furi_string_free(instance->tmp_string);
    furi_string_free(instance->preset_tmp.name);
    furi_mutex_free(instance->mutex);
    free(instance);
}

SubGhzRadioPreset* subghz_history_get_radio_preset(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    if(item) {
        furi_string_set_str(
            instance->preset_tmp.name, subghz_history_name(instance, item->preset_idx));
        instance->preset_tmp.frequency = item->frequency;
        instance->preset_tmp.data = item->preset_data;
        instance->preset_tmp.data_size = item->preset_data_size;
    }
    furi_mutex_release(instance->mutex);
    return &instance->preset_tmp;
}

void subghz_history_reset(SubGhzHistory* instance) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    furi_string_reset(instance->tmp_string);
    subghz_history_clear_items(instance);
    instance->code_last_hash_data = 0;
    furi_mutex_release(instance->mutex);
    subghz_history_release_cache(instance);
    subghz_history_wipe_dir(instance);
}

void subghz_history_delete_item(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    subghz_history_remove_at(instance, idx);
    furi_mutex_release(instance->mutex);
}

uint16_t subghz_history_get_item(SubGhzHistory* instance) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    uint16_t count = instance->count;
    furi_mutex_release(instance->mutex);
    return count;
}

SubGhzHistorySource subghz_history_get_source(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    SubGhzHistorySource source = item ? (SubGhzHistorySource)item->source : SubGhzHistorySourceNone;
    furi_mutex_release(instance->mutex);
    return source;
}

const char* subghz_history_get_protocol_name(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    const char* name = item ? subghz_history_name(instance, item->proto_idx) : "";
    furi_mutex_release(instance->mutex);
    return name;
}

DateTime subghz_history_get_datetime(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    DateTime datetime = item ? item->datetime : (DateTime){};
    furi_mutex_release(instance->mutex);
    return datetime;
}

static bool subghz_history_load_cache(SubGhzHistory* instance, uint32_t file_id) {
    if(!instance->cache) instance->cache = flipper_format_string_alloc();
    Stream* stream = flipper_format_get_raw_stream(instance->cache);
    stream_clean(stream);
    instance->cache_id = 0;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc();
    subghz_history_path(path, file_id);
    bool ok = stream_load_from_file(stream, storage, furi_string_get_cstr(path)) > 0;
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
    stream_rewind(stream);
    if(ok) {
        instance->cache_id = file_id;
    } else {
        FURI_LOG_E(TAG, "Load %lu failed", (unsigned long)file_id);
    }
    return ok;
}

FlipperFormat* subghz_history_get_raw_data(SubGhzHistory* instance, uint16_t idx) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    FlipperFormat* raw_data = NULL;
    uint32_t file_id = 0;
    if(item) {
        if(item->ff) {
            raw_data = item->ff;
        } else if(item->on_sd) {
            file_id = item->file_id;
        }
    }
    furi_mutex_release(instance->mutex);
    if(file_id) {
        if(instance->cache_id != file_id) subghz_history_load_cache(instance, file_id);
        raw_data = instance->cache;
    }
    return raw_data;
}

void subghz_history_release_cache(SubGhzHistory* instance) {
    furi_assert(instance);
    if(instance->cache) {
        flipper_format_free(instance->cache);
        instance->cache = NULL;
    }
    instance->cache_id = 0;
}

bool subghz_history_copy_item_to_file(SubGhzHistory* instance, uint16_t idx, const char* path) {
    furi_assert(instance);
    furi_assert(path);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    bool on_sd = item && !item->ff && item->on_sd;
    uint32_t file_id = item ? item->file_id : 0;
    furi_mutex_release(instance->mutex);
    if(!item) return false;

    bool ok = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(on_sd) {
        FuriString* src = furi_string_alloc();
        subghz_history_path(src, file_id);
        storage_simply_remove(storage, path);
        ok = storage_common_copy(storage, furi_string_get_cstr(src), path) == FSE_OK;
        furi_string_free(src);
    } else {
        FlipperFormat* ff = subghz_history_get_raw_data(instance, idx);
        if(ff) {
            Stream* stream = flipper_format_get_raw_stream(ff);
            size_t size = stream_size(stream);
            stream_rewind(stream);
            ok = stream_save_to_file(stream, storage, path, FSOM_CREATE_ALWAYS) == size;
            stream_rewind(stream);
        }
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static bool subghz_history_write_file(SubGhzHistory* instance, uint32_t file_id, FlipperFormat* ff) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!instance->dir_ready) {
        instance->dir_ready = storage_simply_mkdir(storage, EXT_PATH("subghz")) &&
                              storage_simply_mkdir(storage, SUBGHZ_HISTORY_DIR);
    }
    bool ok = false;
    if(instance->dir_ready) {
        FuriString* path = furi_string_alloc();
        subghz_history_path(path, file_id);
        Stream* stream = flipper_format_get_raw_stream(ff);
        size_t size = stream_size(stream);
        stream_rewind(stream);
        ok = stream_save_to_file(stream, storage, furi_string_get_cstr(path), FSOM_CREATE_ALWAYS) ==
             size;
        stream_rewind(stream);
        furi_string_free(path);
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}

void subghz_history_flush(SubGhzHistory* instance) {
    furi_assert(instance);
    for(uint8_t n = 0; n < SUBGHZ_HISTORY_FLUSH_PER_TICK; n++) {
        FlipperFormat* ff = NULL;
        uint32_t file_id = 0;
        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        if(instance->sd_ok) {
            for(uint16_t i = 0; i < instance->count; i++) {
                SubGhzHistoryItem* item = subghz_history_at(instance, i);
                if(item->ff) {
                    ff = item->ff;
                    file_id = item->file_id;
                    item->ff = NULL;
                    break;
                }
            }
        }
        furi_mutex_release(instance->mutex);
        if(!ff) return;

        bool ok = subghz_history_write_file(instance, file_id, ff);

        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        SubGhzHistoryItem* item = subghz_history_find_id(instance, file_id);
        if(item) {
            if(ok) {
                item->on_sd = true;
            } else {
                item->ff = ff;
                ff = NULL;
                instance->sd_ok = false;
            }
        }
        furi_mutex_release(instance->mutex);
        if(ff) flipper_format_free(ff);
        if(!ok) {
            FURI_LOG_E(TAG, "SD write failed, keeping history in RAM");
            return;
        }
    }
}

void subghz_history_set_auto_save_pending(SubGhzHistory* instance, uint16_t idx, bool pending) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    if(item) item->auto_save_pending = pending;
    furi_mutex_release(instance->mutex);
}

bool subghz_history_find_auto_save_pending(SubGhzHistory* instance, uint16_t* idx) {
    furi_assert(instance);
    furi_assert(idx);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    bool found = false;
    for(uint16_t i = 0; i < instance->count; i++) {
        if(subghz_history_at(instance, i)->auto_save_pending) {
            *idx = i;
            found = true;
            break;
        }
    }
    furi_mutex_release(instance->mutex);
    return found;
}

bool subghz_history_get_text_space_left(SubGhzHistory* instance, FuriString* output) {
    furi_assert(instance);
    if(memmgr_get_free_heap() < SUBGHZ_HISTORY_FREE_HEAP) {
        if(output != NULL) furi_string_printf(output, "  RAM almost FULL");
        return true;
    }
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    uint16_t count = instance->count;
    furi_mutex_release(instance->mutex);
    if(count >= SUBGHZ_HISTORY_MAX) {
        if(output != NULL) furi_string_printf(output, "   Memory is FULL");
        return true;
    }
    if(output != NULL) furi_string_printf(output, "%02u/%u", count, SUBGHZ_HISTORY_MAX);
    return false;
}

uint16_t subghz_history_get_last_index(SubGhzHistory* instance) {
    return subghz_history_get_item(instance);
}

void subghz_history_get_menu_item(
    void* context,
    uint16_t idx,
    FuriString* name,
    FuriString* time,
    uint8_t* type) {
    SubGhzHistory* instance = context;
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    if(item) {
        if(item->source == SubGhzHistorySourceInternal) {
            furi_string_printf(name, "[I] %s", item->label);
        } else if(item->source == SubGhzHistorySourceExternal) {
            furi_string_printf(name, "[E] %s", item->label);
        } else {
            furi_string_set_str(name, item->label);
        }
        furi_string_printf(
            time,
            "%.2d:%.2d:%.2d ",
            item->datetime.hour,
            item->datetime.minute,
            item->datetime.second);
        *type = item->type;
    } else {
        furi_string_reset(name);
        furi_string_reset(time);
        *type = 0;
    }
    furi_mutex_release(instance->mutex);
}

int16_t subghz_history_dedup_last_fiat_hitag2(SubGhzHistory* instance) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    int16_t removed = -1;
    if(instance->count >= 2) {
        uint16_t last = instance->count - 1;
        SubGhzHistoryItem* li = subghz_history_at(instance, last);
        const char* ln = subghz_history_name(instance, li->proto_idx);
        uint32_t ls = li->serial;
        bool last_is_fiat = strcmp(ln, "Fiat V1") == 0;
        bool last_is_hitag = strcmp(ln, "Hitag-2") == 0;
        if(ls != 0 && (last_is_fiat || last_is_hitag)) {
            int16_t target = -1;
            for(uint16_t i = 0; i < last; i++) {
                SubGhzHistoryItem* oi = subghz_history_at(instance, i);
                if(oi->serial != ls) continue;
                const char* on = subghz_history_name(instance, oi->proto_idx);
                if(last_is_fiat && strcmp(on, "Hitag-2") == 0) {
                    target = (int16_t)i;
                    break;
                }
                if(last_is_hitag && strcmp(on, "Fiat V1") == 0) {
                    target = (int16_t)last;
                    break;
                }
            }
            if(target >= 0) {
                subghz_history_remove_at(instance, (uint16_t)target);
                removed = target;
            }
        }
    }
    furi_mutex_release(instance->mutex);
    return removed;
}

int16_t subghz_history_dedup_last_came_holtek(SubGhzHistory* instance) {
    furi_assert(instance);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    int16_t removed = -1;
    if(instance->count >= 2) {
        uint16_t last = instance->count - 1;
        SubGhzHistoryItem* li = subghz_history_at(instance, last);
        const char* ln = subghz_history_name(instance, li->proto_idx);
        bool last_is_came = strcmp(ln, "CAME") == 0;
        bool last_is_holtek = strcmp(ln, "Holtek_HT12X") == 0;
        if(li->has_key && (last_is_came || last_is_holtek)) {
            int16_t target = -1;
            for(uint16_t i = 0; i < last; i++) {
                SubGhzHistoryItem* oi = subghz_history_at(instance, i);
                if(!oi->has_key) continue;
                if(memcmp(oi->key, li->key, sizeof(oi->key)) != 0) continue;
                const char* on = subghz_history_name(instance, oi->proto_idx);
                if(last_is_came && strcmp(on, "Holtek_HT12X") == 0) {
                    target = (int16_t)i;
                    break;
                }
                if(last_is_holtek && strcmp(on, "CAME") == 0) {
                    target = (int16_t)last;
                    break;
                }
            }
            if(target >= 0) {
                subghz_history_remove_at(instance, (uint16_t)target);
                removed = target;
            }
        }
    }
    furi_mutex_release(instance->mutex);
    return removed;
}

int16_t subghz_history_find_identical(
    SubGhzHistory* instance,
    void* context,
    SubGhzRadioPreset* preset,
    bool* out_notify) {
    furi_assert(instance);
    furi_assert(context);
    if(out_notify) *out_notify = false;
    if(memmgr_get_free_heap() < SUBGHZ_HISTORY_FREE_HEAP) return -1;

    SubGhzProtocolDecoderBase* decoder_base = context;

    FlipperFormat* ff = flipper_format_string_alloc();
    FuriString* in_proto = furi_string_alloc();
    uint8_t in_key[sizeof(uint64_t)] = {0};

    subghz_protocol_decoder_base_serialize(decoder_base, ff, preset);
    flipper_format_rewind(ff);
    bool have_proto = flipper_format_read_string(ff, "Protocol", in_proto);
    flipper_format_rewind(ff);
    bool have_key = flipper_format_read_hex(ff, "Key", in_key, sizeof(uint64_t));
    flipper_format_free(ff);

    int16_t found = -1;
    if(have_proto && have_key) {
        furi_mutex_acquire(instance->mutex, FuriWaitForever);
        for(uint16_t i = 0; i < instance->count; i++) {
            SubGhzHistoryItem* oi = subghz_history_at(instance, i);
            if(oi->type == 0) continue;
            if(!oi->has_key) continue;
            if(!furi_string_equal_str(in_proto, subghz_history_name(instance, oi->proto_idx)))
                continue;
            if(memcmp(oi->key, in_key, sizeof(uint64_t)) != 0) continue;
            found = (int16_t)i;
            uint32_t now = furi_get_tick();
            if(out_notify) {
                *out_notify = ((uint32_t)(now - oi->last_ping_tick) >= SUBGHZ_HISTORY_PING_NOTIFY_MS);
            }
            oi->last_ping_tick = now;
            break;
        }
        furi_mutex_release(instance->mutex);
    }
    furi_string_free(in_proto);
    return found;
}

static void subghz_history_build_label(
    SubGhzHistory* instance,
    SubGhzHistoryItem* item,
    FlipperFormat* ff,
    FuriString* label) {
    FuriString* text = furi_string_alloc();
    furi_string_reset(label);
    do {
        if(!flipper_format_rewind(ff)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(!flipper_format_read_string(ff, "Protocol", instance->tmp_string)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            break;
        }
        if(!strcmp(furi_string_get_cstr(instance->tmp_string), "PSA GROUP")) {
            FuriString* psa_type = furi_string_alloc();
            flipper_format_rewind(ff);
            bool psa_has_type = flipper_format_read_string(ff, "Type", psa_type) &&
                                !furi_string_equal_str(psa_type, "00");
            furi_string_free(psa_type);
            flipper_format_rewind(ff);
            if(!psa_has_type) furi_string_set(instance->tmp_string, "Unknown Signal");
        }
        if(item->type == SubGhzProtocolTypeTpms) {
            uint32_t id = 0;
            if(flipper_format_read_uint32(ff, "Id", &id, 1)) {
                furi_string_printf(
                    label, "%s %08lX", furi_string_get_cstr(instance->tmp_string), (unsigned long)id);
            } else {
                furi_string_set(label, instance->tmp_string);
            }
            break;
        }
        if(!strcmp(furi_string_get_cstr(instance->tmp_string), "KeeLoq")) {
            furi_string_set(instance->tmp_string, "KL ");
            if(!flipper_format_read_string(ff, "Manufacture", text)) {
                FURI_LOG_E(TAG, "Missing Protocol");
                break;
            }
            furi_string_cat(instance->tmp_string, text);
        } else if(!strcmp(furi_string_get_cstr(instance->tmp_string), "Star Line")) {
            furi_string_set(instance->tmp_string, "SL ");
            if(!flipper_format_read_string(ff, "Manufacture", text)) {
                FURI_LOG_E(TAG, "Missing Protocol");
                break;
            }
            furi_string_cat(instance->tmp_string, text);
        }
        if(!flipper_format_rewind(ff)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(ff, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_D(TAG, "No Key");
        }
        uint64_t data = 0;
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            data = (data << 8) | key_data[i];
        }
        if(data != 0) {
            if(!(uint32_t)(data >> 32)) {
                furi_string_printf(
                    label,
                    "%s %lX",
                    furi_string_get_cstr(instance->tmp_string),
                    (uint32_t)(data & 0xFFFFFFFF));
            } else {
                furi_string_printf(
                    label,
                    "%s %lX%08lX",
                    furi_string_get_cstr(instance->tmp_string),
                    (uint32_t)(data >> 32),
                    (uint32_t)(data & 0xFFFFFFFF));
            }
        } else {
            furi_string_printf(label, "%s", furi_string_get_cstr(instance->tmp_string));
        }
    } while(false);
    furi_string_free(text);
}

bool subghz_history_add_to_history(
    SubGhzHistory* instance,
    void* context,
    SubGhzRadioPreset* preset,
    SubGhzHistorySource source) {
    furi_assert(instance);
    furi_assert(context);

    if(memmgr_get_free_heap() < SUBGHZ_HISTORY_FREE_HEAP) return false;

    furi_mutex_acquire(instance->mutex, FuriWaitForever);

    if(instance->count >= SUBGHZ_HISTORY_MAX) {
        furi_mutex_release(instance->mutex);
        return false;
    }

    SubGhzProtocolDecoderBase* decoder_base = context;
    if((instance->code_last_hash_data ==
        subghz_protocol_decoder_base_get_hash_data(decoder_base)) &&
       ((furi_get_tick() - instance->last_update_timestamp) < 500)) {
        instance->last_update_timestamp = furi_get_tick();
        furi_mutex_release(instance->mutex);
        return false;
    }

    instance->code_last_hash_data = subghz_protocol_decoder_base_get_hash_data(decoder_base);
    instance->last_update_timestamp = furi_get_tick();

    SubGhzHistoryItem* item = subghz_history_push(instance);
    item->type = decoder_base->protocol->type;
    item->source = source;
    subghz_history_fill_common(instance, item, preset);

    item->ff = flipper_format_string_alloc();
    subghz_protocol_decoder_base_serialize(decoder_base, item->ff, preset);

    FuriString* label = furi_string_alloc();
    subghz_history_build_label(instance, item, item->ff, label);
    snprintf(item->label, sizeof(item->label), "%s", furi_string_get_cstr(label));
    furi_string_free(label);
    subghz_history_fill_match_fields(instance, item, item->ff);

    furi_mutex_release(instance->mutex);
    return true;
}

bool subghz_history_replace_item(
    SubGhzHistory* instance,
    uint16_t idx,
    FlipperFormat* ff,
    uint8_t type) {
    furi_assert(instance);
    furi_assert(ff);
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_at(instance, idx);
    if(!item) {
        furi_mutex_release(instance->mutex);
        return false;
    }
    if(item->ff) flipper_format_free(item->ff);
    item->ff = ff;
    item->on_sd = false;
    item->type = type;
    item->has_key = false;
    item->serial = 0;
    memset(item->key, 0, sizeof(item->key));
    if(instance->cache_id == item->file_id) instance->cache_id = 0;
    FuriString* label = furi_string_alloc();
    subghz_history_build_label(instance, item, item->ff, label);
    snprintf(item->label, sizeof(item->label), "%s", furi_string_get_cstr(label));
    furi_string_free(label);
    subghz_history_fill_match_fields(instance, item, item->ff);
    furi_mutex_release(instance->mutex);
    return true;
}

int16_t subghz_history_add_raw(
    SubGhzHistory* instance,
    FlipperFormat* raw,
    SubGhzRadioPreset* preset,
    const char* label) {
    furi_assert(instance);
    if(!raw) return -1;
    if(memmgr_get_free_heap() < SUBGHZ_HISTORY_FREE_HEAP) return -1;

    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    SubGhzHistoryItem* item = subghz_history_push(instance);
    if(!item) {
        furi_mutex_release(instance->mutex);
        return -1;
    }
    item->type = SubGhzProtocolTypeBinRAW;
    subghz_history_fill_common(instance, item, preset);
    snprintf(item->label, sizeof(item->label), "%s", label);
    item->ff = raw;
    subghz_history_fill_match_fields(instance, item, item->ff);

    int16_t idx = (int16_t)(instance->count - 1);
    furi_mutex_release(instance->mutex);
    return idx;
}
