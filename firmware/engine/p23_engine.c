/* SPDX-License-Identifier: GPL-3.0-only
 * Pulsar23 engine, build 3: the whole instrument as a virtual panel.
 *
 * The Pulsar-23 is a semi-modular system of 23 modules and 13 auxiliary units on a common 0 .. 10 V
 * scale. This file models each of them as a permanent circuit with pins:
 *   master clock + binary divider array, a looper-recorder channel per track (virtual tape, 128 ticks
 *   x 96), an AR envelope generator per module, the BD / BASS / SD / HHT circuits, SHAOS (one 8-stage
 *   shift register, three feedback paths), LFO, the two-channel clock-modulated FX processor, parallel
 *   distortion, VCAs, inverters, switches, attenuators, pulse converters, MIDI-CV, touch CV, noise,
 *   +10 V / GND, a diode and two capacitors.
 * Cables join pins into nets. A net mixes the outputs on it (passive mixing; GND and +10 V are stiff);
 * envelope trigger inputs follow the strongest voltage. Every net is evaluated from the previous
 * sample's outputs, so feedback through any patch has one sample of delay, independent of order.
 * Signals are unipolar about a +5 V rail as on the hardware: module outputs carry a +5 V bias, audio
 * inputs (EXT, MIX IN, FX inputs) are AC-coupled.
 *
 * Written from the SOMA manual, the Synthdawg notebook and public descriptions; no code, tables or
 * DSP from Bigorna. Circuit values the sources do not give are this engine's choices (docs/ENGINE.md).
 */
#include "p23_engine.h"
#include "p23_math.h"
#ifdef P23_HOST
#define PE_MEMORY
#define PE_HOT
#define PE_FAST
#else
#define PE_MEMORY __attribute__((section(".pool")))
#define PE_HOT __attribute__((section(".fast_text"),noinline))
#define PE_FAST __attribute__((section(".fast_text")))   /* (what the audio calls runs from RAM too: no far calls) */
#endif
/* once-a-block code runs from flash (XIP, cached), leaving RAM to the per-sample code: it calls only flash code
 * and inline helpers (PE_BOTH: an inline body with a RAM copy and a flash copy) */
#define PE_BLOCK

#define PE_V1 3277
#define PE_V5 16384
#define PE_V10 32768
#define PE_NETS 32u
#define PE_MEMBERS 64u
#define PE_NONE 255u
#define PE_HOLD 16u
#define PE_LR_LEN (P23_LR_TICKS*P23_LR_SUB)
#define PE_LR_BYTES (PE_LR_LEN/4u)
#define PE_SUB256 (P23_LR_SUB*256u)
#define PE_LOOP256 (PE_LR_LEN*256u)
/* FX memory: 32K words, as the delay RAM of a small effects DSP */
#define PE_FX_WORDS 32768u
#define PE_DLY_LEN 19456u
#define PE_DPS_BASE 19456u
#define PE_DPS_LEN 2048u
#define PE_REV_BASE 21504u
#define PE_RPS_BASE 31232u
#define PE_RPS_LEN 1536u
#define PE_FX_RATE 32768u
#define PE_KS_LEN 2048u            /* each node's line (PLUCK's string, WIRE's comb): down to 21.5 Hz */
#define PE_RSRC 12u                /* route sources recorded a block at a time */
#define PE_OWN_OUT 254u
#define PE_OWN_ENV 253u
#define PE_LIVE 252u
#define PE_FX_QUIET 6200u          /* blocks (4.5 s) of FX silence, beyond its longest repeat, before it rests */

enum { PF_DRV=1, PF_LST=2, PF_STRONG=4 };
enum { XF_DRIVE=1, XF_FOLD=2, XF_RING=4, XF_CRUSH=8, XF_BITS=16 };   /* (TRANSFORM's effects, in their order) */
/* the utility circuits with a pin patched (pe.util): only those are computed */
enum { PU_VCA1=1, PU_VCA2=2, PU_INV=4, PU_CINV=8, PU_SW1=16, PU_SW2=32, PU_ATT=64, PU_IMP=128, PU_DIODE=256,
       PU_CAP=512, PU_SRC=1024, PU_SHAOS=2048, PU_LFO=4096 };

typedef struct { int32_t low,band; } pe_svf;
typedef struct { int32_t a1,a2,a3,k; } pe_coef;   /* (a filter's prepared coefficients, Q16) */
typedef struct { uint32_t stamp; uint8_t note,vel,source,on; } pe_hold;
typedef struct {
    p23_patch patch;
    p23_route route;
    pe_hold hold[PE_HOLD];
    int32_t note_gate,gate,env,trig_prev;
    uint32_t att_rate,rel_k,serial_note;
    uint8_t note,velocity,strike,clap_ok,gate_high,lr_prev;
    /* circuits */
    uint32_t phase,rnd,noise_phase,clap_age;
    int32_t tri,dir,punch,y1,y2,pitch256,glide256,bend,ext_lp,hold_noise,nlp,step,drive,warp0;
    pe_svf filter,res[3];
    /* the further modes: oscillator banks, a grain / index / excitation level, the string or comb's line */
    uint32_t ph[6],minc[6],klen256;
    uint32_t wph,winc; int32_t whold,wgain; uint8_t wcnt,wn;   /* (the WRECK stage) */
    int32_t amp,kprev;
    uint16_t kpos;
    int32_t raw,out,peak,level_gain,mute_gain,gl,gr,gs,dgl,dgr,dgs,tl,tr,ts;
    uint32_t base256,inc,rcp,mods,rmask,now;
    /* the sound's routes, once a sample: each destination's sum; the sources' recordings (or own OUT / ENV) */
    int32_t rv[P23_RDESTS],rdep[P23_ROUTES];
    uint8_t rsel[P23_ROUTES],rdst[P23_ROUTES],nr,cabled,own_out,peaked;
    const int32_t *eb;             /* (the block's envelope while it renders: own ENV routes follow it) */
    /* the voice's overdrive: pre-emphasis, an asymmetric cubic clipper whose bias sags with level, de-emphasis */
    int32_t d_lp,d_post,d_sag,d_g,d_comp,d_pole,d_emph,d_mix;
    int32_t np,rd,pk;
    pe_coef fc,rc[3];
    /* TRANSFORM's stage: its mix (the block's start, end), ring phase, crush hold */
    int32_t xm0,xm,xhold;
    uint32_t xph;
    uint8_t xcnt;
} pe_node;
typedef struct {
    uint32_t pos,target,since,period;
    uint8_t clk_hi,mode,add,del,last_level,sh_out;
} pe_lr;
typedef struct {
    int16_t mem[PE_FX_WORDS];
    uint32_t phase,inc,shift_a,shift_b,rnd,ticks,d1,d2;
    uint16_t p_dly,p_dps,p_rps,p_ap[8],p_line[2];
    int32_t in_lp[2],held[2],out_lp[2][2],bp_low,bp_band,time_s,tune_s,fb_s,damp[2],mad_env;
    int32_t dly_out,rev_out,dly_r,rev_r,tank[2],clip,pole,cg,ratio,ratio_r;
    uint8_t mad;
} pe_fx;

static struct {
    pe_node node[P23_NODES];
    p23_panel panel;
    p23_cable cables[P23_CABLES];
    /* patchbay */
    int32_t drive[P23_PINS],seen_v[P23_PINS];
    uint8_t live[P23_PINS],seen_w[P23_PINS],flags[P23_PINS],net_of[P23_PINS];
    uint8_t member[PE_MEMBERS],net_first[PE_NETS],net_count[PE_NETS],net_lp[PE_NETS],nets,built,cap_shunt[2];
    int32_t net_s[PE_NETS],net_w[PE_NETS],net_max[PE_NETS],net_clamp[PE_NETS],net_lpv[PE_NETS];
    /* clock and looper */
    uint32_t clk_phase,clk_inc256,clk_since,clk_period,samples;
    uint8_t clk_count,clk_in_hi,clk_edge,clk_led,lrst_hi,lr_run,bank,copy_from,copying,vel_low,vel_mid;
    pe_lr lr[P23_TRACKS];
    int32_t lr_v[P23_TRACKS];      /* (each channel's output, kept up to date as the tape and the sensors change) */
    uint8_t tape[P23_LR_BANKS][P23_TRACKS][PE_LR_BYTES];
    uint8_t scratch[PE_LR_BYTES];
    /* SHAOS, LFO, utilities */
    uint32_t sh_phase,sh_steps,sh_inc,lfo_inc,clk_inc,clk_sub,util,tpins,tput,pink_on,lfo_phase,lfo_split,lfo_up,lfo_down;
    uint8_t sh_reg,sh_mark,sh_ck_hi,sh_sh_hi,sh_1,sh_3,lfo_sync_hi,sw_on[2],cinv_hi;
    int32_t imp_lp[2],cap_lp[2][2],mix_dc,ext_rnd;
    /* noise */
    uint32_t random,serial;
    int32_t pink[3],pink_out,white;
    /* FX, master */
    pe_fx fx;
    int32_t dist_dc[2],out_dc[2],dist_acc[2],out_acc[2],usb[P23_BLOCK*2],click;
    uint32_t click_phase,click_step;
    uint16_t master;
    uint32_t diag;                 /* (p23_diagnostic: sections left out, to measure their cost on the target) */
    /* the block's buffers: noise, the nodes rendered a block at a time, and the tracks' OUT / ENV for patched pins */
    uint32_t slow,todo[2],fx_quiet[2],util_rt,util_ro,adv;
    uint32_t rneed;
    uint16_t rtp[PE_RSRC],rub[PE_RSRC];
    uint8_t lr_need,fx_on,nrsrc,rsrc[PE_RSRC],rbias[PE_RSRC];
    int32_t wbuf[P23_BLOCK],pbuf[P23_BLOCK],bl[2][P23_BLOCK],br[2][P23_BLOCK],bs[2][P23_BLOCK];
    int32_t tout[2][P23_TRACKS][P23_BLOCK],tenv[2][P23_TRACKS][P23_BLOCK];
    /* what the routes read, as it was through the last block (written by the sample loop, read by the voices) */
    int32_t rec[PE_RSRC][P23_BLOCK];
    int16_t ks[P23_NODES][PE_KS_LEN];
    /* TRANSFORM: its settings, held or not, the stack drawn at the hold (each effect's draw xf_u, the ring's
     * frequency's), and what they set this block */
    p23_transform xf;
    uint32_t xf_rnd,xf_inc;
    int32_t xf_gd,xf_gf,xf_ring,xf_mask,xf_half;
    uint8_t xf_hold,xf_on,xf_set,xf_every,xf_u[6];
} pe PE_MEMORY;

static void pe_zero(void *memory,uint32_t count)
{ uint8_t *p=(uint8_t *)memory; while(count--) *p++=0; }
PM_INLINE int32_t pe_volts(uint32_t v127) { return (int32_t)v127*258; }
/* one-pole followers with 8 extra bits of state, so slow corners do not stall on rounding */
PM_INLINE int32_t pe_lp(int32_t *s256,int32_t x,int32_t k)
{ *s256+=(int32_t)(((int64_t)(x*256-*s256)*k)>>15); return pm_sdiv(*s256,8); }
PM_INLINE int32_t pe_hp(int32_t *s256,int32_t x,int32_t k) { return x-pe_lp(s256,x,k); }

/* ---------------------------------------------------------------- pins --- */
static uint32_t pe_pin_flags(uint32_t pin)
{
    if(pin==P23_PIN_NONE || pin>=P23_PINS) return 0;
    if(pin<P23_PIN_CLK) {
        uint32_t k=(pin-1u)%P23_TRACK_PINS;
        return k==P23_T_OUT || k==P23_T_ENV ? PF_DRV : k==P23_T_TRIG ? PF_DRV|PF_LST : PF_LST;
    }
    switch(pin) {
    case P23_PIN_CLK: case P23_PIN_SH_DATA:
    case P23_PIN_SW1_IN: case P23_PIN_SW1_OUT: case P23_PIN_SW2_IN: case P23_PIN_SW2_OUT:
    case P23_PIN_DIODE_A: case P23_PIN_DIODE_K:
    case P23_PIN_CAP1_A: case P23_PIN_CAP1_B: case P23_PIN_CAP2_A: case P23_PIN_CAP2_B:
        return PF_DRV|PF_LST;
    case P23_PIN_P10V: case P23_PIN_GND: return PF_DRV|PF_STRONG;
    case P23_PIN_DIV16: case P23_PIN_DIV8: case P23_PIN_DIV4: case P23_PIN_DIV2: case P23_PIN_DIV1:
    case P23_PIN_DIV05: case P23_PIN_DIV025: case P23_PIN_SH_1DIR: case P23_PIN_SH_2DIR: case P23_PIN_SH_1SH:
    case P23_PIN_SH_3SH: case P23_PIN_LFO_SQR: case P23_PIN_LFO_TRI: case P23_PIN_FX_DLY_OUT: case P23_PIN_FX_REV_OUT:
    case P23_PIN_VCA1_OUT: case P23_PIN_VCA2_OUT: case P23_PIN_INV_OUT: case P23_PIN_CINV_OUT:
    case P23_PIN_ATT1_OUT: case P23_PIN_ATT2_OUT: case P23_PIN_ATT3_OUT: case P23_PIN_ATT4_OUT:
    case P23_PIN_IMP1_OUT: case P23_PIN_IMP2_OUT: case P23_PIN_MCV1: case P23_PIN_MCV2: case P23_PIN_MCV3:
    case P23_PIN_MCV4: case P23_PIN_NOISE: case P23_PIN_TOUCH1: case P23_PIN_TOUCH2:
        return PF_DRV;
    default: return PF_LST;
    }
}
PM_INLINE void pe_put(uint32_t pin,int32_t v) { pe.drive[pin]=pm_bound(v,-65536,65536); }
PM_INLINE void pe_live(uint32_t pin,uint32_t on)
{ pe.live[pin]=(uint8_t)(on?((pe.flags[pin]&PF_STRONG)?8u:1u):0u); }
/* the mean of the other outputs on a pin's net (0 V: nothing drives it) */
PM_INLINE int32_t pe_in(uint32_t pin)
{
    uint32_t n=pe.net_of[pin]; int32_t w,s,v;
    if(n>=PE_NETS) return 0;
    if(pe.net_lp[n]) return pm_sdiv(pe.net_lpv[n],8);
    w=pe.net_w[n]-pe.seen_w[pin];
    if(w<=0) return 0;
    s=pe.net_s[n]-(int32_t)pe.seen_w[pin]*pe.seen_v[pin];
    v=w==1?s:s/w;
    return v<pe.net_clamp[n]?v:pe.net_clamp[n];
}
PM_INLINE int pe_driven(uint32_t pin)
{ uint32_t n=pe.net_of[pin]; return n<PE_NETS && pe.net_w[n]-(int32_t)pe.seen_w[pin]>0; }
/* envelope trigger inputs: the strongest voltage on the net, theirs included */
PM_INLINE int32_t pe_in_max(uint32_t pin,int32_t own)
{
    uint32_t n=pe.net_of[pin]; int32_t v;
    if(n>=PE_NETS || pe.net_w[n]<=0) return own;
    v=pe.net_max[n]; if(v>pe.net_clamp[n]) v=pe.net_clamp[n];
    return v>own?v:own;
}
/* CV inputs: the hardware's useful range is 0 .. 10 V; protected inputs pass a little beyond */
PM_INLINE int32_t pe_cv(uint32_t pin) { return pm_bound(pe_in(pin),-PE_V5,PE_V10+PE_V5); }
static PE_FAST void pe_nets(void)
{
    uint32_t n,j;
    for(n=0;n<pe.nets;n++) {
        int32_t s=0,w=0,mx=-65536;
        for(j=0;j<pe.net_count[n];j++) {
            uint32_t p=pe.member[pe.net_first[n]+j]; int32_t v=pe.drive[p],l=pe.live[p];
            pe.seen_v[p]=v; pe.seen_w[p]=(uint8_t)l;
            if(l) { s+=l*v; w+=l; if(v>mx) mx=v; }
        }
        pe.net_s[n]=s; pe.net_w[n]=w; pe.net_max[n]=w?mx:0;
        if(pe.net_lp[n]) {
            /* a capacitor to GND: the net is an RC low-pass (0.1 uF / 10 uF against ~4.7 k) */
            int32_t v=w?(w==1?s:s/w):0;
            pe_lp(&pe.net_lpv[n],v,pe.net_lp[n]==1?1580:16);
        }
    }
}
void p23_set_cables(const p23_cable cables[P23_CABLES])
{
    uint8_t parent[P23_PINS],root_net[P23_PINS],patched[P23_PINS];
    uint32_t i,j,count=0,same=1;
    if(!cables) return;
    for(i=0;i<P23_CABLES;i++) if(cables[i].a!=pe.cables[i].a || cables[i].b!=pe.cables[i].b) same=0;
    if(same && pe.built) return;
    pe.built=1;
    for(i=0;i<P23_CABLES;i++) pe.cables[i]=cables[i];
    for(i=0;i<P23_PINS;i++) { parent[i]=(uint8_t)i; root_net[i]=PE_NONE; patched[i]=0; pe.net_of[i]=PE_NONE; }
    for(i=0;i<P23_CABLES;i++) {
        uint32_t a=cables[i].a,b=cables[i].b;
        if(a==P23_PIN_NONE || b==P23_PIN_NONE || a>=P23_PINS || b>=P23_PINS || a==b) continue;
        patched[a]=patched[b]=1;
        while(parent[a]!=a) a=parent[a];
        while(parent[b]!=b) b=parent[b];
        if(a!=b) parent[a<b?b:a]=(uint8_t)(a<b?a:b);
    }
    pe.nets=0;
    for(i=1;i<P23_PINS;i++) {
        uint32_t r=i;
        if(!patched[i]) continue;
        while(parent[r]!=r) r=parent[r];
        if(root_net[r]==PE_NONE) { if(pe.nets>=PE_NETS) continue; root_net[r]=pe.nets++; }
        pe.net_of[i]=root_net[r];
    }
    for(j=0;j<pe.nets;j++) {
        pe.net_first[j]=(uint8_t)count; pe.net_count[j]=0; pe.net_lp[j]=0;
        pe.net_clamp[j]=0x7fffffff; pe.net_lpv[j]=0;
        for(i=1;i<P23_PINS && count<PE_MEMBERS;i++) if(pe.net_of[i]==j) { pe.member[count++]=(uint8_t)i; pe.net_count[j]++; }
    }
    /* a capacitor with one side on GND makes the other side's net an RC low-pass */
    for(j=0;j<2;j++) {
        uint32_t a=j?P23_PIN_CAP2_A:P23_PIN_CAP1_A,b=a+1u,na=pe.net_of[a],nb=pe.net_of[b],ng=pe.net_of[P23_PIN_GND];
        pe.cap_shunt[j]=0;
        if(ng>=PE_NETS) continue;
        if(na==ng && nb<PE_NETS && nb!=ng) { pe.net_lp[nb]=(uint8_t)(1u+j); pe.cap_shunt[j]=1; }
        if(nb==ng && na<PE_NETS && na!=ng) { pe.net_lp[na]=(uint8_t)(1u+j); pe.cap_shunt[j]=1; }
    }
}
int32_t p23_pin_value(uint32_t pin)
{
    uint32_t n;
    if(pin==P23_PIN_NONE || pin>=P23_PINS) return 0;
    n=pe.net_of[pin];
    if(n<PE_NETS && pe.net_w[n]>0) return pe.net_lp[n]?pm_sdiv(pe.net_lpv[n],8):pe.net_s[n]/pe.net_w[n];
    return (pe.flags[pin]&PF_DRV)?pe.drive[pin]:0;
}

/* --------------------------------------------------------------- noise --- */
PM_INLINE void pe_noise(void)
{
    int32_t w=pm_white(&pe.random);
    pe.white=w;
    if(!pe.pink_on) return;
    /* pink-ish: three leaky integrators spaced about two octaves apart, as an analog shaping network */
    pe.pink[0]+=pm_sdiv(w-pe.pink[0],9);
    pe.pink[1]+=(w-pe.pink[1])/48;
    pe.pink[2]+=(w-pe.pink[2])/5;
    pe.pink_out=pm_bound((pe.pink[0]*7+pe.pink[1]*3+pe.pink[2]*2)/3,-PE_V5,PE_V5);
}

/* --------------------------------------------------------------- clock --- */
static PE_FAST void pe_clock(void)
{
    const p23_panel *g=&pe.panel; uint32_t old=pe.clk_phase;
    int ext=g->clock_source==P23_CLK_EXT;
    pe.clk_edge=0;
    if(ext) {
        /* middle position: the CLK pin is the input; rising edges above 3 V */
        int32_t v=pe_driven(P23_PIN_CLK)?pe_in(P23_PIN_CLK):0;
        pe.clk_since++;
        if(!pe.clk_in_hi && v>3*PE_V1) {
            pe.clk_in_hi=1; pe.clk_edge=1;
            pe.clk_period=pe.clk_since<16u?16u:pe.clk_since; pe.clk_since=0;
        } else if(pe.clk_in_hi && v<PE_V1+PE_V1/2) pe.clk_in_hi=0;
        pe.clk_inc256=pe.clk_since>2u*pe.clk_period || pe.clk_since>5u*P23_RATE ? 0u : PE_SUB256/pe.clk_period;
    } else if(g->clock_source==P23_CLK_INT && g->clock_run) {
        pe.clk_phase+=pe.clk_inc;
        pe.clk_inc256=pe.clk_sub;
        if(pe.clk_phase<old) pe.clk_edge=1;
        pe_put(P23_PIN_CLK,pe.clk_phase<0x80000000u?PE_V10:0);
    } else { pe.clk_inc256=0; pe_put(P23_PIN_CLK,0); }
    if(pe.clk_edge) {
        /* a 7-stage binary divider: each output high for the first half of its cycle */
        uint32_t k;
        pe.clk_count=(uint8_t)((pe.clk_count+1u)&127u);
        pe.clk_led=(uint8_t)(!(pe.clk_count&7u)?(pe.clk_count?1u:2u):0u);
        for(k=0;k<7;k++) pe_put(P23_PIN_DIV16+k,(pe.clk_count>>k)&1u?0:PE_V10);
    }
}

/* ----------------------------------------------------- looper-recorder --- */
PM_INLINE uint32_t pe_tape_get(const uint8_t *t,uint32_t cell) { return (t[cell>>2]>>((cell&3u)*2u))&3u; }
PM_INLINE void pe_tape_set(uint8_t *t,uint32_t cell,uint32_t v)
{ uint32_t s=(cell&3u)*2u; t[cell>>2]=(uint8_t)((t[cell>>2]&~(3u<<s))|((v&3u)<<s)); }
PM_INLINE int32_t pe_lr_volts(uint32_t level)
{ return level==3u?PE_V10:level==2u?7*PE_V1:level==1u?4*PE_V1:0; }
PM_INLINE uint32_t pe_sensor_level(void)
{ return pe.vel_low&&pe.vel_mid?3u:pe.vel_mid?2u:pe.vel_low?1u:3u; }
PM_INLINE void pe_lr_restart(uint32_t pos256)
{
    uint32_t c;
    for(c=0;c<P23_TRACKS;c++) {
        pe.lr[c].pos=pos256%PE_LOOP256;
        pe.lr[c].target=pe.lr[c].pos-pe.lr[c].pos%PE_SUB256+PE_SUB256;
    }
}
static PE_FAST void pe_lr_cell(uint32_t c,uint32_t cell)
{
    pe_lr *l=&pe.lr[c]; uint8_t *t=pe.tape[pe.bank][c];
    uint32_t live=l->add?pe_sensor_level():0u;
    if(pe.copying && l->mode==P23_LR_REC && pe.copy_from!=pe.bank)
        pe_tape_set(t,cell,pe_tape_get(pe.tape[pe.copy_from][c],cell));
    if(l->mode==P23_LR_REC) {
        uint32_t v=pe_tape_get(t,cell);
        if(l->del) v=live;               /* DEL erases; DEL + ADD punches in */
        else if(live>v) v=live;          /* ADD overdubs */
        pe_tape_set(t,cell,v);
    }
    l->last_level=(uint8_t)pe_tape_get(t,cell);
}
/* a channel's output: the tape (unless muted or held off by DEL in PLAY), merged with ADD held */
PM_INLINE int32_t pe_lr_out_body(uint32_t c)
{
    pe_lr *l=&pe.lr[c]; int32_t tape=0,live;
    if(l->mode!=P23_LR_MUTE && pe.lr_run && l->last_level && !(l->del && l->mode==P23_LR_PLAY)) {
        tape=pe_lr_volts(l->last_level);
        /* L / M held in PLAY re-scale the recorded notes while held */
        if(l->mode==P23_LR_PLAY && (pe.vel_low || pe.vel_mid)) tape=pm_product(tape,pe_lr_volts(pe_sensor_level()));
    }
    live=l->add?pe_lr_volts(pe_sensor_level()):0;
    return tape>live?tape:live;
}
static PE_FAST int32_t pe_lr_out(uint32_t c) { return pe_lr_out_body(c); }
static PE_BLOCK int32_t pe_lr_out_x(uint32_t c) { return pe_lr_out_body(c); }
static PE_FAST void pe_looper(void)
{
    uint32_t c;
    if(pe.net_of[P23_PIN_LRST]<PE_NETS) {
        int32_t v=pe_driven(P23_PIN_LRST)?pe_in(P23_PIN_LRST):0;
        if(!pe.lrst_hi && v>2*PE_V1) { pe.lrst_hi=1; pe_lr_restart(0); }
        else if(pe.lrst_hi && v<PE_V1) pe.lrst_hi=0;
    }
    for(c=0;c<P23_TRACKS;c++) {
        pe_lr *l=&pe.lr[c]; uint32_t inc,edge,old_cell,new_cell,pin=P23_PIN_LR_CLK1+c;
        if(pe.net_of[pin]<PE_NETS && pe_driven(pin)) {
            /* an own clock replaces the master: rising edges above 2 V, upsampled x 96 */
            int32_t x=pe_in(pin);
            edge=0; l->since++;
            if(!l->clk_hi && x>2*PE_V1) { l->clk_hi=1; edge=1; l->period=l->since<16u?16u:l->since; l->since=0; }
            else if(l->clk_hi && x<PE_V1) l->clk_hi=0;
            inc=l->since>2u*l->period || l->since>5u*P23_RATE || !l->period ? 0u : PE_SUB256/l->period;
        } else { edge=pe.clk_edge; inc=pe.clk_inc256; }
        if(!pe.lr_run || (!edge && !inc)) continue;
        old_cell=l->pos>>8;
        if(edge) { l->pos=l->target; l->target+=PE_SUB256; }
        else if(l->pos+inc<l->target) l->pos+=inc;
        else l->pos=l->target-1u;
        if(l->pos>=PE_LOOP256) { l->pos-=PE_LOOP256; l->target-=PE_LOOP256; }
        if(l->target>PE_LOOP256+PE_SUB256) l->target=l->pos-l->pos%PE_SUB256+PE_SUB256;
        new_cell=l->pos>>8;
        if(old_cell!=new_cell) {
            while(old_cell!=new_cell) { old_cell=(old_cell+1u)%PE_LR_LEN; pe_lr_cell(c,old_cell); }
            pe.lr_v[c]=pe_lr_out(c);
        }
    }
}

/* --------------------------------------------------------------- SHAOS --- */
/* One 8-stage shift register (Q1 newest .. Q8 oldest). Feedback: Q1^Q8 (63 steps), Q3^Q8 (217 = 7 x 31),
 * or Q8 inverted: a twisted ring whose 16 steps play normal then inverted, and whose input DATA can
 * overwrite. Outputs are adjacent taps through binary-weighted ladders. */
static PE_FAST void pe_shaos_shift(void)
{
    uint32_t r=pe.sh_reg,fb;
    if(pe.panel.shaos_length==P23_SH_16) {
        fb=((r>>7)&1u)^1u;
        if(pe_driven(P23_PIN_SH_DATA)) fb=pe_in(P23_PIN_SH_DATA)>PE_V5;
    } else if(pe.panel.shaos_length==P23_SH_217) fb=((r>>2)^(r>>7))&1u;
    else fb=(r^(r>>7))&1u;
    r=((r<<1)|fb)&255u;
    if(pe.panel.shaos_length!=P23_SH_16) {
        /* The reducible feedback also has short cycles (all-zero; 3; 7 and 31 for 217). The emulation
         * leaves them as the hardware's noise would, so the published lengths hold. */
        uint32_t len=pe.panel.shaos_length==P23_SH_217?217u:63u;
        if(++pe.sh_steps>=len) { pe.sh_steps=0; pe.sh_mark=(uint8_t)r; }
        else if(r==pe.sh_mark || !r) { r^=1u; pe.sh_steps=0; pe.sh_mark=(uint8_t)r; }
    }
    pe.sh_reg=(uint8_t)r;
}
static PE_FAST void pe_shaos_sample(void) { pe.sh_1=(uint8_t)((pe.sh_reg>>3)&1u); pe.sh_3=(uint8_t)((pe.sh_reg>>4)&7u); }
static PE_FAST void pe_shaos(void)
{
    uint32_t shifted=0;
    if(pe_driven(P23_PIN_SH_CLK)) {
        int32_t v=pe_in(P23_PIN_SH_CLK);
        if(!pe.sh_ck_hi && v>2*PE_V1) { pe.sh_ck_hi=1; shifted=1; }
        else if(pe.sh_ck_hi && v<PE_V1) pe.sh_ck_hi=0;
    } else {
        uint32_t old=pe.sh_phase;
        pe.sh_phase+=pe.sh_inc;
        shifted=pe.sh_phase<old;
    }
    if(shifted) pe_shaos_shift();
    if(pe_driven(P23_PIN_SH_SH)) {
        int32_t v=pe_in(P23_PIN_SH_SH);
        if(!pe.sh_sh_hi && v>2*PE_V1) { pe.sh_sh_hi=1; pe_shaos_sample(); }
        else if(pe.sh_sh_hi && v<PE_V1) pe.sh_sh_hi=0;
    } else if(shifted) pe_shaos_sample();
    pe_put(P23_PIN_SH_1DIR,(pe.sh_reg&1u)?PE_V10:0);
    pe_put(P23_PIN_SH_2DIR,(int32_t)((pe.sh_reg>>1)&3u)*PE_V10/3);
    pe_put(P23_PIN_SH_1SH,pe.sh_1?PE_V10:0);
    pe_put(P23_PIN_SH_3SH,(int32_t)pe.sh_3*PE_V10/7);
    pe_put(P23_PIN_SH_DATA,(pe.sh_reg&128u)?PE_V10:0);
}

/* ----------------------------------------------------------------- LFO --- */
static PE_FAST void pe_lfo(void)
{
    const p23_panel *g=&pe.panel; uint32_t lo,hz256,ph,old;
    int32_t oct,mod;
    if(pe_driven(P23_PIN_LFO_SYNC)) {
        int32_t v=pe_in(P23_PIN_LFO_SYNC);
        if(!pe.lfo_sync_hi && v>2*PE_V1) { pe.lfo_sync_hi=1; pe.lfo_phase=0; }
        else if(pe.lfo_sync_hi && v<PE_V1) pe.lfo_sync_hi=0;
    }
    if(pe_driven(P23_PIN_LFO_MOD) && !(pe.samples&3u)) {
        /* LOW 0.05 .. 13 Hz, MID 0.5 .. 256 Hz, HI 10 Hz .. 5 kHz; MOD x AMT, 5 octaves at 10 V */
        lo=g->lfo_range==P23_LFO_HI?2560u:g->lfo_range==P23_LFO_MID?128u:13u;
        oct=(int32_t)g->lfo_freq*(g->lfo_range==P23_LFO_LOW?4128:4644);
        mod=pe_cv(P23_PIN_LFO_MOD);
        oct+=(int32_t)(((int64_t)mod*g->lfo_amt*10)>>7);
        hz256=pm_hz(lo,oct);
        pe.lfo_inc=pm_frequency(hz256);
    }
    old=pe.lfo_phase; (void)old;
    pe.lfo_phase+=pe.lfo_inc;
    ph=pe.lfo_phase>>16;
    /* TRIANGLE shape: falling saw .. triangle .. rising saw */
    {
        int32_t y=ph<pe.lfo_split?(int32_t)((ph*pe.lfo_up)>>16):PE_V10-(int32_t)(((ph-pe.lfo_split)*pe.lfo_down)>>16);
        pe_put(P23_PIN_LFO_TRI,pm_bound(y,0,PE_V10));
    }
    pe_put(P23_PIN_LFO_SQR,pe.lfo_phase<0x80000000u?PE_V10:0);
}

/* ------------------------------------------------------------ utilities --- */
static PE_FAST void pe_utilities(void)
{
    const p23_panel *g=&pe.panel; uint32_t j,u=pe.util;
    if(u&PU_VCA1) pe_put(P23_PIN_VCA1_OUT,pm_product(pe_in(P23_PIN_VCA1_IN),pm_bound(pe_in(P23_PIN_VCA1_CV),0,PE_V10)));
    if(u&PU_VCA2) pe_put(P23_PIN_VCA2_OUT,pm_product(pe_in(P23_PIN_VCA2_IN),pm_bound(pe_in(P23_PIN_VCA2_CV),0,PE_V10)));
    /* the inverter mirrors about +5 V */
    if(u&PU_INV) pe_put(P23_PIN_INV_OUT,PE_V10-pe_in(P23_PIN_INV_IN));
    if(u&PU_CINV) {   /* the controlled inverter: binary 0 / 10 V, inverted while CV > 5 V */
        int32_t c=pe_in(P23_PIN_CINV_CV);
        if(c>PE_V5+PE_V1/4) pe.cinv_hi=1; else if(c<PE_V5-PE_V1/4) pe.cinv_hi=0;
        pe_put(P23_PIN_CINV_OUT,((pe_in(P23_PIN_CINV_IN)>PE_V5)^pe.cinv_hi)?PE_V10:0);
    }
    for(j=0;j<2;j++) {
        /* switches: closed above +5 V, then either side drives the other */
        uint32_t in=j?P23_PIN_SW2_IN:P23_PIN_SW1_IN,out=in+2u,cv=in+1u;
        int32_t c;
        if(!(u&(j?PU_SW2:PU_SW1))) continue;
        c=pe_in(cv);
        if(c>PE_V5+PE_V1/4) pe.sw_on[j]=1; else if(c<PE_V5-PE_V1/4) pe.sw_on[j]=0;
        pe_live(out,pe.sw_on[j] && pe_driven(in)); pe_put(out,pe_in(in));
        pe_live(in,pe.sw_on[j] && pe_driven(out)); pe_put(in,pe_in(out));
    }
    for(j=0;j<4u && (u&PU_ATT);j++) {
        /* passive attenuators, log taper */
        int32_t k=g->atten[j];
        pe_put(P23_PIN_ATT1_OUT+2u*j,pm_product(pe_in(P23_PIN_ATT1_IN+2u*j),k*k*2));
    }
    for(j=0;j<2u && (u&PU_IMP);j++) {
        /* pulse converters: follow the leading edge, then decay (~3 ms) while the input stays high */
        uint32_t in=P23_PIN_IMP1_IN+2u*j; int32_t x=pe_in(in),y;
        y=pe_hp(&pe.imp_lp[j],x,240);
        pe_put(in+1u,y>0?y:0);
    }
    if(u&PU_SRC) {
        for(j=0;j<4;j++) pe_put(P23_PIN_MCV1+j,pe_volts(g->midi_cv[j]));
        pe_put(P23_PIN_TOUCH1,pe_volts(g->touch[0])); pe_put(P23_PIN_TOUCH2,pe_volts(g->touch[1]));
        pe_put(P23_PIN_NOISE,PE_V5+pe.pink_out);
        pe_put(P23_PIN_P10V,PE_V10); pe_put(P23_PIN_GND,0);
    }
    if(u&PU_DIODE) {   /* the diode: conducts anode to cathode, and clamps the anode net to the cathode plus 0.6 V */
        int32_t a=pe_in(P23_PIN_DIODE_A),k=pe_in(P23_PIN_DIODE_K),drop=PE_V1*6/10;
        int ad=pe_driven(P23_PIN_DIODE_A),kd=pe_driven(P23_PIN_DIODE_K);
        uint32_t na=pe.net_of[P23_PIN_DIODE_A];
        pe_live(P23_PIN_DIODE_K,ad && (!kd || a-drop>k)); pe_put(P23_PIN_DIODE_K,a-drop);
        pe_live(P23_PIN_DIODE_A,0);
        if(na<PE_NETS) pe.net_clamp[na]=ad && kd?k+drop:0x7fffffff;
    }
    for(j=0;j<2u && (u&PU_CAP);j++) {
        /* the capacitors in series: each side sees the other's changes, decaying (10 ms / 1 s into 100 k) */
        uint32_t a=j?P23_PIN_CAP2_A:P23_PIN_CAP1_A,b=a+1u; int32_t k=j?1:74;
        int32_t va=pe_in(a),vb=pe_in(b);
        if(pe.cap_shunt[j]) { pe_live(a,0); pe_live(b,0); continue; }
        pe_live(b,pe_driven(a)); pe_put(b,pe_hp(&pe.cap_lp[j][0],va,k));
        pe_live(a,pe_driven(b)); pe_put(a,pe_hp(&pe.cap_lp[j][1],vb,k));
    }
}

/* -------------------------------------------------------------- filters --- */
/* A state-variable filter in trapezoidal form: stable at any cutoff, one step a sample, its coefficients
 * prepared when the cutoff moves. The band integrator saturates, which holds self-oscillation and gives the
 * drive of an overloaded analog filter. */
PM_INLINE void pe_coef_set_body(pe_coef *c,uint32_t hz256,int32_t k16)
{
    uint32_t ph,den; int32_t sn,cs,g;
    if(hz256>19800u*256u) hz256=19800u*256u;
    if(hz256<8u*256u) hz256=8u*256u;
    ph=pm_frequency(hz256)/2u;                    /* pi f / fs */
    sn=pm_sine(ph); cs=pm_sine(ph+0x40000000u);
    if(cs<1200) cs=1200;
    g=(int32_t)(((uint32_t)sn<<16)/(uint32_t)cs);  /* tan, Q16 */
    den=65536u+(uint32_t)(((int64_t)g*(g+k16))>>16);
    if(den<4096u) den=4096u;
    c->a1=(int32_t)(0xffffffffu/den);
    c->a2=pm_q16(g,c->a1);
    c->a3=pm_q16(g,c->a2);
    c->k=k16;
}
static PE_FAST void pe_coef_set(pe_coef *c,uint32_t hz256,int32_t k16) { pe_coef_set_body(c,hz256,k16); }
static PE_BLOCK void pe_coef_set_x(pe_coef *c,uint32_t hz256,int32_t k16) { pe_coef_set_body(c,hz256,k16); }
PM_INLINE int32_t pe_svf_run(pe_svf *f,int32_t x,const pe_coef *c,uint32_t type)
{
    int32_t v3=x-f->low,v1=pm_q16(c->a1,f->band)+pm_q16(c->a2,v3),v2=f->low+pm_q16(c->a2,f->band)+pm_q16(c->a3,v3);
    f->band=pm_soft(2*v1-f->band);
    f->low=pm_bound(2*v2-f->low,-98304,98304);
    return type==0?v2:type==1?v1:x-pm_q16(c->k,v1)-v2;
}
/* Q knob to damping (Q16): 2.0, no resonance, .. slightly negative, self-oscillation held by saturation */
PM_INLINE int32_t pe_damping(int32_t q) { return 131072-q*1050; }
/* knob 0 .. 127 to a cutoff, 20 Hz .. 18 kHz */
PM_INLINE int32_t pe_cutoff_oct(int32_t k) { return k*5056; }

/* --------------------------------------------------- envelope and gates --- */
/* ATT 0.3 ms .. ~5 s (linear charge), REL 2 ms .. ~10 s (exponential discharge) */
static PE_BLOCK void pe_env_rates(pe_node *n)
{
    uint32_t att_ms256=(uint32_t)pm_hz(77u,(int32_t)n->patch.attack*7225);
    uint32_t rel_ms256=(uint32_t)pm_hz(512u,(int32_t)n->patch.release*6290);
    uint32_t att_samples=(uint32_t)(((uint64_t)att_ms256*441u)>>8)/10u+1u;
    uint32_t rel_samples=(uint32_t)(((uint64_t)rel_ms256*441u)>>8)/10u+1u;
    n->att_rate=(1u<<24)/att_samples+1u;
    n->rel_k=0x7fffffffu/rel_samples;
}
static void pe_note_gate(pe_node *n)
{
    uint32_t j,latest=0; int32_t strongest=0; uint8_t note=n->note;
    for(j=0;j<PE_HOLD;j++) if(n->hold[j].on) {
        int32_t v=pe_volts(n->hold[j].vel);
        if(v>strongest) strongest=v;
        if(n->hold[j].stamp>=latest) latest=n->hold[j].stamp,note=n->hold[j].note;
    }
    n->note_gate=strongest; n->note=note;
}
void p23_note(uint32_t index,uint32_t source,uint32_t note,uint32_t velocity,int on)
{
    pe_node *n; uint32_t j,slot=PE_HOLD,oldest=0;
    if(index>=P23_NODES || source>=3u || note>=128u) return;
    n=&pe.node[index];
    for(j=0;j<PE_HOLD;j++) {
        if(n->hold[j].on && n->hold[j].note==note && n->hold[j].source==source) { slot=j; break; }
        if(!n->hold[j].on && slot==PE_HOLD) slot=j;
        if(n->hold[j].stamp<n->hold[oldest].stamp) oldest=j;
    }
    if(!on || !velocity) {
        if(slot<PE_HOLD && n->hold[slot].on && n->hold[slot].note==note && n->hold[slot].source==source) n->hold[slot].on=0;
        pe_note_gate(n); return;
    }
    if(slot==PE_HOLD) slot=oldest;
    n->hold[slot].note=(uint8_t)note; n->hold[slot].source=(uint8_t)source;
    n->hold[slot].vel=(uint8_t)pm_bound((int32_t)velocity,1,127); n->hold[slot].on=1;
    n->hold[slot].stamp=++pe.serial;
    n->velocity=n->hold[slot].vel;
    /* keys, sequencer and MIDI strike like ADD: the clap answers velocity 113 .. 127 */
    n->strike=1; n->peaked=0; n->clap_ok=velocity>=113u;
    if(n->patch.module==P23_BD && n->patch.knob[P23_BD_STAB]) n->phase=0,n->tri=0,n->dir=1;
    pe_note_gate(n);
}
void p23_hush(uint32_t index)
{
    uint32_t j;
    if(index>=P23_NODES) return;
    for(j=0;j<PE_HOLD;j++) pe.node[index].hold[j].on=0;
    pe_note_gate(&pe.node[index]);
}
void p23_bend(uint32_t index,int32_t cents) { if(index<P23_NODES) pe.node[index].bend=pm_bound(cents,-1200,1200); }
/* the gate the envelope follows: a percussive sound's only until its attack peaks */
PM_INLINE int pe_held(const pe_node *n) { return n->gate && !(n->patch.perc && n->peaked); }
static PE_FAST void pe_envelope(pe_node *n)
{
    int32_t target=pe_held(n)?n->gate<<9:0;
    if(n->patch.perc && n->gate && n->env>=n->gate<<9) n->peaked=1;
    if(n->env<target) {
        n->env+=(int32_t)n->att_rate;
        if(n->env>target) n->env=target;
    } else if(n->env>target) {
        n->env-=(int32_t)(((int64_t)(n->env-target)*n->rel_k)>>31)+1;
        if(n->env<target) n->env=target;
    }
}

/* ------------------------------------------------------------- circuits --- */
PM_INLINE uint32_t pe_trk_pin(const pe_node *n,uint32_t k) { return P23_PIN_T(n->route.track,k); }
/* EXT inputs are AC-coupled into the circuit */
/* a route's source: the pin's net, or the output itself when nothing is patched to it */
PM_INLINE int32_t pe_src(uint32_t pin)
{
    uint32_t n=pe.net_of[pin];
    if(n<PE_NETS && pe.net_w[n]>0) return pe.net_lp[n]?pm_sdiv(pe.net_lpv[n],8):pe.net_w[n]==1?pe.net_s[n]:pe.net_s[n]/pe.net_w[n];
    return pe.drive[pin];
}
/* the sound's routes, once a sample: each source x depth / 127 into its destination (P23_RD_*). Sources are
 * read from the last block's recording (0.73 ms late), the track's own OUT / ENV at once. */
static PE_FAST void pe_routes(pe_node *n)
{
    /* EXT follows every sample (audio); the control inputs every fourth (11 kHz) */
    uint32_t j,i=n->now&(P23_BLOCK-1u),all=!(i&3u);
    if(all) for(j=0;j<P23_RDESTS;j++) n->rv[j]=0;
    else n->rv[P23_RD_EXT]=0;
    for(j=0;j<n->nr;j++) {
        uint32_t sel=n->rsel[j]; int32_t x;
        if(!all && n->rdst[j]!=P23_RD_EXT) continue;
        if(sel<PE_RSRC) x=pe.rec[sel][i];
        else if(sel==PE_OWN_OUT) x=n->out;
        else if(sel==PE_OWN_ENV) x=n->eb?n->eb[i]:n->env>>9;
        else x=0;
        n->rv[n->rdst[j]]+=pm_product(x,n->rdep[j]);
    }
}
PM_INLINE int32_t pe_route(const pe_node *n,uint32_t d) { return n->rv[d]; }
PM_INLINE int32_t pe_ext(pe_node *n)
{
    uint32_t pin=pe_trk_pin(n,P23_T_EXT); int32_t x;
    int routed=(n->rmask&(1u<<P23_RD_EXT))!=0;
    if(!pe_driven(pin) && !routed) { n->ext_lp=0; return 0; }
    x=(pe_driven(pin)?pe_in(pin):0)+(routed?pe_route(n,P23_RD_EXT):0);
    return pe_hp(&n->ext_lp,x,30);
}
PM_INLINE int32_t pe_mod(pe_node *n,uint32_t k)
{
    uint32_t pin=pe_trk_pin(n,k),d=k-P23_T_EXT; int32_t v=pe_driven(pin)?pe_cv(pin):0;
    if(n->rmask&(1u<<d)) v=pm_bound(v+pe_route(n,d),-PE_V10,PE_V10+PE_V5);
    return v;
}

PM_INLINE int pe_patched(const pe_node *n,uint32_t k) { return (n->mods>>k)&1u; }
/* a bend node (WTF?, OMG!): what is patched, about the +5 V rail, and the sound's routes */
PM_INLINE int32_t pe_bend(pe_node *n,uint32_t k)
{
    uint32_t pin=pe_trk_pin(n,k),d=k-P23_T_EXT;
    int32_t v=pe_driven(pin)?pe_cv(pin)-PE_V5:0;
    if(n->rmask&(1u<<d)) v+=pe_route(n,d);
    return pm_bound(v,-PE_V10,PE_V10);
}
PM_INLINE const pe_coef *pe_filter_c(pe_node *n,int32_t knob,int32_t q,uint32_t k)
{
    /* the cutoff: the knob's (prepared once a block), or followed every other sample while its MOD pin is patched */
    if(pe_patched(n,k) && !(n->now&(n->cabled?1u:7u))) pe_coef_set(&n->fc,pm_hz(5120u,pe_cutoff_oct(knob)+pe_mod(n,k)*15),pe_damping(q));
    return &n->fc;
}

/* a struck mode: a state-variable resonator (accurate at low frequencies), f = 2 pi hz / fs and damping in Q16 */
PM_INLINE void pe_ring(pe_svf *r,int32_t in,int32_t f16,int32_t q16)
{
    r->low+=pm_q16(f16,r->band);
    r->band=pm_bound(r->band+pm_q16(f16,in-r->low-pm_q16(q16,r->band)),-98304,98304);
}
PM_INLINE int32_t pe_f16(int32_t hz256) { return (int32_t)(((uint32_t)hz256*2390u)>>16); }
PM_INLINE int32_t pe_lcg(pe_node *n) { n->rnd=n->rnd*1664525u+1013904223u; return (int32_t)(n->rnd>>17)-16384; }
/* a node's line, d256 / 256 samples back (PLUCK's string, WIRE's comb) */
PM_INLINE int32_t pe_ks_read(const int16_t *line,uint32_t pos,uint32_t d256)
{
    uint32_t i=(pos-(d256>>8))&(PE_KS_LEN-1u),j=(i-1u)&(PE_KS_LEN-1u);
    return line[i]+(((line[j]-line[i])*(int32_t)(d256&255u))>>8);
}

/* BD SKIN: a struck membrane, its fundamental and (1,1) mode (x 1.593) ringing, both swept down by the pitch
 * modulator; SHAPE the second mode and the head's saturation, STAB a stick click, EXT driving the head */
static PE_FAST int32_t pe_skin(pe_node *n,int32_t ext)
{
    const uint8_t *k=n->patch.knob;
    int32_t head=k[P23_BD_DRIVE],y;
    if(n->strike) {
        n->punch=PE_V10;
        n->res[0].band+=PE_V10/2; n->res[1].band+=pm_product(PE_V10/2,head*200);
        if(k[P23_BD_STAB]) n->amp=PE_V10;
    }
    n->punch-=pm_product(n->punch,n->pk);
    if(pe_patched(n,P23_T_MOD2)) n->punch=pm_bound(n->punch+pm_product(pe_bend(n,P23_T_MOD2),90),-PE_V5,PE_V10+PE_V5);
    if(n->strike || !(n->now&3u)) {
        int32_t pitch=k[P23_BD_PITCH],hz=(int32_t)pm_hz(n->base256,pm_product(n->punch,pitch*pitch*8));
        if(pe_patched(n,P23_T_MOD1)) hz+=(int32_t)(((int64_t)pe_mod(n,P23_T_MOD1)*k[P23_BD_AMT]*5)>>7);
        hz=pm_bound(hz,128,3000*256);
        n->fc.a1=pe_f16(hz); n->fc.a2=pm_q16(n->fc.a1,104399);
    }
    pe_ring(&n->res[0],pm_q16(ext,500),n->fc.a1,500);
    pe_ring(&n->res[1],0,n->fc.a2,700);
    if(pe_patched(n,P23_T_MOD3)) n->res[0].band+=pm_product(pe_bend(n,P23_T_MOD3),300);
    y=n->res[0].band+n->res[1].band;
    if(n->amp) { y+=pm_product(pe_lcg(n),n->amp); n->amp-=n->amp>>6; if(n->amp<64) n->amp=0; }
    return pm_sdiv(pm_soft(pm_product(y,32768+head*300)),1);
}
/* a triangle fold: x reflected back within +/- 10 V however far it is driven */
PM_INLINE int32_t pe_fold(int32_t x)
{
    uint32_t u=(uint32_t)(x+PE_V10)&(4u*PE_V10-1u);   /* (4 x 10 V = 2^17: the fold's period) */
    return (int32_t)(u<2u*PE_V10?u:4u*PE_V10-u)-PE_V10;
}
/* BD HAMMER: the triangle core and its pitch modulator, folded (SHAPE: 1 .. 9 times over), a low noise rumble that
 * outlasts the strike (STAB), a click, then a hard asymmetric clip: industrial impact */
static PE_FAST int32_t pe_hammer(pe_node *n,int32_t ext)
{
    const uint8_t *k=n->patch.knob;
    int32_t x,r;
    if(n->strike) { n->punch=PE_V10; n->amp=PE_V10; }
    n->punch-=pm_product(n->punch,n->pk);
    if(pe_patched(n,P23_T_MOD2)) n->punch=pm_bound(n->punch+pm_product(pe_bend(n,P23_T_MOD2),90),-PE_V5,PE_V10+PE_V5);
    if(n->strike || !(n->now&3u)) {
        int32_t pitch=k[P23_BD_PITCH],hz=(int32_t)pm_hz(n->base256,pm_product(n->punch,pitch*pitch*16));
        if(pe_patched(n,P23_T_MOD1)) hz+=(int32_t)(((int64_t)pe_mod(n,P23_T_MOD1)*k[P23_BD_AMT]*5)>>7);
        n->step=pm_bound((int32_t)(((uint32_t)pm_bound(hz,128,4000*256)*761u)>>16),1,65536);
    }
    n->tri+=n->dir*n->step;
    if(pe_patched(n,P23_T_MOD3)) n->tri+=pm_product(pe_bend(n,P23_T_MOD3),600);
    if(n->tri>=PE_V10) { n->tri=2*PE_V10-n->tri; n->dir=-1; }
    if(n->tri<=-PE_V10) { n->tri=-2*PE_V10-n->tri; n->dir=1; }
    x=pe_fold(pm_product(n->tri+ext*2,32768+k[P23_BD_DRIVE]*2064)*1);
    /* the rumble: low-passed noise riding a slower fall; the click: the first milliseconds' noise */
    r=pe_lcg(n);
    n->nlp+=pm_product(r-n->nlp,600);
    n->amp-=n->amp>>(k[P23_BD_STAB]?11:8);
    x+=pm_product(n->nlp*6,n->amp)+(n->punch>PE_V10*7/8?pm_product(r,n->punch):0);
    x=x>0?pm_soft(pm_sdiv(x*3,1)):pm_sdiv(pm_soft(x*2)*3,2);
    return pm_sdiv(x*3,2);
}
/* BASS GRIND: an oscillator whose phase its own overdriven filter pushes (SHAPE: the feedback), ring-modulated by
 * noise (WARP), hard-clipped into the resonant LPF: screams, grinding, howl */
PM_INLINE int32_t pe_grind(pe_node *n,int32_t pm,int32_t white)
{
    const uint8_t *k=n->patch.knob;
    int32_t y,fb=pm_product(n->filter.low,k[P23_BASS_SHAPE]*258);
    n->phase+=n->inc;
    y=pm_sine(n->phase+((uint32_t)pm<<18)+((uint32_t)fb<<17));
    y+=pm_product(pm_product(y,white*2),k[P23_BASS_WARP]*258);
    return pm_soft(y*3);
}
/* SD WALL: power noise -- white noise held at a falling sample rate (AMT: CRUSH), its bits dropped (MIX: BITS),
 * chopped at a rhythmic rate (CLAP: CHOP), then the band-pass screaming and a hard clip */
PM_INLINE int32_t pe_wall(pe_node *n,int32_t white)
{
    const uint8_t *k=n->patch.knob;
    uint32_t old=n->noise_phase,inc=0xffffffffu>>(k[P23_SD_AMT]>>4);
    int32_t x,bits=k[P23_SD_MIX]/10;
    n->noise_phase+=inc-(inc>>2)*(k[P23_SD_AMT]&15u)/16u;
    if(n->noise_phase<old) n->hold_noise=white*2;
    x=n->hold_noise&~((1<<bits)-1);
    if(k[P23_SD_CLAP]) {
        n->ph[0]+=(uint32_t)k[P23_SD_CLAP]*24000u;   /* (about 0.5 .. 31 Hz) */
        if(n->ph[0]>>31) x=0;
    }
    return x;
}
/* HHT STATIC: electrical crackle -- sparse impulses and short arcs (WARP: DENSITY), random in sign, ringing in
 * the resonant HPF */
PM_INLINE int32_t pe_static(pe_node *n,int32_t white)
{
    int32_t warp=n->patch.knob[P23_HHT_WARP],x=0;
    n->rnd=n->rnd*1664525u+1013904223u;
    if(n->strike) n->amp=PE_V10;
    if((n->rnd>>18)<(uint32_t)(2+warp*warp/6)) x=(n->rnd&65536u)?PE_V10:-PE_V10;
    if((n->rnd>>22)==0u && warp>40) n->amp=PE_V10;   /* (an arc) */
    n->amp-=n->amp>>8;
    return x+pm_product(white*2,n->amp);
}
/* BASS PLUCK: a string (a line one period long, its loop damped by SHAPE, sustained by WARP), excited for one
 * period by noise as bright as SHAPE (and by EXT) */
static PE_FAST int32_t pe_pluck(pe_node *n,int32_t ext,int32_t white)
{
    const uint8_t *k=n->patch.knob;
    int16_t *line=pe.ks[n-pe.node];
    int32_t shape=k[P23_BASS_SHAPE],in=ext,y,lp;
    if(n->strike) n->amp=(int32_t)(n->klen256>>8);
    if(n->amp>0) { n->nlp+=pm_product(white-n->nlp,3000+shape*230); in+=n->nlp*2; n->amp--; }
    y=pe_ks_read(line,n->kpos,n->klen256);
    lp=y+pm_product(n->kprev-y,(127-shape)*120+800); n->kprev=y;
    line[n->kpos]=(int16_t)pm_bound(in+pm_product(lp,31000+k[P23_BASS_WARP]*13),-32767,32767);
    n->kpos=(uint16_t)((n->kpos+1u)&(PE_KS_LEN-1u));
    return y;
}
/* BASS FM: a two-operator ping, the modulator at SHAPE's ratio (1 .. 7, harmonic and not), WARP the index, which
 * falls faster than the envelope */
static PE_FAST int32_t pe_fm(pe_node *n,int32_t pm)
{
    int32_t m;
    if(n->strike) n->amp=PE_V10;
    n->amp-=pm_product(n->amp,n->pk);
    n->ph[0]+=n->minc[0];
    m=pm_sine(n->ph[0]);
    n->phase+=n->inc;
    return pm_sdiv(pm_sine(n->phase+((uint32_t)pm<<18)+((uint32_t)pm_product(m,pm_product(n->amp,n->patch.knob[P23_BASS_WARP]*258))<<17)),1);
}
/* SD TONE: two body modes (x 1, x 1.47) struck with a short downward bend, beside the noise */
PM_INLINE int32_t pe_sd_tone(pe_node *n)
{
    if(n->strike) { n->res[0].band+=PE_V10*2/5; n->res[1].band+=PE_V10/4; n->punch=PE_V10; }
    n->punch-=n->punch>>9;
    if(n->strike || !(n->now&3u)) {
        n->fc.a3=pe_f16((int32_t)(n->base256+(uint32_t)(((uint64_t)n->base256*(uint32_t)n->punch)>>17)));
        n->rc[0].a1=pm_q16(n->fc.a3,96338);
    }
    pe_ring(&n->res[0],0,n->fc.a3,900);
    pe_ring(&n->res[1],0,n->rc[0].a1,1300);
    return n->res[0].band+n->res[1].band;
}
/* HHT METAL: six square oscillators at inharmonic ratios, summed (cymbal) or, with WARP, ring-modulated in
 * pairs (cowbell, clang) */
PM_INLINE int32_t pe_metal(pe_node *n)
{
    uint32_t j,sum=0,ring=0,b[6]; int32_t warp=n->patch.knob[P23_HHT_WARP];
    for(j=0;j<6;j++) { n->ph[j]+=n->minc[j]; b[j]=n->ph[j]>>31; sum+=b[j]; }
    ring=(b[0]^b[1])+(b[2]^b[3])+(b[4]^b[5]);
    return ((int32_t)sum*2-6)*2900*(127-warp)/127+((int32_t)ring*2-3)*5800*warp/127;
}
/* HHT SHAKE: noise in grains, as dense as WARP, each a few milliseconds */
PM_INLINE int32_t pe_shake(pe_node *n,int32_t white)
{
    int32_t warp=n->patch.knob[P23_HHT_WARP];
    n->rnd=n->rnd*1664525u+1013904223u;
    if(n->strike || (n->rnd>>20)<(uint32_t)(6+warp*warp/10)) n->amp=PE_V10;
    n->amp-=n->amp>>7;
    return pm_product(white,n->amp)*2;
}

/* BD: an integrator / comparator triangle core (OMG! injects into its capacitor), a pitch modulator
 * charged at each strike (PITCH: depth and decay; WTF? injects into its capacitor), linear Hz/V MOD, and
 * a DRIVE stage that pushes the triangle through sine into square. EXT enters before the DRIVE stage. */
static PE_FAST int32_t pe_bd(pe_node *n,int32_t ext)
{
    const uint8_t *k=n->patch.knob;
    int32_t x;
    if(n->patch.mode==P23_BD_SKIN) return pe_skin(n,ext);
    if(n->patch.mode==P23_BD_HAMMER) return pe_hammer(n,ext);
    if(n->strike) n->punch=PE_V10;
    /* the modulator's charge decays (3 .. 120 ms with PITCH); WTF? pushes current into it */
    n->punch-=pm_product(n->punch,n->pk);
    if(pe_patched(n,P23_T_MOD2)) n->punch=pm_bound(n->punch+pm_product(pe_bend(n,P23_T_MOD2),90),-PE_V5,PE_V10+PE_V5);
    if(n->strike || !(n->now&3u)) {
        /* the frequency, every fourth sample: pitch charge (up to ~4 octaves), MOD linear (60 Hz/V at full AMT) */
        int32_t pitch=k[P23_BD_PITCH],hz=(int32_t)pm_hz(n->base256,pm_product(n->punch,pitch*pitch*16));
        if(pe_patched(n,P23_T_MOD1)) hz+=(int32_t)(((int64_t)pe_mod(n,P23_T_MOD1)*k[P23_BD_AMT]*5)>>7);
        hz=pm_bound(hz,128,4000*256);
        n->step=pm_bound((int32_t)(((uint32_t)hz*761u)>>16),1,65536);   /* (the core ramps 2 x 20 V a cycle) */
    }
    n->tri+=n->dir*n->step;
    if(pe_patched(n,P23_T_MOD3)) n->tri+=pm_product(pe_bend(n,P23_T_MOD3),600);
    if(n->tri>=PE_V10) { n->tri=2*PE_V10-n->tri; n->dir=-1; }
    if(n->tri<=-PE_V10) { n->tri=-2*PE_V10-n->tri; n->dir=1; }
    n->tri=pm_bound(n->tri,-PE_V10,PE_V10);
    x=n->tri+ext*2;
    /* DRIVE: gain 0.5 .. 24 into a soft knee (triangle .. sine .. square), level kept */
    x=pm_soft((int32_t)(((int64_t)x*n->drive)>>8));
    return pm_sdiv(pm_product(x,n->rd),1);
}

/* BASS: a recursive DCO (feedback phase modulation: sine, toward saw; then toward square), phase MOD,
 * a loaded output shaper (WARP), EXT, and a saturating resonant low-pass. PRC replaces the oscillator
 * by an excited array of three resonators. */
static PE_FAST int32_t pe_bass(pe_node *n,int32_t ext,int32_t white)
{
    const uint8_t *k=n->patch.knob;
    int32_t shape=k[P23_BASS_SHAPE],warp=k[P23_BASS_WARP],pm=0,x;
    if(n->mods&((1u<<P23_T_MOD2)|(1u<<P23_T_MOD3)|(1u<<P23_T_MOD4))) {
        shape=pm_bound(shape+pe_mod(n,P23_T_MOD2)/258,0,127);
        warp=pm_bound(warp+pe_mod(n,P23_T_MOD3)/258,0,127);
        pm=pm_product(pe_mod(n,P23_T_MOD4),(int32_t)k[P23_BASS_AMT]*258);
    }
    if(n->patch.mode==P23_BASS_PLUCK) { x=pe_pluck(n,ext,white); ext=0; }
    else if(n->patch.mode==P23_BASS_FM) x=pe_fm(n,pm);
    else if(n->patch.mode==P23_BASS_GRIND) x=pe_grind(n,pm,white);
    else if(n->patch.mode==P23_BASS_PRC) {
        /* PRC: impulse + noise excite three resonators. SHAPE: clean (harmonic, little noise) .. one
         * resonance with noise .. detuned cluster; WARP: drive and cross-modulation. MOD: sidechain duck. */
        int32_t excite,sum,noise_amt,cross,y;
        uint32_t j;
        noise_amt=shape<64?shape*4:(127-shape)*2+128;
        excite=pm_product(white,noise_amt*60);
        if(n->strike) excite+=PE_V10*4;
        cross=pm_product(n->y1,warp*200);
        /* two-pole resonators (y = x + c y1 - r^2 y2), their input scaled by their bandwidth */
        for(j=0,sum=0;j<3;j++) {
            pe_svf *rs=&n->res[j]; const pe_coef *c=&n->rc[j];
            int32_t in=pm_q16(excite+(j?cross:-cross)+pm_product(n->res[(j+1u)%3u].low,warp*90),c->a3);
            y=pm_bound(in+pm_q16(c->a1,rs->low)-pm_q16(c->a2,rs->band),-98304,98304);
            rs->band=rs->low; rs->low=y;
            sum+=j==0?y:pm_product(y,shape*258);
        }
        sum=pm_product(sum,n->np);                 /* (the array's level, whatever SHAPE mixes in) */
        n->y1=pm_soft(sum);
        x=pm_soft(pm_product(pm_sdiv(sum,1),32768+warp*900));
        if(pm) x=pm_product(x,PE_V10-pm_bound(pm,0,PE_V10));
    } else {
        int32_t beta,fbk,y;
        uint32_t ph;
        n->phase+=n->inc;
        ph=n->phase+((uint32_t)pm<<18);
        /* the recursion: y = sin(phase + beta * mean of the last two outputs) */
        beta=shape<64?shape*400:25600;
        fbk=pm_product(pm_sdiv(n->y1+n->y2,1),beta);
        y=pm_sine(ph+((uint32_t)fbk<<16));
        n->y2=n->y1; n->y1=y;
        if(shape<=64) x=y;
        else {
            int32_t sq=(ph<0x80000000u?32767:-32767)+pm_blep(ph,n->inc,n->rcp)-pm_blep(ph+0x80000000u,n->inc,n->rcp);
            x=y+(sq-y)*(shape-64)/63;
        }
        x=pm_sdiv(x,1);
        /* WARP: the waveshaper's load, asymmetric (even harmonics) and harder as it rises */
        x=pm_soft(pm_product(x,32768+warp*1400)+warp*40)-n->warp0;
        if(n->mods&(1u<<P23_T_MOD3)) x+=n->warp0-pm_soft(warp*40);
    }
    x+=ext;
    /* the filter section is the same in every mode: LPF FR, Q and MOD (0.75 octave a volt) */
    return pe_svf_run(&n->filter,x,pe_filter_c(n,k[P23_BASS_LPF],k[P23_BASS_Q],P23_T_MOD5),0);
}

/* SD: analog pink noise and a pitched "spectral" noise (an oscillator swept by band-limited noise, its
 * centre on TUNE, linear Hz/V MOD x AMT), MIX, the clap's split attack (ADD / velocity >= 113 only),
 * EXT, then a resonant band-pass. */
static PE_FAST int32_t pe_sd(pe_node *n,int32_t ext,int32_t white,int32_t pink)
{
    const uint8_t *k=n->patch.knob;
    int32_t spectral,x,mix=k[P23_SD_MIX],tone=0;
    uint32_t inc=n->inc;
    if(n->patch.mode==P23_SD_TONE) { tone=pe_sd_tone(n); x=pm_sdiv(white+pink,1); }
    else if(n->patch.mode==P23_SD_WALL) x=pe_wall(n,white);
    else {
    if(pe_patched(n,P23_T_MOD1))
        inc=(uint32_t)pm_bound((int32_t)inc+(int32_t)(((int64_t)pe_mod(n,P23_T_MOD1)*k[P23_SD_AMT]*57000)>>7),20000,0x3fffffff);
    /* noise band-limited to about the centre frequency drives the oscillator's frequency */
    n->nlp+=pm_product(white-n->nlp,n->np);
    n->phase+=(uint32_t)(((uint64_t)inc*(uint32_t)pm_bound(32768+n->nlp*3,0,131072))>>15);
    spectral=pm_sdiv(pm_sine(n->phase),1)+pm_product(n->nlp,12000);
    x=pink+pm_product(spectral-pink,mix*258);
    }
    if(k[P23_SD_CLAP] && n->clap_age<0xffffffu && n->patch.mode!=P23_SD_WALL) {
        /* the clap: 1 .. 4 bursts, 4 .. 19 ms apart; each burst flares and falls */
        uint32_t space=176u+(uint32_t)k[P23_SD_CLAP]*5u,bursts=1u+(uint32_t)k[P23_SD_CLAP]/40u;
        uint32_t age=n->clap_age;
        if(age<space*bursts) {
            uint32_t within=age%space;
            x=pm_product(x,(int32_t)(32768u-within*30000u/space));
        }
        n->clap_age++;
    }
    x+=ext;
    if(n->patch.mode==P23_SD_WIRE) {
        /* WIRE: the noise through a comb two octaves above the note, feedback with Q: snare wires, buzz, rattle */
        int16_t *line=pe.ks[n-pe.node];
        int32_t c=pe_ks_read(line,n->kpos,n->klen256);
        x=pm_bound(x+pm_product(c,17000+k[P23_SD_Q]*118),-32767,32767);
        line[n->kpos]=(int16_t)x; n->kpos=(uint16_t)((n->kpos+1u)&(PE_KS_LEN-1u));
    }
    {   /* the band-pass's level held across Q (its resonance may still lift it about 10 dB) */
        const pe_coef *c=pe_filter_c(n,k[P23_SD_BPF],n->patch.mode==P23_SD_WIRE?30:k[P23_SD_Q],P23_T_MOD2);
        int32_t y=pm_sdiv(pm_q16(pe_svf_run(&n->filter,pm_product(x,26000),c,1),pm_bound(c->k,19661,131072))*3,1);
        if(n->patch.mode==P23_SD_WALL) return pm_soft(y*4);   /* (the wall clipped hard) */
        return n->patch.mode==P23_SD_TONE?tone+pm_product(y-tone,mix*258):n->patch.mode==P23_SD_WIRE?y:y*2;   /* (NOISE: level with the others) */
    }
}

/* HHT: a timbral noise generator (white noise resampled at a TUNE-controlled clock: low settings are
 * crunchy and grainy), WARP shaping its spectrum toward sparse grains (shaker), EXT, resonant high-pass. */
static PE_FAST int32_t pe_hht(pe_node *n,int32_t ext,int32_t white)
{
    const uint8_t *k=n->patch.knob;
    int32_t warp=k[P23_HHT_WARP],x,a,a2;
    uint32_t old=n->noise_phase;
    if(pe_patched(n,P23_T_MOD1) && !(n->now&(n->cabled?3u:7u))) {
        uint32_t hz256=pm_hz(n->base256,pe_mod(n,P23_T_MOD1)*12);
        n->inc=hz256>=44100u*256u?0xffffffffu:pm_frequency(hz256);
    }
    if(n->patch.mode==P23_HHT_METAL) x=pe_metal(n);
    else if(n->patch.mode==P23_HHT_SHAKE) x=pe_shake(n,white);
    else if(n->patch.mode==P23_HHT_STATIC) x=pe_static(n,white);
    else {
    n->noise_phase+=n->inc;
    if(n->noise_phase<old || n->inc==0xffffffffu) n->hold_noise=white;
    x=n->hold_noise;
    /* WARP: an expander: |x|^(1 .. 4), spikier and sparser, its level made up */
    a=pm_abs(x); a2=pm_product(a,a);
    a=warp<64?a+pm_sdiv((a2-a)*warp,6):a2+(pm_product(a2,a2)-a2)*(warp-64)/63;
    a=pm_product(a,32768+warp*500);
    x=pm_sdiv(x<0?-a:a,1);
    }
    x+=ext;
    return pe_svf_run(&n->filter,x,pe_filter_c(n,k[P23_HHT_HPF],k[P23_HHT_Q],P23_T_MOD2),2);
}
/* Pitch comes from the note (the scale chosen); TUNE is the recipe's offset from it, in half semitones about 64.
 * BD: two octaves below the note; SD: its spectral noise four octaves above; HHT: its noise clock six above;
 * BASS: the note (with glide and bend) and CV IN at 1 V/oct. */
static PE_BLOCK void pe_tune(pe_node *n)
{
    const uint8_t *k=n->patch.knob;
    int32_t note256=(n->patch.module==P23_BASS?n->glide256:(int32_t)n->note*256)+((int32_t)k[0]-64)*128,lo,hi;
    switch(n->patch.module) {
    case P23_BD:  note256-=(n->patch.mode==P23_BD_SKIN?12:24)*256; lo=20*256; hi=900*256; break;   /* (HAMMER: as TRI) */
    case P23_SD:  note256+=(n->patch.mode==P23_SD_TONE?7:48)*256; lo=60*256; hi=9000*256; break;
    case P23_HHT: note256+=72*256; lo=1000*256; hi=70000*256; break;
    default:
        note256+=n->bend*256/100+pe_mod(n,P23_T_MOD1)*15/16;
        lo=8*256; hi=8000*256; break;
    }
    note256=pm_bound(note256,-12*256,150*256);
    n->base256=(uint32_t)pm_bound((int32_t)(((uint64_t)2093u*pm_exp(note256*256/12))>>16),lo,hi);
}
/* a line's length for a frequency (phase increment): its period in 1/256 samples, within the line */
PM_INLINE uint32_t pe_period256(uint32_t inc)
{
    uint32_t p=(inc>>8)?0xffffffffu/(inc>>8):0xffffffffu;
    return p<512u?512u:p>(PE_KS_LEN-2u)*256u?(PE_KS_LEN-2u)*256u:p;
}
/* the overdrive's settings from its knob, once a block */
static PE_BLOCK void pe_drive_prepare(pe_node *n)
{
    int32_t d=n->patch.drive,g;
    n->d_mix=d>=32?32768:d*1024;
    g=2458+d*d*3;                                       /* gain, Q12: 0.6 .. 12.4 */
    n->d_g=g;
    n->d_comp=(int32_t)(134217728u/(uint32_t)((g<5461?g:5461)*3/2));   /* level kept: 1 / (1.5 min(g, 4/3)), Q15 */
    n->d_emph=d*128;                                    /* pre-emphasis, up to +6 dB above ~1.2 kHz */
    n->d_pole=pm_pole((uint32_t)(16000-d*85)*256u);     /* de-emphasis: 16 kHz .. 5 kHz */
}
PM_INLINE int32_t pe_cubic(int32_t v)                    /* (3 v - v^3) / 2, held at +/- 1 (Q15) */
{
    if(v>=32768) return 32768;
    if(v<=-32768) return -32768;
    return (int32_t)(((int64_t)v*(98304-(int32_t)(((int64_t)v*v)>>15)))>>16);
}
/* count samples in place; gm (or none): each sample's DRIVE route (gain x (1 + v / 10 V)) */
static PE_FAST void pe_drive_run(pe_node *n,int32_t *x,const int32_t *gm,uint32_t count)
{
    int32_t lp=n->d_lp,sag=n->d_sag,post=n->d_post,g0=n->d_g,emph=n->d_emph,pole=n->d_pole,comp=n->d_comp,mix=n->d_mix;
    uint32_t i;
    for(i=0;i<count;i++) {
        int32_t v=x[i],e,u,bias,y,g=gm?pm_bound(g0+pm_product(g0,gm[i]*2),0,65536):g0;
        e=v+pm_product(v-pe_lp(&lp,v,5500),emph);
        u=(int32_t)(((int64_t)e*g)>>12);
        /* the bias: a fixed asymmetry (even harmonics) that sags as the level rises, so the colour breathes */
        bias=2400-pm_sdiv(sag,3);
        y=pe_cubic(u+bias)-pe_cubic(bias);
        sag+=((y<0?-y:y)-sag)>>9;
        y=pm_product(pe_lp(&post,y,pole),comp);
        x[i]=v+pm_product(y-v,mix);
    }
    n->d_lp=lp; n->d_sag=sag; n->d_post=post;
}
/* WRECK, count samples in place: FOLD (a triangle fold, driven up to ~5x), RING (a ring modulator, crossfaded in,
 * its own oscillator at RING FREQ, 20 Hz .. 5 kHz, whatever the note), CRUSH (a sample held 1 .. 16 samples) and
 * BITS (up to 12 dropped) */
static PE_FAST void pe_wreck_run(pe_node *n,int32_t *x,uint32_t count)
{
    const p23_patch *p=&n->patch;
    int32_t fold=p->fold,ring=p->ring*258,g=n->wgain,hold=n->whold;
    uint32_t i,ph=n->wph,inc=n->winc,cnt=n->wcnt,every=n->wn;
    int32_t mask=~((1<<(p->bits/10u))-1);
    for(i=0;i<count;i++) {
        int32_t v=x[i];
        if(fold) v=pe_fold(pm_product(v,g));
        if(ring) { v+=pm_product(pm_product(v,pm_sine(ph))-v,ring); ph+=inc; }
        if(every>1u) { if(++cnt>=every) { cnt=0; hold=v; } v=hold; }
        x[i]=v&mask;
    }
    n->wph=ph; n->wcnt=(uint8_t)cnt; n->whold=hold;
}
PM_INLINE int pe_wrecked(const pe_node *n)
{ return (n->patch.fold|n->patch.ring|n->patch.crush|n->patch.bits) && !(pe.diag&2048u); }
/* TRANSFORM's stage, count samples in place: the effects drawn (DRIVE a soft clip driven 4 .. 20x, FOLD 2 .. 7x,
 * RING 50 .. 100 % with a triangle at 30 Hz .. 3 kHz, CRUSH a sample held 3 .. 15 samples, BITS 13 or 14 dropped,
 * rounded so as to add no DC), crossfaded in from mix m0 to m1 across them. For the target's sake (no FPU, every
 * voice through it): 32-bit products only (the samples held to +/- 10 V first), one pass an effect with no branch
 * inside, a triangle where a sine would cost a polynomial, and no crossfade at full mix */
PM_INLINE int32_t pe_soft32(int32_t x)   /* (pm_soft, its square in 32 bits: |x| < 49152 there) */
{
    int32_t a=pm_abs(x),t;
    if(a<=16384) return x;
    if(a>=49152) return x<0?-32768:32768;
    t=a-16384; a=16384+t-((t*t)>>16);
    return x<0?-a:a;
}
static PE_FAST void pe_xf_run(pe_node *n,int32_t *x,uint32_t count,int32_t m0,int32_t m1)
{
    int32_t dry[P23_BLOCK],g;
    uint32_t i,set=pe.xf_set,full=m0>=32766 && m1>=32766;
    for(i=0;i<count;i++) dry[i]=x[i]=pm_bound(x[i],-PE_V10,PE_V10);
    if(set&XF_DRIVE) {   /* (gain Q8: 4 .. 20x; then -3 dB) */
        g=pe.xf_gd>>4;
        for(i=0;i<count;i++) x[i]=(pe_soft32((x[i]*g)>>8)*23170)>>15;
    }
    if(set&XF_FOLD) {
        g=pe.xf_gf>>4;
        for(i=0;i<count;i++) x[i]=pe_fold((x[i]*g)>>8);
    }
    if(set&XF_RING) {
        uint32_t ph=n->xph,inc=pe.xf_inc; int32_t r=pe.xf_ring>>1;
        for(i=0;i<count;i++) {
            uint32_t u=ph>>16; int32_t tri=(int32_t)(u<32768u?u:65535u-u)*2-32767,v=x[i];
            x[i]=v+((((v*tri)>>15)-v)*r>>14);
            ph+=inc;
        }
        n->xph=ph;
    }
    if(set&XF_CRUSH) {
        uint32_t cnt=n->xcnt,every=pe.xf_every; int32_t hold=n->xhold;
        for(i=0;i<count;i++) { if(++cnt>=every) { cnt=0; hold=x[i]; } x[i]=hold; }
        n->xcnt=(uint8_t)cnt; n->xhold=hold;
    }
    if(set&XF_BITS) {
        int32_t mask=pe.xf_mask,half=pe.xf_half;
        for(i=0;i<count;i++) x[i]=(x[i]+half)&mask;
    }
    if(!full) {
        int32_t m=m0>>1,dm=((m1-m0)>>1)/(int32_t)count;
        for(i=0;i<count;i++) { x[i]=dry[i]+(((x[i]-dry[i])*m)>>14); m+=dm; }
    }
}
/* once a block: what the knobs set, so the samples only follow what is patched */
static PE_BLOCK void pe_prepare(pe_node *n,uint32_t index)
{
    const uint8_t *k=n->patch.knob; uint32_t j,t=n->route.track;
    int32_t cut=0,q=0;
    n->mods=0; n->rmask=0; n->cabled=0;
    for(j=0;j<P23_ROUTES;j++) if(n->patch.mod[j].src && n->patch.mod[j].depth && !(pe.diag&4096u)) n->rmask|=1u<<n->patch.mod[j].dst;
    for(j=P23_T_EXT;j<P23_TRACK_PINS;j++) {
        if(pe.net_of[P23_PIN_T(t,j)]<PE_NETS) n->cabled=1;
        if(pe.net_of[P23_PIN_T(t,j)]<PE_NETS || (n->rmask&(1u<<(j-P23_T_EXT)))) n->mods|=1u<<j;
    }
    if(n->rmask&((1u<<P23_RD_DRIVE)|(1u<<P23_RD_VCA))) n->mods|=1u<<P23_TRACK_PINS;
    if(n->patch.module==P23_BASS) {
        int32_t target=(int32_t)n->note*256,glide=k[P23_BASS_GLIDE];
        if(!glide) n->glide256=target;
        else n->glide256+=(target-n->glide256)*8/(8+glide*glide/8)+(target>n->glide256?1:target<n->glide256?-1:0);
    }
    /* a node that stays silent this block needs nothing more (it is prepared again as it is struck) */
    if(!n->env && !pe_held(n) && !(n->note_gate && !n->patch.perc) && !n->strike
       && !(index<P23_TRACKS && (pe.lr_v[index] || (pe.tpins&(65536u<<index))))) return;
    pe_env_rates(n);
    pe_drive_prepare(n);
    n->wgain=32768+n->patch.fold*1032;
    n->wn=(uint8_t)(1u+n->patch.crush/8u);
    if(n->patch.ring) n->winc=pm_frequency(pm_hz(20u*256u,(int32_t)n->patch.ring_freq*4128));
    pe_tune(n);
    n->inc=pm_frequency(n->base256);
    switch(n->patch.module) {
    case P23_BASS: cut=k[P23_BASS_LPF]; q=k[P23_BASS_Q];
        n->rcp=0xffffffffu/(n->inc?n->inc:1u);
        n->warp0=pm_soft(k[P23_BASS_WARP]*40);
        if(n->patch.mode==P23_BASS_PLUCK) n->klen256=pe_period256(n->inc);
        if(n->patch.mode==P23_BASS_FM) {
            /* the modulator's ratio by SHAPE; the index falls in 2 .. 90 ms, with REL */
            static const int32_t R[8]={65536,92682,131072,180879,229376,275251,353894,458752};
            n->minc[0]=(uint32_t)(((uint64_t)n->inc*(uint32_t)R[(k[P23_BASS_SHAPE]>>4)&7u])>>16);
            n->pk=8+(127-(int32_t)n->patch.release)*3;
        }
        if(n->patch.mode==P23_BASS_PRC) {
            /* resonators: harmonic (1, 2, 3) when clean, one spread cluster (1, 1.06, 0.94) when CW */
            int32_t shape=k[P23_BASS_SHAPE];
            static const int32_t harm[3]={0,65536,103872},spread[3]={0,5500,-5800};
            n->np=(int32_t)(((int64_t)32768*32768)/(32768+2*shape*258));
            for(j=0;j<3;j++) {
                /* each: centre f, bandwidth f k (k 0.055 .. 0.024 with SHAPE): r = 1 - pi f k / fs, c = 2 r cos w */
                uint32_t inc=pm_frequency(pm_hz(n->base256,harm[j]+(spread[j]-harm[j])*shape/127));
                int32_t k16=3600-shape*16,omr,r16;
                if(inc>0x70000000u) inc=0x70000000u;
                omr=(int32_t)((((uint64_t)inc*(uint32_t)k16)>>32)*205887u>>16)+1;
                if(omr>40000) omr=40000;
                r16=65536-omr;
                n->rc[j].a1=(int32_t)(((int64_t)2*r16*pm_sine(inc+0x40000000u))>>15);
                n->rc[j].a2=pm_q16(r16,r16);
                {   /* the input scaled by the square root of the bandwidth: an impulse rings about as loud at any Q */
                    uint32_t v=(uint32_t)omr<<16,q=0,bit=1u<<30;
                    while(bit>v) bit>>=2;
                    while(bit) { if(v>=q+bit) { v-=q+bit; q=(q>>1)+bit; } else q>>=1; bit>>=2; }
                    n->rc[j].a3=(int32_t)q;
                }
            }
        }
        break;
    case P23_SD: cut=k[P23_SD_BPF]; q=k[P23_SD_Q]; n->np=pm_pole(n->base256/2u);
        if(n->patch.mode==P23_SD_WIRE) { n->klen256=pe_period256(n->inc>>2); q=30; }
        break;
    case P23_HHT: cut=k[P23_HHT_HPF]; q=k[P23_HHT_Q];
        if(!pe_patched(n,P23_T_MOD1)) n->inc=n->base256>=44100u*256u?0xffffffffu:pm_frequency(n->base256);
        if(n->patch.mode==P23_HHT_METAL) {
            /* the six squares three octaves below the noise clock, at the 808's spread of ratios */
            static const int32_t R[6]={65536,85459,96076,117113,126616,166199};
            for(j=0;j<6;j++) n->minc[j]=pm_frequency((uint32_t)(((uint64_t)(n->base256>>3)*(uint32_t)R[j])>>16));
        }
        break;
    default: {
        /* BD: pitch modulator decay (3 .. 120 ms), DRIVE gain (Q8) and its level compensation */
        int32_t peak;
        n->pk=(int32_t)pm_hz(248u*256u,-(int32_t)k[P23_BD_PITCH]*2745)/256+1;
        n->drive=(int32_t)pm_hz(128u,(int32_t)k[P23_BD_DRIVE]*2890);
        peak=pm_soft((int32_t)(((int64_t)PE_V10*n->drive)>>8));
        n->rd=(int32_t)(((int64_t)PE_V10*32768)/(peak>1024?peak:1024));
        break; }
    }
    if(n->patch.module!=P23_BD && !pe_patched(n,n->patch.module==P23_BASS?P23_T_MOD5:P23_T_MOD2))
        pe_coef_set_x(&n->fc,pm_hz(5120u,pe_cutoff_oct(cut)),pe_damping(q));
    {   /* the channel strip, once a block: mute (8 ms fade), ducking, VOL and the FM-1's equal-power pan, and the
         * post-VOL FX send, each gain ramped across the block */
        uint32_t angle=(uint32_t)(n->route.pan+127)*(0x40000000u/254u);
        int32_t mutegoal=n->route.enabled && !n->route.muted?32768:0,g,gl,gr,gs;
        n->mute_gain+=pm_bound(mutegoal-n->mute_gain,-3072,3072);
        g=n->mute_gain;
        if(n->route.duck_source<P23_NODES && n->route.duck_amount)
            g=pm_product(g,32768-pm_bound(pm_product(pe.node[n->route.duck_source].env>>9,n->route.duck_amount*258),0,32768));
        n->level_gain+=pm_bound((int32_t)n->route.level-n->level_gain,-4096,4096);
        /* the FX send follows VOL too (unchanged at unity, 16384), so a track turned down is quiet in the FX as well */
        gs=pm_product(pm_q16(g,n->level_gain*4),n->route.fx_send*258);
        g=pm_q16(g,n->level_gain*2);
        gl=pm_product(g,pm_sine(0x40000000u-angle)); gr=pm_product(g,pm_sine(angle));
        n->gl=n->tl; n->gr=n->tr; n->gs=n->ts; n->tl=gl; n->tr=gr; n->ts=gs;
        n->dgl=(gl-n->gl)/(int32_t)P23_BLOCK; n->dgr=(gr-n->gr)/(int32_t)P23_BLOCK; n->dgs=(gs-n->gs)/(int32_t)P23_BLOCK;
    }
}

/* -------------------------------------------------------------------- FX --- */
/* Each structure in the 32K-word memory keeps its own position: no divisions per access. */
PM_INLINE uint32_t pe_back(uint32_t pos,uint32_t len,uint32_t d) { return pos>=d?pos-d:pos+len-d; }
PM_INLINE void pe_memw(uint32_t at,int32_t v) { pe.fx.mem[at]=(int16_t)pm_bound(v,-32767,32767); }
PM_INLINE uint16_t pe_next(uint16_t pos,uint32_t len) { return (uint16_t)(pos+1u>=len?0u:pos+1u); }
/* d256 / 256 words behind the next write position (1 .. len-2) */
PM_INLINE int32_t pe_line(uint32_t base,uint32_t len,uint32_t pos,uint32_t d256)
{
    uint32_t i=pe_back(pos,len,d256>>8),j=i?i-1u:len-1u;
    int32_t a=pe.fx.mem[base+i],b=pe.fx.mem[base+j];
    return a+(((b-a)*(int32_t)(d256&255u))>>8);
}
/* a feedback pitch shifter: two taps sliding through a short buffer, crossfaded */
PM_INLINE int32_t pe_fx_shift(uint32_t base,uint32_t len,uint16_t *pos,uint32_t *ph,int32_t ratio16,int32_t in)
{
    uint32_t span=(len-64u)<<8,a,b; int32_t wa,ya,yb;
    pe_memw(base+*pos,in);
    *pos=pe_next(*pos,len);
    *ph+=(uint32_t)(65536-ratio16)*(65536u/(len-64u));
    a=(uint32_t)(((uint64_t)*ph*span)>>32); b=(uint32_t)(((uint64_t)(*ph+0x80000000u)*span)>>32);
    ya=pe_line(base,len,*pos,a+512u); yb=pe_line(base,len,*pos,b+512u);
    wa=pm_sdiv(pm_triangle(*ph),1)+16384;
    return pm_product(ya,wa)+pm_product(yb,32768-wa);
}
PM_INLINE int32_t pe_fx_ap(uint32_t base,uint32_t len,uint16_t *pos,int32_t x,int32_t k)
{
    int32_t d=pe.fx.mem[base+*pos],v=x-pm_product(d,k);
    pe_memw(base+*pos,v); *pos=pe_next(*pos,len);
    return d+pm_product(v,k);
}
/* one tick of the effects processor at its own (modulated) sample rate */
static PE_FAST void pe_fx_tick(int32_t din,int32_t rin)
{
    pe_fx *f=&pe.fx; const p23_panel *g=&pe.panel;
    uint32_t mode=g->fx_mode%3u,mad=f->mad,dly;
    int32_t time,tune,fb,x,y,tap2=0,rev,decay;
    /* TIME / TUNE / FB with their CV, slewed as potentiometer inputs are */
    if(!(f->ticks++&7u)) {
        time=pm_bound((int32_t)g->fx_time*258+(pe_driven(P23_PIN_FX_TIME)?pe_cv(P23_PIN_FX_TIME):0),0,PE_V10);
        tune=pm_bound((int32_t)g->fx_tune*258+(pe_driven(P23_PIN_FX_TUNE)?pe_cv(P23_PIN_FX_TUNE)-PE_V5:0),0,PE_V10);
        fb=pm_bound((int32_t)g->fx_feedback*258+(pe_driven(P23_PIN_FX_FB)?pe_cv(P23_PIN_FX_FB):0),0,PE_V10);
        f->time_s+=(time-f->time_s)/8; f->tune_s+=(tune-f->tune_s)/8; f->fb_s+=(fb-f->fb_s)/8;
        /* the tone and pitch controls follow at this rate too */
        f->cg=2*pm_sine(pm_frequency((uint32_t)(((uint64_t)pm_hz(150u*256u,f->tune_s*11)*1378u)>>10))/2u);
        f->ratio=(int32_t)pm_exp((f->tune_s-PE_V5)*4);
        f->ratio_r=(int32_t)pm_exp(-(f->tune_s-PE_V5)*4);
        f->d1=32u+(uint32_t)(((uint64_t)(uint32_t)f->time_s*(uint32_t)f->time_s*(PE_DLY_LEN-600u))>>30);
        f->d2=32u+(uint32_t)(((uint64_t)(uint32_t)f->tune_s*(uint32_t)f->tune_s*(PE_DLY_LEN-600u))>>30);
    }
    /* the A/D clips */
    if(pm_abs(din)>30000 || pm_abs(rin)>30000) f->clip=4000;
    else if(f->clip) f->clip--;
    din=pm_soft(din); rin=pm_soft(rin);
    if(pe.fx_on&1u) {
    /* channel 1: delay, 1 ms .. the whole line, square law */
    dly=f->d1;
    if(mad && mode!=P23_FX_DBL) {
        /* madness: the delay read follows its own output's level, feedback runs past unity, clipped */
        f->mad_env+=pm_sdiv(pm_abs(f->dly_out)-f->mad_env,5);
        dly+=(uint32_t)f->mad_env/4u;
        if(dly>PE_DLY_LEN-64u) dly-=PE_DLY_LEN-96u;
    }
    y=pe_line(0,PE_DLY_LEN,f->p_dly,dly<<8);
    if(mode==P23_FX_BPF) {
        /* BPF in the loop, TUNE its centre (150 Hz .. 6 kHz at the full clock) */
        int32_t cg=f->cg,high;
        if(mad) cg=pm_bound(cg+pm_product(f->mad_env,9000),200,40000);
        high=y-f->bp_low-pm_product(f->bp_band,20000);
        f->bp_band=pm_soft(f->bp_band+pm_product(cg,high));
        f->bp_low=pm_bound(f->bp_low+pm_product(cg,f->bp_band),-65536,65536);
        y=f->bp_band;
    } else if(mode==P23_FX_DBL) tap2=pe_line(0,PE_DLY_LEN,f->p_dly,f->d2<<8);
    else {
        /* PCH: the repeat through a +/- 1 octave shifter in the feedback */
        int32_t ratio=f->ratio;
        if(mad) {
            if(!(f->ticks&4095u)) f->rnd=f->rnd*1664525u+1013904223u;
            ratio=(int32_t)((f->rnd>>16)%3u==0u?32768u:(f->rnd>>16)%3u==1u?65536u:131072u);
        }
        y=pe_fx_shift(PE_DPS_BASE,PE_DPS_LEN,&f->p_dps,&f->shift_a,ratio,y);
    }
    x=mode==P23_FX_DBL?pm_sdiv(y+tap2,1):y;
    fb=pm_product(f->fb_s,mad && mode!=P23_FX_DBL?42000:31500);
    pe_memw(f->p_dly,din+pm_soft(pm_product(x,fb)));
    f->p_dly=pe_next(f->p_dly,PE_DLY_LEN);
    f->dly_out=mode==P23_FX_DBL?y:x; f->dly_r=mode==P23_FX_DBL?tap2:x;
    } else f->dly_out=f->dly_r=0;
    if(!(pe.fx_on&2u)) { f->rev_out=f->rev_r=0; return; }
    /* channel 2: a hall -- four diffusers, then two cross-fed loops of two all-passes and a damped line */
    decay=9000+pm_product(f->fb_s,mad && mode!=P23_FX_DBL?24500:21000);
    x=rin;
    x=pe_fx_ap(PE_REV_BASE,213,&f->p_ap[0],x,20000);
    x=pe_fx_ap(PE_REV_BASE+355u,331,&f->p_ap[2],x,19000);
    x=pe_fx_ap(PE_REV_BASE+686u,527,&f->p_ap[3],x,19000);
    rev=0;
    {
        static const uint16_t base[2]={1213,5563},ap1[2]={953,1009},ap2[2]={0,0},line[2]={3397,3147};
        uint32_t j;
        for(j=0;j<2;j++) {
            uint32_t lb=PE_REV_BASE+base[j]+ap1[j]+ap2[j];
            int32_t in=x+pm_product(f->tank[j^1u],decay),z;
            /* the reverb's shifter runs opposite to the delay's */
            if(mode==P23_FX_PCH && j==0) in=pe_fx_shift(PE_RPS_BASE,PE_RPS_LEN,&f->p_rps,&f->shift_b,f->ratio_r,in);
            if(mode==P23_FX_DBL) in=pm_product(in,31000);
            z=pe_fx_ap(PE_REV_BASE+base[j],ap1[j],&f->p_ap[4u+2u*j],in,j?-17000:17000);
            {
                int32_t tail=pe.fx.mem[lb+f->p_line[j]];     /* (the oldest word: the whole line's delay) */
                rev+=pe.fx.mem[lb+pe_back(f->p_line[j],line[j],line[j]/3u+j*97u)];
                pe_memw(lb+f->p_line[j],z); f->p_line[j]=pe_next(f->p_line[j],line[j]);
                f->damp[j]+=pm_product(tail-f->damp[j],mode==P23_FX_DBL?26000:19000);
            }
            f->tank[j]=mad && mode!=P23_FX_DBL?pm_soft(pm_sdiv(f->damp[j]*5,2)):f->damp[j];
        }
        f->rev_out=pm_sdiv(rev,1)+pm_sdiv(f->tank[0],2);
        f->rev_r=pm_sdiv(rev,1)+pm_sdiv(f->tank[1],2);
    }
}
static PE_FAST void pe_fx_run(int32_t din,int32_t rin)
{
    pe_fx *f=&pe.fx; uint32_t old=f->phase;
    /* the converters' filters (one pole each way), following the clock */
    if(din|f->in_lp[0]) f->in_lp[0]+=pm_product(din-f->in_lp[0],f->pole);
    if(rin|f->in_lp[1]) f->in_lp[1]+=pm_product(rin-f->in_lp[1],f->pole);
    f->phase+=f->inc;
    if(f->phase<old) {
        pe_fx_tick(f->in_lp[0],f->in_lp[1]);
        f->held[0]=f->dly_out; f->held[1]=f->rev_out;
    }
    f->out_lp[0][1]+=pm_product(f->held[0]-f->out_lp[0][1],f->pole);
    f->out_lp[1][1]+=pm_product(f->held[1]-f->out_lp[1][1],f->pole);
}
/* once a block: the processor's clock (CLK MOD x MOD slows it up to 7 times; positive CV slower), MAD! */
static PE_BLOCK void pe_fx_block(void)
{
    pe_fx *f=&pe.fx;
    int32_t cv=pe_driven(P23_PIN_FX_CLK)?pm_bound(pe_cv(P23_PIN_FX_CLK),0,PE_V10):0;
    int32_t oct=-(int32_t)(((int64_t)cv*pe.panel.fx_clk_mod*724)>>14);
    f->inc=(uint32_t)(((uint64_t)pm_frequency(PE_FX_RATE*256u)*pm_exp(oct))>>16);
    f->mad=pe.panel.fx_mad || (pe_driven(P23_PIN_FX_MAD) && pe_in(P23_PIN_FX_MAD)>PE_V5);
    /* the converters' filters follow the clock */
    f->pole=(int32_t)(f->inc>>17); if(f->pole>30000) f->pole=30000; if(f->pole<600) f->pole=600;
}

/* ------------------------------------------------------------------ API --- */
void p23_reset(uint32_t seed)
{
    uint32_t i;
    p23_cable none[P23_CABLES];
    pe_zero(&pe,sizeof pe);
    pe.random=seed?seed:0x73951bdu; pe.master=4096; pe.sh_reg=0x5au; pe.lr_run=1; pe.fx_on=3;
    pe.fx.rnd=seed|1u;
    for(i=0;i<P23_PINS;i++) { pe.flags[i]=(uint8_t)pe_pin_flags(i); pe_live(i,(pe.flags[i]&PF_DRV)!=0); pe.net_of[i]=PE_NONE; }
    pe_live(P23_PIN_DIODE_A,0); pe_live(P23_PIN_DIODE_K,0);
    pe_live(P23_PIN_SW1_IN,0); pe_live(P23_PIN_SW1_OUT,0); pe_live(P23_PIN_SW2_IN,0); pe_live(P23_PIN_SW2_OUT,0);
    pe.panel.bpm10=1200; pe.panel.clock_run=1; pe.panel.shaos_freq=40; pe.panel.lfo_freq=64; pe.panel.lfo_shape=64;
    pe.panel.fx_time=40; pe.panel.fx_tune=64; pe.panel.fx_feedback=50; pe.panel.dly_out=90; pe.panel.rev_out=90;
    pe.panel.atten[0]=pe.panel.atten[1]=pe.panel.atten[2]=pe.panel.atten[3]=127;
    pe.lfo_split=32768u; pe.lfo_up=65536u; pe.lfo_down=65536u;
    for(i=0;i<P23_CABLES;i++) none[i].a=none[i].b=P23_PIN_NONE;
    p23_set_cables(none);
    pe_lr_restart(0);
    for(i=0;i<P23_TRACKS;i++) pe.lr[i].mode=P23_LR_PLAY;
    for(i=0;i<P23_NODES;i++) {
        pe_node *n=&pe.node[i];
        n->note=48; n->glide256=48*256; n->patch.release=50; n->patch.knob[0]=64;
        n->patch.mode=P23_BASS_DCO;
        n->route.enabled=i<P23_TRACKS; n->route.track=(uint8_t)(i%P23_TRACKS); n->route.level=16384;
        n->route.duck_source=255; n->rnd=seed+12347u*(i+1u); n->dir=1; n->mute_gain=32768;
        n->clap_age=0xffffffu;
    }
}
void p23_set_patch(uint32_t index,const p23_patch *patch)
{
    pe_node *n; uint32_t j;
    if(index>=P23_NODES || !patch) return;
    n=&pe.node[index];
    if(n->patch.module!=patch->module%4u || n->patch.mode!=patch->mode) {
        /* another circuit: its own state starts clean; the timebase keeps running */
        pe_zero(&n->filter,sizeof n->filter); pe_zero(n->res,sizeof n->res);
        n->y1=n->y2=0; n->punch=0; n->amp=0; n->kprev=0;
        pe_zero(pe.ks[index],sizeof pe.ks[index]);
    }
    n->patch=*patch; n->patch.module%=4u;
    {   static const uint8_t MODES[4]={5,3,4,4};   /* BASS, BD, SD, HHT */
        if(n->patch.mode>=MODES[n->patch.module]) n->patch.mode=0; }
    for(j=0;j<P23_KNOBS;j++) if(n->patch.knob[j]>127) n->patch.knob[j]=127;
    if(n->patch.attack>127) n->patch.attack=127;
    if(n->patch.release>127) n->patch.release=127;
    if(n->patch.drive>127) n->patch.drive=127;
    if(n->patch.fold>127) n->patch.fold=127;
    if(n->patch.ring>127) n->patch.ring=127;
    if(n->patch.ring_freq>127) n->patch.ring_freq=127;
    if(n->patch.crush>127) n->patch.crush=127;
    if(n->patch.bits>127) n->patch.bits=127;
    for(j=0;j<P23_ROUTES;j++) {
        if(n->patch.mod[j].src>=P23_PINS || n->patch.mod[j].dst>=P23_RDESTS) n->patch.mod[j].src=P23_PIN_NONE;
        if(n->patch.mod[j].depth<-127) n->patch.mod[j].depth=-127;
    }
}
void p23_set_route(uint32_t index,const p23_route *route)
{
    if(index>=P23_NODES || !route) return;
    if(pe.node[index].route.enabled && !route->enabled) p23_hush(index);
    pe.node[index].route=*route;
    pe.node[index].route.track%=P23_TRACKS;
    pe.node[index].route.pan=(int16_t)pm_bound(route->pan,-127,127);
    pe.node[index].route.level=(uint32_t)pm_bound((int32_t)route->level,0,262144);
    if(pe.node[index].route.fx_send>127) pe.node[index].route.fx_send=127;
}
void p23_set_panel(const p23_panel *p)
{
    uint32_t split,i,same=1;
    if(!p) return;
    for(i=0;i<sizeof *p;i++) if(((const uint8_t *)p)[i]!=((const uint8_t *)&pe.panel)[i]) { same=0; break; }
    if(same) return;
    pe.panel=*p;
    pe.panel.clock_source%=3u; pe.panel.shaos_length%=3u; pe.panel.lfo_range%=3u;
    pe.panel.fx_mode%=3u; pe.panel.fx_dly_rev&=1u; pe.panel.fx_mad&=1u;
    if(pe.panel.bpm10<10) pe.panel.bpm10=10;
    split=(uint32_t)pm_bound(p->lfo_shape,0,127)*65535u/127u;
    if(split<64u) split=64u; if(split>65471u) split=65471u;
    pe.lfo_split=split;
    pe.lfo_up=(uint32_t)PE_V10*65536u/split;
    pe.lfo_down=(uint32_t)PE_V10*65536u/(65536u-split);
}
void p23_lr_mode(uint32_t c,uint32_t mode) { if(c<P23_TRACKS) pe.lr[c].mode=(uint8_t)(mode%3u); }
void p23_lr_touch(uint32_t c,uint32_t sensor,int on)
{
    if(c>=P23_TRACKS) return;
    if(sensor==P23_LR_ADD) {
        if(on && !pe.lr[c].add) { pe.node[c].strike=1; pe.node[c].clap_ok=pe_sensor_level()==3u; }
        pe.lr[c].add=(uint8_t)(on!=0);
    } else pe.lr[c].del=(uint8_t)(on!=0);
}
void p23_lr_velocity(uint32_t low,uint32_t middle) { pe.vel_low=(uint8_t)(low!=0); pe.vel_mid=(uint8_t)(middle!=0); }
void p23_lr_bank(uint32_t bank,int copy)
{
    bank%=P23_LR_BANKS;
    if(copy) { pe.copy_from=pe.bank; pe.copying=1; }
    pe.bank=(uint8_t)bank;
}
void p23_lr_copy_end(void) { pe.copying=0; }
void p23_lr_start(uint32_t eighth) { pe_lr_restart((eighth%8u)*(PE_LOOP256/8u)); pe.lr_run=1; }
void p23_lr_stop(void) { pe.lr_run=0; }
void p23_lr_clear(uint32_t c) { if(c<P23_TRACKS) pe_zero(pe.tape[pe.bank][c],PE_LR_BYTES); }
void p23_lr_quantize(uint32_t c)
{
    uint8_t *t; uint32_t i,start=0,level=0,run=0;
    if(c>=P23_TRACKS) return;
    t=pe.tape[pe.bank][c];
    pe_zero(pe.scratch,PE_LR_BYTES);
    /* each note (a run of tape) moves to the nearest 16th (2 ticks), keeping its length and level */
    for(i=0;i<=PE_LR_LEN;i++) {
        uint32_t v=i<PE_LR_LEN?pe_tape_get(t,i):0u;
        if(v && !run) { run=1; start=i; level=v; }
        else if(v && run && v>level) level=v;
        else if(!v && run) {
            uint32_t len=i-start,q=(start+96u)/192u*192u,j;
            for(j=0;j<len;j++) { uint32_t cell=(q+j)%PE_LR_LEN; if(pe_tape_get(pe.scratch,cell)<level) pe_tape_set(pe.scratch,cell,level); }
            run=0;
        }
    }
    for(i=0;i<PE_LR_BYTES;i++) t[i]=pe.scratch[i];
    pe.lr_run=0;
}
void p23_clock_reset(void) { pe.clk_count=127u; pe.clk_phase=0xffffffffu; pe_lr_restart(PE_LOOP256-1u); }
void p23_set_master(uint16_t gain4096) { pe.master=(uint16_t)pm_bound(gain4096,0,4096); }
void p23_set_transform(const p23_transform *x)
{
    if(!x) return;
    pe.xf=*x;
    pe.xf.tracks&=(1u<<P23_TRACKS)-1u;
    pe.xf.stack=(uint8_t)pm_bound(x->stack,1,5);
    if(pe.xf.depth>127) pe.xf.depth=127;
    if(pe.xf.mix>127) pe.xf.mix=127;
}
void p23_transform_hold(int on) { pe.xf_hold=(uint8_t)(on!=0); }
int p23_transform_active(void) { return pe.xf_on; }
PM_INLINE uint32_t pe_xf_next(void) { pe.xf_rnd=pe.xf_rnd*1664525u+1013904223u; return pe.xf_rnd; }
/* once a block: held, the stack (drawn as the hold begins) set by DEPTH, and each
 * node's mix moved toward MIX (its tracks) or 0 (8 blocks, ~6 ms) */
static PE_BLOCK void pe_xf_block(void)
{
    uint32_t k;
    if(pe.xf_hold && !pe.xf_on) {
        uint8_t order[5]={XF_DRIVE,XF_FOLD,XF_RING,XF_CRUSH,XF_BITS};
        pe.xf_rnd^=pe.xf.seed;
        for(k=4;k>0;k--) { uint32_t j=(pe_xf_next()>>16)%(k+1u); uint8_t o=order[k]; order[k]=order[j]; order[j]=o; }
        pe.xf_set=0;
        for(k=0;k<pe.xf.stack;k++) pe.xf_set|=order[k];
        for(k=0;k<6;k++) pe.xf_u[k]=(uint8_t)(pe_xf_next()>>24);
        pe.xf_on=1;
    } else if(!pe.xf_hold) pe.xf_on=0;
    if(pe.xf_on) {
        int32_t q[5];
        for(k=0;k<5;k++) q[k]=pm_bound((int32_t)pe.xf.depth+((int32_t)pe.xf_u[k]-128)/4,0,127);
        pe.xf_gd=16384+q[0]*q[0]*4;
        pe.xf_gf=8192+q[1]*160;
        pe.xf_ring=16384+q[2]*129;
        pe.xf_inc=pm_frequency((uint32_t)(((uint64_t)(30u*256u)*pm_exp((int32_t)pe.xf_u[5]*1700))>>16));
        pe.xf_every=(uint8_t)(3+q[3]/10);
        pe.xf_half=1<<(12+q[4]/64);
        pe.xf_mask=~(pe.xf_half*2-1);
    }
    for(k=0;k<P23_NODES;k++) {
        pe_node *n=&pe.node[k];
        int32_t goal=pe.xf_on && ((pe.xf.tracks>>n->route.track)&1u)?pe.xf.mix*258:0;
        n->xm0=n->xm;
        n->xm+=pm_bound(goal-n->xm,-4096,4096);
    }
}
void p23_copy_usb(int32_t *out) { uint32_t i; if(out) for(i=0;i<P23_BLOCK*2;i++) out[i]=pe.usb[i]; }
void p23_click(int accent)
{ pe.click=6000; pe.click_phase=0; pe.click_step=pm_frequency((accent?1800u:900u)*256u); }

/* ------------------------------------------------------------- process --- */
/* once a block: the panel's rates, and which utility circuits have a pin patched (the others' outputs go
 * nowhere, so they are not computed) */
PM_INLINE int pe_on(uint32_t pin) { return pe.net_of[pin]<PE_NETS; }
static PE_BLOCK void pe_block(void)
{
    const p23_panel *g=&pe.panel; uint32_t u=0,p,cab;
    {   /* INT: the session tempo as 32nd-note ticks, MOD x AMT exponential (3 octaves at 10 V) */
        uint32_t hz256=(uint32_t)g->bpm10*8u*256u/600u;
        int32_t mod=pe_driven(P23_PIN_CLK_MOD)?pe_cv(P23_PIN_CLK_MOD):0;
        hz256=pm_hz(hz256,(int32_t)(((int64_t)mod*g->clock_amt*6)>>7));
        if(hz256>200u*256u*8u) hz256=200u*256u*8u;
        pe.clk_inc=pm_frequency(hz256);
        pe.clk_sub=(uint32_t)(((uint64_t)pe.clk_inc*PE_SUB256)>>32);
    }
    {   /* SHAOS: 0.05 Hz .. ~26 kHz, MOD about 0.8 octave a volt */
        int32_t oct=(int32_t)g->shaos_freq*9800+(pe_driven(P23_PIN_SH_MOD)?pe_cv(P23_PIN_SH_MOD)*16:0);
        pe.sh_inc=pm_frequency(pm_hz(13u,oct));
    }
    if(!pe_driven(P23_PIN_LFO_MOD)) {
        uint32_t lo=g->lfo_range==P23_LFO_HI?2560u:g->lfo_range==P23_LFO_MID?128u:13u;
        pe.lfo_inc=pm_frequency(pm_hz(lo,(int32_t)g->lfo_freq*(g->lfo_range==P23_LFO_LOW?4128:4644)));
    }
    if(pe_on(P23_PIN_VCA1_IN)||pe_on(P23_PIN_VCA1_CV)||pe_on(P23_PIN_VCA1_OUT)) u|=PU_VCA1;
    if(pe_on(P23_PIN_VCA2_IN)||pe_on(P23_PIN_VCA2_CV)||pe_on(P23_PIN_VCA2_OUT)) u|=PU_VCA2;
    if(pe_on(P23_PIN_INV_IN)||pe_on(P23_PIN_INV_OUT)) u|=PU_INV;
    if(pe_on(P23_PIN_CINV_IN)||pe_on(P23_PIN_CINV_CV)||pe_on(P23_PIN_CINV_OUT)) u|=PU_CINV;
    if(pe_on(P23_PIN_SW1_IN)||pe_on(P23_PIN_SW1_CV)||pe_on(P23_PIN_SW1_OUT)) u|=PU_SW1;
    if(pe_on(P23_PIN_SW2_IN)||pe_on(P23_PIN_SW2_CV)||pe_on(P23_PIN_SW2_OUT)) u|=PU_SW2;
    for(p=P23_PIN_ATT1_IN;p<=P23_PIN_ATT4_OUT;p++) if(pe_on(p)) u|=PU_ATT;
    for(p=P23_PIN_IMP1_IN;p<=P23_PIN_IMP2_OUT;p++) if(pe_on(p)) u|=PU_IMP;
    if(pe_on(P23_PIN_DIODE_A)||pe_on(P23_PIN_DIODE_K)) u|=PU_DIODE;
    for(p=P23_PIN_CAP1_A;p<=P23_PIN_CAP2_B;p++) if(pe_on(p)) u|=PU_CAP;
    for(p=P23_PIN_MCV1;p<=P23_PIN_TOUCH2;p++) if(pe_on(p)) u|=PU_SRC;
    for(p=P23_PIN_SH_MOD;p<=P23_PIN_SH_3SH;p++) if(pe_on(p)) u|=PU_SHAOS;
    for(p=P23_PIN_LFO_MOD;p<=P23_PIN_LFO_TRI;p++) if(pe_on(p)) u|=PU_LFO;
    cab=u;
    for(p=0,pe.tpins=0;p<P23_TRACKS;p++) {
        if(pe_on(P23_PIN_T(p,P23_T_OUT))) pe.tpins|=1u<<p;
        if(pe_on(P23_PIN_T(p,P23_T_ENV))) pe.tpins|=256u<<p;
        if(pe_on(P23_PIN_T(p,P23_T_TRIG))) pe.tpins|=65536u<<p;
    }
    pe.tput=pe.tpins&0xffffu;   /* (OUT / ENV pins with a cable: written every sample; routes read the buffers) */
    pe.nrsrc=0;
    for(p=0;p<P23_NODES;p++) {
        /* what the sounds' routes read must be computed too, and is recorded for them a block at a time */
        pe_node *n=&pe.node[p]; uint32_t j;
        n->nr=0; n->own_out=0;
        if(pe.diag&4096u) continue;
        for(j=0;j<P23_ROUTES;j++) {
            uint32_t src=n->patch.mod[j].src,sel,m;
            if(!src || !n->patch.mod[j].depth) continue;
            if(src==P23_PIN_T(n->route.track,P23_T_OUT)) { sel=PE_OWN_OUT; n->own_out=1; }
            else if(src==P23_PIN_T(n->route.track,P23_T_ENV)) sel=PE_OWN_ENV;
            else {
                for(sel=0;sel<pe.nrsrc && pe.rsrc[sel]!=src;sel++) {}
                if(sel==pe.nrsrc) {
                    if(sel<PE_RSRC) {
                        /* sources that carry the +5 V audio bias (OUT, NOISE, the FX outputs) pass their swing only */
                        if(pe.rsrc[sel]!=src) for(m=0;m<P23_BLOCK;m++) pe.rec[sel][m]=0;
                        pe.rsrc[sel]=(uint8_t)src; pe.nrsrc++;
                        /* recorded as: 0 the pin, 1 the pin less its +5 V bias (NOISE, the FX outputs, a cabled OUT),
                         * 2 + t / 8 + t track t's OUT / ENV buffer (no cable on it) */
                        uint32_t tk=(src-1u)%P23_TRACK_PINS,tt=(src-1u)/P23_TRACK_PINS;
                        pe.rbias[sel]=(uint8_t)(src<P23_PIN_CLK && !pe_on(src) && tk==P23_T_OUT ? 2u+tt
                                                : src<P23_PIN_CLK && !pe_on(src) && tk==P23_T_ENV ? 8u+tt
                                                : (src<P23_PIN_CLK && tk==P23_T_OUT) || src==P23_PIN_NOISE
                                                  || src==P23_PIN_FX_DLY_OUT || src==P23_PIN_FX_REV_OUT);
                    } else sel=PE_LIVE;   /* (beyond the recordings' room: not heard) */
                }
            }
            n->rsel[n->nr]=(uint8_t)sel; n->rdst[n->nr]=n->patch.mod[j].dst;
            n->rdep[n->nr]=(int32_t)n->patch.mod[j].depth*258; n->nr++;
            if(sel<PE_RSRC) {
                /* what keeps the source up: a track's OUT / ENV buffer, or a panel module */
                uint32_t tb=0,ub=0;
                if(src<P23_PIN_CLK) {
                    uint32_t tk=(src-1u)%P23_TRACK_PINS,tt=(src-1u)/P23_TRACK_PINS;
                    tb=tk==P23_T_OUT?1u<<tt:tk==P23_T_ENV?256u<<tt:0u;
                } else if(src>=P23_PIN_SH_MOD && src<=P23_PIN_SH_3SH) ub=PU_SHAOS;
                else if(src>=P23_PIN_LFO_MOD && src<=P23_PIN_LFO_TRI) ub=PU_LFO;
                else if(src>=P23_PIN_MCV1 && src<=P23_PIN_TOUCH2) ub=PU_SRC;
                else if(src>=P23_PIN_VCA1_IN && src<=P23_PIN_CAP2_B) ub=PU_VCA1|PU_VCA2|PU_INV|PU_CINV|PU_SW1|PU_SW2|PU_ATT|PU_IMP;
                pe.rtp[sel]=(uint16_t)tb; pe.rub[sel]=(uint16_t)ub; u|=ub;
            }
        }
    }
    /* SHAOS and the LFO read by routes alone run every fourth sample, four steps at a time */
    pe.util_rt=u&~cab&(PU_SHAOS|PU_LFO);
    pe.util_ro=u&~cab;              /* (what only routes read: kept up only while a routed node sounds) */
    if(pe.util_rt&PU_SHAOS) pe.sh_inc=(pe.sh_inc>0x3fffffffu?0x3fffffffu:pe.sh_inc)*4u;
    if(pe.util_rt&PU_LFO) pe.lfo_inc=(pe.lfo_inc>0x3fffffffu?0x3fffffffu:pe.lfo_inc)*4u;
    pe.util=u;
    /* the looper moves only with its clock: the master's running, or a patched one */
    pe.lr_need=(pe.lr_run && (g->clock_source==P23_CLK_EXT || (g->clock_source==P23_CLK_INT && g->clock_run))) || pe_on(P23_PIN_LRST);
    for(p=0;p<P23_TRACKS;p++) { pe.lr_v[p]=pe_lr_out_x(p); if(pe_on(P23_PIN_LR_CLK1+p)) pe.lr_need=1; }
    pe.pink_on=(u&PU_SRC)!=0;
    for(p=0;p<P23_NODES;p++) {
        const pe_node *n=&pe.node[p];
        if(n->route.enabled && (n->patch.module==P23_SD || (n->patch.module==P23_BASS && n->patch.mode==P23_BASS_PRC)) && (n->env || n->gate))
            pe.pink_on=1;
    }
    pe_live(P23_PIN_CLK,g->clock_source!=P23_CLK_EXT);
    pe_fx_block();
}
PM_INLINE void pe_node_gate_body(pe_node *n,uint32_t index)
{
    /* the strongest of: the looper channel, keys / sequencer / MIDI, and whatever drives TRIG */
    int32_t internal=n->note_gate,lr=0,g;
    uint32_t pin=pe_trk_pin(n,P23_T_TRIG);
    if(index<P23_TRACKS) {
        lr=pe.lr_v[index];
        if(lr>internal) internal=lr;
        if(lr && !n->lr_prev) { n->strike=1; n->peaked=0; n->clap_ok=lr>=PE_V10; }
        n->lr_prev=lr!=0;
        if(pe.tpins&(65536u<<index)) { pe_put(pin,internal); g=pe_in_max(pin,internal); }
        else g=internal;
    } else g=internal;
    /* a rising edge from a patched trigger strikes the circuit too, but never the clap */
    if(!n->gate_high && g>PE_V1) { n->gate_high=1; n->peaked=0; if(!n->strike) { n->strike=1; n->clap_ok=0; } }
    else if(n->gate_high && g<PE_V1/2) n->gate_high=0;
    n->gate=g;
}
static PE_FAST void pe_node_gate(pe_node *n,uint32_t index) { pe_node_gate_body(n,index); }
static PE_BLOCK void pe_node_gate_x(pe_node *n,uint32_t index) { pe_node_gate_body(n,index); }
PM_INLINE int32_t pe_circuit(pe_node *n,int32_t ext,int32_t white,int32_t pink)
{
    switch(n->patch.module) {
    case P23_BD: return pe_bd(n,ext);
    case P23_BASS: return pe_bass(n,ext,white);
    case P23_SD: return pe_sd(n,ext,white,pink);
    default: return pe_hht(n,ext,white);
    }
}
/* (the per-sample path) the circuit, its overdrive and VCA route */
PM_INLINE int32_t pe_voice(pe_node *n,int32_t ext,int32_t white,int32_t pink)
{
    int32_t x=pe_circuit(n,ext,white,pink);
    if(n->patch.drive && !(pe.diag&2048u)) pe_drive_run(n,&x,(n->rmask&(1u<<P23_RD_DRIVE))?&n->rv[P23_RD_DRIVE]:0,1);
    if(pe_wrecked(n)) pe_wreck_run(n,&x,1);
    if(n->xm|n->xm0) pe_xf_run(n,&x,1,n->xm,n->xm);
    if(n->rmask&(1u<<P23_RD_VCA)) x=pm_product(x,pm_bound(PE_V10+pe_route(n,P23_RD_VCA),0,2*PE_V10));   /* x (1 + v / 10 V) */
    return x;
}
/* a silent node: the VCA is shut, so OUT carries nothing; the free-running cores keep their time (advanced a
 * block at once), the circuit's state rests */
PM_INLINE void pe_idle_body(pe_node *n)
{
    n->phase+=n->inc*P23_BLOCK;
    if(n->patch.module==P23_BD) {
        /* the triangle core, unfolded over a cycle (4 x 10 V), runs on */
        uint32_t u=(uint32_t)(n->tri+PE_V10);
        if(n->dir<0) u=4u*PE_V10-u;
        u=(u+(uint32_t)n->step*P23_BLOCK)%(4u*PE_V10);
        if(u<2u*PE_V10) { n->tri=(int32_t)u-PE_V10; n->dir=1; }
        else { n->tri=3*PE_V10-(int32_t)u; n->dir=-1; }
    }
    n->out=0; n->strike=0;
}
static PE_FAST void pe_idle(pe_node *n) { pe_idle_body(n); }
static PE_BLOCK void pe_idle_x(pe_node *n) { pe_idle_body(n); }
/* the VCA, OUT, and the channel strip into the block's buses (and the track's OUT / ENV, when patched) */
PM_INLINE void pe_strip(pe_node *n,int32_t raw,uint32_t i,int32_t *l,int32_t *r,int32_t *s)
{
    int32_t out=(int32_t)(((int64_t)pm_bound(raw,-PE_V10,PE_V10)*(n->env>>9))>>15),is=(int32_t)i;
    uint32_t t=n->route.track;
    n->out=out;
    if(pe.tpins&(1u<<t)) pe.tout[0][t][i]=pm_bound(pe.tout[0][t][i]+out,-PE_V10,PE_V10);
    if((pe.tpins&(256u<<t)) && (n->env>>9)>pe.tenv[0][t][i]) pe.tenv[0][t][i]=n->env>>9;
    if(out) {
        if(pm_abs(out)>n->peak) n->peak=pm_abs(out);
        *s+=pm_product(out,n->gs+n->dgs*is);
        *l+=pm_product(out,n->gl+n->dgl*is); *r+=pm_product(out,n->gr+n->dgr*is);
    }
}
/* a node with no cable into it: its gate (keys, sequencer, MIDI, the looper channel) is the block's and its routes
 * read recordings, so it renders the whole block at once, into one of two sets of buses (one a core), in passes:
 * the envelope, the circuit (its routes following the envelope), the overdrive and VCA route, then the strip */
static PE_FAST void pe_node_block(pe_node *n,uint32_t part)
{
    int32_t eb[P23_BLOCK],raw[P23_BLOCK],gm[P23_BLOCK],vm[P23_BLOCK];
    uint32_t i,t=n->route.track,to=pe.tpins&(1u<<t),te=pe.tpins&(256u<<t),on=n->route.enabled && !(pe.diag&2u);
    uint32_t rext=n->rmask&(1u<<P23_RD_EXT),rdrv=n->rmask&(1u<<P23_RD_DRIVE),rvca=n->rmask&(1u<<P23_RD_VCA);
    int32_t gl=n->gl,gr=n->gr,gs=n->gs,dgl=n->dgl,dgr=n->dgr,dgs=n->dgs,peak=n->peak,out=n->out;
    int32_t *bl=pe.bl[part],*br=pe.br[part],*bs=pe.bs[part],*tout=pe.tout[part][t],*tenv=pe.tenv[part][t];
    {   /* the envelope: linear attack toward the gate, exponential release */
        int32_t env=n->env,peak=n->gate<<9,target=pe_held(n)?peak:0,ar=(int32_t)n->att_rate; uint32_t rk=n->rel_k;
        for(i=0;i<P23_BLOCK;i++) {
            if(env<target) { env+=ar; if(env>=target) { env=target; if(n->patch.perc) { n->peaked=1; target=0; } } }
            else if(env>target) { env-=(int32_t)(((int64_t)(env-target)*rk)>>31)+1; if(env<target) env=target; }
            eb[i]=env>>9;
        }
        n->env=env;
    }
    if(on) {
        n->eb=eb;
        for(i=0;i<P23_BLOCK;i++) {
            n->now=pe.samples+i;
            if(n->nr && (rext || !(i&3u))) pe_routes(n);
            raw[i]=pe_circuit(n,rext?pe_ext(n):0,pe.wbuf[i],pe.pbuf[i]);
            gm[i]=n->rv[P23_RD_DRIVE]; vm[i]=n->rv[P23_RD_VCA];
            n->strike=0;
        }
        n->eb=0;
        if(n->patch.drive && !(pe.diag&2048u)) pe_drive_run(n,raw,rdrv?gm:0,P23_BLOCK);
        if(pe_wrecked(n)) pe_wreck_run(n,raw,P23_BLOCK);
        if(n->xm|n->xm0) pe_xf_run(n,raw,P23_BLOCK,n->xm0,n->xm);
        if(rvca) for(i=0;i<P23_BLOCK;i++) raw[i]=pm_product(raw[i],pm_bound(PE_V10+vm[i],0,2*PE_V10));   /* x (1 + v / 10 V) */
    } else { for(i=0;i<P23_BLOCK;i++) raw[i]=0; n->strike=0; }
    for(i=0;i<P23_BLOCK;i++) {
        int32_t e=eb[i];
        out=(int32_t)(((int64_t)pm_bound(raw[i],-PE_V10,PE_V10)*e)>>15);
        if(to) tout[i]=pm_bound(tout[i]+out,-PE_V10,PE_V10);
        if(te && e>tenv[i]) tenv[i]=e;
        if(out) {
            if(pm_abs(out)>peak) peak=pm_abs(out);
            bs[i]+=pm_product(out,gs); bl[i]+=pm_product(out,gl); br[i]+=pm_product(out,gr);
        }
        gl+=dgl; gr+=dgr; gs+=dgs;
    }
    n->out=out; n->peak=peak;
}
/* roughly what a sounding node costs a block, to share them between the cores */
PM_INLINE uint32_t pe_cost(const pe_node *n)
{
    static const uint8_t C[4]={4,6,5,4};
    uint32_t c=C[n->patch.module&3u]+(n->patch.mode?(n->patch.module==P23_BASS && n->patch.mode==P23_BASS_PRC?2u:1u):0u);
    return c+(n->patch.drive?1u:0u)+(pe_wrecked(n)?1u:0u)+((n->xm|n->xm0)?1u+pe.xf.stack/3u:0u)+n->nr;
}
/* the block's first part (one core): the panel, every node's gate and preparation; silent nodes rest, those with
 * a cable into them follow the patch sample by sample, the others are shared between the two parts */
uint32_t p23_process_begin(void)
{
    uint32_t i,k,t,load[2]={0,0},parts=0,live=0;
    pe.todo[0]=pe.todo[1]=0; pe.slow=0;
    if(pe.diag&32u) return 0;
    if(pe.diag&64u) for(k=0;k<P23_TRACKS;k++) pe.node[k].note_gate=PE_V10;
    pe_block();
    pe_xf_block();
    if(!(pe.diag&16u)) for(k=0;k<P23_NODES;k++) pe_prepare(&pe.node[k],k);
    /* the block's noise, its buses, and the tracks' patched OUT / ENV */
    for(i=0;i<P23_BLOCK;i++) { pe_noise(); pe.wbuf[i]=pe.white; pe.pbuf[i]=pe.pink_out; }
    for(k=0;k<P23_NODES && !(pe.diag&256u);k++) {
        pe_node *n=&pe.node[k]; uint32_t c;
        if(k<P23_TRACKS && (pe.tpins&(65536u<<k))) { pe.slow|=1u<<k; for(c=0;c<n->nr;c++) if(n->rsel[c]<PE_RSRC) live|=1u<<n->rsel[c]; continue; }
        if(!(pe.diag&4u)) {
            pe_node_gate_x(n,k);
            if(n->strike && n->patch.module==P23_SD) n->clap_age=n->clap_ok?0u:0xffffffu;
        }
        if(!n->env && !n->strike && !pe_held(n)) { pe_idle_x(n); continue; }
        for(c=0;c<n->nr;c++) if(n->rsel[c]<PE_RSRC) live|=1u<<n->rsel[c];   /* (the recorded sources it reads) */
        if(n->cabled || n->own_out) { pe.slow|=1u<<k; continue; }   /* (own OUT fed back: sample by sample) */
        c=pe_cost(n);
        if(load[1]<=load[0]) { load[1]+=c; pe.todo[1]|=1u<<k; parts|=2u; }
        else { load[0]+=c; pe.todo[0]|=1u<<k; parts|=1u; }
    }
    /* what routes read is recorded only while a routed node sounds; a node waking reads none for a block */
    {
        uint32_t ub=0;
        live&=(1u<<PE_RSRC)-1u;
        for(k=0;k<PE_RSRC;k++) {
            if((live>>k)&1u) { pe.tpins|=pe.rtp[k]; ub|=pe.rub[k]; }
            else if((pe.rneed>>k)&1u) for(i=0;i<P23_BLOCK;i++) pe.rec[k][i]=0;
        }
        pe.rneed=live;
        pe.adv=pe.util&pe.util_ro&~ub&(PU_SHAOS|PU_LFO);
        pe.util&=~(pe.util_ro&~ub);
    }
    for(k=0;k<2;k++) {
        for(i=0;i<P23_BLOCK;i++) pe.bl[k][i]=pe.br[k][i]=pe.bs[k][i]=0;
        for(t=0;t<P23_TRACKS;t++) {
            if(pe.tpins&(1u<<t)) for(i=0;i<P23_BLOCK;i++) pe.tout[k][t][i]=0;
            if(pe.tpins&(256u<<t)) for(i=0;i<P23_BLOCK;i++) pe.tenv[k][t][i]=0;
        }
    }
    return parts;
}
/* one part's nodes (either core; the two touch nothing in common) */
PE_HOT void p23_process_part(uint32_t part)
{
    uint32_t k,m;
    if(part>1u) return;
    m=pe.todo[part];
    for(k=0;m>>k;k++) if((m>>k)&1u) pe_node_block(&pe.node[k],part);
}
/* a part lost (the other core failed): its buses dropped, as silence for one block */
void p23_process_drop(uint32_t part)
{
    uint32_t i,t;
    if(part>1u) return;
    for(i=0;i<P23_BLOCK;i++) {
        pe.bl[part][i]=pe.br[part][i]=pe.bs[part][i]=0;
        for(t=0;t<P23_TRACKS;t++) pe.tout[part][t][i]=pe.tenv[part][t][i]=0;
    }
}
/* the block's last part (the first core): the parts' buses joined, then the patch, the nodes with cables, the FX
 * and the output sample by sample */
PE_HOT void p23_process_end(int32_t stereo[P23_BLOCK*2])
{
    uint32_t i,k,t,fx_in=0; int32_t fx_peak[2]={0,0};
    if(pe.diag&32u) { for(i=0;i<P23_BLOCK*2u;i++) stereo[i]=0; return; }
    if(pe.todo[1]) for(i=0;i<P23_BLOCK;i++) {
        pe.bl[0][i]+=pe.bl[1][i]; pe.br[0][i]+=pe.br[1][i]; pe.bs[0][i]+=pe.bs[1][i];
        if(pe.tpins&0xffffu) for(t=0;t<P23_TRACKS;t++) {
            if(pe.tpins&(1u<<t)) pe.tout[0][t][i]=pm_bound(pe.tout[0][t][i]+pe.tout[1][t][i],-PE_V10,PE_V10);
            if((pe.tpins&(256u<<t)) && pe.tenv[1][t][i]>pe.tenv[0][t][i]) pe.tenv[0][t][i]=pe.tenv[1][t][i];
        }
    }
    for(i=0;i<P23_BLOCK;i++) {
        int32_t l=pe.bl[0][i],r=pe.br[0][i],send=pe.bs[0][i],send_d,send_r,mix_in,j;
        if(!(pe.diag&8u)) { if(pe.nets) pe_nets(); pe_clock(); if(pe.lr_need) pe_looper(); }
        pe.white=pe.wbuf[i]; pe.pink_out=pe.pbuf[i];
        if(!(pe.diag&1024u)) {
            if((pe.util&PU_SHAOS) && (!(pe.util_rt&PU_SHAOS) || !(i&3u))) pe_shaos();
            if((pe.util&PU_LFO) && (!(pe.util_rt&PU_LFO) || !(i&3u))) pe_lfo();
            if(pe.util&~(PU_SHAOS|PU_LFO)) pe_utilities();
        }
        for(k=0;(pe.slow>>k) && !(pe.diag&8192u);k++) if((pe.slow>>k)&1u) {
            pe_node *n=&pe.node[k]; int32_t raw=0;
            n->now=pe.samples;
            if(!(pe.diag&4u)) {
                pe_node_gate(n,k);
                if(n->strike && n->patch.module==P23_SD) n->clap_age=n->clap_ok?0u:0xffffffu;
                if(n->env!=n->gate<<9 || n->patch.perc) pe_envelope(n);
            }
            if(!n->env && !n->strike && !pe_held(n)) {
                if(i==0) pe_idle(n);
                n->out=0; n->strike=0;
                continue;
            }
            if(n->route.enabled && !(pe.diag&2u)) {
                if(n->nr) pe_routes(n);
                raw=pe_voice(n,pe_ext(n),pe.white,pe.pink_out);
            }
            n->strike=0;
            pe_strip(n,raw,i,&l,&r,&send);
        }
        send_d=pe.panel.fx_dly_rev?0:send; send_r=pe.panel.fx_dly_rev?send:0;
        if(pe.tput) for(t=0;t<P23_TRACKS;t++) {
            if(pe.tput&(1u<<t)) pe_put(P23_PIN_T(t,P23_T_OUT),PE_V5+pe.tout[0][t][i]);
            if(pe.tput&(256u<<t)) pe_put(P23_PIN_T(t,P23_T_ENV),pe.tenv[0][t][i]);
        }
        /* the FX: both channels run while anything reaches them or still sounds (they rest after PE_FX_QUIET
         * silent blocks, longer than the longest repeat); their aux inputs are AC-coupled */
        {
            int32_t dx=pe_driven(P23_PIN_FX_DLY_IN)?pe_in(P23_PIN_FX_DLY_IN)-PE_V5:0;
            int32_t rx=pe_driven(P23_PIN_FX_REV_IN)?pe_in(P23_PIN_FX_REV_IN)-PE_V5:0;
            if(send_d|dx) { fx_in|=1u; pe.fx_on|=1u; }
            if(send_r|rx) { fx_in|=2u; pe.fx_on|=2u; }
            if(pe.fx_on && !(pe.diag&1u)) {
                int32_t a;
                pe_fx_run(send_d+dx,send_r+rx);
                a=pm_abs(pe.fx.out_lp[0][1])|pm_abs(pe.fx.dly_r); if(a>fx_peak[0]) fx_peak[0]=a;
                a=pm_abs(pe.fx.out_lp[1][1])|pm_abs(pe.fx.rev_r); if(a>fx_peak[1]) fx_peak[1]=a;
            }
        }
        pe_put(P23_PIN_FX_DLY_OUT,PE_V5+pe.fx.out_lp[0][1]);
        pe_put(P23_PIN_FX_REV_OUT,PE_V5+pe.fx.out_lp[1][1]);
        {
            int32_t dl=pm_product(pe.fx.out_lp[0][1],pe.panel.dly_out*258),rv=pm_product(pe.fx.out_lp[1][1],pe.panel.rev_out*258);
            if(pe.panel.fx_mode==P23_FX_DBL && pe.fx.mad) {
                /* DBL with MAD! high: stereo, the two taps and the reverb's two loops apart */
                int32_t dr=pm_product(pe.fx.dly_r,pe.panel.dly_out*258),rr=pm_product(pe.fx.rev_r,pe.panel.rev_out*258);
                l+=dl+rv; r+=dr+rr;
            } else { l+=dl+rv; r+=dl+rv; }
        }
        /* MIX IN, AC-coupled */
        mix_in=pe_driven(P23_PIN_MIX_IN)?pe_in(P23_PIN_MIX_IN):0;
        mix_in=pe_driven(P23_PIN_MIX_IN)?pe_hp(&pe.mix_dc,mix_in,30):0;
        l+=pm_sdiv(mix_in,1); r+=pm_sdiv(mix_in,1);
        for(j=0;j<2;j++) {
            int32_t x=j?r:l;
            if(pe.diag&128u) { stereo[2u*i+j]=0; continue; }
            if(pe.panel.drive_mix) {
                /* parallel distortion over the whole bus: high gain into an asymmetric clipper */
                int32_t gain=32768+(int32_t)pe.panel.drive*pe.panel.drive*64,wet;
                wet=pm_soft(pm_product(x,gain)+3000)-pm_soft(3000);
                wet=pm_sdiv(pm_soft(wet*2),1);
                pe.dist_acc[j]+=wet;
                wet-=pm_sdiv(pe.dist_dc[j],8);
                x+=pm_product(wet-x,pe.panel.drive_mix*258);
            }
            x=pm_bound(x,-120000,120000);
            pe.out_acc[j]+=x;
            x-=pm_sdiv(pe.out_dc[j],8);
            if(pe.click) x+=pm_product(pm_sine(pe.click_phase),pe.click);
            pe.usb[2u*i+j]=pm_bound(pm_soft(x*2),-32767,32767);   /* (6 dB hotter into the output's soft clip) */
            stereo[2u*i+j]=pm_product(pe.usb[2u*i+j],pe.master*8);
        }
        if(pe.click) { pe.click-=24; if(pe.click<0) pe.click=0; pe.click_phase+=pe.click_step; }
        /* what the routes read, recorded for the next block's voices */
        for(k=0;pe.rneed>>k;k++) if((pe.rneed>>k)&1u) {
            uint32_t b=pe.rbias[k];
            pe.rec[k][i]=b>=8u?pe.tenv[0][b-8u][i]:b>=2u?pe.tout[0][b-2u][i]:pe_src(pe.rsrc[k])-(b?PE_V5:0);
        }
        pe.samples++;
    }
    if(pe.adv&PU_SHAOS) {   /* (read by routes alone, none sounding: SHAOS and the LFO keep time a block at once) */
        uint32_t s0=pe.sh_inc; pe.sh_inc=(s0>0x1fffffffu?0x1fffffffu:s0)*8u; pe_shaos(); pe.sh_inc=s0;
    }
    if(pe.adv&PU_LFO) { uint32_t l0=pe.lfo_inc; pe.lfo_inc=(l0>0x1fffffffu?0x1fffffffu:l0)*8u; pe_lfo(); pe.lfo_inc=l0; }
    for(k=0;k<2;k++) {   /* each channel rests on its own (the DLY / REV switch feeds one) */
        if(((fx_in>>k)&1u) || fx_peak[k]>16) pe.fx_quiet[k]=0;
        else if(pe.fx_quiet[k]<PE_FX_QUIET) pe.fx_quiet[k]++;
        else pe.fx_on&=~(1u<<k);
    }
    for(k=0;k<P23_NODES;k++) pe.node[k].peak-=pe.node[k].peak/16;
    for(i=0;i<2;i++) {
        /* DC blocking, once a block from the block's mean: ~1.7 Hz at the output, ~13 Hz after the distortion */
        pe_lp(&pe.out_dc[i],pe.out_acc[i]/(int32_t)P23_BLOCK,256);
        pe_lp(&pe.dist_dc[i],pe.dist_acc[i]/(int32_t)P23_BLOCK,1920);
        pe.out_acc[i]=pe.dist_acc[i]=0;
    }
}
/* one core: the three parts in turn */
void p23_process(int32_t stereo[P23_BLOCK*2])
{
    /* (from flash to RAM code: through pointers, which reach any address) */
    void (*volatile part)(uint32_t)=p23_process_part; void (*volatile end)(int32_t *)=p23_process_end;
    p23_process_begin(); part(0); part(1); end(stereo);
}
p23_meter p23_inspect(uint32_t index)
{
    p23_meter m; pe_zero(&m,sizeof m);
    if(index<P23_NODES) {
        pe_node *n=&pe.node[index];
        m.envelope=n->env>>9; m.peak=n->peak; m.out=n->out;
        m.oscillator_phase=n->patch.module==P23_BD?(uint32_t)(n->tri+PE_V10)<<15:n->phase;
        m.active=(n->env>>9)>8; m.gate=n->gate>PE_V1/2; m.note=n->note; m.velocity=n->velocity;
    }
    return m;
}
void p23_status_get(p23_status *s)
{
    uint32_t c;
    if(!s) return;
    pe_zero(s,sizeof *s);
    s->position=pe.lr[0].pos>>8; s->running=pe.lr_run; s->bank=pe.bank; s->tick=pe.clk_count;
    s->clock_led=pe.clk_led;
    for(c=0;c<P23_TRACKS;c++) {
        uint32_t i,n=0;
        s->mode[c]=pe.lr[c].mode; s->level[c]=pe.lr[c].last_level;
        for(i=0;i<PE_LR_BYTES;i++) n|=pe.tape[pe.bank][c][i];
        s->events[c]=n!=0;
    }
    s->shaos=pe.sh_3; s->lfo_led=pe.lfo_phase<0x80000000u; s->clip=pe.fx.clip>0;
}
uint32_t p23_active_count(void)
{ uint32_t i,n=0; for(i=0;i<P23_NODES;i++) n+=(pe.node[i].env>>9)>8; return n; }
uint32_t p23_state_bytes(void) { return sizeof pe; }
void p23_diagnostic(uint32_t mask)
{
    uint32_t k;
    if((pe.diag&64u) && !(mask&64u)) for(k=0;k<P23_TRACKS;k++) pe_note_gate(&pe.node[k]);
    pe.diag=mask;
}
uint32_t p23_diag_info(uint32_t i)
{
    uint32_t k,m=0;
    switch(i) {
    case 0: return pe.util;
    case 1: return pe.slow;
    case 2: return pe.tpins&0xffffu;
    case 3: return pe.nets|(uint32_t)pe.pink_on<<8;
    default: for(k=0;k<P23_NODES;k++) if(pe.node[k].mods) m|=1u<<k; return m|(uint32_t)pe.fx.mad<<12;
    }
}
