#pragma once

#include <gui/view.h>

/* #15: Fox-style selectable button bar for the SubGHz Receiver-Info screen.
 * Up to 3 rounded-pill buttons along the bottom; Left/Right move a cursor,
 * OK triggers the selected button (press/hold for the Send button). Replaces
 * the stock widget button row when the Fox theme is active. */

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

/* Clear the button list (call before adding). Keeps cursor/last_idx. */
void subghz_receiver_info_bar_reset(SubGhzReceiverInfoBar* instance);

/* Append a button. `icon` draws the play glyph before the label (used for "Raw").
 * `event_press` fires on OK; for a press/hold button (Send) pass a non-zero
 * `event_release` too (fires on OK release, `event_press` on OK press). Pass
 * `event_release = 0` for a normal short-press button. */
void subghz_receiver_info_bar_add_button(
    SubGhzReceiverInfoBar* instance,
    const char* label,
    bool icon,
    uint32_t event_press,
    uint32_t event_release);

/* Push the info strings drawn above the bar. */
void subghz_receiver_info_bar_set_info(
    SubGhzReceiverInfoBar* instance,
    const char* proto_multiline,
    const char* freq,
    const char* mod);

/* Decide the cursor: default to `primary_idx` when the viewed signal changed
 * (signal_idx != last), otherwise keep the remembered cursor (clamped). */
void subghz_receiver_info_bar_set_focus(
    SubGhzReceiverInfoBar* instance,
    uint16_t signal_idx,
    uint8_t primary_idx);

void subghz_receiver_info_bar_scroll_reset(SubGhzReceiverInfoBar* instance);

/* Start/stop the horizontal marquee timer for overflowing info lines. Call
 * resume when the receiver-info scene becomes active and pause on exit. */
void subghz_receiver_info_bar_resume_scroll(SubGhzReceiverInfoBar* instance);
void subghz_receiver_info_bar_pause_scroll(SubGhzReceiverInfoBar* instance);
