

#include "foxdeflock_scan.h"
#include "helpers/oui_vendor.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define SCAN_MODE_PERIOD_MS 8500

static const char* const SCAN_MODE_CMD[] = {
    "WIFISNIFF:PROBE",
    "WIFISNIFF:BEACON",
    "BLETAGSCAN:FLOCK",
};
#define SCAN_MODE_COUNT (sizeof(SCAN_MODE_CMD) / sizeof(SCAN_MODE_CMD[0]))

void foxdeflock_send_probe(FoxDeFlockApp* app) {
    if(!app->esp_at) return;
    esp_at_send(app->esp_at, "info");
}

void foxdeflock_check_drain(FoxDeFlockApp* app) {
    if(!app->esp_at || app->state != FoxDeFlockStateEsp32Check) return;

    for(int i = 0; i < 32; i++) {
        if(!esp_at_receive(app->esp_at, &app->rx_msg, 0)) break;
        if(strcmp(app->rx_msg.line, "Fox ESP32 Firmware") == 0) {
            app->esp32_probe_ok = true;
            return;
        }
    }
}

void foxdeflock_scan_pump(FoxDeFlockApp* app) {
    if(!app->esp_at || app->state != FoxDeFlockStateScanning) return;

    uint32_t now = furi_get_tick();
    if(app->mode_started_tick != 0 &&
       (now - app->mode_started_tick) < furi_ms_to_ticks(SCAN_MODE_PERIOD_MS)) {
        return;
    }

    app->scan_mode = (FoxDeFlockScanMode)((app->scan_mode + 1) % SCAN_MODE_COUNT);
    app->mode_started_tick = now;
    esp_at_send(app->esp_at, SCAN_MODE_CMD[app->scan_mode]);
}

static bool parse_mac_colon(const char* p, uint8_t mac[6]) {
    for(int i = 0; i < 6; i++) {
        if(!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) return false;
        char byte_str[3] = {p[0], p[1], 0};
        mac[i] = (uint8_t)strtoul(byte_str, NULL, 16);
        p += 2;
        if(i < 5) {
            if(*p != ':') return false;
            p++;
        }
    }
    return true;
}

static const char* find_field(const char* line, const char* key) {
    return strstr(line, key);
}

static bool parse_int_field(const char* line, const char* key, int* out) {
    const char* p = find_field(line, key);
    if(!p) return false;
    p += strlen(key);
    *out = atoi(p);
    return true;
}

static bool parse_quoted_field(const char* line, const char* key, char* out, size_t cap) {
    const char* p = find_field(line, key);
    if(!p) return false;
    p += strlen(key);
    if(*p != '"') return false;
    p++;
    size_t i = 0;
    while(*p && *p != '"' && i < cap - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static bool parse_hex_field(const char* line, const char* key, uint8_t* out, size_t cap, size_t* out_len) {
    const char* p = find_field(line, key);
    if(!p) return false;
    p += strlen(key);
    size_t n = 0;
    while(isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]) && n < cap) {
        char byte_str[3] = {p[0], p[1], 0};
        out[n++] = (uint8_t)strtoul(byte_str, NULL, 16);
        p += 2;
    }
    *out_len = n;
    return n > 0;
}

static FoxDeFlockHit* find_or_alloc_hit(FoxDeFlockApp* app, const uint8_t mac[6]) {
    for(uint16_t i = 0; i < app->hit_count; i++) {
        if(app->hits[i].used && memcmp(app->hits[i].mac, mac, 6) == 0) {
            return &app->hits[i];
        }
    }
    if(app->hit_count < FOXDEFLOCK_MAX_HITS) {
        FoxDeFlockHit* h = &app->hits[app->hit_count++];
        memset(h, 0, sizeof(*h));
        memcpy(h->mac, mac, 6);
        h->used = true;
        h->first_seen_tick = furi_get_tick();
        return h;
    }
    uint16_t oldest = 0;
    for(uint16_t i = 1; i < app->hit_count; i++) {
        if(app->hits[i].last_seen_tick < app->hits[oldest].last_seen_tick) oldest = i;
    }
    FoxDeFlockHit* h = &app->hits[oldest];
    memset(h, 0, sizeof(*h));
    memcpy(h->mac, mac, 6);
    h->used = true;
    h->first_seen_tick = furi_get_tick();
    return h;
}

static bool foxdeflock_wifi_confidence(
    const uint8_t mac[6],
    bool have_ssid,
    const char* ssid,
    FlockConfidence* conf_out) {
    FlockConfidence ssid_conf = have_ssid ? flock_ssid_confidence(ssid) : FlockConfidenceNone;
    bool oui = flock_oui_match(mac) || soundthinking_oui_match(mac);

    if(oui && ssid_conf != FlockConfidenceNone) {
        *conf_out = (ssid_conf > FlockConfidencePossible) ? ssid_conf : FlockConfidencePossible;
        return true;
    } else if(ssid_conf != FlockConfidenceNone) {
        *conf_out = ssid_conf;
        return true;
    }
    return false;
}

static void handle_wifi_line(FoxDeFlockApp* app, const char* line) {
    uint8_t mac[6];
    if(!parse_mac_colon(strchr(line, ':') + 1, mac)) return;

    int ch = 0, rssi = 0;
    parse_int_field(line, "ch:", &ch);
    parse_int_field(line, "rssi:", &rssi);
    char ssid[33];
    bool have_ssid = parse_quoted_field(line, "ssid:", ssid, sizeof(ssid)) && ssid[0] != '\0';

    FlockConfidence conf;
    if(!foxdeflock_wifi_confidence(mac, have_ssid, ssid, &conf)) return;

    FoxDeFlockHit* h = find_or_alloc_hit(app, mac);
    h->confidence = conf;
    h->dev_class = flock_class_from_mac(mac);
    h->source = FoxDeFlockSourceWifi;
    h->rssi = (int8_t)rssi;
    h->channel = (uint8_t)ch;
    h->last_seen_tick = furi_get_tick();
    h->sightings++;
    if(have_ssid) {
        h->have_ssid = true;
        strlcpy(h->ssid, ssid, sizeof(h->ssid));
    }
    app->wifi_lines_seen++;
}

static void handle_ble_line(FoxDeFlockApp* app, const char* line) {
    const char* addr_start = strstr(line, "FLOCK:");
    if(!addr_start) return;
    addr_start += strlen("FLOCK:");
    uint8_t mac[6];
    if(!parse_mac_colon(addr_start, mac)) return;

    int rssi = 0;
    parse_int_field(line, "rssi:", &rssi);
    char name[32];
    bool have_name = parse_quoted_field(line, "name:", name, sizeof(name)) && name[0] != '\0';
    uint8_t mfg[32];
    size_t mfg_len = 0;
    parse_hex_field(line, "mfg:", mfg, sizeof(mfg), &mfg_len);

    uint16_t company = 0;
    if(mfg_len >= 2) company = (uint16_t)(mfg[0] | (mfg[1] << 8));

    FlockConfidence conf =
        flock_ble_confidence(company, have_name ? name : NULL, false);

    FoxDeFlockHit* h = find_or_alloc_hit(app, mac);
    h->confidence = conf;
    h->dev_class = FlockClassAlpr;
    h->source = FoxDeFlockSourceBle;
    h->rssi = (int8_t)rssi;
    h->channel = 0;
    h->last_seen_tick = furi_get_tick();
    h->sightings++;
    if(have_name) strlcpy(h->ble_name, name, sizeof(h->ble_name));
    app->ble_lines_seen++;
}

void foxdeflock_scan_drain(FoxDeFlockApp* app) {
    if(!app->esp_at) return;

    for(int i = 0; i < 32; i++) {
        if(!esp_at_receive(app->esp_at, &app->rx_msg, 0)) break;
        const char* line = app->rx_msg.line;
        if(strncmp(line, "BEACON:", 7) == 0 || strncmp(line, "PROBEREQ:", 9) == 0 ||
           strncmp(line, "PROBERESP:", 10) == 0) {
            handle_wifi_line(app, line);
        } else if(strncmp(line, "TAG:FLOCK:", 10) == 0) {
            handle_ble_line(app, line);
        }
    }
}
