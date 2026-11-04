/*
 * pic18_core.c - PIC18 interpreter implementation.
 *
 * Faithful, instruction-by-instruction port of emu/pic18cpu.py.
 */
#include "pic18_core.h"
#include "ihex.h"

#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ SFR map
 * Absolute address (0xF80-0xFFF) -> name. Exact replica of SFR_ADDR from the
 * python. The names are used for the by-name API; the execution logic uses the
 * addresses directly (same as the python via SFR_NAME).
 */
typedef struct {
    uint16_t addr;
    const char* name;
} SfrEntry;

static const SfrEntry SFR_TABLE[] = {
    {0xFFF, "TOSU"},   {0xFFE, "TOSH"},    {0xFFD, "TOSL"},    {0xFFC, "STKPTR"},
    {0xFFB, "PCLATU"}, {0xFFA, "PCLATH"},  {0xFF9, "PCL"},     {0xFF8, "TBLPTRU"},
    {0xFF7, "TBLPTRH"},{0xFF6, "TBLPTRL"}, {0xFF5, "TABLAT"},  {0xFF4, "PRODH"},
    {0xFF3, "PRODL"},  {0xFF2, "INTCON"},  {0xFF1, "INTCON2"}, {0xFF0, "INTCON3"},
    {0xFEF, "INDF0"},  {0xFEE, "POSTINC0"},{0xFED, "POSTDEC0"},{0xFEC, "PREINC0"},
    {0xFEB, "PLUSW0"}, {0xFEA, "FSR0H"},   {0xFE9, "FSR0L"},   {0xFE8, "WREG"},
    {0xFE7, "INDF1"},  {0xFE6, "POSTINC1"},{0xFE5, "POSTDEC1"},{0xFE4, "PREINC1"},
    {0xFE3, "PLUSW1"}, {0xFE2, "FSR1H"},   {0xFE1, "FSR1L"},   {0xFE0, "BSR"},
    {0xFDF, "INDF2"},  {0xFDE, "POSTINC2"},{0xFDD, "POSTDEC2"},{0xFDC, "PREINC2"},
    {0xFDB, "PLUSW2"}, {0xFDA, "FSR2H"},   {0xFD9, "FSR2L"},   {0xFD8, "STATUS"},
    {0xFD7, "TMR0H"},  {0xFD6, "TMR0L"},   {0xFD5, "T0CON"},   {0xFD3, "OSCCON"},
    {0xFD2, "LVDCON"}, {0xFD1, "WDTCON"},  {0xFD0, "RCON"},    {0xFCF, "TMR1H"},
    {0xFCE, "TMR1L"},  {0xFCD, "T1CON"},   {0xFCC, "TMR2"},    {0xFCB, "PR2"},
    {0xFCA, "T2CON"},  {0xFC9, "SSPBUF"},  {0xFC8, "SSPADD"},  {0xFC7, "SSPSTAT"},
    {0xFC6, "SSPCON1"},{0xFC5, "SSPCON2"}, {0xFC4, "ADRESH"},  {0xFC3, "ADRESL"},
    {0xFC2, "ADCON0"}, {0xFC1, "ADCON1"},  {0xFC0, "ADCON2"},  {0xFBF, "CCPR1H"},
    {0xFBE, "CCPR1L"}, {0xFBD, "CCP1CON"}, {0xFBC, "CCPR2H"},  {0xFBB, "CCPR2L"},
    {0xFBA, "CCP2CON"},{0xFB7, "BAUDCON"}, {0xFB4, "ECCP1DEL"},{0xFB3, "TMR3H"},
    {0xFB2, "TMR3L"},  {0xFB1, "T3CON"},   {0xFB0, "SPBRGH"},  {0xFAF, "SPBRG"},
    {0xFAE, "RCREG"},  {0xFAD, "TXREG"},   {0xFAC, "TXSTA"},   {0xFAB, "RCSTA"},
    {0xFAA, "EEADRH"}, {0xFA9, "EEADR"},   {0xFA8, "EEDATA"},  {0xFA7, "EECON2"},
    {0xFA6, "EECON1"}, {0xFA4, "IPR3"},    {0xFA3, "PIR3"},    {0xFA2, "PIE3"},
    {0xFA1, "IPR2"},   {0xFA0, "PIR2"},    {0xF9F, "PIE2"},    {0xF9E, "IPR1"},
    {0xF9D, "PIR1"},   {0xF9C, "PIE1"},    {0xF9B, "OSCTUNE"}, {0xF98, "TRISE"},
    {0xF97, "TRISD"},  {0xF96, "TRISC"},   {0xF95, "TRISB"},   {0xF94, "TRISA"},
    {0xF8E, "LATE"},   {0xF8D, "LATD"},    {0xF8C, "LATC"},    {0xF8B, "LATB"},
    {0xF8A, "LATA"},   {0xF84, "PORTE"},   {0xF83, "PORTD"},   {0xF82, "PORTC"},
    {0xF81, "PORTB"},  {0xF80, "PORTA"},
};
#define SFR_TABLE_N (sizeof(SFR_TABLE) / sizeof(SFR_TABLE[0]))

/* SFR addresses used during execution (equivalent to SFR_NAME['...']) */
#define A_TOSU    0xFFF
#define A_TOSH    0xFFE
#define A_TOSL    0xFFD
#define A_STKPTR  0xFFC
#define A_PCLATU  0xFFB
#define A_PCLATH  0xFFA
#define A_PCL     0xFF9
#define A_TBLPTRU 0xFF8
#define A_TBLPTRH 0xFF7
#define A_TBLPTRL 0xFF6
#define A_TABLAT  0xFF5
#define A_PRODH   0xFF4
#define A_PRODL   0xFF3
#define A_INTCON  0xFF2
#define A_FSR0H   0xFEA
#define A_FSR0L   0xFE9
#define A_WREG    0xFE8
#define A_FSR1H   0xFE2
#define A_FSR1L   0xFE1
#define A_BSR     0xFE0
#define A_FSR2H   0xFDA
#define A_FSR2L   0xFD9
#define A_STATUS  0xFD8

/* Timer + interrupt SFRs */
#define A_INTCON2 0xFF1
#define A_INTCON3 0xFF0
#define A_TMR0H   0xFD7
#define A_TMR0L   0xFD6
#define A_T0CON   0xFD5
#define A_RCON    0xFD0
#define A_TMR1H   0xFCF
#define A_TMR1L   0xFCE
#define A_T1CON   0xFCD
#define A_TMR2    0xFCC
#define A_PR2     0xFCB
#define A_T2CON   0xFCA
#define A_TMR3H   0xFB3
#define A_TMR3L   0xFB2
#define A_T3CON   0xFB1
#define A_PIR1    0xF9D
#define A_PIE1    0xF9C
#define A_IPR1    0xF9E
#define A_PIR2    0xFA0
#define A_PIE2    0xF9F
#define A_IPR2    0xFA1
#define A_PIR3    0xFA3
#define A_PIE3    0xFA2
#define A_IPR3    0xFA4

/* INTCON bits */
#define INTCON_GIE    0x80  /* GIE / GIEH            */
#define INTCON_PEIE   0x40  /* PEIE / GIEL           */
#define INTCON_TMR0IE 0x20
#define INTCON_INT0IE 0x10
#define INTCON_RBIE   0x08
#define INTCON_TMR0IF 0x04
#define INTCON_INT0IF 0x02
#define INTCON_RBIF   0x01
/* INTCON2 priority bits (used when RCON.IPEN=1) */
#define INTCON2_TMR0IP 0x04
#define INTCON2_RBIP   0x01
/* PIR1/PIE1 bits */
#define PIR1_TMR1IF 0x01
#define PIR1_TMR2IF 0x02
/* PIR2/PIE2 bits */
#define PIR2_TMR3IF 0x02
/* RCON bits */
#define RCON_IPEN   0x80

/* Interrupt vectors */
#define VEC_HIGH 0x000008u
#define VEC_LOW  0x000018u

/*
 * Fast SFR classification by address. 0 = normal RAM (not a special SFR);
 * the rest are "classes" that replicate the branches of _read_sfr/_write_sfr in
 * the python. We precompute a table of 0x80 entries (0xF80..0xFFF).
 */
enum {
    SFRK_NONE = 0,
    SFRK_INDF0, SFRK_POSTINC0, SFRK_POSTDEC0, SFRK_PREINC0, SFRK_PLUSW0,
    SFRK_INDF1, SFRK_POSTINC1, SFRK_POSTDEC1, SFRK_PREINC1, SFRK_PLUSW1,
    SFRK_INDF2, SFRK_POSTINC2, SFRK_POSTDEC2, SFRK_PREINC2, SFRK_PLUSW2,
    SFRK_WREG, SFRK_PCL, SFRK_TOSL, SFRK_TOSH, SFRK_TOSU, SFRK_STKPTR,
    SFRK_PORT, SFRK_LAT
};

static uint8_t sfr_kind[0x80]; /* indexed by (addr - 0xF80) */
static int sfr_kind_ready = 0;

static void sfr_kind_init(void) {
    if(sfr_kind_ready) return;
    memset(sfr_kind, SFRK_NONE, sizeof(sfr_kind));
    sfr_kind[0xFEF - 0xF80] = SFRK_INDF0;
    sfr_kind[0xFEE - 0xF80] = SFRK_POSTINC0;
    sfr_kind[0xFED - 0xF80] = SFRK_POSTDEC0;
    sfr_kind[0xFEC - 0xF80] = SFRK_PREINC0;
    sfr_kind[0xFEB - 0xF80] = SFRK_PLUSW0;
    sfr_kind[0xFE7 - 0xF80] = SFRK_INDF1;
    sfr_kind[0xFE6 - 0xF80] = SFRK_POSTINC1;
    sfr_kind[0xFE5 - 0xF80] = SFRK_POSTDEC1;
    sfr_kind[0xFE4 - 0xF80] = SFRK_PREINC1;
    sfr_kind[0xFE3 - 0xF80] = SFRK_PLUSW1;
    sfr_kind[0xFDF - 0xF80] = SFRK_INDF2;
    sfr_kind[0xFDE - 0xF80] = SFRK_POSTINC2;
    sfr_kind[0xFDD - 0xF80] = SFRK_POSTDEC2;
    sfr_kind[0xFDC - 0xF80] = SFRK_PREINC2;
    sfr_kind[0xFDB - 0xF80] = SFRK_PLUSW2;
    sfr_kind[0xFE8 - 0xF80] = SFRK_WREG;
    sfr_kind[0xFF9 - 0xF80] = SFRK_PCL;
    sfr_kind[0xFFD - 0xF80] = SFRK_TOSL;
    sfr_kind[0xFFE - 0xF80] = SFRK_TOSH;
    sfr_kind[0xFFF - 0xF80] = SFRK_TOSU;
    sfr_kind[0xFFC - 0xF80] = SFRK_STKPTR;
    /* PORTA..PORTE */
    sfr_kind[0xF80 - 0xF80] = SFRK_PORT;
    sfr_kind[0xF81 - 0xF80] = SFRK_PORT;
    sfr_kind[0xF82 - 0xF80] = SFRK_PORT;
    sfr_kind[0xF83 - 0xF80] = SFRK_PORT;
    sfr_kind[0xF84 - 0xF80] = SFRK_PORT;
    /* LATA..LATE */
    sfr_kind[0xF8A - 0xF80] = SFRK_LAT;
    sfr_kind[0xF8B - 0xF80] = SFRK_LAT;
    sfr_kind[0xF8C - 0xF80] = SFRK_LAT;
    sfr_kind[0xF8D - 0xF80] = SFRK_LAT;
    sfr_kind[0xF8E - 0xF80] = SFRK_LAT;
    sfr_kind_ready = 1;
}

static uint8_t kind_of(uint16_t addr) {
    if(addr >= 0xF80 && addr <= 0xFFF) return sfr_kind[addr - 0xF80];
    return SFRK_NONE;
}

/* ---- opcode tables (f,d,a) and (f,a) and bit, replica of _FD_OPS/_FA_OPS/_BIT_OPS */
enum {
    FD_NONE = 0,
    FD_ADDWF, FD_ADDWFC, FD_ANDWF, FD_DECF, FD_IORWF, FD_MOVF, FD_RLCF,
    FD_RRCF, FD_SUBWFB, FD_SUBWF, FD_DCFSNZ, FD_COMF, FD_INFSNZ, FD_INCF,
    FD_SUBFWB, FD_XORWF, FD_INCFSZ, FD_DECFSZ, FD_RLNCF, FD_SWAPF, FD_RRNCF
};

/* returns FD_* code for base (op & 0xFC00), or FD_NONE */
static int fd_op(uint16_t base) {
    switch(base) {
    case 0x2400: return FD_ADDWF;
    case 0x2000: return FD_ADDWFC;
    case 0x1400: return FD_ANDWF;
    case 0x0400: return FD_DECF;
    case 0x1000: return FD_IORWF;
    case 0x5000: return FD_MOVF;
    case 0x3400: return FD_RLCF;
    case 0x4400: return FD_RRCF;
    case 0x5800: return FD_SUBWFB;
    case 0x5400: return FD_SUBWF;
    case 0x4C00: return FD_DCFSNZ;
    case 0x1C00: return FD_COMF;
    case 0x4800: return FD_INFSNZ;
    case 0x2800: return FD_INCF;
    case 0x5C00: return FD_SUBFWB;
    case 0x1800: return FD_XORWF;
    case 0x3C00: return FD_INCFSZ;
    case 0x2C00: return FD_DECFSZ;
    case 0x4000: return FD_RLNCF;
    case 0x3000: return FD_SWAPF;
    case 0x3800: return FD_RRNCF;
    default:     return FD_NONE;
    }
}

enum {
    FA_NONE = 0,
    FA_MOVWF, FA_CLRF, FA_SETF, FA_NEGF, FA_TSTFSZ, FA_CPFSGT, FA_CPFSEQ,
    FA_CPFSLT, FA_MULWF
};

static int fa_op(uint16_t base) {
    switch(base) {
    case 0x6E00: return FA_MOVWF;
    case 0x6A00: return FA_CLRF;
    case 0x6800: return FA_SETF;
    case 0x6C00: return FA_NEGF;
    case 0x6600: return FA_TSTFSZ;
    case 0x6400: return FA_CPFSGT;
    case 0x6200: return FA_CPFSEQ;
    case 0x6000: return FA_CPFSLT;
    case 0x0200: return FA_MULWF;
    default:     return FA_NONE;
    }
}

/* bit-oriented: hi4 (op>>12). NON-permuted mapping (known bug already fixed):
 *   BTG=0x7, BSF=0x8, BCF=0x9, BTFSS=0xA, BTFSC=0xB  */
enum { BIT_NONE = 0, BIT_BTG, BIT_BSF, BIT_BCF, BIT_BTFSS, BIT_BTFSC };
static int bit_op(uint8_t hi4) {
    switch(hi4) {
    case 0x7: return BIT_BTG;
    case 0x8: return BIT_BSF;
    case 0x9: return BIT_BCF;
    case 0xA: return BIT_BTFSS;
    case 0xB: return BIT_BTFSC;
    default:  return BIT_NONE;
    }
}

/* ------------------------------------------------------------ internal prototypes */
static uint8_t read_data(Pic18Cpu* c, uint16_t addr);
static void    write_data(Pic18Cpu* c, uint16_t addr, uint8_t val);
static int     fsr_access(Pic18Cpu* c, int n, uint8_t kind, int write, uint8_t wval);

/* --------------------------------------------------------------- utilities */
/* Single code byte: routed through the paging hook when the frontend installs
 * one (on_prog), else a direct prog[] index with 0xFF past the end. */
static uint8_t code_rd(Pic18Cpu* c, uint32_t a) {
    if(c->on_prog) return c->on_prog(c, a, c->prog_ctx);
    return (a < c->prog_size) ? c->prog[a] : 0xFF;
}

static uint16_t pword(Pic18Cpu* c, uint32_t a) {
    uint8_t b0 = code_rd(c, a);
    uint8_t b1 = code_rd(c, a + 1);
    return (uint16_t)(b0 | (b1 << 8));
}

static uint8_t st_get(Pic18Cpu* c, int bit) {
    return (uint8_t)((c->ram[A_STATUS] >> bit) & 1);
}

static void st_set(Pic18Cpu* c, int bit, int val) {
    uint8_t s = c->ram[A_STATUS];
    if(val)
        s |= (uint8_t)(1u << bit);
    else
        s &= (uint8_t)~(1u << bit);
    c->ram[A_STATUS] = s;
}

static uint8_t bsr4(Pic18Cpu* c) {
    return (uint8_t)(c->ram[A_BSR] & 0xF);
}

/* Z and N flags for logic/move ops */
static uint8_t set_flags_logic(Pic18Cpu* c, uint32_t result) {
    uint8_t r = (uint8_t)(result & 0xFF);
    st_set(c, PIC18_ST_Z, r == 0);
    st_set(c, PIC18_ST_N, (r >> 7) & 1);
    return r;
}

static uint8_t set_flags_add(Pic18Cpu* c, uint8_t a, uint8_t b, int carry_in) {
    uint32_t res = (uint32_t)a + (uint32_t)b + (uint32_t)carry_in;
    int cc = (res > 0xFF) ? 1 : 0;
    uint8_t res8 = (uint8_t)(res & 0xFF);
    int dc = (((a & 0xF) + (b & 0xF) + carry_in) > 0xF) ? 1 : 0;
    int ov = ((~(a ^ b) & (a ^ res8)) & 0x80) ? 1 : 0;
    st_set(c, PIC18_ST_C, cc);
    st_set(c, PIC18_ST_DC, dc);
    st_set(c, PIC18_ST_Z, res8 == 0);
    st_set(c, PIC18_ST_OV, ov);
    st_set(c, PIC18_ST_N, (res8 >> 7) & 1);
    return res8;
}

/* PIC18 subtract = a + (~b) + borrow_in. borrow_in=1 => normal subtract. */
static uint8_t set_flags_sub(Pic18Cpu* c, uint8_t a, uint8_t b, int borrow_in) {
    uint8_t nb = (uint8_t)((~b) & 0xFF);
    uint32_t res = (uint32_t)a + (uint32_t)nb + (uint32_t)borrow_in;
    int cc = (res > 0xFF) ? 1 : 0;
    uint8_t res8 = (uint8_t)(res & 0xFF);
    int dc = (((a & 0xF) + (nb & 0xF) + borrow_in) > 0xF) ? 1 : 0;
    int ov = ((~(a ^ nb) & (a ^ res8)) & 0x80) ? 1 : 0;
    st_set(c, PIC18_ST_C, cc);
    st_set(c, PIC18_ST_DC, dc);
    st_set(c, PIC18_ST_Z, res8 == 0);
    st_set(c, PIC18_ST_OV, ov);
    st_set(c, PIC18_ST_N, (res8 >> 7) & 1);
    return res8;
}

/* f address resolution */
static uint16_t resolve_f(Pic18Cpu* c, uint8_t f, int is_access) {
    if(is_access) {
        if(f >= 0x80) return (uint16_t)(0xF00 + f);
        return f;
    }
    return (uint16_t)((bsr4(c) << 8) | f);
}

/* ------------------------------------------------------- return stack */
static uint32_t cpu_tos(Pic18Cpu* c) {
    if(c->stkptr == 0) return 0;
    return c->stack[c->stkptr - 1];
}

void pic18_push(Pic18Cpu* c, uint32_t addr) {
    if(c->stkptr < PIC18_STACK_LEVELS) {
        c->stack[c->stkptr] = addr & 0x1FFFFF;
        c->stkptr++;
    } else {
        c->stack[PIC18_STACK_LEVELS - 1] = addr & 0x1FFFFF;
    }
}

uint32_t pic18_pop(Pic18Cpu* c) {
    if(c->stkptr > 0) {
        c->stkptr--;
        return c->stack[c->stkptr];
    }
    return 0;
}

/* --------------------------------------------------- indirect FSR */
static uint16_t fsr_get(Pic18Cpu* c, int n) {
    uint16_t h, l;
    if(n == 0) { h = c->ram[A_FSR0H]; l = c->ram[A_FSR0L]; }
    else if(n == 1) { h = c->ram[A_FSR1H]; l = c->ram[A_FSR1L]; }
    else { h = c->ram[A_FSR2H]; l = c->ram[A_FSR2L]; }
    return (uint16_t)(((h & 0xF) << 8) | l);
}

static void fsr_set(Pic18Cpu* c, int n, uint16_t v) {
    v &= 0xFFF;
    if(n == 0) { c->ram[A_FSR0H] = (v >> 8) & 0xF; c->ram[A_FSR0L] = v & 0xFF; }
    else if(n == 1) { c->ram[A_FSR1H] = (v >> 8) & 0xF; c->ram[A_FSR1L] = v & 0xFF; }
    else { c->ram[A_FSR2H] = (v >> 8) & 0xF; c->ram[A_FSR2L] = v & 0xFF; }
}

/* kind must be one of INDF/POSTINC/POSTDEC/PREINC/PLUSW of a bank n.
 * write=0 reads (wval ignored); write=1 writes wval. Returns value read or
 * wval written. */
static int fsr_access(Pic18Cpu* c, int n, uint8_t kind, int write, uint8_t wval) {
    uint16_t ptr = fsr_get(c, n);
    uint16_t eff;
    int is_preinc = (kind == SFRK_PREINC0 || kind == SFRK_PREINC1 || kind == SFRK_PREINC2);
    int is_plusw  = (kind == SFRK_PLUSW0 || kind == SFRK_PLUSW1 || kind == SFRK_PLUSW2);
    int is_postinc = (kind == SFRK_POSTINC0 || kind == SFRK_POSTINC1 || kind == SFRK_POSTINC2);
    int is_postdec = (kind == SFRK_POSTDEC0 || kind == SFRK_POSTDEC1 || kind == SFRK_POSTDEC2);

    if(is_preinc) {
        ptr = (uint16_t)((ptr + 1) & 0xFFF);
        fsr_set(c, n, ptr);
        eff = ptr;
    } else if(is_plusw) {
        eff = (uint16_t)((ptr + c->w) & 0xFFF);
    } else {
        eff = ptr;
    }

    int res;
    if(!write) {
        res = read_data(c, eff);
    } else {
        write_data(c, eff, wval);
        res = wval;
    }

    if(is_postinc) {
        fsr_set(c, n, (uint16_t)((ptr + 1) & 0xFFF));
    } else if(is_postdec) {
        fsr_set(c, n, (uint16_t)((ptr - 1) & 0xFFF));
    }
    return res;
}

/* ------------------------------------------------ data read/write */
static uint8_t read_data(Pic18Cpu* c, uint16_t addr) {
    addr &= 0xFFF;
    uint8_t k = kind_of(addr);
    switch(k) {
    case SFRK_NONE:
        return c->ram[addr];
    case SFRK_INDF0: case SFRK_POSTINC0: case SFRK_POSTDEC0:
    case SFRK_PREINC0: case SFRK_PLUSW0:
        return (uint8_t)fsr_access(c, 0, k, 0, 0);
    case SFRK_INDF1: case SFRK_POSTINC1: case SFRK_POSTDEC1:
    case SFRK_PREINC1: case SFRK_PLUSW1:
        return (uint8_t)fsr_access(c, 1, k, 0, 0);
    case SFRK_INDF2: case SFRK_POSTINC2: case SFRK_POSTDEC2:
    case SFRK_PREINC2: case SFRK_PLUSW2:
        return (uint8_t)fsr_access(c, 2, k, 0, 0);
    case SFRK_WREG:
        return c->w;
    case SFRK_PCL:
        return (uint8_t)(c->pc & 0xFF);
    case SFRK_TOSL:
        return (uint8_t)(cpu_tos(c) & 0xFF);
    case SFRK_TOSH:
        return (uint8_t)((cpu_tos(c) >> 8) & 0xFF);
    case SFRK_TOSU:
        return (uint8_t)((cpu_tos(c) >> 16) & 0xFF);
    case SFRK_STKPTR:
        return (uint8_t)(c->stkptr & 0x1F);
    case SFRK_PORT:
        if(c->on_port_read) {
            int v = c->on_port_read(c, addr, c->port_read_ctx);
            if(v >= 0) return (uint8_t)(v & 0xFF);
        }
        return (uint8_t)(c->default_port & 0xFF);
    case SFRK_LAT:
        /* LAT reads from normal ram (it has no special read branch) */
        return c->ram[addr];
    default:
        return c->ram[addr];
    }
}

static void write_data(Pic18Cpu* c, uint16_t addr, uint8_t val) {
    addr &= 0xFFF;
    /* Timer registers: keep the prescaler coherent with software reloads, and
     * model the TMR0 16-bit write buffer (a write to TMR0L transfers the byte
     * previously staged in TMR0H together with the new low byte, and resets the
     * prescaler), matching the PIC18 datasheet. */
    switch(addr) {
    case A_T0CON:
        c->ram[addr] = val;
        c->t0_presc = 0;
        return;
    case A_TMR0H:
        c->t0_wrbuf = val;
        c->ram[addr] = val;
        return;
    case A_TMR0L:
        if(!(c->ram[A_T0CON] & 0x40)) c->ram[A_TMR0H] = c->t0_wrbuf; /* 16-bit */
        c->ram[A_TMR0L] = val;
        c->t0_presc = 0;
        return;
    case A_T1CON: c->ram[addr] = val; c->t1_presc = 0; return;
    case A_T2CON: c->ram[addr] = val; c->t2_presc = 0; c->t2_postsc = 0; return;
    case A_T3CON: c->ram[addr] = val; c->t3_presc = 0; return;
    default: break;
    }
    uint8_t k = kind_of(addr);
    switch(k) {
    case SFRK_NONE:
        c->ram[addr] = val;
        return;
    case SFRK_INDF0: case SFRK_POSTINC0: case SFRK_POSTDEC0:
    case SFRK_PREINC0: case SFRK_PLUSW0:
        fsr_access(c, 0, k, 1, val);
        return;
    case SFRK_INDF1: case SFRK_POSTINC1: case SFRK_POSTDEC1:
    case SFRK_PREINC1: case SFRK_PLUSW1:
        fsr_access(c, 1, k, 1, val);
        return;
    case SFRK_INDF2: case SFRK_POSTINC2: case SFRK_POSTDEC2:
    case SFRK_PREINC2: case SFRK_PLUSW2:
        fsr_access(c, 2, k, 1, val);
        return;
    case SFRK_WREG:
        c->w = val;
        return;
    case SFRK_PCL: {
        uint8_t pch = c->ram[A_PCLATH];
        uint8_t pcu = c->ram[A_PCLATU];
        c->pc = (uint32_t)(((pcu & 0x1F) << 16) | (pch << 8) | val);
        c->ram[addr] = val;
        return;
    }
    case SFRK_STKPTR:
        c->stkptr = (uint8_t)(val & 0x1F);
        c->ram[addr] = val;
        return;
    case SFRK_LAT:
        c->ram[addr] = val;
        if(c->on_lat_write) c->on_lat_write(c, addr, val, c->lat_write_ctx);
        return;
    case SFRK_PORT:
        c->ram[addr] = val;
        if(c->on_port_write) c->on_port_write(c, addr, val, c->port_write_ctx);
        return;
    case SFRK_TOSL: case SFRK_TOSH: case SFRK_TOSU:
        /* They have no special write branch in the python: normal RAM. */
        c->ram[addr] = val;
        return;
    default:
        c->ram[addr] = val;
        return;
    }
}

/* ----------------------------------------------------------- TBLRD/TBLWT */
static uint32_t tblptr_get(Pic18Cpu* c) {
    return (uint32_t)(((c->ram[A_TBLPTRU] & 0x3F) << 16) |
                      (c->ram[A_TBLPTRH] << 8) |
                      c->ram[A_TBLPTRL]);
}

static void tblptr_set(Pic18Cpu* c, uint32_t v) {
    v &= 0x3FFFFF;
    c->ram[A_TBLPTRU] = (v >> 16) & 0x3F;
    c->ram[A_TBLPTRH] = (v >> 8) & 0xFF;
    c->ram[A_TBLPTRL] = v & 0xFF;
}

static uint8_t config_read(Pic18Cpu* c, uint32_t a) {
    for(uint16_t i = 0; i < c->config_count; i++) {
        if(c->config_addr[i] == a) return c->config_val[i];
    }
    return 0xFF;
}

static uint8_t prog_read(Pic18Cpu* c, uint32_t a) {
    if(a >= 0x300000) return config_read(c, a); /* config stays in its table */
    if(c->on_prog) return c->on_prog(c, a, c->prog_ctx); /* demand-paged code */
    if(a < c->prog_size) return c->prog[a];
    return 0xFF;
}

static void tblrd(Pic18Cpu* c, int mode) {
    uint32_t ptr = tblptr_get(c);
    if(mode == 3) {
        ptr = (ptr + 1) & 0x3FFFFF;
        tblptr_set(c, ptr);
    }
    c->ram[A_TABLAT] = prog_read(c, ptr);
    if(mode == 1)
        tblptr_set(c, (ptr + 1) & 0x3FFFFF);
    else if(mode == 2)
        tblptr_set(c, (ptr - 1) & 0x3FFFFF);
    c->cycles++;
}

static void tblwt(Pic18Cpu* c, int mode) {
    uint32_t ptr = tblptr_get(c);
    if(mode == 3) {
        ptr = (ptr + 1) & 0x3FFFFF;
        tblptr_set(c, ptr);
    }
    uint8_t val = c->ram[A_TABLAT];
    if(c->on_prog_write)
        c->on_prog_write(c, ptr, val, c->progw_ctx); /* paged image: read-only, drops */
    else if(ptr < c->prog_size)
        c->prog[ptr] = val;
    if(mode == 1)
        tblptr_set(c, (ptr + 1) & 0x3FFFFF);
    else if(mode == 2)
        tblptr_set(c, (ptr - 1) & 0x3FFFFF);
    c->cycles++;
}

/* ----------------------------------------------------------- DAW */
static void daw(Pic18Cpu* c) {
    uint32_t w = c->w;
    int cc = st_get(c, PIC18_ST_C);
    int dc = st_get(c, PIC18_ST_DC);
    uint32_t lo = w & 0xF;
    if(lo > 9 || dc) w += 0x06;
    if(((w >> 4) & 0xF) > 9 || cc || (w > 0xFF)) {
        w += 0x60;
        st_set(c, PIC18_ST_C, 1);
    }
    c->w = (uint8_t)(w & 0xFF);
}

/* ----------------------------------------------------------- skip helper */
static int is_long_instr(uint16_t nxt) {
    return ((nxt & 0xFF00) == 0xEF00) ||
           ((nxt & 0xFE00) == 0xEC00) ||
           ((nxt & 0xFFC0) == 0xEE00) ||
           ((nxt & 0xF000) == 0xC000);
}

static void cpu_skip(Pic18Cpu* c) {
    uint16_t nxt = pword(c, c->pc);
    int lng = is_long_instr(nxt);
    c->pc = (c->pc + (lng ? 4 : 2)) & 0x1FFFFF;
    c->cycles++;
}

/* ----------------------------------------------------- fd result write */
static void write_fd_result(Pic18Cpu* c, uint16_t addr, uint8_t res, int d_is_f) {
    if(d_is_f)
        write_data(c, addr, res);
    else
        c->w = res;
}

/* --------------------------------------------- f,d,a ops */
static void exec_fd(Pic18Cpu* c, int code, uint8_t f, int d_is_f, int acc) {
    uint16_t addr = resolve_f(c, f, acc);
    uint8_t val = read_data(c, addr);
    uint8_t w = c->w;
    uint8_t res;

    switch(code) {
    case FD_ADDWF:  res = set_flags_add(c, w, val, 0); break;
    case FD_ADDWFC: res = set_flags_add(c, w, val, st_get(c, PIC18_ST_C)); break;
    case FD_ANDWF:  res = set_flags_logic(c, (uint32_t)w & val); break;
    case FD_IORWF:  res = set_flags_logic(c, (uint32_t)w | val); break;
    case FD_XORWF:  res = set_flags_logic(c, (uint32_t)w ^ val); break;
    case FD_MOVF:   res = set_flags_logic(c, val); break;
    case FD_COMF:   res = set_flags_logic(c, (uint32_t)((~val) & 0xFF)); break;
    case FD_INCF:   res = set_flags_add(c, val, 1, 0); break;
    case FD_DECF:   res = set_flags_sub(c, val, 1, 1); break;
    case FD_SUBWF:  res = set_flags_sub(c, val, w, 1); break;
    case FD_SUBWFB: res = set_flags_sub(c, val, w, st_get(c, PIC18_ST_C)); break;
    case FD_SUBFWB: res = set_flags_sub(c, w, val, st_get(c, PIC18_ST_C)); break;
    case FD_RLCF: {
        int cc = st_get(c, PIC18_ST_C);
        int newc = (val >> 7) & 1;
        res = (uint8_t)(((val << 1) | cc) & 0xFF);
        st_set(c, PIC18_ST_C, newc);
        st_set(c, PIC18_ST_Z, res == 0);
        st_set(c, PIC18_ST_N, (res >> 7) & 1);
        break;
    }
    case FD_RRCF: {
        int cc = st_get(c, PIC18_ST_C);
        int newc = val & 1;
        res = (uint8_t)(((val >> 1) | (cc << 7)) & 0xFF);
        st_set(c, PIC18_ST_C, newc);
        st_set(c, PIC18_ST_Z, res == 0);
        st_set(c, PIC18_ST_N, (res >> 7) & 1);
        break;
    }
    case FD_RLNCF:
        res = (uint8_t)(((val << 1) | (val >> 7)) & 0xFF);
        st_set(c, PIC18_ST_Z, res == 0);
        st_set(c, PIC18_ST_N, (res >> 7) & 1);
        break;
    case FD_RRNCF:
        res = (uint8_t)(((val >> 1) | ((val & 1) << 7)) & 0xFF);
        st_set(c, PIC18_ST_Z, res == 0);
        st_set(c, PIC18_ST_N, (res >> 7) & 1);
        break;
    case FD_SWAPF:
        res = (uint8_t)(((val << 4) | (val >> 4)) & 0xFF);
        break;
    case FD_INCFSZ:
        res = (uint8_t)((val + 1) & 0xFF);
        write_fd_result(c, addr, res, d_is_f);
        if(res == 0) cpu_skip(c);
        return;
    case FD_DECFSZ:
        res = (uint8_t)((val - 1) & 0xFF);
        write_fd_result(c, addr, res, d_is_f);
        if(res == 0) cpu_skip(c);
        return;
    case FD_INFSNZ:
        res = (uint8_t)((val + 1) & 0xFF);
        write_fd_result(c, addr, res, d_is_f);
        if(res != 0) cpu_skip(c);
        return;
    case FD_DCFSNZ:
        res = (uint8_t)((val - 1) & 0xFF);
        write_fd_result(c, addr, res, d_is_f);
        if(res != 0) cpu_skip(c);
        return;
    default:
        return; /* unreachable */
    }
    write_fd_result(c, addr, res, d_is_f);
}

/* --------------------------------------------- f,a ops (no d) */
static void exec_fa(Pic18Cpu* c, int code, uint8_t f, int acc) {
    uint16_t addr = resolve_f(c, f, acc);
    switch(code) {
    case FA_MOVWF:
        write_data(c, addr, c->w);
        break;
    case FA_CLRF:
        write_data(c, addr, 0);
        st_set(c, PIC18_ST_Z, 1);
        break;
    case FA_SETF:
        write_data(c, addr, 0xFF);
        break;
    case FA_NEGF: {
        uint8_t val = read_data(c, addr);
        uint8_t res = set_flags_sub(c, 0, val, 1);
        write_data(c, addr, res);
        break;
    }
    case FA_MULWF: {
        uint16_t res = (uint16_t)(c->w * read_data(c, addr));
        c->ram[A_PRODL] = res & 0xFF;
        c->ram[A_PRODH] = (res >> 8) & 0xFF;
        break;
    }
    case FA_CPFSEQ:
        if(read_data(c, addr) == c->w) cpu_skip(c);
        break;
    case FA_CPFSGT:
        if(read_data(c, addr) > c->w) cpu_skip(c);
        break;
    case FA_CPFSLT:
        if(read_data(c, addr) < c->w) cpu_skip(c);
        break;
    case FA_TSTFSZ:
        if(read_data(c, addr) == 0) cpu_skip(c);
        break;
    default:
        break;
    }
}

/* --------------------------------------------- bit ops */
static void exec_bit(Pic18Cpu* c, int code, uint8_t f, int bit, int acc) {
    uint16_t addr = resolve_f(c, f, acc);
    uint8_t val = read_data(c, addr);
    switch(code) {
    case BIT_BSF:
        write_data(c, addr, (uint8_t)(val | (1u << bit)));
        break;
    case BIT_BCF:
        write_data(c, addr, (uint8_t)(val & ~(1u << bit)));
        break;
    case BIT_BTG:
        write_data(c, addr, (uint8_t)(val ^ (1u << bit)));
        break;
    case BIT_BTFSS:
        if((val >> bit) & 1) cpu_skip(c);
        break;
    case BIT_BTFSC:
        if(!((val >> bit) & 1)) cpu_skip(c);
        break;
    default:
        break;
    }
}

/* ============================================================ decoder */
static void exec_op(Pic18Cpu* c, uint32_t pc, uint16_t op) {
    uint8_t hi = (uint8_t)(op >> 8);
    uint8_t lo = (uint8_t)(op & 0xFF);
    int acc = (op & 0x100) ? 0 : 1;      /* acc=1 => ACCESS bank */
    int d_is_f = (op & 0x200) != 0;      /* d: 1=>F, 0=>W */

    /* -------- literals (group 0x0_) -------- */
    if(op >= 0x0E00 && op <= 0x0EFF) { c->w = lo; return; }               /* MOVLW */
    if(op >= 0x0C00 && op <= 0x0CFF) { c->w = lo; c->pc = pic18_pop(c); return; } /* RETLW */
    if(op >= 0x0F00 && op <= 0x0FFF) { c->w = set_flags_add(c, c->w, lo, 0); return; } /* ADDLW */
    if(op >= 0x0800 && op <= 0x08FF) { c->w = set_flags_sub(c, lo, c->w, 1); return; } /* SUBLW k-W */
    if(op >= 0x0900 && op <= 0x09FF) { c->w = set_flags_logic(c, (uint32_t)c->w | lo); return; } /* IORLW */
    if(op >= 0x0A00 && op <= 0x0AFF) { c->w = set_flags_logic(c, (uint32_t)c->w ^ lo); return; } /* XORLW */
    if(op >= 0x0B00 && op <= 0x0BFF) { c->w = set_flags_logic(c, (uint32_t)c->w & lo); return; } /* ANDLW */
    if(op >= 0x0D00 && op <= 0x0DFF) {                                     /* MULLW */
        uint16_t res = (uint16_t)(c->w * lo);
        c->ram[A_PRODL] = res & 0xFF;
        c->ram[A_PRODH] = (res >> 8) & 0xFF;
        return;
    }
    if(op >= 0x0100 && op <= 0x010F) { c->ram[A_BSR] = (uint8_t)(lo & 0xF); return; } /* MOVLB */

    /* -------- 4 bytes: GOTO CALL LFSR MOVFF -------- */
    if((op & 0xFF00) == 0xEF00) {                                          /* GOTO */
        uint16_t w2 = pword(c, pc + 2);
        uint32_t k = (uint32_t)(((w2 & 0x0FFF) << 8) | (op & 0xFF));
        c->pc = (k * 2) & 0x1FFFFF;
        c->cycles++;
        return;
    }
    if((op & 0xFE00) == 0xEC00) {                                          /* CALL */
        int s = (op >> 8) & 1;
        uint16_t w2 = pword(c, pc + 2);
        uint32_t k = (uint32_t)(((w2 & 0x0FFF) << 8) | (op & 0xFF));
        uint32_t ret = (pc + 4) & 0x1FFFFF;
        pic18_push(c, ret);
        if(s) {
            c->ws = c->w;
            c->statuss = c->ram[A_STATUS];
            c->bsrs = bsr4(c);
        }
        c->pc = (k * 2) & 0x1FFFFF;
        c->cycles++;
        return;
    }
    if((op & 0xFFC0) == 0xEE00) {                                          /* LFSR f,k */
        int f = (op >> 4) & 3;
        uint16_t w2 = pword(c, pc + 2);
        uint16_t k = (uint16_t)(((op & 0x0F) << 8) | (w2 & 0xFF));
        fsr_set(c, f, k);
        c->pc = (pc + 4) & 0x1FFFFF;
        c->cycles++;
        return;
    }
    if((op & 0xF000) == 0xC000) {                                          /* MOVFF */
        uint16_t src = op & 0x0FFF;
        uint16_t w2 = pword(c, pc + 2);
        uint16_t dst = w2 & 0x0FFF;
        uint8_t val = read_data(c, src);
        write_data(c, dst, val);
        c->pc = (pc + 4) & 0x1FFFFF;
        c->cycles++;
        return;
    }

    /* -------- relative branches -------- */
    if((op & 0xF800) == 0xD000) {                                          /* BRA */
        int n = op & 0x7FF;
        if(n & 0x400) n -= 0x800;
        c->pc = (uint32_t)((pc + 2 + n * 2)) & 0x1FFFFF;
        c->cycles++;
        return;
    }
    if((op & 0xF800) == 0xD800) {                                          /* RCALL */
        int n = op & 0x7FF;
        if(n & 0x400) n -= 0x800;
        pic18_push(c, (pc + 2) & 0x1FFFFF);
        c->pc = (uint32_t)((pc + 2 + n * 2)) & 0x1FFFFF;
        c->cycles++;
        return;
    }

    /* conditional branches: hi 0xE0..0xE7 */
    if(hi >= 0xE0 && hi <= 0xE7) {
        int n = lo;
        if(n & 0x80) n -= 0x100;
        int take = 0;
        switch(hi) {
        case 0xE0: take = st_get(c, PIC18_ST_Z); break;        /* BZ  */
        case 0xE1: take = !st_get(c, PIC18_ST_Z); break;       /* BNZ */
        case 0xE2: take = st_get(c, PIC18_ST_C); break;        /* BC  */
        case 0xE3: take = !st_get(c, PIC18_ST_C); break;       /* BNC */
        case 0xE4: take = st_get(c, PIC18_ST_OV); break;       /* BOV */
        case 0xE5: take = !st_get(c, PIC18_ST_OV); break;      /* BNOV*/
        case 0xE6: take = st_get(c, PIC18_ST_N); break;        /* BN  */
        case 0xE7: take = !st_get(c, PIC18_ST_N); break;       /* BNN */
        default: break;
        }
        if(take) {
            c->pc = (uint32_t)((pc + 2 + n * 2)) & 0x1FFFFF;
            c->cycles++;
        }
        return;
    }

    /* -------- inherent / control -------- */
    if(op == 0x0000) return;                                               /* NOP */
    if((op & 0xF000) == 0xF000) return;              /* NOP (2nd word of long instr) */
    if(op == 0x0003) { c->sleeping = 1; return; }                          /* SLEEP */
    if(op == 0x0004) return;                                               /* CLRWDT */
    if(op == 0x0005) { pic18_push(c, (pc + 2) & 0x1FFFFF); return; }       /* PUSH */
    if(op == 0x0006) { pic18_pop(c); return; }                             /* POP */
    if(op == 0x0007) { daw(c); return; }                                   /* DAW */
    if(op == 0x00FF) { pic18_reset(c); return; }                           /* RESET */
    if(op == 0x0010 || op == 0x0011) {                                     /* RETFIE */
        c->pc = pic18_pop(c);
        if(op & 1) {
            c->w = c->ws;
            c->ram[A_STATUS] = c->statuss;
            c->ram[A_BSR] = (uint8_t)(c->bsrs & 0xF);
        }
        c->ram[A_INTCON] |= 0x80;
        c->cycles++;
        return;
    }
    if(op == 0x0012 || op == 0x0013) {                                     /* RETURN */
        c->pc = pic18_pop(c);
        if(op & 1) {
            c->w = c->ws;
            c->ram[A_STATUS] = c->statuss;
            c->ram[A_BSR] = (uint8_t)(c->bsrs & 0xF);
        }
        c->cycles++;
        return;
    }
    if((op & 0xFFFC) == 0x0008) { tblrd(c, op & 3); return; }              /* TBLRD */
    if((op & 0xFFFC) == 0x000C) { tblwt(c, op & 3); return; }              /* TBLWT */

    /* -------- byte-oriented f,d,a (bits 15..10) -------- */
    {
        uint16_t base = op & 0xFC00;
        int code = fd_op(base);
        if(code != FD_NONE) {
            exec_fd(c, code, lo, d_is_f, acc);
            return;
        }
    }

    /* -------- byte-oriented f,a (bits 15..9) -------- */
    {
        uint16_t base2 = op & 0xFE00;
        int code = fa_op(base2);
        if(code != FA_NONE) {
            exec_fa(c, code, lo, acc);
            return;
        }
    }

    /* -------- bit-oriented: bits 15..12 -------- */
    {
        uint8_t hi4 = (uint8_t)(op >> 12);
        int code = bit_op(hi4);
        if(code != BIT_NONE) {
            int bit = (op >> 9) & 7;
            int acc3 = (op & 0x100) ? 0 : 1;
            exec_bit(c, code, lo, bit, acc3);
            return;
        }
    }

    /* illegal instruction: in this model we mark halted (there are no
     * exceptions). The python raises IllegalInstruction; for trace fidelity,
     * the harness verifies this does NOT happen. We set halted=1 to stop safely
     * on hardware. */
    c->halted = 1;
}

/* ============================================================ public API */
void pic18_init(Pic18Cpu* c) {
    memset(c, 0, sizeof(*c));
    sfr_kind_init();
    c->prog = NULL;
    c->prog_size = 0;
    c->default_port = 0x00;
}

void pic18_set_prog(Pic18Cpu* c, uint8_t* prog, uint32_t size) {
    c->prog = prog;
    c->prog_size = size;
}

void pic18_reset(Pic18Cpu* c) {
    c->pc = 0;
    c->w = 0;
    c->stkptr = 0;
    for(int i = 0; i < PIC18_STACK_LEVELS; i++) c->stack[i] = 0;
    c->ws = c->statuss = c->bsrs = 0;
    c->cycles = 0;
    c->steps = 0;
    c->halted = 0;
    c->sleeping = 0;
    c->t0_presc = 0;
    c->t0_wrbuf = 0;
    c->t1_presc = 0;
    c->t2_presc = 0;
    c->t2_postsc = 0;
    c->t3_presc = 0;
    memset(c->ram, 0, sizeof(c->ram));
    c->ram[A_BSR] = 0;
    c->ram[A_STKPTR] = 0;
    /* TRIS to 1 (inputs) at reset: TRISA..TRISE */
    c->ram[0xF94] = 0xFF; /* TRISA */
    c->ram[0xF95] = 0xFF; /* TRISB */
    c->ram[0xF96] = 0xFF; /* TRISC */
    c->ram[0xF97] = 0xFF; /* TRISD */
    c->ram[0xF98] = 0xFF; /* TRISE */
}

/* callback for ihex -> fills prog/config */
static void ihex_store(uint32_t addr, uint8_t value, void* ctx) {
    Pic18Cpu* c = (Pic18Cpu*)ctx;
    if(addr < c->prog_size) {
        c->prog[addr] = value;
    } else if(addr >= 0x300000) {
        /* check if it already exists */
        for(uint16_t i = 0; i < c->config_count; i++) {
            if(c->config_addr[i] == addr) {
                c->config_val[i] = value;
                return;
            }
        }
        if(c->config_count < (uint16_t)(sizeof(c->config_addr) / sizeof(c->config_addr[0]))) {
            c->config_addr[c->config_count] = addr;
            c->config_val[c->config_count] = value;
            c->config_count++;
        }
    }
}

int pic18_load_hex_mem(Pic18Cpu* c, const uint8_t* hexdata, size_t size) {
    if(!c->prog) return -1;
    return ihex_parse_mem(hexdata, size, ihex_store, c);
}

int pic18_load_prog(Pic18Cpu* c, const uint8_t* bin, size_t size, uint32_t base) {
    if(!c->prog) return -1;
    for(size_t i = 0; i < size; i++) {
        uint32_t a = base + (uint32_t)i;
        if(a < c->prog_size) c->prog[a] = bin[i];
    }
    return 0;
}

#ifdef PIC18_HOST
int pic18_load_hex(Pic18Cpu* c, const char* path) {
    if(!c->prog) return -1;
    return ihex_parse_file(path, ihex_store, c);
}
#endif

/* =============================================================== timers
 * Advances the on-chip timers by `dcyc` instruction cycles (Fosc/4 ticks) and
 * raises the overflow flags the firmware's ISR polls. Only the internal
 * instruction-clock source is modeled (T*CS=0); external-clock/counter modes
 * are not driven here. Each timer's registers live in ram[] at their SFR
 * addresses; the prescaler/postscaler residues live in the Pic18Cpu. mario2
 * (Pandora DXL-5000) keeps time on a 16-bit TMR0; TMR1/2/3 are modeled too so
 * the subsystem is complete, but the Pandora images leave them off.
 */
static void timers_advance(Pic18Cpu* c, uint32_t dcyc) {
    /* ---- TMR0 ---- */
    uint8_t t0con = c->ram[A_T0CON];
    if((t0con & 0x80) && !(t0con & 0x20)) { /* TMR0ON=1, T0CS=0 (internal) */
        uint32_t div = (t0con & 0x08) ? 1u : (1u << ((t0con & 0x07) + 1)); /* PSA / T0PS */
        c->t0_presc += dcyc;
        uint32_t inc = c->t0_presc / div;
        c->t0_presc -= inc * div;
        if(inc) {
            if(t0con & 0x40) { /* T08BIT: 8-bit */
                uint32_t v = (uint32_t)c->ram[A_TMR0L] + inc;
                if(v > 0xFF) c->ram[A_INTCON] |= INTCON_TMR0IF;
                c->ram[A_TMR0L] = (uint8_t)(v & 0xFF);
            } else { /* 16-bit */
                uint32_t v = (((uint32_t)c->ram[A_TMR0H] << 8) | c->ram[A_TMR0L]) + inc;
                if(v > 0xFFFF) c->ram[A_INTCON] |= INTCON_TMR0IF;
                c->ram[A_TMR0H] = (uint8_t)((v >> 8) & 0xFF);
                c->ram[A_TMR0L] = (uint8_t)(v & 0xFF);
            }
        }
    }
    /* ---- TMR1 (16-bit) ---- */
    uint8_t t1con = c->ram[A_T1CON];
    if((t1con & 0x01) && !(t1con & 0x02)) { /* TMR1ON=1, TMR1CS=0 (internal) */
        uint32_t div = 1u << ((t1con >> 4) & 0x03); /* T1CKPS 1:1..1:8 */
        c->t1_presc += dcyc;
        uint32_t inc = c->t1_presc / div;
        c->t1_presc -= inc * div;
        if(inc) {
            uint32_t v = (((uint32_t)c->ram[A_TMR1H] << 8) | c->ram[A_TMR1L]) + inc;
            if(v > 0xFFFF) c->ram[A_PIR1] |= PIR1_TMR1IF;
            c->ram[A_TMR1H] = (uint8_t)((v >> 8) & 0xFF);
            c->ram[A_TMR1L] = (uint8_t)(v & 0xFF);
        }
    }
    /* ---- TMR2 (8-bit, PR2 match + postscaler) ---- */
    uint8_t t2con = c->ram[A_T2CON];
    if(t2con & 0x04) { /* TMR2ON */
        static const uint8_t t2ps[4] = {1, 4, 16, 16}; /* T2CKPS: 1:1,1:4,1:16,1:16 */
        uint32_t div = t2ps[t2con & 0x03];
        uint8_t post = (uint8_t)(((t2con >> 3) & 0x0F) + 1); /* TOUTPS 1:1..1:16 */
        c->t2_presc += dcyc;
        while(c->t2_presc >= div) {
            c->t2_presc -= div;
            uint8_t nv = (uint8_t)(c->ram[A_TMR2] + 1);
            if(nv > c->ram[A_PR2]) {
                nv = 0;
                if(++c->t2_postsc >= post) {
                    c->t2_postsc = 0;
                    c->ram[A_PIR1] |= PIR1_TMR2IF;
                }
            }
            c->ram[A_TMR2] = nv;
        }
    }
    /* ---- TMR3 (16-bit) ---- */
    uint8_t t3con = c->ram[A_T3CON];
    if((t3con & 0x01) && !(t3con & 0x02)) { /* TMR3ON=1, TMR3CS=0 */
        uint32_t div = 1u << ((t3con >> 4) & 0x03);
        c->t3_presc += dcyc;
        uint32_t inc = c->t3_presc / div;
        c->t3_presc -= inc * div;
        if(inc) {
            uint32_t v = (((uint32_t)c->ram[A_TMR3H] << 8) | c->ram[A_TMR3L]) + inc;
            if(v > 0xFFFF) c->ram[A_PIR2] |= PIR2_TMR3IF;
            c->ram[A_TMR3H] = (uint8_t)((v >> 8) & 0xFF);
            c->ram[A_TMR3L] = (uint8_t)(v & 0xFF);
        }
    }
}

/* ========================================================== interrupts
 * Checks, between instructions, for a pending-and-enabled interrupt and, if
 * found, vectors to it: push the return PC, snapshot WREG/STATUS/BSR into the
 * fast-return shadow (hardware does this on every interrupt), clear the global
 * enable the architecture clears on entry, and jump to the vector. RETFIE
 * (exec_op) re-sets the global enable on return. Both the compatibility model
 * (RCON.IPEN=0, single vector 0x0008) and the two-priority model (IPEN=1,
 * 0x0008 high / 0x0018 low) are handled; the Pandora images run IPEN=0.
 */
static void take_interrupt(Pic18Cpu* c, uint32_t vector, uint8_t clear_gie_bit) {
    pic18_push(c, c->pc);
    c->ws = c->w;
    c->statuss = c->ram[A_STATUS];
    c->bsrs = bsr4(c);
    c->ram[A_INTCON] &= (uint8_t)~clear_gie_bit;
    c->pc = vector;
    c->sleeping = 0;
    c->cycles++;
}

static void service_interrupt(Pic18Cpu* c) {
    if(c->halted) return;
    uint8_t intcon = c->ram[A_INTCON];
    if(!(intcon & INTCON_GIE)) return; /* GIE/GIEH gates every vector */

    /* INTCON-local sources (flag & enable) */
    int tmr0 = (intcon & INTCON_TMR0IF) && (intcon & INTCON_TMR0IE);
    int int0 = (intcon & INTCON_INT0IF) && (intcon & INTCON_INT0IE);
    int rbif = (intcon & INTCON_RBIF) && (intcon & INTCON_RBIE);
    /* peripheral sources (flag & enable), split by which register pair */
    uint8_t p1 = (uint8_t)(c->ram[A_PIR1] & c->ram[A_PIE1]);
    uint8_t p2 = (uint8_t)(c->ram[A_PIR2] & c->ram[A_PIE2]);
    uint8_t p3 = (uint8_t)(c->ram[A_PIR3] & c->ram[A_PIE3]);
    int periph = (p1 || p2 || p3);

    if(!(c->ram[A_RCON] & RCON_IPEN)) {
        /* compatibility mode: GIE gates everything, PEIE gates peripherals */
        if(tmr0 || int0 || rbif || ((intcon & INTCON_PEIE) && periph)) {
            take_interrupt(c, VEC_HIGH, INTCON_GIE);
            return;
        }
    } else {
        /* two-priority mode: GIEH=bit7, GIEL=bit6 */
        uint8_t intcon2 = c->ram[A_INTCON2];
        uint8_t ipr1 = c->ram[A_IPR1], ipr2 = c->ram[A_IPR2], ipr3 = c->ram[A_IPR3];
        int tmr0_hi = tmr0 && (intcon2 & INTCON2_TMR0IP);
        int rb_hi = rbif && (intcon2 & INTCON2_RBIP);
        int p_hi = ((p1 & ipr1) || (p2 & ipr2) || (p3 & ipr3));
        int high = tmr0_hi || int0 || rb_hi || p_hi; /* INT0 is always high priority */
        int low = (tmr0 && !(intcon2 & INTCON2_TMR0IP)) || (rbif && !(intcon2 & INTCON2_RBIP)) ||
                  ((p1 & ~ipr1) || (p2 & ~ipr2) || (p3 & ~ipr3));
        if(high) {
            take_interrupt(c, VEC_HIGH, INTCON_GIE);
            return;
        }
        if((intcon & INTCON_PEIE) && low) {
            take_interrupt(c, VEC_LOW, INTCON_PEIE);
            return;
        }
    }

    /* No on-chip interrupt pending: offer the slot to a frontend-modeled
     * source (e.g. a board whose firmware runs its timer tick off its own
     * software flags). */
    if(c->on_irq) {
        int32_t v = c->on_irq(c, c->irq_ctx);
        if(v >= 0) take_interrupt(c, (uint32_t)v, INTCON_GIE);
    }
}

int pic18_step(Pic18Cpu* c) {
    if(c->halted) return 0;
    /* interrupts are recognized between instructions */
    service_interrupt(c);
    if(c->sleeping) c->sleeping = 0;
    uint32_t pc = c->pc;
    if(c->on_code) c->on_code(c, pc, c->code_ctx);
    uint16_t op = pword(c, pc);
    uint64_t cyc0 = c->cycles;
    c->steps++;
    c->cycles++;
    c->pc = (pc + 2) & 0x1FFFFF;
    exec_op(c, pc, op);
    uint32_t dcyc = (uint32_t)(c->cycles - cyc0);
    if(dcyc == 0) dcyc = 1;
    timers_advance(c, dcyc);
    return !c->halted;
}

uint64_t pic18_run(Pic18Cpu* c, uint64_t max_steps) {
    uint64_t n = 0;
    while(n < max_steps && !c->halted) {
        pic18_step(c);
        n++;
    }
    return n;
}

uint8_t pic18_read_data(Pic18Cpu* c, uint16_t addr) {
    return read_data(c, addr);
}

void pic18_write_data(Pic18Cpu* c, uint16_t addr, uint8_t val) {
    write_data(c, addr, val);
}

uint8_t pic18_read_ram(Pic18Cpu* c, uint16_t addr) {
    return c->ram[addr & 0xFFF];
}

void pic18_write_ram(Pic18Cpu* c, uint16_t addr, uint8_t val) {
    c->ram[addr & 0xFFF] = val;
}

uint8_t pic18_get_sfr_addr(Pic18Cpu* c, uint16_t addr) {
    return read_data(c, addr);
}

void pic18_set_sfr_addr(Pic18Cpu* c, uint16_t addr, uint8_t val) {
    c->ram[addr & 0xFFF] = val;
}

uint16_t pic18_sfr_addr_by_name(const char* name) {
    for(size_t i = 0; i < SFR_TABLE_N; i++) {
        if(strcmp(SFR_TABLE[i].name, name) == 0) return SFR_TABLE[i].addr;
    }
    return 0;
}

int pic18_get_sfr(Pic18Cpu* c, const char* name, uint8_t* out) {
    uint16_t a = pic18_sfr_addr_by_name(name);
    if(a == 0) return -1;
    *out = read_data(c, a);
    return 0;
}

int pic18_set_sfr(Pic18Cpu* c, const char* name, uint8_t val) {
    uint16_t a = pic18_sfr_addr_by_name(name);
    if(a == 0) return -1;
    c->ram[a] = val;
    return 0;
}

uint8_t pic18_status(const Pic18Cpu* c) {
    return c->ram[A_STATUS];
}

uint8_t pic18_bsr(const Pic18Cpu* c) {
    return (uint8_t)(c->ram[A_BSR] & 0xF);
}

uint16_t pic18_fsr(const Pic18Cpu* c, int n) {
    uint16_t h, l;
    if(n == 0) { h = c->ram[A_FSR0H]; l = c->ram[A_FSR0L]; }
    else if(n == 1) { h = c->ram[A_FSR1H]; l = c->ram[A_FSR1L]; }
    else { h = c->ram[A_FSR2H]; l = c->ram[A_FSR2L]; }
    return (uint16_t)(((h & 0xF) << 8) | l);
}
