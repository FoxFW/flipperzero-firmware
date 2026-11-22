#pragma once

#include <lfrfid/tools/t5577.h>

bool t5577mw_add_em41xx_data(LFRFIDT5577* data, uint64_t key, uint8_t from_index);

bool t5577mw_set_em41xx_config(LFRFIDT5577* data, uint8_t keys_count);

uint64_t t5577mw_bytes2num(const uint8_t* src, uint8_t len);
