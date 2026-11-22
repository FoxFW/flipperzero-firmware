#include "t5577_multiwriter_em41xx.h"
#include "core/check.h"
#include "core/log.h"

#define EM41XX_LINES (10)
#define EM41XX_COLUMNS (4)

#define EM41XX_CONFIG_BLANK (0b00000000000101001000000000000000)

#define EM41XX_HEADER (0b111111111)

#define T5577_MAX_BLOCKS (8)
#define EM41XX_BLOCKS (2)

static bool t5577mw_get_parity(uint16_t data) {
    bool result = 0;
    for(int i = 0; i < 16; i++) result ^= ((data >> i) & 1);
    return result;
}

static bool t5577mw_get_line_parity_bit(uint8_t line_num, uint64_t data) {
    uint8_t line = (data >> (EM41XX_COLUMNS * line_num)) & 0x0F;
    return t5577mw_get_parity(line);
}

static bool t5577mw_get_column_parity_bit(uint8_t column_num, uint64_t data) {
    uint16_t column = 0;

    for(int i = 0; i < EM41XX_LINES; i++) {
        column <<= 1;
        column |= (data >> (EM41XX_COLUMNS * i + column_num)) & 1;
    }

    return t5577mw_get_parity(column);
}

static uint64_t t5577mw_em41xx_encode(uint64_t data) {
    uint64_t result = EM41XX_HEADER;

    for(int i = EM41XX_LINES - 1; i >= 0; i--) {
        result <<= EM41XX_COLUMNS;
        uint8_t line = (data >> (i * EM41XX_COLUMNS)) & 0x0F;
        result |= line;

        result <<= 1;
        result |= t5577mw_get_line_parity_bit(i, data);
    }

    for(int i = EM41XX_COLUMNS - 1; i >= 0; i--) {
        result <<= 1;
        result |= t5577mw_get_column_parity_bit(i, data);
    }

    result <<= 1;

    return result;
}

bool t5577mw_add_em41xx_data(LFRFIDT5577* data, uint64_t key, uint8_t from_index) {
    if(from_index + EM41XX_BLOCKS > (T5577_MAX_BLOCKS - 1)) return false;

    uint64_t blocks_data = t5577mw_em41xx_encode(key);
    data->block[from_index] = blocks_data >> 32;
    data->block[from_index + 1] = blocks_data & 0xFFFFFFFF;
    data->blocks_to_write = T5577_MAX_BLOCKS;

    uint8_t mask_addition = (1 << from_index);
    mask_addition |= (1 << (from_index + 1));

    data->mask |= mask_addition;

    FURI_LOG_D("T5577MW", "mask %u", data->mask);

    return true;
}

static uint32_t t5577mw_get_config(uint8_t keys_count) {
    if(keys_count > 3) return 0;

    uint32_t result = EM41XX_CONFIG_BLANK;
    result |= ((keys_count * EM41XX_BLOCKS) << 5);

    return result;
}

bool t5577mw_set_em41xx_config(LFRFIDT5577* data, uint8_t keys_count) {
    if(keys_count > 3) return false;

    data->block[0] = t5577mw_get_config(keys_count);

    data->mask |= 1;
    FURI_LOG_D("T5577MW", "config mask %u", data->mask);

    return true;
}

uint64_t t5577mw_bytes2num(const uint8_t* src, uint8_t len) {
    furi_assert(src);
    furi_assert(len <= 8);

    uint64_t res = 0;
    while(len--) {
        res = (res << 8) | (*src);
        src++;
    }
    return res;
}
