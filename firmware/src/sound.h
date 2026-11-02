/* SPDX-License-Identifier: GPL-3.0-only
 * Frontend patch records retain the control/storage wire layout (0 .. 127, bipolar values centred
 * on 64). Reserved filter/amp/effect bytes support the sequencer storage layout only:
 *   the SYN machine and its 32 parameters (SYN pages 1 .. 4, knobs A .. H), the FLTR machine and its page, the
 *   base-width filter, the AMP page and the FX page.
 * The adapter translates this record into the independent p23_patch API. No DSP state lives here. */
#pragma once

enum { ENG_BASS, ENG_BD, ENG_SD, ENG_HHT, NENG };
/* Reserved frontend IDs only: no filter machines or old AMP/FX DSP are linked. */
enum { FLT_MM, FLT_LP4, FLT_LEG, FLT_CBM, FLT_CBP, FLT_EQ, NFLT };   /* MULTI-MODE, LOWPASS 4, LEGACY LP/HP, COMB-, COMB+, EQUALIZER */

/* FLTR pages 1 and 2 */
enum { F_ATK, F_DEC, F_SUS, F_REL, F_FREQ, F_RESO, F_TYPE, F_ENV, F_DEL, F_KEYT, F_BASE, F_WDTH, F_BWRT, F_RSET, NF };
/* AMP page */
enum { A_ATK, A_HOLD, A_DEC, A_SUS, A_REL, A_RSET, A_MODE, A_PAN, A_VOL, NA };
/* FX page (the routing values: 0 PRE, 1 POST) */
enum { X_BR, X_OD, X_SRR, X_ODT, X_ODRT, X_DEL, X_REV, X_CHR, NX };   /* (X_ODT: OD TYPE from build 61; SRR's route
                                                                    * until build 16, GRAIN in build 17, then unused) */
/* MANGLE page: DESTROY (digital destruction: its amount and its kind) and SLICE (a gate in time with the tempo: its
 * pattern, OFF or 1 .. 16; its rate; how deep it cuts; its edges; the pattern turned by steps) */
enum { M_DST, M_DMODE, M_SLICE, M_SRATE, M_SDEPTH, M_SEDGE, M_SSHIFT, M_SPARE, NM };

/* Native module parameters are defined by engine_control.c, not FM synthesis enums. */

/* an LFO, as the Digitone II's (tracks.c runs them; its MOD pages here: LFO 1 .. 3 after the effects') */
#define NLFO 3
enum { LW_TRI, LW_SINE, LW_SQR, LW_SAW, LW_EXP, LW_RAMP, LW_RND, NLW };
enum { LM_FREE, LM_TRIG, LM_HOLD, LM_ONE, LM_HALF, NLM };
#define NLMULT 24                /* MULT: x1 .. x2048 with the tempo (0 .. 11), the same at 120 BPM (12 .. 23) */
typedef struct {
    uint8_t spd;                 /* SPEED, 0 .. 127, 64 the centre (-64 .. +63; below: the cycle backwards) */
    uint8_t mult;                /* MULT, 0 .. 23 */
    uint8_t fade;                /* FADE, 0 .. 127, 64 none (below: in, above: out) */
    uint8_t dest;                /* DEST: 0 none, 1 + a sound's parameter (page x 8 + knob), or LFO_D* */
    uint8_t wave;                /* WAVE, LW_* */
    uint8_t sph;                 /* START PHASE (RND: SLEW), 0 .. 127 */
    uint8_t mode;                /* MODE, LM_* */
    uint8_t dep;                 /* DEPTH, 0 .. 127, 64 the centre (-64 .. +63) */
} lfo_t;
#define LFO_D_LFO 73u            /* (DEST 73 .. 76: LFO 1's SPEED, its DEPTH, LFO 2's SPEED, its DEPTH -- for the later LFOs) */
#define LFO_ND 77u

typedef struct {
    uint8_t mach;                /* ENG_* */
    uint8_t fmach;               /* FLT_* */
    int16_t syn[32];             /* (4-OP's RATIO B is 0 .. 360: B1 x 19 + B2) */
    uint8_t flt[NF];
    uint8_t amp[NA];
    uint8_t fx[NX];
    uint8_t family;              /* the recipe it was drawn from (gen.c), for SHIFT + HOME */
    uint8_t mg[NM];              /* MANGLE (from build 18; sessions saved before load with it all 0: off) */
    uint32_t seed;
    lfo_t lfo[NLFO];             /* the three LFOs (from build 50; sessions saved before load with them off) */
} sound_t;

/* one parameter's place on a page, its range, and its centre (bipolar parameters show value - centre) */
typedef struct { const char *name; int16_t lo, hi, mid; } pdef_t;
