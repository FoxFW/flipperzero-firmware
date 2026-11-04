/* Pandora PIC - emulator of the REAL Pandora DXL-5000 keyfob firmware (PIC18)
 * for the Flipper Zero.
 *
 * The keyfob firmware runs UNMODIFIED on top of the PIC18 interpreter
 * (lib/pic18core). This app emulates the hardware the firmware expects:
 *   - SSD1306 128x32 OLED display: its GDDRAM is reconstructed by intercepting
 *     the firmware's SW-SPI serializer (hook on send-byte + DC pin), just like
 *     the reference Python emulator (emu/pic18_tui.py). It is scaled/drawn on
 *     the Flipper's 128x64 screen.
 *   - 5 buttons (register 0xF8E, active-low) mapped to the Flipper's buttons
 *     (direct GPIO polling, like FlipperGB).
 *   - OOK radio: the firmware bit-bangs the RF. It is bridged to the Flipper's
 *     CC1101 (TX of the OOK frame captured from the bit-bang pin; RX feeding the
 *     firmware's data pin with the real RF edges).
 *
 * FLOW (v2):
 *   Phase 1 - MENU (normal GUI with view_dispatcher + submenu + number_input):
 *     * "FOB Firmware: <name>"  -> opens the file browser (.hex/.bin)
 *     * "PIN: <value>"      -> number_input (numeric PIN, default 2552)
 *     * "Launch"            -> starts the emulator
 *     The view_dispatcher BLOCKS until the user chooses Launch or exits.
 *   Phase 2 - EMULATOR (direct-draw takeover, INCOMPATIBLE with view_dispatcher):
 *     after Launch, the view_dispatcher and its views are FULLY released, and
 *     ONLY THEN is the takeover done (gui_direct_draw_acquire +
 *     gui_add_framebuffer_callback) and the emulator loop is run. On exiting the
 *     emulator the app terminates (v1: does not return to the menu).
 *
 * The file browser CANNOT be opened while the view_dispatcher runs (its event
 * loop monopolizes the GUI). The robust approach chosen and DOCUMENTED: the
 * "FOB Firmware" item sets a flag (FlowFileBrowser) and calls view_dispatcher_stop();
 * after returning from run() main opens the file browser and RE-CREATES the
 * menu. Same for "PIN" and "Launch". This way view_dispatcher and file browser /
 * direct-draw never coexist. See PandoraPIC_v2.md.
 *
 * Emulator frontend inspired by FlipperGB (direct-draw takeover, pin polling,
 * safe_malloc, mandatory per-frame yield).
 */

#include <furi.h>
#include <furi_hal.h>
/* --- Debug logging compiled out (unused; set to 1 to re-enable) --- */
#define PANDORA_DEBUG_LOG 0
#if !PANDORA_DEBUG_LOG
#undef FURI_LOG_E
#undef FURI_LOG_W
#undef FURI_LOG_I
#undef FURI_LOG_D
#undef FURI_LOG_T
/* Strip the log statements but still reference their args in a dead if(0)
 * branch: avoids -Werror=unused-* on log-only locals, keeps format strings
 * type-checked, and the branch is optimized away (no code, no overhead). */
static inline void pandora_log_sink(const char* tag, const char* fmt, ...) {
    (void)tag;
    (void)fmt;
}
#define FURI_LOG_E(...) do { if(0) pandora_log_sink(__VA_ARGS__); } while(0)
#define FURI_LOG_W(...) do { if(0) pandora_log_sink(__VA_ARGS__); } while(0)
#define FURI_LOG_I(...) do { if(0) pandora_log_sink(__VA_ARGS__); } while(0)
#define FURI_LOG_D(...) do { if(0) pandora_log_sink(__VA_ARGS__); } while(0)
#define FURI_LOG_T(...) do { if(0) pandora_log_sink(__VA_ARGS__); } while(0)
#endif
#include <furi_hal_subghz.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/number_input.h>
#include <gui/modules/widget.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include <stdlib.h>
#include <string.h>

#include "lib/pic18core/pic18_core.h"

#define TAG "PandoraPIC"

#define SCREEN_W 128
#define SCREEN_H 64
#define FB_SIZE (SCREEN_W * SCREEN_H / 8) /* 1024 bytes, page-format */

/* Keyfob SSD1306: 128 columns x 4 pages = 512 bytes (128x32) */
#define GDDRAM_COLS 128
#define GDDRAM_PAGES 4
#define GDDRAM_SIZE (GDDRAM_COLS * GDDRAM_PAGES)

#define PROG_MAX 0x1B000u /* covers up to 0x1A17B (mariofull) with margin */

/* Flash demand-paging: page the code image from SD instead of keeping it all in
 * RAM. A 24KB page cache (24 x 1KB) replaces the ~96-107KB full prog buffer. */
#define PIC_PAGE  1024u
#define PIC_PAGES 24u
#define PIC_FLASH_TEMP_PATH "/ext/pandora_pic_flash.tmp" /* .hex -> flat temp */

#define ALLOC_MARGIN 1024u
#define FRAME_US 30000 /* ~33 Hz UI refresh; the keyfob is not a game */
/* Blank-screen watchdog: if the emulated OLED shows no real content within
 * STATUS_GRACE_MS of launch, show an explanatory status screen instead of a
 * bare black screen (a locked/blank keyfob should never look like a crash). */
#define STATUS_GRACE_MS 3000u
#define STATUS_CONTENT_BYTES 6 /* >= this many lit framebuffer bytes = real content */

#define PIN_DEFAULT 2552

/* --- Pandora keyfob periodic timer tick (drives the OLED refresh) ---------
 * The DXL-5000 firmware refreshes the OLED and runs its scheduler from a
 * periodic timer interrupt. That interrupt is dispatched through the firmware's
 * own software flags in bank-E GPR (not INTCON/PIE): an enable mask at
 * TICK_ENABLE_REG and a pending mask at TICK_PENDING_REG, with the ISR at the
 * high vector 0x0008. On real hardware TMR0 overflow raises the pending bit;
 * the firmware only arms TMR0 from inside its first tick, which can never
 * arrive on its own (it waits for the tick before arming the timebase). So the
 * frontend starts TMR0, enables the tick source, and bridges each TMR0 overflow
 * to the pending flag via pic_irq_hook() (installed as the core's on_irq); the
 * core then vectors to 0x0008 whenever GIE is set, exactly like the chip.
 * Both bundled profiles (mario2, mariofull) use the same flags, bit and vector.
 * Full reverse-engineering notes in the app's CLAUDE comments file. */
#define TICK_ISR_VECTOR  0x000008u
#define TICK_ENABLE_REG  0xEC6u /* bank-E GPR: firmware interrupt-enable mask  */
#define TICK_PENDING_REG 0xECEu /* bank-E GPR: firmware interrupt-pending mask */
#define TICK_BIT         0x02u  /* source bit 1 = the display/scheduler tick   */
#define SFR_INTCON       0xFF2u
#define SFR_T0CON        0xFD5u
#define SFR_TMR0H        0xFD7u
#define SFR_TMR0L        0xFD6u
#define INTCON_TMR0IF    0x04u

/* On the Flipper malloc() does NOT return NULL: it crashes the firmware with
 * OOM. Every allocation that depends on the .hex size goes through here. */
static void* safe_malloc(size_t size) {
    if(memmgr_heap_get_max_free_block() < size + ALLOC_MARGIN) return NULL;
    return malloc(size);
}

/* ------------------------------------------------------------ HW profiles */

typedef struct {
    const char* name;
    uint32_t send_byte_pc; /* hook of the SW-SPI serializer */
    uint16_t byte_reg; /* bank4 register with the already-assembled byte */
    uint16_t dc_flag_a; /* bank1 flags: nonzero => COMMAND, zero => DATA */
    uint16_t dc_flag_b;
    bool has_pin; /* mariofull has PIN 2552 */
    /* --- boot: the XC8 runtime has a cinit that must be handled --- */
    uint8_t boot_mode; /* 0 = run-to (mariofull, PORTB.1 handshake); 1 = cinit records (mario2) */
    uint32_t cinit_wait_pc; /* mariofull: run up to here (main loop) */
    uint32_t cinit_table; /* mario2: cinit records table */
    uint32_t cinit_table_end;
    uint32_t cinit_entry; /* mario2: run up to here before applying cinit */
    uint32_t cinit_exit; /* mario2: PC after applying cinit */
    bool portb1_handshake; /* mariofull: PORTB.1 always reads 0 (ACK) */

    /* --- PIN (mariofull): validation 0x018D76 compares 2,5,5,2 --- */
    uint32_t pin_validate; /* RCALL of the validation */
    uint32_t pin_accept; /* PC of ACCEPT */
    uint32_t pin_reject; /* PC of REJECT */
    uint16_t pin_slot_lo[4]; /* 4 16-bit integers (lo) in bank1 */
    uint16_t pin_slot_hi[4];
    uint16_t pin_unlock_lo; /* "already unlocked" flag (lo,hi) */
    uint16_t pin_unlock_hi;

    /* --- OOK RF bit-bang: output pin (LAT) and input pin (PORT) --- */
    uint16_t ook_tx_lat; /* absolute LAT SFR of the OOK TX pin */
    uint8_t ook_tx_bit; /* bit of the OOK TX pin */

    /* --- #14: SSD1306 command/data (D/C) classification ---
     * dc_mode 0: read the firmware's D/C flags (dc_flag_a/dc_flag_b) at the
     *            SW-SPI serializer (legacy; correct where those flags mirror D/C).
     * dc_mode 1: classify by the serializer's grandparent caller: the display
     *            driver sends command/arg bytes from its command-list/addressing
     *            code and pixel bytes from its framebuffer-blit code, which live
     *            in well-separated program regions. COMMAND iff that caller
     *            address < dc_caller_below, else DATA. Used where the firmware
     *            drives D/C as a per-batch GPIO not observable as a byte-time
     *            flag (see claude/ findings for the mario2 analysis). */
    uint8_t dc_mode;
    uint32_t dc_caller_below;
} PicProfile;

/* mariofull: send 0x174C, byte 0x4FE, flags 0x1F6|0x1F7; boot run-to main loop
 *            0x001712 with PORTB.1 handshake; OOK TX on LATB.0.
 * mario2:    send 0x1758, byte 0x4EF, flags 0x1F7|0x1F8; boot cinit records
 *            (table 0x17530..0x175F8, entry 0x175FA, exit 0x17636); OOK TX LATB.1. */
static const PicProfile PROFILE_FULL = {
    .name = "mariofull",
    .send_byte_pc = 0x00174Cu,
    .byte_reg = 0x4FEu,
    .dc_flag_a = 0x1F6u,
    .dc_flag_b = 0x1F7u,
    .has_pin = true,
    .boot_mode = 0,
    .cinit_wait_pc = 0x001712u,
    .cinit_table = 0,
    .cinit_table_end = 0,
    .cinit_entry = 0,
    .cinit_exit = 0,
    .portb1_handshake = true,
    .pin_validate = 0x018D76u,
    .pin_accept = 0x018DA8u,
    .pin_reject = 0x018DE2u,
    .pin_slot_lo = {0x1D9u, 0x1DBu, 0x1DDu, 0x1DFu},
    .pin_slot_hi = {0x1DAu, 0x1DCu, 0x1DEu, 0x1E0u},
    .pin_unlock_lo = 0x1D7u,
    .pin_unlock_hi = 0x1D8u,
    .ook_tx_lat = 0xF8Bu, /* LATB */
    .ook_tx_bit = 0u, /* LATB.0 */
    .dc_mode = 1u, /* caller-based D/C (verified: 0x1F6/0x1F7 are not the D/C line) */
    .dc_caller_below = 0x4000u, /* cmd code < 0x4000; font/bitmap blit >= 0x4600 */
};
static const PicProfile PROFILE_MARIO2 = {
    .name = "mario2",
    .send_byte_pc = 0x001758u,
    .byte_reg = 0x4EFu,
    .dc_flag_a = 0x1F7u,
    .dc_flag_b = 0x1F8u,
    .has_pin = false,
    .boot_mode = 1,
    .cinit_wait_pc = 0,
    .cinit_table = 0x017530u,
    .cinit_table_end = 0x0175F8u,
    .cinit_entry = 0x0175FAu,
    .cinit_exit = 0x017636u,
    .portb1_handshake = false,
    .pin_validate = 0,
    .pin_accept = 0,
    .pin_reject = 0,
    .pin_slot_lo = {0, 0, 0, 0},
    .pin_slot_hi = {0, 0, 0, 0},
    .pin_unlock_lo = 0,
    .pin_unlock_hi = 0,
    .ook_tx_lat = 0xF8Bu, /* LATB */
    .ook_tx_bit = 1u, /* LATB.1 */
    .dc_mode = 1u, /* caller-based D/C (its 0x1F7/0x1F8 are not the D/C line) */
    .dc_caller_below = 0x4000u, /* cmd code < 0x4000; font/bitmap blit >= 0x4600 */
};

/* ------------------------------------------------------------ app state */

enum {
    KBIT_UP = 1 << 0,
    KBIT_DOWN = 1 << 1,
    KBIT_LEFT = 1 << 2,
    KBIT_RIGHT = 1 << 3,
    KBIT_OK = 1 << 4,
    KBIT_BACK = 1 << 5,
};

/* Keyfob buttons in register 0xF8E (active-low). */
#define BTN_REG 0xF8Eu
#define BTN_B1_BIT 1 /* up / + / increment PIN digit */
#define BTN_B2_BIT 2 /* OK / confirm PIN */
#define BTN_B3_BIT 4 /* menu */
#define BTN_B5_BIT 6
#define BTN_B6_BIT 7
#define BTN_ALL_MASK ((1 << 1) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7))

/* SFR addresses used in the port read */
#define SFR_PORTB 0xF81u
#define SFR_LATB 0xF8Bu

/* --- CC1101 bridge --- */
/* TX: the firmware toggles the OOK pin (LATB.x). We capture each edge with its
 * duration (measured by cpu->cycles between toggles) in a LevelDuration ring and
 * replay it via furi_hal_subghz_start_async_tx. */
#define TX_RING_LEN 512u

/* RX: furi_hal_subghz_start_async_rx delivers (level,duration_us) per edge from
 * the IRQ. We enqueue them in a thread-safe queue and in the main loop set the
 * level of the firmware's input pin (on_port_read) with its timing. */
#define RX_QUEUE_LEN 256u

typedef struct {
    bool level;
    uint32_t duration; /* CC1101 ticks (us) */
} RfEdge;

/* PIC cycles->us conversion: the keyfob runs on the internal oscillator; the RE
 * does not fix the absolute Fosc (see RE_DYN_mario2/mariofull "real Te in us
 * DESC"). We use 4MHz => 1 instruction cycle = 1us as a base; it is adjustable
 * and only affects the time scale of the replayed OOK, not its content. */
#define PIC_CYCLE_US_NUM 1u
#define PIC_CYCLE_US_DEN 1u

typedef struct {
    /* core */
    Pic18Cpu* cpu;
    /* demand-paged program memory (see PIC_PAGE/PIC_PAGES): the code image is
     * read from SD on page faults instead of a full in-RAM buffer. */
    File* flash_file;       /* backing store: the .bin, or the .hex->flat temp */
    uint32_t flash_bk_size; /* valid code bytes in the backing store           */
    bool flash_temp;        /* backing is PIC_FLASH_TEMP_PATH (remove on exit)  */
    uint8_t* page_cache;    /* PIC_PAGES * PIC_PAGE                             */
    uint32_t page_tag[PIC_PAGES];
    uint32_t cur_page;      /* fast-path: last page served                     */
    const uint8_t* cur_ptr;
    uint32_t pg_hits, pg_misses;
    const PicProfile* prof;

    /* reconstructed SSD1306 GDDRAM */
    uint8_t gddram[GDDRAM_SIZE];      /* VISIBLE buffer (render reads this) */
    uint8_t gddram_work[GDDRAM_SIZE]; /* WORK buffer (built during a flush) */
    uint16_t gd_page;
    uint16_t gd_col;
    int gd_cmd_arg_left; /* multi-byte commands: how many args remain */

    /* Flipper framebuffer (1bpp, bit=1 => light) */
    uint8_t screen[FB_SIZE];

    Gui* gui;
    Canvas* canvas;
    FuriMutex* fb_mutex;

    /* buttons */
    uint8_t btn_mask; /* BTN_*_BIT bits currently pressed */

    /* CC1101 radio */
    bool radio_on; /* CC1101 acquired/configured */
    bool rf_tx_enabled; /* TX bridge active (captures firmware edges) */
    bool rf_rx_enabled; /* RX bridge active (injects real RF edges) */
    uint32_t frequency;

    /* TX bridge: ring of edges captured from the firmware's OOK pin */
    RfEdge tx_ring[TX_RING_LEN];
    volatile uint32_t tx_head; /* producer (main loop, on_lat_write) */
    volatile uint32_t tx_tail; /* consumer (async_tx callback) */
    bool tx_last_level; /* last level of the OOK pin */
    uint64_t tx_last_cycles; /* CPU cycles at the last edge */
    bool tx_active; /* async_tx in progress */
    uint32_t tx_frames; /* counter of logged TX frames */

    /* RX bridge: thread-safe queue fed from the CC1101 IRQ */
    FuriMessageQueue* rx_queue;
    uint16_t rx_in_port; /* PORT SFR of the firmware's RX input pin */
    uint8_t rx_in_bit; /* bit of the RX input pin */
    bool rx_cur_level; /* current level injected into the input pin */
    uint32_t rx_level_until; /* tick until which to hold rx_cur_level */
    uint32_t rx_events; /* counter of injected RX edges */

    volatile bool exit_requested; /* long-hold Back: exit the whole app */
    volatile bool back_to_menu; /* short Back / overlay Back: return to start menu */
    volatile bool menu_requested; /* in-emulator help overlay */
    bool menu_active;

    uint32_t steps_per_frame;

    /* PIN chosen in the menu (numeric; 2552 by default). */
    int32_t pin_value;
    bool pin_auto; /* pin_value was auto-detected (txt/literals), not typed */
    bool pin_entered; /* the PIN was already injected after boot */
    uint8_t pin_verdict; /* 0 n/a, 1 accept, 2 reject, 3 no-verdict (mariofull) */
    bool content_seen; /* the emulated OLED has shown real content at least once */
    bool status_active; /* status screen is up -> framebuffer blit is skipped */
    uint32_t launch_tick; /* tick at emulator launch, for the blank watchdog */

    /* --- display tuning (calibrate the OLED reconstruction on real hardware;
     * open the in-emulator overlay with Up+Down, then Up/Down/Right cycle) --- */
    uint8_t disp_dc;       /* 0 = caller-address D/C, 1 = firmware RAM D/C flag */
    uint8_t disp_colreset; /* 1 = reset column to 0 on a page cmd, 0 = keep it */
    uint8_t disp_rot;      /* 0 none, 1 H-flip, 2 V-flip, 3 180 */

    /* --- phase 1 menu (view_dispatcher) --- */
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    NumberInput* number_input;
    Widget* about; /* About screen (scrollable help text) */
    uint8_t menu_current_view; /* current phase-1 view (PandoraView; for Back routing) */
    Storage* storage; /* for reading <stem>_PIN.txt on selection */
    FuriString* fw_path; /* chosen firmware path */
    char fw_name[32]; /* short name for the label */
} AppState;

static AppState* g_app = NULL;
static volatile uint32_t s_fb_cb_inflight = 0;

/* ---------------------------------------------------- SSD1306 GDDRAM model */

/* Decodes a byte sent to the SSD1306 according to the DC level.
 * DC=0 => command (addressing/config), DC=1 => pixel data. */
/* Atomic frame commit (double buffering, like the ARM OLED fix): copy the WORK
 * buffer to the VISIBLE buffer so render_gddram never sees a half-drawn frame.
 * The SSD1306 firmware re-inits every frame starting with D5 (clock-divide), so
 * that is the frame boundary - commit the previous frame there, then clear the
 * work buffer for the fresh one. Skips an all-blank work buffer. */
static void gddram_commit(AppState* app) {
    bool any = false;
    for(int i = 0; i < GDDRAM_SIZE; i++) {
        if(app->gddram_work[i]) { any = true; break; }
    }
    if(any) memcpy(app->gddram, app->gddram_work, GDDRAM_SIZE);
    /* Do NOT clear the work buffer: the SSD1306 re-init does not clear the
     * controller GDDRAM, so the image accumulates across frames (partial updates
     * overwrite only the cells they touch, exactly like real hardware). */
}

static void gddram_feed(AppState* app, uint8_t b, bool is_cmd) {
    if(is_cmd) {
        if(app->gd_cmd_arg_left > 0) {
            /* argument of a multi-byte command: 0x21 col start/end, 0x22 page */
            app->gd_cmd_arg_left--;
            return;
        }
        if(b == 0xD5) {
            /* start of a new frame's re-init: commit the frame just finished */
            gddram_commit(app);
            app->gd_cmd_arg_left = 1; /* D5 takes 1 arg (clock divide) */
            return;
        }
        if(b >= 0xB0 && b <= 0xB7) {
            app->gd_page = b - 0xB0;
            if(app->disp_colreset) app->gd_col = 0; /* page-addressed: col->0 */
        } else if(b <= 0x0F) {
            app->gd_col = (app->gd_col & 0xF0) | b;
        } else if(b >= 0x10 && b <= 0x1F) {
            app->gd_col = (app->gd_col & 0x0F) | ((b & 0x0F) << 4);
        } else if(b == 0x20) {
            app->gd_cmd_arg_left = 1; /* addressing mode takes 1 arg */
        } else if(b == 0x21) {
            app->gd_cmd_arg_left = 2; /* col start, col end */
            app->gd_col = 0;
        } else if(b == 0x22) {
            app->gd_cmd_arg_left = 2; /* page start, page end */
            app->gd_page = 0;
        } else if(
            b == 0x81 || b == 0xA8 || b == 0xD3 || b == 0xDA || b == 0xD9 ||
            b == 0xDB || b == 0x8D || b == 0xAD) {
            app->gd_cmd_arg_left = 1; /* 1-argument commands */
        }
        /* 1-byte commands (0x40,0xA1,0xC8,0xA4,0xA6,0xAF,0xAE...) do not move pointers */
    } else {
        /* data: 8 vertical pixels of (page,col), bit0 at top -> WORK buffer */
        if(app->gd_page < GDDRAM_PAGES && app->gd_col < GDDRAM_COLS) {
            app->gddram_work[app->gd_page * GDDRAM_COLS + app->gd_col] = b;
        }
        app->gd_col++;
        if(app->gd_col >= GDDRAM_COLS) {
            app->gd_col = 0;
            app->gd_page = (app->gd_page + 1) % GDDRAM_PAGES;
        }
    }
}

/* Code hook: on entering the send-byte serializer, reads the already-assembled
 * byte and decides command-vs-data (see PicProfile.dc_mode), then feeds it into
 * the GDDRAM. This way we capture EVERYTHING the firmware "draws" untouched. */
static void code_hook(Pic18Cpu* cpu, uint32_t pc, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(pc != app->prof->send_byte_pc) return;
    uint8_t b = pic18_read_ram(cpu, app->prof->byte_reg);
    bool is_cmd;
    if(app->disp_dc == 0u) {
        /* caller-address D/C: the serializer is reached as display driver ->
         * send wrapper -> here. The wrapper's own caller (TOS-1) is the
         * command/addressing code (< dc_caller_below) for command bytes and the
         * framebuffer/font blit code (>= it) for pixel bytes. */
        uint32_t caller = (cpu->stkptr > 1) ? cpu->stack[cpu->stkptr - 2] : 0;
        is_cmd = (caller < app->prof->dc_caller_below);
    } else {
        /* firmware RAM D/C flag (the v2.0.13 method): the firmware stores its own
         * command/data state in these bank-1 flags before each byte. */
        uint8_t fa = pic18_read_ram(cpu, app->prof->dc_flag_a);
        uint8_t fb = pic18_read_ram(cpu, app->prof->dc_flag_b);
        is_cmd = (fa != 0) || (fb != 0); /* nonzero => COMMAND */
    }
    gddram_feed(app, b, is_cmd);
}

/* Interrupt-poll hook (installed as the core's on_irq). The core calls this
 * between instructions while GIE is set and no on-chip interrupt is pending.
 * We bridge a TMR0 overflow to the firmware's software tick-pending flag and,
 * when that tick is enabled and pending, ask the core to vector to the ISR. */
static int32_t pic_irq_hook(Pic18Cpu* cpu, void* ctx) {
    UNUSED(ctx);
    uint8_t intcon = pic18_read_ram(cpu, SFR_INTCON);
    if(intcon & INTCON_TMR0IF) {
        pic18_write_ram(cpu, SFR_INTCON, (uint8_t)(intcon & ~INTCON_TMR0IF));
        pic18_write_ram(
            cpu, TICK_PENDING_REG, (uint8_t)(pic18_read_ram(cpu, TICK_PENDING_REG) | TICK_BIT));
    }
    if((pic18_read_ram(cpu, TICK_ENABLE_REG) & TICK_BIT) &&
       (pic18_read_ram(cpu, TICK_PENDING_REG) & TICK_BIT)) {
        return (int32_t)TICK_ISR_VECTOR;
    }
    return -1;
}

/* Starts the keyfob's periodic timer tick after boot. The firmware waits for
 * the first tick before arming the timebase itself, so the frontend starts
 * TMR0 (TMR0ON, 16-bit, Fosc/4, 1:2 prescale) and enables the tick source;
 * pic_irq_hook() then bridges each overflow into the firmware's ISR. */
static void arm_keyfob_tick(AppState* app) {
    Pic18Cpu* cpu = app->cpu;
    pic18_write_data(cpu, SFR_T0CON, 0x80);
    pic18_write_data(cpu, SFR_TMR0H, 0x00);
    pic18_write_data(cpu, SFR_TMR0L, 0x00);
    pic18_write_ram(
        cpu, TICK_ENABLE_REG, (uint8_t)(pic18_read_ram(cpu, TICK_ENABLE_REG) | TICK_BIT));
    cpu->on_irq = pic_irq_hook;
    cpu->irq_ctx = app;
}

/* ----------------------------------------------------- buttons (PORT read) */

/* The firmware reads the 5 buttons in register 0xF8E (active-low). We return
 * 0xFF with the pressed bits at 0. For PORTB: mariofull's handshake. For the RX
 * input pin (if the RX bridge is active) we inject the current level. */
static int port_read_hook(Pic18Cpu* cpu, uint16_t addr, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(addr == BTN_REG) {
        return (int)(0xFFu & ~app->btn_mask);
    }
    if(addr == SFR_PORTB && app->prof && app->prof->portb1_handshake) {
        uint8_t latb = pic18_read_ram(cpu, SFR_LATB);
        uint8_t v = (uint8_t)(latb & (uint8_t)~0x02u); /* bit1 = 0 (ACK) */
        /* If the RX bridge uses PORTB, we overlay the injected level. */
        if(app->rf_rx_enabled && app->rx_in_port == SFR_PORTB) {
            if(app->rx_cur_level)
                v |= (uint8_t)(1u << app->rx_in_bit);
            else
                v &= (uint8_t)~(1u << app->rx_in_bit);
        }
        return (int)v;
    }
    if(app->rf_rx_enabled && addr == app->rx_in_port) {
        uint8_t v = pic18_read_ram(cpu, addr);
        if(app->rx_cur_level)
            v |= (uint8_t)(1u << app->rx_in_bit);
        else
            v &= (uint8_t)~(1u << app->rx_in_bit);
        return (int)v;
    }
    return -1; /* default */
}

/* LAT write hook: when the TX bridge is active, detects the toggles of the
 * firmware's OOK pin and accumulates the edges with their duration (measured by
 * cpu->cycles between toggles) in the TX ring. The async_tx callback drains them. */
static void lat_write_hook(Pic18Cpu* cpu, uint16_t addr, uint8_t val, void* ctx) {
    AppState* app = (AppState*)ctx;
    if(!app->rf_tx_enabled) return;
    if(addr != app->prof->ook_tx_lat) return;
    bool level = (val >> app->prof->ook_tx_bit) & 1u;
    if(level == app->tx_last_level) return; /* no edge */

    uint64_t now = cpu->cycles;
    uint64_t dcyc = now - app->tx_last_cycles;
    app->tx_last_cycles = now;

    /* duration of the level that JUST ended (tx_last_level) */
    uint32_t dur_us = (uint32_t)((dcyc * PIC_CYCLE_US_NUM) / PIC_CYCLE_US_DEN);
    if(dur_us == 0) dur_us = 1;

    uint32_t head = app->tx_head;
    uint32_t next = (head + 1) % TX_RING_LEN;
    if(next != app->tx_tail) { /* ring not full */
        app->tx_ring[head].level = app->tx_last_level;
        app->tx_ring[head].duration = dur_us;
        app->tx_head = next;
    }
    app->tx_last_level = level;
}

static void set_button_bit(AppState* app, uint8_t bit, bool pressed) {
    if(pressed)
        app->btn_mask |= (uint8_t)(1u << bit);
    else
        app->btn_mask &= (uint8_t)~(1u << bit);
    app->btn_mask &= BTN_ALL_MASK;
}

/* ------------------------------------------------------------ rendering */

/* The GDDRAM is 128x32 (4 pages). The Flipper's screen is 128x64. We draw it
 * vertically centered (rows 16..47) at 1:1 scale, 1 pixel per pixel. screen[]
 * uses the Flipper page format: byte (x, page) with bit = row&7, bit=1 => light. */
static void render_gddram(AppState* app) {
    furi_mutex_acquire(app->fb_mutex, FuriWaitForever);
    memset(app->screen, 0, FB_SIZE);
    const int y_off = 16; /* center 32 rows in 64 */
    for(int page = 0; page < GDDRAM_PAGES; page++) {
        for(int col = 0; col < GDDRAM_COLS; col++) {
            uint8_t v = app->gddram[page * GDDRAM_COLS + col];
            for(int bit = 0; bit < 8; bit++) {
                int src_y = page * 8 + bit; /* 0..31 */
                if(!((v >> bit) & 1)) continue;
                int x = col;
                int yy = src_y;
                /* user rotation (calibrate to the physical panel: A1/C8 remap) */
                switch(app->disp_rot) {
                case 1: x = (GDDRAM_COLS - 1) - col; break;            /* H-flip */
                case 2: yy = 31 - src_y; break;                        /* V-flip */
                case 3: x = (GDDRAM_COLS - 1) - col; yy = 31 - src_y; break; /* 180 */
                default: break;
                }
                int y = y_off + yy;
                if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) continue;
                app->screen[(y >> 3) * SCREEN_W + x] |= (uint8_t)(1u << (y & 7));
            }
        }
    }
    furi_mutex_release(app->fb_mutex);
}

/* Framebuffer commit callback (direct draw). screen bit=1 => light; the display
 * buffer is bit=1 => dark, hence the XOR. */
static void framebuffer_commit_callback(
    uint8_t* data,
    size_t size,
    CanvasOrientation orientation,
    void* context) {
    __atomic_fetch_add(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
    AppState* app = (AppState*)context;
    UNUSED(orientation);
    if(!app || !data || size < FB_SIZE || app->menu_active || app->status_active) {
        __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
        return;
    }
    if(furi_mutex_acquire(app->fb_mutex, 0) != FuriStatusOk) {
        __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
        return;
    }
    const uint8_t* src = app->screen;
    for(size_t i = 0; i < FB_SIZE; i++) {
        data[i] = (uint8_t)(src[i] ^ 0xFF);
    }
    furi_mutex_release(app->fb_mutex);
    __atomic_fetch_sub(&s_fb_cb_inflight, 1, __ATOMIC_RELAXED);
}

/* ------------------------------------------------------------ input */

static void wait_inflight_zero(volatile uint32_t* counter) {
    while(__atomic_load_n(counter, __ATOMIC_ACQUIRE) != 0) {
        furi_delay_ms(1);
    }
}

/* Direct pin polling (avoids the input service, like FlipperGB). Maps:
 *   Flipper Up    -> B1 (increment PIN digit / up)
 *   Flipper Down  -> B3 (menu/down)
 *   Flipper Ok    -> B2 (confirm)
 *   Flipper Left  -> B5
 *   Flipper Right -> B6
 *   Flipper Back  -> (handled for exit / menu)
 * Up+Down simultaneously = help overlay / RF toggle (a physically impossible
 * gesture).
 */
static uint8_t poll_raw_keys(void) {
    uint8_t keys = 0;
    for(size_t i = 0; i < input_pins_count; i++) {
        const InputPin* p = &input_pins[i];
        if(furi_hal_gpio_read(p->gpio) == p->inverted) continue;
        switch(p->key) {
        case InputKeyUp: keys |= KBIT_UP; break;
        case InputKeyDown: keys |= KBIT_DOWN; break;
        case InputKeyLeft: keys |= KBIT_LEFT; break;
        case InputKeyRight: keys |= KBIT_RIGHT; break;
        case InputKeyOk: keys |= KBIT_OK; break;
        case InputKeyBack: keys |= KBIT_BACK; break;
        default: break;
        }
    }
    return keys;
}

static void apply_keys_to_buttons(AppState* app, uint8_t keys) {
    /* Up+Down together open the help overlay (and toggle the RF bridge). */
    bool combo_menu = (keys & (KBIT_UP | KBIT_DOWN)) == (KBIT_UP | KBIT_DOWN);
    if(combo_menu) {
        app->menu_requested = true;
        set_button_bit(app, BTN_B1_BIT, false);
        set_button_bit(app, BTN_B3_BIT, false);
    } else {
        set_button_bit(app, BTN_B1_BIT, keys & KBIT_UP);
        set_button_bit(app, BTN_B3_BIT, keys & KBIT_DOWN);
    }
    set_button_bit(app, BTN_B2_BIT, keys & KBIT_OK);
    set_button_bit(app, BTN_B5_BIT, keys & KBIT_LEFT);
    set_button_bit(app, BTN_B6_BIT, keys & KBIT_RIGHT);
}

/* ------------------------------------------------------------ .hex loading */

typedef enum {
    LoadOk,
    LoadIoError,
    LoadNoMem,
    LoadBadFormat,
} LoadResult;

/* Decodes 2 hex chars. -1 if invalid. */
static int pic_hexbyte(const char* s) {
    int hi = (unsigned char)s[0], lo = (unsigned char)s[1];
    if(hi >= '0' && hi <= '9') hi -= '0';
    else { hi |= 0x20; if(hi >= 'a' && hi <= 'f') hi = hi - 'a' + 10; else return -1; }
    if(lo >= '0' && lo <= '9') lo -= '0';
    else { lo |= 0x20; if(lo >= 'a' && lo <= 'f') lo = lo - 'a' + 10; else return -1; }
    return (hi << 4) | lo;
}


static uint32_t pic_scan_prog_size(File* f) {
    static uint8_t sbuf[512];
    static char sline[600];
    uint8_t first = 0;
    if(storage_file_read(f, &first, 1) != 1) {
        storage_file_seek(f, 0, true);
        return PROG_MAX;
    }
    storage_file_seek(f, 0, true);
    if(first != ':') {
        uint64_t sz = storage_file_size(f);
        return (sz > 0 && sz < PROG_MAX) ? (uint32_t)sz : PROG_MAX;
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
                    int cnt = pic_hexbyte(sline);
                    int ah = pic_hexbyte(sline + 2);
                    int al = pic_hexbyte(sline + 4);
                    int rt = pic_hexbyte(sline + 6);
                    if(cnt >= 0 && ah >= 0 && al >= 0 && rt >= 0) {
                        uint32_t a = ((uint32_t)ah << 8) | (uint32_t)al;
                        if(rt == 0x04) {
                            int b0 = pic_hexbyte(sline + 8), b1 = pic_hexbyte(sline + 10);
                            if(b0 >= 0 && b1 >= 0) ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 16;
                        } else if(rt == 0x02) {
                            int b0 = pic_hexbyte(sline + 8), b1 = pic_hexbyte(sline + 10);
                            if(b0 >= 0 && b1 >= 0) ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 4;
                        } else if(rt == 0x00) {
                            uint32_t start = ext + a;
                            uint32_t end = start + (uint32_t)cnt;
                            if(start < PROG_MAX && end > hi) hi = end;
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
    return (hi > 0 && hi < PROG_MAX) ? hi : PROG_MAX;
}

/* ---- flash demand-paging ---------------------------------------------- */
/* One 1KB page from the backing store, direct-mapped with a current-page fast
 * path. Bytes past flash_bk_size read as 0xFF (unprogrammed flash). */
static const uint8_t* pic_page_ptr(AppState* app, uint32_t page) {
    if(page == app->cur_page) {
        app->pg_hits++;
        return app->cur_ptr;
    }
    uint32_t slot = page % PIC_PAGES;
    uint8_t* ptr = app->page_cache + slot * PIC_PAGE;
    if(app->page_tag[slot] != page) {
        app->pg_misses++;
        memset(ptr, 0xFF, PIC_PAGE);
        uint32_t off = page * PIC_PAGE;
        if(app->flash_file && off < app->flash_bk_size) {
            uint32_t n = PIC_PAGE;
            if(off + n > app->flash_bk_size) n = app->flash_bk_size - off;
            storage_file_seek(app->flash_file, off, true);
            storage_file_read(app->flash_file, ptr, (uint16_t)n);
        }
        app->page_tag[slot] = page;
    } else {
        app->pg_hits++;
    }
    app->cur_page = page;
    app->cur_ptr = ptr;
    return ptr;
}

/* One code byte at absolute address a (< 0x300000). */
static uint8_t pic_code_byte(AppState* app, uint32_t a) {
    const uint8_t* pg = pic_page_ptr(app, a / PIC_PAGE);
    return pg[a % PIC_PAGE];
}

/* pic18core program-read hook: every code fetch / TBLRD of code space routes
 * here. Config words (>= 0x300000) are handled inside the core, not here. */
static uint8_t pic_prog_read_cb(Pic18Cpu* cpu, uint32_t a, void* ctx) {
    (void)cpu;
    return pic_code_byte((AppState*)ctx, a);
}

/* pic18core program-write hook (TBLWT): the paged image is read-only. These
 * keyfob firmwares never self-program flash (host-verified: 0 TBLWT), so the
 * write is dropped. */
static void pic_prog_write_cb(Pic18Cpu* cpu, uint32_t a, uint8_t v, void* ctx) {
    (void)cpu;
    (void)a;
    (void)v;
    (void)ctx;
}

/* Releases the paging backing store + cache; removes the temp image if we made
 * one. Safe to call repeatedly (fields nulled). */
static void pic_free_flash(AppState* app, Storage* storage) {
    if(app->flash_file) {
        storage_file_close(app->flash_file);
        storage_file_free(app->flash_file);
        app->flash_file = NULL;
    }
    if(app->flash_temp && storage) {
        storage_common_remove(storage, PIC_FLASH_TEMP_PATH);
        app->flash_temp = false;
    }
    if(app->page_cache) {
        free(app->page_cache);
        app->page_cache = NULL;
    }
    app->cur_page = 0xFFFFFFFFu;
    app->cur_ptr = NULL;
}

/* Streaming Intel-HEX -> flat code image + config table. Writes code bytes
 * (addr < 0x300000) into `out` by absolute address (pre-filled 0xFF, seek+write
 * so record order does not matter) and stores config words (>= 0x300000) in the
 * core's config table. No full-size RAM buffer. (Host-verified byte-exact vs
 * pic18_load_hex over the code space.) */
static bool pic_hex_to_flat(Pic18Cpu* cpu, File* in, File* out, uint32_t codesz) {
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
                    int count = pic_hexbyte(line);
                    int ah = pic_hexbyte(line + 2);
                    int al = pic_hexbyte(line + 4);
                    int rtype = pic_hexbyte(line + 6);
                    if(count < 0 || ah < 0 || al < 0 || rtype < 0) return false;
                    uint32_t addr = ((uint32_t)ah << 8) | (uint32_t)al;
                    const char* data = line + 8;
                    if(rtype == 0x00) {
                        uint32_t base = ext + addr;
                        if(base >= 0x300000u) {
                            for(int k = 0; k < count; k++) {
                                int b = pic_hexbyte(data + k * 2);
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
                                int b = pic_hexbyte(data + k * 2);
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
                        int b0 = pic_hexbyte(data), b1 = pic_hexbyte(data + 2);
                        if(b0 < 0 || b1 < 0) return false;
                        ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 16;
                    } else if(rtype == 0x02) {
                        int b0 = pic_hexbyte(data), b1 = pic_hexbyte(data + 2);
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

/* STREAMING paged load: a .bin is paged directly; a .hex is converted once to a
 * flat temp image (config captured) and paged from that. Only a 24KB page cache
 * is held in RAM (see pic_page_ptr). */
static LoadResult load_firmware(AppState* app, Storage* storage, const char* path) {
    File* f = storage_file_alloc(storage);
    LoadResult res = LoadIoError;
    bool keep_f = false;
    do {
        if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        uint64_t fsize = storage_file_size(f);
        if(fsize == 0 || fsize > 4u * 1024u * 1024u) {
            res = LoadBadFormat;
            break;
        }

        uint32_t codesz = pic_scan_prog_size(f); /* top code address (leaves pos at 0) */

        app->page_cache = (uint8_t*)safe_malloc(PIC_PAGES * PIC_PAGE);
        if(!app->page_cache) {
            FURI_LOG_E(TAG, "no heap for page cache (%u B)", (unsigned)(PIC_PAGES * PIC_PAGE));
            res = LoadNoMem;
            break;
        }
        for(uint32_t i = 0; i < PIC_PAGES; i++) app->page_tag[i] = 0xFFFFFFFFu;
        app->cur_page = 0xFFFFFFFFu;
        app->cur_ptr = NULL;
        app->pg_hits = app->pg_misses = 0;
        app->flash_bk_size = codesz;
        app->cpu->config_count = 0;

        uint8_t first = 0;
        if(storage_file_read(f, &first, 1) != 1) break;
        storage_file_seek(f, 0, true);

        if(first == ':') {
            /* .hex: stream-convert to a flat temp image + config table */
            File* out = storage_file_alloc(storage);
            if(!storage_file_open(out, PIC_FLASH_TEMP_PATH, FSAM_READ_WRITE, FSOM_CREATE_ALWAYS)) {
                storage_file_free(out);
                res = LoadIoError;
                break;
            }
            if(!pic_hex_to_flat(app->cpu, f, out, codesz)) {
                storage_file_close(out);
                storage_file_free(out);
                res = LoadBadFormat;
                break;
            }
            app->flash_file = out;
            app->flash_temp = true;
        } else {
            /* .bin: page directly from the selected file */
            app->flash_file = f;
            app->flash_temp = false;
            keep_f = true;
            if((uint32_t)fsize < codesz) app->flash_bk_size = (uint32_t)fsize;
        }

        pic18_set_prog(app->cpu, NULL, codesz);
        app->cpu->on_prog = pic_prog_read_cb;
        app->cpu->prog_ctx = app;
        app->cpu->on_prog_write = pic_prog_write_cb;
        app->cpu->progw_ctx = app;
        res = LoadOk;
    } while(false);
    if(!keep_f) {
        storage_file_close(f);
        storage_file_free(f);
    }
    return res;
}

/* Selects the profile by the config words (CONFIG3L 0xA2 full / 0xBF mario2).
 * Defaults to mariofull. */
/* Case-insensitive substring match. */
static bool pic_name_has(const char* hay, const char* needle) {
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

/* Picks mario2 vs mariofull. Primary: CONFIG3L (0xA2 full / 0xBF mario2),
 * present when loaded from .hex. Fallback for a flat .bin (no config words):
 * the file name, then the code size (mario2 tops ~0x178AC, full ~0x1A17C). */
static const PicProfile* pick_profile(const char* path, Pic18Cpu* cpu, uint32_t codesz) {
    for(uint16_t i = 0; i < cpu->config_count; i++) {
        if(cpu->config_addr[i] == 0x300004u) {
            return (cpu->config_val[i] == 0xA2u) ? &PROFILE_FULL : &PROFILE_MARIO2;
        }
    }
    if(pic_name_has(path, "full")) return &PROFILE_FULL;
    if(pic_name_has(path, "mario2") || pic_name_has(path, "v2")) return &PROFILE_MARIO2;
    return (codesz >= 0x19000u) ? &PROFILE_FULL : &PROFILE_MARIO2;
}

/* ------------------------------------------------------------ boot/cinit */

/* Applies the XC8 runtime cinit records (mario2): flash->RAM copy from the
 * initialization table. Equivalent to emu/boot_mario2.py apply_cinit. Only
 * writes GPR RAM (<0xF80), not SFR. */
static void apply_cinit_records(AppState* app) {
    Pic18Cpu* cpu = app->cpu;
    uint32_t tp = app->prof->cinit_table;
    uint32_t end = app->prof->cinit_table_end;
    uint16_t fsr0 = 0x000;
    int guard = 0;
    while(tp < end && guard++ < 512) {
        uint8_t n = (tp < cpu->prog_size) ? pic_code_byte(app, tp) : 0;
        tp++;
        if(n == 0) break;
        uint8_t fd = (tp < cpu->prog_size) ? pic_code_byte(app, tp) : 0;
        tp++;
        if(fd & 0x80) {
            uint8_t lo = (tp < cpu->prog_size) ? pic_code_byte(app, tp) : 0;
            tp++;
            fsr0 = (uint16_t)(((fd & 0x0F) << 8) | lo);
        }
        if(!(fd & 0x40)) {
            tp++; /* consume 1 extra byte (like the python) */
        }
        if(tp + n > end + 2) break; /* record runs off the table: desync */
        for(uint8_t k = 0; k < n; k++) {
            uint8_t b = (tp < cpu->prog_size) ? pic_code_byte(app, tp) : 0;
            tp++;
            if(fsr0 < 0xF80u) cpu->ram[fsr0 & 0xFFF] = b;
            fsr0 = (uint16_t)((fsr0 + 1) & 0xFFF);
        }
    }
}

/* Takes the firmware from reset to the main loop, with the runtime init resolved
 * according to the profile. Returns true if the boot reached its destination. */
static bool boot_firmware(AppState* app) {
    Pic18Cpu* cpu = app->cpu;
    pic18_reset(cpu);
    if(app->prof->boot_mode == 1) {
        /* mario2: run prologue+data-init up to the cinit entry, apply the
         * records, jump to the cinit exit. */
        uint64_t budget = 2000000;
        while(cpu->pc != app->prof->cinit_entry && budget > 0) {
            pic18_step(cpu);
            budget--;
            if(cpu->halted) break;
        }
        if(cpu->pc != app->prof->cinit_entry) return false;
        apply_cinit_records(app);
        cpu->pc = app->prof->cinit_exit;
        return true;
    } else {
        /* mariofull: the PORTB.1 handshake (port_read_hook) lets the cinit finish
         * on its own; we run up to the main loop. */
        uint64_t budget = 3000000;
        while(cpu->pc != app->prof->cinit_wait_pc && budget > 0) {
            pic18_step(cpu);
            budget--;
            if(cpu->halted) break;
        }
        return cpu->pc == app->prof->cinit_wait_pc;
    }
}

/* ------------------------------------------------------------ PIN inject */

/* Return sentinel placed on the stack before triggering the validation. */
#define PIN_RET_SENTINEL 0x01FFFEu

/* Result of the PIN validation (filled by pin_code_hook via ctx). */
typedef struct {
    const PicProfile* prof;
    int accept;
    int reject;
    int done;
} PinRun;

/* Code hook used only during enter_pin(): detects ACCEPT/REJECT and stops at the
 * sentinel. The context (ctx) is a PinRun* local to this run; this way we do NOT
 * need global state and it is reentrant. */
static void pin_code_hook(Pic18Cpu* cpu, uint32_t pc, void* ctx) {
    PinRun* r = (PinRun*)ctx;
    if(pc == r->prof->pin_accept) {
        r->accept = 1;
        r->done = 1;
    } else if(pc == r->prof->pin_reject) {
        r->reject = 1;
        r->done = 1;
    } else if(pc == PIN_RET_SENTINEL) {
        cpu->halted = 1;
        r->done = 1;
    }
}

/* Injects the numeric PIN chosen in the menu into the PIN FSM buffer and triggers
 * the firmware's REAL validation (0x018D76), just like emu/pic18_tui.py
 * enter_pin. Logs the ACCEPT/REJECT verdict. Only applies to mariofull (has_pin);
 * for mario2 the PIN field is ignored.
 *
 * The core's prog/RAM content after boot stays intact except for the PIN buffer:
 * the validation runs with its own stack/BSR and a return sentinel, and we
 * restore PC/stack/BSR/stkptr so as not to disturb the emulator's main loop
 * (which starts right after from cinit_wait_pc / cinit_exit). */
static void enter_pin(AppState* app) {
    const PicProfile* prof = app->prof;
    if(!prof->has_pin || app->pin_entered) return;
    app->pin_entered = true;

    Pic18Cpu* cpu = app->cpu;

    /* decompose the numeric PIN into 4 decimal digits (2552 -> 2,5,5,2) */
    int32_t v = app->pin_value;
    if(v < 0) v = 0;
    int digits[4];
    digits[0] = (v / 1000) % 10;
    digits[1] = (v / 100) % 10;
    digits[2] = (v / 10) % 10;
    digits[3] = v % 10;

    /* 1) compose the PIN buffer (result of the B1/B2 presses) */
    for(int i = 0; i < 4; i++) {
        pic18_write_ram(cpu, prof->pin_slot_lo[i], (uint8_t)(digits[i] & 0xFF));
        pic18_write_ram(cpu, prof->pin_slot_hi[i], (uint8_t)((digits[i] >> 8) & 0xFF));
    }
    pic18_write_ram(cpu, prof->pin_unlock_lo, 0); /* "already unlocked" flag = 0 */
    pic18_write_ram(cpu, prof->pin_unlock_hi, 0);

    /* 2) run the REAL validation with a return sentinel and an ACCEPT/REJECT
     * hook. We save and restore the main loop context. */
    uint32_t saved_pc = cpu->pc;
    uint8_t saved_bsr = cpu->ram[PIC18_SFR_BSR];
    uint8_t saved_stkptr = cpu->stkptr;
    uint8_t saved_halted = cpu->halted;
    Pic18CodeHook saved_hook = cpu->on_code;
    void* saved_ctx = cpu->code_ctx;

    PinRun run = {.prof = prof, .accept = 0, .reject = 0, .done = 0};
    cpu->on_code = pin_code_hook;
    cpu->code_ctx = &run;

    cpu->halted = 0;
    cpu->stkptr = 0;
    cpu->ram[PIC18_SFR_BSR] = 0;
    pic18_push(cpu, PIN_RET_SENTINEL); /* return sentinel */
    cpu->pc = prof->pin_validate;

    uint64_t budget = 4000000;
    while(budget-- > 0 && !cpu->halted && !run.done) {
        pic18_step(cpu);
    }

    /* restore the main loop context */
    cpu->on_code = saved_hook;
    cpu->code_ctx = saved_ctx;
    cpu->pc = saved_pc;
    cpu->ram[PIC18_SFR_BSR] = saved_bsr;
    cpu->stkptr = saved_stkptr;
    cpu->halted = saved_halted;

    if(run.accept) {
        app->pin_verdict = 1;
        FURI_LOG_I(TAG, "PIN %04ld -> ACCEPT (OOK service TX)", (long)app->pin_value);
    } else if(run.reject) {
        app->pin_verdict = 2;
        FURI_LOG_I(TAG, "PIN %04ld -> REJECT (no transmit)", (long)app->pin_value);
    } else {
        app->pin_verdict = 3;
        FURI_LOG_W(TAG, "PIN %04ld -> no verdict (budget exhausted)", (long)app->pin_value);
    }
}

/* ------------------------------------------------------------ CC1101 RF */

/* OOK 650kHz async preset in the furi_hal_subghz_load_custom_preset format:
 * {reg, val} pairs terminated by 0x00, then 2 bytes (ignored) + 8 bytes of
 * PA table. Values aligned with the firmware's async OOK table
 * (lib/subghz/devices/cc1101_configs.c: preset_ook_650khz_async_regs). */
static const uint8_t PRESET_OOK650[] = {
    0x02, 0x0D, /* IOCFG0: async serial data out/in */
    0x07, 0x04, /* FIFOTHR */
    0x08, 0x32, /* PKTCTRL0: async, continuous, no whitening */
    0x0B, 0x06, /* FSCTRL1 */
    0x10, 0xF8, /* MDMCFG4 (bw) */
    0x11, 0x32, /* MDMCFG3 (data rate) */
    0x12, 0x30, /* MDMCFG2: ASK/OOK, no preamble/sync */
    0x14, 0x00, /* MDMCFG0 */
    0x15, 0x00, /* DEVIATN */
    0x18, 0x18, /* MCSM0 */
    0x19, 0x16, /* FOCCFG */
    0x1B, 0x07, /* AGCCTRL2 */
    0x1C, 0x00, /* AGCCTRL1 */
    0x1D, 0x91, /* AGCCTRL0 */
    0x20, 0xFB, /* WORCTRL */
    0x21, 0xB6, /* FREND1 */
    0x22, 0x11, /* FREND0 */
    0x00, /* terminator */
    0x00, 0x00, /* 2 bytes (ignored by the parser before PA) */
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* PA table (OOK: C0) */
};

/* The Pandora firmware does its own OOK modulation by bit-bang; here we
 * initialize the Flipper's CC1101 (async RX/TX) for the radio bridge. */
static void radio_init(AppState* app) {
    app->frequency = 433920000;
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(PRESET_OOK650);
    furi_hal_subghz_set_frequency_and_path(app->frequency);
    furi_hal_subghz_idle();
    app->radio_on = true;
    /* default RX input pin: the PORTB handshake (mariofull) or PORTB in general;
     * it is the pin the firmware samples. It can be adjusted per profile. */
    app->rx_in_port = SFR_PORTB;
    app->rx_in_bit = 0;
    FURI_LOG_I(
        TAG, "CC1101 init OOK650 @%lu Hz", (unsigned long)app->frequency);
}

static void radio_deinit(AppState* app) {
    if(!app->radio_on) return;
    /* stop any async in progress */
    if(app->tx_active) {
        furi_hal_subghz_stop_async_tx();
        app->tx_active = false;
    }
    if(app->rf_rx_enabled) {
        furi_hal_subghz_stop_async_rx();
        app->rf_rx_enabled = false;
    }
    furi_hal_subghz_idle();
    furi_hal_subghz_sleep();
    app->radio_on = false;
    FURI_LOG_I(TAG, "CC1101 sleep");
}

/* --- TX bridge --- */

/* async_tx callback: drains the ring of edges captured from the OOK pin. */
static LevelDuration radio_tx_callback(void* context) {
    AppState* app = (AppState*)context;
    uint32_t tail = app->tx_tail;
    if(tail == app->tx_head) {
        /* nothing more to send for now: end the burst */
        return level_duration_reset();
    }
    RfEdge e = app->tx_ring[tail];
    app->tx_tail = (tail + 1) % TX_RING_LEN;
    return level_duration_make(e.level, e.duration);
}

/* Launches the replay of the accumulated edges if there are enough and there is
 * no TX in progress. Called in the main loop. */
static void tx_bridge_flush(AppState* app) {
    if(!app->rf_tx_enabled || !app->radio_on) return;
    if(app->tx_active) {
        if(furi_hal_subghz_is_async_tx_complete()) {
            furi_hal_subghz_stop_async_tx();
            furi_hal_subghz_idle();
            app->tx_active = false;
            app->tx_frames++;
            FURI_LOG_I(TAG, "TX frame #%lu sent", (unsigned long)app->tx_frames);
        }
        return;
    }
    /* count pending edges */
    uint32_t pending = (app->tx_head + TX_RING_LEN - app->tx_tail) % TX_RING_LEN;
    if(pending < 16) return; /* wait for a reasonable burst */
    if(!furi_hal_subghz_is_tx_allowed(app->frequency)) {
        FURI_LOG_E(
            TAG, "TX NOT allowed @%lu Hz; discarding %lu edges",
            (unsigned long)app->frequency, (unsigned long)pending);
        app->tx_tail = app->tx_head; /* discard */
        return;
    }
    FURI_LOG_I(TAG, "TX async start: %lu edges", (unsigned long)pending);
    if(furi_hal_subghz_start_async_tx(radio_tx_callback, app)) {
        app->tx_active = true;
    } else {
        FURI_LOG_E(TAG, "start_async_tx failed");
        app->tx_tail = app->tx_head;
    }
}

/* --- RX bridge --- */

/* CC1101 callback (runs in IRQ): enqueues the edge. Do NOT block. */
static void radio_rx_callback(bool level, uint32_t duration, void* context) {
    AppState* app = (AppState*)context;
    RfEdge e = {.level = level, .duration = duration};
    /* timeout 0: if the queue is full, the edge is lost (better than blocking in
     * the IRQ). */
    furi_message_queue_put(app->rx_queue, &e, 0);
}

static void rx_bridge_start(AppState* app) {
    if(app->rf_rx_enabled || !app->radio_on) return;
    if(app->tx_active) {
        furi_hal_subghz_stop_async_tx();
        app->tx_active = false;
    }
    furi_hal_subghz_idle();
    furi_message_queue_reset(app->rx_queue);
    app->rx_cur_level = false;
    app->rx_level_until = furi_get_tick();
    furi_hal_subghz_start_async_rx(radio_rx_callback, app);
    app->rf_rx_enabled = true;
    FURI_LOG_I(TAG, "RX bridge ON (injects into %03X.%u)", app->rx_in_port, app->rx_in_bit);
}

static void rx_bridge_stop(AppState* app) {
    if(!app->rf_rx_enabled) return;
    furi_hal_subghz_stop_async_rx();
    furi_hal_subghz_idle();
    app->rf_rx_enabled = false;
    FURI_LOG_I(TAG, "RX bridge OFF (%lu edges injected)", (unsigned long)app->rx_events);
}

/* Drains the RX queue and updates the level injected into the firmware's input
 * pin. The timing is honored by each edge's duration; the firmware reads the pin
 * via on_port_read (port_read_hook) and demodulates on its own. */
static void rx_bridge_pump(AppState* app) {
    if(!app->rf_rx_enabled) return;
    uint32_t now = furi_get_tick();
    /* while the current level has already "fulfilled" its duration, we pull the
     * next edge from the queue. */
    RfEdge e;
    int drained = 0;
    while((int32_t)(now - app->rx_level_until) >= 0 &&
          furi_message_queue_get(app->rx_queue, &e, 0) == FuriStatusOk) {
        app->rx_cur_level = e.level;
        /* duration comes in us; the Flipper tick is ms. We hold the level for at
         * least 1 tick (sub-ms edges are aggregated de facto). */
        uint32_t dur_ms = e.duration / 1000u;
        if(dur_ms == 0) dur_ms = 1;
        app->rx_level_until = now + dur_ms;
        app->rx_events++;
        drained++;
        if(drained >= 64) break; /* don't hog the frame */
    }
    if(drained) {
        FURI_LOG_D(
            TAG, "RX injected %d edges (total %lu)", drained,
            (unsigned long)app->rx_events);
    }
}

/* Cycles the RF bridge: OFF -> TX -> RX -> OFF. Logged. */
static void rf_cycle_mode(AppState* app) {
    if(!app->rf_tx_enabled && !app->rf_rx_enabled) {
        /* OFF -> TX */
        app->rf_tx_enabled = true;
        app->tx_last_level = (pic18_read_ram(app->cpu, app->prof->ook_tx_lat) >>
                              app->prof->ook_tx_bit) &
                             1u;
        app->tx_last_cycles = app->cpu->cycles;
        app->tx_head = app->tx_tail = 0;
        FURI_LOG_I(TAG, "RF mode TX (capture OOK pin %03X.%u)",
                   app->prof->ook_tx_lat, app->prof->ook_tx_bit);
    } else if(app->rf_tx_enabled && !app->rf_rx_enabled) {
        /* TX -> RX */
        app->rf_tx_enabled = false;
        if(app->tx_active) {
            furi_hal_subghz_stop_async_tx();
            app->tx_active = false;
        }
        rx_bridge_start(app);
    } else {
        /* RX -> OFF */
        rx_bridge_stop(app);
    }
}

/* ------------------------------------------------------------ help overlay */

/* Count non-empty framebuffer bytes (cheap blank detector). */
static int screen_lit_bytes(const AppState* app) {
    int n = 0;
    for(size_t i = 0; i < FB_SIZE; i++)
        if(app->screen[i]) n++;
    return n;
}

/* Status screen shown when the emulated OLED stays blank: explains WHY instead
 * of leaving a bare black screen (locked keyfob / PIN / still booting). */
static void status_draw(AppState* app) {
    Canvas* c = app->canvas;
    canvas_reset(c);
    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 11, "Pandora PIC");
    canvas_draw_line(c, 0, 14, 127, 14);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 2, 26, "Booting keyfob firmware");

    /* Indeterminate progress bar (sweeping segment): the emulator is busy
     * running the firmware while it boots and draws its first screen. */
    {
        const int bx = 4, by = 31, bw = 120, bh = 7;
        canvas_draw_frame(c, bx, by, bw, bh);
        const int seg = 26;
        int span = bw - 2 - seg;
        if(span < 1) span = 1;
        int pos = (int)((furi_get_tick() / 6u) % (uint32_t)(2 * span));
        if(pos > span) pos = 2 * span - pos; /* bounce back */
        canvas_draw_box(c, bx + 1 + pos, by + 1, seg, bh - 2);
    }

    if(app->prof && app->prof->has_pin) {
        char buf[36];
        if(app->pin_verdict == 2)
            snprintf(buf, sizeof(buf), "PIN %04ld: rejected", (long)app->pin_value);
        else if(app->pin_verdict == 1)
            snprintf(buf, sizeof(buf), "PIN %04ld: accepted", (long)app->pin_value);
        else
            snprintf(buf, sizeof(buf), "PIN %04ld", (long)app->pin_value);
        canvas_draw_str(c, 2, 50, buf);
    } else {
        canvas_draw_str(c, 2, 50, "Please wait...");
    }
    canvas_draw_str(c, 2, 62, "Back: menu");
    canvas_commit(c);
}

static void overlay_draw(AppState* app) {
    Canvas* c = app->canvas;
    canvas_reset(c);
    canvas_clear(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 10, "Display Setup");
    canvas_draw_line(c, 0, 12, 127, 12);
    canvas_set_font(c, FontSecondary);
    char buf[40];
    const char* dc = app->disp_dc ? "RAM-flag" : "caller";
    const char* col = app->disp_colreset ? "reset" : "keep";
    const char* rot = (app->disp_rot == 1) ? "H-flip" :
                      (app->disp_rot == 2) ? "V-flip" :
                      (app->disp_rot == 3) ? "180" : "0";
    const char* rf = app->rf_tx_enabled ? "TX" : app->rf_rx_enabled ? "RX" : "OFF";
    snprintf(buf, sizeof(buf), "Up   D/C src : %s", dc);
    canvas_draw_str(c, 2, 23, buf);
    snprintf(buf, sizeof(buf), "Down Column  : %s", col);
    canvas_draw_str(c, 2, 32, buf);
    snprintf(buf, sizeof(buf), "Rght Rotate  : %s", rot);
    canvas_draw_str(c, 2, 41, buf);
    snprintf(buf, sizeof(buf), "Left RF      : %s", rf);
    canvas_draw_str(c, 2, 50, buf);
    canvas_draw_str(c, 2, 62, "OK = view screen   Back = menu");
    canvas_commit(c);
}

/* ------------------------------------------------------------ phase 1 menu */

/* view_dispatcher views */
typedef enum {
    PandoraViewSubmenu,
    PandoraViewNumberInput,
    PandoraViewAbout,
} PandoraView;

/* Main submenu items */
typedef enum {
    MenuItemFirmware,
    MenuItemPin,
    MenuItemLaunch,
    MenuItemAbout,
} MenuItem;

/* Phase 1 result: which action the user requested when stopping the dispatcher. */
typedef enum {
    FlowExit, /* exit the app */
    FlowFileBrowser, /* open the file browser and return to the menu */
    FlowPinInput, /* enter the number_input (handled inside the dispatcher) */
    FlowLaunch, /* start the emulator */
} MenuFlow;

static volatile MenuFlow g_menu_flow = FlowExit;

/* submenu item selection callback (declared before menu_rebuild) */
static void menu_submenu_callback(void* context, uint32_t index);

/* About text (scrollable). The button mapping below MUST match what the code
 * actually does: see apply_keys_to_buttons() and the emulator main loop.
 *   Flipper Up    -> keyfob B1 (bit1 of reg 0xF8E) : up / increment PIN digit
 *   Flipper Ok    -> keyfob B2 (bit2)              : confirm / OK
 *   Flipper Down  -> keyfob B3 (bit4)              : menu / down
 *   Flipper Left  -> keyfob B5 (bit6)
 *   Flipper Right -> keyfob B6 (bit7)
 *   Up+Down       -> in-emulator overlay; inside it Left cycles RF OFF/TX/RX. */
static const char* const ABOUT_TEXT =
    "\e#Pandora DXL-5000 (PIC)\n"
    "Emulator of the real Pandora DXL-5000 car-alarm keyfob firmware "
    "(Microchip PIC18). The firmware runs unmodified; this app emulates the "
    "keyfob hardware: the SSD1306 OLED (128x32) and the physical buttons, and "
    "bridges the Flipper CC1101 for real RX/TX.\n"
    "\n"
    "\e#Buttons (Flipper key = keyfob button):\n"
    "Up = B1: up / increment PIN digit\n"
    "Ok = B2: confirm / OK\n"
    "Down = B3: menu / down\n"
    "Left = B5\n"
    "Right = B6\n"
    "Back: tap = start menu, hold = exit app\n"
    "\n"
    "\e#RF:\n"
    "Up+Down opens the overlay; inside it Ok continues, Left cycles RF "
    "OFF/TX/RX, Back returns to the start menu.\n"
    "\n"
    "\e#PIN:\n"
    "The 'full' firmware (dxl5000_full_pin2552) needs PIN 2552, set it in the "
    "menu before Launch. mario2 has no PIN.";

/* Rebuilds the submenu (updated Firmware/PIN labels). The submenu module COPIES
 * the label internally, so stack buffers are fine. */
static void menu_rebuild(AppState* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Pandora PIC");
    char buf[48];
    snprintf(buf, sizeof(buf), "FOB Firmware: %s", app->fw_name[0] ? app->fw_name : "<none>");
    submenu_add_item(app->submenu, buf, MenuItemFirmware, menu_submenu_callback, app);
    if(app->pin_value < 0)
        snprintf(buf, sizeof(buf), "PIN: none");
    else
        snprintf(buf, sizeof(buf), "PIN: %04ld%s", (long)app->pin_value, app->pin_auto ? " (auto)" : "");
    submenu_add_item(app->submenu, buf, MenuItemPin, menu_submenu_callback, app);
    submenu_add_item(app->submenu, "Launch", MenuItemLaunch, menu_submenu_callback, app);
    submenu_add_item(app->submenu, "About", MenuItemAbout, menu_submenu_callback, app);
}

/* submenu item selection callback */
static void menu_submenu_callback(void* context, uint32_t index) {
    AppState* app = (AppState*)context;
    switch(index) {
    case MenuItemFirmware:
        g_menu_flow = FlowFileBrowser;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    case MenuItemPin:
        /* the number_input is handled INSIDE the dispatcher: we switch views.
         * The result callback was set once in run_menu; here we only set the
         * header and the current value. */
        number_input_set_header_text(app->number_input, "PIN (e.g. 2552)");
        app->menu_current_view = PandoraViewNumberInput;
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewNumberInput);
        break;
    case MenuItemLaunch:
        g_menu_flow = FlowLaunch;
        view_dispatcher_stop(app->view_dispatcher);
        break;
    case MenuItemAbout:
        /* show the scrollable About screen; Back returns to the submenu
         * (handled by menu_back_callback). */
        widget_reset(app->about);
        widget_add_text_scroll_element(app->about, 0, 0, 128, 64, ABOUT_TEXT);
        app->menu_current_view = PandoraViewAbout;
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewAbout);
        break;
    default:
        break;
    }
}

/* number_input callback: stores the PIN and returns to the submenu */
static void menu_number_input_callback(void* context, int32_t number) {
    AppState* app = (AppState*)context;
    app->pin_value = number;
    app->pin_auto = false; /* user typed it */
    FURI_LOG_I(TAG, "PIN set in menu: %ld", (long)number);
    menu_rebuild(app);
    app->menu_current_view = PandoraViewSubmenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);
}

/* Global back: from the About screen it returns to the submenu; from the submenu
 * it exits the app. (number_input handles its own back internally.) */
static bool menu_back_callback(void* context) {
    AppState* app = (AppState*)context;
    if(app->menu_current_view == PandoraViewAbout) {
        app->menu_current_view = PandoraViewSubmenu;
        view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);
        return true;
    }
    g_menu_flow = FlowExit;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

/* Builds and runs the menu (blocks until stop). Returns the requested flow.
 * The view_dispatcher and its views are created and destroyed HERE, so that on
 * return the GUI is free for the file browser or direct-draw. */
static MenuFlow run_menu(AppState* app, Gui* gui) {
    g_menu_flow = FlowExit;

    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->number_input = number_input_alloc();
    app->about = widget_alloc();
    app->menu_current_view = PandoraViewSubmenu;

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, menu_back_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewNumberInput, number_input_get_view(app->number_input));
    view_dispatcher_add_view(
        app->view_dispatcher, PandoraViewAbout, widget_get_view(app->about));

    /* the number_input result callback is set once; the current value is passed
     * as current_number to pre-load the keyboard. */
    number_input_set_result_callback(
        app->number_input, menu_number_input_callback, app,
        app->pin_value < 0 ? 0 : app->pin_value, 0, 9999);

    /* submenu items */
    menu_rebuild(app);

    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, PandoraViewSubmenu);

    view_dispatcher_run(app->view_dispatcher); /* BLOCKS until stop */

    /* FULL teardown: without this the GUI is not free for the emulator */
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewNumberInput);
    view_dispatcher_remove_view(app->view_dispatcher, PandoraViewAbout);
    submenu_free(app->submenu);
    number_input_free(app->number_input);
    widget_free(app->about);
    view_dispatcher_free(app->view_dispatcher);
    app->submenu = NULL;
    app->number_input = NULL;
    app->about = NULL;
    app->view_dispatcher = NULL;

    return g_menu_flow;
}

/* Fills fw_name with the basename of the chosen path, for the menu label. */
/* ---- PIN auto-detect for the menu PIN box -------------------------------- */
/* (1) <stem>_PIN.txt beside the firmware (authoritative; written by the PIN
 * bruteforcer). Parse the first 1-4 digit run. Returns PIN or -1. */
static int pic_pin_from_txt(AppState* app) {
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
    char path[160];
    if(stem_end > (int)sizeof(path) - 16) return -1;
    memcpy(path, src, (size_t)stem_end);
    snprintf(path + stem_end, sizeof(path) - (size_t)stem_end, "_PIN.txt");

    File* f = storage_file_alloc(app->storage);
    int pin = -1;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[48];
        uint16_t n = storage_file_read(f, buf, sizeof(buf) - 1);
        buf[n] = 0;
        int i = 0;
        while(i < n && (buf[i] < '0' || buf[i] > '9')) i++;
        if(i < n) {
            int v = 0, cnt = 0;
            while(i < n && buf[i] >= '0' && buf[i] <= '9' && cnt < 4) {
                v = v * 10 + (buf[i] - '0');
                i++;
                cnt++;
            }
            if(cnt > 0) pin = v;
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    return (pin >= 0 && pin <= 9999) ? pin : -1;
}

/* Scan a code window (win[0] = byte at PROFILE_FULL.pin_validate) for the
 * "MOVF digit,w; SUBLW k" compare and return the 4-digit PIN, or -1. */
static int pic_pin_from_window(const uint8_t* win, uint32_t winlen) {
    const PicProfile* p = &PROFILE_FULL;
    int dig[4], found = 0;
    uint16_t prev = 0;
    bool have = false;
    for(uint32_t off = 0; off + 1 < winlen && found < 4; off += 2) {
        uint16_t w = (uint16_t)(win[off] | (win[off + 1] << 8));
        if(have) {
            uint8_t ph = (uint8_t)(prev >> 8);
            if((ph == 0x50u || ph == 0x51u) && (prev & 0xFFu) == (p->pin_slot_lo[found] & 0xFFu) &&
               (w & 0xFF00u) == 0x0800u) {
                dig[found] = (int)(w & 0xFFu);
                found++;
            }
        }
        prev = w;
        have = true;
    }
    if(found != 4) return -1;
    for(int i = 0; i < 4; i++)
        if(dig[i] > 9) return -1;
    return dig[0] * 1000 + dig[1] * 100 + dig[2] * 10 + dig[3];
}

/* (2) Read the hardcoded PIN literals straight from the firmware file (no full
 * load). Handles .bin (flat) and .hex (by address). mario2 has no code at the
 * full-profile validate address, so the window stays 0xFF and this returns -1. */
static int pic_pin_from_literals(AppState* app) {
    const char* path = furi_string_get_cstr(app->fw_path);
    uint32_t va = PROFILE_FULL.pin_validate;
    uint8_t win[64];
    memset(win, 0xFF, sizeof(win));
    File* f = storage_file_alloc(app->storage);
    int pin = -1;
    do {
        if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        uint8_t first = 0;
        if(storage_file_read(f, &first, 1) != 1) break;
        storage_file_seek(f, 0, true);
        if(first != ':') {
            if(storage_file_size(f) < (uint64_t)va + 8u) break;
            storage_file_seek(f, va, true);
            storage_file_read(f, win, sizeof(win));
        } else {
            static uint8_t chunk[512];
            static char line[600];
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
                            int cnt = pic_hexbyte(line), ah = pic_hexbyte(line + 2),
                                al = pic_hexbyte(line + 4), rt = pic_hexbyte(line + 6);
                            if(cnt >= 0 && ah >= 0 && al >= 0 && rt >= 0) {
                                uint32_t a = ((uint32_t)ah << 8) | (uint32_t)al;
                                const char* dp = line + 8;
                                if(rt == 0x04) {
                                    int b0 = pic_hexbyte(dp), b1 = pic_hexbyte(dp + 2);
                                    if(b0 >= 0 && b1 >= 0) ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 16;
                                } else if(rt == 0x02) {
                                    int b0 = pic_hexbyte(dp), b1 = pic_hexbyte(dp + 2);
                                    if(b0 >= 0 && b1 >= 0) ext = (((uint32_t)b0 << 8) | (uint32_t)b1) << 4;
                                } else if(rt == 0x00) {
                                    uint32_t base = ext + a;
                                    for(int k = 0; k < cnt; k++) {
                                        uint32_t aa = base + (uint32_t)k;
                                        if(aa >= va && aa < va + sizeof(win)) {
                                            int b = pic_hexbyte(dp + k * 2);
                                            if(b >= 0) win[aa - va] = (uint8_t)b;
                                        }
                                    }
                                } else if(rt == 0x01) {
                                    done = true;
                                }
                            }
                        }
                        lp = 0;
                    } else {
                        if(lp < sizeof(line) - 1) line[lp++] = ch;
                    }
                }
            }
        }
        pin = pic_pin_from_window(win, sizeof(win));
    } while(false);
    storage_file_close(f);
    storage_file_free(f);
    return pin;
}

/* Sets app->pin_value from a cracked _PIN.txt, else the code literals, else -1
 * ("none" -> load with no PIN entered). Sets pin_auto. Runs on fw selection. */
static void pic_detect_pin(AppState* app) {
    int pin = pic_pin_from_txt(app);
    if(pin < 0) pin = pic_pin_from_literals(app);
    if(pin >= 0) {
        app->pin_value = pin;
        app->pin_auto = true;
    } else {
        app->pin_value = -1;
        app->pin_auto = false;
    }
}

static void set_fw_name_from_path(AppState* app) {
    const char* p = furi_string_get_cstr(app->fw_path);
    const char* slash = strrchr(p, '/');
    const char* base = slash ? slash + 1 : p;
    strncpy(app->fw_name, base, sizeof(app->fw_name) - 1);
    app->fw_name[sizeof(app->fw_name) - 1] = 0;
    pic_detect_pin(app);
}

/* ------------------------------------------------------------ emulator */

/* Runs the emulator with direct-draw takeover. Returns on exit. Assumes the
 * firmware is ALREADY loaded and booted, and that the view_dispatcher was
 * already released. */
/* Event-driven Back handling (via the input pubsub). Reliable short/long
 * classification independent of how long an emulator frame (pic18_run) takes:
 * short Back -> return to the start menu, long Back -> exit the whole app.
 * Runs in the input service thread; only sets the volatile control flags. */
static void pandora_pic_input_cb(const void* value, void* context) {
    const InputEvent* event = value;
    AppState* app = context;
    if(event->key != InputKeyBack) return;
    if(event->type == InputTypeShort) {
        app->back_to_menu = true;
    } else if(event->type == InputTypeLong) {
        app->exit_requested = true;
    }
}

static void run_emulator(AppState* app, Gui* gui) {
    Canvas* canvas = NULL;
    bool fb_cb_added = false;
    FuriPubSub* input_events = NULL;
    FuriPubSubSubscription* input_sub = NULL;

    app->fb_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->fb_mutex) {
        FURI_LOG_E(TAG, "fb_mutex alloc failed");
        return;
    }
    app->rx_queue = furi_message_queue_alloc(RX_QUEUE_LEN, sizeof(RfEdge));
    if(!app->rx_queue) {
        FURI_LOG_E(TAG, "rx_queue alloc failed");
        furi_mutex_free(app->fb_mutex);
        app->fb_mutex = NULL;
        return;
    }

    radio_init(app);

    /* --- PIN injection after boot (mariofull) --- */
    enter_pin(app);

    app->gui = gui;
    gui_add_framebuffer_callback(gui, framebuffer_commit_callback, app);
    fb_cb_added = true;
    canvas = gui_direct_draw_acquire(gui);
    if(!canvas) {
        FURI_LOG_E(TAG, "direct_draw_acquire failed");
        goto teardown;
    }
    app->canvas = canvas;

    input_events = furi_record_open(RECORD_INPUT_EVENTS);
    input_sub = furi_pubsub_subscribe(input_events, pandora_pic_input_cb, app);

    FURI_LOG_I(TAG, "emulator: boot OK, main loop (steps/frame=%lu)",
               (unsigned long)app->steps_per_frame);

    uint32_t epoch = furi_get_tick();
    uint64_t next_us = 0;
    app->content_seen = false;
    app->status_active = false;
    app->launch_tick = furi_get_tick();

    while(!app->exit_requested && !app->back_to_menu) {
        if(app->menu_requested) {
            app->menu_requested = false;
            app->menu_active = true;
            overlay_draw(app);
            /* Display Setup overlay: Up=D/C src, Down=column, Right=rotate,
             * Left=RF, OK=back to the live screen, Back=start menu. The Up+Down
             * combo that opened it is still held, so seed those edges as pressed
             * to avoid a spurious first toggle. */
            bool left_prev = false, up_prev = true, down_prev = true, right_prev = false;
            while(app->menu_active && !app->exit_requested && !app->back_to_menu) {
                uint8_t k = poll_raw_keys();
                if(k & KBIT_BACK) {
                    app->back_to_menu = true; /* Back -> start menu */
                    app->menu_active = false;
                }
                if(k & KBIT_OK) app->menu_active = false;
                bool up = (k & KBIT_UP) != 0;
                bool down = (k & KBIT_DOWN) != 0;
                bool right = (k & KBIT_RIGHT) != 0;
                bool left = (k & KBIT_LEFT) != 0;
                /* don't act on Up/Down while BOTH are held (that is the open combo) */
                bool combo = up && down;
                if(up && !up_prev && !combo) {
                    app->disp_dc ^= 1u;
                    memset(app->gddram, 0, sizeof(app->gddram));
                    memset(app->gddram_work, 0, sizeof(app->gddram_work));
                    overlay_draw(app);
                }
                if(down && !down_prev && !combo) {
                    app->disp_colreset ^= 1u;
                    memset(app->gddram, 0, sizeof(app->gddram));
                    memset(app->gddram_work, 0, sizeof(app->gddram_work));
                    overlay_draw(app);
                }
                if(right && !right_prev) {
                    app->disp_rot = (uint8_t)((app->disp_rot + 1u) & 3u);
                    overlay_draw(app);
                }
                if(left && !left_prev) {
                    rf_cycle_mode(app);
                    overlay_draw(app);
                }
                up_prev = up;
                down_prev = down;
                right_prev = right;
                left_prev = left;
                furi_delay_ms(16);
            }
            epoch = furi_get_tick();
            next_us = 0;
            continue;
        }

        /* input -> keyfob buttons. Back is the app-level control: a short press
         * returns to the start menu; holding it for 700ms exits the whole app. */
        uint8_t keys = poll_raw_keys();
        apply_keys_to_buttons(app, keys);
        /* Back is handled event-driven in pandora_pic_input_cb (short -> start
         * menu, long -> exit); no per-frame GPIO timing here. */

        /* run a slice of the real firmware (captures TX via lat_write_hook) */
        pic18_run(app->cpu, app->steps_per_frame);

        /* RF bridge: replay accumulated TX / inject received RX */
        tx_bridge_flush(app);
        rx_bridge_pump(app);

        /* reconstruct the screen from the captured GDDRAM */
        render_gddram(app);

        /* blank-screen watchdog: never leave a bare black screen up */
        if(!app->content_seen && screen_lit_bytes(app) >= STATUS_CONTENT_BYTES) {
            app->content_seen = true;
        }
        app->status_active =
            !app->content_seen && !app->menu_active &&
            (furi_get_tick() - app->launch_tick > furi_ms_to_ticks(STATUS_GRACE_MS));

        /* present */
        if(app->status_active) {
            status_draw(app);
        } else if(!app->menu_active) {
            canvas_commit(canvas);
        }

        /* pacing + mandatory yield (don't starve the input service) */
        next_us += FRAME_US;
        uint32_t target = epoch + (uint32_t)((next_us + 500) / 1000);
        uint32_t now = furi_get_tick();
        int32_t ahead = (int32_t)(target - now);
        if(ahead > 4 * 33) {
            epoch = now;
            next_us = 0;
            furi_delay_ms(1);
        } else if(ahead > 0) {
            furi_delay_until_tick(target);
        } else {
            furi_delay_ms(1);
        }
    }

teardown:
    radio_deinit(app);
    if(input_sub) {
        furi_pubsub_unsubscribe(input_events, input_sub);
        input_sub = NULL;
    }
    if(input_events) {
        furi_record_close(RECORD_INPUT_EVENTS);
        input_events = NULL;
    }
    if(fb_cb_added) gui_remove_framebuffer_callback(gui, framebuffer_commit_callback, app);
    wait_inflight_zero(&s_fb_cb_inflight);
    if(canvas) gui_direct_draw_release(gui);
    if(app->rx_queue) {
        furi_message_queue_free(app->rx_queue);
        app->rx_queue = NULL;
    }
    if(app->fb_mutex) {
        furi_mutex_free(app->fb_mutex);
        app->fb_mutex = NULL;
    }
    FURI_LOG_I(TAG, "emulator: exit");
}

/* ------------------------------------------------------------ main */

int32_t pandora_pic_app(void* p) {
    UNUSED(p);

    AppState* app = (AppState*)malloc(sizeof(AppState));
    if(!app) return -1;
    memset(app, 0, sizeof(AppState));
    g_app = app;
    app->steps_per_frame = 150000; /* adjustable; the keyfob is not real-time */
    app->pin_value = -1; /* no PIN until a firmware is selected + auto-detected */
    /* display defaults = the current reconstruction; calibrate on hardware via
     * the Up+Down overlay (D/C source, column reset, rotation). */
    app->disp_dc = 0;       /* caller-address D/C */
    app->disp_colreset = 1; /* reset column on page cmd */
    app->disp_rot = 0;      /* no rotation */

    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    app->storage = storage;
    DialogsApp* dialogs = (DialogsApp*)furi_record_open(RECORD_DIALOGS);
    Gui* gui = (Gui*)furi_record_open(RECORD_GUI);

    app->fw_path = furi_string_alloc_set_str("/ext/apps_assets/pandora_pic");

    bool quit = false;

    /* Outer loop: PHASE 1 menu -> PHASE 2 emulator -> (short Back) back to the
     * menu, until Exit from the menu or a long-hold Back in the emulator. */
    while(!quit) {
        bool launch = false;

        /* --- PHASE 1: menu (view_dispatcher). Loop until Launch or Exit. --- */
        while(true) {
            MenuFlow flow = run_menu(app, gui);
            if(flow == FlowExit) {
                quit = true;
                break;
            } else if(flow == FlowFileBrowser) {
                /* the GUI is already free (view_dispatcher released in run_menu) */
                DialogsFileBrowserOptions opts;
                dialog_file_browser_set_basic_options(&opts, ".hex|.bin", NULL);
                opts.base_path = "/ext";
                opts.hide_ext = false;
                if(dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts)) {
                    set_fw_name_from_path(app);
                    FURI_LOG_I(TAG, "firmware chosen: %s", app->fw_name);
                }
                continue; /* re-create the menu */
            } else if(flow == FlowLaunch) {
                if(app->fw_name[0] == 0) {
                    FURI_LOG_W(TAG, "Launch without firmware; opening file browser");
                    DialogsFileBrowserOptions opts;
                    dialog_file_browser_set_basic_options(&opts, ".hex|.bin", NULL);
                    opts.base_path = "/ext";
                    opts.hide_ext = false;
                    if(!dialog_file_browser_show(dialogs, app->fw_path, app->fw_path, &opts)) {
                        continue; /* cancelled: return to the menu */
                    }
                    set_fw_name_from_path(app);
                }
                launch = true;
                break;
            }
        }
        if(quit) break;
        if(!launch) continue;

        /* --- PHASE 2: emulator (direct-draw). One session. --- */
        /* reset per-session state (AppState persists across sessions) */
        app->exit_requested = false;
        app->back_to_menu = false;
        app->menu_requested = false;
        app->menu_active = false;
        app->pin_entered = false;
        app->pin_verdict = 0;
        app->btn_mask = 0;
        memset(app->gddram, 0, sizeof(app->gddram));
        memset(app->gddram_work, 0, sizeof(app->gddram_work));
        app->gd_page = 0;
        app->gd_col = 0;
        app->gd_cmd_arg_left = 0;
        app->radio_on = false;
        app->rf_tx_enabled = false;
        app->rf_rx_enabled = false;
        app->tx_active = false;
        app->tx_head = 0;
        app->tx_tail = 0;
        app->tx_frames = 0;
        app->rx_events = 0;

        do {
            app->cpu = (Pic18Cpu*)malloc(sizeof(Pic18Cpu));
            if(!app->cpu) {
                FURI_LOG_E(TAG, "cpu alloc failed");
                break;
            }
            pic18_init(app->cpu);

            FURI_LOG_I(
                TAG, "free heap=%uK, max block=%uK, need prog=%uK",
                (unsigned)(memmgr_get_free_heap() / 1024u),
                (unsigned)(memmgr_heap_get_max_free_block() / 1024u),
                (unsigned)(PROG_MAX / 1024u));

            LoadResult lr = load_firmware(app, storage, furi_string_get_cstr(app->fw_path));
            if(lr != LoadOk) {
                char tbuf[64];
                if(lr == LoadNoMem)
                    snprintf(
                        tbuf, sizeof(tbuf), "Out of memory\nfree:%uK max:%uK",
                        (unsigned)(memmgr_get_free_heap() / 1024u),
                        (unsigned)(memmgr_heap_get_max_free_block() / 1024u));
                else if(lr == LoadBadFormat)
                    snprintf(tbuf, sizeof(tbuf), "Invalid format");
                else
                    snprintf(tbuf, sizeof(tbuf), "Load error");
                FURI_LOG_E(TAG, "load failed: %s", tbuf);
                DialogMessage* msg = dialog_message_alloc();
                dialog_message_set_text(msg, tbuf, 64, 30, AlignCenter, AlignCenter);
                dialog_message_set_buttons(msg, NULL, "OK", NULL);
                dialog_message_show(dialogs, msg);
                dialog_message_free(msg);
                break;
            }

            app->prof = pick_profile(furi_string_get_cstr(app->fw_path), app->cpu, app->flash_bk_size);
            FURI_LOG_I(TAG, "profile: %s (PIN=%s)", app->prof->name,
                       app->prof->has_pin ? "yes" : "no");

            /* core callbacks: display + buttons + RF (BEFORE boot: mariofull's
             * PORTB.1 handshake is used during the cinit). */
            app->cpu->on_code = code_hook;
            app->cpu->code_ctx = app;
            app->cpu->on_port_read = port_read_hook;
            app->cpu->port_read_ctx = app;
            app->cpu->on_lat_write = lat_write_hook;
            app->cpu->lat_write_ctx = app;

            if(!boot_firmware(app)) {
                FURI_LOG_E(TAG, "boot failed (init not complete)");
                DialogMessage* msg = dialog_message_alloc();
                dialog_message_set_text(
                    msg, "Boot failed", 64, 30, AlignCenter, AlignCenter);
                dialog_message_set_buttons(msg, NULL, "OK", NULL);
                dialog_message_show(dialogs, msg);
                dialog_message_free(msg);
                break;
            }
            FURI_LOG_I(TAG, "boot OK -> PC=0x%06lX", (unsigned long)app->cpu->pc);

            /* start the keyfob's periodic timer tick so the OLED refreshes */
            arm_keyfob_tick(app);

            run_emulator(app, gui);
        } while(false);

        /* free per-session resources so a re-launch starts clean */
        if(app->cpu) {
            free(app->cpu);
            app->cpu = NULL;
        }
        pic_free_flash(app, storage);

        /* a long-hold Back in the emulator exits the app; a short Back, an
         * overlay Back, or a load/boot error returns to the start menu */
        if(app->exit_requested) quit = true;
    }

    /* --- teardown --- */
    if(app->cpu) free(app->cpu);
    pic_free_flash(app, storage);
    furi_string_free(app->fw_path);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    g_app = NULL;
    return 0;
}
