/* SPDX-License-Identifier: GPL-3.0-only
 * Links only the engine, never the UI, sequencer, Bigorna DSP or its tables.
 * Checks the virtual panel's documented behaviours: circuits, envelopes and gate merging, the clock and
 * divider array, the looper-recorder, SHAOS lengths, the patchbay rules, utilities, FX and the master. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../firmware/engine/p23_engine.h"
static int failures;
#define CHECK(c,m) do { if(!(c)){fprintf(stderr,"FAIL standalone: %s\n",m);failures++;} } while(0)
#define V(x) ((int32_t)((x)*P23_VOLT))
static int32_t out[P23_BLOCK*2];
static p23_cable cab[P23_CABLES];
static p23_panel panel;

static void blocks(uint32_t n) { while(n--) p23_process(out); }
static void no_cables(void) { memset(cab,0,sizeof cab); p23_set_cables(cab); }
static void patch_pins(uint32_t slot,uint32_t a,uint32_t b) { cab[slot].a=(uint8_t)a; cab[slot].b=(uint8_t)b; p23_set_cables(cab); }
static p23_patch module(uint32_t m,uint32_t v)
{
    p23_patch p; memset(&p,0,sizeof p);
    p.module=(uint8_t)m; p.mode=P23_BASS_DCO; p.attack=(uint8_t)(v%3); p.release=(uint8_t)(40+v%40);
    switch(m) {
    case P23_BD: p.knob[P23_BD_TUNE]=(uint8_t)(40+v%40); p.knob[P23_BD_PITCH]=(uint8_t)(30+v%80); p.knob[P23_BD_DRIVE]=(uint8_t)(v*5%128); break;
    case P23_BASS: p.knob[P23_BASS_TUNE]=64; p.knob[P23_BASS_SHAPE]=(uint8_t)(v*7%128); p.knob[P23_BASS_WARP]=(uint8_t)(v*3%100);
        p.knob[P23_BASS_LPF]=(uint8_t)(50+v%70); p.knob[P23_BASS_Q]=(uint8_t)(v*11%110); p.mode=(uint8_t)(v%3==2?P23_BASS_PRC:P23_BASS_DCO); p.drive=(uint8_t)(v*13%128); break;
    case P23_SD: p.knob[P23_SD_TUNE]=(uint8_t)(v*9%128); p.knob[P23_SD_CLAP]=(uint8_t)(v%2?64:0); p.knob[P23_SD_MIX]=(uint8_t)(v*13%128);
        p.knob[P23_SD_BPF]=(uint8_t)(50+v%60); p.knob[P23_SD_Q]=(uint8_t)(v*5%100); break;
    default: p.knob[P23_HHT_TUNE]=(uint8_t)(v*9%128); p.knob[P23_HHT_WARP]=(uint8_t)(v*17%128); p.knob[P23_HHT_HPF]=(uint8_t)(60+v%60);
        p.knob[P23_HHT_Q]=(uint8_t)(v*7%100); break;
    }
    return p;
}
static void solo(uint32_t node)
{
    uint32_t i; p23_route r;
    for(i=0;i<P23_NODES;i++) {
        memset(&r,0,sizeof r); r.duck_source=255; r.track=(uint8_t)(i%P23_TRACKS); r.level=32768;
        r.enabled=i==node; p23_set_route(i,&r);
    }
}
static void fresh(uint32_t seed)
{
    p23_reset(seed); memset(&panel,0,sizeof panel);
    panel.bpm10=1200; panel.clock_run=1; panel.shaos_freq=64; panel.lfo_freq=64; panel.lfo_shape=64;
    panel.fx_time=40; panel.fx_tune=64; panel.fx_feedback=50; panel.atten[0]=panel.atten[1]=panel.atten[2]=panel.atten[3]=127;
    p23_set_panel(&panel); no_cables();
    { uint32_t c; for(c=0;c<P23_TRACKS;c++) p23_lr_mode(c,P23_LR_PLAY); }
}
static uint64_t energy(uint32_t n,int32_t *peak)
{
    uint64_t e=0; uint32_t k,i; *peak=0;
    for(k=0;k<n;k++) { p23_process(out); for(i=0;i<P23_BLOCK*2;i++) { int32_t a=abs(out[i]); e+=(uint64_t)a*a; if(a>*peak)*peak=a; } }
    return e;
}
/* the period of a sequence: the least p it repeats with over the tail */
static uint32_t period(const uint8_t *s,uint32_t n)
{
    uint32_t p,i;
    for(p=1;p<n/3;p++) { for(i=n/3;i+p<n;i++) if(s[i]!=s[i+p]) break; if(i+p>=n) return p; }
    return 0;
}

static void test_circuits(void)
{
    uint32_t m,v;
    for(m=0;m<4;m++) for(v=0;v<48;v++) {
        p23_patch p=module(m,v); int32_t peak; uint64_t e;
        fresh(100+v); solo(0); p23_set_patch(0,&p);
        p23_note(0,0,m==P23_BASS?48:60,127,1);
        e=energy(120,&peak);
        CHECK(e>0 && peak>40,"every circuit speaks while gated");
        CHECK(peak<=32767,"output bounded");
        p23_note(0,0,m==P23_BASS?48:60,0,0);
        blocks(20000);
        e=energy(40,&peak);
        CHECK(peak<200,"release reaches silence (DC removed)");
    }
}
static void test_envelope(void)
{
    p23_patch p=module(P23_SD,4); p23_meter a,b;
    fresh(7); solo(0); p.attack=0; p.release=60; p23_set_patch(0,&p);
    p23_note(0,0,60,127,1); blocks(400);
    a=p23_inspect(0); blocks(800); b=p23_inspect(0);
    CHECK(a.envelope>V(9.5) && b.envelope==a.envelope,"AR holds its sustain while the gate is held");
    p23_note(0,1,62,64,1); blocks(100);
    CHECK(p23_inspect(0).envelope==a.envelope,"strongest gate wins (a weaker source does not lower it)");
    p23_note(0,0,60,0,0); blocks(3000);
    b=p23_inspect(0);
    CHECK(b.envelope>V(4.5) && b.envelope<V(5.5),"weaker held source revealed when the strongest stops");
    p23_note(0,1,62,0,0); blocks(4000);
    CHECK(p23_inspect(0).envelope<V(1),"released");
}
static void test_bd_phase(void)
{
    p23_patch p=module(P23_BD,0); uint32_t a,b;
    fresh(3); solo(0); p23_set_patch(0,&p);
    p23_note(0,0,60,127,1); blocks(3); p23_note(0,0,60,0,0); blocks(37);
    a=p23_inspect(0).oscillator_phase; blocks(1);
    b=p23_inspect(0).oscillator_phase;
    CHECK(a!=b,"BD core keeps running with the VCA closed");
    p23_note(0,0,60,127,1); blocks(1);
    CHECK(p23_inspect(0).oscillator_phase!=0,"BD is free running: a hit does not reset its phase");
    p.knob[P23_BD_STAB]=1; p23_set_patch(0,&p); p23_note(0,0,60,0,0); p23_note(0,0,60,127,1);
    CHECK(p23_inspect(0).oscillator_phase==1u<<30,"STAB restarts the core on a hit");
}
static uint64_t clap_energy(int trig_pin,uint32_t velocity)
{
    p23_patch p=module(P23_SD,0); uint64_t e=0; uint32_t k,i,early=0;
    fresh(11); solo(0); p.knob[P23_SD_CLAP]=127; p.knob[P23_SD_MIX]=0; p.release=70; p23_set_patch(0,&p);
    if(trig_pin) patch_pins(0,P23_PIN_P10V,P23_PIN_SW1_OUT),patch_pins(1,P23_PIN_SW1_IN,P23_PIN_P10V),patch_pins(2,P23_PIN_SW1_OUT,P23_PIN_T(0,P23_T_TRIG)),
        patch_pins(3,P23_PIN_SW1_CV,P23_PIN_P10V);
    else p23_note(0,0,60,velocity,1);
    for(k=0;k<60;k++) { p23_process(out); for(i=0;i<P23_BLOCK*2;i+=2) { int32_t x=out[i]; uint32_t t=k*P23_BLOCK+i/2; if(t>200 && t<1800) e+=(uint64_t)x*x; (void)early; } }
    return e;
}
static void test_clap(void)
{
    uint64_t plain=clap_energy(0,100),clap=clap_energy(0,127),trig=clap_energy(1,127);
    CHECK(clap<plain*3/4,"velocity >= 113 splits the attack (the clap)");
    CHECK(trig>clap,"no clap from a patched TRIG");
}
static void test_clock(void)
{
    uint32_t k,edges=0,last=0,first=0,hi=0,ticks;
    fresh(5);
    for(k=0;k<4000;k++) {
        p23_status s; p23_process(out); p23_status_get(&s);
        if(p23_pin_value(P23_PIN_DIV16)>V(5) && !hi) { hi=1; edges++; if(edges==2) first=k; if(edges==6) last=k; }
        else if(p23_pin_value(P23_PIN_DIV16)<V(5)) hi=0;
    }
    ticks=(last-first)*P23_BLOCK;
    /* 120 BPM: 16 ticks a second (32nds); DIV16 rises every 2 ticks */
    CHECK(ticks>4*5400 && ticks<4*5640,"divider 16: a cycle every 2 ticks of the 32nd-note clock");
    {
        p23_status s; p23_clock_reset(); blocks(2); p23_status_get(&s);
        CHECK(s.tick==0 && p23_pin_value(P23_PIN_DIV025)>V(9),"RST: dividers at the loop start, all high");
    }
}
static void test_looper(void)
{
    p23_patch p=module(P23_HHT,1); uint32_t k,gates=0,hi=0,first=0;
    p23_status s;
    fresh(9); solo(0); p.release=0; p23_set_patch(0,&p);
    p23_lr_mode(0,P23_LR_REC); p23_clock_reset();
    /* record a note at tick 16 (1/8 of the loop) held for 2 ticks */
    for(k=0;k<2000;k++) {
        p23_process(out); p23_status_get(&s);
        if(s.position>=16*96 && s.position<18*96) p23_lr_touch(0,P23_LR_ADD,1); else p23_lr_touch(0,P23_LR_ADD,0);
    }
    p23_lr_touch(0,P23_LR_ADD,0);
    p23_lr_mode(0,P23_LR_PLAY);
    /* one loop is 128 ticks = 8 s at 120 BPM; listen to the next pass */
    for(k=0;k<13000;k++) {
        p23_process(out); p23_status_get(&s);
        if(p23_inspect(0).gate && !hi) { hi=1; gates++; if(!first) first=s.position; }
        else if(!p23_inspect(0).gate) hi=0;
    }
    CHECK(gates>=1,"looper plays back what ADD recorded");
    CHECK(first>=16*96-4 && first<=16*96+8,"playback at the recorded position");
    p23_lr_mode(0,P23_LR_MUTE); hi=0; gates=0;
    for(k=0;k<11100;k++) { p23_process(out); if(p23_inspect(0).gate && !hi) { hi=1; gates++; } else if(!p23_inspect(0).gate) hi=0; }
    CHECK(gates==0,"MUTE silences the channel while the tape moves");
    /* banks are independent, quantize moves to 16ths */
    p23_lr_bank(1,0); p23_status_get(&s); CHECK(s.bank==1 && !s.events[0],"another bank is empty");
    p23_lr_bank(0,0); p23_status_get(&s); CHECK(s.events[0],"the first bank kept its loop");
    p23_lr_quantize(0); p23_status_get(&s); CHECK(!s.running,"quantize stops the looper");
    p23_lr_start(0); p23_status_get(&s); CHECK(s.running,"start");
    p23_lr_clear(0); p23_status_get(&s); CHECK(!s.events[0],"clear");
    /* LRST restarts the loop */
    fresh(9); blocks(3000); patch_pins(0,P23_PIN_P10V,P23_PIN_LRST); blocks(2); p23_status_get(&s);
    CHECK(s.position<4,"LRST restarts the looper");
}
static void test_shaos(void)
{
    uint32_t mode;
    static const uint32_t want[3]={63,16,217};
    for(mode=0;mode<3;mode++) {
        uint8_t seq[1400]; uint32_t n=0,hi=0,k;
        fresh(21); panel.shaos_length=(uint8_t)mode; panel.lfo_range=P23_LFO_MID; panel.lfo_freq=90; p23_set_panel(&panel);
        patch_pins(0,P23_PIN_LFO_SQR,P23_PIN_SH_CLK);
        for(k=0;n<1400 && k<400000;k++) {
            int32_t c; p23_process(out);
            c=p23_pin_value(P23_PIN_LFO_SQR);
            if(c>V(5) && !hi) {
                hi=1;
                seq[n++]=(uint8_t)((p23_pin_value(P23_PIN_SH_1DIR)>V(5))|((p23_pin_value(P23_PIN_SH_2DIR)*3+V(5))/V(10)<<1));
            } else if(c<V(5)) hi=0;
        }
        CHECK(n==1400 && period(seq,n)==want[mode],mode==0?"SHAOS 63":mode==1?"SHAOS 16 (twisted ring)":"SHAOS 217 = 7 x 31");
    }
}
static void test_patchbay(void)
{
    p23_patch p=module(P23_HHT,2); uint32_t k,gates=0,hi=0;
    fresh(31); solo(0); p.release=0; p23_set_patch(0,&p);
    /* divider 4 (8 ticks a cycle) into TRIG: 8 gates in 64 ticks */
    patch_pins(0,P23_PIN_DIV4,P23_PIN_T(0,P23_T_TRIG));
    for(k=0;k<64*2756/32;k++) { p23_process(out); if(p23_inspect(0).gate && !hi) { hi=1; gates++; } else if(!p23_inspect(0).gate) hi=0; }
    CHECK(gates>=7 && gates<=9,"a divider patched to TRIG plays the module");
    /* summed dividers: the envelope follows the strongest */
    patch_pins(1,P23_PIN_DIV2,P23_PIN_T(0,P23_T_TRIG)); blocks(10);
    CHECK(p23_inspect(0).gate==(p23_pin_value(P23_PIN_DIV4)>V(5) || p23_pin_value(P23_PIN_DIV2)>V(5)),"joined trigger sources: strongest wins");
    /* utilities */
    no_cables(); patch_pins(0,P23_PIN_P10V,P23_PIN_INV_IN); blocks(2);
    CHECK(p23_pin_value(P23_PIN_INV_OUT)<V(0.1),"inverter: 10 V -> 0 V (about +5 V)");
    patch_pins(1,P23_PIN_P10V,P23_PIN_VCA1_IN); patch_pins(2,P23_PIN_LFO_TRI,P23_PIN_VCA1_CV); blocks(2);
    CHECK(abs(p23_pin_value(P23_PIN_VCA1_OUT)-p23_pin_value(P23_PIN_LFO_TRI))<V(0.3),"VCA: gain = CV / 10 V");
    patch_pins(3,P23_PIN_P10V,P23_PIN_ATT1_IN); panel.atten[0]=64; p23_set_panel(&panel); blocks(2);
    CHECK(p23_pin_value(P23_PIN_ATT1_OUT)>V(1.5) && p23_pin_value(P23_PIN_ATT1_OUT)<V(4),"attenuator, log taper");
    patch_pins(4,P23_PIN_P10V,P23_PIN_IMP1_IN); blocks(1);
    k=(uint32_t)p23_pin_value(P23_PIN_IMP1_OUT); blocks(40);
    CHECK(k>(uint32_t)V(5) && p23_pin_value(P23_PIN_IMP1_OUT)<V(0.5),"pulse converter follows the edge, then decays");
    /* GND is stiff: shorting a divider to GND kills it */
    no_cables(); patch_pins(0,P23_PIN_DIV16,P23_PIN_GND); blocks(2);
    CHECK(p23_pin_value(P23_PIN_DIV16)<V(1.2),"GND overrides an output on its net");
    /* switch: closed above 5 V */
    no_cables(); patch_pins(0,P23_PIN_P10V,P23_PIN_SW1_IN); patch_pins(1,P23_PIN_SW1_OUT,P23_PIN_MIX_IN); blocks(3);
    CHECK(p23_pin_value(P23_PIN_MIX_IN)==0,"switch open");
    patch_pins(2,P23_PIN_P10V,P23_PIN_SW1_CV); blocks(3);
    CHECK(p23_pin_value(P23_PIN_MIX_IN)>V(9),"switch closed above +5 V");
    /* controlled inverter */
    no_cables(); patch_pins(0,P23_PIN_P10V,P23_PIN_CINV_IN); blocks(2);
    CHECK(p23_pin_value(P23_PIN_CINV_OUT)>V(9),"controlled inverter passes");
    patch_pins(1,P23_PIN_P10V,P23_PIN_CINV_CV); blocks(2);
    CHECK(p23_pin_value(P23_PIN_CINV_OUT)<V(1),"controlled inverter inverts above +5 V");
    /* the diode clamps the anode net to the cathode + 0.6 V */
    no_cables(); patch_pins(0,P23_PIN_P10V,P23_PIN_DIODE_A); patch_pins(1,P23_PIN_DIODE_A,P23_PIN_INV_IN);
    patch_pins(2,P23_PIN_DIODE_K,P23_PIN_ATT2_OUT); patch_pins(3,P23_PIN_P10V,P23_PIN_ATT2_IN); panel.atten[1]=90; p23_set_panel(&panel); blocks(4);
    CHECK(p23_pin_value(P23_PIN_INV_OUT)>V(10)-p23_pin_value(P23_PIN_ATT2_OUT)-V(1),"diode clamp");
    /* capacitor to GND: a low-pass on the net */
    no_cables(); patch_pins(0,P23_PIN_CAP2_A,P23_PIN_GND); patch_pins(1,P23_PIN_CAP2_B,P23_PIN_P10V); patch_pins(2,P23_PIN_CAP2_B,P23_PIN_INV_IN); blocks(1);
    CHECK(p23_pin_value(P23_PIN_INV_OUT)>V(9),"10 uF to GND: the net rises slowly");
    blocks(400);
    CHECK(p23_pin_value(P23_PIN_INV_OUT)<V(2),"... and settles");
}
static uint32_t first_echo(uint32_t mode,uint32_t clk)
{
    p23_patch p=module(P23_SD,0); p23_route r; uint32_t k,i,first=0;
    int32_t base=0;
    fresh(17); solo(0); p.release=0; p23_set_patch(0,&p);
    memset(&r,0,sizeof r); r.enabled=1; r.duck_source=255; r.fx_send=127; r.level=16384; p23_set_route(0,&r);   /* (the send follows VOL: the dry hit is over by block 24) */
    panel.fx_mode=(uint8_t)mode; panel.fx_time=60; panel.fx_feedback=0; panel.dly_out=127; panel.rev_out=0; panel.fx_clk_mod=127;
    p23_set_panel(&panel);
    if(clk) patch_pins(0,P23_PIN_P10V,P23_PIN_FX_CLK);
    p23_note(0,0,60,127,1); blocks(4); p23_note(0,0,60,0,0);
    for(k=4;k<4000 && !first;k++) {
        p23_process(out);
        for(i=0;i<P23_BLOCK*2;i+=2) { CHECK(out[i]>=-32767 && out[i]<=32767,"FX bounded"); if(!first && abs(out[i])>1000 && k>24) first=k*P23_BLOCK+i/2; }
    }
    (void)base;
    return first;
}
static void test_fx(void)
{
    uint32_t m;
    for(m=0;m<3;m++) {
        uint32_t a=first_echo(m,0),b=first_echo(m,1);
        CHECK(a>0,"delay repeats");
        CHECK(b>a*4,"a slower processor clock stretches the delay (up to 7x)");
    }
}
static uint32_t order_hash(uint32_t swapped)
{
    p23_patch a=module(P23_BASS,3),b=module(P23_BD,2); p23_route r; uint32_t i,k,h=0;
    fresh(9);
    for(i=0;i<P23_NODES;i++) { memset(&r,0,sizeof r); r.enabled=i<2; r.duck_source=255; r.track=(uint8_t)i; r.level=32768; p23_set_route(i,&r); }
    p23_set_patch(swapped,&a); p23_set_patch(swapped^1u,&b);
    patch_pins(0,P23_PIN_T(swapped^1u,P23_T_OUT),P23_PIN_T(swapped,P23_T_MOD4));
    patch_pins(1,P23_PIN_T(swapped,P23_T_ENV),P23_PIN_T(swapped^1u,P23_T_MOD1));
    p23_note(swapped,0,48,127,1); p23_note(swapped^1u,0,60,127,1);
    for(k=0;k<300;k++) { p23_process(out); for(i=0;i<P23_BLOCK*2;i++) h=h*33u+(uint32_t)out[i]; }
    return h;
}
/* zero crossings over n blocks: a pitch measure */
static uint32_t crossings(uint32_t n)
{
    uint32_t k,i,c=0; int32_t last=0;
    for(k=0;k<n;k++) { p23_process(out); for(i=0;i<P23_BLOCK*2;i+=2) { if((out[i]>0)!=(last>0)) c++; last=out[i]; } }
    return c;
}
static void test_routes_drive(void)
{
    p23_patch p=module(P23_BD,0); uint32_t plain,routed; uint64_t e0,e1; int32_t pk0,pk1;
    p.knob[P23_BD_PITCH]=0; p.knob[P23_BD_DRIVE]=0; p.release=127; p.knob[P23_BD_AMT]=127;
    fresh(41); solo(0); p23_set_patch(0,&p); p23_note(0,0,60,127,1); blocks(40);
    plain=crossings(200);
    p.mod[0].src=(uint8_t)P23_PIN_P10V; p.mod[0].dst=P23_RD_MOD1; p.mod[0].depth=100;
    fresh(41); solo(0); p23_set_patch(0,&p); p23_note(0,0,60,127,1); blocks(40);
    routed=crossings(200);
    CHECK(routed>plain*2,"a sound's route (+10 V into BD MOD) raises the pitch");
    CHECK(plain>0,"the BD follows the note");
    {   /* the note sets pitch: an octave up doubles it */
        uint32_t up; p.mod[0].depth=0;
        fresh(41); solo(0); p23_set_patch(0,&p); p23_note(0,0,72,127,1); blocks(40); up=crossings(200);
        CHECK(up>plain*17/10 && up<plain*23/10,"pitch from the note, an octave per 12");
    }
    /* drive: harmonics up, level kept within reason */
    p=module(P23_BASS,0); p.mode=P23_BASS_DCO; p.knob[P23_BASS_SHAPE]=0; p.knob[P23_BASS_LPF]=127; p.release=127;
    fresh(43); solo(0); p23_set_patch(0,&p); p23_note(0,0,48,127,1); blocks(40); e0=energy(100,&pk0);
    p.drive=110; fresh(43); solo(0); p23_set_patch(0,&p); p23_note(0,0,48,127,1); blocks(40); e1=energy(100,&pk1);
    CHECK(e1>e0/4 && e1<e0*4 && pk1<=32767,"overdrive keeps the level within 6 dB");
    p.drive=0; fresh(43); solo(0); p23_set_patch(0,&p); p23_note(0,0,48,127,1); blocks(40);
    plain=crossings(100);
    p.drive=127; fresh(43); solo(0); p23_set_patch(0,&p); p23_note(0,0,48,127,1); blocks(40);
    routed=crossings(100);
    CHECK(routed>=plain,"overdrive adds harmonics (a sine gains crossings or keeps them)");
}
/* two cores: the parts rendered in either order give the same output, routes across tracks included */
static uint64_t split_run(int split,uint32_t *both)
{
    uint32_t i,k,b; uint64_t h=1469598103934665603ull; p23_route r;
    fresh(77);
    for(k=0;k<P23_TRACKS;k++) {
        p23_patch p=module(k%4u,k*5u+3u);
        memset(&r,0,sizeof r); r.duck_source=255; r.track=(uint8_t)k; r.level=24000; r.enabled=1; r.fx_send=40;
        p23_set_route(k,&r);
        if(k==0) { p.mod[0].src=(uint8_t)P23_PIN_T(1,P23_T_ENV); p.mod[0].dst=P23_RD_MOD1; p.mod[0].depth=90; }
        if(k==2) { p.mod[0].src=(uint8_t)P23_PIN_LFO_TRI; p.mod[0].dst=P23_RD_DRIVE; p.mod[0].depth=60;
                   p.mod[1].src=(uint8_t)P23_PIN_T(2,P23_T_OUT); p.mod[1].dst=P23_RD_EXT; p.mod[1].depth=50; }
        if(k==3) { p.mod[0].src=(uint8_t)P23_PIN_T(0,P23_T_OUT); p.mod[0].dst=P23_RD_EXT; p.mod[0].depth=100; }
        p23_set_patch(k,&p);
    }
    *both=0;
    for(b=0;b<600;b++) {
        if(b%40==0) for(k=0;k<P23_TRACKS;k++) p23_note(k,1,48+k*3,127,1);
        if(b%40==12) for(k=0;k<P23_TRACKS;k++) p23_note(k,1,48+k*3,0,0);
        if(split) {
            uint32_t parts=p23_process_begin();
            if(parts==3u) (*both)++;
            p23_process_part(1); p23_process_part(0); p23_process_end(out);
        } else p23_process(out);
        for(i=0;i<P23_BLOCK*2;i++) h=(h^(uint32_t)out[i])*1099511628211ull;
    }
    return h;
}
static void test_split(void)
{
    uint32_t both0,both1; int32_t pk; uint64_t e_on,e_off;
    CHECK(split_run(0,&both0)==split_run(1,&both1),"the two parts in either order render as one core does");
    CHECK(both1>100,"sounding voices are shared between both parts");
    {   /* the FX rests after a long silence, and wakes with the next send */
        p23_patch p=module(P23_SD,4); p23_route r; uint32_t send;
        for(send=0;send<2;send++) {
            fresh(5); solo(0); panel.dly_out=panel.rev_out=110; panel.fx_feedback=90; p23_set_panel(&panel);
            memset(&r,0,sizeof r); r.duck_source=255; r.level=32768; r.enabled=1; r.fx_send=send?127:0; p23_set_route(0,&r);
            p.release=10; p23_set_patch(0,&p);
            p23_note(0,0,60,127,1); blocks(4); p23_note(0,0,60,0,0); blocks(7000);
            p23_note(0,0,60,127,1); blocks(4); p23_note(0,0,60,0,0); blocks(200);
            if(send) e_on=energy(300,&pk); else e_off=energy(300,&pk);
        }
        CHECK(e_on>e_off*4+1000,"the FX wakes after resting: repeats follow a send");
    }
}
/* every mode of every circuit: it speaks, stays bounded and falls silent after its release */
static void test_modes(void)
{
    static const uint8_t MODES[4]={5,3,4,4};
    uint32_t m,md,v;
    for(m=0;m<4;m++) for(md=0;md<MODES[m];md++) for(v=0;v<4;v++) {
        p23_patch p=module(m,v*31u+7u); int32_t pk,pk2; char msg[96];
        p.mode=(uint8_t)md; p.release=20; p.drive=0;
        fresh(90+v); solo(0); p23_set_patch(0,&p);
        p23_note(0,0,36+v*12,127,1); blocks(3);
        energy(40,&pk);
        p23_note(0,0,36+v*12,0,0); blocks(1500); energy(20,&pk2);
        snprintf(msg,sizeof msg,"circuit %u mode %u (%u): speaks, bounded, releases (peak %d, after %d)",m,md,v,pk,pk2);
        CHECK(pk>300 && pk<=32767 && pk2<40,msg);
    }
}
/* WRECK changes the voice and keeps it bounded; the stutter repeats the mix while held, and only then */
static void test_wreck_stut(void)
{
    p23_patch p=module(P23_BD,0); uint32_t plain,folded,k; int32_t pk; uint64_t e_stut,e_off;
    p.knob[P23_BD_PITCH]=0; p.release=127;
    fresh(51); solo(0); p23_set_patch(0,&p); p23_note(0,0,48,127,1); blocks(40); plain=crossings(100);
    p.fold=110; fresh(51); solo(0); p23_set_patch(0,&p); p23_note(0,0,48,127,1); blocks(40); folded=crossings(100);
    CHECK(folded>plain,"WRECK FOLD adds partials");
    p.fold=127; p.ring=127; p.ring_freq=90; p.crush=127; p.bits=127;
    fresh(51); solo(0); p23_set_patch(0,&p); p23_note(0,0,48,127,1); blocks(40); energy(100,&pk);
    CHECK(pk>500 && pk<=32767,"WRECK at its extremes: heard, bounded");
    {   /* a pattern of hits, with the stutter held from bar 2 and without: the outputs part ways while it is held,
         * and meet again once it is let go and its fade is over */
        static int32_t a[1600*P23_BLOCK*2]; uint64_t diff=0,base=0,after=0; uint32_t b,i;
        p23_stutter st={1,64,P23_ST_FWD,0,64,0,127};
        for(k=0;k<2;k++) {
            p23_patch q=module(P23_SD,0); q.release=10;
            fresh(52); solo(0); p23_set_patch(0,&q); p23_set_stutter(&st); p23_clock_reset(); p23_stutter_hold(0);
            for(b=0;b<1600;b++) {
                if(b%45==0) p23_note(0,0,60,127,1);
                if(b%45==3) p23_note(0,0,60,0,0);
                if(k) p23_stutter_hold(b>=300 && b<1200);
                p23_process(out);
                for(i=0;i<P23_BLOCK*2;i++) {
                    if(!k) { a[b*P23_BLOCK*2+i]=out[i]; if(b>=300 && b<1200) base+=(uint64_t)((int64_t)out[i]*out[i]); }
                    else { int64_t d=(int64_t)out[i]-a[b*P23_BLOCK*2+i]; if(b>=300 && b<1200) diff+=(uint64_t)(d*d); if(b>=1300) after+=(uint64_t)(d*d); }
                }
            }
        }
        e_stut=diff; e_off=base;
        CHECK(after*1000<base,"the stutter let go: the mix as without it (its DC blocking aside)");
    }
    CHECK(e_stut>e_off/4,"the stutter held replaces the mix with its repeats");


}
int main(void)
{
    CHECK(p23_state_bytes()<270000,"fixed memory budget");
    CHECK(P23_PINS<255,"pins fit a byte");
    test_circuits();
    test_envelope();
    test_bd_phase();
    test_clap();
    test_clock();
    test_looper();
    test_shaos();
    test_patchbay();
    test_fx();
    test_routes_drive();
    test_split();
    test_modes();
    test_wreck_stut();
    CHECK(order_hash(0)==order_hash(1),"patch feedback independent of node order");
    {
        int32_t usb[P23_BLOCK*2]; p23_patch p=module(P23_BD,1);
        fresh(1); solo(0); p23_set_patch(0,&p); p23_set_master(0); p23_note(0,0,60,127,1); blocks(40);
        p23_copy_usb(usb); CHECK(out[0]==0 && usb[0]!=0,"USB tap before the master volume");
    }
    printf("standalone: circuits, AR / gate merging, BD phase, clap, clock, looper, SHAOS, patchbay, FX: %d failures; state %u bytes\n",
           failures,p23_state_bytes());
    return failures!=0;
}
