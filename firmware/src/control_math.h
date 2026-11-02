/* SPDX-License-Identifier: GPL-3.0-only
 * Retained generic arithmetic for the Bigorna UI, sequencer and control LFOs.
 * The separately compiled audio engine neither includes nor links these helpers. */
#pragma once
#include <stdint.h>
/* big zero-initialised buffers go to the pool (app.ld: 336 KiB beside the 96 KiB of .data / .bss) */
/* always inline: the audio's RAM code (FAST) may not call into the flash */
#define AINL static inline __attribute__((always_inline))
/* a per-sample loop unrolled by 4 (for (i = 0; i < CTL;) { REP4({ ... }) }, CTL a multiple of 4): on this chip a
 * loop's step, its branch back, costs ~7 cycles and an add 1 (build 31, measured); -Os ignores clang's unroll pragma */
#define REP4(...) { __VA_ARGS__ } i++; { __VA_ARGS__ } i++; { __VA_ARGS__ } i++; { __VA_ARGS__ } i++;
/* RAM code and the flash are beyond the 23-bit call's range, each way: such a call goes through a pointer the
 * compiler cannot fold back (as the flash driver's FL_FAR) */
AINL void *far_ptr(void *p) { void *volatile q = p; return q; }
#define FAR(fn) ((__typeof__(&fn))far_ptr((void *)&fn))
#ifdef HOST
#define POOL
#define FAST                     /* (the host: no RAM code) */
#define DLYMEM
#else
#define POOL __attribute__((section(".pool")))
#ifndef FAST_RAM
#define FAST_RAM 1               /* 1: the audio's hot path (FAST) runs from RAM (app.ld .fast_text) */
#endif
#if FAST_RAM
#define FAST __attribute__((section(".fast_text"), noinline))   /* run from RAM (app.ld): the audio's hottest code */
#else
#define FAST                     /* (in the flash, as before build 31) */
#endif
#define DLYMEM __attribute__((section(".dly_mem")))   /* data in the RAM beside the code (app.ld .dly): the delay's */
#endif

#define Q15 32768
#define Q16 65536

/* a layer's generative path: what HOME's draws lean towards, on every engine (the open path: no leaning) */
enum { PATH_OPEN, PATH_AMBIENT, PATH_EXP, PATH_N };
/* the generators' generation: a sound is drawn again from (seed, path, gen) bit for bit, so a sound saved as its
 * seed (sessions, presets) keeps its generation, and every generation stays as it was (tests/golden: 0 is the
 * generators up to 0.20). New sounds are drawn on GEN_NOW. */
#define GEN_NOW 3u
#define GEN_N 4u                 /* 0: up to 0.20; 1: 0.21 (more families, kinds, species); 2: FM's ratios as meant; 3: 0.29 (plucks, mallets, warps, glitches, rubber; eight ANIMA kinds more, and breadth) */
/* EDIT's four macros on a sound: -MAC_MAX .. MAC_MAX each, 0 = the sound as it was drawn */
#define MAC_MAX 64
#define NMAC 4u

AINL int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
AINL int32_t mulq15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
AINL int32_t mulq16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
AINL int32_t mulq12(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 12); }
AINL int32_t absi(int32_t x) { return x < 0 ? -x : x; }
AINL int32_t maxi(int32_t a, int32_t b) { return a > b ? a : b; }

/* 2^(x / 65536), Q16 in and out; x from -16 to +15 octaves */
AINL uint32_t exp2_q16(int32_t x)
{
    int32_t i = x >> 16;
    uint32_t f = (uint32_t)x & 0xFFFFu, k = f >> 8, a = EXP2_FRAC[k];
    uint32_t m = a + (((EXP2_FRAC[k + 1u] - a) * (f & 0xFFu)) >> 8);   /* 2^frac, Q16 (65536 .. 131072) */
    if (i >= 15)
        return 0x7FFFFFFFu;
    if (i >= 0)
        return m << i;
    if (i < -24)
        return 0;
    return m >> -i;
}

/* log2(x), x > 0 an integer; Q16 result */
AINL int32_t log2_q16(uint32_t x)
{
    int32_t e = 0;
    uint32_t f, k;
    if (!x)
        return -(32 << 16);
    while (x >= (2u << 15)) {               /* bring x into 1 .. 2 as Q15 (32768 .. 65535) */
        x >>= 1;
        e++;
    }
    while (x < (1u << 15)) {
        x <<= 1;
        e--;
    }
    f = (x - (1u << 15)) << 1;              /* the fraction, Q16 */
    k = f >> 8;
    return ((e + 15) << 16) + (int32_t)(LOG2_FRAC[k] + (((LOG2_FRAC[k + 1u] - LOG2_FRAC[k]) * (f & 0xFFu)) >> 8));
}

/* sine of a phase (2^32 = one cycle), Q15, linear between SINE's 1024 points */
AINL int32_t sin_ph(uint32_t ph)
{
    const int16_t *s = &SINE[ph >> 22];
    return s[0] + (((s[1] - s[0]) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

typedef struct { uint32_t s; } rnd_t;
AINL uint32_t rnd_next(rnd_t *r)
{
    uint32_t x = r->s ? r->s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}
AINL void rnd_seed(rnd_t *r, uint32_t seed)
{
    uint32_t i;
    r->s = seed * 2654435761u + 0x6A09E667u;
    if (!r->s)
        r->s = 1;
    for (i = 0; i < 4u; i++)
        rnd_next(r);
}
AINL int32_t rnd_q15(rnd_t *r) { return (int32_t)(rnd_next(r) >> 17); }               /* 0 .. 32767 */
AINL int32_t rnd_bi(rnd_t *r) { return (int32_t)(rnd_next(r) >> 16) - 32768; }        /* -32768 .. 32767 */
AINL int32_t rnd_below(rnd_t *r, int32_t n) { return n <= 1 ? 0 : (int32_t)(((uint64_t)rnd_next(r) * (uint32_t)n) >> 32); }
AINL int rnd_chance(rnd_t *r, int32_t p_pct) { return rnd_below(r, 1000) < p_pct * 10; }   /* p in percent */
/* one of n, each as likely as its weight */
AINL uint32_t rnd_weighted(rnd_t *r, const uint8_t *w, uint32_t n)
{
    uint32_t i, t = 0, x;
    for (i = 0; i < n; i++)
        t += w[i];
    x = (uint32_t)rnd_below(r, (int32_t)t);
    for (i = 0; i + 1u < n && x >= w[i]; i++)
        x -= w[i];
    return i;
}
AINL int rnd_chance_q15(rnd_t *r, int32_t p) { return rnd_q15(r) < p; }
/* uniform in [a, b] (any integer unit) */
AINL int32_t rnd_in(rnd_t *r, int32_t a, int32_t b)
{
    return a + (int32_t)(((int64_t)(b - a) * rnd_q15(r)) >> 15);
}

