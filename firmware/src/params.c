/* SPDX-License-Identifier: GPL-3.0-only
 * The parameter pages (EDIT): each page eight parameters, as the Digitone II's (KNOB 1 .. 4: A .. D; with SHIFT
 * held: E .. H): SYN 1 .. 4 of the track's machine, FLTR 1 and 2, AMP, FX. A parameter: its name, its range, its
 * centre (a bipolar one shows the distance from it), how its value reads. */

enum { PK_NUM, PK_BIP, PK_ALGO, PK_RATC, PK_RATA, PK_RATB, PK_ONOFF, PK_LIST, PK_HOLD, PK_PHC, PK_NONE, PK_PIN };
static void pin_name(uint32_t pin, char *b);   /* (ui.c: a patch pin, as the PATCH page names it) */
typedef struct {
    const char *name;
    int16_t lo, hi, mid;
    uint8_t kind;
    const char *const *list;
} par_t;

#define P_(n, lo, hi, mid, k) {n, lo, hi, mid, k, 0}
#define PL(n, hi, l) {n, 0, hi, 0, PK_LIST, l}
#define PN(n) {n, 0, 127, 0, PK_NUM, 0}
#define PB(n) {n, 0, 127, 64, PK_BIP, 0}
#define PO(n) {n, 0, 1, 0, PK_ONOFF, 0}
#define PX {"", 0, 0, 0, PK_NONE, 0}

static const char *prof_name(const sound_t *s);   /* (gen.c: the profile's name) */
static const char *const P_BMODES[] = {"DCO", "PRC", "PLUCK", "FM", "GRIND"};
static const char *const P_DMODES[] = {"TRI", "SKIN", "HAMMER"};
static const char *const P_SMODES[] = {"NOISE", "TONE", "WIRE", "WALL"};
static const char *const P_HMODES[] = {"NOISE", "METAL", "SHAKE", "STATIC"};
static const char *const P_RMAC[] = {"-", "M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8"};
/* where each route may go, by circuit (P23_RD_*: EXT, MOD1 .. MOD5, DRIVE, VCA) */
static const char *const P_RDST[4][8] = {
    {"EXT", "CV IN", "SHAPE", "WARP", "PM", "LPF", "DRIVE", "VCA"},
    {"EXT", "MOD", "WTF?", "OMG!", "-", "-", "DRIVE", "VCA"},
    {"EXT", "TUNE", "BPF", "-", "-", "-", "DRIVE", "VCA"},
    {"EXT", "TUNE", "HPF", "-", "-", "-", "DRIVE", "VCA"},
};
enum { PG_ROUTE = 3, PG_FLT1 = 4, PG_FLT2, PG_AMP, PG_FX, PG_MNG };
#define PG_MACRO 2u
#define PG_ROUTE2 PG_FLT2
#define PG_WRECK PG_FLT1                /* (the voice's WRECK stage: syn[11 .. 15]) */
static const par_t PAR_WRECK[5] = {PN("FOLD"), PN("RING"), PN("RING FRQ"), PN("CRUSH"), PN("BITS")};
static const uint8_t SYN_PAGES[NENG] = {3,3,3,3};
/* the circuits' own knobs (no TUNE: the note sets pitch), as syn[] indices in the engine's order */
static const par_t PAR_P[4][8] = {
    {PN("SHAPE"), PN("WARP"), PN("AMT"), PN("LPF FR"), PN("LPF Q"), PN("GLIDE"), PL("MODE",4,P_BMODES), PX},
    {PN("AMT"), PN("PITCH"), PN("SHAPE"), PO("STAB"), PL("MODE",2,P_DMODES), PX, PX, PX},
    {PN("AMT"), PN("CLAP"), PN("MIX"), PN("BPF FR"), PN("BPF Q"), PL("MODE",3,P_SMODES), PX, PX},
    {PN("WARP"), PN("HPF FR"), PN("HPF Q"), PL("MODE",3,P_HMODES), PX, PX, PX, PX},
};
static const uint8_t SYN_IX[4][8] = {{1,2,3,4,5,6,7,255},{1,2,3,4,7,255,255,255},{1,2,3,4,5,7,255,255},{1,2,3,7,255,255,255,255}};
/* the envelope, the channel strip and the voice's overdrive */
static const par_t PAR_P_ENV[8] = {PN("ATT"),PN("REL"),PN("VOL"),PN("FX"),PN("DRIVE"),PX,PX,PX};
/* the macros: KNOB 1 .. 4 on the track's view, and with SHIFT the second layer (M5 the overdrive) */
static const char *const MAC_NAME[4][8] = {
    {"CUTOFF", "TIMBRE", "MOTION", "DECAY", "DRIVE", "FX", "ATTACK", "RESO"},
    {"PUNCH", "SHAPE", "CHAOS", "DECAY", "DRIVE", "FX", "ATTACK", "BEND"},
    {"TONE", "SNAP", "CHAOS", "DECAY", "DRIVE", "FX", "ATTACK", "RING"},
    {"TONE", "GRIT", "CHAOS", "DECAY", "DRIVE", "FX", "ATTACK", "RING"},
};
static uint32_t pages_n(const sound_t *s) { (void)s;return 6; }
static uint32_t page_id(const sound_t *s,uint32_t page) { static const uint8_t ID[6]={0,1,PG_WRECK,PG_MACRO,PG_ROUTE,PG_ROUTE2}; (void)s;return ID[page<6?page:0]; }
static void page_title(const sound_t *s,uint32_t id,char *text)
{
    str_cpy(text,id==0?prof_name(s):id==1?"ENV":id==PG_WRECK?"WRECK":id==PG_MACRO?"MACRO":id==PG_ROUTE?"ROUTE 1":"ROUTE 3",12);
}
static uint32_t native_cut(const sound_t *s) { return s->mach==ENG_BD?2:s->mach==ENG_HHT?2:4; }
static uint32_t native_q(const sound_t *s) { return s->mach==ENG_BD?3:s->mach==ENG_HHT?3:5; }
static uint32_t native_drive(const sound_t *s) { (void)s; return P_DRIVE; }
/* a route's fields in syn[24 + 2 r]: source + 256 x destination; depth + 128 + 256 x macro */
static uint32_t rt_src(const sound_t *s,uint32_t r) { return (uint32_t)s->syn[P_ROUTE+2u*r]&255u; }
static uint32_t rt_dst(const sound_t *s,uint32_t r) { return ((uint32_t)s->syn[P_ROUTE+2u*r]>>8)&7u; }
static int32_t rt_depth(const sound_t *s,uint32_t r) { return (int32_t)((uint32_t)s->syn[P_ROUTE+1u+2u*r]&255u)-128; }
static uint32_t rt_mac(const sound_t *s,uint32_t r) { return ((uint32_t)s->syn[P_ROUTE+1u+2u*r]>>8)%9u; }
static void rt_set(sound_t *s,uint32_t r,uint32_t src,uint32_t dst,int32_t depth,uint32_t mac)
{
    s->syn[P_ROUTE+2u*r]=(int16_t)((src&255u)|(dst&7u)<<8);
    s->syn[P_ROUTE+1u+2u*r]=(int16_t)((uint32_t)(clampi(depth,-127,127)+128)|(mac%9u)<<8);
}
/* parameter j (0 .. 7) of page id: its definition. The main view's virtual pages: FX A VOL (for the locks) */
static void par_get(const sound_t *s, uint32_t id, uint32_t j, par_t *p)
{
    static const par_t none=PX,SRC={"SRC",0,P23_PINS+1,0,PK_PIN,0},DEP={"DEPTH",-127,127,0,PK_BIP,0};
    j &= 7u;
    *p=none;
    if (id == 0) {
        /* the circuit's knobs, named as its mode uses them */
        static const struct { uint8_t mach, mode, j; const char *name; } REN[] = {
            {ENG_BD,1,2,"HEAD"},{ENG_BD,1,3,"STICK"},{ENG_BD,2,2,"FOLD"},{ENG_BD,2,3,"RUMBLE"},
            {ENG_BASS,2,0,"BRIGHT"},{ENG_BASS,2,1,"SUSTAIN"},{ENG_BASS,3,0,"RATIO"},{ENG_BASS,3,1,"INDEX"},
            {ENG_BASS,4,0,"FDBK"},{ENG_BASS,4,1,"RING"},
            {ENG_SD,1,2,"NOISE"},{ENG_SD,2,4,"BUZZ"},{ENG_SD,3,0,"CRUSH"},{ENG_SD,3,1,"CHOP"},{ENG_SD,3,2,"BITS"},
            {ENG_HHT,1,0,"RING"},{ENG_HHT,2,0,"DENSITY"},{ENG_HHT,3,0,"DENSITY"}};
        uint32_t r, md = (uint32_t)clampi(s->syn[7], 0, 7);
        *p = PAR_P[s->mach % NENG][j];
        for (r = 0; r < sizeof REN / sizeof REN[0]; r++)
            if (REN[r].mach == s->mach && REN[r].mode == md && REN[r].j == j)
                p->name = REN[r].name;
    }
    else if (id == 1) *p = PAR_P_ENV[j];
    else if (id == PG_MACRO) { par_t m={MAC_NAME[s->mach%NENG][j],0,127,0,PK_NUM,0}; *p=m; }
    else if (id == PG_ROUTE || id == PG_ROUTE2) {
        uint32_t f=j&3u;
        if(f==0) *p=SRC;
        else if(f==1) { par_t d={"DST",0,7,0,PK_LIST,P_RDST[s->mach%NENG]}; *p=d; }
        else if(f==2) *p=DEP;
        else { par_t m={"MACRO",0,8,0,PK_LIST,P_RMAC}; *p=m; }
    }
    else if(id==PG_WRECK && j<5) *p=PAR_WRECK[j];
    else if(id==PG_FX) {
        if(j==0) *p=PAR_P_ENV[2];
        else if(j==1) *p=PAR_P_ENV[4];
        else if(j==4) *p=PAR_P_ENV[3];
        else if(j==5) *p=PAR_P_ENV[1];
        else if(j==6) *p=PAR_P_ENV[0];
    } else if(id==PG_AMP && (j==0 || j==2 || j==4 || j==7)) *p=PAR_P_ENV[j==0?0:j==7?2:1];
}
/* the macro page's storage: M1 .. M4 and M8 bipolar about the recipe (syn[16 ..]); M5 DRIVE, M6 FX, M7 ATT the
 * parameters themselves */
static int16_t *mac_ref(sound_t *s,uint32_t j)
{
    static int16_t fx;
    if(j==4) return &s->syn[P_DRIVE];
    if(j==6) return &s->syn[P_ATT];
    if(j==5) { fx=s->fx[X_DEL]; return &fx; }
    return &s->syn[P_MACRO+j];
}
static int32_t par_val(const sound_t *s,uint32_t id,uint32_t j)
{
    j&=7u;
    if(id==0) return SYN_IX[s->mach%NENG][j]<8?s->syn[SYN_IX[s->mach%NENG][j]]:0;
    if(id==1) return j==0?s->syn[P_ATT]:j==1?s->syn[P_REL]:j==2?s->amp[A_VOL]:j==3?s->fx[X_DEL]:j==4?s->syn[P_DRIVE]:0;
    if(id==PG_MACRO) return *mac_ref((sound_t *)s,j);
    if(id==PG_ROUTE || id==PG_ROUTE2) {
        uint32_t r=(id==PG_ROUTE2?2u:0u)+j/4u,f=j&3u;
        return f==0?(int32_t)rt_src(s,r):f==1?(int32_t)rt_dst(s,r):f==2?rt_depth(s,r):(int32_t)rt_mac(s,r);
    }
    if(id==PG_WRECK) return j<5?s->syn[P_WRECK+j]:0;
    if(id==PG_AMP) return j==0?s->syn[P_ATT]:j==7?s->amp[A_VOL]:s->syn[P_REL];
    if(id==PG_FX) return j==0?s->amp[A_VOL]:j==1?s->syn[P_DRIVE]:j==4?s->fx[X_DEL]:j==5?s->syn[P_REL]:j==6?s->syn[P_ATT]:0;
    return 0;
}
static void par_put(sound_t *s,uint32_t id,uint32_t j,int32_t value)
{
    par_t p;
    j&=7u;
    par_get(s,id,j,&p);
    if(p.kind==PK_NONE) return;
    value=clampi(value,p.lo,p.hi);
    if(id==0) { if(SYN_IX[s->mach%NENG][j]<8) s->syn[SYN_IX[s->mach%NENG][j]]=(int16_t)value; }
    else if(id==1) {
        if(j==0) s->syn[P_ATT]=(int16_t)value;
        else if(j==1) s->syn[P_REL]=(int16_t)value;
        else if(j==2) s->amp[A_VOL]=(uint8_t)value;
        else if(j==3) s->fx[X_DEL]=(uint8_t)value;
        else if(j==4) s->syn[P_DRIVE]=(int16_t)value;
    } else if(id==PG_MACRO) {
        if(j==5) s->fx[X_DEL]=(uint8_t)value; else *mac_ref(s,j)=(int16_t)value;
    } else if(id==PG_ROUTE || id==PG_ROUTE2) {
        uint32_t r=(id==PG_ROUTE2?2u:0u)+j/4u,f=j&3u;
        rt_set(s,r,f==0?(uint32_t)value:rt_src(s,r),f==1?(uint32_t)value:rt_dst(s,r),f==2?value:rt_depth(s,r),f==3?(uint32_t)value:rt_mac(s,r));
    } else if(id==PG_WRECK && j<5) s->syn[P_WRECK+j]=(int16_t)value;
    else if(id==PG_AMP) {
        if(j==7)s->amp[A_VOL]=(uint8_t)value;
        else if(j==0)s->syn[P_ATT]=(int16_t)value;
        else if(j==2 || j==4)s->syn[P_REL]=(int16_t)value;
    } else if(id==PG_FX) {
        if(j==0) s->amp[A_VOL]=(uint8_t)value;
        else if(j==1) s->syn[P_DRIVE]=(int16_t)value;
        else if(j==4) s->fx[X_DEL]=(uint8_t)value;
        else if(j==5) s->syn[P_REL]=(int16_t)value;
        else if(j==6) s->syn[P_ATT]=(int16_t)value;
    }
}
/* the value as it reads */
/* the ranges as they show and turn: a plain 0 .. 127 as 0 .. 100, a bipolar 0 .. 127 (centre 64) as -50 .. +50, a
 * click a step of what shows (the sound keeps the Digitone II's 0 .. 127: its curves are fitted to them) */
static int32_t pct_disp(int32_t v) { return (clampi(v, 0, 127) * 100 + 63) / 127; }
static int32_t pct_raw(int32_t d) { return (clampi(d, 0, 100) * 127 + 50) / 100; }
static int32_t bip_disp(int32_t v)
{
    int32_t d = clampi(v, 1, 127) - 64;
    return d >= 0 ? (d * 50 + 31) / 63 : -((-d * 50 + 31) / 63);
}
static int32_t bip_raw(int32_t d)
{
    d = clampi(d, -50, 50);
    return 64 + (d >= 0 ? (d * 63 + 25) / 50 : -((-d * 63 + 25) / 50));
}
static int par_pct(const par_t *p) { return (p->kind == PK_NUM || p->kind == PK_HOLD) && p->lo == 0 && p->hi == 127; }
static int par_bip(const par_t *p) { return p->kind == PK_BIP && p->lo == 0 && p->hi == 127 && p->mid == 64; }
/* a parameter at v turned s clicks */
static int32_t par_step(const par_t *p, int32_t v, int32_t s)
{
    if (par_pct(p))
        return pct_raw(pct_disp(v) + s);
    if (par_bip(p))
        return bip_raw(bip_disp(v) + s);
    return clampi(v + s, p->lo, p->hi);
}
static void par_text(const par_t *p, int32_t v, char *b)
{
    b[0] = 0;
    if (par_pct(p) && !(p->kind == PK_HOLD && v >= 127)) {
        fmt_int(b, pct_disp(v));
        return;
    }
    switch (p->kind) {
    case PK_BIP:
        v = par_bip(p) ? bip_disp(v) : v - p->mid;
        if (v > 0)
            str_cpy(b, "+", 2);
        fmt_int(b + str_len(b), v);
        break;
    case PK_ALGO:
        fmt_int(b, v + 1);
        break;

    case PK_ONOFF:
        str_cpy(b, v ? "ON" : "OFF", 4);
        break;
    case PK_LIST:
        str_cpy(b, p->list[clampi(v, 0, p->hi)], 12);
        break;
    case PK_HOLD:
        if (v >= 127)
            str_cpy(b, "NOTE", 6);
        else
            fmt_int(b, v);
        break;
    case PK_PHC:
        if (v >= 91)
            str_cpy(b, "OFF", 4);
        else
            fmt_int(b, v);
        break;
    case PK_NONE:
        break;
    case PK_PIN:
        pin_name((uint32_t)v, b);
        break;
    default:
        fmt_int(b, v);
        break;
    }
}
#undef P_
#undef PL
#undef PN
#undef PB
#undef PO
#undef PX

/* ------------------------------------------------------------- LFOs --- */
/* (build 50) an LFO's destination as a parameter of the sound (dest 1 .. 72: 1 + page id x 8 + knob) that this
 * machine has: 0 if not (none, another machine's SYN page, an empty place, FLTR 2's G -- the filter's machine) */
static int lfo_par(const sound_t *s, uint32_t dest, par_t *p)
{
    uint32_t id, pg, k;
    if (!dest || dest >= LFO_D_LFO)
        return 0;
    id = dest - 1u;
    pg = id / 8u;
    k = id % 8u;
    if ((pg < 4u && pg >= SYN_PAGES[s->mach % NENG]) || pg == PG_FLT2 || (pg == PG_MACRO && k == 5u))
        return 0;
    par_get(s, pg, k, p);
    return p->kind != PK_NONE && p->hi > p->lo;
}
/* the LFOs' shares (Q15, of each destination's whole range) onto a sound (tracks.c, the audio's: each cook) */
static void lfo_apply(sound_t *s, const int32_t *mod)
{
    uint32_t n;
    for (n = 0; n < NLFO; n++) {
        par_t p;
        uint32_t d = s->lfo[n].dest;
        int32_t v;
        if (!mod[n] || !lfo_par(s, d, &p))
            continue;
        v = par_val(s, (d - 1u) / 8u, (d - 1u) % 8u) + (int32_t)(((int64_t)mod[n] * (p.hi - p.lo)) >> 15);
        par_put(s, (d - 1u) / 8u, (d - 1u) % 8u, clampi(v, p.lo, p.hi));
    }
}
/* a destination as it reads on the LFO page: none, its page (S1 .. S4 the SYN pages, F1, F2, AMP, FX, MNG) and the
 * parameter's name, or LFO 1 / 2's SPEED or DEPTH */
static void lfo_dest_text(const sound_t *s, uint32_t dest, char *b)
{
    static const char *const LD[4] = {"L1 SPEED", "L1 DEPTH", "L2 SPEED", "L2 DEPTH"};
    static const char *const PT[9] = {"S1 ", "S2 ", "S3 ", "S4 ", "F1 ", "F2 ", "AMP ", "FX ", "MNG "};
    par_t p;
    if (!dest) {
        str_cpy(b, "NONE", 8);
        return;
    }
    if (dest >= LFO_D_LFO) {
        str_cpy(b, LD[(dest - LFO_D_LFO) & 3u], 12);
        return;
    }
    if (!lfo_par(s, dest, &p)) {
        str_cpy(b, "--", 4);
        return;
    }
    str_cpy(b, PT[(dest - 1u) / 8u % 9u], 6);
    str_cpy(b + str_len(b), p.name, 16);
}
/* a destination LFO n (0 .. 2) may take: none, this machine's parameters, the earlier LFOs' SPEED and DEPTH */
static int lfo_dest_ok(const sound_t *s, uint32_t n, uint32_t d)
{
    par_t q;
    if (!d)
        return 1;
    if (d >= LFO_D_LFO)
        return d < LFO_D_LFO + 2u * n && d < LFO_ND;
    return lfo_par(s, d, &q);
}
/* the destination st steps on from dest, the ones LFO n may not take passed over (at an end: it stays) */
static uint32_t lfo_dest_step(const sound_t *s, uint32_t n, uint32_t dest, int32_t st)
{
    for (; st; st += st > 0 ? -1 : 1) {
        uint32_t d = dest;
        do {
            if (st > 0 ? d + 1u >= LFO_ND : d == 0u) {
                d = dest;
                break;
            }
            d = st > 0 ? d + 1u : d - 1u;
        } while (!lfo_dest_ok(s, n, d));
        dest = d;
    }
    return dest;
}
