#include "protopirate_lowram_guard.h"

#include <furi.h>
#include <furi_hal.h>
#include <cli/cli_vcp.h>

#define PROTOPIRATE_LOWRAM_SETTLE_MS 300

static FuriHalUsbInterface* s_pp_saved_usb = NULL;
static bool s_pp_cli_locked = false;

void protopirate_lowram_softlock_engage(void) {
    if(s_pp_cli_locked) return;

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_session_lock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);

    s_pp_saved_usb = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(NULL, NULL);
    s_pp_cli_locked = true;
}

void protopirate_lowram_softlock_release(void) {
    if(!s_pp_cli_locked) return;

    furi_hal_usb_set_config(s_pp_saved_usb, NULL);
    s_pp_saved_usb = NULL;

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    cli_vcp_session_unlock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);
    s_pp_cli_locked = false;
}

bool protopirate_lowram_mitigate(size_t threshold) {
    if(s_pp_cli_locked) return true;
    if(memmgr_get_free_heap() >= threshold) return false;
    protopirate_lowram_softlock_engage();
    furi_delay_ms(PROTOPIRATE_LOWRAM_SETTLE_MS);
    return true;
}
