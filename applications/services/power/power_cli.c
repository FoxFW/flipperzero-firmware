#include "power_cli.h"

#include <furi_hal.h>
#include <toolbox/cli/cli_command.h>
#include <cli/cli_main_commands.h>
#include <lib/toolbox/args.h>
#include <power/power_service/power.h>
#include <toolbox/pipe.h>

void power_cli_off(PipeSide* pipe, FuriString* args) {
    UNUSED(pipe);
    UNUSED(args);
    Power* power = furi_record_open(RECORD_POWER);
    printf("It's now safe to disconnect USB from your flipper\r\n");
    furi_delay_ms(666);
    power_off(power);
}

void power_cli_reboot(PipeSide* pipe, FuriString* args) {
    UNUSED(pipe);
    UNUSED(args);
    Power* power = furi_record_open(RECORD_POWER);
    power_reboot(power, PowerBootModeNormal);
}

void power_cli_reboot2dfu(PipeSide* pipe, FuriString* args) {
    UNUSED(pipe);
    UNUSED(args);
    Power* power = furi_record_open(RECORD_POWER);
    power_reboot(power, PowerBootModeDfu);
}

void power_cli_5v(PipeSide* pipe, FuriString* args) {
    UNUSED(pipe);
    Power* power = furi_record_open(RECORD_POWER);
    if(!furi_string_cmp(args, "0")) {
        power_enable_otg(power, false);
    } else if(!furi_string_cmp(args, "1")) {
        power_enable_otg(power, true);
    } else {
        cli_print_usage("power_otg", "<1|0>", furi_string_get_cstr(args));
    }

    furi_record_close(RECORD_POWER);
}

#ifdef FURI_DEBUG
void power_cli_3v3(PipeSide* pipe, FuriString* args) {
    UNUSED(pipe);
    if(!furi_string_cmp(args, "0")) {
        furi_hal_power_disable_external_3_3v();
    } else if(!furi_string_cmp(args, "1")) {
        furi_hal_power_enable_external_3_3v();
    } else {
        cli_print_usage("power_ext", "<1|0>", furi_string_get_cstr(args));
    }
}
#endif

static void power_cli_info(PipeSide* pipe, FuriString* args) {
    UNUSED(pipe);
    UNUSED(args);
    printf("Charge: %u %%\r\n", (unsigned)furi_hal_power_get_pct());
    printf("Health: %u %%\r\n", (unsigned)furi_hal_power_get_bat_health_pct());
    printf(
        "Charging: %s\r\n",
        furi_hal_power_is_charging() ?
            (furi_hal_power_is_charging_done() ? "done" : "yes") :
            "no");
    printf(
        "Voltage: %d mV\r\n",
        (int)(furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge) * 1000.0f));
    printf(
        "Current: %d mA\r\n",
        (int)(furi_hal_power_get_battery_current(FuriHalPowerICFuelGauge) * 1000.0f));
    printf(
        "Temp: %d C\r\n",
        (int)furi_hal_power_get_battery_temperature(FuriHalPowerICFuelGauge));
    printf(
        "Cap remaining: %lu mAh\r\n",
        (unsigned long)furi_hal_power_get_battery_remaining_capacity());
    printf(
        "Cap full: %lu mAh\r\n",
        (unsigned long)furi_hal_power_get_battery_full_capacity());
    printf(
        "Cap design: %lu mAh\r\n",
        (unsigned long)furi_hal_power_get_battery_design_capacity());
    printf(
        "USB: %d mV\r\n",
        (int)(furi_hal_power_get_usb_voltage() * 1000.0f));
}

static void power_cli_command_print_usage(void) {
    printf("Usage:\r\n");
    printf("power <cmd> <args>\r\n");
    printf("Cmd list:\r\n");

    printf("\toff\t - shutdown power\r\n");
    printf("\treboot\t - reboot\r\n");
    printf("\treboot2dfu\t - reboot to dfu bootloader\r\n");
    printf("\t5v <0 or 1>\t - enable or disable 5v ext\r\n");
    printf("\tinfo\t - print battery / charger info\r\n");
#ifdef FURI_DEBUG
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        printf("\t3v3 <0 or 1>\t - enable or disable 3v3 ext\r\n");
    }
#endif
}

void power_cli(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(context);
    FuriString* cmd;
    cmd = furi_string_alloc();

    do {
        if(!args_read_string_and_trim(args, cmd)) {
            power_cli_command_print_usage();
            break;
        }

        if(furi_string_cmp_str(cmd, "off") == 0) {
            power_cli_off(pipe, args);
            break;
        }

        if(furi_string_cmp_str(cmd, "reboot") == 0) {
            power_cli_reboot(pipe, args);
            break;
        }

        if(furi_string_cmp_str(cmd, "reboot2dfu") == 0) {
            power_cli_reboot2dfu(pipe, args);
            break;
        }

        if(furi_string_cmp_str(cmd, "5v") == 0) {
            power_cli_5v(pipe, args);
            break;
        }

        if(furi_string_cmp_str(cmd, "info") == 0) {
            power_cli_info(pipe, args);
            break;
        }

#ifdef FURI_DEBUG
        if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
            if(furi_string_cmp_str(cmd, "3v3") == 0) {
                power_cli_3v3(pipe, args);
                break;
            }
        }
#endif

        power_cli_command_print_usage();
    } while(false);

    furi_string_free(cmd);
}

CLI_COMMAND_INTERFACE(power, power_cli, CliCommandFlagParallelSafe, 2 * 1024, CLI_APPID);
