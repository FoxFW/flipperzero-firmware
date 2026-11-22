

#include "nfc_supported_card_plugin.h"
#include <flipper_application.h>

#include "protocols/mf_classic/mf_classic.h"

#include <bit_lib.h>
#include <datetime.h>
#include <locale/locale.h>

#define TAG "Umarsh"

bool parse_datetime(uint16_t date, DateTime* result) {
    result->year = 2000 + (date >> 9);
    result->month = date >> 5 & 0x0F;
    result->day = date & 0x1F;
    return date != 0;
}

static bool umarsh_parse(const NfcDevice* device, FuriString* parsed_data) {
    furi_assert(device);

    const MfClassicData* data = nfc_device_get_data(device, NfcProtocolMfClassic);

    bool parsed = false;

    do {

        if(data->type != MfClassicType1k) break;

        const uint8_t ticket_sector = 8;

        const uint8_t ticket_sector_start_block_number =
            mf_classic_get_first_block_num_of_sector(ticket_sector);

        const uint8_t* block_start_ptr = &data->block[ticket_sector_start_block_number].data[0];

        const uint32_t header_part_0 = bit_lib_bytes_to_num_be(block_start_ptr, 4);
        const uint32_t header_part_1 = bit_lib_bytes_to_num_be(block_start_ptr + 4, 4);
        if((header_part_0 + header_part_1) != 0xFFFFFFFF) break;

        block_start_ptr = &data->block[ticket_sector_start_block_number + 1].data[0];
        const uint16_t expiry_date = bit_lib_bytes_to_num_be(block_start_ptr + 1, 2);
        const uint8_t region_number = (((block_start_ptr[8] >> 5) & 0x07) << 4) |
                                      (block_start_ptr[12] & 0x0F);
        const uint8_t refill_counter = bit_lib_bytes_to_num_be(block_start_ptr + 7, 1);
        const uint32_t card_number = bit_lib_bytes_to_num_be(block_start_ptr + 8, 4) & 0x3FFFFFFF;

        if(card_number == 0) break;

        block_start_ptr = &data->block[ticket_sector_start_block_number + 2].data[0];
        const uint16_t valid_to = bit_lib_bytes_to_num_be(block_start_ptr, 2);
        const uint32_t terminal_number = bit_lib_bytes_to_num_be(block_start_ptr + 3, 3);
        const uint16_t last_refill_date = bit_lib_bytes_to_num_be(block_start_ptr + 6, 2);
        const uint16_t balance_rub = (bit_lib_bytes_to_num_be(block_start_ptr + 8, 2)) & 0x7FFF;
        const uint8_t balance_kop = bit_lib_bytes_to_num_be(block_start_ptr + 10, 1) & 0x7F;

        DateTime expiry_datetime;
        bool is_expiry_datetime_valid = parse_datetime(expiry_date, &expiry_datetime);

        DateTime valid_to_datetime;
        bool is_valid_to_datetime_valid = parse_datetime(valid_to, &valid_to_datetime);

        DateTime last_refill_datetime;
        bool is_last_refill_datetime_valid =
            parse_datetime(last_refill_date, &last_refill_datetime);

        LocaleDateFormat date_format = locale_get_date_format();
        const char* separator = (date_format == LocaleDateFormatDMY) ? "." : "/";

        FuriString* expiry_datetime_str = furi_string_alloc();
        locale_format_date(expiry_datetime_str, &expiry_datetime, date_format, separator);

        FuriString* valid_to_datetime_str = furi_string_alloc();
        locale_format_date(valid_to_datetime_str, &valid_to_datetime, date_format, separator);

        FuriString* last_refill_datetime_str = furi_string_alloc();
        locale_format_date(
            last_refill_datetime_str, &last_refill_datetime, date_format, separator);

        furi_string_cat_printf(
            parsed_data,
            "\e#Umarsh\nCard number: %lu\nRegion: %02u\nTerminal number: %lu\nRefill counter: %u\nBalance: %u.%02u RUR",
            card_number,
            region_number,
            terminal_number,
            refill_counter,
            balance_rub,
            balance_kop);

        if(is_expiry_datetime_valid)
            furi_string_cat_printf(
                parsed_data, "\nExpires: %s", furi_string_get_cstr(expiry_datetime_str));
        if(is_valid_to_datetime_valid)
            furi_string_cat_printf(
                parsed_data, "\nValid to: %s", furi_string_get_cstr(valid_to_datetime_str));
        if(is_last_refill_datetime_valid)
            furi_string_cat_printf(
                parsed_data, "\nLast refill: %s", furi_string_get_cstr(last_refill_datetime_str));

        furi_string_free(expiry_datetime_str);
        furi_string_free(valid_to_datetime_str);
        furi_string_free(last_refill_datetime_str);

        parsed = true;
    } while(false);

    return parsed;
}

static const NfcSupportedCardsPlugin umarsh_plugin = {
    .protocol = NfcProtocolMfClassic,
    .verify = NULL,
    .read = NULL,
    .parse = umarsh_parse,
};

static const FlipperAppPluginDescriptor umarsh_plugin_descriptor = {
    .appid = NFC_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = NFC_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &umarsh_plugin,
};

const FlipperAppPluginDescriptor* umarsh_plugin_ep(void) {
    return &umarsh_plugin_descriptor;
}
