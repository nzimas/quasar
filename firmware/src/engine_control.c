/* SPDX-License-Identifier: GPL-3.0-only
 * Parameter/event adapter only. These bytes preserve frontend session layouts;
 * no signal processing or DSP state is implemented in this file.
 *
 * A sound's syn[0 .. 7] are its circuit's knobs in the engine's order (P23_BD_*, P23_BASS_*, P23_SD_*,
 * P23_HHT_*; syn[0] the recipe's pitch offset from the note; syn[7] the circuit's mode: P23_BASS_*, P23_BD_*, P23_SD_*, P23_HHT_*), syn[P_ATT] /
 * syn[P_REL] its envelope, syn[P_DRIVE] its overdrive, syn[16 ..] its macros, syn[24 ..] its routes, amp[A_VOL]
 * its VOL and fx[X_DEL] its FX send. The panel (clock, looper, SHAOS, LFO, FX, distortion, attenuators, CV)
 * and the patchbay are the session's: pnl[] and pcab[]. */
#include "../engine/p23_engine.h"
enum { P_TUNE,P_SHAPE,P_WARP,P_CUT,P_Q,P_AMT,P_MODE,P_EXTRA,
       P_ATT,P_REL,P_LEVEL,P_GATE,P_UNUSED12,P_UNUSED13,P_UNUSED14,P_UNUSED15 };
#define P_BMODE 7u                 /* the circuit's mode (BASS DCO / PRC / PLUCK / FM, BD TRI / SKIN, ..) */
#define REL_MAX 72u                /* (a percussive instrument: release at most a ~0.22 s time constant) */
#define P_DRIVE 10u                /* the voice's overdrive (M5) */
#define P_WRECK 11u                /* syn[11 .. 15]: WRECK's FOLD, RING, RING FRQ, CRUSH, BITS */
#define P_MACRO 16u                /* syn[16 .. 23]: the macros (M5 .. M7 live in DRIVE, FX, ATT; theirs unused) */
#define P_ROUTE 24u                /* syn[24 .. 31]: four routes, two words each (params.c rt_*) */
/* What the bipolar macros (M1 .. M4, M8: 64 the recipe's setting) move, by circuit: up to four targets each, a
 * syn[] index (0 .. 7 the circuit, 8 ATT, 9 REL, 10 DRIVE) and how far at the knob's end. Routes that name a macro
 * are scaled by it as well (0 off, 64 as set, 127 about twice). */
typedef struct { uint8_t t; int8_t a; } mac_t;
static const mac_t MACRO_MAP[4][5][4] = {
    /* BASS: CUTOFF, TIMBRE, MOTION, DECAY, RESO */
    {{{4,127},{2,20},{0,0},{0,0}}, {{1,110},{2,80},{5,20},{0,0}}, {{3,60},{0,0},{0,0},{0,0}},
     {{9,127},{4,-20},{0,0},{0,0}}, {{5,110},{4,-15},{0,0},{0,0}}},
    /* BD: PUNCH, SHAPE, CHAOS, DECAY, BEND */
    {{{2,100},{9,-25},{3,15},{0,0}}, {{3,127},{0,0},{0,0},{0,0}}, {{1,70},{0,0},{0,0},{0,0}},
     {{9,127},{2,25},{0,0},{0,0}}, {{1,40},{0,0},{0,0},{0,0}}},
    /* SD: TONE, SNAP, CHAOS, DECAY, RING */
    {{{4,127},{0,40},{3,30},{0,0}}, {{2,100},{3,-50},{5,40},{0,0}}, {{1,80},{0,0},{0,0},{0,0}},
     {{9,127},{0,0},{0,0},{0,0}}, {{5,110},{0,0},{0,0},{0,0}}},
    /* HHT: TONE, GRIT, CHAOS, DECAY, RING */
    {{{2,127},{0,30},{0,0},{0,0}}, {{1,127},{0,-40},{3,20},{0,0}}, {{0,0},{0,0},{0,0},{0,0}},
     {{9,127},{0,0},{0,0},{0,0}}, {{3,110},{2,-20},{0,0},{0,0}}},
};
/* the panel's bytes, as saved */
enum { PN_CLK_SRC, PN_CLK_AMT, PN_SH_FREQ, PN_SH_LEN, PN_LFO_FREQ, PN_LFO_SHAPE, PN_LFO_RANGE, PN_LFO_AMT,
       PN_FX_MODE, PN_FX_ROUTE, PN_FX_TIME, PN_FX_TUNE, PN_FX_FB, PN_FX_CLK, PN_FX_MAD, PN_DLY_OUT, PN_REV_OUT,
       PN_DRIVE, PN_DMIX, PN_ATT1, PN_ATT2, PN_ATT3, PN_ATT4, PN_TOUCH1, PN_TOUCH2, PN_MCV1, PN_MCV2, PN_MCV3,
       PN_MCV4, PN_LR_BANK, PN_LR_VEL, PN_LR_MODE1, PN_LR_MODE6=PN_LR_MODE1+5,
       PN_ST_SLICE, PN_ST_RATE, PN_ST_MODE, PN_ST_DECAY, PN_ST_DRIFT, PN_ST_ROLL, PN_ST_MIX,   /* (the STUTTER's) */
       PN_CTRL_ALL, NPN };   /* (the session's CTRL ALL: 0 KNOBS, 1 ALL -- ui.c's PERFORMANCE; not the engine's) */
#define NPNL 48u
#define PNL_MARK 0xA5u             /* (pnl's last byte as saved: a panel is there) */
#define NPCAB P23_CABLES
/* the session layout's reserved bytes (Bigorna's delay, reverb, utility and compressor pages): kept, unused */
#define NDL 8u
#define NRV 7u
#define NCH 7u
#define NCP 8u
#define NCP_SCS 7u                 /* the DUCK page's sources: none, then each track's envelope */
static uint8_t pnl[NPNL];
static p23_cable pcab[NPCAB];
static volatile int32_t fx_bpm10=1200;
static volatile uint8_t pnl_run;   /* the transport: the INT clock runs with PLAY */
static volatile uint8_t stut_ui;   /* (SHIFT + the STUTTER's key held: ui.c) */
/* the STUTTER's RND settings: SLICE ST_SLC_RND, MODE ST_MODE_RND, RATE and DECAY ST_VAL_RND -- each press, a value
 * drawn for it (ui.c's stut_draw, into stut_pick: SLICE, RATE, MODE, DECAY) */
#define ST_SLC_RND 6u
#define ST_MODE_RND 4u
#define ST_VAL_RND 128u
static volatile uint8_t stut_pick[4];
static const uint8_t PNL0[NPN]={0,0,40,0,64,64,0,0, 0,0,40,64,50,0,0,90,90, 0,0,127,127,127,127,0,0,0,0,0,
                                 0,0,0,1,1,1,1,1,1, 1,64,0,0,64,0,127, 0};
static void fx_cook_all(void) {} /* the panel is read whole at each block */
static void fx_init(void)
{
    uint32_t i;
    memset(pnl,0,sizeof pnl); memcpy(pnl,PNL0,NPN);
    for(i=0;i<NPCAB;i++) pcab[i].a=pcab[i].b=P23_PIN_NONE;
}
/* a saved panel and patchbay taken (none saved: the start's) */
static void pnl_give(const uint8_t *p,const p23_cable *c)
{
    uint32_t i;
    fx_init();
    if(p[NPNL-1u]!=PNL_MARK) return;
    memcpy(pnl,p,NPN);
    if(!(pnl[PN_ST_RATE]|pnl[PN_ST_DRIFT]|pnl[PN_ST_MIX]))   /* (a session from before the STUTTER: its defaults) */
        memcpy(pnl+PN_ST_SLICE,PNL0+PN_ST_SLICE,NPN-PN_ST_SLICE);
    for(i=0;i<NPCAB;i++) {
        pcab[i]=c[i];
        if(pcab[i].a>=P23_PINS || pcab[i].b>=P23_PINS || pcab[i].a==pcab[i].b) pcab[i].a=pcab[i].b=P23_PIN_NONE;
    }
}
/* a macro's position (M1 .. M4: j 0 .. 3, M8: j 7), 64 the middle */
static int32_t mac_pos(const sound_t *s,uint32_t j) { return clampi(s->syn[P_MACRO+j],0,127); }
#define PIN_OWN_OUT P23_PINS          /* (a route's source: the track's own OUT .. */
#define PIN_OWN_ENV (P23_PINS+1u)     /* .. its own ENV, whatever track the sound is on) */
static void p23_from_frontend(const sound_t *s,p23_patch *p,uint32_t track)
{
    int32_t v[11]; uint32_t j,k;
    static const uint8_t MJ[5]={0,1,2,3,7};
    memset(p,0,sizeof *p);p->module=s->mach;
    for(j=0;j<11u;j++) v[j]=clampi(s->syn[j],0,127);
    for(j=0;j<5u;j++) {
        int32_t m=mac_pos(s,MJ[j])-64;
        for(k=0;k<4u && m;k++) {
            const mac_t *t=&MACRO_MAP[s->mach%NENG][j][k];
            if(t->a && t->t<11u) v[t->t]+=m*t->a/64;
        }
    }
    for(j=0;j<P23_KNOBS;j++) p->knob[j]=(uint8_t)clampi(v[j],0,127);
    p->mode=(uint8_t)clampi(s->syn[P_BMODE],0,4);
    p->knob[P_BMODE]=0;
    p->attack=(uint8_t)clampi(v[P_ATT],0,127);
    p->release=(uint8_t)clampi(v[P_REL],0,REL_MAX);
    p->drive=(uint8_t)clampi(v[P_DRIVE],0,127);
    p->perc=1;
    p->fold=(uint8_t)clampi(s->syn[P_WRECK],0,127); p->ring=(uint8_t)clampi(s->syn[P_WRECK+1u],0,127);
    p->ring_freq=(uint8_t)clampi(s->syn[P_WRECK+2u],0,127); p->crush=(uint8_t)clampi(s->syn[P_WRECK+3u],0,127);
    p->bits=(uint8_t)clampi(s->syn[P_WRECK+4u],0,127);                     /* (a percussive instrument: every sound falls once struck, held or not) */
    for(j=0;j<P23_ROUTES;j++) {
        uint32_t w0=(uint32_t)s->syn[P_ROUTE+2u*j],w1=(uint32_t)s->syn[P_ROUTE+1u+2u*j],mac=(w1>>8)%9u;
        int32_t depth=(int32_t)(w1&255u)-128;
        if(mac>=1u && mac!=5u && mac!=6u && mac!=7u) depth=depth*mac_pos(s,mac-1u)/64;
        uint32_t src=w0&255u;
        src=src==PIN_OWN_OUT?P23_PIN_T(track,P23_T_OUT):src==PIN_OWN_ENV?P23_PIN_T(track,P23_T_ENV):src<P23_PINS?src:0u;
        p->mod[j].src=(uint8_t)src; p->mod[j].dst=(uint8_t)((w0>>8)&7u);
        p->mod[j].depth=(int8_t)clampi(depth,-127,127);
    }
}
static void p23_global_from_frontend(void)
{
    p23_panel g; uint32_t j;
    memset(&g,0,sizeof g);
    g.bpm10=(uint16_t)clampi(fx_bpm10,10,3000); g.clock_run=pnl_run;
    g.clock_source=P23_CLK_INT; g.clock_amt=0;   /* (no clock settings: the INT clock follows the tempo, unbent) */
    g.shaos_freq=pnl[PN_SH_FREQ]; g.shaos_length=pnl[PN_SH_LEN];
    g.lfo_freq=pnl[PN_LFO_FREQ]; g.lfo_shape=pnl[PN_LFO_SHAPE]; g.lfo_range=pnl[PN_LFO_RANGE]; g.lfo_amt=pnl[PN_LFO_AMT];
    g.fx_mode=pnl[PN_FX_MODE]; g.fx_dly_rev=pnl[PN_FX_ROUTE]; g.fx_time=pnl[PN_FX_TIME]; g.fx_tune=pnl[PN_FX_TUNE];
    g.fx_feedback=pnl[PN_FX_FB]; g.fx_clk_mod=pnl[PN_FX_CLK]; g.fx_mad=pnl[PN_FX_MAD];
    g.dly_out=pnl[PN_DLY_OUT]; g.rev_out=pnl[PN_REV_OUT]; g.drive=pnl[PN_DRIVE]; g.drive_mix=pnl[PN_DMIX];
    for(j=0;j<4;j++) g.atten[j]=pnl[PN_ATT1+j],g.midi_cv[j]=pnl[PN_MCV1+j];
    g.touch[0]=pnl[PN_TOUCH1]; g.touch[1]=pnl[PN_TOUCH2];
    p23_set_panel(&g);
    {   /* (Quasar: no patchbay and no looper shown -- no cables, and each looper channel plays an empty tape, so
         * nothing hidden reaches the sound) */
        static const p23_cable none[P23_CABLES]={{0,0}};
        p23_set_cables(none);
    }
    for(j=0;j<P23_TRACKS;j++) p23_lr_mode(j,P23_LR_PLAY);
    {   /* the STUTTER as its page sets it (a break's own may stand in for it: tracks.c mix_block) */
        p23_stutter st;
        st.slice=pnl[PN_ST_SLICE]>=ST_SLC_RND?stut_pick[0]:pnl[PN_ST_SLICE];
        st.rate=pnl[PN_ST_RATE]>=ST_VAL_RND?stut_pick[1]:pnl[PN_ST_RATE];
        st.mode=pnl[PN_ST_MODE]>=ST_MODE_RND?stut_pick[2]:pnl[PN_ST_MODE];
        st.decay=pnl[PN_ST_DECAY]>=ST_VAL_RND?stut_pick[3]:pnl[PN_ST_DECAY];
        st.drift=pnl[PN_ST_DRIFT]; st.roll=pnl[PN_ST_ROLL]; st.mix=pnl[PN_ST_MIX];
        p23_set_stutter(&st);
    }
    p23_lr_velocity(pnl[PN_LR_VEL]==2u,pnl[PN_LR_VEL]==1u);
}
