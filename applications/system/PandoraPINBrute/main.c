/*
 * Pandora PIN Brute - brute-forces the numeric PIN of a Pandora DXL-5000 (PIC18)
 * keyfob firmware. It does NOT run the full emulator: it boots the firmware once
 * to the point where the PIN is checked, snapshots the CPU, then for every code
 * 0000..9999 restores the snapshot, injects the digits, and runs ONLY the PIN
 * validation routine (~25 instructions) until it reports ACCEPT. On a hit it shows
 * the PIN and writes "<firmware name>_PIN.txt" next to the firmware file.
 *
 * Reuses the PandoraPIC PIC18 interpreter (lib/pic18core) with the same SD flash
 * demand-paging as PandoraPIC, so large images never need full-RAM buffers.
 *
 * The PIN-check addresses are build-specific; v1 ships the DXL-5000 "full"
 * (PIN 2552) layout. mario2 has no PIN. HOST-VERIFIED (byte-exact engine, unique
 * correct PIN for both .hex and .bin); NOT yet hardware-tested.
 */
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>

#include "lib/pic18core/pic18_core.h"

#define TAG "PandoraPINBrute"

/* ---- flash demand-paging (same shape as PandoraPIC) ---- */
#define PB_PAGE 1024u
#define PB_PAGES 24u
#define PB_FLASH_TEMP "/ext/pandora_pinbrute_flash.tmp"
#define PB_PROG_MAX 0x1B000u

/* boot handshake SFRs */
#define BTN_REG 0xF8Eu
#define SFR_PORTB 0xF81u
#define SFR_LATB 0xF8Bu

/* ---- per-firmware PIN layout (build-specific) ---- */
typedef struct {
    const char* name;
    bool has_pin;
    uint32_t cinit_wait; /* boot target PC (PORTB.1 handshake) */
    uint32_t pin_validate, pin_accept, pin_reject, pin_ret;
    uint16_t pin_lo[4], pin_hi[4]; /* RAM addrs of the 4 entered digits */
    uint16_t pin_ulo, pin_uhi; /* unlock flag to clear before each try */
} PinProfile;

static const PinProfile PROFILE_FULL = {
    .name = "DXL-5000 full",
    .has_pin = true,
    .cinit_wait = 0x1712u,
    .pin_validate = 0x18D76u,
    .pin_accept = 0x18DA8u,
    .pin_reject = 0x18DE2u,
    .pin_ret = 0x01FFFEu,
    .pin_lo = {0x1D9, 0x1DB, 0x1DD, 0x1DF},
    .pin_hi = {0x1DA, 0x1DC, 0x1DE, 0x1E0},
    .pin_ulo = 0x1D7,
    .pin_uhi = 0x1D8,
};
static const PinProfile PROFILE_MARIO2 = {
    .name = "DXL-5000 v2 (mario2)",
    .has_pin = false,
};

typedef enum {
    BF_IDLE,
    BF_BOOTING,
    BF_RUNNING,
    BF_FOUND,
    BF_NOTFOUND,
    BF_NOPIN,
    BF_ERROR,
} BfState;

typedef struct {
    Gui* gui;
    ViewPort* vp;
    Storage* storage;
    FuriString* fw_path;

    Pic18Cpu* cpu;
    Pic18Cpu* snap;

    /* paging */
    File* flash_file;
    uint32_t flash_bk_size;
    bool flash_temp;
    uint8_t* page_cache;
    uint32_t page_tag[PB_PAGES];
    uint32_t cur_page;
    const uint8_t* cur_ptr;

    const PinProfile* prof;

    FuriThread* worker;
    volatile BfState state;
    volatile int cur_pin; /* last code tried (0..9999) */
    volatile int found_pin; /* -1 = none yet */
    volatile bool via_read; /* PIN came from the code literals, not a sweep */
    volatile bool cancel;
    volatile bool exit_app;
    char saved_path[160];
    char err[48];
    bool is_arm;          /* firmware is ARM (knock) vs PIC (PIN) */
    char knock_str[48];   /* recovered ARM knock, e.g. "UP OK OK UP UP DOWN" */
} App;

/* =============================== paging ================================= */
static const uint8_t* pb_page_ptr(App* app, uint32_t page) {
    if(page == app->cur_page) return app->cur_ptr;
    uint32_t slot = page % PB_PAGES;
    uint8_t* ptr = app->page_cache + slot * PB_PAGE;
    if(app->page_tag[slot] != page) {
        memset(ptr, 0xFF, PB_PAGE);
        uint32_t off = page * PB_PAGE;
        if(app->flash_file && off < app->flash_bk_size) {
            uint32_t n = PB_PAGE;
            if(off + n > app->flash_bk_size) n = app->flash_bk_size - off;
            storage_file_seek(app->flash_file, off, true);
            storage_file_read(app->flash_file, ptr, (uint16_t)n);
        }
        app->page_tag[slot] = page;
    }
    app->cur_page = page;
    app->cur_ptr = ptr;
    return ptr;
}
static uint8_t pb_code_byte(App* app, uint32_t a) {
    const uint8_t* pg = pb_page_ptr(app, a / PB_PAGE);
    return pg[a % PB_PAGE];
}
static uint8_t pb_prog_read(Pic18Cpu* cpu, uint32_t a, void* ctx) {
    (void)cpu;
    return pb_code_byte((App*)ctx, a);
}
static void pb_prog_write(Pic18Cpu* cpu, uint32_t a, uint8_t v, void* ctx) {
    (void)cpu;
    (void)a;
    (void)v;
    (void)ctx;
}
static void pb_free_flash(App* app) {
    if(app->flash_file) {
        storage_file_close(app->flash_file);
        storage_file_free(app->flash_file);
        app->flash_file = NULL;
    }
    if(app->flash_temp) {
        storage_common_remove(app->storage, PB_FLASH_TEMP);
        app->flash_temp = false;
    }
    if(app->page_cache) {
        free(app->page_cache);
        app->page_cache = NULL;
    }
}

/* =============================== .hex parse ============================= */
static int pb_hexbyte(const char* s) {
    int hi = (unsigned char)s[0], lo = (unsigned char)s[1];
    if(hi >= '0' && hi <= '9')
        hi -= '0';
    else {
        hi |= 0x20;
        if(hi >= 'a' && hi <= 'f')
            hi = hi - 'a' + 10;
        else
            return -1;
    }
    if(lo >= '0' && lo <= '9')
        lo -= '0';
    else {
        lo |= 0x20;
        if(lo >= 'a' && lo <= 'f')
            lo = lo - 'a' + 10;
        else
            return -1;
    }
    return (hi << 4) | lo;
}

/* top code address (leaves file pos at 0) */
static uint32_t pb_scan_size(File* f) {
    static uint8_t sbuf[512];
    static char sline[600];
    uint8_t first = 0;
    if(storage_file_read(f, &first, 1) != 1) {
        storage_file_seek(f, 0, true);
        return PB_PROG_MAX;
    }
    storage_file_seek(f, 0, true);
    if(first != ':') {
        uint64_t sz = storage_file_size(f);
        return (sz > 0 && sz < PB_PROG_MAX) ? (uint32_t)sz : PB_PROG_MAX;
    }
    uint32_t hi = 0, ext = 0;
    size_t lp = 0, rd;
    bool done = false;
    while(!done && (rd = storage_file_read(f, sbuf, sizeof(sbuf))) > 0) {
        for(size_t i = 0; i < rd; i++) {
            char ch = (char)sbuf[i];
            if(ch == ':') {
                lp = 0;
            } else if(ch == '\n' || ch == '\r') {
                if(lp >= 10) {
                    int cnt = pb_hexbyte(sline);
                    int ah = pb_hexbyte(sline + 2);
                    int al = pb_hexbyte(sline + 4);
                    int rt = pb_hexbyte(sline + 6);
                    if(cnt >= 0 && ah >= 0 && al >= 0 && rt >= 0) {
                        uint32_t a = ((uint32_t)ah << 8) | (uint32_t)al;
                        if(rt == 0x04) {
                            int b0 = pb_hexbyte(sline + 8), b1 = pb_hexbyte(sline + 10);
                            if(b0 >= 0 && b1 >= 0) ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 16;
                        } else if(rt == 0x02) {
                            int b0 = pb_hexbyte(sline + 8), b1 = pb_hexbyte(sline + 10);
                            if(b0 >= 0 && b1 >= 0) ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 4;
                        } else if(rt == 0x00) {
                            uint32_t start = ext + a;
                            uint32_t end = start + (uint32_t)cnt;
                            if(start < PB_PROG_MAX && end > hi) hi = end;
                        } else if(rt == 0x01) {
                            done = true;
                        }
                    }
                }
                lp = 0;
            } else {
                if(lp < sizeof(sline) - 1) sline[lp++] = ch;
            }
        }
    }
    storage_file_seek(f, 0, true);
    return (hi > 0 && hi < PB_PROG_MAX) ? hi : PB_PROG_MAX;
}

/* stream .hex -> flat code image + config table */
static bool pb_hex_to_flat(Pic18Cpu* cpu, File* in, File* out, uint32_t codesz) {
    static uint8_t fillbuf[512];
    memset(fillbuf, 0xFF, sizeof(fillbuf));
    storage_file_seek(out, 0, true);
    uint32_t left = codesz;
    while(left) {
        uint32_t n = left < sizeof(fillbuf) ? left : sizeof(fillbuf);
        if(storage_file_write(out, fillbuf, (uint16_t)n) != n) return false;
        left -= n;
    }
    static uint8_t chunk[512];
    static char line[600];
    size_t linepos = 0, rd;
    uint32_t ext = 0;
    bool done = false;
    storage_file_seek(in, 0, true);
    while(!done && (rd = storage_file_read(in, chunk, sizeof(chunk))) > 0) {
        for(size_t i = 0; i < rd; i++) {
            char ch = (char)chunk[i];
            if(ch == ':') {
                linepos = 0;
            } else if(ch == '\n' || ch == '\r') {
                if(linepos >= 10) {
                    int count = pb_hexbyte(line);
                    int ah = pb_hexbyte(line + 2);
                    int al = pb_hexbyte(line + 4);
                    int rtype = pb_hexbyte(line + 6);
                    if(count < 0 || ah < 0 || al < 0 || rtype < 0) return false;
                    uint32_t addr = ((uint32_t)ah << 8) | (uint32_t)al;
                    const char* data = line + 8;
                    if(rtype == 0x00) {
                        uint32_t base = ext + addr;
                        if(base >= 0x300000u) {
                            for(int k = 0; k < count; k++) {
                                int b = pb_hexbyte(data + k * 2);
                                if(b < 0) return false;
                                if(cpu->config_count < 64) {
                                    cpu->config_addr[cpu->config_count] = base + (uint32_t)k;
                                    cpu->config_val[cpu->config_count] = (uint8_t)b;
                                    cpu->config_count++;
                                }
                            }
                        } else {
                            uint8_t rec[256];
                            int m = 0;
                            for(int k = 0; k < count && m < (int)sizeof(rec); k++) {
                                int b = pb_hexbyte(data + k * 2);
                                if(b < 0) return false;
                                rec[m++] = (uint8_t)b;
                            }
                            if(base < codesz) {
                                uint32_t n = (uint32_t)m;
                                if(base + n > codesz) n = codesz - base;
                                storage_file_seek(out, base, true);
                                if(storage_file_write(out, rec, (uint16_t)n) != n) return false;
                            }
                        }
                    } else if(rtype == 0x01) {
                        done = true;
                        break;
                    } else if(rtype == 0x04) {
                        int b0 = pb_hexbyte(data), b1 = pb_hexbyte(data + 2);
                        if(b0 < 0 || b1 < 0) return false;
                        ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 16;
                    } else if(rtype == 0x02) {
                        int b0 = pb_hexbyte(data), b1 = pb_hexbyte(data + 2);
                        if(b0 < 0 || b1 < 0) return false;
                        ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 4;
                    }
                }
                linepos = 0;
            } else {
                if(linepos < sizeof(line) - 1) line[linepos++] = ch;
            }
        }
    }
    return true;
}

/* load firmware (paged). returns code size, or 0 on failure. */
static uint32_t pb_load(App* app, const char* path) {
    File* f = storage_file_alloc(app->storage);
    uint32_t codesz = 0;
    bool keep_f = false;
    bool ok = false;
    do {
        if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        uint64_t fsize = storage_file_size(f);
        if(fsize == 0 || fsize > 4u * 1024u * 1024u) break;

        codesz = pb_scan_size(f);
        app->page_cache = (uint8_t*)malloc(PB_PAGES * PB_PAGE);
        if(!app->page_cache) {
            codesz = 0;
            break;
        }
        for(uint32_t i = 0; i < PB_PAGES; i++) app->page_tag[i] = 0xFFFFFFFFu;
        app->cur_page = 0xFFFFFFFFu;
        app->cur_ptr = NULL;
        app->flash_bk_size = codesz;
        app->cpu->config_count = 0;

        uint8_t first = 0;
        if(storage_file_read(f, &first, 1) != 1) {
            codesz = 0;
            break;
        }
        storage_file_seek(f, 0, true);

        if(first == ':') {
            File* out = storage_file_alloc(app->storage);
            if(!storage_file_open(out, PB_FLASH_TEMP, FSAM_READ_WRITE, FSOM_CREATE_ALWAYS)) {
                storage_file_free(out);
                codesz = 0;
                break;
            }
            if(!pb_hex_to_flat(app->cpu, f, out, codesz)) {
                storage_file_close(out);
                storage_file_free(out);
                codesz = 0;
                break;
            }
            app->flash_file = out;
            app->flash_temp = true;
        } else {
            app->flash_file = f;
            app->flash_temp = false;
            keep_f = true;
            if((uint32_t)fsize < codesz) app->flash_bk_size = (uint32_t)fsize;
        }
        pic18_set_prog(app->cpu, NULL, codesz);
        app->cpu->on_prog = pb_prog_read;
        app->cpu->prog_ctx = app;
        app->cpu->on_prog_write = pb_prog_write;
        app->cpu->progw_ctx = app;
        ok = true;
    } while(false);
    if(!keep_f) {
        storage_file_close(f);
        storage_file_free(f);
    }
    return ok ? codesz : 0;
}

static bool pb_name_has(const char* hay, const char* needle) {
    if(!hay) return false;
    for(const char* p = hay; *p; p++) {
        const char* a = p;
        const char* b = needle;
        while(*a && *b && ((*a | 0x20) == (*b | 0x20))) {
            a++;
            b++;
        }
        if(!*b) return true;
    }
    return false;
}
static const PinProfile* pb_pick(const char* path, Pic18Cpu* cpu, uint32_t codesz) {
    for(uint16_t i = 0; i < cpu->config_count; i++) {
        if(cpu->config_addr[i] == 0x300004u)
            return (cpu->config_val[i] == 0xA2u) ? &PROFILE_FULL : &PROFILE_MARIO2;
    }
    if(pb_name_has(path, "full")) return &PROFILE_FULL;
    if(pb_name_has(path, "mario2") || pb_name_has(path, "v2")) return &PROFILE_MARIO2;
    return (codesz >= 0x19000u) ? &PROFILE_FULL : &PROFILE_MARIO2;
}

/* =============================== engine ================================ */
static int pb_port(Pic18Cpu* c, uint16_t ad, void* ctx) {
    (void)ctx;
    if(ad == BTN_REG) return 0xFF; /* no buttons held */
    if(ad == SFR_PORTB) {
        uint8_t latb = pic18_read_ram(c, SFR_LATB);
        return (int)(latb & (uint8_t)~0x02u); /* PORTB.1 handshake */
    }
    return -1;
}

static bool pb_boot(App* app) {
    pic18_reset(app->cpu);
    uint64_t budget = 3000000;
    while(app->cpu->pc != app->prof->cinit_wait && budget > 0) {
        pic18_step(app->cpu);
        budget--;
        if(app->cpu->halted) break;
    }
    return app->cpu->pc == app->prof->cinit_wait;
}

/* 1 accept, 2 reject, 0 no-verdict/budget */
static int pb_try(App* app, int pin) {
    const PinProfile* p = app->prof;
    memcpy(app->cpu, app->snap, sizeof(Pic18Cpu));
    int d[4] = {(pin / 1000) % 10, (pin / 100) % 10, (pin / 10) % 10, pin % 10};
    for(int i = 0; i < 4; i++) {
        pic18_write_ram(app->cpu, p->pin_lo[i], (uint8_t)(d[i] & 0xFF));
        pic18_write_ram(app->cpu, p->pin_hi[i], 0);
    }
    pic18_write_ram(app->cpu, p->pin_ulo, 0);
    pic18_write_ram(app->cpu, p->pin_uhi, 0);
    app->cpu->halted = 0;
    app->cpu->stkptr = 0;
    app->cpu->ram[PIC18_SFR_BSR] = 0;
    pic18_push(app->cpu, p->pin_ret);
    app->cpu->pc = p->pin_validate;
    uint64_t budget = 200000;
    while(budget-- > 0 && !app->cpu->halted) {
        uint32_t pc = app->cpu->pc;
        if(pc == p->pin_accept) return 1;
        if(pc == p->pin_reject) return 2;
        if(pc == p->pin_ret) return 0;
        pic18_step(app->cpu);
    }
    return 0;
}

/* Writes `content` to <firmware stem><suffix> beside the firmware. */
static void cracker_save(App* app, const char* suffix, const char* content) {
    const char* src = furi_string_get_cstr(app->fw_path);
    int len = (int)strlen(src), slash = -1, dot = -1;
    for(int i = 0; i < len; i++)
        if(src[i] == '/') slash = i;
    for(int i = len - 1; i > slash; i--) {
        if(src[i] == '.') {
            dot = i;
            break;
        }
    }
    int stem_end = (dot > slash) ? dot : len;
    char path[176];
    int cap = (int)sizeof(path) - ((int)strlen(suffix) + 1);
    if(stem_end > cap) stem_end = cap;
    memcpy(path, src, (size_t)stem_end);
    snprintf(path + stem_end, sizeof(path) - (size_t)stem_end, "%s", suffix);

    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, content, (uint16_t)strlen(content));
        storage_file_close(f);
        strncpy(app->saved_path, path, sizeof(app->saved_path) - 1);
    }
    storage_file_free(f);
}

static void pb_save(App* app) {
    char buf[32];
    snprintf(buf, sizeof(buf), "PIN: %04d\n", app->found_pin);
    cracker_save(app, "_PIN.txt", buf);
}

/* Fast path: the PIN is compared digit-by-digit against hardcoded literals in
 * the validate routine ("MOVF digit,w; SUBLW k"). Scan the code from pin_validate
 * and read those 4 SUBLW operands directly - no 10000-try sweep. Returns the PIN
 * (0..9999), or -1 if the code does not match this pattern (then we sweep). */
static int pb_read_literals(App* app) {
    const PinProfile* p = app->prof;
    uint32_t pc = p->pin_validate;
    uint32_t end = pc + 128u; /* the compare block is tiny; small window */
    int dig[4];
    int found = 0;
    uint16_t prev = 0;
    bool have_prev = false;
    while(pc + 1 < end && found < 4) {
        uint16_t w = (uint16_t)(pb_code_byte(app, pc) | (pb_code_byte(app, pc + 1) << 8));
        if(have_prev) {
            uint8_t ph = (uint8_t)(prev >> 8);
            bool is_movf_w = (ph == 0x50u || ph == 0x51u); /* MOVF f,W (a=0/1) */
            if(is_movf_w && (prev & 0xFFu) == (p->pin_lo[found] & 0xFFu) &&
               (w & 0xFF00u) == 0x0800u) { /* SUBLW k */
                dig[found] = (int)(w & 0xFFu);
                found++;
            }
        }
        prev = w;
        have_prev = true;
        pc += 2;
    }
    if(found != 4) return -1;
    for(int i = 0; i < 4; i++)
        if(dig[i] > 9) return -1; /* not a plain decimal digit -> bail to sweep */
    return dig[0] * 1000 + dig[1] * 100 + dig[2] * 10 + dig[3];
}

static int32_t pb_worker(void* ctx) {
    App* app = (App*)ctx;
    app->state = BF_BOOTING;
    view_port_update(app->vp);
    if(!pb_boot(app)) {
        strncpy(app->err, "boot failed", sizeof(app->err) - 1);
        app->state = BF_ERROR;
        view_port_update(app->vp);
        return 0;
    }
    memcpy(app->snap, app->cpu, sizeof(Pic18Cpu));
    app->state = BF_RUNNING;
    view_port_update(app->vp);

    /* Fast path: read the hardcoded PIN straight from the validate code, then
     * verify it actually ACCEPTs. Falls back to the full sweep otherwise. */
    int quick = pb_read_literals(app);
    if(quick >= 0 && pb_try(app, quick) == 1) {
        app->cur_pin = quick;
        app->found_pin = quick;
        app->via_read = true;
    } else {
        for(int pin = 0; pin <= 9999 && !app->cancel; pin++) {
            app->cur_pin = pin;
            if(pb_try(app, pin) == 1) {
                app->found_pin = pin;
                break;
            }
            if((pin & 0x3F) == 0) view_port_update(app->vp);
        }
    }
    if(app->cancel) {
        app->state = BF_IDLE;
    } else if(app->found_pin >= 0) {
        pb_save(app);
        app->state = BF_FOUND;
    } else {
        app->state = BF_NOTFOUND;
    }
    view_port_update(app->vp);
    return 0;
}

/* ===================== ARM knock recovery (static) ===================== */
/* B_ indices match PandoraARM's enum + the _KNOCK.txt token names. */
enum { AK_UP = 0, AK_DOWN, AK_OK, AK_BACK, AK_B5, AK_B6 };
static const char* ak_name(int b) {
    switch(b) {
    case AK_UP: return "UP";
    case AK_DOWN: return "DOWN";
    case AK_OK: return "OK";
    case AK_BACK: return "BACK";
    case AK_B5: return "B5";
    case AK_B6: return "B6";
    default: return "?";
    }
}
static int ak_portpin(int port, int pin) {
    if(port == 5 && pin == 7) return AK_UP;
    if(port == 5 && pin == 5) return AK_DOWN;
    if(port == 2 && pin == 9) return AK_OK;
    if((port == 2 && pin == 7) || (port == 0 && pin == 15)) return AK_BACK;
    if(port == 0 && pin == 1) return AK_B5;
    if(port == 0 && pin == 6) return AK_B6;
    return -1;
}
/* known ARM scanner entry points (scan_head) to try */
static const uint32_t ARM_SCAN_HEADS[] = {0xFDFAu, 0x112E4u};

/* read [addr, addr+len) of the firmware code image into buf (0xFF-filled).
 * .bin -> flat file offset; .hex -> by absolute address. */
static void cracker_read_window(App* app, uint32_t addr, uint8_t* buf, uint32_t len) {
    memset(buf, 0xFF, len);
    File* f = storage_file_alloc(app->storage);
    const char* path = furi_string_get_cstr(app->fw_path);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t first = 0;
        if(storage_file_read(f, &first, 1) == 1) {
            storage_file_seek(f, 0, true);
            if(first != ':') {
                if(storage_file_size(f) > addr) {
                    storage_file_seek(f, addr, true);
                    uint32_t got = 0;
                    while(got < len) {
                        uint16_t want = (uint16_t)((len - got) > 512 ? 512 : (len - got));
                        uint16_t r = storage_file_read(f, buf + got, want);
                        if(r == 0) break;
                        got += r;
                    }
                }
            } else {
                static uint8_t chunk[512];
                static char ln[600];
                size_t lp = 0, rd;
                uint32_t ext = 0;
                bool done = false;
                while(!done && (rd = storage_file_read(f, chunk, sizeof(chunk))) > 0) {
                    for(size_t i = 0; i < rd; i++) {
                        char ch = (char)chunk[i];
                        if(ch == ':') {
                            lp = 0;
                        } else if(ch == '\n' || ch == '\r') {
                            if(lp >= 10) {
                                int cnt = pb_hexbyte(ln), ah = pb_hexbyte(ln + 2),
                                    al = pb_hexbyte(ln + 4), rt = pb_hexbyte(ln + 6);
                                if(cnt >= 0 && ah >= 0 && al >= 0 && rt >= 0) {
                                    uint32_t aa = ((uint32_t)ah << 8) | (uint32_t)al;
                                    const char* dp = ln + 8;
                                    if(rt == 0x04) {
                                        int b0 = pb_hexbyte(dp), b1 = pb_hexbyte(dp + 2);
                                        if(b0 >= 0 && b1 >= 0) ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 16;
                                    } else if(rt == 0x02) {
                                        int b0 = pb_hexbyte(dp), b1 = pb_hexbyte(dp + 2);
                                        if(b0 >= 0 && b1 >= 0) ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 4;
                                    } else if(rt == 0x00) {
                                        uint32_t base = ext + aa;
                                        for(int k = 0; k < cnt; k++) {
                                            uint32_t x = base + (uint32_t)k;
                                            if(x >= addr && x < addr + len) {
                                                int b = pb_hexbyte(dp + k * 2);
                                                if(b >= 0) buf[x - addr] = (uint8_t)b;
                                            }
                                        }
                                    } else if(rt == 0x01) {
                                        done = true;
                                    }
                                }
                            }
                            lp = 0;
                        } else {
                            if(lp < sizeof(ln) - 1) ln[lp++] = ch;
                        }
                    }
                }
            }
        }
        storage_file_close(f);
    }
    storage_file_free(f);
}

static bool cracker_is_arm(App* app) {
    uint8_t v[8];
    cracker_read_window(app, 0, v, sizeof(v));
    uint32_t sp = (uint32_t)(v[0] | (v[1] << 8) | (v[2] << 16) | ((uint32_t)v[3] << 24));
    return (sp >= 0x20000000u && sp < 0x20040000u); /* EFM32 initial SP in SRAM */
}

static int ak_rd16(const uint8_t* w, uint32_t wlen, uint32_t off, uint16_t* out) {
    if(off + 1 >= wlen) return 0;
    *out = (uint16_t)(w[off] | (w[off + 1] << 8));
    return 1;
}
static int ak_is32(uint16_t hw) {
    return (hw >> 11) >= 0x1Du;
}

/* Static recovery: pattern-match the unrolled Thumb-2 knock scanner. w[0] is the
 * byte at scan_head; offsets (incl. branch targets) are window-relative. */
static int arm_extract_knock(const uint8_t* w, uint32_t wlen, uint8_t* out, int maxout) {
    uint32_t a = 0, disp = 0;
    int have_disp = 0;
    while(a + 1 < wlen) {
        uint16_t hw;
        ak_rd16(w, wlen, a, &hw);
        if(hw == 0xE9DDu) {
            uint16_t hw2;
            if(ak_rd16(w, wlen, a + 2, &hw2) && (hw2 & 0xFF00u) == 0x0100u) {
                disp = a;
                have_disp = 1;
                break;
            }
        }
        a += ak_is32(hw) ? 4 : 2;
    }
    if(!have_disp) return 0;
    uint32_t handlers[16];
    int nh = 0;
    a = disp + 4;
    uint32_t dend = disp + 0x80;
    if(dend > wlen) dend = wlen;
    while(a + 1 < dend && nh < 16) {
        uint16_t hw;
        ak_rd16(w, wlen, a, &hw);
        if((hw & 0xFF00u) == 0xD000u) { /* beq (T1) */
            int8_t off = (int8_t)(hw & 0xFF);
            long t = (long)a + 4 + (long)off * 2;
            if(t >= 0 && (uint32_t)t < wlen) handlers[nh++] = (uint32_t)t;
            a += 2;
            continue;
        }
        if((hw & 0xF800u) == 0xE000u && nh > 0) break; /* unconditional B ends the chain */
        a += ak_is32(hw) ? 4 : 2;
    }
    if(nh == 0) return 0;
    int cnt = 0;
    for(int h = 0; h < nh && cnt < maxout; h++) {
        uint32_t p = handlers[h];
        int r0 = -1, r1 = -1, found = 0;
        for(int step = 0; step < 16 && p + 1 < wlen; step++) {
            uint16_t hw;
            ak_rd16(w, wlen, p, &hw);
            if((hw & 0xF800u) == 0x2000u) { /* movs rd,#imm8 */
                int rd = (hw >> 8) & 7, imm = hw & 0xFF;
                if(rd == 0) r0 = imm;
                else if(rd == 1) r1 = imm;
                p += 2;
                continue;
            }
            if(hw == 0x4608u) { /* mov r0,r1 */
                r0 = r1;
                p += 2;
                continue;
            }
            if((hw & 0xF800u) == 0xF000u) { /* bl (read_gpio) */
                found = 1;
                break;
            }
            p += ak_is32(hw) ? 4 : 2;
        }
        int btn = (found && r0 >= 0 && r1 >= 0) ? ak_portpin(r0, r1) : -1;
        if(btn < 0) break; /* terminal / non-button handler -> end of knock */
        out[cnt++] = (uint8_t)btn;
    }
    return cnt;
}

static void cracker_crack_arm(App* app) {
    static uint8_t win[0x400];
    uint8_t seq[16];
    int n = 0;
    for(unsigned i = 0; i < sizeof(ARM_SCAN_HEADS) / sizeof(ARM_SCAN_HEADS[0]); i++) {
        cracker_read_window(app, ARM_SCAN_HEADS[i], win, sizeof(win));
        int k = arm_extract_knock(win, sizeof(win), seq, 16);
        if(k >= 4) {
            n = k;
            break;
        }
    }
    if(n >= 4) {
        int off = 0;
        app->knock_str[0] = 0;
        for(int i = 0; i < n; i++) {
            int w = snprintf(app->knock_str + off, sizeof(app->knock_str) - (size_t)off, "%s%s",
                             ak_name(seq[i]), (i + 1 < n) ? " " : "");
            if(w < 0 || (size_t)(off + w) >= sizeof(app->knock_str)) break;
            off += w;
        }
        char content[72];
        snprintf(content, sizeof(content), "KNOCK: %s\n", app->knock_str);
        cracker_save(app, "_KNOCK.txt", content);
        app->state = BF_FOUND;
    } else {
        app->state = BF_NOTFOUND;
    }
}

/* =============================== UI ==================================== */
static void pb_draw(Canvas* c, void* ctx) {
    App* app = (App*)ctx;
    BfState st = app->state;
    int cur = app->cur_pin;
    int found = app->found_pin;

    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 64, 7, AlignCenter, AlignCenter, "Pandora Cracker");
    canvas_set_font(c, FontSecondary);

    /* progress bar, centered */
    const int bx = 14, by = 27, bw = 100, bh = 14;
    canvas_draw_rframe(c, bx, by, bw, bh, 3);
    int total;
    if(st == BF_FOUND)
        total = 10000; /* done -> full bar */
    else if(st == BF_RUNNING)
        total = cur + 1;
    else if(st == BF_NOTFOUND)
        total = 10000;
    else
        total = 0;
    if(total < 0) total = 0;
    if(total > 10000) total = 10000;
    int fillw = (bw - 4) * total / 10000;
    if(fillw > 0) canvas_draw_box(c, bx + 2, by + 2, fillw, bh - 4);

    char line[64];
    int pct = total / 100;
    switch(st) {
    case BF_BOOTING:
        snprintf(line, sizeof(line), "Booting firmware...");
        break;
    case BF_RUNNING:
        snprintf(line, sizeof(line), "Trying %04d   %d%%", cur, pct);
        break;
    case BF_FOUND:
        if(app->is_arm)
            snprintf(line, sizeof(line), "%s", app->knock_str);
        else
            snprintf(line, sizeof(line), "PIN: %04d (%s)", found, app->via_read ? "read" : "brute");
        break;
    case BF_NOTFOUND:
        snprintf(line, sizeof(line), "%s", app->is_arm ? "No knock found" : "No PIN matched 0000-9999");
        break;
    case BF_NOPIN:
        snprintf(line, sizeof(line), "Firmware has no PIN");
        break;
    case BF_ERROR:
        snprintf(line, sizeof(line), "Error: %s", app->err);
        break;
    default:
        line[0] = 0;
        break;
    }
    canvas_draw_str_aligned(c, 64, by + bh + 9, AlignCenter, AlignCenter, line);

    const char* hint = (st == BF_RUNNING || st == BF_BOOTING) ? "Back: cancel" : "Back: exit";
    canvas_draw_str_aligned(c, 64, 62, AlignCenter, AlignBottom, hint);
}

static void pb_input(InputEvent* e, void* ctx) {
    App* app = (App*)ctx;
    if(e->type == InputTypeShort && e->key == InputKeyBack) {
        app->cancel = true;
        app->exit_app = true;
    }
}

/* =============================== main ================================== */
int32_t pandora_pin_brute_app(void* p) {
    (void)p;
    App* app = (App*)malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->found_pin = -1;
    app->state = BF_IDLE;

    app->storage = (Storage*)furi_record_open(RECORD_STORAGE);
    app->gui = (Gui*)furi_record_open(RECORD_GUI);
    DialogsApp* dialogs = (DialogsApp*)furi_record_open(RECORD_DIALOGS);
    app->fw_path = furi_string_alloc_set_str("/ext/apps_assets/pandora_pic");

    /* pick a firmware */
    DialogsFileBrowserOptions opts;
    dialog_file_browser_set_basic_options(&opts, ".hex|.bin", NULL);
    opts.base_path = "/ext";
    opts.hide_ext = false;
    bool picked = dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts);

    if(picked) {
        app->is_arm = cracker_is_arm(app);
        if(app->is_arm) {
            cracker_crack_arm(app); /* static knock recovery (instant); sets state + knock_str */
        } else {
            app->cpu = (Pic18Cpu*)malloc(sizeof(Pic18Cpu));
            app->snap = (Pic18Cpu*)malloc(sizeof(Pic18Cpu));
            pic18_init(app->cpu);
            uint32_t codesz = pb_load(app, furi_string_get_cstr(app->fw_path));
            if(codesz == 0) {
                strncpy(app->err, "load failed", sizeof(app->err) - 1);
                app->state = BF_ERROR;
            } else {
                app->cpu->on_port_read = pb_port;
                app->cpu->port_read_ctx = app;
                app->prof = pb_pick(furi_string_get_cstr(app->fw_path), app->cpu, codesz);
                app->state = app->prof->has_pin ? BF_BOOTING : BF_NOPIN;
            }
        }

        app->vp = view_port_alloc();
        view_port_draw_callback_set(app->vp, pb_draw, app);
        view_port_input_callback_set(app->vp, pb_input, app);
        gui_add_view_port(app->gui, app->vp, GuiLayerFullscreen);

        if(app->state == BF_BOOTING) {
            app->worker = furi_thread_alloc_ex("PinBrute", 4096, pb_worker, app);
            furi_thread_start(app->worker);
        }

        while(!app->exit_app) {
            view_port_update(app->vp);
            furi_delay_ms(60);
        }

        if(app->worker) {
            app->cancel = true;
            furi_thread_join(app->worker);
            furi_thread_free(app->worker);
        }
        gui_remove_view_port(app->gui, app->vp);
        view_port_free(app->vp);
        pb_free_flash(app);
        free(app->cpu);
        free(app->snap);
    }

    furi_string_free(app->fw_path);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
