#include "subghz_keystore.h"
#include "subghz_keystore_i.h"

#include <furi.h>
#include <furi_hal.h>

#include <storage/storage.h>
#include <toolbox/hex.h>
#include <toolbox/stream/stream.h>
#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>

#define TAG "SubGhzKeystore"

#define FILE_BUFFER_SIZE 64

#define SUBGHZ_KEYSTORE_FILE_TYPE     "Flipper SubGhz Keystore File"
#define SUBGHZ_KEYSTORE_FILE_RAW_TYPE "Flipper SubGhz Keystore RAW File"
#define SUBGHZ_KEYSTORE_FILE_VERSION  0

#define SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT 1
#define SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE 512
#define SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE (SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE * 2)

typedef enum {
    SubGhzKeystoreEncryptionNone,
    SubGhzKeystoreEncryptionAES256,
} SubGhzKeystoreEncryption;

typedef struct {
    FlipperFormat* flipper_format;
    Stream* stream;
    bool encrypted;
    bool stop;
    bool pending_reset;
    uint8_t buffer[FILE_BUFFER_SIZE];
    size_t buffer_len;
    size_t buffer_pos;
    char* line;
    char* decrypted;
    size_t cursor;
} SubGhzKeystoreReader;

struct SubGhzKeystoreIter {
    SubGhzKeystore* keystore;
    Storage* storage;
    size_t file_idx;
    size_t ram_idx;
    bool reader_open;
    SubGhzKeystoreReader reader;
    SubGhzKey current;
};

SubGhzKeystore* subghz_keystore_alloc(void) {
    SubGhzKeystore* instance = malloc(sizeof(SubGhzKeystore));
    memset(instance, 0, sizeof(SubGhzKeystore));

    SubGhzKeyArray_init(instance->data);
    SubGhzKeystoreNameArray_init(instance->names);
    instance->names_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    subghz_keystore_reset_kl(instance);

    return instance;
}

void subghz_keystore_reset_kl(SubGhzKeystore* instance) {
    furi_assert(instance);

    instance->mfname = "";
    instance->kl_type = 0;
}

void subghz_keystore_free(SubGhzKeystore* instance) {
    furi_assert(instance);

    for
        M_EACH(manufacture_code, instance->data, SubGhzKeyArray_t) {
            furi_string_free(manufacture_code->name);
            manufacture_code->key = 0;
        }
    SubGhzKeyArray_clear(instance->data);

    for(size_t i = 0; i < instance->file_count; i++) {
        furi_string_free(instance->files[i]);
    }
    size_t names = SubGhzKeystoreNameArray_size(instance->names);
    for(size_t i = 0; i < names; i++) {
        furi_string_free(*SubGhzKeystoreNameArray_get(instance->names, i));
    }
    SubGhzKeystoreNameArray_clear(instance->names);
    furi_mutex_free(instance->names_mutex);

    free(instance);
}

static bool subghz_keystore_parse_line(const char* line, FuriString* out_name, uint64_t* out_key, uint16_t* out_type) {
    uint16_t type = 0;
    char skey[17] = {0};
    char name[65] = {0};
    int ret = sscanf(line, "%16s:%hu:%64s", skey, &type, name);
    uint64_t key = strtoull(skey, NULL, 16);
    if(ret == 3) {
        furi_string_set_str(out_name, name);
        *out_key = key;
        *out_type = type;
        return true;
    } else {
        FURI_LOG_E(TAG, "Failed to load line: %s\r\n", line);
        return false;
    }
}

static void subghz_keystore_mess_with_iv(uint8_t* iv) {

    furi_assert(((uint32_t)iv) % 4 == 0);

    asm volatile("nani%=:                  \n"
                 "ldrd  r0, r2, [%0, #0x0] \n"
                 "lsl   r1, r0, #8         \n"
                 "lsl   r3, r2, #8         \n"
                 "orr   r3, r3, r0, lsr #24\n"
                 "uadd8 r1, r1, r0         \n"
                 "uadd8 r3, r3, r2         \n"
                 "strd  r1, r3, [%0, #0x0] \n"
                 "ldrd  r1, r3, [%0, #0x8] \n"
                 "lsl   r0, r1, #8         \n"
                 "orr   r0, r0, r2, lsr #24\n"
                 "lsl   r2, r3, #8         \n"
                 "orr   r2, r2, r1, lsr #24\n"
                 "uadd8 r1, r1, r0         \n"
                 "uadd8 r3, r3, r2         \n"
                 "strd  r1, r3, [%0, #0x8] \n"
                 :
                 : "r"(iv)
                 : "r0", "r1", "r2", "r3", "memory");
}

static bool subghz_keystore_reader_open(
    SubGhzKeystoreReader* reader,
    Storage* storage,
    const char* file_name) {
    memset(reader, 0, sizeof(SubGhzKeystoreReader));
    uint8_t iv[16];
    uint32_t version;
    uint32_t encryption;
    bool opened = false;
    FuriString* filetype = furi_string_alloc();

    reader->flipper_format = flipper_format_buffered_file_alloc(storage);
    do {
        if(!flipper_format_buffered_file_open_existing(reader->flipper_format, file_name)) {
            FURI_LOG_D(TAG, "Unable to open file for read: %s", file_name);
            break;
        }
        if(!flipper_format_read_header(reader->flipper_format, filetype, &version)) {
            FURI_LOG_E(TAG, "Missing or incorrect header");
            break;
        }
        if(!flipper_format_read_uint32(
               reader->flipper_format, "Encryption", (uint32_t*)&encryption, 1)) {
            FURI_LOG_E(TAG, "Missing encryption type");
            break;
        }
        if(strcmp(furi_string_get_cstr(filetype), SUBGHZ_KEYSTORE_FILE_TYPE) != 0 ||
           version != SUBGHZ_KEYSTORE_FILE_VERSION) {
            FURI_LOG_E(TAG, "Type or version mismatch");
            break;
        }
        if(encryption == SubGhzKeystoreEncryptionNone) {
            reader->encrypted = false;
        } else if(encryption == SubGhzKeystoreEncryptionAES256) {
            if(!flipper_format_read_hex(reader->flipper_format, "IV", iv, 16)) {
                FURI_LOG_E(TAG, "Missing IV");
                break;
            }
            subghz_keystore_mess_with_iv(iv);
            if(!furi_hal_crypto_enclave_load_key(SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT, iv)) {
                FURI_LOG_E(TAG, "Unable to load decryption key");
                break;
            }
            reader->encrypted = true;
        } else {
            FURI_LOG_E(TAG, "Unknown encryption");
            break;
        }
        reader->stream = flipper_format_get_raw_stream(reader->flipper_format);
        reader->line = malloc(SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE + 1);
        memset(reader->line, 0, SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE + 1);
        if(reader->encrypted) {
            reader->decrypted = malloc(SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE + 1);
            memset(reader->decrypted, 0, SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE + 1);
        }
        opened = true;
    } while(false);

    furi_string_free(filetype);
    if(!opened) {
        flipper_format_free(reader->flipper_format);
        reader->flipper_format = NULL;
    }
    return opened;
}

static void subghz_keystore_reader_close(SubGhzKeystoreReader* reader) {
    if(reader->encrypted) {
        furi_hal_crypto_enclave_unload_key(SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT);
        reader->encrypted = false;
    }
    if(reader->line) free(reader->line);
    if(reader->decrypted) free(reader->decrypted);
    if(reader->flipper_format) flipper_format_free(reader->flipper_format);
    memset(reader, 0, sizeof(SubGhzKeystoreReader));
}

static const char* subghz_keystore_reader_next(SubGhzKeystoreReader* reader) {
    if(reader->pending_reset) {
        memset(reader->line, 0, SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE + 1);
        if(reader->decrypted) memset(reader->decrypted, 0, SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE + 1);
        reader->cursor = 0;
        reader->pending_reset = false;
    }
    while(!reader->stop) {
        if(reader->buffer_pos >= reader->buffer_len) {
            reader->buffer_len = stream_read(reader->stream, reader->buffer, FILE_BUFFER_SIZE);
            reader->buffer_pos = 0;
            if(reader->buffer_len == 0) {
                reader->stop = true;
                break;
            }
        }
        uint8_t c = reader->buffer[reader->buffer_pos++];
        if(c == '\n' && reader->cursor > 0) {
            reader->pending_reset = true;
            if(!reader->encrypted) return reader->line;
            size_t len = strlen(reader->line);
            if(len % 32 == 0) {
                for(size_t i = 0; i < len; i += 2) {
                    uint8_t hi_nibble = 0;
                    uint8_t lo_nibble = 0;
                    hex_char_to_hex_nibble(reader->line[i], &hi_nibble);
                    hex_char_to_hex_nibble(reader->line[i + 1], &lo_nibble);
                    reader->line[i / 2] = (hi_nibble << 4) | lo_nibble;
                }
                len /= 2;
                if(furi_hal_crypto_decrypt(
                       (uint8_t*)reader->line, (uint8_t*)reader->decrypted, len)) {
                    return reader->decrypted;
                }
                FURI_LOG_E(TAG, "Decryption failed");
                reader->stop = true;
                break;
            }
            FURI_LOG_E(TAG, "Invalid encrypted data: %s", reader->line);
            memset(reader->line, 0, SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE + 1);
            memset(reader->decrypted, 0, SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE + 1);
            reader->cursor = 0;
            reader->pending_reset = false;
        } else if(c == '\r' || c == '\n') {
        } else if(reader->cursor < SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE) {
            reader->line[reader->cursor++] = (char)c;
        } else {
            FURI_LOG_E(TAG, "Malformed file");
            reader->stop = true;
        }
    }
    return NULL;
}

bool subghz_keystore_load(SubGhzKeystore* instance, const char* file_name) {
    furi_assert(instance);
    FURI_LOG_I(TAG, "Loading keystore %s", file_name);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    SubGhzKeystoreReader* reader = malloc(sizeof(SubGhzKeystoreReader));
    bool result = subghz_keystore_reader_open(reader, storage, file_name);
    if(result) {
        FuriString* name = furi_string_alloc();
        uint64_t key = 0;
        uint16_t type = 0;
        const char* line;
        while((line = subghz_keystore_reader_next(reader)) != NULL) {
            if(subghz_keystore_parse_line(line, name, &key, &type)) {
                SubGhzKey* manufacture_code = SubGhzKeyArray_push_raw(instance->data);
                manufacture_code->name = furi_string_alloc_set(name);
                manufacture_code->key = key;
                manufacture_code->type = type;
            }
        }
        furi_string_free(name);
        result = reader->buffer_len == 0;
        subghz_keystore_reader_close(reader);
    }
    free(reader);
    furi_record_close(RECORD_STORAGE);
    return result;
}

bool subghz_keystore_attach_file(SubGhzKeystore* instance, const char* file_name) {
    furi_assert(instance);
    furi_assert(file_name);
    if(instance->file_count >= SUBGHZ_KEYSTORE_MAX_FILES) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    SubGhzKeystoreReader* reader = malloc(sizeof(SubGhzKeystoreReader));
    bool result = subghz_keystore_reader_open(reader, storage, file_name);
    if(result) subghz_keystore_reader_close(reader);
    instance->files[instance->file_count++] = furi_string_alloc_set_str(file_name);
    free(reader);
    furi_record_close(RECORD_STORAGE);
    FURI_LOG_I(TAG, "Attach keystore %s: %s", file_name, result ? "ok" : "failed");
    return result;
}

SubGhzKeystoreIter* subghz_keystore_iter_alloc(SubGhzKeystore* instance) {
    furi_assert(instance);
    SubGhzKeystoreIter* iter = malloc(sizeof(SubGhzKeystoreIter));
    memset(iter, 0, sizeof(SubGhzKeystoreIter));
    iter->keystore = instance;
    iter->current.name = furi_string_alloc();
    if(instance->file_count) iter->storage = furi_record_open(RECORD_STORAGE);
    return iter;
}

const SubGhzKey* subghz_keystore_iter_next(SubGhzKeystoreIter* iter) {
    furi_assert(iter);
    SubGhzKeystore* instance = iter->keystore;
    for(;;) {
        if(iter->reader_open) {
            const char* line = subghz_keystore_reader_next(&iter->reader);
            if(line) {
                if(subghz_keystore_parse_line(
                       line, iter->current.name, &iter->current.key, &iter->current.type)) {
                    return &iter->current;
                }
                continue;
            }
            subghz_keystore_reader_close(&iter->reader);
            iter->reader_open = false;
            continue;
        }
        if(iter->file_idx < instance->file_count) {
            const char* path = furi_string_get_cstr(instance->files[iter->file_idx++]);
            iter->reader_open = subghz_keystore_reader_open(&iter->reader, iter->storage, path);
            continue;
        }
        if(iter->ram_idx < SubGhzKeyArray_size(instance->data)) {
            const SubGhzKey* entry = SubGhzKeyArray_cget(instance->data, iter->ram_idx++);
            furi_string_set(iter->current.name, entry->name);
            iter->current.key = entry->key;
            iter->current.type = entry->type;
            return &iter->current;
        }
        return NULL;
    }
}

void subghz_keystore_iter_free(SubGhzKeystoreIter* iter) {
    furi_assert(iter);
    if(iter->reader_open) subghz_keystore_reader_close(&iter->reader);
    if(iter->storage) furi_record_close(RECORD_STORAGE);
    furi_string_free(iter->current.name);
    free(iter);
}

const char* subghz_keystore_keep_name(SubGhzKeystore* instance, const char* name) {
    furi_assert(instance);
    furi_assert(name);
    furi_mutex_acquire(instance->names_mutex, FuriWaitForever);
    const char* kept = NULL;
    size_t size = SubGhzKeystoreNameArray_size(instance->names);
    for(size_t i = 0; i < size; i++) {
        FuriString* existing = *SubGhzKeystoreNameArray_get(instance->names, i);
        if(furi_string_equal_str(existing, name)) {
            kept = furi_string_get_cstr(existing);
            break;
        }
    }
    if(!kept) {
        FuriString* added = furi_string_alloc_set_str(name);
        SubGhzKeystoreNameArray_push_back(instance->names, added);
        kept = furi_string_get_cstr(added);
    }
    furi_mutex_release(instance->names_mutex);
    return kept;
}

bool subghz_keystore_save(SubGhzKeystore* instance, const char* file_name, uint8_t* iv) {
    furi_assert(instance);
    bool result = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    char* decrypted_line = malloc(SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE);
    char* encrypted_line = malloc(SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE);

    FlipperFormat* flipper_format = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_always(flipper_format, file_name)) {
            FURI_LOG_E(TAG, "Unable to open file for write: %s", file_name);
            break;
        }
        if(!flipper_format_write_header_cstr(
               flipper_format, SUBGHZ_KEYSTORE_FILE_TYPE, SUBGHZ_KEYSTORE_FILE_VERSION)) {
            FURI_LOG_E(TAG, "Unable to add header");
            break;
        }
        uint32_t encryption = SubGhzKeystoreEncryptionAES256;
        if(!flipper_format_write_uint32(flipper_format, "Encryption", &encryption, 1)) {
            FURI_LOG_E(TAG, "Unable to add Encryption");
            break;
        }
        if(!flipper_format_write_hex(flipper_format, "IV", iv, 16)) {
            FURI_LOG_E(TAG, "Unable to add IV");
            break;
        }

        subghz_keystore_mess_with_iv(iv);

        if(!furi_hal_crypto_enclave_load_key(SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT, iv)) {
            FURI_LOG_E(TAG, "Unable to load encryption key");
            break;
        }

        Stream* stream = flipper_format_get_raw_stream(flipper_format);
        size_t encrypted_line_count = 0;
        for
            M_EACH(key, instance->data, SubGhzKeyArray_t) {

                memset(decrypted_line, 0, SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE);
                memset(encrypted_line, 0, SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE);

                int len = snprintf(
                    decrypted_line,
                    SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE,
                    "%08lX%08lX:%hu:%s",
                    (uint32_t)(key->key >> 32),
                    (uint32_t)key->key,
                    key->type,
                    furi_string_get_cstr(key->name));

                furi_assert(len > 0);
                if(len % 16 != 0) {
                    len += (16 - len % 16);
                }
                furi_assert(len % 16 == 0);
                furi_assert(len <= SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE);

                if(!furi_hal_crypto_encrypt(
                       (uint8_t*)decrypted_line, (uint8_t*)encrypted_line, len)) {
                    FURI_LOG_E(TAG, "Encryption failed");
                    break;
                }

                const char xx[] = "0123456789ABCDEF";
                for(int i = 0; i < len; i++) {
                    size_t cursor = len - i - 1;
                    size_t hex_cursor = len * 2 - i * 2 - 1;
                    encrypted_line[hex_cursor] = xx[encrypted_line[cursor] & 0xF];
                    encrypted_line[hex_cursor - 1] = xx[(encrypted_line[cursor] >> 4) & 0xF];
                }
                stream_write_cstring(stream, encrypted_line);
                stream_write_char(stream, '\n');
                encrypted_line_count++;
            }
        furi_hal_crypto_enclave_unload_key(SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT);
        size_t total_keys = SubGhzKeyArray_size(instance->data);
        result = encrypted_line_count == total_keys;
        if(result) {
            FURI_LOG_I(TAG, "Success. Encrypted: %zu of %zu", encrypted_line_count, total_keys);
        } else {
            FURI_LOG_E(TAG, "Failure. Encrypted: %zu of %zu", encrypted_line_count, total_keys);
        }
    } while(0);
    flipper_format_free(flipper_format);

    free(encrypted_line);
    free(decrypted_line);
    furi_record_close(RECORD_STORAGE);

    return result;
}

SubGhzKeyArray_t* subghz_keystore_get_data(SubGhzKeystore* instance) {
    furi_assert(instance);
    return &instance->data;
}

bool subghz_keystore_raw_encrypted_save(
    const char* input_file_name,
    const char* output_file_name,
    uint8_t* iv) {
    bool encrypted = false;
    uint32_t version;
    uint32_t encryption;
    FuriString* filetype;
    filetype = furi_string_alloc();

    Storage* storage = furi_record_open(RECORD_STORAGE);

    char* encrypted_line = malloc(SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE);

    FlipperFormat* input_flipper_format = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_existing(input_flipper_format, input_file_name)) {
            FURI_LOG_E(TAG, "Unable to open file for read: %s", input_file_name);
            break;
        }
        if(!flipper_format_read_header(input_flipper_format, filetype, &version)) {
            FURI_LOG_E(TAG, "Missing or incorrect header");
            break;
        }
        if(!flipper_format_read_uint32(
               input_flipper_format, "Encryption", (uint32_t*)&encryption, 1)) {
            FURI_LOG_E(TAG, "Missing encryption type");
            break;
        }

        if(strcmp(furi_string_get_cstr(filetype), SUBGHZ_KEYSTORE_FILE_RAW_TYPE) != 0 ||
           version != SUBGHZ_KEYSTORE_FILE_VERSION) {
            FURI_LOG_E(TAG, "Type or version mismatch");
            break;
        }

        if(encryption != SubGhzKeystoreEncryptionNone) {
            FURI_LOG_E(TAG, "Already encryption");
            break;
        }
        Stream* input_stream = flipper_format_get_raw_stream(input_flipper_format);

        FlipperFormat* output_flipper_format = flipper_format_file_alloc(storage);

        if(!flipper_format_file_open_always(output_flipper_format, output_file_name)) {
            FURI_LOG_E(TAG, "Unable to open file for write: %s", output_file_name);
            break;
        }
        if(!flipper_format_write_header_cstr(
               output_flipper_format,
               furi_string_get_cstr(filetype),
               SUBGHZ_KEYSTORE_FILE_VERSION)) {
            FURI_LOG_E(TAG, "Unable to add header");
            break;
        }
        uint32_t encryption = SubGhzKeystoreEncryptionAES256;
        if(!flipper_format_write_uint32(output_flipper_format, "Encryption", &encryption, 1)) {
            FURI_LOG_E(TAG, "Unable to add Encryption");
            break;
        }
        if(!flipper_format_write_hex(output_flipper_format, "IV", iv, 16)) {
            FURI_LOG_E(TAG, "Unable to add IV");
            break;
        }

        if(!flipper_format_write_string_cstr(output_flipper_format, "Encrypt_data", "RAW")) {
            FURI_LOG_E(TAG, "Unable to add Encrypt_data");
            break;
        }

        subghz_keystore_mess_with_iv(iv);

        if(!furi_hal_crypto_enclave_load_key(SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT, iv)) {
            FURI_LOG_E(TAG, "Unable to load encryption key");
            break;
        }

        Stream* output_stream = flipper_format_get_raw_stream(output_flipper_format);
        uint8_t buffer[FILE_BUFFER_SIZE];
        bool result = true;

        size_t ret = 0;
        furi_assert(FILE_BUFFER_SIZE % 16 == 0);

        stream_read(input_stream, buffer, 1);

        do {
            memset(buffer, 0, FILE_BUFFER_SIZE);
            ret = stream_read(input_stream, buffer, FILE_BUFFER_SIZE);
            if(ret == 0) {
                break;
            }

            for(uint16_t i = 0; i < FILE_BUFFER_SIZE - 1; i += 2) {
                uint8_t hi_nibble = 0;
                uint8_t lo_nibble = 0;
                hex_char_to_hex_nibble(buffer[i], &hi_nibble);
                hex_char_to_hex_nibble(buffer[i + 1], &lo_nibble);
                buffer[i / 2] = (hi_nibble << 4) | lo_nibble;
            }

            memset(encrypted_line, 0, SUBGHZ_KEYSTORE_FILE_ENCRYPTED_LINE_SIZE);

            if(!furi_hal_crypto_encrypt(
                   (uint8_t*)buffer, (uint8_t*)encrypted_line, FILE_BUFFER_SIZE / 2)) {
                FURI_LOG_E(TAG, "Encryption failed");
                result = false;
                break;
            }

            const char xx[] = "0123456789ABCDEF";
            for(size_t i = 0; i < FILE_BUFFER_SIZE / 2; i++) {
                size_t cursor = FILE_BUFFER_SIZE / 2 - i - 1;
                size_t hex_cursor = FILE_BUFFER_SIZE - i * 2 - 1;
                encrypted_line[hex_cursor] = xx[encrypted_line[cursor] & 0xF];
                encrypted_line[hex_cursor - 1] = xx[(encrypted_line[cursor] >> 4) & 0xF];
            }
            stream_write_cstring(output_stream, encrypted_line);

        } while(true);

        flipper_format_free(output_flipper_format);

        furi_hal_crypto_enclave_unload_key(SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT);

        if(!result) break;

        encrypted = true;
    } while(0);

    flipper_format_free(input_flipper_format);

    free(encrypted_line);

    furi_record_close(RECORD_STORAGE);

    return encrypted;
}

bool subghz_keystore_raw_get_data(const char* file_name, size_t offset, uint8_t* data, size_t len) {
    bool result = false;
    uint8_t iv[16];
    uint32_t version;
    uint32_t encryption;

    FuriString* str_temp;
    str_temp = furi_string_alloc();

    Storage* storage = furi_record_open(RECORD_STORAGE);
    char* decrypted_line = malloc(SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE);

    FlipperFormat* flipper_format = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_existing(flipper_format, file_name)) {
            FURI_LOG_E(TAG, "Unable to open file for read: %s", file_name);
            break;
        }
        if(!flipper_format_read_header(flipper_format, str_temp, &version)) {
            FURI_LOG_E(TAG, "Missing or incorrect header");
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "Encryption", (uint32_t*)&encryption, 1)) {
            FURI_LOG_E(TAG, "Missing encryption type");
            break;
        }

        if(strcmp(furi_string_get_cstr(str_temp), SUBGHZ_KEYSTORE_FILE_RAW_TYPE) != 0 ||
           version != SUBGHZ_KEYSTORE_FILE_VERSION) {
            FURI_LOG_E(TAG, "Type or version mismatch");
            break;
        }

        Stream* stream = flipper_format_get_raw_stream(flipper_format);
        if(encryption != SubGhzKeystoreEncryptionAES256) {
            FURI_LOG_E(TAG, "Unknown encryption");
            break;
        }

        if(offset < 16) {
            if(!flipper_format_read_hex(flipper_format, "IV", iv, 16)) {
                FURI_LOG_E(TAG, "Missing IV");
                break;
            }
            subghz_keystore_mess_with_iv(iv);
        }

        if(!flipper_format_read_string(flipper_format, "Encrypt_data", str_temp)) {
            FURI_LOG_E(TAG, "Missing Encrypt_data");
            break;
        }

        size_t bufer_size;
        if(len <= (16 - offset % 16)) {
            bufer_size = 32;
        } else {
            bufer_size = (((len) / 16) + 2) * 32;
        }
        furi_assert(SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE >= bufer_size / 2);

        uint8_t buffer[bufer_size];
        size_t ret = 0;
        bool decrypted = true;

        stream_read(stream, buffer, 1);

        size_t size = stream_size(stream);
        size -= stream_tell(stream);
        if(size < (offset * 2 + len * 2)) {
            FURI_LOG_E(TAG, "Seek position exceeds file size");
            break;
        }

        if(offset >= 16) {
            stream_seek(stream, ((offset / 16) - 1) * 32, StreamOffsetFromCurrent);
            ret = stream_read(stream, buffer, 32);
            furi_assert(ret == 32);
            for(uint16_t i = 0; i < ret - 1; i += 2) {
                uint8_t hi_nibble = 0;
                uint8_t lo_nibble = 0;
                hex_char_to_hex_nibble(buffer[i], &hi_nibble);
                hex_char_to_hex_nibble(buffer[i + 1], &lo_nibble);
                iv[i / 2] = (hi_nibble << 4) | lo_nibble;
            }
        }

        if(!furi_hal_crypto_enclave_load_key(SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT, iv)) {
            FURI_LOG_E(TAG, "Unable to load encryption key");
            break;
        }

        do {
            memset(buffer, 0, bufer_size);
            ret = stream_read(stream, buffer, bufer_size);
            furi_assert(ret == bufer_size);
            for(uint16_t i = 0; i < ret - 1; i += 2) {
                uint8_t hi_nibble = 0;
                uint8_t lo_nibble = 0;
                hex_char_to_hex_nibble(buffer[i], &hi_nibble);
                hex_char_to_hex_nibble(buffer[i + 1], &lo_nibble);
                buffer[i / 2] = (hi_nibble << 4) | lo_nibble;
            }

            memset(decrypted_line, 0, SUBGHZ_KEYSTORE_FILE_DECRYPTED_LINE_SIZE);

            if(!furi_hal_crypto_decrypt(
                   (uint8_t*)buffer, (uint8_t*)decrypted_line, bufer_size / 2)) {
                decrypted = false;
                FURI_LOG_E(TAG, "Decryption failed");
                break;
            }
            memcpy(data, (uint8_t*)decrypted_line + (offset - (offset / 16) * 16), len);

        } while(0);
        furi_hal_crypto_enclave_unload_key(SUBGHZ_KEYSTORE_FILE_ENCRYPTION_KEY_SLOT);
        if(decrypted) result = true;
    } while(0);
    flipper_format_free(flipper_format);

    furi_record_close(RECORD_STORAGE);

    free(decrypted_line);

    furi_string_free(str_temp);

    return result;
}
