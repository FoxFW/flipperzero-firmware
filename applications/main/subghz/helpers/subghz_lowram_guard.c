#include "subghz_lowram_guard.h"

#include <furi.h>
#include <furi_hal.h>
#include <cli/cli_vcp.h>

#define SUBGHZ_LOWRAM_SETTLE_MS 300

static FuriHalUsbInterface* s_saved_usb = NULL;
static bool s_cli_locked = false;

bool subghz_lowram_should_softlock(void) {
    return memmgr_get_free_heap() < SUBGHZ_LOWRAM_SOFTLOCK_THRESHOLD;
}

void subghz_lowram_softlock_engage(void) {
    if(s_cli_locked) return;

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_session_lock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);

    s_saved_usb = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(NULL, NULL);
    s_cli_locked = true;
}

void subghz_lowram_softlock_release(void) {
    if(!s_cli_locked) return;

    furi_hal_usb_set_config(s_saved_usb, NULL);
    s_saved_usb = NULL;

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_session_unlock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);
    s_cli_locked = false;
}

bool subghz_lowram_mitigate(size_t threshold) {
    if(s_cli_locked) return true;
    if(memmgr_get_free_heap() >= threshold) return false;
    subghz_lowram_softlock_engage();
    furi_delay_ms(SUBGHZ_LOWRAM_SETTLE_MS);
    return true;
}
