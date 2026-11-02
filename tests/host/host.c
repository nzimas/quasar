/* SPDX-License-Identifier: GPL-3.0-only
 * Host harness: the firmware's own DSP, tracks, effects, patch generator and screen, compiled for the computer.
 *
 *   host render DIR [SEEDS]   every recipe (gen.c), SEEDS sounds each (default 2): a note held 1 s and let go,
 *                             3 s through the real mixer and effects, a WAV each and a line of figures
 *   host check                every recipe, 12 seeds: none silent, none clipping hard, none running away, none
 *                             with DC; exits 1 on a failure
 *   host screens DIR          the screen in several states, as PPM images
 *   host phrases DIR [N]      N starts (default 4), a phrase on every track, 8 s played: a WAV each, its peak and
 *                             loudness
 * Build: tests/run_tests.sh */
#define HOST 1
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define RAMTAB
#include "control_tables.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FM1_TICKS_PER_US 24u
static volatile uint32_t fm1_ms;
static uint32_t host_ticks;
static uint32_t fm1_ticks(void) { return host_ticks += 1u; }

static int str_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static unsigned str_len(const char *s) { return (unsigned)strlen(s); }
static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void fmt_fix(char *b, int32_t v, int decimals)
{
    char t[12];
    int n = 0, neg = v < 0, i;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
        if (n == decimals)
            t[n++] = '.';
    } while (u || n <= decimals);
    if (neg)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    b[n] = 0;
}
static void fmt_int(char *b, int32_t v) { fmt_fix(b, v, 0); }

static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
            }
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = c;
}

#include "control_math.h"
#include "gfx.c"
#include "sound.h"
#include "engine_control.c"
#define MQ 64u
static uint32_t midi_in_q[MQ];
static volatile uint32_t mi_w, mi_r;
/* (usb.c's MIDI out, here a record of what is sent) */
static uint32_t host_mo[4096], host_mo_n;
static void midi_out_event(uint32_t pkt)
{
    if (host_mo_n < 4096u)
        host_mo[host_mo_n] = pkt;
    host_mo_n++;
}
#include "midi.c"
#include "midi_uart.c"
/* (the host renders on one core: CPU1's mailbox never takes a job, src/audio_worker.h) */
static int audio_worker_online;
static int audio_worker_submit(void (*fn)(void *), void *ctx) { (void)fn; (void)ctx; return 0; }
static int audio_worker_join(void) { return 1; }
static int audio_worker_done(void) { return 1; }
static void audio_worker_check(void) {}
static void fm1_irq_off(void) {}              /* (build 73: the looper's moves) */
static void fm1_irq_on(void) {}
#include "tracks.c"
#include "params.c"
/* (build 73) the pattern playing's loop l on track t: HL, as it is (none: an empty one, apart); HN, made if none */
static loop_t hl_none;
static loop_t *HL(uint32_t t, uint32_t l)
{
    loop_t *p = lp_get(t, l);
    if (p)
        return p;
    memset(&hl_none, 0, sizeof hl_none);
    hl_none.pat = LP_FREE;
    return &hl_none;
}
typedef struct { loop_t h; lev_t ev[LOOP_EV]; } hl_copy_t;
static void hl_snap(hl_copy_t (*w)[NLOOP])       /* (the pattern playing's loops, kept: their notes too) */
{
    uint32_t t, l;
    for (t = 0; t < NTRACK; t++)
        for (l = 0; l < NLOOP; l++) {
            const loop_t *p = HL(t, l);
            w[t][l].h = *p;
            memset(w[t][l].ev, 0, sizeof w[t][l].ev);
            memcpy(w[t][l].ev, p->ev, (p->ev ? p->n : 0u) * sizeof(lev_t));
        }
}
static loop_t *HN(uint32_t t, uint32_t l)
{
    loop_t *p = lp_get(t, l);
    return p ? p : lp_new(seq_lpat, t, l, LOOP_EV);
}
#include "gen.c"

#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, {5, 11, 4, 10, 3, 9, 2, 8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13, 7, 6, 12}, {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    {0, 1, 15, 14, 17, 16, 19, 18, 20, 21, 22}, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_press;
static int32_t host_enc[7];
static uint8_t fm1_led_dim[FM1_NCOL];
static uint32_t fm1_input_edges(uint32_t *rel)
{
    uint32_t p = host_press;
    host_press = 0;
    if (rel)
        *rel = 0;
    return p;
}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e];
    host_enc[e] = 0;
    return s;
}
static struct { uint8_t config; volatile uint8_t suspended; } usb;   /* (usb.c's, as the battery reads it) */
static uint8_t host_led[64];
static void fm1_led_key(uint32_t id, int on) { if (id < 64u) host_led[id] = (uint8_t)!!on; }
#include "ui.c"
#include "projects.c"
#include "setup.c"
#include "backup.c"
#include "phrase.c"
#include "logo.c"

/* the flash, emulated: NOR (erased to 0xFF, programming only clears bits) */
static uint8_t host_flash[0x100000];
static uint32_t host_erases;
static int prj_flash_read(uint32_t off, void *p, uint32_t n)
{
    memcpy(p, host_flash + off, n);
    return 0;
}
/* (main.c's areas: the sessions and the loop store's first 9 sectors; past the update's staging, its other 22 and
 * the last session's note) */
static int host_prj_in(uint32_t off, uint32_t n)
{
    return (off >= 0x97000u && off + n <= 0xE0000u) || (off >= 0xE5000u && off + n <= 0xFE000u);
}
static int prj_flash_erase(uint32_t off)
{
    if (!host_prj_in(off, 4096u) || (off & 4095u))
        return -8;
    memset(host_flash + off, 0xFF, 4096u);
    host_erases++;
    return 0;
}
static int prj_flash_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t i;
    if (!host_prj_in(off, n))
        return -8;
    for (i = 0; i < n; i++)
        host_flash[off + i] &= ((const uint8_t *)p)[i];
    return 0;
}

static void ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f)
        return;
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        uint8_t px[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63u) << 2), (uint8_t)((c & 31u) << 3)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void shot(const char *dir, const char *name)
{
    char p[256];
    ui.force = 1;
    ui_draw();
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    ppm(p);
}

static void key(uint32_t k, int down)
{
    if (down)
        fm1_in.notes |= 1u << k;
    else
        fm1_in.notes &= ~(1u << k);
    ui_input();
}

static void btn(uint32_t b, int sh)
{
    if (sh)
        fm1_in.notes |= 1u << KEY_SHIFT;
    host_press = 1u << PANEL_BTN[b];
    ui_input();
    fm1_in.notes &= ~(1u << KEY_SHIFT);
    fm1_ms += 2000;
}

static int host_dim_on(uint32_t id)             /* a key's LED lit dimly */
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                return (fm1_led_dim[p] >> r) & 1u;
    return 0;
}

static void tap(uint32_t k)                      /* a key pressed and let go */
{
    fm1_in.notes |= 1u << k;
    ui_input();
    fm1_in.notes &= ~(1u << k);
    ui_input();
}

static void turn(uint32_t e, int32_t n)
{
    host_enc[PANEL_ENC[e]] = n;
    ui_input();
}
static void grid_open(void) { if(ui.page!=PG_PAT)btn(B_SAVE,0); }
static int fails;
#define LEXPECT(c, what) do { if (!(c)) { printf("FAIL lfo: %s\n", what); fails++; } } while (0)
static void lfo_use(const sound_t *s)            /* (s on track 0, as the audio takes a published sound) */
{
    ck_cook(&ck_buf[0][0], s);
    ck_buf[0][1] = ck_buf[0][0];
    ck_pub[0] = ck_use[0] = 0;
    lfo_flips[0]++;
}
static void lfo_one(sound_t *s, uint32_t dest, uint32_t wave, uint32_t mode, int32_t spd, uint32_t mult, int32_t dep)
{
    lfos_off(s);
    s->lfo[0].dest = (uint8_t)dest;
    s->lfo[0].wave = (uint8_t)wave;
    s->lfo[0].mode = (uint8_t)mode;
    s->lfo[0].spd = (uint8_t)(64 + spd);
    s->lfo[0].mult = (uint8_t)mult;
    s->lfo[0].dep = (uint8_t)(64 + dep);
    lfo_use(s);
}
static int cmd_lfo(const char *dir)
{
    sound_t s;
    int32_t i, prev, up[4], nup = 0, mx = -99999, mn = 99999, a, b;
    const uint32_t FREQ = 1u + PG_MACRO * 8u;
    uint32_t per;
    tracks_init();
    fx_bpm10 = 1200;
    gen_sound(&s, ENG_HHT, -1, -1, 77);
    par_put(&s,PG_MACRO,0,64);
    /* SPEED +32, MULT x4: a cycle in 2048 / 128 = 16 16ths, a bar: 2 s at 120 BPM, 2756.25 blocks, 689 steps */
    lfo_one(&s, FREQ, LW_TRI, LM_FREE, 32, 2, 32);
    per = 44100u * 2u / CTL / LFO_EVERY;         /* (a step: LFO_EVERY blocks) */
    prev = 0;
    for (i = 0; i < (int32_t)per * 3 + 10; i++) {
        lfo_step(0);
        if (i > 0 && prev < 0 && lfo_mod[0][0] >= 0 && nup < 4)
            up[nup++] = i;
        prev = lfo_mod[0][0];
        mx = lfo_mod[0][0] > mx ? lfo_mod[0][0] : mx;
        mn = lfo_mod[0][0] < mn ? lfo_mod[0][0] : mn;
    }
    LEXPECT(nup >= 3 && absi(up[2] - up[1] - (int32_t)per) <= 2, "SPEED +32, MULT x4 at 120 BPM: a cycle a bar");
    LEXPECT(absi(mx - 16383) < 300 && absi(mn + 16384) < 300, "DEPTH +32: half the destination's range each way");
    if (nup >= 3)
        printf("lfo: a cycle %d steps (%u a bar), share %d .. %d\n", up[2] - up[1], per, mn, mx);
    fx_bpm10 = 600;                              /* (60 BPM: twice as long; MULT .4 at 120 BPM whatever the tempo) */
    nup = 0;
    for (i = 0; i < (int32_t)per * 8 && nup < 3; i++) {
        lfo_step(0);
        if (i > 0 && prev < 0 && lfo_mod[0][0] >= 0)
            up[nup++] = i;
        prev = lfo_mod[0][0];
    }
    LEXPECT(nup == 3 && absi(up[2] - up[1] - 2 * (int32_t)per) <= 3, "the tempo halved: the cycle twice as long");
    lfo_one(&s, FREQ, LW_TRI, LM_FREE, 32, 12u + 2u, 32);
    nup = 0;
    for (i = 0; i < (int32_t)per * 4 && nup < 3; i++) {
        lfo_step(0);
        if (i > 0 && prev < 0 && lfo_mod[0][0] >= 0)
            up[nup++] = i;
        prev = lfo_mod[0][0];
    }
    LEXPECT(nup == 3 && absi(up[2] - up[1] - (int32_t)per) <= 2, "MULT .4: at 120 BPM, not the tempo's");
    fx_bpm10 = 1200;

    /* the cooked copy: the destination moved by the share, the copy the one the voices play */
    lfo_one(&s, FREQ, LW_SQR, LM_FREE, 8, 0, 20);
    for (i = 0; i < 2 * (int32_t)LFO_EVERY; i++)
        lfo_block();
    {
        sound_t c = ck_buf[0][0].snd;
        lfo_apply(&c, lfo_mod[0]);
        LEXPECT(lfo_live(0) && par_val(&ck_lfo[0].snd,PG_MACRO,0) == par_val(&c,PG_MACRO,0) && par_val(&c,PG_MACRO,0) == 64 + 20 * 127 / 64,
                "SQR, DEPTH +20 on FREQ 64: the copy cooked at FREQ 103");
    }
    s.lfo[0].dep = 64;                           /* (DEPTH 0: off, the track's own sound) */
    lfo_use(&s);
    for (i = 0; i < (int32_t)LFO_EVERY; i++)
        lfo_block();
    LEXPECT(!lfo_live(0), "DEPTH 0: the LFO off, the track's own cooked sound played");

    /* TRIG: from START at each note; FREE: on as it was */
    lfo_one(&s, FREQ, LW_SINE, LM_TRIG, 20, 3, 63);
    for (i = 0; i < 700; i++)
        lfo_step(0);
    a = lfo_mod[0][0];
    lfo_trig[0] = 1;
    lfo_step(0);
    LEXPECT(absi(lfo_mod[0][0]) < 2000 && absi(a) > 2000, "TRIG: a note starts the wave again (SINE from 0)");
    s.lfo[0].mode = LM_FREE;
    lfo_use(&s);
    for (i = 0; i < 700; i++)
        lfo_step(0);
    a = lfo_mod[0][0];
    lfo_trig[0] = 1;
    lfo_step(0);
    LEXPECT(absi(lfo_mod[0][0] - a) < 2000 && absi(a) > 2000, "FREE: a note does not");

    /* HOLD: the wave as it was at the note, held till the next */
    lfo_one(&s, FREQ, LW_TRI, LM_HOLD, 40, 3, 63);
    for (i = 0; i < 333; i++)
        lfo_step(0);
    lfo_trig[0] = 1;
    lfo_step(0);
    a = lfo_mod[0][0];
    for (i = 0, b = 1; i < 900; i++) {
        lfo_step(0);
        b &= lfo_mod[0][0] == a;
    }
    lfo_trig[0] = 1;
    lfo_step(0);
    LEXPECT(b && a != 0 && lfo_mod[0][0] != a, "HOLD: the value at a note, held until the next note");

    /* ONE: a cycle and stop; HALF: half */
    lfo_one(&s, FREQ, LW_SAW, LM_ONE, 32, 2, 63);
    lfo_trig[0] = 1;
    for (i = 0; i < (int32_t)per * 3 / 2; i++)
        lfo_step(0);
    a = lfo_mod[0][0];
    for (i = 0, b = 1; i < (int32_t)per; i++) {
        lfo_step(0);
        b &= lfo_mod[0][0] == a;
    }
    LEXPECT(b && a > 30000 * 63 / 64, "ONE: a cycle from a note, then still (SAW: at its start, the top)");
    lfo_one(&s, FREQ, LW_TRI, LM_HALF, 32, 2, 63);
    lfo_trig[0] = 1;
    for (i = 0; i < (int32_t)per; i++)
        lfo_step(0);
    a = lfo_mod[0][0];
    lfo_step(0);
    LEXPECT(lfo_mod[0][0] == a && absi(a) < 1500, "HALF: half a cycle, then still (TRI: back at 0)");

    /* FADE -16: in over 16 16ths (a bar) from each note; FADE +16: out */
    lfo_one(&s, FREQ, LW_SQR, LM_FREE, 1, 0, 63);
    s.lfo[0].fade = 64 - 16;
    lfo_use(&s);
    lfo_trig[0] = 1;
    lfo_step(0);
    a = absi(lfo_mod[0][0]);
    for (i = 0; i < (int32_t)per / 2; i++)
        lfo_step(0);
    b = absi(lfo_mod[0][0]);
    for (i = 0; i < (int32_t)per; i++)
        lfo_step(0);
    LEXPECT(a < 100 && absi(b - 16000) < 800 && absi(lfo_mod[0][0]) > 32000 * 63 / 64 - 300,
            "FADE -16: none at a note, half at half a bar, all after a bar");
    s.lfo[0].fade = 64 + 16;
    lfo_use(&s);
    lfo_trig[0] = 1;
    lfo_step(0);
    a = absi(lfo_mod[0][0]);
    for (i = 0; i < (int32_t)per + 2; i++)
        lfo_step(0);
    LEXPECT(a > 31000 && lfo_mod[0][0] == 0, "FADE +16: all at a note, none after a bar");

    /* RND: a new value each cycle, within the depth; START slews it */
    lfo_one(&s, FREQ, LW_RND, LM_FREE, 64, 9, 32);
    {
        int32_t ch = 0, last = 0, big = 0;
        for (i = 0; i < 2000; i++) {
            lfo_step(0);
            ch += lfo_mod[0][0] != last;
            big |= absi(lfo_mod[0][0]) > 16384;
            last = lfo_mod[0][0];
        }
        LEXPECT(ch > 20 && !big, "RND: new values, within the depth (and on again in FREE after HALF stopped it)");
    }

    /* LFO 2 on LFO 1's DEPTH: LFO 1 at DEPTH 0 moves as LFO 2 swings it */
    lfo_one(&s, FREQ, LW_SQR, LM_FREE, 8, 0, 0);
    s.lfo[1].dest = LFO_D_LFO + 1u;
    s.lfo[1].wave = LW_SQR;
    s.lfo[1].spd = 64 + 2;
    s.lfo[1].mult = 0;
    s.lfo[1].dep = 64 + 32;
    lfo_use(&s);
    for (i = 0, mx = 0; i < 4000; i++) {
        lfo_step(0);
        mx = absi(lfo_mod[0][0]) > mx ? absi(lfo_mod[0][0]) : mx;
    }
    LEXPECT(lfo_any[0] && absi(mx - 16383) < 600 && lfo_mod[0][1] == 0, "LFO 2 on LFO 1's DEPTH: LFO 1 moves by it");
    LEXPECT(!lfo_dest_ok(&s, 0, LFO_D_LFO) && !lfo_dest_ok(&s, 1, LFO_D_LFO + 2u) && lfo_dest_ok(&s, 2, LFO_D_LFO + 3u),
            "an LFO reaches only the earlier LFOs");

    /* the pages: after REVERB, LFO 1 .. 3 of the chosen track; DEST steps over what the machine lacks */
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    ui.sel = 2;
    for (i = 0; i < 20u && !(ui.page == PG_FXP && ui.fx_pg == FXP_L1); i++)   /* (the pages shown only) */
        btn(B_SEQ, 0);
    LEXPECT(ui.page == PG_FXP && ui.fx_pg == FXP_L1, "SEQ: the panel's pages, DUCK, then AUTO 1");
    shot(dir, "lfo1");
    lfos_off(cur());
    fx_turn(LF_DEST, 1);
    LEXPECT(cur()->lfo[0].dest == 1u && ui.snd[0][ui.eng[0]].lfo[0].dest == 0u && ui.dirty[2],
            "DEST on LFO 1: the chosen track's sound's, the others' not");
    fx_turn(LF_DEST, 200);
    LEXPECT(cur()->lfo[0].dest == LFO_D_LFO - 1u || lfo_dest_ok(cur(), 0, cur()->lfo[0].dest),
            "DEST at its end: a parameter LFO 1 may take");
    fx_turn(LF_DEST, -200);
    fx_turn(LF_DEST, (int32_t)(FREQ - 1u));
    {
        char t[24];
        lfo_dest_text(cur(), cur()->lfo[0].dest, t);
        printf("lfo: DEST %u reads %s\n", cur()->lfo[0].dest, t);
    }
    fx_turn(LF_DEP, 20);
    fx_turn(LF_MULT, 30);
    fx_turn(LF_WAVE, 3);
    LEXPECT(cur()->lfo[0].dep == 84u && cur()->lfo[0].mult == NLMULT - 1u && cur()->lfo[0].wave == LW_SAW,
            "DEPTH, MULT (at its end), WAVE turned");
    ui.half = 0;
    shot(dir, "lfo1_set");
    btn(B_SEQ, 0);
    btn(B_SEQ, 0);
    LEXPECT(ui.fx_pg == FXP_L3, "SEQ: LFO 2, LFO 3");
    btn(B_SEQ, 0);
    LEXPECT(ui.page == PG_MAIN, "SEQ after AUTO 3: back");
    {   /* SHIFT + SEQ (build 58): the chosen track's three LFOs thrown anew, the others' kept; 400 throws, each
         * machine: two or three on, each to a parameter that moves smoothly or an earlier LFO, none twice */
        uint32_t i, bad = 0, dup = 0, onl = 0, ok3 = 0, lfo2 = 0, seen = 0;
        sound_t keep = ui.snd[1][ui.eng[1]];
        ui.page = PG_MAIN;
        ui.sel = 0;
        for (i = 0; i < 400u; i++) {
            sound_t *sn = cur();
            uint32_t n, on = 0, j;
            if (i % 100u == 0u) {
                ui.eng[0] = (uint8_t)(i / 100u);
                gen_sound(cur(), ui.eng[0], -1, -1, 7u + i);
                sn = cur();
            }
            btn(B_SEQ, 1);
            for (n = 0; n < NLFO; n++) {
                const lfo_t *l = &sn->lfo[n];
                if (!lfo_on(l)) {
                    bad += l->dest != 0;
                    continue;
                }
                on++;
                bad += !lfo_dest_ok(sn, n, l->dest) || (l->dest < LFO_D_LFO && !lfo_rnd_dest_ok(sn, l->dest)) ||
                       l->spd == 64 || l->wave >= NLW || l->mode >= NLM || l->mult >= NLMULT;
                lfo2 += l->dest >= LFO_D_LFO;
                for (j = 0; j < n; j++)
                    dup += sn->lfo[j].dest == l->dest;
                seen |= 1u << l->wave;
            }
            onl += on < 2u || on > 3u;
            ok3 += on == 3u;
        }
        printf("lfo: SHIFT + SEQ 400 throws: %u with 3 on, %u on another LFO, %u twice to one, %u bad\n", ok3, lfo2, dup,
               bad);
        LEXPECT(!bad && !onl && ok3 > 100u && ok3 < 300u && lfo2 > 20u && dup < 20u && seen == (1u << NLW) - 1u,
                "SHIFT + SEQ: two or three LFOs on, each to a smooth parameter or an earlier LFO, every wave");
        LEXPECT(!memcmp(&keep, &ui.snd[1][ui.eng[1]], sizeof keep) && ui.dirty[0] && !strcmp(ui.msg, "LFO RANDOM"),
                "SHIFT + SEQ: the chosen track's alone, published, LFO RANDOM shown");
        ui.fx_pg = FXP_L1;
        ui.page = PG_FXP;
        ui_draw();
        shot(dir, "lfo_random");
    }
    printf("lfo: %d failures\n", fails);
    return fails != 0;
}

/* transposition (build 51): OCT- / OCT+ the keys' octave in the track's view, the track's steps in the step view,
 * SHIFT every track's; in steps of the scale; kept with the pattern */
#define LPN(t, l, i) lp_note(HL(t, l), HL(t, l)->ev[i].note)   /* (a loop's note as it plays: build 68) */
#define TEXPECT(c, what) do { if (!(c)) { printf("FAIL trn: %s\n", what); fails++; } } while (0)
static int cmd_trn(const char *dir)
{
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    memset((void *)seq_note, 0, sizeof seq_note);
    ui.scale = 1;                                /* MAJOR, from C */
    ui.root = 0;
    seq_note[0][0] = 60; seq_note[0][1] = 64; seq_note[0][2] = 71;
    seq_note[1][0] = 48;
    ui.sel = 0;
    ui.sv = 0;
    ui.oct[0] = 0;
    btn(B_OCTUP, 0);
    TEXPECT(ui.oct[0] == 1 && seq_note[0][0] == 60 && !ui.trn[0], "the track's view: OCT+ the keys' octave, the steps kept");
    btn(B_OCTDN, 0);
    ui.sv = 1;
    btn(B_OCTUP, 0);
    TEXPECT(seq_note[0][0] == 62 && seq_note[0][1] == 65 && seq_note[0][2] == 72 && ui.trn[0] == 1 && !ui.oct[0] &&
            seq_note[1][0] == 48 && !ui.trn[1], "the step view: OCT+ the track's steps a step of the scale up (C E B: D F C)");
    ui_draw();
    shot(dir, "trn_step");
    btn(B_OCTDN, 0);
    btn(B_OCTDN, 0);
    TEXPECT(seq_note[0][0] == 59 && seq_note[0][1] == 62 && seq_note[0][2] == 69 && ui.trn[0] == -1,
            "OCT- twice: a step of the scale below where they were (B D A)");
    ui.sv = 0;
    btn(B_OCTUP, 1);
    TEXPECT(seq_note[0][0] == 60 && seq_note[0][2] == 71 && !ui.trn[0] && seq_note[1][0] == 50 && ui.trn[1] == 1 &&
            !ui.oct[0], "SHIFT + OCT+: every track's steps up a step of the scale, the octave kept");
    {   /* the track's loops with its steps (build 54): a loop on track 1 (C, A), another not on */
        loop_t *p;
        lop_clear();
        p = HN(0, 0);
        p->n = 2; p->len = 16; p->ev[0].note = 60; p->ev[1].note = 69; p->ev[0].step = 0; p->ev[1].step = 8;
        p->on = 1;
        HL(0, 0)->ref = lp_trn[0] = ui.trn[0];    /* (recorded now: build 68) */
        (*HN(0, 1)).n = 1; (*HN(0, 1)).ev[0].note = 60;   /* (not on: one being recorded) */
        ui.sv = 1;
        ui.sel = 0;
        btn(B_OCTUP, 0);
        TEXPECT(LPN(0, 0, 0) == 62 && LPN(0, 0, 1) == 71 && (*HL(0, 1)).ev[0].note == 60 && seq_note[0][0] == 62 &&
                ui.trn[0] == 1, "the step view: OCT+ the track's loop up a step of the scale too, with its steps");
        btn(B_OCTDN, 1);
        TEXPECT(LPN(0, 0, 0) == 60 && LPN(0, 0, 1) == 69 && seq_note[0][0] == 60 && !ui.trn[0] && ui.trn[1] == 0,
                "SHIFT + OCT-: every track's steps and loops down");
        btn(B_OCTUP, 1);                         /* (back as the tests below had it: track 1 at 0, track 2 at +1) */
        btn(B_OCTDN, 0);
        lop_clear();
    }
    ui.scale = 0;                                /* CHROMATIC: semitones */
    ui.sv = 1;
    ui.sel = 1;
    btn(B_OCTUP, 0);
    TEXPECT(seq_note[1][0] == 51 && ui.trn[1] == 2, "CHROMATIC: a semitone");
    ui.scale = 1;
    btn(B_OCTDN, 0);
    TEXPECT(seq_note[1][0] == 50 && ui.trn[1] == 1, "a note out of the scale: to the scale's next");

    /* the pattern keeps it: pattern 1 copied to 2, 2 transposed, each as it was left */
    pat_take(&pat_store[0]);
    pat_store[1] = pat_store[0];
    pat_give(1, 1);
    TEXPECT(ui.trn[1] == 1 && seq_note[1][0] == 50, "a copy of the pattern: its transposition with it");
    btn(B_OCTUP, 1);
    TEXPECT(ui.trn[0] == 1 && ui.trn[1] == 2 && seq_note[0][0] == 62 && seq_note[1][0] == 52, "pattern 2 transposed");
    pat_take(&pat_store[1]);
    pat_give(0, 1);
    TEXPECT(!ui.trn[0] && ui.trn[1] == 1 && seq_note[0][0] == 60 && seq_note[1][0] == 50, "pattern 1 as it was");
    pat_give(1, 1);
    TEXPECT(ui.trn[0] == 1 && ui.trn[1] == 2 && seq_note[0][0] == 62 && seq_note[1][0] == 52, "pattern 2 as transposed");
    {   /* (build 73) a loop is its pattern's: pattern 2's not in pattern 1 (playing or not); pattern 2 again, as it
         * was, at its transposition */
        loop_t *lq = HN(1, 0);
        lq->len = 16;
        lq->n = 2;
        lq->ev[0].note = 52;                     /* (as pattern 2 has it: TRN +2 on track 2) */
        lq->ev[1].note = 55;
        lq->ev[1].step = 8;
        lq->on = 1;
        lq->ref = lp_trn[1] = ui.trn[1];         /* (recorded now) */
        pat_give(0, 1);
        TEXPECT(ui.trn[1] == 1 && seq_lpat == 0u && !lp_get(1, 0), "pattern 1 in: pattern 2's loop not in it");
        pat_give(1, 1);
        TEXPECT(ui.trn[1] == 2 && lp_get(1, 0) == lq && LPN(1, 0, 0) == 52 && LPN(1, 0, 1) == 55,
                "pattern 2 again: its loop as it was");
        {   /* playing: as the pattern is taken in */
            int32_t out[2 * CTL];
            uint32_t j;
            seq_run = 1;
            pat_switch(0);
            for (j = 0; j < 4000u && seq_pend; j++) {
                mix_block(out);
                pat_poll();
            }
            TEXPECT(!seq_pend && pat_cur == 0u && seq_lpat == 0u && !lp_get(1, 0),
                    "playing: pattern 1 taken in, pattern 2's loop not with it");
            seq_run = 0;
            pat_give(1, 1);
        }
        lop_clear();
    }
    {   /* a pattern saved before build 51: no transposition */
        pat_t p = pat_store[1];
        memset(p.seq + SEQ_TRN, 0, NTRACK);
        static seqbuf_t w;
        pat_decode(&p, &w);
        TEXPECT(!w.trn[0] && !w.trn[1] && w.note[1][0] == 52, "an older pattern: TRN 0, its notes");
    }
    flash("SAVED", "SESSION 16", C_OK);          /* (the second line in the size that fits: build 52) */
    ui_draw();
    shot(dir, "saved_session16");
    {   /* the battery, top right (build 56): each level, and on a USB host's power; redrawn as it changes */
        static const int32_t RAW[5] = {600, 570, 540, 500, 600};
        static const int32_t KEY[5] = {3, 2, 1, 0, 4};
        uint32_t i;
        char nm[32];
        ui.oct[ui.sel] = 1;
        ui.sv = 0;
        ui.page = PG_MAIN;
        ui.flash_on = 0;
        for (i = 0; i < 5u; i++) {
            batt_raw = RAW[i];
            usb.config = i == 4u;
            ui_draw();
            TEXPECT(ui.shown_batt == KEY[i], "the battery shown as its level (USB: the bolt)");
            snprintf(nm, sizeof nm, "batt_%u", i);
            shot(dir, nm);
        }
        batt_raw = 0;
        usb.config = 0;
        ui_draw();
        TEXPECT(ui.shown_batt == -1, "the battery not read yet: none shown");
    }
    {   /* the knobs' dial (build 59): its name and the track's number within the screen -- REVERB SEND 6, CHORUS
         * SEND 6 in the size that fits; nothing at its edges */
        static const uint32_t K[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
        uint32_t j, sh, y, x, edge = 0;
        ui.page = PG_MAIN;
        ui.sv = 0;
        ui.sel = 5;
        for (sh = 0; sh < 2u; sh++)
            for (j = 0; j < 4u; j++) {
                char nm[32];
                if (sh)
                    fm1_in.notes |= 1u << KEY_SHIFT;
                host_enc[PANEL_ENC[K[j]]] = 1;
                ui_input();
                fm1_in.notes &= ~(1u << KEY_SHIFT);
                ui.force = 1;
                ui_draw();
                for (y = 0; y < 40u; y++)
                    for (x = 0; x < 240u; x += 239u)
                        edge += fb[y * 240u + x] != C_BLACK;
                snprintf(nm, sizeof nm, "dial_k%u%s", j + 1u, sh ? "_shift" : "");
                shot(dir, nm);
                fm1_ms += 2000;
            }
        TEXPECT(!edge, "the knobs' dial: its name and the track within the screen (track 6, KNOB 1 .. 4, SHIFT too)");
    }
    printf("trn: %d failures\n", fails);
    return fails != 0;
}

/* the arpeggiator (build 52): the 5th black key, its page, its modes on the tempo's grid, LATCH, recorded into the
 * steps and into a loop, kept with the session */
#define AEXPECT(c, what) do { if (!(c)) { printf("FAIL arp: %s\n", what); fails++; } } while (0)
static uint8_t arp_got[64];
static uint32_t arp_ngot, arp_gotb[64];
static void arp_run(uint32_t blocks)             /* (the audio and, every 8 blocks, the main loop) */
{
    int32_t b[2u * CTL];
    uint32_t k, c0 = arps[0].c;
    for (k = 0; k < blocks; k++) {
        mix_block(b);
        if (arps[0].c != c0 && arps[0].snd && arp_ngot < 64u) {
            arp_gotb[arp_ngot] = k;
            arp_got[arp_ngot++] = arps[0].snd;
        }
        c0 = arps[0].c;
        fm1_ms += (k % 4u == 0u);
        if (k % 8u == 7u)
            ui_input();
    }
}
static int arp_seq(const uint8_t *want, uint32_t n)
{
    uint32_t i;
    if (arp_ngot < n)
        return 0;
    for (i = 0; i < n; i++)
        if (arp_got[i] != want[i])
            return 0;
    return 1;
}
static void arp_print(const char *w)
{
    uint32_t i;
    printf("arp: %s:", w);
    for (i = 0; i < arp_ngot && i < 16u; i++)
        printf(" %u", arp_got[i]);
    printf("\n");
}
static int cmd_arp(const char *dir)
{
    const uint32_t B16 = 44100u * 60u / 120u / 4u / CTL;   /* (a 16th at 120 BPM: 172 blocks) */
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    ui.scale = 0;
    ui.root = 5;                                 /* (CHROMATIC: key k plays 53 + k) */
    ui.oct[0] = 0;
    ui.sel = 0;
    ui.tempo = 120;
    tempo_apply();
    tap(KEY_ARP);
    AEXPECT(arp_on[0] && !arp_on[1], "the 5th black key: the track's arpeggiator on");
    ui_draw();
    shot(dir, "arp_main");
    key(0, 1); key(4, 1); key(7, 1);             /* F A C, held */
    arp_ngot = 0;
    arp_run(B16 * 7u + 10u);
    {
        static const uint8_t UP[7] = {53, 57, 60, 53, 57, 60, 53};
        arp_print("UP");
        AEXPECT(arp_seq(UP, 7) && arp_gotb[0] < 3u && absi((int32_t)(arp_gotb[3] - arp_gotb[0]) - 3 * (int32_t)B16) <= 2,
                "UP, 1/16: the notes held in turn, the first at once, a 16th apart");
    }
    arp_p[0][AR_MODE] = AM_UPDN;
    arp_p[0][AR_RNG] = 1;                        /* (2 octaves) */
    arps[0].c = 0;
    arp_ngot = 0;
    arp_run(B16 * 10u + 4u);
    {
        static const uint8_t UD[10] = {53, 57, 60, 65, 69, 72, 69, 65, 60, 57};
        arp_print("UP DOWN, 2 OCT");
        AEXPECT(arp_seq(UD, 10), "UP DOWN over two octaves: the ends once");
    }
    arp_p[0][AR_MODE] = AM_THUMB;
    arp_p[0][AR_RNG] = 0;
    arps[0].c = 0;
    arp_ngot = 0;
    arp_run(B16 * 4u + 4u);
    {
        static const uint8_t TH[4] = {53, 57, 53, 60};
        arp_print("THUMB");
        AEXPECT(arp_seq(TH, 4), "THUMB: the lowest between the others");
    }
    arp_p[0][AR_MODE] = AM_UP;
    arp_p[0][AR_BASS] = 3;                       /* (every 4th: the lowest an octave down) */
    arps[0].c = 0;
    arp_ngot = 0;
    arp_run(B16 * 5u + 4u);
    {
        static const uint8_t BS[5] = {41, 57, 60, 53, 41};
        arp_print("BASS EVERY 4");
        AEXPECT(arp_seq(BS, 5), "BASS EVERY 4: the lowest an octave down every 4th note");
    }
    arp_p[0][AR_BASS] = 0;
    key(0, 0); key(4, 0); key(7, 0);
    arp_ngot = 0;
    arp_run(B16 * 3u);
    AEXPECT(!arps[0].n && !arps[0].snd && !arp_ngot, "the keys let go: it stops");
    arp_p[0][AR_LATCH] = 1;                      /* LATCH: kept with the keys up; a new chord in its place */
    key(2, 1); key(2, 0);
    arp_run(B16 * 2u);
    AEXPECT(arps[0].n == 1u && arps[0].note[0] == 55, "LATCH: the note kept, the key up");
    key(9, 1); key(11, 1); key(9, 0); key(11, 0);
    arp_run(8);
    AEXPECT(arps[0].n == 2u && arps[0].note[0] == 62 && arps[0].note[1] == 64, "LATCH: a new chord in place of the last");
    ui.page = PG_FXP;                            /* LATCH turned off on the page: the notes let go */
    ui.fx_pg = FXP_ARP;
    fx_turn(AR_LATCH, -1);
    arp_run(8);
    AEXPECT(!arps[0].n && !arps[0].snd, "LATCH off: the notes kept let go");
    ui.page = PG_MAIN;

    /* REC: the arpeggio into the steps, at the 16ths, each its length */
    memset((void *)seq_note, 0, sizeof seq_note);
    arp_p[0][AR_GATE] = 127;
    btn(B_REC, 0);
    key(0, 1); key(4, 1);
    arp_run(B16 * 16u + B16 / 2u);
    {
        uint32_t i, ok = 1;
        for (i = 0; i < 16u; i++)
            ok &= seq_note[0][i] == (i & 1u ? 57u : 53u);
        printf("arp: steps %u %u %u %u .. %u, len %u\n", seq_note[0][0], seq_note[0][1], seq_note[0][2], seq_note[0][3],
               seq_note[0][15], seq_glen[0][1]);
        AEXPECT(ok && seq_run, "REC armed: the arpeggio started the sequencer, each note into its step");
    }
    key(0, 0); key(4, 0);
    btn(B_REC, 0);
    btn(B_PLAY, 0);
    arp_run(4);

    /* the looper: SHIFT + REC, the arpeggio in, REC ends it */
    arp_p[0][AR_RATE] = 4;                       /* (1/8) */
    btn(B_REC, 1);
    arp_run(8);
    key(7, 1);
    arp_run(B16 * 16u);
    btn(B_REC, 0);
    {
        const loop_t *p = &(*HL(0, 0));
        uint32_t i, ok = p->on && p->len == 16u && p->n == 8u;
        for (i = 0; ok && i < p->n; i++)
            ok &= p->ev[i].note == 60u && p->ev[i].step == 2u * i;
        printf("arp: loop on %u len %u n %u, steps %u %u %u, len8 %u\n", p->on, p->len, p->n, p->ev[0].step,
               p->ev[1].step, p->ev[2].step, p->ev[0].len8);
        AEXPECT(ok, "SHIFT + REC: the arpeggio at 1/8 into a loop, a note every two steps");
    }
    key(7, 0);

    /* its page; kept with the session */
    btn(B_PLAY, 0);
    {
        uint32_t was = arp_on[0];
        key(KEY_ARP, 1);
        fm1_ms += 900;
        ui_input();
        key(KEY_ARP, 0);
        AEXPECT(ui.page == PG_FXP && ui.fx_pg == FXP_ARP && arp_on[0] == was,
                "the 5th black key held 0.8 s: the ARP page, the arpeggiator as it was");
    }
    fx_turn(AR_MODE, 8);                         /* PINKY */
    fx_turn(AR_DENS, -20);
    ui_draw();
    shot(dir, "arp_page");
    AEXPECT(arp_p[0][AR_MODE] == AM_PINKY && arp_p[0][AR_DENS] < 127u, "MODE and DENSITY turned on the page");
    {
        uint8_t keep[NARP];
        memcpy(keep, (const void *)arp_p[0], NARP);
        AEXPECT(!prj_save(4), "(session 5 saved)");
        tap(KEY_ARP);
        arp_p[0][AR_MODE] = AM_UP;
        AEXPECT(!prj_load(4) && arp_on[0] == 1u && !memcmp(keep, (const void *)arp_p[0], NARP),
                "the session loaded: the arpeggiator on, its settings as saved");
        /* a session of builds 50, 51: no arpeggiator */
        prj_flash_read(prj_sector(4, 0), prj_buf, PRJ_SECT);
        prj_head_fill(PRJ_MAGIC, 4u * 16u, PRJ_LEN50);
        prj_flash_erase(prj_sector(4, 0));
        prj_flash_prog(prj_sector(4, 0), prj_buf, PRJ_SECT);
        {                                        /* (its patterns as builds before 62 saved them: no state) */
            uint32_t s, j;
            for (s = 1; s <= PAT_SECTS; s++)
                if (!prj_read(4, s)) {
                    pat_t *pp = (pat_t *)(void *)(prj_buf + sizeof(prj_head_t));
                    for (j = 0; j < PAT_PER; j++)
                        pp[j].st = 0;
                    prj_write(4, s);
                }
        }
        prj_scan(1);
        AEXPECT(prj_has[4] && !prj_load(4) && !arp_on[0] && arp_p[0][AR_MODE] == AM_UP && arp_p[0][AR_DENS] == 127u,
                "a session of build 51 loads: the arpeggiators off, their start settings");
    }
    printf("arp: %d failures\n", fails);
    return fails != 0;
}

/* bkserve (build 55): the FM-1's side of the backup page, for tests/web/test_backup.mjs -- two sessions (one with
 * loops, one without) and GLOBAL SETUP in the flash, then each line from stdin a message (hex) or a command (WIPE,
 * SCAN, LOAD k: its tempo and loops), the replies to stdout as hex */
static int bk_serving;
static int bk_wire(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (!bk_serving)
        return 0;
    for (i = 0; i < n; i++)
        printf("%02x", p[i]);
    printf("\n");
    return 0;
}
static int cmd_bkserve(void)
{
    char line[4096];
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    ui.tempo = 133;
    (*HN(1, 0)).n = 3; (*HN(1, 0)).len = 32; (*HN(1, 0)).on = 1;
    (*HN(1, 0)).ev[0].note = 60; (*HN(1, 0)).ev[1].note = 63; (*HN(1, 0)).ev[2].note = 67;
    (*HN(1, 0)).ev[1].step = 8; (*HN(1, 0)).ev[2].step = 20;
    (*HN(4, 0)).n = (*HN(4, 1)).n = (*HN(4, 2)).n = 250;   /* (three long ones: the stream over two sectors) */
    (*HN(4, 0)).len = (*HN(4, 1)).len = (*HN(4, 2)).len = 512;
    (*HN(4, 0)).on = (*HN(4, 1)).on = (*HN(4, 2)).on = 1;
    if (prj_save(2))
        printf("SAVE FAILED\n");
    lop_clear();
    ui.tempo = 97;
    if (prj_save(9))
        printf("SAVE FAILED\n");
    mcfg.send_cc = 1;
    setup_write();
    prj_scan(1);
    bk_serving = 1;
    printf("READY\n");
    fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        static uint8_t w[2048], m[2048];
        uint32_t n = 0, i, d = 0, acc = 0, nb = 0, sm = 0;
        if (!strncmp(line, "WIPE", 4)) {
            memset(host_flash + 0x97000u, 0x5A, 0xFE000u - 0x97000u);
            prj_scan(1);
            printf("OK\n");
        } else if (!strncmp(line, "SCAN", 4)) {
            prj_scan(1);
            printf("OK\n");
        } else if (!strncmp(line, "CLEAN", 5)) {      /* (the store erased, as a new FM-1's: tools/make_demo.mjs) */
            memset(host_flash + PRJ_LO, 0xFF, sizeof host_flash - PRJ_LO);
            prj_scan(1);
            printf("OK\n");
        } else if (!strncmp(line, "PAT", 3)) {        /* PAT k j: session k (0 ..) loads at pattern j (0 ..) */
            uint32_t k = 0, j = 0;
            prj_body_t b;
            sscanf(line + 4, "%u %u", &k, &j);
            if (prj_read(k, 0) || j >= PAT_SLOTS) {
                printf("FAIL\n");
            } else {
                memcpy(&b, prj_buf + sizeof(prj_head_t), sizeof b);
                b.pat = (uint8_t)j;
                memcpy(prj_buf + sizeof(prj_head_t), &b, sizeof b);
                printf(prj_write(k, 0) ? "FAIL\n" : "OK\n");
            }
        } else if (!strncmp(line, "DUMP", 4)) {       /* DUMP FILE: the store's sectors not erased, as fm1-emu packs them */
            FILE *f = fopen(strtok(line + 5, "\n"), "wb");
            uint32_t at, ns = 0, e;
            for (at = PRJ_LO; f && at < sizeof host_flash; at += PRJ_SECT) {
                for (e = 0; e < PRJ_SECT && host_flash[at + e] == 0xFFu; e++)
                    ;
                if (e < PRJ_SECT) {
                    uint8_t le[4] = {(uint8_t)at, (uint8_t)(at >> 8), (uint8_t)(at >> 16), (uint8_t)(at >> 24)};
                    fwrite(le, 1, 4, f);
                    fwrite(host_flash + at, 1, PRJ_SECT, f);
                    ns++;
                }
            }
            if (f)
                fclose(f);
            printf(f ? "OK %u\n" : "FAIL\n", ns);
        } else if (!strncmp(line, "LOAD", 4)) {
            int rc = prj_load((uint32_t)atoi(line + 5));
            uint32_t t, l, nl = 0;
            for (t = 0; t < NTRACK; t++)
                for (l = 0; l < NLOOP; l++)
                    nl += (*HL(t, l)).on ? (*HL(t, l)).n : 0u;
            setup_load();
            printf(rc ? "FAIL %d\n" : "OK %d %u %u\n", rc ? rc : ui.tempo, nl, mcfg.send_cc);
        } else {
            for (i = 0; line[i] && line[i + 1] && line[i] != '\n'; i += 2) {
                unsigned v;
                sscanf(line + i, "%2x", &v);
                w[n++] = (uint8_t)v;
            }
            for (i = 1; i + 1u < n; i++) {             /* (ota_unpack7) */
                acc |= (uint32_t)(w[i] & 0x7Fu) << nb;
                nb += 7u;
                while (nb >= 8u) {
                    m[d++] = (uint8_t)acc;
                    acc >>= 8;
                    nb -= 8u;
                }
            }
            for (i = 6; i + 1u < d; i++)
                sm += m[i];
            if (d >= 7u && m[0] == 0 && m[1] == 0x59 && (uint32_t)(m[3] | m[4] << 8 | m[5] << 16) + 7u == d &&
                (uint8_t)~sm == m[d - 1u] && m[2] >= 0x50 && m[2] <= 0x54)
                bk_handle(m, d);
            else
                printf("BAD\n");
        }
        fflush(stdout);
    }
    return 0;
}

/* a new session (build 57): SAVE on an empty slot while a saved session plays -- a blank one there, nothing of the
 * one before (asked first if it has changes not saved); with nothing saved yet, what plays goes into it */
#define NEXPECT(c, what) do { if (!(c)) { printf("FAIL newses: %s\n", what); fails++; } } while (0)
static void ns_slot_save(uint32_t k)             /* (the sessions' list, slot k, its page, SAVE) */
{
    ui.page = PG_MAIN;                           /* (from the track's view: SHIFT + SAVE) */
    btn(B_SAVE, 1);
    turn(EN_PRESET, -20);
    turn(EN_PRESET, (int32_t)k);
    btn(B_SAVE, 0);                              /* (its page, on LOAD) */
    turn(EN_PRESET, 1);                          /* SAVE */
    btn(B_SAVE, 0);
}
static int cmd_newses(const char *dir)
{
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    ui.tempo = 150;
    seq_note[0][0] = 60;
    (*HN(2, 0)).n = 2; (*HN(2, 0)).len = 16; (*HN(2, 0)).on = 1; (*HN(2, 0)).ev[0].note = 50; (*HN(2, 0)).ev[1].note = 52;
    arp_on[1] = 1;
    ui.chord_on[3] = 1;
    pnl[PN_FX_MODE] = 2;
    ui.snd[0][ui.eng[0]].amp[A_VOL] = 33;
    ns_slot_save(0);                             /* (nothing saved yet: what plays goes into slot 1) */
    NEXPECT(prj_cur == 0 && prj_has[0] && ui.tempo == 150 && seq_note[0][0] == 60 && (*HL(2, 0)).on,
            "nothing saved yet: SAVE on an empty slot keeps what plays");
    ns_slot_save(3);
    NEXPECT(prj_cur == 3 && prj_has[3] && ui.page == PG_MAIN, "a saved session playing: SAVE on an empty slot, a new "
            "session there, saved");
    NEXPECT(ui.tempo == 120 && !seq_note[0][0] && !(*HL(2, 0)).on && !arp_on[1] && !ui.chord_on[3] && pnl[PN_FX_MODE] == PNL0[PN_FX_MODE] &&
            ui.snd[0][ui.eng[0]].amp[A_VOL] != 33 && !prj_dirty && !pat_dirty && !seq_run,
            "the new session: nothing of the one before (tempo, steps, loops, ARP, chords, effects, sounds)");
    ui_draw();
    shot(dir, "new_session");
    NEXPECT(!prj_load(0) && ui.tempo == 150 && seq_note[0][0] == 60 && (*HL(2, 0)).on && arp_on[1],
            "the session before, loaded again: all of it");
    ui.tempo = 151;                              /* (changes not saved: asked) */
    prj_dirty = 1;
    ns_slot_save(5);
    NEXPECT(pj.ask == PQ_NEW && !prj_has[5], "changes not saved: NEW SESSION? asked");
    btn(B_SAVE, 0);                              /* (NO, as it opens) */
    NEXPECT(!prj_has[5] && ui.tempo == 151, "NO: nothing changed");
    btn(B_SAVE, 0);
    turn(EN_PRESET, 1);                          /* (YES) */
    btn(B_SAVE, 0);
    NEXPECT(prj_has[5] && prj_cur == 5 && ui.tempo == 120, "YES: the new session");
    printf("newses: %d failures\n", fails);
    return fails != 0;
}

/* (build 62) a pattern holds the tracks' state as it was saved -- muted, machine, octave, level, pan, sidechain,
 * CHORD mode, arpeggiator -- and gives it back when it plays; a pattern saved before (or empty) leaves it as it is */
#define PEXPECT(c, what) do { if (!(c)) { printf("FAIL pstate: %s\n", what); fails++; } } while (0)
static void ps_set_a(void)                       /* (state A) */
{
    uint8_t ar[NARP];
    memcpy(ar, AR_DEF, NARP);
    ar[AR_MODE] = AM_DOWN;
    ar[AR_GATE] = 100;
    memset((void *)trk_mute, 0, NTRACK);
    trk_mute[2] = 1;
    arp_set(1, 1, ar);
    ui.chord_on[3] = 1;
    ui.chord_form[3] = 2;
    ui.eng[4] = 2;
    ui.oct[0] = 2;
    ui.level[1] = 7;
    ui.pan[2] = -10;
    level_pan_apply(1);
    level_pan_apply(2);
    sc_src[5] = 2;
    sc_lvl[5] = 90;
}
static int ps_is_a(void)
{
    return !trk_mute[0] && !trk_mute[1] && trk_mute[2] && arp_on[1] && arp_p[1][AR_MODE] == AM_DOWN &&
           arp_p[1][AR_GATE] == 100u && ui.chord_on[3] && ui.chord_form[3] == 2u && ui.eng[4] == 2u && ui.oct[0] == 2 &&
           ui.level[1] == 7 && ui.pan[2] == -10 && sc_src[5] == 2u && sc_lvl[5] == 90u;
}
static void ps_set_b(void)                       /* (state B) */
{
    memset((void *)trk_mute, 0, NTRACK);
    trk_mute[4] = trk_mute[5] = 1;
    arp_set(1, 0, AR_DEF);
    ui.chord_on[3] = 0;
    ui.eng[4] = 0;
    ui.oct[0] = -1;
    ui.level[1] = LEVEL_UNITY;
    ui.pan[2] = 0;
    sc_src[5] = 0;
    sc_lvl[5] = 64;
}
static int ps_is_b(void)
{
    return !trk_mute[2] && trk_mute[4] && trk_mute[5] && !arp_on[1] && !ui.chord_on[3] && ui.eng[4] == 0u &&
           ui.oct[0] == -1 && ui.level[1] == LEVEL_UNITY && ui.pan[2] == 0 && sc_src[5] == 0u && sc_lvl[5] == 64u;
}
static int cmd_pstate(const char *dir)
{
    int32_t out[2 * CTL];
    uint32_t i, t;
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    seq_note[0][0] = 60;
    ps_set_a();
    PEXPECT(!prj_save(0) && pat_store[0].st == PX_ON, "(session 1 saved, pattern 1 with it, its state)");
    pat_switch(1);                               /* (stopped: at once) */
    PEXPECT(pat_cur == 1u && ps_is_a(), "an empty pattern: the state left as it plays");
    ps_set_b();
    pat_poll();
    PEXPECT(pat_dirty, "the state changed: the pattern changed");
    seq_note[1][4] = 62;
    PEXPECT(!pat_save(1) && !pat_dirty && pat_store[1].st == PX_ON, "pattern 2 saved, with state B");
    pat_switch(0);
    PEXPECT(pat_cur == 0u && ps_is_a() && seq_note[0][0] == 60u && !pat_dirty, "pattern 1: state A back, its steps");
    pat_switch(1);
    PEXPECT(pat_cur == 1u && ps_is_b() && seq_note[1][4] == 62u && !seq_note[0][0], "pattern 2: state B back");
    {   /* (the flash holds it: the session loaded again, at pattern 2) */
        PEXPECT(!prj_save(0), "(session 1 saved at pattern 2)");
        ps_set_a();
        PEXPECT(!prj_load(0) && pat_cur == 1u && ps_is_b(), "the session loaded: its pattern's state");
        pat_switch(0);
        PEXPECT(ps_is_a(), "and pattern 1's, from the flash");
    }
    /* a change not saved: kept in the store as the steps are, while another plays */
    pat_switch(1);
    trk_mute[0] = 1;
    pat_switch(0);
    PEXPECT(!trk_mute[0] && pat_dirtyv[1], "pattern 2's mute not saved: kept in the store, pattern 1 as saved");
    pat_switch(1);
    PEXPECT(trk_mute[0] && pat_dirty, "pattern 2 again: the mute as it was left");
    /* switched while playing: at the end of the chosen track's time through, with its state */
    seq_run = 1;
    pat_switch(0);
    PEXPECT(trk_mute[0] && seq_pend == 1u, "playing: pattern 1 waits, the state as it plays");
    for (i = 0; i < 4000u && seq_pend; i++) {
        mix_block(out);
        pat_poll();
    }
    PEXPECT(!seq_pend && pat_cur == 0u && ps_is_a() && !trk_mute[0], "taken in: pattern 1's state");
    seq_run = 0;
    /* a pattern saved before build 62: no state -- the state left as it plays; its steps as ever */
    pat_store[2] = pat_store[0];
    pat_store[2].st = 0;
    pat_switch(1);
    pat_switch(2);
    PEXPECT(pat_cur == 2u && ps_is_b() && seq_note[0][0] == 60u && !pat_dirty,
            "a pattern of build 61: the state left as it plays, its steps");
    /* its session's mutes, as build 61 kept them (a session loaded: they, then its pattern's -- none) */
    PEXPECT(!prj_save(3), "(session 4 saved, at pattern 3)");
    {
        uint32_t s, j;
        for (s = 1; s <= PAT_SECTS; s++)
            if (!prj_read(3, s)) {
                pat_t *pp = (pat_t *)(void *)(prj_buf + sizeof(prj_head_t));
                for (j = 0; j < PAT_PER; j++)
                    pp[j].st = 0;
                prj_write(3, s);
            }
    }
    ps_set_a();
    PEXPECT(!prj_load(3) && pat_cur == 2u && ps_is_b(), "a session of build 61: the session's mutes and settings");
    /* the room: every step a note and four locks -- the steps end before the state, the state whole */
    {
        static pat_t pp;
        static seqbuf_t sb, sb2;
        uint32_t o = SEQ_REC4, r;
        for (t = 0; t < NTRACK; t++)
            for (i = 0; i < NSTEP; i++) {
                uint32_t l;
                seq_note[t][i] = (uint8_t)(48u + i);
                for (l = 0; l < 4u && l < NPL; l++) {
                    seq_lid[t][i][l] = (uint8_t)l;
                    seq_lv[t][i][l] = 5;
                }
            }
        ps_set_a();
        pat_take(&pp);
        for (r = 0; r < pp.seq[SEQ_NREC]; r++)
            o += 6u + 3u * (pp.seq[o + 5u] & 63u) + 3u * (pp.seq[o + 5u] >> 7) + (pp.seq[o + 5u] >> 6 & 1u);
        PEXPECT(o <= SEQ_PX && pp.seq[SEQ_NREC] > 0u, "a full pattern: its steps end before the state");
        ps_set_b();
        pat_store[7] = pp;
        pat_give(7, 1);
        PEXPECT(ps_is_a(), "a full pattern: its state whole");
        pat_decode(&pp, &sb);
        memcpy(sb2.note, sb.note, sizeof sb.note);
        memcpy(sb2.lid, sb.lid, sizeof sb.lid);
        pp.st = 0;                               /* (as build 61 reads it: the same steps) */
        pat_decode(&pp, &sb);
        PEXPECT(!memcmp(sb2.note, sb.note, sizeof sb.note) && !memcmp(sb2.lid, sb.lid, sizeof sb.lid) &&
                sb.note[0][0] == 48u, "a full pattern read without its state: the steps as ever");
    }
    {   /* (build 64) 96 steps of notes, no locks: every one kept beside the state */
        static pat_t pq;
        static seqbuf_t sq;
        uint32_t ok = 1;
        for (t = 0; t < NTRACK; t++)
            for (i = 0; i < NSTEP; i++) {
                seq_note[t][i] = (uint8_t)(40u + i);
                memset((void *)seq_lid[t][i], PL_NONE, NPL);
            }
        ps_set_a();
        pat_take(&pq);
        pat_decode(&pq, &sq);
        for (t = 0; t < NTRACK; t++)
            for (i = 0; i < NSTEP; i++)
                ok &= sq.note[t][i] == 40u + i;
        PEXPECT(ok && pq.seq[SEQ_NREC] == NTRACK * NSTEP, "96 steps of notes: all kept with the state");
        /* a pattern of builds 62, 63: the state unpacked, its last 78 bytes */
        {
            uint8_t x[NTRACK * PX_T];
            px_take(x);
            pq.st = PX_ON1;
            memset(pq.seq + SEQ_PX1, 0, PAT_SEQ_ROOM - SEQ_PX1);
            memcpy(pq.seq + SEQ_PX1, x, sizeof x);
            ps_set_b();
            pat_store[8] = pq;
            pat_give(8, 1);
            PEXPECT(ps_is_a(), "a pattern of build 62 or 63: its state, as they saved it");
        }
    }
    ui_draw();
    shot(dir, "pattern_state");
    /* (build 63) MUTE mode on the PATTERNS grid: kept when the grid opens, the 2nd black key turns it on or off there,
     * the tracks' buttons mute and the grid stays */
    ui.page = PG_MAIN;
    ui.sv = 0;
    ui.mute_mode = 0;
    memset((void *)trk_mute, 0, NTRACK);
    tap(KEY_TRK);
    grid_open();
    PEXPECT(ui.page == PG_PAT && ui.mute_mode, "MUTE mode on, SAVE: the grid, MUTE mode still on");
    btn((uint32_t)TRACK_BTN[3], 0);
    ui_input();
    ui_leds();
    PEXPECT(trk_mute[3] && ui.page == PG_PAT && host_led[14u + KEY_TRK] && !host_led[PANEL_BTN[TRACK_BTN[3]]] &&
            host_led[PANEL_BTN[TRACK_BTN[0]]], "on the grid: a track's button mutes it (dim), the grid stays");
    ui_draw();
    shot(dir, "pat_grid_mute");
    tap(KEY_TRK);
    ui_leds();
    PEXPECT(!ui.mute_mode && ui.page == PG_PAT && !host_led[14u + KEY_TRK], "the 2nd black key on the grid: MUTE mode off");
    tap(KEY_TRK);
    PEXPECT(ui.mute_mode && ui.page == PG_PAT, "and on again, on the grid");
    tap(KEY_TRK);
    btn((uint32_t)TRACK_BTN[0], 0);
    ui_input();
    PEXPECT(ui.page == PG_MAIN && ui.sel == 0u && trk_mute[3], "MUTE mode off: a track's button back to the track, the mutes stay");
    ui.sv = 1;                                   /* (the step view on: on the grid, MUTE mode all the same) */
    grid_open();
    tap(KEY_TRK);
    ui_input();
    PEXPECT(ui.page == PG_PAT && ui.mute_mode, "the step view on: MUTE mode on the grid all the same");
    tap(KEY_TRK);
    ui.sv = 0;
    {   /* (build 78) MUTE QT, the session's: MUTE mode's mutes and solos wait for the chosen track's next time through */
        int32_t out[2 * CTL];
        uint32_t n, at = 0xFFu, b0 = 1u << PANEL_BTN[TRACK_BTN[0]];
        int l0, l1;
        ui.page = PG_MAIN;
        ui.mute_mode = 0;
        memset((void *)trk_mute, 0, NTRACK);
        ses_muq = 1;
        ui.sel = 1;
        seq_pend = 0;
        seq_len[1] = 8;
        seq_run = 1;
        for (n = 0; n < 20000u && seq_pos[1] != 2u; n++)
            mix_block(out);
        tap(KEY_TRK);
        PEXPECT(ui.mute_mode, "(MUTE mode on)");
        btn((uint32_t)TRACK_BTN[3], 0);
        ui_input();
        PEXPECT(!trk_mute[3] && seq_mq == (0x80u | 0x08u) && seq_mq_t == 1u, "MUTE QT: track 4's mute waits");
        fm1_ms = 1500;
        ui_leds();
        l0 = host_led[PANEL_BTN[TRACK_BTN[3]]];
        fm1_ms = 1650;
        ui_leds();
        l1 = host_led[PANEL_BTN[TRACK_BTN[3]]];
        PEXPECT(l0 != l1, "its button blinks while it waits");
        for (n = 0; n < 20000u && seq_mq; n++)
            mix_block(out);
        at = seq_pos[1];
        PEXPECT(!seq_mq && trk_mute[3] && at == 0u, "muted as track 2 (the chosen one) plays its first step");
        fm1_in.buttons |= b0;                    /* track 1 held 1 s: soloed, waiting too */
        host_press = b0;
        ui_input();
        fm1_ms += 1100;
        ui_input();
        fm1_in.buttons = 0;
        ui_input();
        PEXPECT(seq_mq == (0x80u | 0x3Eu) && !trk_mute[1] && trk_mute[3], "a solo waits too");
        seq_run = 0;
        ui_input();
        PEXPECT(!seq_mq && !trk_mute[0] && trk_mute[1] && trk_mute[5], "stopped: the mutes waiting, at once");
        seq_run = 1;
        btn((uint32_t)TRACK_BTN[0], 0);
        ui_input();
        PEXPECT(seq_mq & 0x80u, "(track 1's mute waiting)");
        pat_switch(pat_cur);
        PEXPECT(!seq_mq, "a pattern chosen: the mutes waiting dropped, the pattern brings its own");
        seq_pend = 0;
        seq_run = 0;
        ses_muq = 0;
        btn((uint32_t)TRACK_BTN[0], 0);
        ui_input();
        PEXPECT(trk_mute[0] && !seq_mq, "MUTE QT off: at once");
        tap(KEY_TRK);
        memset((void *)trk_mute, 0, NTRACK);
        seq_len[1] = 0;
    }
    printf("pstate: %d failures\n", fails);
    return fails != 0;
}

/* (build 64) BREAK on the PATTERNS grid; the 3rd black key held and a white key held 0.8 s: the break saved there */
#define BEXPECT(c, what) do { if (!(c)) { printf("FAIL brksave: %s\n", what); fails++; } } while (0)
static uint32_t bs_kw[16];
static void bs_hold(uint32_t w)                  /* (white key w held 0.8 s, let go) */
{
    key(bs_kw[w], 1);
    fm1_ms += 900;
    ui_input();
    key(bs_kw[w], 0);
}
static void bs_break(int all)                    /* (the 3rd black key down -- SHIFT too: every track -- its plan
                                                  * drawn) */
{
    int32_t out[2 * CTL];
    uint32_t i;
    if (all)
        fm1_in.notes |= 1u << KEY_SHIFT;
    key(KEY_TRIG, 1);
    fm1_in.notes &= ~(1u << KEY_SHIFT);
    for (i = 0; i < 400u && brk_shown == 0xFFu; i++)
        mix_block(out);
    ui_input();
}
/* pattern k's steps: each broken track's step p the step its plan plays there (or 0 in the gap, or an octave
 * away); the others as written (w) */
static int bs_check(uint32_t k, const seqbuf_t *w, uint32_t mask)
{
    static seqbuf_t b;
    uint32_t t, p, ok = 1;
    pat_decode(&pat_store[k], &b);
    for (t = 0; t < NTRACK; t++) {
        uint32_t len = w->len[t] ? w->len[t] : NSTEP, a = brk_fresh >> t & 1u ? 0u : brk_anchor[t];
        for (p = 0; p < NSTEP; p++) {
            if (!(mask >> t & 1u) || (brk_keep >> t & 1u) || p >= len) {
                ok &= b.note[t][p] == w->note[t][p];
                continue;
            }
            {
                uint32_t src = brk_map(t, a, p, len, brk_kick >> t & 1u), n = w->note[t][src], g = b.note[t][p], q, any = 0;
                for (q = 0; q < len; q++)        /* (JUMP leaps at random: any of the track's notes) */
                    any |= g == w->note[t][q];
                ok &= g == n || (n && (g == n + 12u || g + 12u == n)) ||
                      ((brk_lay & BL_RISE) && n && g == n + (p % 8u) * 2u) || (brk_rec == BR_JUMP && any);
            }
        }
    }
    return (int)ok;
}
static int cmd_brksave(const char *dir)
{
    static seqbuf_t w;
    int32_t out[2 * CTL];
    uint32_t k, t, i, cur0;
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    for (k = 0; k < NKEY; k++)
        if (white_ix(k) >= 0)
            bs_kw[white_ix(k)] = k;
    for (t = 0; t < NTRACK; t++)                 /* (three steps of four a note of its own: where it came from,
                                                  * readable; the pattern's room not the question) */
        for (i = 0; i < NSTEP; i++)
            seq_note[t][i] = (uint8_t)(i % 4u == 3u ? 0u : 40u + t * 2u + i);
    trk_mute[4] = 1;
    BEXPECT(!prj_save(0), "(session 1 saved)");
    seq_copy_out(&w);
    btn(B_PLAY, 0);
    for (i = 0; i < 40u; i++)
        mix_block(out);
    grid_open();
    cur0 = pat_cur;
    bs_break(1);
    ui_leds();
    BEXPECT(ui.page == PG_PAT && ui.brk == (1u << NTRACK) - 1u && brk_shown != 0xFFu && host_led[14u + KEY_TRIG],
            "the grid: SHIFT and the 3rd black key, every track broken, the key lit");
    for (i = 0; i < 1500u; i++)                  /* (each track into it, on its next beat) */
        mix_block(out);
    ui_input();
    ui_draw();
    shot(dir, "pat_grid_break");
    BEXPECT(!(brk_fresh & ~brk_keep & ((1u << NTRACK) - 1u)), "(every track into the break)");
    bs_hold(6);
    BEXPECT(pat_store[6].used && pat_cur == cur0 && ui.page == PG_PAT && str_eq(ui.flash_l1, "BREAK SAVED"),
            "a white key held 0.8 s: the break saved as pattern 7, the one playing kept");
    BEXPECT(bs_check(6, &w, (1u << NTRACK) - 1u), "pattern 7: each track's steps the ones its break plays");
    {
        uint8_t x[NTRACK * PX_T];
        px_unpack(x, pat_store[6].seq + SEQ_PX);
        BEXPECT(pat_store[6].st == PX_ON && (x[4u * PX_T] & 1u), "with the tracks' state (track 5 muted)");
    }
    {
        int rc = prj_read(0, 2u);
        const pat_t *pp = (const pat_t *)(const void *)(prj_buf + sizeof(prj_head_t));
        BEXPECT(!rc && !memcmp(&pp[0], &pat_store[6], sizeof pp[0]), "pattern 7 in the session's flash");
    }
    for (t = 0, i = 1; t < NTRACK; t++)
        for (k = 0; k < NSTEP; k++)
            i &= seq_note[t][k] == w.note[t][k];
    BEXPECT(i, "the steps playing untouched");
    key(KEY_TRIG, 0);
    BEXPECT(!ui.brk && !brk_mask, "the key let go: the break over");
    /* the chosen track alone (no SHIFT): the others as written; saved over an occupied slot */
    ui.sel = 2;
    bs_break(0);
    for (i = 0; i < 1500u; i++)
        mix_block(out);
    bs_hold(6);
    BEXPECT(bs_check(6, &w, 1u << 2), "the chosen track's break over pattern 7: the other tracks as written");
    key(KEY_TRIG, 0);
    /* over the pattern playing: its steps the break's, at the end of the chosen track's time through */
    bs_break(1);
    for (i = 0; i < 300u; i++)
        mix_block(out);
    bs_hold(cur0);
    BEXPECT(bs_check(cur0, &w, (1u << NTRACK) - 1u), "over the one playing: the break saved there");
    BEXPECT(seq_pend == 1u && !pat_dirty, "over the one playing: the break's steps waiting");
    for (i = 0; i < 4000u && seq_pend; i++) {
        mix_block(out);
        pat_poll();
    }
    BEXPECT(!seq_pend && pat_cur == cur0, "taken in, at the end of the chosen track's time through");
    key(KEY_TRIG, 0);
    {
        static seqbuf_t b;
        pat_decode(&pat_store[cur0], &b);
        for (t = 0, i = 1; t < NTRACK; t++)
            for (k = 0; k < NSTEP; k++)
                i &= seq_note[t][k] == b.note[t][k];
        BEXPECT(i && !pat_dirty, "the break's steps playing, the pattern as saved");
    }
    /* stopped: nothing to save */
    btn(B_PLAY, 0);
    for (i = 0; i < 8u; i++)
        mix_block(out);
    memset(&pat_store[9], 0, sizeof pat_store[9]);
    bs_break(1);
    bs_hold(9);
    BEXPECT(!pat_store[9].used, "stopped: no break, nothing saved");
    key(KEY_TRIG, 0);
    printf("brksave: %d failures\n", fails);
    return fails != 0;
}

/* (build 73) a loop is its pattern's: another pattern in, it is silent (an empty one: nothing at all); its own back,
 * from its first step with the pattern's; saved into another slot, the loops with it; a pattern emptied, its loops
 * gone. (Build 65: a pattern that unmutes a track, its notes from the muted time let go) */
#define QEXPECT(c, what) do { if (!(c)) { printf("FAIL loopin: %s\n", what); fails++; } } while (0)
static void li_until(uint32_t g)                 /* (played until the tempo's 16th g; the main loop's poll each block) */
{
    int32_t out[2 * CTL];
    uint32_t i;
    for (i = 0; i < 200000u && (int32_t)(seq_gcount - g) < 0; i++) {
        mix_block(out);
        pat_poll();
    }
}
static int li_sounding(uint32_t t) { p23_meter m = p23_inspect(t); return m.gate || m.active; }   /* (a note playing: percussive sounds fall while it is held) */
static int pat_save_as_rc(uint32_t k)            /* (the pattern playing saved as pattern k, its loops with it) */
{
    pat_save_as(k);
    return !pat_store[k].used || strcmp(ui.flash_l1, "SAVED") != 0;
}
static int cmd_loopin(const char *dir)
{
    loop_t *lp;
    uint32_t n0, i, top;
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    for (i = 0; i < NSTEP; i += 4u)
        seq_note[0][i] = 36;
    ui.sel = 0;
    trk_mute[3] = 1;                             /* pattern 1: track 4 muted; pattern 2: playing */
    QEXPECT(!pat_save(0), "(pattern 1 saved, track 4 muted)");
    trk_mute[3] = 0;
    QEXPECT(!pat_save(1), "(pattern 2 saved, track 4 playing)");
    QEXPECT(!pat_save(0), "(pattern 1 again, track 4 playing)");
    pat_switch(0);
    QEXPECT(seq_lpat == 0u, "(pattern 1: its loops)");
    btn(B_PLAY, 0);
    li_until(1);                                 /* (started: the loops' anchors from it) */
    /* a loop on track 4 in pattern 1: two bars, a long note at its first step, another at its 25th; its first step at
     * bar 2 */
    lp = HN(3, 0);
    lp->len = 32;
    lp->n = 2;
    lp->ev[0].step = 0;  lp->ev[0].note = 60; lp->ev[0].vel = 100; lp->ev[0].len8 = 8 * 4;
    lp->ev[1].step = 24; lp->ev[1].note = 62; lp->ev[1].vel = 100; lp->ev[1].len8 = 8 * 4;
    lp->anchor = 16;
    lp->on = 1;
    li_until(17);
    QEXPECT(seq_sounded[3] == 1u && li_sounding(3), "pattern 1: its loop plays (its first note, at bar 2)");
    pat_switch(1);
    li_until(33);
    QEXPECT(pat_cur == 1u && seq_lpat == 1u && !lp_get(3, 0), "pattern 2 in (bar 3): pattern 1's loop not in it");
    n0 = seq_sounded[3];
    li_until(80);
    QEXPECT(seq_sounded[3] == n0, "pattern 2: the loop silent (its 25th step, its first, gone by)");
    pat_switch(0);                               /* (in at bar 7) */
    li_until(97);
    QEXPECT(pat_cur == 0u && lp_get(3, 0) == lp && seq_sounded[3] == n0 + 1u && li_sounding(3),
            "pattern 1 back (bar 7): its loop from its first step, with the pattern's");
    li_until(121);
    QEXPECT(seq_sounded[3] == n0 + 2u, "and on from there (its 25th step)");
    /* the pattern playing saved into slot 5: its loop with it; there, played as its own */
    top = lp_top();
    pat_save_as(4);
    QEXPECT(lp_at(4, 3, 0) && lp_at(4, 3, 0) != lp && lp_at(4, 3, 0)->on && lp_at(4, 3, 0)->n == 2u &&
            !memcmp(lp_at(4, 3, 0)->ev, lp->ev, 2u * sizeof(lev_t)) && lp_top() == top + 2u && lp->on,
            "saved into pattern 5: its loop copied there, pattern 1's kept");
    pat_switch(4);
    li_until(129);
    n0 = seq_sounded[3];
    QEXPECT(pat_cur == 4u && seq_lpat == 4u && li_sounding(3), "pattern 5 in: its own copy of the loop plays");
    /* an empty pattern: no loop at all */
    pat_switch(5);
    li_until(145);
    n0 = seq_sounded[3];
    li_until(220);
    QEXPECT(pat_cur == 5u && seq_sounded[3] == n0 && !lp_get(3, 0) && !lp_get(3, 1) && !lp_get(3, 2),
            "an empty pattern: no loop plays");
    /* pattern 5 emptied: its loop gone, its room given back; pattern 1's as it was */
    pat_clear(4);
    QEXPECT(!lp_at(4, 3, 0) && lp_top() == top && lp_at(0, 3, 0) == lp && lp->ev[1].note == 62u,
            "pattern 5 emptied: its loop gone, the room back, pattern 1's untouched");
    /* stopped: a pattern switched to at once, its loops with it */
    btn(B_PLAY, 0);
    {
        int32_t out[2 * CTL];
        for (i = 0; i < 8u; i++)                 /* (the sequencer stopped at its next block) */
            mix_block(out);
    }
    QEXPECT(!seq_run, "(stopped)");
    pat_switch(0);
    QEXPECT(pat_cur == 0u && seq_lpat == 0u && lp_get(3, 0) == lp, "stopped: pattern 1, its loop");
    {   /* (build 74) a session saved before build 73 (its loops the session's, in every pattern): loaded, its loops
         * in the pattern it was saved playing and in every other pattern saved -- not in the empty ones */
        uint32_t sec;
        lop_meta_t m;
        int rc;
        QEXPECT(!pat_save(0) && !pat_save_as_rc(2), "(patterns 1 and 3)");
        pat_switch(0);                           /* (saved into 3, 3 plays: back to pattern 1) */
        lp_clear_pat(2);
        QEXPECT(!prj_save(6) && lop_bytes() == 1u + 8u + 2u * sizeof(lev_t), "(session 7: one loop, in pattern 1)");
        for (sec = 0; sec < LOP_N; sec++)        /* (its loop as builds 38 .. 72 wrote it: no pattern) */
            if (!lop_peek(sec, &m) && m.slot == 6u && m.idx == 0u && m.gen == prj_lgen[6])
                break;
        QEXPECT(sec < LOP_N && !lop_read(sec), "(its loops' first sector)");
        lop_data()[1 + 3] = 0;
        QEXPECT(!lop_write(sec), "(written as before build 73)");
        lop_clear();
        rc = prj_load(6);
        QEXPECT(!rc && pat_cur == 0u && !pat_store[3].used && !pat_store[5].used && lp_at(0, 3, 0) && lp_at(1, 3, 0) &&
                lp_at(2, 3, 0) && lp_at(2, 3, 0)->on && lp_at(2, 3, 0)->n == 2u && !lp_at(3, 3, 0) && !lp_at(5, 3, 0),
                "an older session: its loop in pattern 1 (saved playing) and in patterns 2 and 3 (saved), not in the "
                "empty ones");
    }
    (void)dir;
    printf("loopin: %d failures\n", fails);
    return fails != 0;
}

/* (build 68) a pattern with another transposition, switched to while playing: the loops play its transposition from
 * the step it comes in on (before: from the next, the main loop moving the notes after the sequencer had played) */
#define XEXPECT(c, what) do { if (!(c)) { printf("FAIL lptrn: %s\n", what); fails++; } } while (0)
static int lx_sounds(uint32_t t, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < SEQ_HOLD; i++)
        if (sq_note[t][i] == n)
            return 1;
    return 0;
}
static void lx_settle(void)                      /* (a few blocks on: a pattern due taken in) */
{
    int32_t out[2 * CTL];
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        mix_block(out);
        pat_poll();
    }
}
static int cmd_lptrn(const char *dir)
{
    loop_t *lp;
    uint32_t i;
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    ui.scale = 0;                                /* (CHROMATIC: a step a semitone) */
    for (i = 0; i < NSTEP; i += 4u)
        seq_note[0][i] = 36;
    ui.sel = 0;
    XEXPECT(!pat_save(0), "(pattern 1: no transposition)");
    ui.trn[3] = 2;
    XEXPECT(!pat_save(1), "(pattern 2: track 4 at +2)");
    ui.trn[3] = 0;
    pat_switch(0);
    XEXPECT(!ui.trn[3] && !lp_trn[3], "(pattern 1)");
    /* (build 73) a loop recorded in pattern 2 (track 4 at +2): its notes at pattern 2's transposition; played
     * there, and only there; the session saved at pattern 1: the loop as it plays in its own pattern */
    pat_switch(1);
    XEXPECT(pat_cur == 1u && lp_trn[3] == 2, "(pattern 2)");
    btn(B_PLAY, 0);
    li_until(1);
    lp = HN(3, 0);                               /* a loop on track 4, a bar, D at its first step (short) */
    lp->len = 16;
    lp->n = 1;
    lp->ev[0].note = 62; lp->ev[0].vel = 100; lp->ev[0].len8 = 4;
    lp->anchor = 0;
    lp->ref = 2;                                 /* (recorded at +2) */
    lp->on = 1;
    li_until(16);
    XEXPECT(lx_sounds(3, 62), "pattern 2: the loop's D at bar 2, as recorded");
    li_until(20);
    pat_switch(0);
    li_until(32);
    lx_settle();
    XEXPECT(pat_cur == 0u && !lx_sounds(3, 60) && !lx_sounds(3, 62), "pattern 1 in at bar 3: the loop not with it");
    XEXPECT(!prj_save(2), "(session 3 saved, at pattern 1)");
    XEXPECT(!prj_load(2) && pat_cur == 0u && !lp_get(3, 0) && lp_at(1, 3, 0) && lp_at(1, 3, 0)->ev[0].note == 62 &&
            lp_at(1, 3, 0)->ref == 2, "loaded: the loop pattern 2's, at its transposition (D, at +2)");
    lp = lp_at(1, 3, 0);
    btn(B_PLAY, 0);
    {
        int32_t out[2 * CTL];
        for (i = 0; i < 8u; i++)
            mix_block(out);
    }
    pat_switch(1);
    XEXPECT(lp_get(3, 0) == lp && lp_note(lp, lp->ev[0].note) == 62, "and pattern 2 from there: D");
    /* (build 69) the session loaded at a pattern: the grid's cursor (white) on it, not on pattern 1 */
    {
        int32_t out[2 * CTL];
        for (i = 0; i < 8u; i++)
            mix_block(out);
        pat_switch(6);
        XEXPECT(pat_cur == 6u && !prj_save(4), "(session 5 saved at pattern 7)");
        pj.slot[1] = 0;
        pat_switch(0);
        pat_poll();
        XEXPECT(!prj_load(4) && pat_cur == 6u, "(session 5 loaded: pattern 7 playing)");
        pat_poll();
        grid_open();
        XEXPECT(ui.page == PG_PAT && pj.slot[1] == 6u, "the grid: its cursor on pattern 7, the one playing");
        ui_draw();
        shot(dir, "pat_grid_loaded7");
        btn(B_SAVE, 0);
    }
    printf("lptrn: %d failures\n", fails);
    return fails != 0;
}

/* (build 72) a pattern saved holds every track's steps: a track locked on every step no longer leaves the tracks
 * after it empty (the locks share the room left); the root's keys dim in the track's view; the grid's white key held
 * saves the pattern playing there */
#define WEXPECT(c, what) do { if (!(c)) { printf("FAIL patsave: %s\n", what); fails++; } } while (0)
static int cmd_patsave(void)
{
    static seqbuf_t w;
    uint32_t t, i, j, k, kw[16], all = 1, lk = 0;
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    for (k = 0; k < NKEY; k++)
        if (white_ix(k) >= 0)
            kw[white_ix(k)] = k;
    for (t = 0; t < NTRACK; t++)                 /* (track 1 a note on every step, locked 8 times on each; the others
                                                  * every other step) */
        for (i = 0; i < NSTEP; i++) {
            seq_note[t][i] = (uint8_t)(t && (i & 1u) ? 0u : 40u + t * 3u + i);
            for (j = 0; t == 0u && j < NPL; j++) {
                seq_lid[0][i][j] = (uint8_t)j;
                seq_lv[0][i][j] = (int16_t)(10 + j);
            }
        }
    pat_take(&pat_store[2]);
    pat_decode(&pat_store[2], &w);
    for (t = 0; t < NTRACK; t++)
        for (i = 0; i < NSTEP; i++) {
            all &= w.note[t][i] == seq_note[t][i];
            for (j = 0; j < NPL; j++)
                lk += w.lid[t][i][j] != PL_NONE;
        }
    WEXPECT(all, "track 1 locked on every step: every track's steps saved");
    WEXPECT(lk > 0u && w.lid[0][NSTEP - 1u][0] == seq_lid[0][NSTEP - 1u][0], "the locks share the room left, the "
            "last step's too");
    for (t = 1; t < NTRACK; t++)                 /* (every step of every track a note: 96, each still there) */
        for (i = 1; i < NSTEP; i += 2u)
            seq_note[t][i] = (uint8_t)(60u + t);
    pat_take(&pat_store[3]);
    pat_decode(&pat_store[3], &w);
    for (t = 0, all = 1; t < NTRACK; t++)
        for (i = 0; i < NSTEP; i++)
            all &= w.note[t][i] == seq_note[t][i];
    WEXPECT(all, "96 steps, track 1 locked on each: all 96 saved");
    /* the grid: a white key held, the pattern playing saved there, all six tracks */
    grid_open();
    key(kw[7], 1);
    fm1_ms += 900;
    ui_input();
    key(kw[7], 0);
    pat_decode(&pat_store[7], &w);
    for (t = 0, all = 1; t < NTRACK; t++)
        for (i = 0; i < NSTEP; i++)
            all &= w.note[t][i] == seq_note[t][i];
    WEXPECT(pat_store[7].used && all && pat_cur == 7u && str_eq(ui.flash_l1, "SAVED"),
            "a white key held on the grid: the pattern playing saved there, every track, and it plays from now on");
    btn(B_GLO, 0);
    /* the track's view: the root's keys dim, the others dark */
    ui.scale = 2;                                /* (MINOR, root D) */
    ui.root = 2;
    ui_leds();
    WEXPECT(host_dim_on(14u + kw[0]) && host_dim_on(14u + kw[SCALE_N[2]]) && !host_dim_on(14u + kw[1]) &&
            !host_dim_on(14u + kw[2]), "a scale: the root's white keys dim");
    ui.scale = 0;                                /* (CHROMATIC: the key whose note is D) */
    ui_leds();
    for (k = 0, all = 0, j = 0; k < NKEY; k++)
        if (key_plays(k) && host_dim_on(14u + k)) {
            all++;
            j |= (key_note(k, 0) + 120u - ui.root) % 12u != 0u;
        }
    WEXPECT(all > 0u && !j, "chromatic: the keys dim the root's alone");
    {   /* saved into another slot, it plays from then on; the one left keeps its own mutes -- stopped and playing */
        static int32_t out[CTL * 2];
        uint32_t n, run;
        for (run = 0; run < 2u; run++) {
            seq_run = 0;
            pat_switch(0);
            memset((void *)trk_mute, 0, NTRACK);
            trk_mute[1] = 1;
            pat_save_as(0);                      /* (pattern 1: track 2 muted) */
            trk_mute[1] = 0;                     /* (track 2 unmuted, then saved as pattern 2) */
            pat_save_as(1);
            WEXPECT(pat_cur == 1u && !pat_dirty && !trk_mute[1], "saved into pattern 2: pattern 2 plays, as it was saved");
            seq_run = (uint8_t)run;
            pat_switch(0);
            for (n = 0; n < 4000u && seq_pend; n++) {
                mix_block(out);
                pat_poll();
            }
            WEXPECT(pat_cur == 0u && trk_mute[1], run ? "playing, back to pattern 1: track 2 muted again, as it was saved"
                                                      : "stopped, back to pattern 1: track 2 muted again, as it was saved");
            pat_switch(1);
            for (n = 0; n < 4000u && seq_pend; n++) {
                mix_block(out);
                pat_poll();
            }
            WEXPECT(pat_cur == 1u && !trk_mute[1], "and pattern 2: track 2 playing");
        }
        seq_run = 0;
        memset((void *)trk_mute, 0, NTRACK);
    }
    printf("patsave: %d failures\n", fails);
    return fails != 0;
}
/* (build 79) subtracks: a DRUM track's ST-B .. ST-D on the NXL shared lanes, each a track in its own right */

/* the SESSIONS grid (SHIFT + SAVE): as the PATTERNS', a white key let go soon loads its session, held saves the one
 * playing there; CTRL ALL (the session's page) in PERFORMANCE: KNOBS, the macros alone; ALL, a phrase and an EDIT turn
 * on every track with notes; a pattern kept while PERFORMANCE is on stays as it goes off */
#define GEXPECT(c, what) do { if (!(c)) { printf("FAIL sesgrid: %s\n", what); fails++; } } while (0)
static void hold(uint32_t k) { key(k, 1); fm1_ms += 900; ui_input(); key(k, 0); }
static int cmd_sesgrid(const char *dir)
{
    static seqbuf_t w;
    uint32_t k, kw[16], i, n1 = 0, n4 = 0, ch2 = 0, ch5 = 0;
    uint8_t keep[NSTEP];
    int16_t v2;
    memset(host_flash, 0xFF, sizeof host_flash);
    prj_scan(1);
    tracks_init();
    ui_init();
    fm1_ms = 5000;
    for (k = 0; k < NKEY; k++)
        if (white_ix(k) >= 0 && white_ix(k) < 16)
            kw[white_ix(k)] = k;
    ui.tempo = 133;
    seq_note[0][0] = 60;
    btn(B_SAVE, 1);
    GEXPECT(ui.page == PG_PRJ, "SHIFT + SAVE: the SESSIONS grid");
    hold(kw[2]);
    GEXPECT(prj_has[2] && prj_cur == 2 && ui.page == PG_PRJ, "a white key held: the session playing saved there");
    ui.tempo = 99;
    hold(kw[5]);
    GEXPECT(prj_has[5] && prj_cur == 5, "another held: saved there too");
    fm1_ms += 3000;                              /* (the SAVED flash gone) */
    ui_leds();
    GEXPECT(host_led[14u + kw[5]] && !host_led[14u + kw[2]] && host_dim_on(14u + kw[2]) && host_dim_on(14u + kw[9]),
            "the white keys: the page's sessions dim, the one playing lit");
    ui_draw();
    shot(dir, "sessions_grid");
    tap(kw[2]);
    GEXPECT(prj_cur == 2 && ui.tempo == 133 && ui.page == PG_PRJ, "a white key let go soon: its session loaded at once");
    tap(kw[9]);
    GEXPECT(prj_cur == 2 && str_eq(ui.msg, "EMPTY"), "an empty slot: nothing loaded");
    ui.tempo = 140;
    prj_dirty = 1;
    tap(kw[5]);
    GEXPECT(prj_cur == 2 && ui.tempo == 140, "changes not saved: the first press warns");
    tap(kw[5]);
    GEXPECT(prj_cur == 5 && ui.tempo == 99, "the same key again: loaded");
    turn(EN_PRESET, 20);
    GEXPECT(pj.slot[0] == PRJ_SLOTS - 1u, "PRESETS through the slots");
    /* CTRL ALL on the session's page */
    turn(EN_PRESET, -20);
    btn(B_SAVE, 0);
    turn(EN_PRESET, 20);
    GEXPECT(ui.page == PG_PRJD && pj.row == PR_CTRL, "the session's page: CTRL ALL its last row");
    ui_draw();
    shot(dir, "session_ctrl_all");
    btn(B_SAVE, 0);
    turn(EN_PRESET, 1);
    btn(B_SAVE, 0);
    GEXPECT(pnl[PN_CTRL_ALL] == 1u, "CTRL ALL set to ALL");
    btn(B_SAVE, 1);
    btn(B_SAVE, 1);
    ui.page = PG_MAIN;
    /* PERFORMANCE with CTRL ALL: a phrase on every track with notes, each its own; empty tracks stay empty */
    for (k = 0; k < NTRACK; k++)
        memset((void *)seq_note[k], 0, NSTEP);
    seq_note[0][0] = 60;
    seq_note[2][4] = 50;
    seq_note[5][8] = 40;
    memcpy(w.note, (const void *)seq_note, sizeof w.note);
    ui.sel = 0;
    sel_sync();
    tap(KEY_PERF);
    GEXPECT(perf_on, "PERFORMANCE on");
    btn(B_GLO, 0);
    ui.sv = 1;
    tap(KEY_GEN);
    for (i = 0; i < NSTEP; i++) {
        n1 += seq_note[1][i] != 0;
        n4 += seq_note[4][i] != 0;
        ch2 += seq_note[2][i] != w.note[2][i];
        ch5 += seq_note[5][i] != w.note[5][i];
    }
    GEXPECT(ch2 && ch5 && !n1 && !n4, "ALL: a new phrase on each track with notes, none on the empty ones");
    ui.sv = 0;
    ui.page = PG_EDIT;
    ui.edit_pg = 2;                              /* (WRECK: FOLD first) */
    ui.half = ui.ehalf = 0;
    v2 = ui.snd[2][ui.eng[2]].syn[P_WRECK];
    host_enc[PANEL_ENC[EN_K1]] = 10;
    ui_input();
    GEXPECT(ui.snd[2][ui.eng[2]].syn[P_WRECK] != v2, "ALL: an EDIT turn reaches the other tracks with notes");
    /* a pattern kept while it is on: saved, and still there after; the pattern before given back */
    ui.page = PG_MAIN;
    grid_open();
    GEXPECT(ui.page == PG_PAT && perf_on, "PERFORMANCE: the PATTERNS grid opens");
    memcpy(keep, (const void *)seq_note[2], NSTEP);
    ui.snd[2][ui.eng[2]].syn[P_WRECK + 1u] = 91;     /* (a sound as it plays when kept .. */
    pnl[PN_FX_MODE] = 2;
    hold(kw[6]);
    ui.snd[2][ui.eng[2]].syn[P_WRECK + 1u] = 17;     /* .. and changed after) */
    GEXPECT(pat_store[6].used && perf_on, "a white key held: what plays kept as pattern 7");
    tap(kw[6]);
    GEXPECT(pat_cur == 0u, "PERFORMANCE: the pattern does not switch");
    tap(KEY_PERF);
    GEXPECT(!perf_on && pat_cur == 0u && seq_note[2][4] == 50 && seq_note[0][0] == 60, "off: the pattern before, as it was");
    pat_decode(&pat_store[6], &w);
    GEXPECT(!memcmp(w.note[2], keep, NSTEP) && prj_dirty, "off: pattern 7 keeps what was played");
    GEXPECT(ui.snd[2][ui.eng[2]].syn[P_WRECK + 1u] == 91 && pnl[PN_FX_MODE] == 2,
            "off: the session keeps the sounds and effects as they were when the pattern was kept, not after");
    /* kept over the pattern it began on: as it goes off, that pattern as kept */
    tap(KEY_PERF);
    seq_note[3][1] = 77;
    hold(kw[0]);
    tap(KEY_PERF);
    GEXPECT(!perf_on && pat_cur == 0u && seq_note[3][1] == 77 && !pat_dirty, "kept over the pattern it began on: "
            "back to it as kept");
    btn(B_SAVE, 0);
    printf("sesgrid: %d failures\n", fails);
    return fails != 0;
}

#include "engine_bridge_tests.c"
int main(int argc,char **argv)
{
    if(argc<2) return 2;
    if(!strcmp(argv[1],"bridge"))return cmd_bridge();
    if(!strcmp(argv[1],"levels"))return cmd_levels();
    if(!strcmp(argv[1],"render"))return cmd_render(argc>2?argv[2]:"build/sounds",argc>3?(uint32_t)atoi(argv[3]):2u);
    if(!strcmp(argv[1],"screens"))return cmd_native_screens(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"bkserve"))return cmd_bkserve();
    if(!strcmp(argv[1],"patsave"))return cmd_patsave();
    if(!strcmp(argv[1],"lfo"))return cmd_lfo(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"trn"))return cmd_trn(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"arp"))return cmd_arp(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"newses"))return cmd_newses(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"pstate"))return cmd_pstate(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"brksave"))return cmd_brksave(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"loopin"))return cmd_loopin(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"lptrn"))return cmd_lptrn(argc>2?argv[2]:"build/screens");
    if(!strcmp(argv[1],"demo"))return cmd_demo(argc>2?argv[2]:"build/demo");
    if(!strcmp(argv[1],"sesgrid"))return cmd_sesgrid(argc>2?argv[2]:"build/screens");
    return 2;
}
