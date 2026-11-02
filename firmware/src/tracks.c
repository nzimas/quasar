/* SPDX-License-Identifier: GPL-3.0-only
 * Bigorna's control/sequencing port. Audio crosses only the p23_engine.h API.
 * ck_* are frontend parameter snapshots, not cooked DSP coefficients.
 * There is no voice pool, audio filter chain, effect tank or audio mixer here.
 */
#define NTRACK 6
#define NSUB 4u
#define NXL 3u
#define NLANE (NTRACK+NXL)
static volatile uint8_t ln_t[NLANE]={0,1,2,3,4,5},ln_s[NLANE],ln_on[NLANE]={1,1,1,1,1,1};
#define LANE_T(l) ((uint32_t)ln_t[l])
#define LANE_S(l) ((l)<NTRACK?0u:(uint32_t)ln_s[l])
static const char *const ENG_NAME[NENG]={"BASS","BD","SD","HHT"};
static const char *const FLT_NAME[NFLT]={"NATIVE","NATIVE","NATIVE","NATIVE","NATIVE","NATIVE"};
#define NPL 8u
#define PL_NONE 0xffu
#define PL_PAN 72u
#define PL_DEL (7u*8u+4u)
#define PL_REV (7u*8u+5u)
#define PL_CHR (7u*8u+6u)
#define PL_MNG (8u*8u)
static int pl_track(uint32_t id) { return id==PL_DEL || id==PL_REV || id==PL_CHR || id>=PL_MNG; }
static void par_put(sound_t *,uint32_t,uint32_t,int32_t);
static void lfo_apply(sound_t *,const int32_t *);
typedef struct { uint8_t mach; sound_t snd; } ck_t;
static ck_t ck_buf[NLANE][2] POOL;
static volatile uint8_t ck_pub[NLANE],ck_use[NLANE];
static void ck_cook(ck_t *c,const sound_t *s) { c->mach=s->mach; c->snd=*s; }
static volatile uint8_t sc_src[NTRACK],sc_lvl[NTRACK]={64,64,64,64,64,64};
static uint8_t sc_sub[NTRACK];
static volatile uint8_t sc_ln[NTRACK]={255,255,255,255,255,255};
/* Only transport locks and UI metering live in the frontend. */
static struct { int32_t meter; uint8_t tid[NPL]; int16_t tv[NPL]; } trk[NLANE];
static volatile int32_t trk_gain[NLANE],trk_pl[NLANE],trk_pr[NLANE],trk_pan[NLANE];
static volatile uint8_t trk_mute[NTRACK];
static volatile uint8_t lfo_trig[NLANE];
static uint32_t lfo_flips[NLANE];
static uint8_t active_lid[NLANE][NPL];
static int16_t active_lv[NLANE][NPL];
static uint32_t blk_clock;
#if TELEMETRY
static uint32_t tl_cook[8];                /* (the diagnostic page's cook costs: there are no cooks now) */
static volatile uint8_t tl_c1_crash;       /* (its CPU1 fault test: the engine runs on CPU0 alone) */
#endif
static volatile uint8_t load_pct;
static int32_t master_q12=2048;
#if defined(FELUCCA_UAC) && FELUCCA_UAC
static int32_t usb_blk[2*CTL];
#endif
#define COST_NOW() FAR(fm1_ticks)()
#define BLK_TICKS ((uint32_t)CTL*1000000u/FS*FM1_TICKS_PER_US)
enum { SRC_KEY,SRC_SEQ,SRC_ARP };
enum { EV_ON,EV_OFF,EV_STOP,EV_ARP_ON,EV_ARP_OFF,EV_ARP_CLR,EV_LR,EV_LR_BANK,EV_LR_RUN,EV_LR_QUANT,EV_LR_CLEAR };
static uint8_t lr_held[NTRACK][2];   /* (the looper's sensors: the keys holding each track's ADD and DEL) */
typedef struct { uint8_t type,track,note,val; } ev_t;
#define EVQ 64u
static ev_t evq[EVQ];
static volatile uint32_t ev_w,ev_r;
static void ev_post(uint32_t type,uint32_t t,uint32_t note,uint32_t val)
{
    ev_t e;
    if(ev_w-ev_r>=EVQ || t>=NLANE) return;
    e.type=(uint8_t)type;e.track=(uint8_t)t;e.note=(uint8_t)note;e.val=(uint8_t)val;
    evq[ev_w%EVQ]=e;RING_PUBLISH();ev_w++;
}
static uint8_t note_vel=127;
static uint8_t lane_force[NLANE];         /* (a note set the lane's patch without the block's LFO and locks) */
static const volatile uint8_t *note_lid;
static const volatile int16_t *note_lv;
static void note_on(uint32_t lane,uint32_t note,uint32_t source)
{
    uint32_t j; sound_t s=ck_buf[lane][ck_use[lane]].snd; p23_patch patch;
    for(j=0;j<NPL;j++) {
        active_lid[lane][j]=note_lid?note_lid[j]:PL_NONE;
        active_lv[lane][j]=note_lid?note_lv[j]:0;
        if(active_lid[lane][j]<PL_MNG && !pl_track(active_lid[lane][j]))
            par_put(&s,active_lid[lane][j]/8u,active_lid[lane][j]%8u,active_lv[lane][j]);
    }
    p23_from_frontend(&s,&patch,LANE_T(lane)); p23_set_patch(lane,&patch);
    p23_note(lane,source,note,note_vel,1); lfo_trig[lane]=1; lane_force[lane]=1;
    midi_note_out(LANE_T(lane),note,note_vel,trk_mute[LANE_T(lane)]);
}
static void note_off(uint32_t lane,uint32_t note,uint32_t source)
{ p23_note(lane,source,note,0,0);midi_note_out(LANE_T(lane),note,0,0); }
static int track_sounding(uint32_t lane) { return p23_inspect(lane).active; }
static int trk_sounding(uint32_t t)
{ uint32_t l;for(l=0;l<NLANE;l++)if(LANE_T(l)==t && track_sounding(l))return 1;return 0; }
enum { AR_MODE, AR_RATE, AR_RNG, AR_GATE, AR_LATCH, AR_DENS, AR_ORND, AR_BASS, NARP };
enum { AM_UP, AM_DOWN, AM_UPDN, AM_DNUP, AM_ORDER, AM_RND, AM_CONV, AM_DIV, AM_PINKY, AM_THUMB, NAM };
#define NAR_RATE 6u
static const uint8_t AR_R6[NAR_RATE] = {3, 4, 6, 8, 12, 24};   /* (1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4: sixths of a 16th) */
#define NAR_RNG 4u
#define NAR_BASS 8u                              /* (OFF, then every 2 .. 8 notes) */
#define ARP_N 12u
static volatile uint8_t arp_on[NTRACK], arp_p[NTRACK][NARP];
typedef struct {
    uint8_t n, note[ARP_N], vel[ARP_N];          /* the notes held, in the order played */
    uint8_t down;                                /* keys down now (LATCH: the notes kept with none) */
    uint8_t fresh;                               /* the first note of a new chord: at once */
    uint8_t snd;                                 /* the note sounding (0 none) .. */
    uint32_t left;                               /* .. its blocks left */
    uint32_t c, acc, since;                      /* notes in turn; stopped: its clock; blocks since a note */
    int32_t k;                                   /* playing: the grid's place it played last */
    uint8_t lane;                                /* the lane it plays (build 79: the track's subtrack its keys were on) */
} arp_t;
static arp_t arps[NTRACK];
static uint32_t arp_rnd = 0x9E3779B9u;
typedef struct { uint8_t t, note, vel, pos; uint16_t len8; uint32_t acc, q; } arp_rec_t;   /* (a note played, for the
                                                  * main loop to record: where the sequencer was) */
#define ARP_RQ 16u
static arp_rec_t arp_rq[ARP_RQ];
static volatile uint32_t arp_rw, arp_rr;
static const uint8_t AR_DEF[NARP] = {AM_UP, 2, 0, 64, 0, 127, 0, 0};   /* (UP, 1/16, 1 OCT, GATE 50, LATCH OFF,
                                                  * DENSITY 100, OCT RND 0, BASS OFF) */
static void arp_defaults(uint32_t t)
{
    uint32_t j;
    for (j = 0; j < NARP; j++)
        arp_p[t][j] = AR_DEF[j];
}
static void arp_quiet(uint32_t t)                /* (its note let go) */
{
    arp_t *a = &arps[t];
    if (a->snd)
        note_off(a->lane, a->snd, SRC_ARP);
    a->snd = 0;
    a->left = 0;
}
static void arp_key(uint32_t t, uint32_t note, uint32_t vel, int on)   /* (the audio's: an event) */
{
    arp_t *a = &arps[t];
    uint32_t i, j;
    if (!on) {
        if (a->down)
            a->down--;
        if (arp_p[t][AR_LATCH])
            return;
        for (i = 0; i < a->n && a->note[i] != note; i++)
            ;
        if (i < a->n) {
            for (j = i; j + 1u < a->n; j++) {
                a->note[j] = a->note[j + 1u];
                a->vel[j] = a->vel[j + 1u];
            }
            a->n--;
        }
        if (!a->n)
            arp_quiet(t);
        return;
    }
    if (!a->down && arp_p[t][AR_LATCH])          /* (LATCH: a new chord after all were let go, in place of the last) */
        a->n = 0;
    a->down++;
    for (i = 0; i < a->n && a->note[i] != note; i++)
        ;
    if (i < a->n || a->n >= ARP_N)
        return;
    if (!a->n) {
        a->fresh = 1;
        a->c = 0;
    }
    a->note[a->n] = (uint8_t)note;
    a->vel[a->n] = (uint8_t)(vel ? vel : 127u);
    a->n++;
}
static void arp_clear(uint32_t t)
{
    arps[t].n = arps[t].down = 0;
    arp_quiet(t);
    arps[t].lane = (uint8_t)t;                   /* (back on the track's own lane) */
}
/* (the main loop's: a session's) track t's arpeggiator on or off and its settings, each in its range; its notes let go */
static void arp_set(uint32_t t, uint32_t on, const uint8_t *p)
{
    static const uint8_t N[NARP] = {NAM, NAR_RATE, NAR_RNG, 128, 2, 128, 128, NAR_BASS};
    uint32_t j;
    arp_on[t] = (uint8_t)(on != 0);
    for (j = 0; j < NARP; j++)
        arp_p[t][j] = (uint8_t)(p[j] < N[j] ? p[j] : 0u);
    ev_post(EV_ARP_CLR, t, 0, 0);
}

static void trk_hush(uint32_t t)
{
    uint32_t lane;
    midi_track_off(t);
    for(lane=0;lane<NLANE;lane++) if(LANE_T(lane)==t) p23_hush(lane);
}
static void events_take(void)
{
    while (ev_r != ev_w) {
        ev_t e = evq[ev_r % EVQ];
        RING_PUBLISH();
        ev_r++;
        switch (e.type) {
        case EV_ON:
            note_vel = (uint8_t)(e.val && e.val < 127u ? e.val : 127u);
            note_on(e.track, e.note, SRC_KEY);
            note_vel = 127;
            break;
        case EV_OFF:
            note_off(e.track, e.note, SRC_KEY);
            break;
        case EV_ARP_ON:
        case EV_ARP_OFF: {                       /* (a lane's keys: its track's arpeggiator, playing on that lane) */
            uint32_t tr = LANE_T(e.track);
            if (e.type == EV_ARP_ON && arps[tr].lane != e.track) {
                arp_quiet(tr);
                arps[tr].lane = e.track;
            }
            arp_key(tr, e.note, e.val, e.type == EV_ARP_ON);
            break;
        }
        case EV_ARP_CLR:
            arp_clear(e.track);
            break;
        case EV_STOP:
            trk_hush(e.track);
            break;
        case EV_LR: {                            /* a looper sensor: held while any key holds it */
            uint8_t *h = &lr_held[e.track % NTRACK][e.note & 1u];
            if (e.val) { if (*h < 255u) (*h)++; }
            else if (*h) (*h)--;
            p23_lr_touch(e.track % NTRACK, e.note & 1u, *h != 0);
            break;
        }
        case EV_LR_BANK:
            p23_lr_bank(e.track, 0);
            break;
        case EV_LR_RUN:
            if (e.note) p23_lr_start(e.note - 1u); else p23_lr_stop();
            break;
        case EV_LR_QUANT:
            p23_lr_quantize(e.track);
            break;
        case EV_LR_CLEAR:
            p23_lr_clear(e.track);
            break;
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------ the sequencer --- */
/* 16 steps a track, a 16th note each at the tempo. A step holds its note (0: empty), its velocity, its length and its
 * trig condition (the Digitone II's: PRE, NEI, 1ST, a probability, A:B, each also negated but the probability). The
 * main loop writes the steps and the lengths (bytes: whole as written), the ISR plays them */
#define NSTEP 16u
#define SEQ_16TH (FS * 15u * 10u)         /* a 16th note, in samples x the tempo x 10 */
typedef char seq_midi16[SEQ_16TH == MIDI_16TH ? 1 : -1];   /* (midi.c's) */
#define VEL_DEF 100u
/* a step's length, in eighths of a step; the last, INF, held until the step plays again or the sequencer stops */
#define NGLEN 15u
#define GLEN_DEF 3u                       /* (half a step) */
#define GLEN_INF (NGLEN - 1u)
static const uint8_t GLEN_8TH[NGLEN] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 0};
static const char *const GLEN_NAME[NGLEN] = {"1/8", "1/4", "3/8", "1/2", "3/4", "1", "1.5", "2", "3", "4", "6", "8",
                                            "12", "16", "INF"};
/* the conditions: 0 none, then PRE, !PRE, NEI, !NEI, 1ST, !1ST, the probabilities, then A:B and !A:B in turn for
 * B 2 .. 8, A 1 .. B */
enum { CD_NONE, CD_PRE, CD_NPRE, CD_NEI, CD_NNEI, CD_1ST, CD_N1ST, CD_PROB };
#define NPROB 21u
#define CD_AB (CD_PROB + NPROB)
#define NCOND (CD_AB + 70u)
static const uint8_t PROB[NPROB] = {1, 2, 4, 6, 9, 13, 19, 25, 33, 41, 50, 59, 67, 75, 81, 87, 91, 94, 96, 98, 99};
static void cond_ab(uint32_t c, uint32_t *a, uint32_t *b)   /* (c an A:B condition: its A and B) */
{
    uint32_t i = (c - CD_AB) / 2u;
    for (*b = 2u; i >= *b; *b += 1u)
        i -= *b;
    *a = i + 1u;
}
static volatile uint8_t seq_note[NLANE][NSTEP];
static volatile uint8_t seq_vel[NLANE][NSTEP], seq_glen[NLANE][NSTEP], seq_cond[NLANE][NSTEP];
static volatile uint8_t seq_lid[NLANE][NSTEP][NPL];    /* the steps' locks: ids (PL_NONE: a free place) .. */
static volatile int16_t seq_lv[NLANE][NSTEP][NPL] POOL;     /* .. and values (written before the id: the ISR reads a
                                                         * lock whole) */
static volatile uint8_t seq_skip[NLANE][NSTEP];   /* a step not to play when it next comes (recorded just before
                                                    * it: the key played it already) */
static volatile uint32_t seq_acc_now;     /* (the ISR's: how far into the step playing, of SEQ_16TH) */
/* the metronome: a click on each beat (higher on the bar's first), while met_on; with the sequencer stopped it
 * keeps the tempo by itself */
static volatile uint8_t met_on;
static uint32_t met_acc, met_n, met_ph, met_inc;
static uint8_t met_was, met_step, seq_gstep;
#define MET_LEN 1100u                     /* ~25 ms */
static void met_tick(uint32_t step)
{ if(met_on && !(step&3u)) p23_click(step==0); }

static volatile uint8_t seq_len[NLANE];  /* (0: 16) */
/* each track's clock against the tempo's 16ths (TRACK page): 1/8, 1/4, 1/2, 1 (SEQ_DIV1), 2, 4 times as fast -- in
 * eighths of the tempo's (DIV_R8). Its own count (seq_tacc: a step is SEQ_STEP8), from PLAY; the metronome, the looper
 * and MANGLE's clock keep the tempo's */
#define NDIV 6u
#define SEQ_DIV1 3u
#define SEQ_STEP8 (SEQ_16TH * 8u)
static const uint8_t DIV_R8[NDIV] = {1, 2, 4, 8, 16, 32};
static const char *const DIV_NAME[NDIV] = {"1/8", "1/4", "1/2", "1X", "2X", "4X"};
static volatile uint8_t seq_div[NLANE] = {SEQ_DIV1, SEQ_DIV1, SEQ_DIV1, SEQ_DIV1, SEQ_DIV1, SEQ_DIV1, SEQ_DIV1,
                                         SEQ_DIV1, SEQ_DIV1};
static uint32_t seq_tacc[NLANE];
/* each track's direction through its steps (the step view's 4th .. 6th black keys) */
enum { DIR_FWD, DIR_REV, DIR_PING, DIR_RND, NDIR };
static volatile uint8_t seq_dir[NLANE];
static int8_t seq_ppd[NLANE];            /* (PING: the way it goes now) */
static uint8_t seq_rnd_n[NLANE];         /* (RANDOM: steps since the count of its times through moved) */
/* a step's ratchet: the step played RTG_N times, evenly through it, each a little softer (index 0: once) */
#define NRTG 6u
static const uint8_t RTG_N[NRTG] = {1, 2, 3, 4, 6, 8};
static const char *const RTG_NAME[NRTG] = {"OFF", "2", "3", "4", "6", "8"};
static volatile uint8_t seq_rtg[NLANE][NSTEP];
static uint8_t rt_left[NLANE], rt_note[NLANE], rt_vel[NLANE], rt_step[NLANE], rt_chf[NLANE], rt_n[NLANE];
static uint32_t rt_next[NLANE], rt_int[NLANE];
/* a step's MOD (the step view's 4th black key): MUTATE -- its note moved by so many steps of the scale (-12 .. 12;
 * MUT_RND: a step of the scale drawn at random, up to an octave either way) -- every (seq_mcy + 1) times it plays;
 * and a MIDI FX every (seq_fcy + 1) times it plays (after TONITOR's looper VARY and BRICKLAYER): echoes (rising,
 * falling), a stutter, a flam, late, a grace note before it, a fifth or a triad with it, an octave up or down, or
 * dropped. The times are the step's own: counted as it plays (its condition met), from PLAY */
#define MUT_RND 100
enum { MFX_OFF, MFX_ECHO, MFX_ECHOUP, MFX_ECHODN, MFX_STUT, MFX_FLAM, MFX_LATE, MFX_GRACE, MFX_FIFTH, MFX_TRIAD,
       MFX_OCTUP, MFX_OCTDN, MFX_DROP, NMFX };
static const char *const MFX_NAME[NMFX] = {"OFF", "ECHO", "ECHO UP", "ECHO DN", "STUTTER", "FLAM", "LATE", "GRACE",
                                           "FIFTH", "TRIAD", "OCT UP", "OCT DN", "DROP"};
static volatile int8_t seq_mut[NLANE][NSTEP];
static volatile uint8_t seq_mcy[NLANE][NSTEP], seq_mfx[NLANE][NSTEP], seq_fcy[NLANE][NSTEP];
static volatile uint8_t seq_chf[NLANE][NSTEP];   /* a step's chord: its form + 1 (0: the note alone; build 33) */
static uint8_t seq_plays[NLANE][NSTEP];         /* (the ISR's: the times each step has played since PLAY) */
static uint8_t seq_played[NLANE];               /* (the note a track's step played last, mutated: the tests') */
/* the scale, for MUTATE and the MIDI FX (the main loop's, as it is set): its notes from the root (bit k: k
 * semitones up), the root */
static volatile uint16_t seq_smask = 0xFFFu;
static volatile uint8_t seq_sroot;
static int sc_in(int32_t n) { return (int)((seq_smask >> ((uint32_t)(n - seq_sroot + 120) % 12u)) & 1u); }
static int32_t sc_move(int32_t n, int32_t d)     /* n moved d steps of the scale */
{
    int32_t u = d > 0 ? 1 : -1, m;
    for (; d; d -= u) {
        for (m = n + u; m >= 12 && m <= 120 && !sc_in(m); m += u)
            ;
        if (m < 12 || m > 120)
            break;
        n = m;
    }
    return n;
}
static int32_t sc_snap(int32_t n)                /* the note of the scale nearest n (below first) */
{
    int32_t d;
    for (d = 0; d < 6; d++) {
        if (sc_in(n - d))
            return n - d;
        if (sc_in(n + d))
            return n + d;
    }
    return n;
}
static int32_t sc_count(void)
{
    uint32_t m = seq_smask & 0xFFFu, c = 0;
    for (; m; m &= m - 1u)
        c++;
    return (int32_t)(c ? c : 12u);
}
/* the chord forms (CHORD mode, after TONITOR's): three notes as steps of the scale from the root, so in the scale
 * and of the quality it gives; in CHROMATIC, as semitones (a major triad, its inversions ..) */
#define CHF_N 7u
static const int8_t CH_STEP[CHF_N][3] = {{0, 2, 4}, {2, 4, 7}, {-3, 0, 2}, {0, 1, 4}, {0, 3, 4}, {0, 4, 9}, {0, 3, 6}};
static const int8_t CH_SEMI[CHF_N][3] = {{0, 4, 7}, {4, 7, 12}, {-5, 0, 4}, {0, 2, 7}, {0, 5, 7}, {0, 7, 16}, {0, 5, 10}};
static const char *const CH_NAME[CHF_N] = {"TRIAD", "INV 1", "INV 2", "SUS2", "SUS4", "OPEN", "FOURTHS"};
static const char *const CH_ABBR[CHF_N] = {"TRI", "IN1", "IN2", "SU2", "SU4", "OPN", "4TH"};
static void chord_of(int32_t root, uint32_t form, int32_t *n)   /* the chord's three notes */
{
    uint32_t i;
    form %= CHF_N;
    for (i = 0; i < 3u; i++)
        n[i] = clampi((seq_smask & 0xFFFu) == 0xFFFu ? root + CH_SEMI[form][i] : sc_move(root, CH_STEP[form][i]), 12,
                      120);
}
/* the MIDI FX's notes to come: a track's, each so many blocks from now (note 0: a free place) */
#define FXQ 8u
typedef struct { uint16_t left; uint8_t note, vel, len8, step; } fxq_t;
static fxq_t fxq[NLANE][FXQ];
static void fxq_add(uint32_t t, uint32_t blocks, int32_t note, int32_t vel, uint32_t len8, uint32_t step)
{
    uint32_t k;
    for (k = 0; k < FXQ && fxq[t][k].note; k++)
        ;
    if (k == FXQ || note < 12 || note > 120)
        return;
    fxq[t][k].left = (uint16_t)(blocks ? (blocks < 0xFFFFu ? blocks : 0xFFFFu) : 1u);
    fxq[t][k].vel = (uint8_t)clampi(vel, 1, 127);
    fxq[t][k].len8 = (uint8_t)clampi((int32_t)len8, 1, 255);
    fxq[t][k].step = (uint8_t)step;
    fxq[t][k].note = (uint8_t)note;
}

static volatile uint32_t seq_tacc_now[NLANE];   /* (as the block ended: where in its step the track is) */
static volatile uint8_t seq_run;          /* PLAY */
static volatile uint8_t seq_pos[NLANE];  /* the step playing (the ISR's; the screen and the LEDs read it) */
static uint32_t seq_acc, seq_rnd = 0x2545F491u;
static uint8_t seq_was;
static uint16_t seq_loop[NLANE];         /* the times the track has played through since PLAY */
static uint8_t seq_pre[NLANE];           /* the last condition evaluated on the track (but PRE): 0 none yet, 1 false,
                                           * 2 true */
#define SEQ_HOLD 6u                       /* notes a track's steps and loops keep sounding at once */
static uint8_t sq_note[NLANE][SEQ_HOLD];
static uint16_t sq_left[NLANE][SEQ_HOLD];   /* blocks left (0xFFFF: INF) */
static void seq_release(void)
{
    uint32_t t, i;
    memset(fxq, 0, sizeof fxq);
    for (t = 0; t < NLANE; t++)
        for (i = 0; i < SEQ_HOLD; i++)
            if (sq_note[t][i]) {
                note_off(t, sq_note[t][i], SRC_SEQ);
                sq_note[t][i] = 0;
            }
}
static int seq_cond_ok(uint32_t t, uint32_t c)
{
    int r;
    if (c == CD_NONE)
        return 1;
    if (c == CD_PRE || c == CD_NPRE)
        return (seq_pre[t] == 2u) == (c == CD_PRE);
    if (c == CD_NEI || c == CD_NNEI)
        r = (t > 0u && seq_pre[t - 1u] == 2u) == (c == CD_NEI);
    else if (c == CD_1ST || c == CD_N1ST)
        r = (seq_loop[t] == 0u) == (c == CD_1ST);
    else if (c < CD_AB) {
        seq_rnd = seq_rnd * 1664525u + 1013904223u;
        r = (seq_rnd >> 16) % 100u < PROB[c - CD_PROB];
    } else {
        uint32_t a, b;
        cond_ab(c, &a, &b);
        r = (seq_loop[t] % b == a - 1u) == !((c - CD_AB) & 1u);
    }
    seq_pre[t] = (uint8_t)(r ? 2u : 1u);
    return r;
}
/* a note of the sequencer's (a step's, a loop's) on track t, for len8 eighths of a step (0: until it plays again or
 * the sequencer stops), with the locks lk (0 none) */
static uint32_t seq_sounded[NLANE];             /* (the notes the sequencer has played a track: the tests count them) */
static void seq_sound(uint32_t t, uint32_t n, uint32_t vel, uint32_t len8, const volatile uint8_t *lid,
                      const volatile int16_t *lv)
{
    seq_sounded[t]++;
    uint32_t i, k = SEQ_HOLD;
    for (i = 0; i < SEQ_HOLD; i++)               /* (the same note sounding: let go first, its slot taken) */
        if (sq_note[t][i] == n || (!sq_note[t][i] && k == SEQ_HOLD))
            k = i;
    if (k == SEQ_HOLD)                           /* (all sounding: the one nearest its end) */
        for (k = 0, i = 1; i < SEQ_HOLD; i++)
            if (sq_left[t][i] < sq_left[t][k])
                k = i;
    if (sq_note[t][k])
        note_off(t, sq_note[t][k], SRC_SEQ);
    note_vel = (uint8_t)(vel ? vel : VEL_DEF);
    note_lid = lid;
    note_lv = lv;
    note_on(t, n, SRC_SEQ);
    note_lid = 0;
    note_vel = 127;
    sq_note[t][k] = (uint8_t)n;
    if (!len8)
        sq_left[t][k] = 0xFFFFu;
    else {   /* (blocks: eighths x the blocks of an eighth, x 16 for its fraction) */
        uint32_t bl = (len8 < 4096u ? len8 : 4096u) * (SEQ_16TH / 8u * 16u / (CTL * (uint32_t)clampi(fx_bpm10, 100, 3000))) / 16u;
        sq_left[t][k] = (uint16_t)(bl ? (bl < 0xFFFEu ? bl : 0xFFFEu) : 1u);
    }
}
/* a step's note, or its chord (f1: form + 1) */
static void seq_soundc(uint32_t t, uint32_t n, uint32_t vel, uint32_t len8, const volatile uint8_t *lid,
                       const volatile int16_t *lv, uint32_t f1)
{
    int32_t c[3];
    uint32_t i;
    if (!f1 || ck_buf[t][ck_use[t]].mach == ENG_BD) {   /* (DRUM: the root alone) */
        seq_sound(t, n, vel, len8, lid, lv);
        return;
    }
    chord_of((int32_t)n, f1 - 1u, c);
    for (i = 0; i < 3u; i++)
        seq_sound(t, (uint32_t)c[i], vel, len8, lid, lv);
}
/* the step track t plays next, by its direction (and its times through counted, for A:B and 1ST) */
static uint32_t seq_next(uint32_t t, uint32_t len)
{
    uint32_t pos = seq_pos[t], p;
    int first = pos == 0xFFu;
    if (!first && pos >= len)
        pos = len - 1u;                          /* (the length made shorter under it) */
    switch (seq_dir[t]) {
    case DIR_REV:
        p = first || pos == 0u ? len - 1u : pos - 1u;
        if (!first && pos == 0u)
            seq_loop[t]++;
        break;
    case DIR_PING:                               /* (0 .. len - 1 .. 0 .., the ends once each) */
        if (first || len < 2u) {
            seq_ppd[t] = 1;
            p = 0;
            break;
        }
        if (!seq_ppd[t])                         /* (PING chosen while playing, never started in it: on up, build 71 --
                                                  * at 0 it stood on its step for good) */
            seq_ppd[t] = 1;
        if (seq_ppd[t] > 0 && pos + 1u >= len)
            seq_ppd[t] = -1;
        else if (seq_ppd[t] < 0 && pos == 0u) {
            seq_ppd[t] = 1;
            seq_loop[t]++;
        }
        p = (uint32_t)((int32_t)pos + seq_ppd[t]);
        break;
    case DIR_RND:
        seq_rnd = seq_rnd * 1664525u + 1013904223u;
        p = (seq_rnd >> 16) % len;
        if (!first && ++seq_rnd_n[t] >= len) {
            seq_rnd_n[t] = 0;
            seq_loop[t]++;
        }
        break;
    default:
        p = first ? 0u : pos + 1u;
        if (p >= len) {
            p = 0;
            seq_loop[t]++;
        }
        break;
    }
    return p < len ? p : 0u;
}
/* BREAK (build 37; after MOTOR:CYCLES' brk.c): while the 3rd black key is held, the tracks in brk_mask play other
 * steps than the ones they are on -- a beat repeated, dragged to half time, rushed to double, its steps played
 * backwards, the bar's beats shuffled, or a roll -- with ratchets that speed up the longer it is held, flams, octave
 * leaps and a gap before the bar's end. Nothing is written: each track's own position goes on underneath, so let go,
 * it carries on where it would have been. The loops are not broken up: they play on as recorded (from build 45, as
 * before 41).
 * It stays on the pattern's beat (build 45): a track goes into the break on its next beat (its next half bar for
 * REPEAT 8), playing as written until then; every way keeps what is on a beat on a beat -- windows of 1, 2, 4 or 8
 * steps, REVERSE keeping each window's first step, CHOP moving whole beats. One plan a press (build 53: until then
 * another every two bars held), never the same way twice running. The kick keeps the bar's first step and takes no leaps, no
 * gap and few ratchets; a drone is left alone */
/* Quasar adds ways after breakcore and Autechre -- SHUFFLE (the bar's 16 steps in another order), POLY (each track
 * round its own odd window: 3, 5, 6, 7 or 9 steps against the bar), JUMP (each beat a leap to another), SKIP (the
 * steps read 3, 5, 7, 9 or 11 at a time through the bar) -- and layers: RISE (each hit climbing in pitch through the
 * half bar), WRECK (a hit's sound folded, crushed, its bits dropped or rung), XFM (TRANSFORM thrown in for a beat
 * or so, as its page sets it, over the tracks under the break), GHOST (soft hits on empty steps). No gap: the music never drops out. Held, the plan is drawn again about
 * every other bar, so a long break keeps turning */
enum { BR_REPEAT, BR_HALF, BR_DOUBLE, BR_REVERSE, BR_CHOP, BR_ROLL, BR_SHUFFLE, BR_POLY, BR_JUMP, BR_SKIP, NBR };
static const char *const BR_NAME[NBR] = {"REPEAT", "HALF TIME", "DOUBLE", "REVERSE", "CHOP", "ROLL", "SHUFFLE", "POLY",
                                         "JUMP", "SKIP"};
enum { BL_RAT = 1, BL_OCT = 2, BL_FLAM = 4, BL_RISE = 8, BL_ACC = 16, BL_WRECK = 32, BL_XFM = 64, BL_GHOST = 128 };
#define PL_WRECK (4u*8u)                         /* (page 4: params.c's PG_WRECK -- FOLD, RING, RING FRQ, CRUSH, BITS) */
static volatile uint8_t brk_mask;                /* (the main loop's: the tracks under the break; 0 none) */
static volatile uint16_t brk_kick, brk_drum, brk_keep;   /* (.. the kicks, the drums, the drones: as the press found
                                                  * them; a bit a lane) */
static volatile uint32_t brk_seed;               /* (.. a new one each press) */
static volatile uint8_t brk_shown = 0xFF;        /* (the ISR's: the way playing, BR_*, for the screen; w after) */
static volatile uint8_t brk_shown_w;
static uint32_t brk_seen, brk_r, brk_n16, brk_since;
static uint8_t brk_rec, brk_prev = 0xFF, brk_w, brk_chop[4], brk_lay;
static uint16_t brk_fresh;                       /* (a bit a lane) */
static uint8_t brk_anchor[NLANE];
static uint8_t brk_rt[NLANE], brk_fl[NLANE];   /* (a step's: its ratchet, flam, velocity %) */
static uint8_t brk_vp[NLANE] = {100, 100, 100, 100, 100, 100, 100, 100, 100};
static int8_t brk_oct[NLANE];
static uint8_t brk_perm[16], brk_pw[NLANE], brk_jump[NLANE], brk_mul, brk_semi[NLANE], brk_wk[NLANE], brk_gh[NLANE];
static uint8_t brk_lid[NLANE][NPL];              /* (a wrecked hit's locks: its step's, and the wreck's) */
static int16_t brk_lv[NLANE][NPL];
static volatile uint8_t brk_xf_left;             /* (XFM: the 16ths the break's TRANSFORM has still to hold) */
static p23_transform brk_xf;
static uint8_t brk_last;                         /* (the tracks under it as the last block ended) */
static uint32_t brk_rnd(uint32_t n)
{
    brk_r = brk_r * 1664525u + 1013904223u;
    return n ? (brk_r >> 16) % n : 0u;
}
static void brk_draw(void)                       /* a plan: the way, its window, its beats' order, its layers */
{
    static const uint8_t RW[6] = {1, 2, 2, 4, 4, 8};   /* (each a whole part of the bar: on the beat, always) */
    uint32_t i;
    do
        brk_rec = (uint8_t)brk_rnd(NBR);
    while (brk_rec == brk_prev);
    brk_prev = brk_rec;
    brk_w = brk_rec == BR_REPEAT ? RW[brk_rnd(6)] : brk_rec == BR_REVERSE ? (brk_rnd(2) ? 8u : 4u) : 4u;
    for (i = 0; i < 4u; i++)                     /* CHOP: the bar's four beats, the first kept, the others swapped */
        brk_chop[i] = (uint8_t)i;
    for (i = 1; i < 4u; i++)
        if (brk_rnd(100) < 60u) {
            uint32_t j = 1u + brk_rnd(3);
            uint8_t x = brk_chop[i];
            brk_chop[i] = brk_chop[j];
            brk_chop[j] = x;
        }
    for (i = 0; i < 16u; i++)                   /* SHUFFLE: the bar's steps in another order */
        brk_perm[i] = (uint8_t)i;
    for (i = 15u; i > 0u; i--) {
        uint32_t j = brk_rnd(i + 1u);
        uint8_t x = brk_perm[i];
        brk_perm[i] = brk_perm[j];
        brk_perm[j] = x;
    }
    for (i = 0; i < NLANE; i++) {                /* POLY: each track its own window */
        static const uint8_t PW[5] = {3, 5, 6, 7, 9};
        brk_pw[i] = PW[brk_rnd(5)];
        brk_jump[i] = 0;
    }
    {   static const uint8_t MUL[5] = {3, 5, 7, 9, 11};
        brk_mul = MUL[brk_rnd(5)]; }
    for (brk_lay = 0; !brk_lay;)
        for (i = 0; i < 8u; i++)
            if (brk_rnd(100) < 35u)
                brk_lay |= (uint8_t)(1u << i);
    if (brk_rec == BR_ROLL)
        brk_lay |= BL_RAT;
    brk_fresh = (uint16_t)((1u << NLANE) - 1u);
    brk_since = 0;
    brk_shown_w = brk_w;
    brk_shown = brk_rec;
}
static volatile uint32_t seq_gcount;             /* (the looper's, below: the tempo's 16ths since PLAY) */
static void brk_tick(void)                       /* (each of the tempo's 16ths, playing) */
{
    if (!brk_mask) {
        brk_shown = 0xFF;
        brk_xf_left = 0;
        return;
    }
    if (brk_seed != brk_seen) {                  /* pressed: a plan */
        brk_seen = brk_seed;
        brk_r = brk_seed | 1u;
        brk_n16 = 0;
        brk_draw();
        return;
    }
    brk_n16++;
    brk_since++;
    if (!(brk_n16 % 16u) && brk_rnd(100) < 45u)  /* (held on: another plan about every other bar) */
        brk_draw();
    if (brk_xf_left)                             /* XFM: the break's TRANSFORM held a 16th more, or let go */
        brk_xf_left--;
    else if ((brk_lay & BL_XFM) && !(brk_n16 % 4u) && brk_rnd(100) < 35u) {
        xf_draw(brk_mask, (brk_rnd(60000u) + 1u) * 2654435761u + brk_n16, &brk_xf);
        brk_xf_left = (uint8_t)(2u + brk_rnd(4));
    }
}
/* the way's step for step p of len, from anchor a (a beat) */
static uint32_t brk_map(uint32_t t, uint32_t a, uint32_t p, uint32_t len, uint32_t kick)
{
    uint32_t rel, w, src = p;
    a = a < len ? a : 0u;
    rel = (p + len - a) % len;
    w = brk_w < len ? brk_w : len;
    switch (brk_rec) {
    case BR_REPEAT:
        src = (a + rel % w) % len;
        break;
    case BR_HALF:
        src = (a + rel / 2u) % len;
        break;
    case BR_DOUBLE:
        src = (a + 2u * rel) % len;
        break;
    case BR_REVERSE:                             /* (each window backwards after its first step: 0 3 2 1) */
        src = p - p % w + (w - p % w) % w;
        break;
    case BR_CHOP:
        src = len >= 16u ? p - p % 16u + brk_chop[(p % 16u) / 4u] * 4u + p % 4u : p;
        break;
    case BR_SHUFFLE:
        src = len >= 16u ? p - p % 16u + brk_perm[p % 16u] : (a + brk_perm[rel % 16u]) % len;
        break;
    case BR_POLY:
        src = (a + rel % (brk_pw[t] < len ? brk_pw[t] : len)) % len;
        break;
    case BR_JUMP:                                /* (each beat: a leap to another beat) */
        if (!(rel % 4u))
            brk_jump[t] = (uint8_t)(brk_rnd((len + 3u) / 4u) * 4u);
        src = (brk_jump[t] + rel % 4u) % len;
        break;
    case BR_SKIP:
        src = (a + rel * brk_mul) % len;
        break;
    default:
        break;
    }
    if (src >= len || (kick && p % 16u == 0u))   /* (the kick: the bar's first step its own) */
        src = p;
    return src;
}
/* the step track t plays for its step p (of len), and its layers for that hit (0xFF: none plays) */
static uint32_t brk_step(uint32_t t, uint32_t p, uint32_t len)
{
    uint32_t src, kick = brk_kick >> t & 1u, drum = brk_drum >> t & 1u, ro, rate;
    if (brk_fresh >> t & 1u) {                   /* (into the break on the track's next beat -- its next half bar for
                                                  * REPEAT 8: as written until then) */
        if (p % (brk_w > 4u ? 8u : 4u))
            return p;
        brk_anchor[t] = (uint8_t)p;
        brk_fresh &= (uint16_t)~(1u << t);
    }
    src = brk_map(t, brk_anchor[t], p, len, kick);
    if ((brk_lay & BL_RISE) && !kick)            /* RISE: climbing through each half bar */
        brk_semi[t] = (uint8_t)((p % 8u) * 2u);
    if ((brk_lay & BL_WRECK) && brk_rnd(100) < (kick ? 15u : 40u))
        brk_wk[t] = 1;
    if ((brk_lay & BL_GHOST) && !kick && !seq_note[t][src] && brk_rnd(100) < 35u)
        brk_gh[t] = 1;
    ro = brk_rec == BR_ROLL ? (kick ? 10u : drum ? 70u : 35u) : (kick ? 5u : drum ? 35u : 15u);
    if ((brk_lay & BL_RAT) && !seq_rtg[t][src] && brk_rnd(100) < ro) {   /* (faster the longer it is held) */
        rate = 2u + (brk_rec == BR_ROLL) + brk_n16 / 16u + brk_rnd(3);
        brk_rt[t] = (uint8_t)(rate < 8u ? rate : 8u);
    }
    if ((brk_lay & BL_OCT) && !kick && !drum && brk_rnd(100) < 30u)
        brk_oct[t] = (int8_t)(brk_rnd(2) ? 12 : -12);
    if ((brk_lay & BL_FLAM) && !kick && drum && brk_rnd(100) < 25u)
        brk_fl[t] = 1;
    if (brk_lay & BL_ACC)                        /* (accents: on the beat full, between softer) */
        brk_vp[t] = (uint8_t)(p % 4u == 0u ? 100u : 55u + brk_rnd(40));
    return src;
}
static void seq_trig(uint32_t t, uint32_t p)
{
    uint32_t n = seq_note[t][p], g = seq_glen[t][p], i;
    if (seq_skip[t][p]) {
        seq_skip[t][p] = 0;
        return;
    }
    if (!n && brk_gh[t] && seq_played[t])       /* (BREAK's GHOST: a soft hit of the last note on an empty step) */
        n = seq_played[t];
    if (!n || !seq_cond_ok(t, seq_cond[t][p]))
        return;
    {
        uint32_t r8 = DIV_R8[seq_div[t] % NDIV], rn = brk_rt[t] ? brk_rt[t] : RTG_N[seq_rtg[t][p] % NRTG];
        uint32_t c = ++seq_plays[t][p], fx = MFX_OFF;
        uint32_t len8 = g >= GLEN_INF ? 0u : (uint32_t)maxi(1, GLEN_8TH[g] * 8 / (int32_t)r8);   /* (in the tempo's
                                                                                              * 16ths) */
        uint32_t sb = SEQ_STEP8 / (CTL * (uint32_t)clampi(fx_bpm10, 100, 3000) * r8);   /* (the step, in blocks) */
        uint32_t vel = (seq_vel[t][p] ? seq_vel[t][p] : VEL_DEF) * brk_vp[t] / 100u, k, l8 = len8 ? len8 : 64u / r8;
        int32_t m = seq_mut[t][p];
        if (m && c % (seq_mcy[t][p] + 1u) == 0u) {   /* MUTATE */
            int32_t d = m;
            if (m == MUT_RND) {
                int32_t nn = sc_count();
                seq_rnd = seq_rnd * 1664525u + 1013904223u;
                d = (int32_t)((seq_rnd >> 16) % (uint32_t)(2 * nn + 1)) - nn;
            }
            n = (uint32_t)sc_move((int32_t)n, d);
        }
        if (seq_mfx[t][p] && c % (seq_fcy[t][p] + 1u) == 0u)
            fx = seq_mfx[t][p] % NMFX;
        if (brk_oct[t] && (int32_t)n + brk_oct[t] >= 24 && (int32_t)n + brk_oct[t] <= 108)   /* (BREAK's leap) */
            n = (uint32_t)((int32_t)n + brk_oct[t]);
        if (brk_fl[t] && fx == MFX_OFF)          /* (BREAK's flam) */
            fx = MFX_FLAM;
        if (brk_semi[t] && n + brk_semi[t] <= 108u)   /* (BREAK's RISE) */
            n += brk_semi[t];
        if (brk_gh[t] && !seq_note[t][p])
            vel = vel * (35u + brk_rnd(35)) / 100u;
        if (brk_wk[t]) {                         /* (BREAK's WRECK: the step's locks, and one or two of its own) */
            uint32_t w, slot = 0, kinds = 1u + brk_rnd(2);
            for (k = 0; k < NPL; k++) { brk_lid[t][k] = seq_lid[t][p][k]; brk_lv[t][k] = seq_lv[t][p][k]; }
            for (w = 0; w < kinds; w++) {
                static const uint8_t J[4] = {0, 1, 3, 4};   /* (FOLD, RING, CRUSH, BITS) */
                uint32_t j = J[brk_rnd(4)];
                while (slot < NPL && brk_lid[t][slot] != PL_NONE)
                    slot++;
                if (slot >= NPL)
                    break;
                brk_lid[t][slot] = (uint8_t)(PL_WRECK + j);
                brk_lv[t][slot] = (int16_t)(50 + brk_rnd(78));
                if (j == 1u && slot + 1u < NPL && brk_lid[t][slot + 1u] == PL_NONE) {
                    brk_lid[t][slot + 1u] = (uint8_t)(PL_WRECK + 2u);
                    brk_lv[t][slot + 1u] = (int16_t)(20 + brk_rnd(100));
                }
            }
        }
        for (k = 0; k < NPL; k++) {              /* (the track's locks: until its next step plays) */
            uint32_t id = seq_lid[t][p][k];
            trk[t].tv[k] = seq_lv[t][p][k];
            trk[t].tid[k] = (uint8_t)(id != PL_NONE && pl_track(id) ? id : PL_NONE);
        }
        seq_played[t] = (uint8_t)n;
        if (fx == MFX_DROP)
            return;
        if (fx == MFX_LATE || fx == MFX_GRACE) { /* the note a little late (a grace note a step below before it) */
            if (fx == MFX_GRACE)
                seq_sound(t, (uint32_t)sc_move((int32_t)n, -1), vel * 3u / 5u, 2u, seq_lid[t][p], seq_lv[t][p]);
            fxq_add(t, fx == MFX_LATE ? sb / 4u : sb / 6u, (int32_t)n, (int32_t)vel, l8, p);
            return;
        }
        if (rn > 1u) {                           /* a ratchet: the hits after the first, as the step goes on */
            uint32_t hit8 = (uint32_t)maxi(1, (int32_t)(64u / r8 / (2u * rn)));
            if (len8 == 0u || len8 > hit8)
                len8 = hit8;
            rt_left[t] = (uint8_t)(rn - 1u);
            rt_int[t] = SEQ_STEP8 / rn;
            rt_next[t] = seq_tacc[t] + rt_int[t];
            rt_note[t] = (uint8_t)n;
            rt_vel[t] = (uint8_t)maxi(1, (int32_t)vel);
            rt_step[t] = (uint8_t)p;
            rt_n[t] = (uint8_t)rn;
            rt_chf[t] = seq_chf[t][p];
        }
        seq_soundc(t, n, vel, len8, brk_wk[t] ? brk_lid[t] : seq_lid[t][p], brk_wk[t] ? brk_lv[t] : seq_lv[t][p],
                   seq_chf[t][p]);
        switch (fx) {                            /* the MIDI FX's notes with it, and to come */
        case MFX_ECHO:
        case MFX_ECHOUP:
        case MFX_ECHODN: {
            int32_t e = (int32_t)n, d = fx == MFX_ECHOUP ? 1 : fx == MFX_ECHODN ? -1 : 0;
            for (k = 1; k <= 2u; k++) {
                e = sc_move(e, d);
                fxq_add(t, sb * k / 2u, e, (int32_t)(vel * (k == 1u ? 3u : 9u) / (k == 1u ? 5u : 25u)), l8, p);
            }
            break;
        }
        case MFX_STUT:
            for (k = 1; k <= 3u; k++)
                fxq_add(t, sb * k / 8u, (int32_t)n, (int32_t)(vel * (8u - k) / 8u), 1u, p);
            break;
        case MFX_FLAM:
            fxq_add(t, 16u, (int32_t)n, (int32_t)(vel * 3u / 4u), l8, p);   /* (~12 ms) */
            break;
        case MFX_FIFTH:
            seq_sound(t, (uint32_t)sc_snap((int32_t)n + 7), vel * 4u / 5u, len8, seq_lid[t][p], seq_lv[t][p]);
            break;
        case MFX_TRIAD:
            seq_sound(t, (uint32_t)sc_move((int32_t)n, 2), vel * 4u / 5u, len8, seq_lid[t][p], seq_lv[t][p]);
            seq_sound(t, (uint32_t)sc_snap((int32_t)n + 7), vel * 4u / 5u, len8, seq_lid[t][p], seq_lv[t][p]);
            break;
        case MFX_OCTUP:
        case MFX_OCTDN:
            if ((fx == MFX_OCTUP && n <= 108u) || (fx == MFX_OCTDN && n >= 24u))
                seq_sound(t, fx == MFX_OCTUP ? n + 12u : n - 12u, vel * 4u / 5u, len8, seq_lid[t][p], seq_lv[t][p]);
            break;
        default:
            break;
        }
    }
    (void)i;
}
static void fxq_block(void)                      /* (each block, playing: the MIDI FX's notes come due) */
{
    uint32_t t, k;
    for (t = 0; t < NLANE; t++)
        for (k = 0; k < FXQ; k++) {
            fxq_t *q = &fxq[t][k];
            if (q->note && !--q->left) {
                seq_soundc(t, q->note, q->vel, q->len8, seq_lid[t][q->step], seq_lv[t][q->step], seq_chf[t][q->step]);
                q->note = 0;
            }
        }
}

/* ---------------------------------------------------------------- the looper --- */
/* up to three loops a track in each pattern (build 73: a pattern's own -- before, the session's, playing in every
 * pattern), each its own length in steps (up to 32 bars; build 37), played over the track's steps in time with them:
 * notes at steps (recorded quantized: ui.c), each with its velocity and its length in eighths of a step. A loop
 * being recorded is not played (on 0) until it is ended; then it plays from its first step on the step it ended at
 * (anchor, in seq_gcount's count).
 * Every pattern's loops are in one table, NLP of them, each marked with its pattern, its track and its place (0 ..
 * 2); their notes in one pool, each loop's room (cap) one after another from the pool's start, the room free after
 * them. The sequencer plays the loops of seq_lpat, the pattern it plays: a pattern coming in, its loops from their
 * first step with its first step (the ones it left silent). Only the main loop changes the table and the pool;
 * a loop's room moved (another emptied, or its own made smaller), with the interrupts off */
#define NLOOP 3u                 /* (loops a track, in a pattern) */
#define NLP 40u                  /* (loops in all, every pattern's) */
#define LOOP_EV 256u             /* (notes a loop) */
#define LP_POOL 4608u            /* (notes in all: as the 18 loops of 256 before build 73) */
#define LOOP_MAX 512u            /* (steps: 32 bars) */
#define LP_FREE 0xFFu
typedef struct { uint16_t step, len8; uint8_t note, vel; } lev_t;
/* (build 79) a loop's note: the subtrack it was played on in len8's top two bits (0, ST-A: every loop before) */
#define LEV_LEN(e) ((uint32_t)(e).len8 & 0x3FFFu)
#define LEV_SUB(e) ((uint32_t)(e).len8 >> 14)
#define LEV_PACK(len, s) ((uint16_t)(((len) > 0x3FFFu ? 0x3FFFu : (len)) | (uint32_t)(s) << 14))
/* the lane of track t's subtrack s (s 0: the track's own); NLANE: none taken */
static uint32_t lane_find(uint32_t t, uint32_t s)
{
    uint32_t l;
    if (!s)
        return t;
    for (l = NTRACK; l < NLANE; l++)
        if (ln_s[l] == s && ln_t[l] == t)
            return l;
    return NLANE;
}
typedef struct {
    volatile uint8_t on, mute;
    volatile uint8_t kick;       /* ended after its first step had gone by: that step's notes played at once */
    volatile uint8_t pat;        /* its pattern (LP_FREE: the place unused) */
    uint8_t t, l;                /* its track, its place there */
    int8_t ref;                  /* the transposition its notes are kept at (below) */
    uint16_t len, n;
    volatile uint32_t anchor;
    uint16_t cur;                /* (the ISR's: the next note) */
    uint16_t cap;                /* its room in the pool, notes */
    lev_t *ev;
} loop_t;
static loop_t lps[NLP];
static lev_t lp_pool[LP_POOL];
static volatile uint8_t seq_lpat;                /* the pattern whose loops play (the sequencer's, as a pattern comes
                                                  * in; the main loop's, stopped) */
static volatile uint8_t seq_pend_lp;             /* (the pattern waiting: its loops, as it comes in) */
static volatile uint32_t seq_gcount;      /* the steps played since PLAY (the first: 0) */
/* (build 65) a track a pattern change unmuted: each of its loops silent until it comes round to its first step, then
 * on in its place (a bit a loop; set at the switch -- the ISR's, playing; the main loop's, stopped -- cleared here) */
static volatile uint8_t loop_wait[NTRACK];
/* (build 68) a loop's notes kept as recorded, at the transposition ref (the track's TRN in its pattern as it was
 * recorded, or as the session was saved); played moved by the track's transposition playing, lp_trn, less it (lp_trn:
 * set by the main loop, and by the sequencer as a pattern comes in) */
static volatile int8_t lp_trn[NTRACK];
static uint32_t lp_note(const loop_t *p, uint32_t n)
{
    int32_t d = lp_trn[p->t] - p->ref;
    return d ? (uint32_t)sc_move((int32_t)n, d) : n;
}
/* pattern k's loop l on track t (0: none) */
static loop_t *lp_at(uint32_t k, uint32_t t, uint32_t l)
{
    uint32_t i;
    for (i = 0; i < NLP; i++)
        if (lps[i].pat == k && lps[i].t == t && lps[i].l == l)
            return &lps[i];
    return 0;
}
static loop_t *lp_get(uint32_t t, uint32_t l) { return lp_at(seq_lpat, t, l); }   /* (the pattern playing's) */
static uint32_t lp_top(void)                     /* the pool's room in use */
{
    uint32_t i, n = 0;
    for (i = 0; i < NLP; i++)
        if (lps[i].pat != LP_FREE)
            n += lps[i].cap;
    return n;
}
/* loop p's room made cap notes (smaller: its notes past it lost), the rooms after it moved -- the interrupts off,
 * so that the sequencer never reads a loop's notes half moved */
static void lp_resize(loop_t *p, uint32_t cap)
{
    lev_t *end = p->ev + p->cap;
    uint32_t top = lp_top(), i;
    int32_t d = (int32_t)cap - (int32_t)p->cap;
    if (!d)
        return;
    fm1_irq_off();
    memmove(end + d, end, (uint32_t)(lp_pool + top - end) * sizeof(lev_t));
    for (i = 0; i < NLP; i++)
        if (lps[i].pat != LP_FREE && lps[i].ev >= end)
            lps[i].ev += d;
    p->cap = (uint16_t)cap;
    if (p->n > cap)
        p->n = (uint16_t)cap;
    fm1_irq_on();
}
/* a new loop: pattern k's l on track t, room for cap notes (the most the pool has, if less), empty and not on;
 * 0 if the table or the pool is full (or the place taken) */
static loop_t *lp_new(uint32_t k, uint32_t t, uint32_t l, uint32_t cap)
{
    uint32_t i, top = lp_top();
    loop_t *p = 0;
    if (lp_at(k, t, l) || top >= LP_POOL)
        return 0;
    for (i = 0; i < NLP && !p; i++)
        if (lps[i].pat == LP_FREE)
            p = &lps[i];
    if (!p)
        return 0;
    p->on = 0;
    p->mute = 0;
    p->kick = 0;
    p->t = (uint8_t)t;
    p->l = (uint8_t)l;
    p->ref = 0;
    p->len = p->n = p->cur = 0;
    p->anchor = 0;
    p->ev = lp_pool + top;
    p->cap = (uint16_t)(cap < LP_POOL - top ? cap : LP_POOL - top);
    RING_PUBLISH();
    p->pat = (uint8_t)k;
    return p;
}
static void lp_free(loop_t *p)                   /* a loop gone, its room given back */
{
    if (!p || p->pat == LP_FREE)
        return;
    p->on = 0;
    RING_PUBLISH();
    lp_resize(p, 0);
    p->pat = LP_FREE;
}
static void lp_init(void)
{
    uint32_t i;
    for (i = 0; i < NLP; i++)
        lps[i].pat = LP_FREE;
}
static void loop_wait_set(uint32_t t)
{
    uint32_t i, k = seq_lpat, m = 0;
    for (i = 0; i < NLP; i++)
        if (lps[i].pat == k && lps[i].t == t && lps[i].on && lps[i].len)
            m |= 1u << lps[i].l;
    loop_wait[t] = (uint8_t)m;
}
static void loop_kick(void)                      /* (each block: a loop's first step, late) */
{
    uint32_t i, k = seq_lpat;
    for (i = 0; i < NLP; i++) {
        loop_t *p = &lps[i];
        uint32_t t = p->t;
        if (p->pat != k || !p->on || !p->kick)
            continue;
        loop_wait[t] &= (uint8_t)~(1u << p->l);  /* (its first step: no more waiting) */
        for (p->cur = 0; p->cur < p->n && p->ev[p->cur].step == 0u; p->cur++) {
            uint32_t l = lane_find(t, LEV_SUB(p->ev[p->cur]));   /* (the subtrack it was played on; gone: silent) */
            if (!p->mute && l < NLANE && ln_on[l])
                seq_sound(l, lp_note(p, p->ev[p->cur].note), p->ev[p->cur].vel, LEV_LEN(p->ev[p->cur]), 0, 0);
        }
        p->kick = 0;
    }
}
static void loop_step(void)
{
    uint32_t i, k = seq_lpat;
    for (i = 0; i < NLP; i++) {
        loop_t *p = &lps[i];
        uint32_t t = p->t, ph;
        if (p->pat != k || !p->on || !p->len || (int32_t)(seq_gcount - p->anchor) < 0 || p->kick)
            continue;
        ph = (seq_gcount - p->anchor) % p->len;
        if (!ph) {
            p->cur = 0;
            loop_wait[t] &= (uint8_t)~(1u << p->l);   /* (its first step: on, in its place) */
        }
        while (p->cur < p->n && p->ev[p->cur].step < ph)
            p->cur++;
        for (; p->cur < p->n && p->ev[p->cur].step == ph; p->cur++) {
            uint32_t l = lane_find(t, LEV_SUB(p->ev[p->cur]));
            if (!p->mute && !(loop_wait[t] >> p->l & 1u) && l < NLANE && ln_on[l])
                seq_sound(l, lp_note(p, p->ev[p->cur].note), p->ev[p->cur].vel, LEV_LEN(p->ev[p->cur]), 0, 0);
        }
    }
}
/* a pattern's steps whole, as the sequencer plays them (projects.c fills one, the ISR takes it in) */
typedef struct {
    uint8_t note[NLANE][NSTEP], vel[NLANE][NSTEP], glen[NLANE][NSTEP], cond[NLANE][NSTEP], rtg[NLANE][NSTEP];
    int8_t mut[NLANE][NSTEP];
    uint8_t mcy[NLANE][NSTEP], mfx[NLANE][NSTEP], fcy[NLANE][NSTEP];
    uint8_t chf[NLANE][NSTEP];
    uint8_t lid[NLANE][NSTEP][NPL];
    int16_t lv[NLANE][NSTEP][NPL];
    uint8_t len[NLANE], div[NLANE], dir[NLANE];
    uint8_t den[NLANE], cx[NLANE];             /* (the phrase generator's, the main loop's: not the ISR's) */
    int8_t trn[NLANE];                          /* (each track's transposition, steps of the scale: the main loop's) */
} seqbuf_t;
static void seq_copy_out(seqbuf_t *b)            /* the steps playing, into b */
{
    memcpy(b->note, (const void *)seq_note, sizeof b->note);
    memcpy(b->vel, (const void *)seq_vel, sizeof b->vel);
    memcpy(b->glen, (const void *)seq_glen, sizeof b->glen);
    memcpy(b->cond, (const void *)seq_cond, sizeof b->cond);
    memcpy(b->rtg, (const void *)seq_rtg, sizeof b->rtg);
    memcpy(b->mut, (const void *)seq_mut, sizeof b->mut);
    memcpy(b->mcy, (const void *)seq_mcy, sizeof b->mcy);
    memcpy(b->mfx, (const void *)seq_mfx, sizeof b->mfx);
    memcpy(b->fcy, (const void *)seq_fcy, sizeof b->fcy);
    memcpy(b->chf, (const void *)seq_chf, sizeof b->chf);
    memcpy(b->lid, (const void *)seq_lid, sizeof b->lid);
    memcpy(b->lv, (const void *)seq_lv, sizeof b->lv);
    memcpy(b->len, (const void *)seq_len, sizeof b->len);
    memcpy(b->div, (const void *)seq_div, sizeof b->div);
    memcpy(b->dir, (const void *)seq_dir, sizeof b->dir);
}
static void seq_copy_in(const seqbuf_t *b)       /* b, as the steps playing */
{
    memcpy((void *)seq_note, b->note, sizeof b->note);
    memcpy((void *)seq_vel, b->vel, sizeof b->vel);
    memcpy((void *)seq_glen, b->glen, sizeof b->glen);
    memcpy((void *)seq_cond, b->cond, sizeof b->cond);
    memcpy((void *)seq_rtg, b->rtg, sizeof b->rtg);
    memcpy((void *)seq_mut, b->mut, sizeof b->mut);
    memcpy((void *)seq_mcy, b->mcy, sizeof b->mcy);
    memcpy((void *)seq_mfx, b->mfx, sizeof b->mfx);
    memcpy((void *)seq_fcy, b->fcy, sizeof b->fcy);
    memcpy((void *)seq_chf, b->chf, sizeof b->chf);
    memcpy((void *)seq_lid, b->lid, sizeof b->lid);
    memcpy((void *)seq_lv, b->lv, sizeof b->lv);
    memcpy((void *)seq_len, b->len, sizeof b->len);
    memcpy((void *)seq_div, b->div, sizeof b->div);
    memcpy((void *)seq_dir, b->dir, sizeof b->dir);
}
/* a pattern waiting (projects.c's pat_switch, the sequencer playing): sb_next taken in as track seq_pend_t ends its
 * time through -- the steps it leaves into sb_old, every track from its start -- then seq_pend 2 (the main loop
 * finishes it: pat_poll) */
static seqbuf_t sb_next, sb_old;
static volatile uint8_t sb_lock;                 /* (the main loop's scratch in sb_old: no pattern taken in meanwhile;
                                                  * projects.c's sb_scratch) */
static volatile uint8_t seq_pend, seq_pend_t;
static volatile uint8_t seq_pend_mu;             /* (build 65: the pattern waiting's mutes, a bit a track, bit 7 set: it has
                                                  * them -- applied as it is taken in) */
static volatile uint8_t seq_mu_was;              /* (.. and the mutes it found, bit 7 set: for the main loop) */
static volatile uint8_t seq_mq, seq_mq_t;        /* (build 78, MUTE QT: MUTE mode's mutes waiting, a bit a track, bit 7
                                                  * set: waiting -- applied as track seq_mq_t begins its time through) */
static int seq_wraps(uint32_t t)                 /* track t's next step begins its time through again */
{
    uint32_t len = seq_len[t] ? seq_len[t] : NSTEP, pos = seq_pos[t];
    if (pos == 0xFFu || pos >= len)
        return 1;
    switch (seq_dir[t]) {
    case DIR_REV: return pos == 0u;
    case DIR_PING: return len < 2u || (seq_ppd[t] < 0 && pos == 1u);
    case DIR_RND: return seq_rnd_n[t] + 1u >= len;
    default: return pos + 1u >= len;
    }
}
/* the arpeggiator's next note: the held notes over RANGE octaves, in MODE's order; BASS: every so many, the lowest an
 * octave down; OCT RND: an octave up or down at random (0: none) */
static uint32_t arp_rand(void)
{
    arp_rnd = arp_rnd * 1664525u + 1013904223u;
    return arp_rnd >> 8;
}
static uint32_t arp_pick(uint32_t t)
{
    arp_t *a = &arps[t];
    const volatile uint8_t *p = arp_p[t];
    uint8_t s[ARP_N];
    uint32_t n = a->n, rng = p[AR_RNG] % NAR_RNG + 1u, nn = n * rng, c = a->c, i, j, m, b = p[AR_BASS] % NAR_BASS;
    int32_t note;
    for (i = 0; i < n; i++) {                    /* (the notes by pitch; ORDER: as played) */
        uint8_t x = a->note[i];
        for (j = i; p[AR_MODE] != AM_ORDER && j > 0u && s[j - 1u] > x; j--)
            s[j] = s[j - 1u];
        s[j] = x;
    }
    m = c % nn;
    switch (p[AR_MODE] % NAM) {
    case AM_DOWN: i = nn - 1u - m; break;
    case AM_UPDN: m = nn < 2u ? 0u : c % (2u * nn - 2u); i = m < nn ? m : 2u * nn - 2u - m; break;
    case AM_DNUP: m = nn < 2u ? 0u : c % (2u * nn - 2u); i = nn - 1u - (m < nn ? m : 2u * nn - 2u - m); break;
    case AM_RND: i = arp_rand() % nn; break;
    case AM_CONV: i = m & 1u ? nn - 1u - m / 2u : m / 2u; break;   /* (the ends, in to the middle) */
    case AM_DIV: m = nn - 1u - m; i = m & 1u ? nn - 1u - m / 2u : m / 2u; break;   /* (the middle, out) */
    case AM_PINKY: m = nn < 2u ? 0u : c % (2u * nn - 2u); i = m & 1u ? nn - 1u : m / 2u; break;   /* (the top between) */
    case AM_THUMB: m = nn < 2u ? 0u : c % (2u * nn - 2u); i = m & 1u ? 1u + m / 2u : 0u; break;   /* (the bottom between) */
    default: i = m; break;                       /* UP, ORDER */
    }
    note = (int32_t)s[i % n] + 12 * (int32_t)(i / n);
    if (b && c % (b + 1u) == 0u)
        note = (int32_t)s[0] - 12 >= 12 ? (int32_t)s[0] - 12 : (int32_t)s[0];
    else if (p[AR_ORND] && arp_rand() % 127u < p[AR_ORND])
        note += arp_rand() & 1u ? 12 : -12;
    while (note > 120)
        note -= 12;
    while (note < 12)
        note += 12;
    return (uint32_t)note;
}
/* a note of the arpeggiator now (DENSITY: or a rest), its length GATE's share of the rate's; for the main loop too */
static void arp_fire(uint32_t t, uint32_t l, uint32_t adv)
{
    arp_t *a = &arps[t];
    const volatile uint8_t *p = arp_p[t];
    uint32_t note, vel = 127, g = (uint32_t)p[AR_GATE] + 1u, i;
    a->since = 0;
    if (p[AR_DENS] < 127u && arp_rand() % 127u >= p[AR_DENS]) {
        a->c++;
        return;
    }
    note = arp_pick(t);
    for (i = 0; i < a->n; i++)                   /* (its velocity: the key's it came from) */
        if (a->note[i] % 12u == note % 12u) {
            vel = a->vel[i];
            break;
        }
    a->c++;
    arp_quiet(t);
    note_vel = (uint8_t)vel;
    note_on(a->lane, note, SRC_ARP);
    note_vel = 127;
    a->snd = (uint8_t)note;
    a->left = adv ? (l >> 7) * g / adv : 1u;
    if (!a->left)
        a->left = 1;
    if (arp_rw - arp_rr < ARP_RQ) {
        arp_rec_t *r = &arp_rq[arp_rw % ARP_RQ];
        uint32_t e = AR_R6[p[AR_RATE] % NAR_RATE] * g / 96u;   /* (eighths of a 16th: rate x 8 / 6, x the gate / 128) */
        r->t = a->lane;                          /* (a lane: the track's subtrack) */
        r->note = (uint8_t)note;
        r->vel = (uint8_t)vel;
        r->len8 = (uint16_t)(e ? e : 1u);
        r->pos = seq_pos[a->lane];
        r->acc = seq_tacc[a->lane];
        r->q = seq_gcount == 0xFFFFFFFFu ? 0u : seq_gcount + (seq_acc >= SEQ_16TH / 2u);
        RING_PUBLISH();
        arp_rw++;
    }
}
/* each block: each track's arpeggiator on (notes held), on the sequencer's grid while it plays (its place in four
 * 16ths, at the rate's), else on its own clock; a new chord plays at once (playing: if no later than a quarter of
 * the rate's past its place, else from the next) */
static void arp_block(uint32_t adv, int run)
{
    uint32_t t;
    for (t = 0; t < NTRACK; t++) {
        arp_t *a = &arps[t];
        uint32_t l = SEQ_16TH / 6u * AR_R6[arp_p[t][AR_RATE] % NAR_RATE];
        if (a->snd && a->left && !--a->left)
            arp_quiet(t);
        if (a->since < 0xFFFFu)
            a->since++;
        if (!arp_on[t] || !a->n)
            continue;
        if (run) {
            uint32_t ph = (seq_gcount == 0xFFFFFFFFu ? 0u : seq_gcount % 4u) * SEQ_16TH + (seq_acc < SEQ_16TH ? seq_acc : 0u);
            int32_t k = (int32_t)(ph / l);
            if (a->fresh) {
                a->fresh = 0;
                a->k = k;
                if (ph % l <= l / 4u)
                    arp_fire(t, l, adv);
            } else if (k != a->k) {
                a->k = k;
                arp_fire(t, l, adv);
            }
            a->acc = 0;
        } else {
            if (a->fresh) {
                a->fresh = 0;
                a->acc = 0;
                arp_fire(t, l, adv);
            } else if (a->acc >= l) {
                a->acc -= l;
                arp_fire(t, l, adv);
            }
            a->acc += adv;
        }
    }
}
static void seq_block(void)
{
    uint32_t t, i, adv, xm = 0;
    midi_main_drain();                           /* (the main loop's MIDI out: CCs, program changes) */
    if (xk_stop) {                               /* MIDI STOP, START, CONTINUE (midi.c) */
        xk_stop = 0;
        seq_run = 0;
    }
    if (xk_start) {
        xm = xk_start;
        xk_start = 0;
        if (seq_run)                             /* (playing: from the start again) */
            seq_was = 0;
        seq_run = 1;
    }
    adv = midi_adv(seq_run && seq_was);          /* this block's way on: the tempo's, or the clock followed's */
    if (!seq_run) {
        midi_clock_out(adv);
        if (seq_was) {
            midi_stopped();
            seq_gcount = 0xFFFFFFFFu;            /* (stopped: not started -- else the looper, armed and its first
                                                  * note playing, measured from the last run's count and ended at
                                                  * once after 32 bars of it; build 44) */
            seq_release();
            for (t = 0; t < NLANE; t++)
                memset(trk[t].tid, PL_NONE, sizeof trk[t].tid);
        }
        seq_was = 0;
        if (met_on && !met_was) {                /* (the metronome alone: a bar's first beat now) */
            met_acc = SEQ_16TH;
            met_step = 15;
        }
        met_was = met_on;
        arp_block(adv, 0);
        if (met_on) {
            if (met_acc >= SEQ_16TH) {
                met_acc -= SEQ_16TH;
                met_step = (uint8_t)((met_step + 1u) & 15u);
                met_tick(met_step);
            }
            met_acc += adv;
        }
        return;
    }
    met_was = 0;
    if (!seq_was) {                              /* started: step 1 now */
        seq_was = 1;
        seq_acc = SEQ_16TH;
        for (t = 0; t < NLANE; t++) {
            seq_pos[t] = 0xFF;
            seq_loop[t] = 0;
            seq_pre[t] = 0;
            seq_tacc[t] = SEQ_STEP8;
            seq_rnd_n[t] = 0;
            rt_left[t] = 0;
        }
        memset(seq_plays, 0, sizeof seq_plays);
        for (t = 0; t < NTRACK; t++)             /* (the arpeggiators on the grid from step 1: a note just played --
                                                  * the one that started it, REC or the looper armed -- is step 1's) */
            arps[t].k = arps[t].since < 64u && arps[t].since * adv <= SEQ_16TH / 24u * AR_R6[arp_p[t][AR_RATE] % NAR_RATE]
                        ? 0 : -1;                /* (a note no more than a quarter of the rate's ago) */
        seq_gstep = 15;
        seq_gcount = 0xFFFFFFFFu;
        for (i = 0; i < NLP; i++)                /* (the loops from their first steps) */
            lps[i].anchor = 0;
        if (xm == 2u && xk_spp) {                /* MIDI CONTINUE: from the song position, in 16ths -- each track
                                                  * where it would be by then on its clock, the loops too */
            uint32_t spp = xk_spp;
            seq_gcount = spp - 1u;
            seq_gstep = (uint8_t)((spp - 1u) & 15u);
            for (t = 0; t < NLANE; t++) {
                uint32_t len = seq_len[t] ? seq_len[t] : NSTEP, p8 = spp * DIV_R8[seq_div[t] % NDIV], st = p8 / 8u;
                seq_loop[t] = (uint16_t)(st / len);
                if (p8 % 8u) {                   /* (inside a step: that one, played already; the next to come) */
                    seq_pos[t] = (uint8_t)(st % len);
                    seq_tacc[t] = (p8 % 8u) * SEQ_16TH;
                } else
                    seq_pos[t] = (uint8_t)(st ? (st - 1u) % len : 0xFFu);
            }
            midi_started(spp);
        } else
            midi_started(0);
        adv = midi_adv(1);
    }
    midi_clock_out(adv);
    for (t = 0; t < NLANE; t++)                  /* the lengths run out */
        for (i = 0; i < SEQ_HOLD; i++)
            if (sq_note[t][i] && sq_left[t][i] != 0xFFFFu && !--sq_left[t][i]) {
                note_off(t, sq_note[t][i], SRC_SEQ);
                sq_note[t][i] = 0;
            }
    /* the pattern waiting, in -- before the tempo's 16th that comes with it, so that its loops play that step as the
     * pattern has them (build 68) */
    if (seq_pend == 1u && !sb_lock && seq_tacc[seq_pend_t] >= SEQ_STEP8 && seq_wraps(seq_pend_t)) {
        seq_copy_out(&sb_old);
        seq_copy_in(&sb_next);
        for (t = 0; t < NLANE; t++) {
            seq_pos[t] = 0xFF;
            seq_loop[t] = 0;
            seq_pre[t] = 0;
            seq_rnd_n[t] = 0;
            rt_left[t] = 0;
            seq_tacc[t] = SEQ_STEP8;
        }
        memset(seq_plays, 0, sizeof seq_plays);
        for (t = 0; t < NTRACK; t++) {           /* (its transpositions: its loops' from now) */
            lp_trn[t] = sb_next.trn[t];
            loop_wait[t] = 0;
        }
        {   /* (build 73) its loops, from their first step with its first step: the tempo's 16th to come (this one
             * gone by, the track's clock a little behind: now, played at once) -- the loops of the one left silent */
            uint32_t k = seq_pend_lp, now = seq_acc < SEQ_16TH;
            for (i = 0; i < NLP; i++)
                if (lps[i].pat == k) {
                    lps[i].cur = 0;
                    lps[i].anchor = now ? seq_gcount : seq_gcount + 1u;
                    lps[i].kick = (uint8_t)(now && lps[i].on);
                }
            RING_PUBLISH();
            seq_lpat = (uint8_t)k;
        }
        if (seq_pend_mu & 0x80u) {               /* (build 65) its mutes, now: a track it unmutes, its notes from the
                                                  * muted time let go */
            uint32_t was = 0x80u;
            for (t = 0; t < NTRACK; t++) {
                uint32_t mu = seq_pend_mu >> t & 1u;
                was |= (uint32_t)(trk_mute[t] != 0) << t;
                if (trk_mute[t] && !mu)
                    trk_hush(t);
                trk_mute[t] = (uint8_t)mu;
            }
            seq_mu_was = (uint8_t)was;
        }
        RING_PUBLISH();
        seq_pend = 2;
    }
    if ((seq_mq & 0x80u) && seq_tacc[seq_mq_t] >= SEQ_STEP8 && seq_wraps(seq_mq_t)) {   /* (MUTE QT: the mutes waiting,
                                                  * now, with the track's first step -- a track unmuted, its notes from
                                                  * the muted time let go) */
        for (t = 0; t < NTRACK; t++) {
            uint32_t mu = seq_mq >> t & 1u;
            if (trk_mute[t] && !mu)
                trk_hush(t);
            trk_mute[t] = (uint8_t)mu;
        }
        RING_PUBLISH();
        seq_mq = 0;
    }
    if (seq_acc >= SEQ_16TH) {
        seq_acc -= SEQ_16TH;
        seq_gstep = (uint8_t)((seq_gstep + 1u) & 15u);
        seq_gcount++;
        met_tick(seq_gstep);
        brk_tick();
        loop_step();
    }
    {   /* BREAK let go: its ratchets and echoes still to come dropped at once, so the pattern is back as the key
         * comes up (the notes sounding ring on) */
        uint8_t m = brk_mask, gone = (uint8_t)(brk_last & ~m);
        for (t = 0; gone && t < NLANE; t++)
            if (gone >> LANE_T(t) & 1u) {
                rt_left[t] = 0;
                memset(fxq[t], 0, sizeof fxq[t]);
            }
        brk_last = m;
    }
    for (t = 0; t < NLANE; t++) {                /* each lane on its own clock (a lane that does not play: in time,
                                                  * silent) */
        if (seq_tacc[t] >= SEQ_STEP8) {
            uint32_t len = seq_len[t] ? seq_len[t] : NSTEP, p = seq_next(t, len), q = p;
            seq_tacc[t] -= SEQ_STEP8;
            rt_left[t] = 0;                      /* (a ratchet not done by the next step: cut) */
            seq_pos[t] = (uint8_t)p;
            brk_vp[t] = 100;
            if ((brk_mask >> LANE_T(t) & 1u) && !(brk_keep >> t & 1u) && brk_shown != 0xFFu)
                q = brk_step(t, p, len);         /* (BREAK: another step, its own position kept) */
            if (q != 0xFFu && ln_on[t])
                seq_trig(t, q);
            brk_rt[t] = brk_fl[t] = 0;
            brk_oct[t] = 0;
            brk_semi[t] = brk_wk[t] = brk_gh[t] = 0;
        } else if (rt_left[t] && seq_tacc[t] >= rt_next[t]) {   /* the ratchet's next hit */
            uint32_t r8 = DIV_R8[seq_div[t] % NDIV], rn = rt_n[t];
            rt_vel[t] = (uint8_t)maxi(1, rt_vel[t] - rt_vel[t] / 8);
            seq_soundc(t, rt_note[t], rt_vel[t], (uint32_t)maxi(1, (int32_t)(64u / r8 / (2u * (rn > 1u ? rn : 2u)))),
                       seq_lid[t][rt_step[t]], seq_lv[t][rt_step[t]], rt_chf[t]);
            rt_next[t] += rt_int[t];
            rt_left[t]--;
        }
        seq_tacc[t] += adv * DIV_R8[seq_div[t] % NDIV];
        seq_tacc_now[t] = seq_tacc[t];
    }
    loop_kick();
    fxq_block();
    arp_block(adv, 1);
    seq_acc += adv;
    seq_acc_now = seq_acc;
}

/* ---------------------------------------------------------------- LFOs --- */
/* Three LFOs a track (build 50), the Digitone II's (its MOD pages; sound.h lfo_t): each runs in the track's sound,
 * its output (a wave, -1 .. 1, EXP and RAMP 0 .. 1) times its FADE and its DEPTH (+-63: the destination's whole
 * range) moving one parameter of the sound -- or LFO 1's or LFO 2's SPEED or DEPTH, for LFO 2 and 3 (computed first,
 * LFO 3 to 1). The rate is the Digitone II's law: a cycle every 2048 / (|SPEED| x MULT) 16ths, at the tempo (MULT
 * 0 .. 11: x1 .. x2048) or at 120 BPM (12 .. 23); a SPEED below 0 runs it backwards. MODE: FREE runs on; TRIG starts
 * it again at its START PHASE at each note on the track; HOLD runs on and holds its value at each note; ONE and HALF
 * start at a note, run one cycle (half), and stay. FADE: from each note, over |FADE| 16ths, in (below 64) or out
 * (above). RND: a value drawn at each cycle, START PHASE its slew.
 * They are the track's, not each voice's (as the Digitone II's POLY M.LFO): one copy of the track's cooked sound
 * moves, and its voices play it. That copy -- and each voice with step locks on such a track -- is cooked anew in
 * turn, one a block (~15 us at most: 2 % of a block, whatever is modulated); a track whose sound was just taken anew
 * first, the plain sound played until its copy is ready */
#define LFO_K 664862u                            /* (2^32 x CTL / (FS x 307200), x 65536: a block's phase, the rate's
                                                  * BPM x 10 x |SPEED| x MULT apart) */
typedef char lfo_k_ok[CTL == 32 && FS == 44100 ? 1 : -1];
typedef struct {
    uint32_t ph[NLFO], run[NLFO], go[NLFO];      /* the phase; how far a ONE or HALF has run; blocks since a note */
    int32_t live[NLFO], held[NLFO], tgt[NLFO];   /* the wave now; HOLD's value; RND's next */
    uint8_t stop[NLFO];
    uint32_t cooked;                             /* lfo_flips[t] + 1 when its copy was cooked (0: none) */
} lfo_st_t;
static lfo_st_t lfs[NLANE];
static ck_t ck_lfo[NLANE];                 /* (each track's cooked sound, as its LFOs have it now) */
static int32_t lfo_mod[NLANE][NLFO];            /* (each LFO's share on its sound's destination, Q15; 0 none) */
static uint8_t lfo_any[NLANE];                  /* (an LFO of the track has a destination and a depth) */
static uint32_t lfo_rr, lfo_rnd = 0x2545F491u;
static void lfo_off(lfo_t *l)
{
    l->spd = 80;
    l->mult = 2;
    l->fade = 64;
    l->dest = 0;
    l->wave = LW_TRI;
    l->sph = 0;
    l->mode = LM_FREE;
    l->dep = 64;
}
static void lfos_off(sound_t *s)
{
    uint32_t n;
    for (n = 0; n < NLFO; n++)
        lfo_off(&s->lfo[n]);
}
static int lfo_on(const lfo_t *l) { return l->dest && l->dest < LFO_ND && l->dep != 64; }
static int32_t lfo_wave(uint32_t w, uint32_t ph)  /* Q15 */
{
    uint32_t x;
    switch (w) {
    case LW_SINE:
        return sin_ph(ph);
    case LW_SQR:
        return ph < 0x80000000u ? 32767 : -32768;
    case LW_SAW:                                 /* (+1 down to -1) */
        return 32767 - (int32_t)(ph >> 16);
    case LW_EXP:                                 /* (1 down to 0, (1 - x)^3) */
        x = 32767u - (ph >> 17);
        return (int32_t)((x * x >> 15) * x >> 15);
    case LW_RAMP:                                /* (0 up to 1) */
        return (int32_t)(ph >> 17);
    default:                                     /* TRI: from 0 up, as SINE */
        x = (ph + 0x40000000u) >> 17;
        return x < 16384u ? (int32_t)(x * 4u) - 32768 : (x == 16384u ? 32767 : 98304 - (int32_t)(x * 4u));
    }
}
#define LFO_EVERY 4u                             /* (the LFOs' work every 4 blocks, 2.9 ms: the steps on one, a cook on
                                                  * another -- a cook in the audio takes ~3x what it does warm) */
static void lfo_step(uint32_t t)                 /* (LFO_EVERY blocks on: each LFO on, its share) */
{
    const sound_t *s = &ck_buf[t][ck_use[t]].snd;
    lfo_st_t *f = &lfs[t];
    int32_t sm[NLFO] = {0, 0, 0}, dm[NLFO] = {0, 0, 0}, n;
    int trig = lfo_trig[t];
    uint32_t any = 0;
    lfo_trig[t] = 0;
    for (n = NLFO - 1; n >= 0; n--) {            /* (LFO 3 first: it may move LFO 1's or 2's SPEED or DEPTH) */
        const lfo_t *l = &s->lfo[n];
        int32_t spd = (int32_t)l->spd - 64 + sm[n], dep = clampi((int32_t)l->dep - 64 + dm[n], -64, 64), o, fq = Q15;
        uint32_t mult = 1u << (l->mult % 12u), bpm10 = l->mult < 12u ? (uint32_t)clampi(fx_bpm10, 100, 3000) : 1200u, inc, p0;
        lfo_mod[t][n] = 0;
        if (!lfo_on(l) && !(sm[n] || dm[n]))
            continue;
        spd = clampi(spd, -64, 64);
        {
            uint64_t i64 = (((uint64_t)bpm10 * (uint32_t)absi(spd) * mult * LFO_K) >> 16) * LFO_EVERY;
            inc = i64 > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)i64;   /* (faster than a cycle in two: as good as) */
        }
        if (trig) {
            f->go[n] = 0;
            if (l->mode == LM_HOLD)
                f->held[n] = f->live[n];
            else if (l->mode != LM_FREE) {
                f->ph[n] = (uint32_t)l->sph << 25;
                f->run[n] = 0;
                f->stop[n] = 0;
            }
        }
        p0 = f->ph[n];
        if (l->mode != LM_ONE && l->mode != LM_HALF)
            f->stop[n] = 0;                      /* (stopped by ONE or HALF, then another MODE: on again) */
        if (!f->stop[n]) {
            f->ph[n] += spd >= 0 ? inc : (uint32_t)-inc;
            if ((l->mode == LM_ONE && f->run[n] + inc < f->run[n]) ||
                (l->mode == LM_HALF && f->run[n] < 0x80000000u && f->run[n] + inc >= 0x80000000u))
                f->stop[n] = 1;                  /* (ONE: a cycle run; HALF: half) */
            f->run[n] += inc;
        }
        if (l->wave == LW_RND) {                 /* (a new value each cycle; START PHASE its slew) */
            if ((spd >= 0 ? f->ph[n] < p0 : f->ph[n] > p0) || trig) {
                lfo_rnd = lfo_rnd * 1664525u + 1013904223u;
                f->tgt[n] = (int32_t)(lfo_rnd >> 16) - 32768;
            }
            f->live[n] += l->sph >= 48u ? (f->tgt[n] - f->live[n]) >> (l->sph / 16u - 1u) : f->tgt[n] - f->live[n];
        } else
            f->live[n] = lfo_wave(l->wave, f->ph[n]);
        o = l->mode == LM_HOLD ? f->held[n] : f->live[n];
        if (l->fade != 64) {                     /* (from the last note, over |FADE| 16ths) */
            uint32_t len = (uint32_t)absi((int32_t)l->fade - 64) * (SEQ_16TH / CTL / LFO_EVERY) / bpm10 + 1u;
            uint32_t g = f->go[n] < len ? f->go[n] : len;
            fq = (int32_t)(g * (uint32_t)Q15 / len);
            if (l->fade > 64)
                fq = Q15 - fq;
            if (f->go[n] < 0xFFFFFFF0u)
                f->go[n]++;
        }
        o = (o * fq >> 15) * dep / 64;           /* (32767 x 32767 < 2^31: no 64 bits) */
        if (l->dest >= LFO_D_LFO) {              /* (LFO 1's or 2's SPEED or DEPTH, for a later LFO) */
            uint32_t m = (l->dest - LFO_D_LFO) / 2u;
            if ((int32_t)m < n) {
                if ((l->dest - LFO_D_LFO) & 1u)
                    dm[m] += o * 64 / 32768;
                else
                    sm[m] += o * 64 / 32768;
            }
        } else if (l->dest && l->dest < LFO_D_LFO) {   /* (on, or its DEPTH moved by a later LFO) */
            lfo_mod[t][n] = o;
            any = 1;
        }
    }
    lfo_any[t] = (uint8_t)any;
}
static int lfo_live(uint32_t t) { return lfo_any[t] && lfs[t].cooked == lfo_flips[t] + 1u; }
/* one sound cooked anew a block: a track whose own copy is old first, else the next in turn of the tracks with LFOs
 * on and their voices with step locks */
#if TELEMETRY
static uint32_t tl_lfo[4];                       /* (a TELEMETRY build's: the steps' and the cook's ticks, mean x 16 and
                                                  * highest) */
#endif
static void lfo_cook(void);
static void lfo_block(void)
{
    static uint8_t ph;
#if TELEMETRY
    uint32_t t0 = fm1_ticks(), t1, t2, t;
    ph = (uint8_t)((ph + 1u) % LFO_EVERY);
    for (t = 0; ph == 0u && t < NLANE; t++)
        lfo_step(t);
    t1 = fm1_ticks();
    if (ph == LFO_EVERY / 2u)
        lfo_cook();
    t2 = fm1_ticks();
    tl_lfo[0] += (t1 - t0) - (tl_lfo[0] >> 4);
    tl_lfo[1] = t1 - t0 > tl_lfo[1] ? t1 - t0 : tl_lfo[1];
    tl_lfo[2] += (t2 - t1) - (tl_lfo[2] >> 4);
    tl_lfo[3] = t2 - t1 > tl_lfo[3] ? t2 - t1 : tl_lfo[3];
#else
    uint32_t t;
    ph = (uint8_t)((ph + 1u) % LFO_EVERY);
    for (t = 0; ph == 0u && t < NLANE; t++)
        lfo_step(t);
    if (ph == LFO_EVERY / 2u)
        lfo_cook();
#endif
}
static void lfo_cook(void)
{
    uint32_t i;
    for(i=0;i<NLANE;i++) {
        uint32_t lane=(lfo_rr+i)%NLANE;
        if(!lfo_any[lane]) continue;
        ck_lfo[lane]=ck_buf[lane][ck_use[lane]];
        lfo_apply(&ck_lfo[lane].snd,lfo_mod[lane]);
        lfs[lane].cooked=lfo_flips[lane]+1u;
        lfo_rr=lane+1u;
        break;
    }
}
static volatile uint8_t tl_core;   /* (telemetry: 1 CPU0 alone, 2 a waiting loop that spares the shared RAM) */
/* CPU1's job: the engine's second part (src/audio_worker.h) */
static void part1_job(void *context) { (void)context; FAR(p23_process_part)(1u); }
/* The bridge publishes a whole node patch at block boundaries. All audio
 * processing, including the fader, pan, sends and master, belongs to the engine. */
static void mix_block(int32_t *out)
{
    uint32_t lane,j;
    audio_worker_check();
    events_take();
    for(lane=0;lane<NLANE;lane++) if(ck_use[lane]!=ck_pub[lane]) {
        ck_use[lane]=ck_pub[lane]; lfo_flips[lane]++;
    }
    seq_block(); lfo_block();
    {   /* the Pulsar's INT clock runs with the transport, its dividers and looper from the loop start */
        static uint8_t was_run;
        if(seq_run && !was_run) p23_clock_reset();
        was_run=seq_run; pnl_run=seq_run;
    }
    p23_global_from_frontend();
    if(brk_xf_left && !xf_ui) p23_set_transform(&brk_xf);   /* (a break's own TRANSFORM, while it holds) */
    p23_transform_hold(xf_ui || brk_xf_left);
    for(lane=0;lane<NLANE;lane++) {
        /* the lane's patch again only when what makes it changed: a sound published, a note (its locks), the
         * main view's locks, an LFO moving it, or every 32 blocks (23 ms) for anything else */
        static uint32_t lane_sig[NLANE]; static uint8_t lane_fx[NLANE]; static int32_t lane_vol[NLANE]={32768,32768,32768,32768,32768,32768,32768,32768,32768};
        p23_route route;
        int32_t pan=trk_pan[lane];
        uint32_t t=LANE_T(lane),sig=lfo_flips[lane]*2654435761u;
        for(j=0;j<NPL;j++) {
            sig=(sig^trk[lane].tid[j]^(uint32_t)trk[lane].tv[j]<<8)*16777619u;
            if(trk[lane].tid[j]==PL_PAN) pan=(trk[lane].tv[j]-64)*2;
        }
        if(lane_force[lane] || lfo_any[lane] || sig!=lane_sig[lane] || !((blk_clock+lane)&31u)) {
            sound_t s=ck_buf[lane][ck_use[lane]].snd;
            p23_patch patch;
            lane_force[lane]=0; lane_sig[lane]=sig;
            for(j=0;j<NPL;j++) if(active_lid[lane][j]<PL_MNG && !pl_track(active_lid[lane][j]))
                par_put(&s,active_lid[lane][j]/8u,active_lid[lane][j]%8u,active_lv[lane][j]);
            if(lfo_any[lane]) lfo_apply(&s,lfo_mod[lane]);
            for(j=0;j<NPL;j++)   /* the main view's locks: the FX send, REL and ATT */
                if(trk[lane].tid[j]==PL_DEL || trk[lane].tid[j]==PL_REV || trk[lane].tid[j]==PL_CHR)
                    par_put(&s,7u,trk[lane].tid[j]%8u,trk[lane].tv[j]);   /* (page 7: params.c's PG_FX) */
            p23_from_frontend(&s,&patch,t); p23_set_patch(lane,&patch);
            lane_fx[lane]=s.fx[X_DEL];
            {   /* the sound's VOL: 108 unity, square law (127 about +3 dB, 54 -12 dB) */
                int32_t v=clampi(s.amp[A_VOL],0,127);
                lane_vol[lane]=v*v*32768/(108*108);
            }
        }
        memset(&route,0,sizeof route);
        route.enabled=ln_on[lane];route.track=(uint8_t)t;route.muted=trk_mute[t];
        route.level=(uint32_t)clampi((int32_t)(((int64_t)trk_gain[lane]*lane_vol[lane])>>15),0,262144);
        route.pan=(int16_t)clampi(pan,-127,127);
        route.fx_send=lane_fx[lane];
        route.duck_source=sc_ln[t];
        route.duck_amount=sc_lvl[t];
        p23_set_route(lane,&route);
    }
    p23_set_master((uint16_t)clampi(master_q12,0,4096));
    {   /* the engine's block in parts: CPU1 renders part 1's voices while CPU0 renders part 0's; alone (CPU1 held
         * or retired) CPU0 renders both. A job lost to a CPU1 fault is one block of its voices' silence */
        uint32_t parts=FAR(p23_process_begin)();
        int sent=(parts&2u) && !(tl_core&1u) && audio_worker_submit(part1_job,0);
        FAR(p23_process_part)(0u);
        if(sent && (tl_core&2u)) while(!audio_worker_done()) { uint32_t t0=fm1_ticks(); while(fm1_ticks()-t0<FM1_TICKS_PER_US) {} }
        if(!sent) FAR(p23_process_part)(1u);
        else if(!audio_worker_join()) FAR(p23_process_drop)(1u);
        FAR(p23_process_end)(out);
    }
#if defined(FELUCCA_UAC) && FELUCCA_UAC
    p23_copy_usb(usb_blk);
#endif
    for(lane=0;lane<NLANE;lane++) trk[lane].meter=p23_inspect(lane).peak;
    blk_clock++;
}
static void tracks_init(void)
{
    uint32_t t;
    p23_reset(0x237231u);lp_init();fx_init();memset(lr_held,0,sizeof lr_held);
    memset(trk,0,sizeof trk);memset(active_lid,PL_NONE,sizeof active_lid);
    memset((void *)seq_lid,PL_NONE,sizeof seq_lid);memset(arps,0,sizeof arps);
    memset((void *)arp_on,0,sizeof arp_on);memset(lfs,0,sizeof lfs);
    memset(lfo_any,0,sizeof lfo_any);memset(lfo_mod,0,sizeof lfo_mod);
    memset((void *)lfo_trig,0,sizeof lfo_trig);memset(lfo_flips,0,sizeof lfo_flips);
    ev_w=ev_r=0;blk_clock=0;
    for(t=0;t<NTRACK;t++) { arp_defaults(t);arps[t].lane=(uint8_t)t; }
    for(t=0;t<NLANE;t++) {
        ln_t[t]=(uint8_t)(t<NTRACK?t:0);ln_s[t]=0;ln_on[t]=(uint8_t)(t<NTRACK);
        memset(trk[t].tid,PL_NONE,sizeof trk[t].tid);
    }
}
static int track_publish(uint32_t lane,const sound_t *sound)
{
    uint32_t next;
    if(ck_pub[lane]!=ck_use[lane])return 0;
    next=ck_use[lane]^1u;ck_cook(&ck_buf[lane][next],sound);RING_PUBLISH();ck_pub[lane]=(uint8_t)next;
    return 1;
}
