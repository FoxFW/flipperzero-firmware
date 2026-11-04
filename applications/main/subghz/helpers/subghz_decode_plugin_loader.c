/* On-demand decode-plugin loader (app side): feed stays internal; when a decoder
 * fires, the shared wrapper (lib common.c) dispatches by protocol name and this
 * loader pulls JUST that protocol's decode_<x>.fal from the SD, runs its get_string,
 * and caches it. One .fal resident at a time (small LRU). No SD / missing plugin ->
 * libsubghz writes a slim raw fallback. FoxFW2.0 2026-10-21 full split. */

#include "subghz_decode_plugin_loader.h"
#include "../protocols_ext/subghz_decode_plugin_desc.h"
#include <lib/subghz/protocols/decode_plugin.h>

#include <furi.h>
#include <string.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

#define TAG "SubGhzDecodeLoader"
#define SUBGHZ_DECODE_CACHE          1
#define SUBGHZ_DECODE_MIN_FREE_HEAP  8192u

typedef struct {
    const char* name;
    const char* fal;
} SubGhzDecodeMap;

static const SubGhzDecodeMap subghz_decode_map[] = {
    {.name = "Princeton", .fal = "decode_princeton"},
    {.name = "Mitsubishi V0", .fal = "decode_g1"},
    {.name = "Nissan V0", .fal = "decode_g1"},
    {.name = "Opel V0", .fal = "decode_g1"},
    {.name = "Porsche Touareg", .fal = "decode_g1"},
    {.name = "Tesla Charge", .fal = "decode_tesla_charge"},
    {.name = "PSA GROUP", .fal = "decode_psa"},
    {.name = "Audi V1", .fal = "decode_audi_v1"},
    {.name = "Audi V2", .fal = "decode_audi_v2"},
    {.name = "Audiovox PRO", .fal = "decode_audiovox"},
    {.name = "BMW V2", .fal = "decode_bmw_v2"},
    {.name = "BMW/Mini V3", .fal = "decode_bmw_v3"},
    {.name = "Chevrolet V0", .fal = "decode_chevrolet_v0"},
    {.name = "Chrysler", .fal = "decode_chrysler"},
    {.name = "Compustar", .fal = "decode_compustar"},
    {.name = "Fiat Ducato V0", .fal = "decode_ducato_v0"},
    {.name = "Fiat V0", .fal = "decode_fiat_v0"},
    {.name = "Fiat V1", .fal = "decode_fiat_v1"},
    {.name = "Fiat V2", .fal = "decode_fiat_v2"},
    {.name = "FORD V0", .fal = "decode_ford_v0"},
    {.name = "Ford V2", .fal = "decode_ford_v2"},
    {.name = "Ford V3", .fal = "decode_ford_v3"},
    {.name = "GM", .fal = "decode_gm"},
    {.name = "GM ABO1502T", .fal = "decode_gm_rke"},
    {.name = "Hitag2", .fal = "decode_hitag2"},
    {.name = "Honda V4", .fal = "decode_honda_v4"},
    {.name = "Honda V7", .fal = "decode_honda_v7"},
    {.name = "Honda V8", .fal = "decode_honda_v8"},
    {.name = "Honda V9", .fal = "decode_honda_v9"},
    {.name = "Hyundai/Kia RIO", .fal = "decode_hyundai_kia_rio"},
    {.name = "Jeep V0", .fal = "decode_jeep_v0"},
    {.name = "KIA/HYU V0", .fal = "decode_kia_v0"},
    {.name = "KIA/HYU V1", .fal = "decode_kia_v1"},
    {.name = "KIA/HYU V2", .fal = "decode_kia_v2"},
    {.name = "KIA/HYU V3/V4", .fal = "decode_kia_v3_v4"},
    {.name = "KIA/HYU V5", .fal = "decode_kia_v5"},
    {.name = "KIA/HYU V6", .fal = "decode_kia_v6"},
    {.name = "Kia V7", .fal = "decode_kia_v7"},
    {.name = "LDV V0", .fal = "decode_ldv_v0"},
    {.name = "Lexus V0", .fal = "decode_lexus_v0"},
    {.name = "MazdaSiemens", .fal = "decode_mazda_siemens"},
    {.name = "Mazda V0", .fal = "decode_mazda_v0"},
    {.name = "Mazda V1", .fal = "decode_mazda_v1"},
    {.name = "Mazda V2", .fal = "decode_mazda_v2"},
    {.name = "Mitsubishi V0-a", .fal = "decode_mitsubishi_v0a"},
    {.name = "Mitsubishi V1", .fal = "decode_mitsubishi_v1"},
    {.name = "Renault V0", .fal = "decode_renault_v0"},
    {.name = "Renault V2", .fal = "decode_renault_v2"},
    {.name = "Renault V3", .fal = "decode_renault_v3"},
    {.name = "Renault/xHorse V4", .fal = "decode_renault_v4"},
    {.name = "SantaFe 13-16", .fal = "decode_santa_fe"},
    {.name = "Scher-Khan", .fal = "decode_scher_khan"},
    {.name = "Sheriff CFM", .fal = "decode_sheriff_cfm"},
    {.name = "Siemens VDO", .fal = "decode_siemens_vdo"},
    {.name = "SUBARU", .fal = "decode_subaru"},
    {.name = "SUZUKI", .fal = "decode_suzuki"},
    {.name = "Allstar Firefly", .fal = "decode_allstar_firefly"},
    {.name = "Ansonic", .fal = "decode_ansonic"},
    {.name = "BETT", .fal = "decode_bett"},
    {.name = "CAME", .fal = "decode_came"},
    {.name = "Cham_Code", .fal = "decode_chamberlain_code"},
    {.name = "Clemsa", .fal = "decode_clemsa"},
    {.name = "Doitrand", .fal = "decode_doitrand"},
    {.name = "Elplast", .fal = "decode_elplast"},
    {.name = "Feron", .fal = "decode_feron"},
    {.name = "GangQi", .fal = "decode_gangqi"},
    {.name = "GateTX", .fal = "decode_gate_tx"},
    {.name = "Hollarm", .fal = "decode_hollarm"},
    {.name = "Holtek_HT12X", .fal = "decode_holtek_ht12x"},
    {.name = "Honeywell Sec", .fal = "decode_honeywell"},
    {.name = "Intertechno_V3", .fal = "decode_intertechno_v3"},
    {.name = "Legrand", .fal = "decode_legrand"},
    {.name = "Linear", .fal = "decode_linear"},
    {.name = "LinearDelta3", .fal = "decode_linear_delta3"},
    {.name = "Magellan", .fal = "decode_magellan"},
    {.name = "Marantec24", .fal = "decode_marantec24"},
    {.name = "Mastercode", .fal = "decode_mastercode"},
    {.name = "MegaCode", .fal = "decode_megacode"},
    {.name = "Nice FLO", .fal = "decode_nice_flo"},
    {.name = "SMC5326", .fal = "decode_smc5326"},
    {.name = "X10", .fal = "decode_x10"},
    {.name = "KeeLoq", .fal = "decode_keeloq"},
    {.name = "Star Line", .fal = "decode_star_line"},
    {.name = "VAG GROUP", .fal = "decode_vag"},
    {.name = "VAG Polo", .fal = "decode_vag_polo"},
    {.name = "AstroStart 2000", .fal = "decode_astrostart"},
    {.name = "Audi V3", .fal = "decode_audi_v3"},
    {.name = "Continental RKE", .fal = "decode_continental_rke"},
    {.name = "Honda V5", .fal = "decode_honda_v5"},
    {.name = "Honda V6", .fal = "decode_honda_v6"},
    {.name = "Honda V10", .fal = "decode_honda_v10"},
    {.name = "Peugot V0", .fal = "decode_peugot_v0"},
    {.name = "Toyota V0", .fal = "decode_toyota_v0"},
    {.name = "FIAT SPA", .fal = "decode_fiat_spa"},
    {.name = "Honda Static", .fal = "decode_honda_static"},
    {.name = "Honda V1", .fal = "decode_honda_v1"},
    {.name = "HondaV2&V3", .fal = "decode_honda_v2_v3"},
    {.name = "Toyota", .fal = "decode_toyota"},
    {.name = "Porsche AG", .fal = "decode_porsche_cayenne"},
    {.name = "Fiat Multi", .fal = "decode_fiat_v2_v3_v5"},
    {.name = "Ford V1", .fal = "decode_ford_v1"},
    {.name = "Hitag-2", .fal = "decode_renault_v1"},
    /* 2026-10-22: remaining in-tree protocols brought live */
    {.name = "Alps FWB1U545", .fal = "decode_alps_rke"},
    {.name = "Alutech AT-4N", .fal = "decode_alutech_at_4n"},
    {.name = "Audi", .fal = "decode_audi"},
    {.name = "Beninca ARC", .fal = "decode_beninca_arc"},
    {.name = "BMW CAS3", .fal = "decode_bmw_cas3"},
    {.name = "BMW CAS4", .fal = "decode_bmw_cas4"},
    {.name = "CAME Atomo", .fal = "decode_came_atomo"},
    {.name = "CAME TWEE", .fal = "decode_came_twee"},
    {.name = "Code Alarm", .fal = "decode_code_alarm"},
    {.name = "Dickert_MAHS", .fal = "decode_dickert_mahs"},
    {.name = "Ditec GOL4", .fal = "decode_ditec_gol4"},
    {.name = "Dooya", .fal = "decode_dooya"},
    {.name = "Faac SLH", .fal = "decode_faac_slh"},
    {.name = "MARELLI", .fal = "decode_fiat_marelli"},
    {.name = "Hay21", .fal = "decode_hay21"},
    {.name = "Holtek", .fal = "decode_holtek"},
    {.name = "Honeywell", .fal = "decode_honeywell_wdb"},
    {.name = "Hormann HSM", .fal = "decode_hormann"},
    {.name = "Hundai", .fal = "decode_hundai"},
    {.name = "iDo 117/111", .fal = "decode_ido"},
    {.name = "Jarolift", .fal = "decode_jarolift"},
    {.name = "KeyFinder", .fal = "decode_keyfinder"},
    {.name = "KingGates Stylo4k", .fal = "decode_kinggates_stylo_4k"},
    {.name = "Land Rover V0", .fal = "decode_land_rover_v0"},
    {.name = "Marantec", .fal = "decode_marantec"},
    {.name = "Mazda Infinity", .fal = "decode_mazda_infinity"},
    {.name = "Mercedes", .fal = "decode_mercedes"},
    {.name = "Microchip 6SC2", .fal = "decode_mic6sc2"},
    {.name = "Nero Radio", .fal = "decode_nero_radio"},
    {.name = "Nero Sketch", .fal = "decode_nero_sketch"},
    {.name = "Nice FloR-S", .fal = "decode_nice_flor_s"},
    {.name = "Nidec RKE", .fal = "decode_nidec_rke"},
    {.name = "Nord ICE", .fal = "decode_nord_ice"},
    {.name = "Phoenix_V2", .fal = "decode_phoenix_v2"},
    {.name = "Power Smart", .fal = "decode_power_smart"},
    {.name = "PSA OLD", .fal = "decode_psa2"},
    {.name = "Revers_RB2", .fal = "decode_revers_rb2"},
    {.name = "Roger", .fal = "decode_roger"},
    {.name = "Security+ 1.0", .fal = "decode_secplus_v1"},
    {.name = "Security+ 2.0", .fal = "decode_secplus_v2"},
    {.name = "Somfy Keytis", .fal = "decode_somfy_keytis"},
    {.name = "Somfy Telis", .fal = "decode_somfy_telis"},
    {.name = "Telcoma/Cardin EDGE", .fal = "decode_telcoma_edge"},
    {.name = "Treadmill37", .fal = "decode_treadmill37"},
    {.name = "Urmet", .fal = "decode_urmet"},
    {.name = "KeyFinder2", .fal = "decode_keyfinder2"},
    {.name = "Doorbell32", .fal = "decode_doorbell32"},
    {.name = "Opel V1", .fal = "decode_opel_v1"},
};
#define SUBGHZ_DECODE_MAP_COUNT (sizeof(subghz_decode_map) / sizeof(subghz_decode_map[0]))

typedef struct {
    char fal[40];
    PluginManager* manager;
    const SubGhzDecodeRegistry* registry;
} SubGhzDecodeCache;

static SubGhzDecodeCache subghz_decode_cache[SUBGHZ_DECODE_CACHE];
static uint8_t subghz_decode_cache_next = 0;

void subghz_decode_plugin_loader_flush(void) {
    for(uint8_t i = 0; i < SUBGHZ_DECODE_CACHE; i++) {
        if(subghz_decode_cache[i].manager) plugin_manager_free(subghz_decode_cache[i].manager);
        subghz_decode_cache[i].manager = NULL;
        subghz_decode_cache[i].registry = NULL;
        subghz_decode_cache[i].fal[0] = '\0';
    }
    subghz_decode_cache_next = 0;
}

static const SubGhzDecodeRegistry* subghz_decode_load(const char* fal) {
    for(uint8_t i = 0; i < SUBGHZ_DECODE_CACHE; i++) {
        if(subghz_decode_cache[i].registry &&
           strcmp(subghz_decode_cache[i].fal, fal) == 0) {
            return subghz_decode_cache[i].registry;
        }
    }
    subghz_decode_plugin_loader_flush();
    if(memmgr_get_free_heap() < SUBGHZ_DECODE_MIN_FREE_HEAP) return NULL;
    PluginManager* manager = plugin_manager_alloc(
        SUBGHZ_DECODE_PLUGIN_APP_ID, SUBGHZ_DECODE_PLUGIN_API_VERSION, firmware_api_interface);
    FuriString* path = furi_string_alloc();
    furi_string_printf(path, APP_DATA_PATH("plugins/%s.fal"), fal);
    PluginManagerError err = plugin_manager_load_single(manager, furi_string_get_cstr(path));
    furi_string_free(path);
    if(err != PluginManagerErrorNone) {
        plugin_manager_free(manager);
        return NULL;
    }
    const SubGhzDecodeRegistry* reg = plugin_manager_get_ep(manager, 0);
    if(!reg || !reg->items) {
        plugin_manager_free(manager);
        return NULL;
    }
    SubGhzDecodeCache* e = &subghz_decode_cache[subghz_decode_cache_next];
    if(e->manager) plugin_manager_free(e->manager);
    strncpy(e->fal, fal, sizeof(e->fal) - 1);
    e->fal[sizeof(e->fal) - 1] = '\0';
    e->manager = manager;
    e->registry = reg;
    subghz_decode_cache_next = (subghz_decode_cache_next + 1) % SUBGHZ_DECODE_CACHE;
    return reg;
}

static bool subghz_decode_loader_run(
    const char* name,
    const SubGhzDecodeInput* input,
    FuriString* output) {
    if(!name) return false;
    const char* fal = NULL;
    for(size_t i = 0; i < SUBGHZ_DECODE_MAP_COUNT; i++) {
        if(strcmp(subghz_decode_map[i].name, name) == 0) {
            fal = subghz_decode_map[i].fal;
            break;
        }
    }
    if(!fal) return false;
    const SubGhzDecodeRegistry* reg = subghz_decode_load(fal);
    if(!reg) return false;
    for(size_t i = 0; i < reg->size; i++) {
        if(reg->items[i].name && reg->items[i].get_string &&
           strcmp(reg->items[i].name, name) == 0) {
            reg->items[i].get_string(input, output);
            return true;
        }
    }
    return false;
}

void subghz_decode_plugin_loader_install(void) {
    subghz_decode_plugin_set_loader(subghz_decode_loader_run);
}
