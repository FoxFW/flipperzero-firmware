/*
 * pic18_core.h - Functional PIC18 CPU interpreter / emulator (16-bit core).
 *
 * Faithful, instruction-by-instruction port of emu/pic18cpu.py (verified
 * reference python). Same opcode semantics, STATUS flags (C/DC/Z/OV/N), banks
 * (BSR, bit access 'a'), FSR0/1/2 with INDF/POSTINC/POSTDEC/PREINC/PLUSW,
 * 31-level return stack, fast stack (bit 's'), TBLPTR/TABLAT, PRODH:L, SFRs
 * 0xF80-0xFFF.
 *
 * Goal: compilable both for host (gcc -std=c11) and for Flipper Zero / ARM
 * Cortex-M4 (arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os). No dependencies
 * beyond <stdint.h>, <string.h>, <stdlib.h> (and <stdio.h> only in the file
 * parser, guarded with #ifdef PIC18_HOST).
 *
 * ---------------------------------------------------------------------------
 * PROGRAM MEMORY (important for the Flipper):
 *   The program buffer `prog` is NOT owned by the core: it is a pointer to a
 *   buffer allocated by the caller (e.g. safe_malloc of the real .hex size,
 *   ~110KB) plus the config words. The core receives prog as a pointer + size
 *   via pic18_set_prog(). On host you can allocate 0x20000 (128KB). On the
 *   Flipper only what's needed is allocated. Addresses outside [0, prog_size)
 *   return 0xFF on read (same as the not-present region of the Python).
 * ---------------------------------------------------------------------------
 */
#ifndef PIC18_CORE_H
#define PIC18_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------- constants */
#define PIC18_PROG_SIZE 0x20000u /* covers up to 0x1A180 with margin (host) */
#define PIC18_RAM_SIZE  0x1000u  /* data RAM + SFR (banks 0..F)            */
#define PIC18_STACK_LEVELS 31    /* return stack                           */

/* STATUS bit positions (PIC18) */
#define PIC18_ST_C  0
#define PIC18_ST_DC 1
#define PIC18_ST_Z  2
#define PIC18_ST_OV 3
#define PIC18_ST_N  4

/* Absolute addresses (in the data space) of SFRs used by the API */
#define PIC18_SFR_WREG    0xFE8u
#define PIC18_SFR_STATUS  0xFD8u
#define PIC18_SFR_BSR     0xFE0u
#define PIC18_SFR_PCLATH  0xFFAu
#define PIC18_SFR_PCLATU  0xFFBu

struct Pic18Cpu; /* fwd */

/* -------------------------------------------------------------- callbacks ---
 * Equivalent to the python hooks (on_port_read / on_lat_write /
 * on_port_write / address trace). Each carries its own void* ctx.
 */

/* Read of a PORTx. Must return 0..255 to override, or -1 to keep the default
 * behavior (default_port). addr is the absolute address of the SFR
 * (0xF80..0xF84). */
typedef int (*Pic18PortRead)(struct Pic18Cpu* cpu, uint16_t addr, void* ctx);

/* Write to a LATx or PORTx (bit-bang TX). addr is the absolute address of the
 * SFR; val is the byte written. */
typedef void (*Pic18PortWrite)(struct Pic18Cpu* cpu, uint16_t addr, uint8_t val, void* ctx);

/* Code hook: invoked on entry (PC) to any instruction, with the current pc.
 * Equivalent to the set of address hooks + trace in the python; the caller
 * decides whether to filter by pc. */
typedef void (*Pic18CodeHook)(struct Pic18Cpu* cpu, uint32_t pc, void* ctx);

/* Interrupt-poll hook: lets the frontend model an interrupt source the core
 * does not (e.g. a board whose firmware dispatches its timer tick through its
 * own software flags rather than INTCON/PIE). Called between instructions,
 * only while the global interrupt enable is set and no on-chip interrupt is
 * already pending. Return the vector address to take (e.g. 0x000008), or a
 * negative value for "nothing to service". The core performs the actual
 * vectoring (push PC, shadow WREG/STATUS/BSR, clear GIE, jump), so RETFIE
 * returns and re-enables interrupts exactly as for a hardware interrupt. */
typedef int32_t (*Pic18IrqPoll)(struct Pic18Cpu* cpu, void* ctx);

/* Program-memory read hook: lets the frontend demand-page the code image from
 * external storage instead of holding all of it in RAM. When set, every code
 * fetch (instruction fetch and TBLRD of code addresses < 0x300000) is routed
 * here instead of indexing prog[]. Must return the byte at absolute code
 * address a (0xFF for addresses the firmware does not populate). Config words
 * (>= 0x300000) are NOT routed here - they stay in the config table. */
typedef uint8_t (*Pic18ProgRead)(struct Pic18Cpu* cpu, uint32_t a, void* ctx);

/* Program-memory write hook (TBLWT). When set, flash writes are routed here
 * instead of into prog[]. These keyfob firmwares do not self-program flash;
 * the frontend's paged image is read-only and simply drops writes. */
typedef void (*Pic18ProgWrite)(struct Pic18Cpu* cpu, uint32_t a, uint8_t v, void* ctx);

/* ----------------------------------------------------------------- state --- */
typedef struct Pic18Cpu {
    /* Program memory: pointer owned by the caller (see header). */
    uint8_t* prog;
    uint32_t prog_size;

    /* Config words 0x300000+ (like self.config in the python). Small and
     * sparse: we store it as a linear address->byte table. */
    uint32_t config_addr[64];
    uint8_t  config_val[64];
    uint16_t config_count;

    uint8_t ram[PIC18_RAM_SIZE]; /* data RAM + SFR */

    /* Core registers */
    uint32_t pc;                 /* 21 bits */
    uint8_t  w;
    uint32_t stack[PIC18_STACK_LEVELS];
    uint8_t  stkptr;             /* number of used entries */
    uint8_t  ws;                 /* fast return shadow: WREG  */
    uint8_t  statuss;            /* fast return shadow: STATUS*/
    uint8_t  bsrs;               /* fast return shadow: BSR   */

    uint64_t cycles;
    uint64_t steps;
    uint8_t  halted;
    uint8_t  sleeping;

    /* Timer + interrupt subsystem (see pic18_core.c "timers_advance" and
     * "service_interrupt"). The timer registers themselves live in ram[] at
     * their SFR addresses; these are the prescaler/postscaler accumulators and
     * the TMR0 16-bit write buffer that the register model needs on the side. */
    uint32_t t0_presc;   /* TMR0 prescaler cycle accumulator            */
    uint8_t  t0_wrbuf;   /* TMR0 16-bit write buffer (written via TMR0H) */
    uint32_t t1_presc;   /* TMR1 prescaler cycle accumulator            */
    uint32_t t2_presc;   /* TMR2 prescaler cycle accumulator            */
    uint8_t  t2_postsc;  /* TMR2 postscaler accumulator                 */
    uint32_t t3_presc;   /* TMR3 prescaler cycle accumulator            */

    /* Default value for non-intercepted PORT reads */
    uint8_t  default_port;

    /* Callbacks + contexts */
    Pic18PortRead  on_port_read;
    void*          port_read_ctx;
    Pic18PortWrite on_lat_write;
    void*          lat_write_ctx;
    Pic18PortWrite on_port_write;
    void*          port_write_ctx;
    Pic18CodeHook  on_code;      /* address trace/hook (optional) */
    void*          code_ctx;
    Pic18IrqPoll   on_irq;       /* frontend interrupt source (optional) */
    void*          irq_ctx;

    /* Optional program-memory paging hooks (see typedefs above). When on_prog
     * is set, prog[] is not used for code reads; when on_prog_write is set,
     * TBLWT is routed to it. Both default to NULL (direct prog[] access). */
    Pic18ProgRead  on_prog;      /* code-read hook (paging)            */
    void*          prog_ctx;
    Pic18ProgWrite on_prog_write;/* code-write hook (TBLWT; optional)  */
    void*          progw_ctx;
} Pic18Cpu;

/* ---------------------------------------------------------------- API ------- */

/* Initializes the structure to zero and sets prog = NULL. Call before anything. */
void pic18_init(Pic18Cpu* cpu);

/* Assigns the program buffer (owned by the caller). size in bytes. */
void pic18_set_prog(Pic18Cpu* cpu, uint8_t* prog, uint32_t size);

/* Core reset (PC=0, SFRs to typical POR, RAM to 0, TRIS=0xFF). */
void pic18_reset(Pic18Cpu* cpu);

/* Loads Intel HEX from a buffer in memory (the .hex read into RAM). Fills
 * prog[] by real address and stores config words 0x300000+. Returns 0 OK,
 * <0 on format error. Requires prog already assigned (pic18_set_prog). */
int pic18_load_hex_mem(Pic18Cpu* cpu, const uint8_t* hexdata, size_t size);

/* Loads a flat binary into prog[base..]. Returns 0 OK, <0 if it doesn't fit. */
int pic18_load_prog(Pic18Cpu* cpu, const uint8_t* bin, size_t size, uint32_t base);

#ifdef PIC18_HOST
/* Loads Intel HEX from a file (host only). Returns 0 OK, <0 error. */
int pic18_load_hex(Pic18Cpu* cpu, const char* path);
#endif

/* Executes one instruction. Returns 1 if still alive, 0 if halted. */
int pic18_step(Pic18Cpu* cpu);

/* Executes up to max_steps or halted. Returns number of steps executed. */
uint64_t pic18_run(Pic18Cpu* cpu, uint64_t max_steps);

/* Data memory access with SFR semantics (same as read/write_data). */
uint8_t pic18_read_data(Pic18Cpu* cpu, uint16_t addr);
void    pic18_write_data(Pic18Cpu* cpu, uint16_t addr, uint8_t val);

/* Raw RAM access (without SFR semantics), equivalent to read_ram/write_ram. */
uint8_t pic18_read_ram(Pic18Cpu* cpu, uint16_t addr);
void    pic18_write_ram(Pic18Cpu* cpu, uint16_t addr, uint8_t val);

/* SFR by absolute address. get uses read semantics (read_data);
 * set writes raw into ram[] (same as set_sfr in the python). */
uint8_t pic18_get_sfr_addr(Pic18Cpu* cpu, uint16_t addr);
void    pic18_set_sfr_addr(Pic18Cpu* cpu, uint16_t addr, uint8_t val);

/* SFR by name (e.g. "STATUS"). Returns address 0 if it does not exist. */
uint16_t pic18_sfr_addr_by_name(const char* name);
int      pic18_get_sfr(Pic18Cpu* cpu, const char* name, uint8_t* out);
int      pic18_set_sfr(Pic18Cpu* cpu, const char* name, uint8_t val);

/* Frequently used state helpers */
uint8_t  pic18_status(const Pic18Cpu* cpu);
uint8_t  pic18_bsr(const Pic18Cpu* cpu);
uint16_t pic18_fsr(const Pic18Cpu* cpu, int n);

/* Return stack (for setting up return sentinels in tests/harness). */
void     pic18_push(Pic18Cpu* cpu, uint32_t addr);
uint32_t pic18_pop(Pic18Cpu* cpu);

#ifdef __cplusplus
}
#endif

#endif /* PIC18_CORE_H */
