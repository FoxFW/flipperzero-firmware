

#include "nfc_supported_card_plugin.h"
#include <flipper_application.h>

#include <lib/nfc/protocols/mf_desfire/mf_desfire.h>

#include <applications/services/locale/locale.h>
#include <datetime.h>

static const MfDesfireApplicationId opal_app_id = {.data = {0x31, 0x45, 0x53}};

static const MfDesfireFileId opal_file_id = 0x07;

static const char* opal_modes[5] =
    {"Rail / Metro", "Ferry / Light Rail", "Bus", "Unknown mode", "Manly Ferry"};

static const char* opal_usages[14] = {
    "New / Unused",
    "Tap on: new journey",
    "Tap on: transfer from same mode",
    "Tap on: transfer from other mode",
    NULL,
    NULL,
    NULL,
    "Tap off: distance fare",
    "Tap off: flat fare",
    "Automated tap off: failed to tap off",
    "Tap off: end of trip without start",
    "Tap off: reversal",
    "Tap on: rejected",
    "Unknown usage",
};

typedef struct FURI_PACKED {
    uint32_t serial         : 32;
    uint8_t check_digit     : 4;
    bool blocked            : 1;
    uint16_t txn_number     : 16;
    int32_t balance         : 21;
    uint16_t days           : 15;
    uint16_t minutes        : 11;
    uint8_t mode            : 3;
    uint16_t usage          : 4;
    bool auto_topup         : 1;
    uint8_t weekly_journeys : 4;
    uint16_t checksum       : 16;
} OpalFile;

static_assert(sizeof(OpalFile) == 16, "OpalFile");

static void opal_days_minutes_to_datetime(uint16_t days, uint16_t minutes, DateTime* out) {
    out->year = 1980;
    out->month = 1;

    out->weekday = ((days + 1) % 7) + 1;
    out->hour = minutes / 60;
    out->minute = minutes % 60;
    out->second = 0;

    for(;;) {
        const uint16_t num_days_in_year = datetime_get_days_per_year(out->year);
        if(days < num_days_in_year) break;
        days -= num_days_in_year;
        out->year++;
    }

    days++;

    for(;;) {

        const bool is_leap = datetime_is_leap_year(out->year);
        const uint8_t num_days_in_month = datetime_get_days_per_month(is_leap, out->month);
        if(days <= num_days_in_month) break;
        days -= num_days_in_month;
        out->month++;
    }

    out->day = days;
}

static bool opal_parse(const NfcDevice* device, FuriString* parsed_data) {
    furi_assert(device);
    furi_assert(parsed_data);

    const MfDesfireData* data = nfc_device_get_data(device, NfcProtocolMfDesfire);

    bool parsed = false;

    do {
        const MfDesfireApplication* app = mf_desfire_get_application(data, &opal_app_id);
        if(app == NULL) break;

        const MfDesfireFileSettings* file_settings =
            mf_desfire_get_file_settings(app, &opal_file_id);
        if(file_settings == NULL || file_settings->type != MfDesfireFileTypeStandard ||
           file_settings->data.size != sizeof(OpalFile))
            break;

        const MfDesfireFileData* file_data = mf_desfire_get_file_data(app, &opal_file_id);
        if(file_data == NULL) break;

        const OpalFile* opal_file = simple_array_cget_data(file_data->data);

        const uint8_t serial2 = opal_file->serial / 10000000;
        const uint16_t serial3 = (opal_file->serial / 1000) % 10000;
        const uint16_t serial4 = (opal_file->serial % 1000);

        if(opal_file->check_digit > 9) break;

        const bool is_negative_balance = (opal_file->balance < 0);
        const char* sign = is_negative_balance ? "-" : "";
        const int32_t balance = is_negative_balance ? labs(opal_file->balance) :
                                                      opal_file->balance;
        const uint8_t balance_cents = balance % 100;
        const int32_t balance_dollars = balance / 100;

        DateTime timestamp;
        opal_days_minutes_to_datetime(opal_file->days, opal_file->minutes, &timestamp);

        const bool is_manly_ferry = (opal_file->usage >= 4) && (opal_file->usage <= 6);

        const uint8_t mode = is_manly_ferry ? 4 : opal_file->mode;
        const uint8_t usage = is_manly_ferry ? opal_file->usage - 3 : opal_file->usage;

        const char* mode_str = opal_modes[mode > 4 ? 3 : mode];
        const char* usage_str = opal_usages[usage > 12 ? 13 : usage];

        furi_string_printf(
            parsed_data,
            "\e#Opal: $%s%ld.%02hu\nNo.: 3085 22%02hhu %04hu %03hu%01hhu\n%s, %s\n",
            sign,
            balance_dollars,
            balance_cents,
            serial2,
            serial3,
            serial4,
            opal_file->check_digit,
            mode_str,
            usage_str);

        FuriString* timestamp_str = furi_string_alloc();

        locale_format_date(timestamp_str, &timestamp, locale_get_date_format(), "-");
        furi_string_cat(parsed_data, timestamp_str);
        furi_string_cat(parsed_data, " at ");

        locale_format_time(timestamp_str, &timestamp, locale_get_time_format(), false);
        furi_string_cat(parsed_data, timestamp_str);

        furi_string_free(timestamp_str);

        furi_string_cat_printf(
            parsed_data,
            "\nWeekly journeys: %hhu, Txn #%hu\n",
            opal_file->weekly_journeys,
            opal_file->txn_number);

        if(opal_file->auto_topup) {
            furi_string_cat_str(parsed_data, "Auto-topup enabled\n");
        }

        if(opal_file->blocked) {
            furi_string_cat_str(parsed_data, "Card blocked\n");
        }

        parsed = true;
    } while(false);

    return parsed;
}

static const NfcSupportedCardsPlugin opal_plugin = {
    .protocol = NfcProtocolMfDesfire,
    .verify = NULL,
    .read = NULL,
    .parse = opal_parse,
};

static const FlipperAppPluginDescriptor opal_plugin_descriptor = {
    .appid = NFC_SUPPORTED_CARD_PLUGIN_APP_ID,
    .ep_api_version = NFC_SUPPORTED_CARD_PLUGIN_API_VERSION,
    .entry_point = &opal_plugin,
};

const FlipperAppPluginDescriptor* opal_plugin_ep(void) {
    return &opal_plugin_descriptor;
}
