/* SPDX-License-Identifier: GPL-3.0-only
 * Pulsar23 engine, build 3: a virtual Pulsar-23 panel. Independent API: no Bigorna headers, patch types,
 * tables or allocator.
 *
 * Every module of the instrument is a permanent circuit with pins. Pins carry voltages in a common
 * representation (P23_VOLT per volt, 0 .. 10 V nominal for CV and gates; audio swings about 0 V). Cables
 * join two pins, as alligator clips do: the pins joined form a net; outputs on a net mix, inputs read it.
 * Unpatched inputs keep the instrument's hard-wired (normalled) routing. */
#ifndef P23_ENGINE_H
#define P23_ENGINE_H
#include <stdint.h>
#define P23_RATE 44100u
#define P23_BLOCK 32u
#define P23_NODES 9u          /* six tracks, then three extra sequencer lanes */
#define P23_TRACKS 6u
#define P23_CABLES 32u
#define P23_LR_BANKS 4u
#define P23_VOLT 3277         /* one volt in pin units: 10 V = 32768 */

/* The four circuits a track can carry (wire order retained by the control adapter). */
enum { P23_BASS, P23_BD, P23_SD, P23_HHT };
/* BASS modes: the DCO (pitch from the note, plus CV IN at 1 V/oct) or PRC percussion */
enum { P23_BASS_DCO, P23_BASS_PRC, P23_BASS_PLUCK, P23_BASS_FM, P23_BASS_GRIND };   /* (each circuit's modes: p23_patch.mode) */
enum { P23_BD_TRI, P23_BD_SKIN, P23_BD_HAMMER };
enum { P23_SD_NOISE, P23_SD_TONE, P23_SD_WIRE, P23_SD_WALL };
enum { P23_HHT_NOISE, P23_HHT_METAL, P23_HHT_SHAKE, P23_HHT_STATIC };

/* Module knobs of p23_patch.knob[], by circuit, 0 .. 127. Pitch comes from the note played (the scale);
 * TUNE is an offset from it in half semitones about 64, for the recipes (no panel knob). */
enum { P23_BD_TUNE, P23_BD_AMT, P23_BD_PITCH, P23_BD_DRIVE, P23_BD_STAB };
enum { P23_BASS_TUNE, P23_BASS_SHAPE, P23_BASS_WARP, P23_BASS_AMT, P23_BASS_LPF, P23_BASS_Q, P23_BASS_GLIDE };
enum { P23_SD_TUNE, P23_SD_AMT, P23_SD_CLAP, P23_SD_MIX, P23_SD_BPF, P23_SD_Q };
enum { P23_HHT_TUNE, P23_HHT_WARP, P23_HHT_HPF, P23_HHT_Q };
#define P23_KNOBS 8u

/* ---------------------------------------------------------------- pins --- */
/* Each track's pins: P23_PIN_T(track, P23_T_*). MOD1 .. MOD5 mean, by circuit:
 *   BD:   MOD1 pitch (linear, via AMT), MOD2 WTF? (pitch-modulator bend node), MOD3 OMG! (core bend node)
 *   BASS: MOD1 CV IN (1 V/oct), MOD2 SHAPE, MOD3 WARP, MOD4 AMT (phase modulation; PRC: sidechain), MOD5 LPF FR
 *   SD:   MOD1 TUNE (noise spectrum, via AMT), MOD2 BPF FR
 *   HHT:  MOD1 TUNE (noise spectrum), MOD2 HPF FR */
enum { P23_T_OUT, P23_T_ENV, P23_T_TRIG, P23_T_EXT, P23_T_MOD1, P23_T_MOD2, P23_T_MOD3, P23_T_MOD4, P23_T_MOD5,
       P23_TRACK_PINS };
#define P23_PIN_T(t,k) (1u+(uint32_t)(t)*P23_TRACK_PINS+(uint32_t)(k))
enum {
    P23_PIN_NONE=0,
    P23_PIN_CLK=1+P23_TRACKS*P23_TRACK_PINS,   /* clock out, or in with the clock on EXT */
    P23_PIN_CLK_MOD,
    P23_PIN_DIV16, P23_PIN_DIV8, P23_PIN_DIV4, P23_PIN_DIV2, P23_PIN_DIV1, P23_PIN_DIV05, P23_PIN_DIV025,
    P23_PIN_LRST,
    P23_PIN_LR_CLK1, P23_PIN_LR_CLK6=P23_PIN_LR_CLK1+P23_TRACKS-1,
    P23_PIN_SH_MOD, P23_PIN_SH_CLK, P23_PIN_SH_SH, P23_PIN_SH_DATA,
    P23_PIN_SH_1DIR, P23_PIN_SH_2DIR, P23_PIN_SH_1SH, P23_PIN_SH_3SH,
    P23_PIN_LFO_MOD, P23_PIN_LFO_SYNC, P23_PIN_LFO_SQR, P23_PIN_LFO_TRI,
    P23_PIN_FX_TIME, P23_PIN_FX_CLK, P23_PIN_FX_TUNE, P23_PIN_FX_FB, P23_PIN_FX_MAD,
    P23_PIN_FX_DLY_IN, P23_PIN_FX_REV_IN, P23_PIN_FX_DLY_OUT, P23_PIN_FX_REV_OUT,
    P23_PIN_MIX_IN,
    P23_PIN_VCA1_IN, P23_PIN_VCA1_CV, P23_PIN_VCA1_OUT, P23_PIN_VCA2_IN, P23_PIN_VCA2_CV, P23_PIN_VCA2_OUT,
    P23_PIN_INV_IN, P23_PIN_INV_OUT, P23_PIN_CINV_IN, P23_PIN_CINV_CV, P23_PIN_CINV_OUT,
    P23_PIN_SW1_IN, P23_PIN_SW1_CV, P23_PIN_SW1_OUT, P23_PIN_SW2_IN, P23_PIN_SW2_CV, P23_PIN_SW2_OUT,
    P23_PIN_ATT1_IN, P23_PIN_ATT1_OUT, P23_PIN_ATT2_IN, P23_PIN_ATT2_OUT,
    P23_PIN_ATT3_IN, P23_PIN_ATT3_OUT, P23_PIN_ATT4_IN, P23_PIN_ATT4_OUT,
    P23_PIN_IMP1_IN, P23_PIN_IMP1_OUT, P23_PIN_IMP2_IN, P23_PIN_IMP2_OUT,
    P23_PIN_MCV1, P23_PIN_MCV2, P23_PIN_MCV3, P23_PIN_MCV4,
    P23_PIN_NOISE, P23_PIN_P10V, P23_PIN_GND, P23_PIN_TOUCH1, P23_PIN_TOUCH2,
    P23_PIN_DIODE_A, P23_PIN_DIODE_K, P23_PIN_CAP1_A, P23_PIN_CAP1_B, P23_PIN_CAP2_A, P23_PIN_CAP2_B,
    P23_PINS
};
typedef struct { uint8_t a, b; } p23_cable;   /* two pins joined; P23_PIN_NONE: unused */

/* ------------------------------------------------------------ settings --- */
/* A sound's own modulation routes: a source pin's voltage x depth / 127 added to one of the circuit's inputs.
 * They work alongside the patchbay, as if through a private attenuator (negative depth: through an inverter). */
#define P23_ROUTES 4u
enum { P23_RD_EXT, P23_RD_MOD1, P23_RD_MOD2, P23_RD_MOD3, P23_RD_MOD4, P23_RD_MOD5, P23_RD_DRIVE, P23_RD_VCA, P23_RDESTS };
typedef struct { uint8_t src, dst; int8_t depth; } p23_route_mod;
typedef struct {
    uint8_t module;                /* P23_BASS .. P23_HHT */
    uint8_t mode;                  /* the circuit's mode: P23_BASS_*, P23_BD_TRI / SKIN, P23_SD_*, P23_HHT_* */
    uint8_t knob[P23_KNOBS];       /* the circuit's knobs, above */
    uint8_t attack, release;       /* the AR envelope generator's ATT and REL */
    uint8_t drive;                 /* the voice's overdrive, 0 (bypassed) .. 127 */
    uint8_t perc;                  /* 1: percussive -- the envelope falls once its attack peaks, the gate held or not */
    uint8_t fold, ring, ring_freq, crush, bits;   /* the voice's WRECK stage, after its overdrive: all 0 bypass it */
    p23_route_mod mod[P23_ROUTES];
} p23_patch;
typedef struct {
    uint8_t enabled, track, muted;
    int16_t pan;                   /* -127 .. 127 (the FM-1's stereo field; the Pulsar's bus is mono) */
    uint32_t level;                /* VOL, Q15, 0 .. 262144 */
    uint8_t fx_send;               /* FX knob: pre-VOL send to the FX submixer */
    uint8_t duck_source, duck_amount;   /* duck_source: node index, 255 off */
} p23_route;
enum { P23_CLK_INT, P23_CLK_EXT, P23_CLK_STOP };   /* INT (follows the session tempo), middle (CLK pin), stopped */
enum { P23_FX_BPF, P23_FX_DBL, P23_FX_PCH };
enum { P23_SH_63, P23_SH_16, P23_SH_217 };
enum { P23_LFO_LOW, P23_LFO_MID, P23_LFO_HI };
typedef struct {
    /* CLOCK */
    uint16_t bpm10;                /* session tempo x 10: INT clock = 32nd notes (8 ticks a beat) */
    uint8_t clock_source, clock_amt, clock_run;
    /* SHAOS */
    uint8_t shaos_freq, shaos_length;
    /* LFO */
    uint8_t lfo_freq, lfo_shape, lfo_range, lfo_amt;
    /* FX */
    uint8_t fx_mode, fx_dly_rev;   /* fx_dly_rev: 0 the submixer feeds DLY, 1 REV */
    uint8_t fx_time, fx_tune, fx_feedback, fx_clk_mod, fx_mad;   /* fx_mad: 1 = +10 V on MAD!/stereo */
    uint8_t dly_out, rev_out;
    /* DISTORTION, ATTENUATORS, TOUCH CV, MIDI CV, master */
    uint8_t drive, drive_mix;
    uint8_t atten[4], touch[2], midi_cv[4];
} p23_panel;

/* ----------------------------------------------------- looper-recorder --- */
enum { P23_LR_MUTE, P23_LR_PLAY, P23_LR_REC };
enum { P23_LR_ADD, P23_LR_DEL };
#define P23_LR_TICKS 128u          /* one loop: 128 clock ticks (32nd notes) .. */
#define P23_LR_SUB 96u             /* .. each upsampled 96 times */

typedef struct {
    int32_t envelope, peak, out;
    uint32_t oscillator_phase;
    uint8_t active, gate, note, velocity;
} p23_meter;
typedef struct {
    uint32_t position;             /* LR position, 0 .. P23_LR_TICKS*P23_LR_SUB-1 */
    uint8_t running, bank, tick, clock_led;   /* clock_led: 1 on a quarter, 2 at the loop start */
    uint8_t mode[P23_TRACKS], level[P23_TRACKS], events[P23_TRACKS];
    uint8_t shaos, lfo_led, clip;
} p23_status;

void p23_reset(uint32_t seed);
void p23_set_patch(uint32_t node, const p23_patch *patch);
void p23_set_route(uint32_t node, const p23_route *route);
void p23_set_panel(const p23_panel *panel);
/* The whole patchbay at once (P23_CABLES joins); nets are rebuilt at the next block. */
void p23_set_cables(const p23_cable cables[P23_CABLES]);
/* Gates from the keys, the step sequencer and MIDI (source 0 .. 2). They merge with the looper by the
 * strongest voltage; the latest held note sets pitch. */
void p23_note(uint32_t node, uint32_t source, uint32_t note, uint32_t velocity, int on);
void p23_hush(uint32_t node);
void p23_bend(uint32_t node, int32_t cents);   /* BASS pitch bend (MIDI: +/- 12 semitones) */
/* The looper-recorder's sensors and switches (channel = track). */
void p23_lr_mode(uint32_t channel, uint32_t mode);
void p23_lr_touch(uint32_t channel, uint32_t sensor, int on);
void p23_lr_velocity(uint32_t low, uint32_t middle);   /* the REC.CONT L and M sensors held */
void p23_lr_bank(uint32_t bank, int copy);   /* BANK + ADD/DEL (copy: + ADD + DEL, while held) */
void p23_lr_copy_end(void);
void p23_lr_start(uint32_t eighth);          /* BANK + M + sensor: (re)start from that eighth of the loop */
void p23_lr_stop(void);                      /* BANK + L */
void p23_lr_quantize(uint32_t channel);      /* BANK + L + M + sensor: to 16ths; the LR stops */
void p23_lr_clear(uint32_t channel);
void p23_clock_reset(void);                  /* RST: dividers and LR to the loop start */
void p23_click(int accent);
void p23_set_master(uint16_t gain4096);
/* The STUTTER: a beat repeat of the whole output, apart from the FX (its own 0.74 s of memory). Held, it takes the
 * slice just heard (on the next 32nd while the clock runs) and plays it round and round in place of the mix */
typedef struct {
    uint8_t slice;   /* 0 .. 5: 1/32, 1/16, 1/8, 1/4, 1/16 triplet, 1/8 triplet of the tempo */
    uint8_t rate;    /* 0 .. 127: its speed, 64 as heard (a quarter .. four times) */
    uint8_t mode;    /* P23_ST_*: forward, reversed, turning each pass, or each pass its own way and speed */
    uint8_t decay;   /* 0 .. 127: each pass quieter */
    uint8_t drift;   /* 0 .. 127: each pass slower (below 64) or faster (above) */
    uint8_t roll;    /* 0 .. 127: the slice halves every so many passes, faster as it rises (0: never) */
    uint8_t mix;     /* 0 .. 127: how far the repeat replaces the mix */
} p23_stutter;
enum { P23_ST_FWD, P23_ST_REV, P23_ST_ALT, P23_ST_RND };
void p23_set_stutter(const p23_stutter *s);
void p23_stutter_hold(int on);
int p23_stutter_active(void);
void p23_copy_usb(int32_t stereo[P23_BLOCK*2]);
/* Advances every circuit, including oscillators whose VCA is closed. */
void p23_process(int32_t stereo[P23_BLOCK*2]);
/* The same block in parts, for two cores: begin (the panel, gates, preparation; returns bit p set when part p has
 * nodes to render), part 0 and part 1 (in either order or at once, on different cores: they share no state), then
 * end (the patch, the FX and the output). drop(p) silences a part whose render was lost. */
uint32_t p23_process_begin(void);
void p23_process_part(uint32_t part);
void p23_process_drop(uint32_t part);
void p23_process_end(int32_t stereo[P23_BLOCK*2]);
p23_meter p23_inspect(uint32_t node);
void p23_status_get(p23_status *status);
int32_t p23_pin_value(uint32_t pin);         /* last value on a pin (its net), for the patch page's meter */
uint32_t p23_active_count(void);
uint32_t p23_state_bytes(void);
/* Diagnosis only: leave sections out of p23_process to measure their cost (1 FX, 2 circuits, 4 gates and
 * envelopes, 8 patchbay / clock / looper, 16 block preparation). 0: everything (the default). */
void p23_diagnostic(uint32_t mask);
uint32_t p23_diag_info(uint32_t i);   /* (telemetry: which modules run per sample) */
#endif
