#include "texkom_render.h"

void nfc_render_texkom_info(
    const TexkomData* data,
    NfcProtocolFormatType format_type,
    FuriString* str) {
    size_t uid_len = 0;
    const uint8_t* uid = texkom_get_uid(data, &uid_len);

    furi_string_cat_printf(str, "UID:");
    for(size_t i = 0; i < uid_len; i++) {
        furi_string_cat_printf(str, " %02X", uid[i]);
    }

    const bool crc_valid = texkom_is_crc_valid(data);
    furi_string_cat_printf(
        str, "\nCRC: %02X (%s)", data->frame[TEXKOM_FRAME_SIZE - 1], crc_valid ? "OK" : "Invalid");

    uint8_t expected_crc = 0;
    if(!crc_valid && texkom_get_expected_crc(data, &expected_crc)) {
        furi_string_cat_printf(str, "\nExpected CRC: %02X", expected_crc);
    }

    if(format_type == NfcProtocolFormatTypeFull) {
        furi_string_cat_printf(str, "\nFrame:");
        for(size_t i = 0; i < TEXKOM_FRAME_SIZE; i++) {
            furi_string_cat_printf(str, " %02X", data->frame[i]);
        }
    }
}
