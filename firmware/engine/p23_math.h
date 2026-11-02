/* SPDX-License-Identifier: GPL-3.0-only
 * New table-free arithmetic for this engine; no platform DSP dependencies. */
#ifndef P23_MATH_H
#define P23_MATH_H
#include <stdint.h>
#define PM_ONE 32768
#define PM_INLINE static inline __attribute__((always_inline))
PM_INLINE int32_t pm_bound(int32_t x,int32_t a,int32_t b) { return x<a?a:x>b?b:x; }
PM_INLINE int32_t pm_abs(int32_t x) { return x<0?-x:x; }
PM_INLINE int32_t pm_product(int32_t a,int32_t b) { return (int32_t)(((int64_t)a*b)>>15); }
/* Fixed shifts keep the target free of 64-bit division runtime calls. */
PM_INLINE int32_t pm_q16(int32_t a,int32_t b) { return (int32_t)(((int64_t)a*b)>>16); }
/* x / 2^k exactly as C divides (toward zero), in shifts: the target's compiler keeps a divide instruction for a
 * signed division by a power of two (the per-sample code's: the same results, bit for bit) */
PM_INLINE int32_t pm_sdiv(int32_t x,uint32_t k) { return (x+((x>>31)&(int32_t)((1u<<k)-1u)))>>k; }
PM_INLINE int32_t pm_soft(int32_t x)
{
    int32_t a=pm_abs(x), t;
    if (a<=16384) return x;
    if (a>=49152) return x<0?-PM_ONE:PM_ONE;
    t=a-16384; a=16384+t-(int32_t)(((int64_t)t*t)>>16);
    return x<0?-a:a;
}
/* Fifth-degree exp2 on [0,1], exact at both endpoints. No lookup tables. */
PM_INLINE uint32_t pm_exp(int32_t oct16)
{
    int32_t whole=oct16/65536, frac=oct16%65536, y;
    if (frac<0) { whole--; frac+=65536; }
    y=98; y=631+pm_q16(y,frac); y=3638+pm_q16(y,frac);
    y=15743+pm_q16(y,frac); y=45426+pm_q16(y,frac); y=65536+pm_q16(y,frac);
    if (whole>=15) return 0x7fffffffu;
    if (whole < -16) return 0;
    return whole>=0?(uint32_t)y<<(uint32_t)whole:(uint32_t)y>>(uint32_t)-whole;
}
PM_INLINE uint32_t pm_frequency(uint32_t hz256)
{ return (uint32_t)(((uint64_t)hz256*6382652533ull)>>24); }
PM_INLINE uint32_t pm_note(int32_t note256)
{
    int32_t octave=(note256-69*256)*256/12;
    return (uint32_t)(((uint64_t)42852281u*pm_exp(octave))>>16);
}
/* Corrected parabolic sine, Q15, evaluated directly from phase. */
PM_INLINE int32_t pm_sine(uint32_t phase)
{
    int32_t x=(int32_t)((phase&0x7fffffffu)>>15);
    int32_t y=(int32_t)(((int64_t)x*(65536-x))>>14);
    y+=pm_q16(pm_q16(y,y-65536),14746);
    y/=2;
    return phase&0x80000000u?-y:y;
}
PM_INLINE int32_t pm_triangle(uint32_t phase)
{
    uint32_t x=phase>>16;
    return x<32768u?(int32_t)x*2-32768:98304-(int32_t)x*2;
}
PM_INLINE int32_t pm_white(uint32_t *state)
{
    /* Weyl sequence + avalanche: independent of the host's control randomizer. */
    uint32_t x=(*state+=0x61c88647u);
    x=(x^(x>>16))*0x21f0aaadu; x=(x^(x>>15))*0x735a2d97u; x^=x>>15;
    return (int32_t)(x>>16)-32768;
}
/* hz256 = lo256 x 2^(oct16/65536): knob laws and exponential CV */
PM_INLINE uint32_t pm_hz(uint32_t lo256,int32_t oct16)
{ return (uint32_t)(((uint64_t)lo256*pm_exp(oct16))>>16); }
/* A one-pole smoothing coefficient (Q15) for a corner at hz256: w/(1+w), w = 2 pi f / 44100 */
PM_INLINE int32_t pm_pole(uint32_t hz256)
{
    uint32_t w=(uint32_t)(((uint64_t)hz256*1195u)>>16);
    if(w>200000u) w=200000u;
    return (int32_t)(w*32768u/(32768u+w));
}
/* Chamberlin integrator coefficient for two substeps a sample: 2 sin(pi f / 88200), Q15 */
PM_INLINE int32_t pm_svf_g(uint32_t hz256)
{
    if(hz256>18000u*256u) hz256=18000u*256u;
    if(hz256<8u*256u) hz256=8u*256u;
    return 2*pm_sine(pm_frequency(hz256)/4u);
}
PM_INLINE int32_t pm_blep(uint32_t ph,uint32_t inc,uint32_t reciprocal)
{
    int32_t q;
    if (ph<inc) {
        q=(int32_t)(((uint64_t)ph*reciprocal)>>17);
        return q+q-pm_product(q,q)-32768;
    }
    if (ph>0xffffffffu-inc) {
        q=-(int32_t)(((uint64_t)(0u-ph)*reciprocal)>>17);
        return pm_product(q,q)+q+q+32768;
    }
    return 0;
}
#endif
