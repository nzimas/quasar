/* SPDX-License-Identifier: GPL-3.0-only */
#define BCHECK(x,msg) do { if(!(x)){printf("FAIL bridge: %s\n",msg);failures++;} } while(0)
static int cmd_bridge(void)
{
    uint32_t i,lane; int failures=0; int32_t out[CTL*2]; sound_t before; p23_status st;
    memset(host_flash,255,sizeof host_flash);prj_scan(1);tracks_init();ui_init();
    BCHECK(NLANE==P23_NODES && NTRACK==P23_TRACKS,"frontend ports match independent API");
    ui.sel=0;sel_sync();before=*cur();
    ev_post(EV_ON,0,60,127);for(i=0;i<100;i++)mix_block(out);
    BCHECK(p23_inspect(0).gate && p23_inspect(0).envelope>0,"keyboard event crosses API");
    ev_post(EV_OFF,0,60,0);for(i=0;i<16000;i++)mix_block(out);
    BCHECK(!p23_inspect(0).gate && p23_inspect(0).envelope<P23_VOLT/10,"note release crosses API");
    lane=lane_take(0,1);BCHECK(lane<NLANE,"subtrack allocation");
    gen_sound(&ui_lsnd[lane-NTRACK],ENG_SD,5,-1,333);mark(lane);
    /* the panel and the patchbay are the session's */
    pnl[PN_FX_MODE]=2;pnl[PN_SH_LEN]=2;pnl[PN_LR_MODE1+3]=P23_LR_REC;
    pcab[0].a=P23_PIN_DIV4;pcab[0].b=(uint8_t)P23_PIN_T(0,P23_T_TRIG);
    BCHECK(!prj_save(2),"save the panel and patchbay through retained storage");
    memset(&ui_lsnd[lane-NTRACK],0,sizeof(sound_t));pnl[PN_FX_MODE]=0;pnl[PN_SH_LEN]=0;pcab[0].a=pcab[0].b=0;
    BCHECK(!prj_load(2),"load native session");
    BCHECK(ui_lsnd[lane-NTRACK].mach==ENG_SD && pnl[PN_FX_MODE]==2 && pnl[PN_SH_LEN]==2 && pnl[PN_LR_MODE1+3]==P23_LR_REC
           && pcab[0].a==P23_PIN_DIV4 && pcab[0].b==P23_PIN_T(0,P23_T_TRIG),"module, panel, looper modes and cables round trip");
    BCHECK(PRJ_MAGIC!=0x314e3350u,"build 1 audio semantics cannot be loaded silently");
    ui.sel=0;ui.sub[0]=0;sel_sync();
    {   /* the four macros and their second layer: KNOB 1 .. 4, SHIFT + KNOB 1 .. 4 */
        sound_t t=*cur(); p23_patch a,b; uint32_t i2,distinct=0; sound_t v[10];
        t.mach=ENG_BD; t.syn[2]=40;
        p23_from_frontend(&t,&a,0);
        par_put(&t,PG_MACRO,0,127); p23_from_frontend(&t,&b,0);
        BCHECK(b.knob[P23_BD_PITCH]>a.knob[P23_BD_PITCH]+40,"M1 PUNCH moves BD PITCH");
        par_put(&t,PG_MACRO,4,90); par_put(&t,PG_MACRO,5,77); par_put(&t,PG_MACRO,6,12);
        BCHECK(t.syn[P_DRIVE]==90 && t.fx[X_DEL]==77 && t.syn[P_ATT]==12,"SHIFT layer: DRIVE, FX, ATTACK");
        p23_from_frontend(&t,&b,0);
        BCHECK(b.drive==90,"the overdrive reaches the engine");
        BCHECK(pages_n(cur())==6,"circuit, ENV, WRECK, MACRO and two ROUTE pages");
        /* a family drawn again comes out different, and its routes are patched */
        for(i2=0;i2<10;i2++) gen_sound(&v[i2],ENG_BD,fam_find("SYN TOM BURN"),-1,1000u+i2*7919u);
        for(i2=1;i2<10;i2++) if(memcmp(v[i2].syn,v[0].syn,sizeof v[0].syn)) distinct++;
        BCHECK(distinct==9,"variations of a family differ");
        BCHECK(!strcmp(family_name(&v[0]),"SYN TOM BURN") && rt_src(&v[0],0)==P23_PINS+1u && rt_dst(&v[0],0)==P23_RD_MOD1,"SYN TOM BURN: own ENV into BD MOD");
        p23_from_frontend(&v[0],&a,2);
        BCHECK(a.mod[0].src==P23_PIN_T(2,P23_T_ENV) && a.mod[0].depth>0,"a route's own pins resolve to the sound's track");
        BCHECK(NRECIPE>=80,"a broad library of families");
        {   /* every drawing: no SHAOS or divider on a pitch, a percussive release, a mode the circuit has */
            uint32_t f,k,j,bad=0,long_rel=0,bad_mode=0; sound_t d; p23_patch q;
            static const uint8_t MODES[4]={5,3,4,4};
            for(f=0;f<NRECIPE;f++) for(k=0;k<12u;k++) {
                gen_sound(&d,RECIPES[f].mach,(int32_t)f,-1,5u+k*7919u+f*31u);
                for(j=0;j<4u;j++) if(rt_depth(&d,j) && stepped_src(rt_src(&d,j)) && pitch_dst(d.mach,rt_dst(&d,j))) bad++;
                p23_from_frontend(&d,&q,0);
                if(q.release>REL_MAX) long_rel++;
                if(q.mode>=MODES[q.module%4u]) bad_mode++;
            }
            BCHECK(!bad,"no SHAOS or clock divider moves a pitch");
            BCHECK(!long_rel && !bad_mode,"releases stay percussive; modes are the circuit's");
        }
    }
    {   /* a CC reaches every page a Quasar sound has: ENV, MACRO and ROUTE 1 too */
        sound_t keep=ui.snd[0][ui.eng[0]];
        BCHECK(snd_cc_set(0,1,0,127) && snd_cc_set(0,PG_MACRO,0,0) && snd_cc_set(0,PG_ROUTE,2,127),"CCs on ENV, MACRO, ROUTE 1");
        BCHECK(ui.snd[0][ui.eng[0]].syn[P_ATT]==127 && ui.snd[0][ui.eng[0]].syn[P_MACRO]==0,"those CCs set the values");
        ui.snd[0][ui.eng[0]]=keep;
    }
    *cur()=before;mark(0);
    {   /* the phrase generator: polymeters and its generative layer -- forty phrases on a hat track come out at
         * several lengths, with ratchets and steps off the grid */
        uint32_t lens=0,ratch=0,off=0,n,q;
        for(n=0;n<40u;n++) {
            phrase_new(3);
            lens|=1u<<(seq_len[3]&31u);
            for(q=0;q<NSTEP;q++) { ratch+=seq_rtg[3][q]!=0; off+=seq_mfx[3][q]==MFX_LATE || seq_mfx[3][q]==MFX_GRACE; }
        }
        BCHECK(__builtin_popcount(lens)>=4 && ratch>20 && off>5,"phrases: polymeters, ratchets, hits off the grid");
    }
    /* the STUTTER's key (the 6th black key): SHIFT and it held, the stutter plays; let go, it stops; held alone 0.8 s,
     * its page */
    ui.page=PG_MAIN;ui.sv=0;
    fm1_in.notes|=1u<<KEY_SHIFT; key(KEY_STUT,1); key(KEY_STUT,1); for(i=0;i<20;i++) mix_block(out);
    BCHECK(stut_ui==1,"SHIFT + the STUTTER key held: it plays");
    key(KEY_STUT,0); fm1_in.notes&=~(1u<<KEY_SHIFT); key(KEY_STUT,0);
    BCHECK(stut_ui==0,"let go: it stops");
    key(KEY_STUT,1); fm1_ms+=900; key(KEY_STUT,1); key(KEY_STUT,0);
    BCHECK(ui.page==PG_FXP && ui.fx_pg==FXP_STUT && stut_ui==0,"the STUTTER key held alone: its page, and no stutter");
    {   /* RATE past 100: RND, and back; read as RND */
        char tb[24];
        pnl[PN_ST_RATE]=127; fx_turn(1,1);
        BCHECK(pnl[PN_ST_RATE]==ST_VAL_RND,"RATE turned past 100: RND");
        fx_text(1,pnl[PN_ST_RATE],tb); BCHECK(!strcmp(tb,"RND"),"RND reads RND");
        fx_turn(1,-1); BCHECK(pnl[PN_ST_RATE]==127,"turned back from RND: 100");
        BCHECK(fx_kind(0)==3u && fx_list_n(0)==7u && fx_list_n(2)==5u && fx_kind(3)==9u,"SLICE and MODE end in RND, DECAY too");
    }
    ui.page=PG_MAIN;
    {   /* the STUTTER's RND rows: each press draws its own SLICE, RATE, MODE and DECAY, in their ranges */
        uint32_t n,slices=0,rates=0,bad=0;
        pnl[PN_ST_SLICE]=ST_SLC_RND; pnl[PN_ST_RATE]=ST_VAL_RND; pnl[PN_ST_MODE]=ST_MODE_RND; pnl[PN_ST_DECAY]=ST_VAL_RND;
        for(n=0;n<16u;n++) {
            fm1_ms+=37; fm1_in.notes|=1u<<KEY_SHIFT; key(KEY_STUT,1); key(KEY_STUT,1);
            if(stut_pick[0]>=6u || stut_pick[1]>=128u || stut_pick[2]>=4u || stut_pick[3]>=128u || !stut_ui) bad++;
            slices|=1u<<stut_pick[0]; rates|=1u<<(stut_pick[1]>>4);
            key(KEY_STUT,0); fm1_in.notes&=~(1u<<KEY_SHIFT); key(KEY_STUT,0);
        }
        BCHECK(!bad,"RND: every press plays, its values in range");
        BCHECK(__builtin_popcount(slices)>=3 && __builtin_popcount(rates)>=4,"RND: the presses differ");
        memcpy(pnl+PN_ST_SLICE,PNL0+PN_ST_SLICE,NPN-PN_ST_SLICE);
    }
    /* the STUTTER and PERFORMANCE keys lit dimly in the track view */
    ui.page=PG_MAIN; ui.sv=0; ui_leds();
    BCHECK(host_dim_on(14u+KEY_STUT) && host_dim_on(14u+KEY_PERF),"the STUTTER and PERFORMANCE keys dim in the track view");
    {   /* PERFORMANCE (the 7th black key): the macros of every track with notes move together; off, everything as
         * it was -- sounds, steps, level, panel, mutes, and nothing marked changed */
        int16_t m0,m2,m4; int8_t lv=ui.level[0]; uint32_t q; uint8_t fxm=pnl[PN_FX_MODE],pc=pat_cur;
        for(q=0;q<NTRACK;q++) memset((void *)seq_note[q],0,NSTEP);
        seq_note[0][0]=60; seq_note[2][3]=50; trk_mute[2]=0;
        ui.sel=0; sel_sync(); ui_publish();
        m0=ui.snd[0][ui.eng[0]].syn[P_MACRO]; m2=ui.snd[2][ui.eng[2]].syn[P_MACRO]; m4=ui.snd[4][ui.eng[4]].syn[P_MACRO];
        prj_dirty=0; pat_dirty=0;
        key(KEY_PERF,1); key(KEY_PERF,0);
        BCHECK(perf_on,"the 7th black key: PERFORMANCE on");
        ui_leds(); BCHECK(host_led[14u+KEY_PERF],"PERFORMANCE lit while on");
        perf_macro(0,-20);
        BCHECK(ui.snd[0][ui.eng[0]].syn[P_MACRO]!=m0 && ui.snd[2][ui.eng[2]].syn[P_MACRO]!=m2 &&
               ui.snd[4][ui.eng[4]].syn[P_MACRO]==m4,"PERFORMANCE: M1 moves every track with notes, not the empty ones");
        seq_note[0][5]=72; ui.level[0]=3; level_pan_apply(0); pnl[PN_FX_MODE]=(uint8_t)(fxm^1u); trk_mute[2]=1;
        phrase_new(1); pat_switch((pc+1u)%PAT_SLOTS);
        BCHECK(pat_cur==pc && !seq_pend,"PERFORMANCE: the pattern stays");
        key(KEY_PERF,1); key(KEY_PERF,0);
        BCHECK(!perf_on,"pressed again: off");
        BCHECK(ui.snd[0][ui.eng[0]].syn[P_MACRO]==m0 && ui.snd[2][ui.eng[2]].syn[P_MACRO]==m2,"off: the macros back");
        BCHECK(seq_note[0][5]==0 && seq_note[0][0]==60 && seq_note[2][3]==50,"off: the steps back");
        for(q=0;q<NSTEP;q++) if(seq_note[1][q]) break;
        BCHECK(q==NSTEP,"off: a phrase drawn meanwhile gone");
        BCHECK(ui.level[0]==lv && pnl[PN_FX_MODE]==fxm && !trk_mute[2],"off: level, panel and mutes back");
        BCHECK(!prj_dirty && !pat_dirty,"off: nothing marked changed");
        for(q=0;q<NTRACK;q++) memset((void *)seq_note[q],0,NSTEP);
    }
    ui.page=PG_MAIN;
    /* Quasar shows no patchbay and no looper: a cable left in a session, or a channel left in REC, does nothing */
    memset(pcab,0,sizeof pcab);pcab[0].a=P23_PIN_P10V;pcab[0].b=(uint8_t)P23_PIN_T(0,P23_T_TRIG);
    for(i=0;i<200;i++) mix_block(out);
    BCHECK(!p23_inspect(0).gate && p23_inspect(0).envelope==0,"a hidden cable (+10 V to TRIG) strikes nothing");
    memset(pcab,0,sizeof pcab);for(i=0;i<16000;i++)mix_block(out);
    pnl[PN_LR_MODE1]=P23_LR_REC;seq_run=1;for(i=0;i<20;i++)mix_block(out);
    ev_post(EV_LR,0,P23_LR_ADD,1);for(i=0;i<400;i++)mix_block(out);ev_post(EV_LR,0,P23_LR_ADD,0);for(i=0;i<20;i++)mix_block(out);
    p23_status_get(&st);
    BCHECK(!st.events[0] && st.mode[0]==P23_LR_PLAY,"a looper channel left in REC records nothing");
    ev_post(EV_LR_CLEAR,0,0,0);for(i=0;i<2;i++)mix_block(out);p23_status_get(&st);
    BCHECK(!st.events[0],"looper channel cleared");
    pnl[PN_LR_MODE1]=P23_LR_PLAY;seq_run=0;
    /* Native knob locks retain the sequencer's ID and storage machinery. */
    seq_note[0][0]=60;seq_vel[0][0]=127;seq_glen[0][0]=3;
    seq_lid[0][0][0]=0;seq_lv[0][0][0]=25;seq_len[0]=1;seq_run=1;
    for(i=0;i<100;i++)mix_block(out);
    BCHECK(p23_inspect(0).note==60 && active_lv[0][0]==25,"step note and parameter lock reach native module");
    seq_run=0;
    printf("bridge: %d failures\n",failures);return failures!=0;
}
/* every profile answers the track's level and the sound's VOL, through the frontend: off is silent */
static uint64_t lvl_energy(uint32_t blocks)
{
    int32_t out[P23_BLOCK*2]; uint64_t e=0; uint32_t b,i;
    for(b=0;b<blocks;b++) {
        if(b%40==0) ev_post(EV_ON,0,48,127);
        if(b%40==6) ev_post(EV_OFF,0,48,0);
        mix_block(out); for(i=0;i<P23_BLOCK*2;i++) e+=(uint64_t)((int64_t)out[i]*out[i]);
    }
    return e;
}
static int cmd_levels(void)
{
    uint32_t p,bad=0;
    for(p=0;p<NPROF;p++) {
        uint64_t full,off,voff;
        tracks_init(); ui_init(); master_q12=4096;
        ui.eng[0]=PROFILES[p].mach; gen_profile(&ui.snd[0][PROFILES[p].mach],p,-1,11u+p); sel_sync(); mark(0);
        ui.level[0]=LEVEL_UNITY; level_pan_apply(0); ui_publish(); lvl_energy(80); full=lvl_energy(400);
        ui.level[0]=0; level_pan_apply(0); lvl_energy(6000); off=lvl_energy(400);   /* (the FX's repeats from before rung out) */
        ui.level[0]=LEVEL_UNITY; level_pan_apply(0); cur()->amp[A_VOL]=0; mark(0); ui_publish(); lvl_energy(6000); voff=lvl_energy(400);
        printf("%-7s %-13s full %.3g  level off %.3g  VOL 0 %.3g\n",PROFILES[p].name,family_name(&ui.snd[0][PROFILES[p].mach]),(double)full,(double)off,(double)voff);
        if(!full || off*1000>full || voff*1000>full) bad++;
    }
    printf("levels: %u profiles not following\n",bad);
    return bad!=0;
}
static int cmd_native_screens(const char *dir)
{
    uint32_t m;char name[32],path[256];
    static const char *const PG[FXP_PATCH+1]={"clock","looper","shaos","lfo","fx","mix","stutter_slot","cv","patch"};
    tracks_init();ui_init();fm1_ms=5000;
    splash_draw("BUILD 3");snprintf(path,sizeof path,"%s/native_splash.ppm",dir);ppm(path);
    for(m=0;m<NTRACK;m++) {   /* the track view, each track of the start's kit */
        ui.sel=(uint8_t)m;sel_sync();ui.page=PG_MAIN;ui.force=1;
        trk[m].meter=6000+(int32_t)m*3000;
        snprintf(name,sizeof name,"native_track_view%u",m+1);shot(dir,name);
    }
    ui.sel=1;sel_sync();trk_mute[1]=1;ui.oct[1]=-1;arp_on[1]=1;ui.rec=REC_ON;ui.level[1]=0;ui.force=1;shot(dir,"native_track_view_muted");
    trk_mute[1]=0;ui.oct[1]=0;arp_on[1]=0;ui.rec=0;ui.level[1]=LEVEL_UNITY;ui.sel=0;sel_sync();
    for(m=0;m<4;m++) {
        ui.sel=0;ui.eng[0]=(uint8_t)m;sel_sync();ui.page=PG_EDIT;ui.edit_pg=0;ui.half=0;
        snprintf(name,sizeof name,"native_%s",ENG_NAME[m]);shot(dir,name);
    }
    ui.edit_pg=1;shot(dir,"native_env");
    ui.page=PG_MAIN;ui.force=1;shot(dir,"native_main");
    ui.page=PG_GEN;ui.force=1;shot(dir,"native_phrase");
    ui.page=PG_TRK;ui.force=1;shot(dir,"native_track");
    ui.page=PG_EDIT;
    ui.eng[0]=ENG_BD;gen_sound(&ui.snd[0][ENG_BD],ENG_BD,3,-1,5);sel_sync();
    ui.edit_pg=2;ui.force=1;shot(dir,"native_wreck");ui.edit_pg=3;ui.force=1;shot(dir,"native_macro");ui.edit_pg=4;ui.force=1;shot(dir,"native_route");
    pcab[0].a=P23_PIN_DIV4;pcab[0].b=(uint8_t)P23_PIN_T(0,P23_T_TRIG);
    for(m=FXP_LR;m<=FXP_PATCH;m++) {
        ui.page=PG_FXP;ui.fx_pg=(uint8_t)m;ui.half=0;ui.force=1;
        snprintf(name,sizeof name,"panel_%s",PG[m]);shot(dir,name);
    }
    ui.fx_pg=FXP_FX;ui.half=1;ui.force=1;shot(dir,"panel_fx_eh");
    ui.fx_pg=FXP_STUT;ui.half=0;ui.force=1;shot(dir,"panel_stutter");ui.half=1;ui.force=1;shot(dir,"panel_stutter_eh");ui.half=0;
    pnl[PN_ST_SLICE]=ST_SLC_RND;pnl[PN_ST_RATE]=ST_VAL_RND;pnl[PN_ST_MODE]=ST_MODE_RND;pnl[PN_ST_DECAY]=ST_VAL_RND;
    ui.force=1;shot(dir,"panel_stutter_rnd");memcpy(pnl+PN_ST_SLICE,PNL0+PN_ST_SLICE,NPN-PN_ST_SLICE);
    ui.page=PG_MAIN;perf_on=1;ui.force=1;shot(dir,"native_track_view_perf");perf_on=0;ui.page=PG_FXP;
    ui.fx_pg=FXP_CMP;ui.half=0;shot(dir,"native_duck");
    return 0;
}
/* every family, SEEDS drawings each, as WAV: a phrase in C minor (a drone: one note held), the sound on track 3 */
static void wav_write(const char *path,const int16_t *pcm,uint32_t frames)
{
    FILE *f=fopen(path,"wb"); uint32_t bytes=frames*4u,v;
    if(!f) return;
    fwrite("RIFF",1,4,f); v=36u+bytes; fwrite(&v,4,1,f); fwrite("WAVEfmt ",1,8,f);
    v=16; fwrite(&v,4,1,f); v=1u|2u<<16; fwrite(&v,4,1,f); v=44100; fwrite(&v,4,1,f); v=44100u*4u; fwrite(&v,4,1,f);
    v=4u|16u<<16; fwrite(&v,4,1,f); fwrite("data",1,4,f); fwrite(&bytes,4,1,f); fwrite(pcm,2,frames*2u,f); fclose(f);
}
/* the website's demos: a kit of six profiles, a phrase drawn on each track, eight bars through the whole frontend and
 * engine (the firmware's own code, on the computer), the STUTTER thrown in on the last beat of bars 5 and 9; ten bars,
 * so that tools/make_loop.py takes eight from the second on (the effects' tails in, and a bar after to fade into) */
static int cmd_demo(const char *dir)
{
    static const struct { const char *name; uint32_t bpm, seed, cx, den; uint8_t prof[6]; } D[3]={
        {"fracture",143,0x2301u,80,60,{0,4,8,11,13,9}},
        {"polymeter",127,0x2302u,70,55,{1,6,10,12,15,7}},
        {"overload",165,0x2303u,95,70,{2,3,9,14,11,5}},
    };
    static int16_t pcm[44100*40*2];
    uint32_t d,t,frames,b,i,nb; char path[512];
    for(d=0;d<3u;d++) {
        int32_t out[CTL*2];
        tracks_init(); ui_init(); ui.seed=D[d].seed; master_q12=4096;
        ui.tempo=(int16_t)D[d].bpm; tempo_apply();
        for(t=0;t<NTRACK;t++) {
            uint32_t m=PROFILES[D[d].prof[t]].mach;
            ui.eng[t]=(uint8_t)m; gen_profile(&ui.snd[t][m],D[d].prof[t],-1,next_seed()); mark(t);
            ui.gen_cx[t]=(uint8_t)D[d].cx; ui.gen_den[t]=(uint8_t)D[d].den; phrase_new(t);
        }
        sel_sync(); ui_publish(); seq_run=1;
        nb=(uint32_t)(44100u*60u*4u*10u/D[d].bpm)/CTL;
        for(b=0,frames=0;b<nb;b++) {
            uint32_t bar=b*CTL/(44100u*60u*4u/D[d].bpm), beat=b*CTL/(44100u*60u/D[d].bpm)%4u;
            stut_ui=(uint8_t)((bar==4u || bar==8u) && beat==3u);
            if(!(b%32u)) ui_publish();
            mix_block(out);
            for(i=0;i<CTL*2u;i++) pcm[frames*2u+i]=(int16_t)clampi(out[i],-32767,32767);
            frames+=CTL;
        }
        stut_ui=0; seq_run=0;
        snprintf(path,sizeof path,"%s/%s.wav",dir,D[d].name);
        wav_write(path,pcm,frames);
        printf("demo: %s %u BPM, %u frames (%.3f s)\n",path,D[d].bpm,frames,frames/44100.0);
    }
    return 0;
}
static int cmd_render(const char *dir,uint32_t seeds)
{
    static int16_t pcm[44100*5*2];
    static const uint8_t NOTE[8]={48,51,55,58,48,53,55,60};
    uint32_t f,k,b,i; char path[512],name[32];
    for(f=0;f<NRECIPE;f++) for(k=0;k<seeds;k++) {
        sound_t s; p23_patch p; p23_route r; uint32_t frames=0,node=2,uses_t1=0,j;
        int drone=RECIPES[f].role==RO_D;
        gen_sound(&s,RECIPES[f].mach,(int32_t)f,-1,77u+k*104729u);
        tracks_init(); p23_reset(5); fx_init(); pnl_run=1; p23_global_from_frontend();
        for(i=0;i<P23_NODES;i++) { memset(&r,0,sizeof r); r.duck_source=255; r.track=(uint8_t)(i%P23_TRACKS); r.level=26000;
            r.enabled=i==node || i==0; r.fx_send=i==node?s.fx[X_DEL]:0;
            if(i==node) r.level=(uint32_t)(26000*s.amp[A_VOL]*s.amp[A_VOL]/(108*108));   /* (the sound's VOL, as mix_block applies it) */
            p23_set_route(i,&r); }
        for(j=0;j<4u;j++) if(rt_src(&s,j)==S_T1E || rt_src(&s,j)==S_T1O) uses_t1=1;
        { sound_t kick; gen_sound(&kick,ENG_BD,0,-1,9); p23_from_frontend(&kick,&p,0); p23_set_patch(0,&p); }
        p23_from_frontend(&s,&p,node); p23_set_patch(node,&p); p23_clock_reset();
        for(b=0;b<44100u*4u/P23_BLOCK;b++) {
            int32_t out[P23_BLOCK*2]; uint32_t t=b*P23_BLOCK,step=t/22050u,in=t%22050u;
            if(drone) { if(t==0) p23_note(node,0,48,127,1); if(t==44100u*3u) p23_note(node,0,48,0,0); }
            else if(step<8u) { if(in<P23_BLOCK) p23_note(node,0,NOTE[step],127,1); if(in>=11025u && in<11025u+P23_BLOCK) p23_note(node,0,NOTE[step],0,0); }
            if(uses_t1 && !(t%11025u)) p23_note(0,0,48,127,1);
            if(uses_t1 && t%11025u>=2000u && t%11025u<2000u+P23_BLOCK) p23_note(0,0,48,0,0);
            p23_process(out);
            for(i=0;i<P23_BLOCK*2u;i++) pcm[frames*2u+i]=(int16_t)clampi(out[i],-32767,32767);
            frames+=P23_BLOCK;
        }
        str_cpy(name,RECIPES[f].name,24);
        for(i=0;name[i];i++) if(name[i]==' ') name[i]='_';
        snprintf(path,sizeof path,"%s/%02u_%s_%u.wav",dir,f,name,k+1u);
        wav_write(path,pcm,frames);
    }
    printf("render: %u families x %u in %s\n",(unsigned)NRECIPE,seeds,dir);
    return 0;
}
