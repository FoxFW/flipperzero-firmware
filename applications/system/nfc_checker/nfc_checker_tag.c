#include "nfc_checker_tag.h"

#include <stdio.h>
#include <string.h>

#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>

NfcCheckerTag* nfc_checker_tag_alloc(void) {
    NfcCheckerTag* tag = malloc(sizeof(NfcCheckerTag));
    furi_check(tag);
    nfc_checker_tag_reset(tag);
    return tag;
}

void nfc_checker_tag_free(NfcCheckerTag* tag) {
    free(tag);
}

void nfc_checker_tag_reset(NfcCheckerTag* tag) {
    memset(tag, 0, sizeof(NfcCheckerTag));
    tag->protocol = NfcProtocolInvalid;
    tag->memory.kind = NfcCheckerMemoryNone;
    strncpy(tag->protocol_name, "Unknown", sizeof(tag->protocol_name) - 1);
    strncpy(tag->name, "Unknown", sizeof(tag->name) - 1);
}

const char* nfc_checker_tag_protocol_name(const NfcCheckerTag* tag) {
    /* nfc_device_get_protocol_name() asserts on NfcProtocolInvalid. */
    if(tag->protocol == NfcProtocolInvalid) {
        return (tag->protocol_name[0] != '\0') ? tag->protocol_name : "Unknown";
    }

    const char* name = nfc_device_get_protocol_name(tag->protocol);
    return name ? name : "Unknown";
}

static void nfc_checker_tag_copy_iso14443_3a(NfcCheckerTag* tag, const Iso14443_3aData* base) {
    tag->has_atqa_sak = true;
    tag->atqa[0] = base->atqa[0];
    tag->atqa[1] = base->atqa[1];
    tag->sak = base->sak;

    if(!tag->has_uid && base->uid_len <= NFC_CHECKER_UID_MAX_SIZE) {
        memcpy(tag->uid, base->uid, base->uid_len);
        tag->uid_len = base->uid_len;
        tag->has_uid = base->uid_len > 0;
    }
}

static void nfc_checker_tag_copy_classic(NfcCheckerTag* tag, const MfClassicData* data) {
    NfcCheckerMemory* memory = &tag->memory;

    memory->kind = NfcCheckerMemoryClassic;
    memory->classic_type = (uint8_t)data->type;
    memory->sector_count = mf_classic_get_total_sectors_num(data->type);
    memory->block_count = mf_classic_get_total_block_num(data->type);
    if(memory->sector_count > NFC_CHECKER_CLASSIC_MAX_SECTORS) {
        memory->sector_count = NFC_CHECKER_CLASSIC_MAX_SECTORS;
    }
    if(memory->block_count > NFC_CHECKER_CLASSIC_MAX_BLOCKS) {
        memory->block_count = NFC_CHECKER_CLASSIC_MAX_BLOCKS;
    }

    memcpy(memory->blocks, data->block, sizeof(memory->blocks));
    memcpy(memory->block_read_mask, data->block_read_mask, sizeof(memory->block_read_mask));
    memory->key_a_mask = data->key_a_mask;
    memory->key_b_mask = data->key_b_mask;

    for(uint8_t sector = 0; sector < memory->sector_count; sector++) {
        if(mf_classic_is_key_found(data, sector, MfClassicKeyTypeA)) {
            MfClassicKey key = mf_classic_get_key(data, sector, MfClassicKeyTypeA);
            memcpy(memory->key_a[sector], key.data, NFC_CHECKER_CLASSIC_KEY_SIZE);
        }
        if(mf_classic_is_key_found(data, sector, MfClassicKeyTypeB)) {
            MfClassicKey key = mf_classic_get_key(data, sector, MfClassicKeyTypeB);
            memcpy(memory->key_b[sector], key.data, NFC_CHECKER_CLASSIC_KEY_SIZE);
        }
    }
}

static void nfc_checker_tag_copy_ultralight(NfcCheckerTag* tag, const MfUltralightData* data) {
    NfcCheckerMemory* memory = &tag->memory;

    memory->kind = NfcCheckerMemoryUltralight;
    memory->pages_total = data->pages_total ?
                              data->pages_total :
                              mf_ultralight_get_pages_total(data->type);
    memory->pages_read = data->pages_read;

    uint16_t pages = memory->pages_total;
    if(pages > NFC_CHECKER_ULTRALIGHT_MAX_PAGES) {
        pages = NFC_CHECKER_ULTRALIGHT_MAX_PAGES;
    }
    if(pages > memory->pages_read) {
        pages = memory->pages_read;
    }

    for(uint16_t page = 0; page < pages; page++) {
        memcpy(memory->pages[page], data->page[page].data, NFC_CHECKER_ULTRALIGHT_PAGE_SIZE);
    }
}

bool nfc_checker_tag_from_device(NfcCheckerTag* tag, const NfcDevice* device) {
    nfc_checker_tag_reset(tag);
    if(device == NULL) {
        return false;
    }

    tag->protocol = nfc_device_get_protocol(device);
    if(tag->protocol == NfcProtocolInvalid) {
        /* Empty device: nfc_device_get_uid()/get_name() would assert. */
        return false;
    }

    snprintf(
        tag->protocol_name,
        sizeof(tag->protocol_name),
        "%s",
        nfc_checker_tag_protocol_name(tag));

    const char* name = nfc_device_get_name(device, NfcDeviceNameTypeFull);
    if(name) {
        snprintf(tag->name, sizeof(tag->name), "%s", name);
    }

    size_t uid_len = 0;
    const uint8_t* uid = nfc_device_get_uid(device, &uid_len);
    if(uid && uid_len > 0 && uid_len <= NFC_CHECKER_UID_MAX_SIZE) {
        memcpy(tag->uid, uid, uid_len);
        tag->uid_len = (uint8_t)uid_len;
        tag->has_uid = true;
    }

    /* Base data is what a clone has to reproduce during anti-collision. It is
     * available for every ISO14443-3A derived protocol (Classic, Ultralight,
     * Plus, DESFire, 4A, ...). */
    const Iso14443_3aData* base = nfc_device_get_data(device, NfcProtocolIso14443_3a);

    if(tag->protocol == NfcProtocolMfClassic) {
        const MfClassicData* classic = nfc_device_get_data(device, NfcProtocolMfClassic);
        if(classic) {
            if(base == NULL) {
                base = mf_classic_get_base_data(classic);
            }
            nfc_checker_tag_copy_classic(tag, classic);
        }
    } else if(tag->protocol == NfcProtocolMfUltralight) {
        const MfUltralightData* ultralight =
            nfc_device_get_data(device, NfcProtocolMfUltralight);
        if(ultralight) {
            if(base == NULL) {
                base = mf_ultralight_get_base_data(ultralight);
            }
            nfc_checker_tag_copy_ultralight(tag, ultralight);
        }
    }

    if(base) {
        nfc_checker_tag_copy_iso14443_3a(tag, base);
    }

    return tag->has_uid || tag->memory.kind != NfcCheckerMemoryNone;
}

bool nfc_checker_tag_uid_equal(const NfcCheckerTag* left, const NfcCheckerTag* right) {
    if(!left->has_uid || !right->has_uid) {
        return false;
    }
    if(left->uid_len != right->uid_len) {
        return false;
    }
    return memcmp(left->uid, right->uid, left->uid_len) == 0;
}

void nfc_checker_tag_uid_to_string(const NfcCheckerTag* tag, char* out, size_t out_size) {
    if(out_size == 0) {
        return;
    }
    out[0] = '\0';
    if(!tag->has_uid) {
        snprintf(out, out_size, "unknown");
        return;
    }

    size_t position = 0;
    for(uint8_t i = 0; i < tag->uid_len && position + 3 <= out_size; i++) {
        position += (size_t)snprintf(
            out + position, out_size - position, "%02X", tag->uid[i]);
    }
}

bool nfc_checker_tag_block_read(const NfcCheckerTag* tag, uint16_t block) {
    if(tag->memory.kind != NfcCheckerMemoryClassic) {
        return false;
    }
    if(block >= tag->memory.block_count || block >= NFC_CHECKER_CLASSIC_MAX_BLOCKS) {
        return false;
    }
    return (tag->memory.block_read_mask[block / 32] & (1UL << (block % 32))) != 0;
}

bool nfc_checker_tag_key_known(const NfcCheckerTag* tag, uint8_t sector, bool key_b) {
    if(tag->memory.kind != NfcCheckerMemoryClassic || sector >= NFC_CHECKER_CLASSIC_MAX_SECTORS) {
        return false;
    }
    uint64_t mask = key_b ? tag->memory.key_b_mask : tag->memory.key_a_mask;
    return (mask & (1ULL << sector)) != 0;
}

const uint8_t* nfc_checker_tag_key(const NfcCheckerTag* tag, uint8_t sector, bool key_b) {
    if(!nfc_checker_tag_key_known(tag, sector, key_b)) {
        return NULL;
    }
    return key_b ? tag->memory.key_b[sector] : tag->memory.key_a[sector];
}

bool nfc_checker_tag_has_classic(const NfcCheckerTag* tag) {
    return tag->memory.kind == NfcCheckerMemoryClassic;
}

bool nfc_checker_tag_has_ultralight(const NfcCheckerTag* tag) {
    return tag->memory.kind == NfcCheckerMemoryUltralight;
}
