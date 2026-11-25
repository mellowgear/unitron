/* Runtime for C lifted from TLCS-900/H code (unitron lift).
 *
 * The CPU state is one global struct. Registers are named exactly as in the
 * disassembly (XWA, WA, A, W, QA, RA3, XIX, ...) through macros generated in
 * ngpc_regs.h. Memory and I/O go through rd8/rd16/rd32 and wr8/wr16/wr32,
 * which the host program provides.
 */
#ifndef NGPC_RT_H
#define NGPC_RT_H
#include <stdint.h>

typedef struct {
    uint32_t b[4][4];   /* banked XWA XBC XDE XHL, banks 0..3 */
    uint32_t x[4];      /* XIX XIY XIZ XSP */
    unsigned rfp;       /* current register bank */
    unsigned iff;       /* interrupt mask level */
    uint8_t fs, fz, fh, fv, fn, fc;          /* flags */
    uint8_t fs_, fz_, fh_, fv_, fn_, fc_;    /* F' */
    uint32_t cr[0x40];  /* control registers (LDC) */
} Cpu;
extern Cpu cpu;

/* Little-endian sub-register access: R8(v,i) = byte i, R16(v,i) = halfword i. */
#define R8(v, i)  (((uint8_t  *)&(v))[i])
#define R16(v, i) (((uint16_t *)&(v))[i])

#include "ngpc_regs.h"

/* ---- host-provided machine interface */
uint8_t  rd8(uint32_t a);
uint16_t rd16(uint32_t a);
uint32_t rd32(uint32_t a);
void wr8(uint32_t a, uint8_t v);
void wr16(uint32_t a, uint16_t v);
void wr32(uint32_t a, uint32_t v);
void bios_call(void);          /* SWI 1: function number in RW3 */
void swi(int n);
void halt(void);
void unknown_code(uint32_t a); /* indirect transfer to an address we never lifted */
void fallthrough_unknown(uint32_t a);
void UNIMPLEMENTED(const char *what);
uint32_t ldc_read(const char *cr);
void ldc_write(const char *cr, uint32_t v);

void call_ind(uint32_t a);
void jump_ind(uint32_t a);

/* ---- stack */
static inline void push8(uint8_t v)   { XSP -= 1; wr8(XSP, v); }
static inline void push16(uint16_t v) { XSP -= 2; wr16(XSP, v); }
static inline void push32(uint32_t v) { XSP -= 4; wr32(XSP, v); }
static inline uint8_t  pop8(void)  { uint8_t  v = rd8(XSP);  XSP += 1; return v; }
static inline uint16_t pop16(void) { uint16_t v = rd16(XSP); XSP += 2; return v; }
static inline uint32_t pop32(void) { uint32_t v = rd32(XSP); XSP += 4; return v; }

/* ---- flags */
static inline uint8_t get_f(void) {
    return (uint8_t)(cpu.fs << 7 | cpu.fz << 6 | cpu.fh << 4 | cpu.fv << 2 | cpu.fn << 1 | cpu.fc);
}
static inline void set_f(uint8_t f) {
    cpu.fs = f >> 7 & 1; cpu.fz = f >> 6 & 1; cpu.fh = f >> 4 & 1;
    cpu.fv = f >> 2 & 1; cpu.fn = f >> 1 & 1; cpu.fc = f & 1;
}
static inline uint16_t get_sr(void) {
    return (uint16_t)(0x8800 | (cpu.iff & 7) << 12 | (cpu.rfp & 3) << 8 | get_f());
}
static inline void set_sr(uint16_t v) { cpu.iff = v >> 12 & 7; cpu.rfp = v >> 8 & 3; set_f((uint8_t)v); }
static inline void reti(void) { uint16_t sr = pop16(); XSP += 4; set_sr(sr); }
static inline void ex_f(void) {
    uint8_t t;
#define SWP(a, b) t = cpu.a; cpu.a = cpu.b; cpu.b = t
    SWP(fs, fs_); SWP(fz, fz_); SWP(fh, fh_); SWP(fv, fv_); SWP(fn, fn_); SWP(fc, fc_);
#undef SWP
}

static inline uint32_t mask_of(int bits) { return bits == 32 ? 0xFFFFFFFFu : (1u << bits) - 1; }
static inline int parity(uint32_t v) { v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return !(v & 1); }
static inline void szflags(uint32_t r, int bits) {
    r &= mask_of(bits); cpu.fz = r == 0; cpu.fs = (r >> (bits - 1)) & 1;
}

static inline uint32_t alu_addc(uint32_t a, uint32_t b, int c, int bits) {
    uint32_t m = mask_of(bits); a &= m; b &= m;
    uint64_t r = (uint64_t)a + b + c;
    uint32_t res = (uint32_t)r & m;
    szflags(res, bits);
    cpu.fc = (r >> bits) & 1;
    cpu.fv = ((~(a ^ b) & (a ^ res)) >> (bits - 1)) & 1;
    cpu.fh = ((a ^ b ^ res) >> 4) & 1;
    cpu.fn = 0;
    return res;
}
static inline uint32_t alu_subc(uint32_t a, uint32_t b, int c, int bits) {
    uint32_t m = mask_of(bits); a &= m; b &= m;
    uint64_t r = (uint64_t)a - b - c;
    uint32_t res = (uint32_t)r & m;
    szflags(res, bits);
    cpu.fc = (uint64_t)a < (uint64_t)b + c;
    cpu.fv = (((a ^ b) & (a ^ res)) >> (bits - 1)) & 1;
    cpu.fh = ((a ^ b ^ res) >> 4) & 1;
    cpu.fn = 1;
    return res;
}
#define alu_add(a, b, bits) alu_addc((a), (b), 0, (bits))
#define alu_adc(a, b, bits) alu_addc((a), (b), cpu.fc, (bits))
#define alu_sub(a, b, bits) alu_subc((a), (b), 0, (bits))
#define alu_sbc(a, b, bits) alu_subc((a), (b), cpu.fc, (bits))
#define alu_cp(a, b, bits)  ((void)alu_subc((a), (b), 0, (bits)))
static inline uint32_t alu_logic(uint32_t r, int bits, int h) {
    r &= mask_of(bits); szflags(r, bits);
    cpu.fh = h; cpu.fn = 0; cpu.fc = 0; cpu.fv = parity(r);
    return r;
}
#define alu_and(a, b, bits) alu_logic((a) & (b), (bits), 1)
#define alu_or(a, b, bits)  alu_logic((a) | (b), (bits), 0)
#define alu_xor(a, b, bits) alu_logic((a) ^ (b), (bits), 0)

/* INC/DEC with flags (byte register or memory operand): C unchanged. */
static inline uint32_t alu_inc(uint32_t a, uint32_t n, int bits) {
    uint8_t c = cpu.fc; uint32_t r = alu_addc(a, n, 0, bits); cpu.fc = c; return r;
}
static inline uint32_t alu_dec(uint32_t a, uint32_t n, int bits) {
    uint8_t c = cpu.fc; uint32_t r = alu_subc(a, n, 0, bits); cpu.fc = c; return r;
}

/* Shifts and rotates: count 1..16, C = last bit out, V = parity. */
static inline uint32_t shift_op(int kind, uint32_t v, int n, int bits) {
    uint32_t m = mask_of(bits), top = 1u << (bits - 1);
    v &= m;
    for (int i = 0; i < n; i++) {
        uint32_t out;
        switch (kind) {
        case 0: out = (v & top) != 0; v = ((v << 1) | out) & m; break;                 /* RLC */
        case 1: out = v & 1; v = (v >> 1) | (out ? top : 0); break;                     /* RRC */
        case 2: out = (v & top) != 0; v = ((v << 1) | cpu.fc) & m; break;               /* RL  */
        case 3: out = v & 1; v = (v >> 1) | (cpu.fc ? top : 0); break;                  /* RR  */
        case 4: case 6: out = (v & top) != 0; v = (v << 1) & m; break;                  /* SLA/SLL */
        case 5: out = v & 1; v = (v >> 1) | (v & top); break;                           /* SRA */
        default: out = v & 1; v >>= 1; break;                                           /* SRL */
        }
        cpu.fc = (uint8_t)out;
    }
    szflags(v, bits); cpu.fh = 0; cpu.fn = 0; cpu.fv = parity(v);
    return v;
}
#define shift_rlc(v, n, b) shift_op(0, (v), (n), (b))
#define shift_rrc(v, n, b) shift_op(1, (v), (n), (b))
#define shift_rl(v, n, b)  shift_op(2, (v), (n), (b))
#define shift_rr(v, n, b)  shift_op(3, (v), (n), (b))
#define shift_sla(v, n, b) shift_op(4, (v), (n), (b))
#define shift_sra(v, n, b) shift_op(5, (v), (n), (b))
#define shift_sll(v, n, b) shift_op(6, (v), (n), (b))
#define shift_srl(v, n, b) shift_op(7, (v), (n), (b))

static inline uint32_t tset(uint32_t v, int b) {
    cpu.fz = !((v >> b) & 1); cpu.fh = 1; cpu.fn = 0; return v | (1u << b);
}

/* MUL/DIV. RR is the double-size register; its low half is the multiplicand,
 * and for DIV the quotient goes to the low half, the remainder to the high. */
static inline uint16_t mulu8(uint16_t rr, uint8_t s)  { return (uint16_t)((uint8_t)rr * s); }
static inline uint16_t muls8(uint16_t rr, uint8_t s)  { return (uint16_t)((int8_t)rr * (int8_t)s); }
static inline uint32_t mulu16(uint32_t rr, uint16_t s) { return (uint32_t)(uint16_t)rr * s; }
static inline uint32_t muls16(uint32_t rr, uint16_t s) { return (uint32_t)((int32_t)(int16_t)rr * (int16_t)s); }
static inline uint16_t divu8(uint16_t rr, uint8_t s) {
    if (s == 0) { cpu.fv = 1; return rr; }
    uint32_t q = rr / s, r = rr % s; cpu.fv = q > 0xFF;
    return (uint16_t)((r << 8) | (q & 0xFF));
}
static inline uint16_t divs8(uint16_t rr, uint8_t s) {
    if (s == 0) { cpu.fv = 1; return rr; }
    int32_t q = (int16_t)rr / (int8_t)s, r = (int16_t)rr % (int8_t)s; cpu.fv = q < -128 || q > 127;
    return (uint16_t)(((uint8_t)r << 8) | (uint8_t)q);
}
static inline uint32_t divu16(uint32_t rr, uint16_t s) {
    if (s == 0) { cpu.fv = 1; return rr; }
    uint32_t q = rr / s, r = rr % s; cpu.fv = q > 0xFFFF;
    return (r << 16) | (q & 0xFFFF);
}
static inline uint32_t divs16(uint32_t rr, uint16_t s) {
    if (s == 0) { cpu.fv = 1; return rr; }
    int64_t q = (int64_t)(int32_t)rr / (int16_t)s, r = (int64_t)(int32_t)rr % (int16_t)s;
    cpu.fv = q < -32768 || q > 32767;
    return ((uint32_t)(uint16_t)r << 16) | (uint16_t)q;
}

static inline uint16_t mirr16(uint16_t v) {
    uint16_t r = 0; for (int i = 0; i < 16; i++) r |= ((v >> i) & 1) << (15 - i); return r;
}
static inline uint8_t daa(uint8_t a) {
    uint8_t adj = 0, c = cpu.fc;
    if (cpu.fh || (a & 15) > 9) adj |= 0x06;
    if (c || a > 0x99) { adj |= 0x60; c = 1; }
    uint8_t r = cpu.fn ? (uint8_t)(a - adj) : (uint8_t)(a + adj);
    cpu.fh = ((a ^ r) >> 4) & 1;
    szflags(r, 8); cpu.fv = parity(r); cpu.fc = c;
    return r;
}
static inline uint8_t bs1f(uint16_t v) {
    for (int i = 0; i < 16; i++) if (v >> i & 1) { cpu.fv = 0; return (uint8_t)i; }
    cpu.fv = 1; return 0;
}
static inline uint8_t bs1b(uint16_t v) {
    for (int i = 15; i >= 0; i--) if (v >> i & 1) { cpu.fv = 0; return (uint8_t)i; }
    cpu.fv = 1; return 0;
}

#endif
