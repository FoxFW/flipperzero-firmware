#pragma once

#include <gui/view.h>

#define SUBGHZ_RIB_MAX_BTN 3

typedef struct SubGhzReceiverInfoBar SubGhzReceiverInfoBar;

typedef void (*SubGhzReceiverInfoBarCallback)(void* context, uint32_t event);

SubGhzReceiverInfoBar* subghz_receiver_info_bar_alloc(void);
void subghz_receiver_info_bar_free(SubGhzReceiverInfoBar* instance);
View* subghz_receiver_info_bar_get_view(SubGhzReceiverInfoBar* instance);

void subghz_receiver_info_bar_set_callback(
    SubGhzReceiverInfoBar* instance,
    SubGhzReceiverInfoBarCallback callback,
    void* context);

void subghz_receiver_info_bar_reset(SubGhzReceiverInfoBar* instance);

void subghz_receiver_info_bar_add_button(
    SubGhzReceiverInfoBar* instance,
    const char* label,
    bool icon,
    uint32_t event_press,
    uint32_t event_release);

void subghz_receiver_info_bar_set_info(
    SubGhzReceiverInfoBar* instance,
    const char* proto_multiline,
    const char* freq,
    const char* mod);

void subghz_receiver_info_bar_set_focus(
    SubGhzReceiverInfoBar* instance,
    uint16_t signal_idx,
    uint8_t primary_idx);

void subghz_receiver_info_bar_scroll_reset(SubGhzReceiverInfoBar* instance);

void subghz_receiver_info_bar_resume_scroll(SubGhzReceiverInfoBar* instance);
void subghz_receiver_info_bar_pause_scroll(SubGhzReceiverInfoBar* instance);
