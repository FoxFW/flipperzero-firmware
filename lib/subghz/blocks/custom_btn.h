#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_CUSTOM_BTN_OK    (0U)
#define SUBGHZ_CUSTOM_BTN_UP    (1U)
#define SUBGHZ_CUSTOM_BTN_DOWN  (2U)
#define SUBGHZ_CUSTOM_BTN_LEFT  (3U)
#define SUBGHZ_CUSTOM_BTN_RIGHT (4U)

bool subghz_custom_btn_set(uint8_t btn_id);

uint8_t subghz_custom_btn_get(void);

uint8_t subghz_custom_btn_get_original(void);
void subghz_custom_btn_set_original(uint8_t btn_code);

void subghz_custom_btn_set_max(uint8_t b);

void subghz_custom_btns_reset(void);

bool subghz_custom_btn_is_allowed(void);

void subghz_custom_btn_set_long(bool v);
bool subghz_custom_btn_get_long(void);

void subghz_custom_btn_set_pages(bool enabled);
bool subghz_custom_btn_has_pages(void);
void subghz_custom_btn_set_page(uint8_t page);
uint8_t subghz_custom_btn_get_page(void);
void subghz_custom_btn_set_max_pages(uint8_t n);
uint8_t subghz_custom_btn_get_max_pages(void);

const char* subghz_custom_btn_get_label_for_proto(const char* proto_name, uint8_t btn_dir);

void subghz_custom_btn_set_original_if_unset(uint8_t btn_code);
void subghz_custom_btn_apply_map(
    uint8_t* btn,
    uint8_t original,
    uint8_t custom_id,
    const uint8_t* btns,
    const uint8_t* btn_map,
    size_t btns_count,
    uint8_t custom_max);
void subghz_custom_btn_fill_labels(
    uint8_t btn,
    const uint8_t* btns,
    const uint8_t* btn_map,
    size_t btns_count,
    uint8_t custom_max,
    const char* (*get_name)(uint8_t));

#ifdef __cplusplus
}
#endif
