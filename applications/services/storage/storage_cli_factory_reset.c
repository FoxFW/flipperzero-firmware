#include <furi.h>
#include <furi_hal.h>
#include <toolbox/pipe.h>
#include <toolbox/cli/cli_command.h>
#include <cli/cli_main_commands.h>
#include <power/power_service/power.h>

static void storage_cli_factory_reset(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    UNUSED(args);
    UNUSED(context);
    printf("All data will be lost! Are you sure (y/n)?\r\n");
    char c = getchar();
    if(c == 'y' || c == 'Y') {
        printf("Data will be wiped after reboot.\r\n");

        furi_hal_rtc_reset_registers();
        furi_hal_rtc_set_flag(FuriHalRtcFlagStorageFormatInternal);

        Power* power = furi_record_open(RECORD_POWER);
        power_reboot(power, PowerBootModeNormal);
    } else {
        printf("Safe choice.\r\n");
    }
}

CLI_COMMAND_INTERFACE(factory_reset, storage_cli_factory_reset, CliCommandFlagParallelSafe, 1 * 1024, CLI_APPID);
