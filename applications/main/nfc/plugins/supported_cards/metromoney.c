
#include "nfc_supported_card_plugin.h"
#include <flipper_application.h>

#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>

#include <bit_lib.h>

#define TAG "Metromoney"

typedef struct {
    uint64_t a;
    uint64_t b;
} MfClassicKeyPair;

static const MfClassicKeyPair metromoney_1k_keys[] = {
    {.a = 0x2803BCB0C7E1, .b = 0x4FA9EB49F75E},
    {.a = 0x9C616585E26D, .b = 0xD1C71E590D16},
    {.a = 0x9C616585E26D, .b = 0xA160FCD5EC4C},
    {.a = 0x9C616585E26D, .b = 0xA160FCD5EC4C},
    {.a = 0x9C616585E26D, .b = 0xA160FCD5EC4C},
    {.a = 0x9C616585E26D, .b = 0xA160FCD5EC4C},
    {.a = 0xFFFFFFFFFFFF, .b = 0xFFFFFFFFFFFF},
    {.a = 0xFFFFFFFFFFFF, .b = 0xFFFFFFFFFFFF},
    {.a = 0x112233445566, .b = 0x361A62F35BC9},
    {.a = 0x112233445566, .b = 0x361A62F35BC9},
    {.a = 0xFFFFFFFFFFFF, .b = 0xFFFFFFFFFFFF},
    {.a = 0xFFFFFFFFFFFF, .b = 0xFFFFFFFFFFFF},
    {.a = 0xFFFFFFFFFFFF, .b = 0xFFFFFFFFFFFF},
    {.a = 0xFFFFFFFFFFFF, .b = 0xFFFFFFFFFFFF},
    {.a = 0xFFFFFFFFFFFF, .b = 0xFFFFFFFFFFFF},
    {.a = 0xFFFFFFFFFFFF, .b = 0xFFFFFFFFFFFF},
};

static bool metromoney_verify(Nfc* nfc) {
    bool verified = false;

    do {
        const uint8_t ticket_sector_number = 1;
        const uint8_t ticket_block_number =
            mf_classic_get_first_block_num_of_sector(ticket_sector_number) + 1;
        FURI_LOG_D(TAG, "Verifying sector %u", ticket_sector_number);

        MfClassicKey key = {0};
        bit_lib_num_to_bytes_be(
            metromoney_1k_keys[ticket_sector_number].a, COUNT_OF(key.data), key.data);

        MfClassicAuthContext auth_context;
        MfClassicError error = mf_classic_poller_sync_auth(
            nfc, ticket_block_number, &key, MfClassicKeyTypeA, &auth_context);
        if(error != MfClassicErrorNone) {
            FURI_LOG_D(TAG, "Failed to read block %u: %d", ticket_block_number, error);
            break;
        }

        verified = true;
    } while(false);

    return verified;
}

static bool metromoney_read(Nfc* nfc, NfcDevice* device) {
    furi_assert(nfc);
    furi_assert(device);

    bool is_read = false;

    MfClassicData* data = mf_classic_alloc();
    nfc_device_copy_data(device, NfcProtocolMfClassic, data);

    do {
        MfClassicType type = MfClassicTypeMini;
        MfClassicError error = mf_classic_poller_sync_detect_type(nfc, &type);
        if(error != MfClassicErrorNone) break;

        data->type = type;
        if(type != MfClassicType1k) break;

        MfClassicDeviceKeys keys = {
            .key_a_mask = 0,
            .key_b_mask = 0,
        };
        for(size_t i = 0; i < mf_classic_get_total_sectors_num(data->type); i++) {
            bit_lib_num_to_bytes_be(
                metromoney_1k_keys[i].a, sizeof(MfClassicKey), keys.key_a[i].data);
            FURI_BIT_SET(keys.key_a_mask, i);
            bit_lib_num_to_bytes_be(
                metromoney_1k_keys[i].b, sizeof(MfClassicKey), keys.key_b[i].data);
            FURI_BIT_SET(keys.key_b_mask, i);
        }

        error = mf_classic_poller_sync_read(nfc, &keys, data);
        if(error == MfClassicErrorNotPresent) {
            FURI_LOG_W(TAG, "Failed to read data");
            break;
        }

        nfc_device_set_data(device, NfcProtocolMfClassic, data);

        is_read = (error == MfClassicErrorNone);
    } while(false);

    mf_classic_free(data);

    return is_read;
}

static bool metromoney_parse(const NfcDevice* device, FuriString* parsed_data) {
    furi_assert(device);

    const MfClassicData* data = nfc_device_get_data(device, NfcProtocolMfClassic);

    bool parsed = false;

    do {

        const uint8_t ticket_sector_number = 1;
        const uint8_t ticket_block_number = 1;

        const MfClassicSectorTrailer* sec_tr =
            mf_classic_get_sector_trailer_by_sector(data, ticket_sector_number);

        const uint64_t key =
            bit_lib_bytes_to_num_be(sec_tr->key_a.data, COUNT_OF(sec_tr->key_a.data));
        if(key != metromoney_1k_keys[ticket_sector_number].a) break;

        const uint8_t start_block_num =
            mf_classic_get_first_block_num_of_sector(ticket_sector_number);

        const uint8_t* block_start_ptr =
            &data->block[start_block_num + ticket_block_number].data[0];

        uint32_t balance = bit_lib_bytes_to_num_le(block_start_ptr, 4) - 100;

        uint32_t balance_lari = balance / 100;
        uint8_t balance_tetri = balance % 100;

        size_t uid_len = 0;
        const uint8_t* uid = mf_classic_get_uid(data, &uid_len);
        uint32_t card_number = bit_lib_bytes_to_num_le(uid, 4);

        furi_string_printf(
            parsed_data,
            "\e#Metromoney\nCard number: %lu\nBalance: %lu.%02u GEL",
            card_number,
            balance_lari,
            balance_tetri);
        parsed = true;
    } while(false);

    return parsed;
}

static const NfcSupportedCardsPlugin metromoney_plugin = {
    .protocol = NfcProtocolMfClassic,
    .verify = metromoney_verify,
    .read = metromoney_read,
    .parse = metromoney_parse,
};

static const FlipperAppPluginDescriptor metromoney_plugin_descriptor = {
    .appid = NFC_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = NFC_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &metromoney_plugin,
};

const FlipperAppPluginDescriptor* metromoney_plugin_ep(void) {
    return &metromoney_plugin_descriptor;
}
