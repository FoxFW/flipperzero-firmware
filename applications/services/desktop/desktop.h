#pragma once

#include <furi.h>

#include "desktop_settings.h"
#include "helpers/pin_code.h"

#define RECORD_DESKTOP "desktop"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Desktop Desktop;

typedef struct {
    bool locked;
} DesktopStatus;

typedef enum {
    DesktopUsbModeQflipper,
    DesktopUsbModeMassStorage,
} DesktopUsbMode;

bool desktop_api_is_locked(Desktop* instance);

void desktop_api_unlock(Desktop* instance);

FuriPubSub* desktop_api_get_status_pubsub(Desktop* instance);

void desktop_api_get_settings(Desktop* instance, DesktopSettings* settings);

void desktop_api_set_settings(Desktop* instance, const DesktopSettings* settings);

void desktop_api_set_pin(Desktop* instance, const DesktopPinCode* pin_code);

void desktop_api_clear_pin(Desktop* instance);

DesktopUsbMode desktop_api_get_usb_mode(Desktop* instance);

void desktop_api_set_usb_mode(Desktop* instance, DesktopUsbMode mode);

bool desktop_boot_is_ready(void);

#ifdef __cplusplus
}
#endif
