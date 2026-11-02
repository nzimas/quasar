/* SPDX-License-Identifier: GPL-3.0-only
 * Pulsar23 sound families. HOME and family browsing follow Bigorna's API.
 */
static int str_has(const char *s, const char *w)  /* (w within s) */
{
    uint32_t i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; w[j] && s[i + j] == w[j]; j++)
            ;
        if (!w[j])
            return 1;
    }
    return 0;
}

typedef struct { rnd_t r; int32_t ind, idm, abs_; } gctx_t;   /* the tastes, 0 .. 100 */
static int32_t RI(gctx_t *g, int32_t a, int32_t b) { return a + rnd_below(&g->r, b - a + 1); }
static int CH(gctx_t *g, int32_t pct) { return rnd_below(&g->r, 100) < pct; }

/* The families. Each is a circuit and its mode (BD TRI / SKIN, BASS DCO / PRC / PLUCK / FM, SD NOISE / TONE / WIRE,
 * HHT NOISE / METAL / SHAKE), its knobs, and up to four of the sound's own patch routes (a source, where it goes,
 * how deep, the macro that scales it); a route lists up to three sources, one drawn each time. Their starting
 * points: the Synthdawg Notebook's sound-design examples and patch recipes, the SOMA manual's tips and patches the
 * instrument's players share (docs/ENGINE.md), and percussion beyond the instrument's four voices (membranes, strings,
 * FM bells and woods, 808-style tone snares and metal, comb-filtered wires, shakers). The instrument is percussive:
 * every release is short (engine_control.c REL_MAX) and every sound falls once struck; SHAOS and the clock dividers
 * step timbre, drive and level, never pitch. Drawing one: every knob moves by the family's spread, each route picks
 * its source and depth, the drive and send wander, and about one time in two a further route is patched from the
 * circuit's pool -- so the same family seldom comes out twice the same. Pitch is the note's (the scale); off is the
 * family's offset from it, in semitones. */
#define S_OUT P23_PINS                   /* (the track's own OUT, ENV: engine_control.c PIN_OWN_*) */
#define S_ENV (P23_PINS + 1u)
#define S_T1E P23_PIN_T(0, P23_T_ENV)    /* (track 1, the kit's kick at the start) */
#define S_T1O P23_PIN_T(0, P23_T_OUT)
#define S_T2O P23_PIN_T(1, P23_T_OUT)
#define S_T3O P23_PIN_T(2, P23_T_OUT)
#define S_T4O P23_PIN_T(3, P23_T_OUT)
enum { RO_K, RO_S, RO_H, RO_P, RO_N, RO_B, RO_L, RO_T, RO_D };   /* (phrase.c's roles, in its order) */
typedef struct { uint8_t src[3], dst; int8_t depth; uint8_t mac; } rr_t;
typedef struct {
    const char *name;
    uint8_t mach, mode, role;
    int8_t off;
    uint8_t k[6];                        /* the circuit's knobs, syn[1 ..] (params.c SYN_IX) */
    uint8_t att, rel, drive, fx, spread;
    rr_t r[4];
} recipe_t;
#define R_(a, b, c, d, dep, m) {{a, b, c}, d, dep, m}
#define RN {{0, 0, 0}, 0, 0, 0}

static const recipe_t RECIPES[] = {
    {"BURNT 909", ENG_BD, P23_BD_TRI, RO_K, 9, {0, 80, 80, 0, 0, 0}, 0, 46, 105, 0, 12, {RN, RN, RN, RN}},
    {"HARD KICK", ENG_BD, P23_BD_TRI, RO_K, 7, {0, 70, 40, 0, 0, 0}, 0, 38, 75, 0, 12, {RN, RN, RN, RN}},
    {"BLOWN 808", ENG_BD, P23_BD_TRI, RO_K, 5, {0, 40, 30, 0, 0, 0}, 0, 58, 110, 0, 12, {RN, RN, RN, RN}},
    {"CLICK KICK", ENG_BD, P23_BD_TRI, RO_K, 10, {0, 110, 55, 1, 0, 0}, 0, 26, 85, 0, 12, {RN, RN, RN, RN}},
    {"SQUARE THUD", ENG_BD, P23_BD_TRI, RO_K, 7, {0, 60, 120, 0, 0, 0}, 0, 40, 80, 0, 12, {RN, RN, RN, RN}},
    {"GRIT KICK", ENG_BD, P23_BD_TRI, RO_K, 7, {50, 60, 50, 0, 0, 0}, 0, 44, 90, 0, 14, {R_(P23_PIN_NOISE, 0, 0, P23_RD_MOD1, 40, 3), RN, RN, RN}},
    {"NOISE KICK", ENG_BD, P23_BD_TRI, RO_K, 4, {0, 45, 30, 0, 0, 0}, 0, 40, 80, 0, 14, {R_(P23_PIN_NOISE, 0, 0, P23_RD_MOD2, 50, 3), R_(P23_PIN_NOISE, 0, 0, P23_RD_DRIVE, 40, 8), RN, RN}},
    {"WTF GROWL", ENG_BD, P23_BD_TRI, RO_N, 5, {0, 40, 40, 0, 0, 0}, 0, 48, 70, 10, 20, {R_(S_OUT, 0, 0, P23_RD_MOD2, 80, 3), RN, RN, RN}},
    {"OMG METAL", ENG_BD, P23_BD_TRI, RO_P, 12, {0, 50, 70, 0, 0, 0}, 0, 44, 70, 20, 20, {R_(S_OUT, 0, 0, P23_RD_MOD3, 70, 3), RN, RN, RN}},
    {"CROSS FM BD", ENG_BD, P23_BD_TRI, RO_K, 7, {0, 60, 35, 0, 0, 0}, 0, 46, 80, 0, 16, {R_(S_T2O, S_T3O, S_T4O, P23_RD_MOD3, 60, 3), RN, RN, RN}},
    {"PUMP KICK", ENG_BD, P23_BD_TRI, RO_K, 8, {0, 55, 40, 0, 0, 0}, 0, 42, 90, 0, 14, {R_(P23_PIN_DIV8, P23_PIN_DIV4, P23_PIN_DIV16, P23_RD_DRIVE, 60, 3), RN, RN, RN}},
    {"STEP DRIVE BD", ENG_BD, P23_BD_TRI, RO_K, 7, {0, 65, 45, 0, 0, 0}, 0, 40, 70, 0, 14, {R_(P23_PIN_SH_2DIR, P23_PIN_SH_1SH, 0, P23_RD_DRIVE, 70, 3), RN, RN, RN}},
    {"ACID KICK", ENG_BD, P23_BD_TRI, RO_K, 7, {0, 70, 60, 0, 0, 0}, 0, 40, 110, 0, 16, {R_(P23_PIN_LFO_SQR, P23_PIN_DIV16, 0, P23_RD_DRIVE, 40, 3), RN, RN, RN}},
    {"FEEDBACK BD", ENG_BD, P23_BD_TRI, RO_N, 5, {0, 50, 40, 0, 0, 0}, 0, 48, 80, 10, 20, {R_(S_OUT, 0, 0, P23_RD_EXT, 50, 3), RN, RN, RN}},
    {"BASS-FED BD", ENG_BD, P23_BD_TRI, RO_K, 7, {0, 60, 50, 0, 0, 0}, 0, 46, 90, 0, 16, {R_(S_T2O, S_T3O, 0, P23_RD_EXT, 70, 3), RN, RN, RN}},
    {"HAT-FED BD", ENG_BD, P23_BD_TRI, RO_K, 7, {0, 70, 40, 0, 0, 0}, 0, 40, 80, 0, 16, {R_(S_T4O, 0, 0, P23_RD_EXT, 60, 3), RN, RN, RN}},
    {"GLITCH KICK", ENG_BD, P23_BD_TRI, RO_N, 7, {0, 60, 40, 0, 0, 0}, 0, 42, 80, 15, 20, {R_(P23_PIN_DIV16, P23_PIN_CLK, 0, P23_RD_VCA, -70, 3), R_(P23_PIN_NOISE, 0, 0, P23_RD_MOD2, 40, 8), RN, RN}},
    {"LASER ZAP", ENG_BD, P23_BD_TRI, RO_N, 24, {127, 127, 60, 0, 0, 0}, 0, 26, 80, 30, 16, {R_(S_ENV, 0, 0, P23_RD_MOD1, 127, 3), RN, RN, RN}},
    {"SYN TOM BURN", ENG_BD, P23_BD_TRI, RO_P, 17, {90, 20, 20, 0, 0, 0}, 0, 50, 90, 20, 14, {R_(S_ENV, 0, 0, P23_RD_MOD1, 90, 3), RN, RN, RN}},
    {"HAMMER", ENG_BD, P23_BD_HAMMER, RO_K, 7, {0, 60, 60, 1, 0, 0}, 0, 44, 60, 0, 14, {RN, RN, RN, RN}},
    {"GABBER", ENG_BD, P23_BD_HAMMER, RO_K, 9, {0, 90, 110, 0, 0, 0}, 0, 34, 110, 0, 14, {RN, RN, RN, RN}},
    {"RUMBLE KICK", ENG_BD, P23_BD_HAMMER, RO_K, 4, {0, 55, 40, 1, 0, 0}, 0, 54, 70, 0, 14, {RN, RN, RN, RN}},
    {"DOOM", ENG_BD, P23_BD_HAMMER, RO_K, -2, {0, 40, 80, 1, 0, 0}, 0, 58, 100, 10, 14, {RN, RN, RN, RN}},
    {"STOMP", ENG_BD, P23_BD_HAMMER, RO_K, 2, {0, 75, 70, 1, 0, 0}, 0, 40, 90, 0, 16, {R_(P23_PIN_NOISE, 0, 0, P23_RD_MOD3, 30, 3), RN, RN, RN}},
    {"PIPE HIT", ENG_BD, P23_BD_HAMMER, RO_P, 19, {0, 30, 90, 0, 0, 0}, 0, 26, 50, 10, 16, {RN, RN, RN, RN}},
    {"ANVIL", ENG_BD, P23_BD_HAMMER, RO_P, 24, {60, 20, 127, 0, 0, 0}, 0, 30, 80, 10, 16, {R_(S_ENV, 0, 0, P23_RD_MOD1, 60, 3), RN, RN, RN}},
    {"FOLD ZAP", ENG_BD, P23_BD_HAMMER, RO_N, 30, {100, 127, 100, 0, 0, 0}, 0, 22, 60, 20, 18, {R_(S_ENV, 0, 0, P23_RD_MOD1, 120, 3), RN, RN, RN}},
    {"CROSS HAMMER", ENG_BD, P23_BD_HAMMER, RO_K, 7, {0, 60, 90, 1, 0, 0}, 0, 42, 80, 0, 18, {R_(S_T2O, S_T3O, S_T4O, P23_RD_MOD3, 60, 3), RN, RN, RN}},
    {"STEP HAMMER", ENG_BD, P23_BD_HAMMER, RO_K, 6, {0, 65, 70, 1, 0, 0}, 0, 40, 70, 0, 18, {R_(P23_PIN_SH_3SH, P23_PIN_SH_2DIR, 0, P23_RD_DRIVE, 60, 3), R_(P23_PIN_DIV8, P23_PIN_DIV4, 0, P23_RD_VCA, -50, 8), RN, RN}},
    {"FED HAMMER", ENG_BD, P23_BD_HAMMER, RO_N, 9, {0, 50, 100, 0, 0, 0}, 0, 46, 70, 10, 20, {R_(S_T3O, S_T4O, 0, P23_RD_EXT, 70, 3), RN, RN, RN}},
    {"WAR TOM", ENG_BD, P23_BD_SKIN, RO_P, 0, {0, 35, 60, 1, 0, 0}, 0, 52, 80, 10, 14, {RN, RN, RN, RN}},
    {"TOM BLAST", ENG_BD, P23_BD_SKIN, RO_P, 7, {0, 45, 80, 1, 0, 0}, 0, 44, 100, 10, 14, {RN, RN, RN, RN}},
    {"TABLA", ENG_BD, P23_BD_SKIN, RO_P, 24, {0, 55, 110, 0, 0, 0}, 0, 40, 40, 15, 16, {RN, RN, RN, RN}},
    {"TIMBALE", ENG_BD, P23_BD_SKIN, RO_P, 22, {0, 10, 90, 1, 0, 0}, 0, 36, 70, 20, 14, {RN, RN, RN, RN}},
    {"DHOL", ENG_BD, P23_BD_SKIN, RO_K, 3, {0, 60, 50, 1, 0, 0}, 0, 46, 80, 10, 16, {RN, RN, RN, RN}},
    {"SKIN KICK", ENG_BD, P23_BD_SKIN, RO_K, -5, {0, 70, 10, 1, 0, 0}, 0, 42, 90, 0, 14, {RN, RN, RN, RN}},
    {"TALKING DRUM", ENG_BD, P23_BD_SKIN, RO_P, 12, {90, 25, 60, 0, 0, 0}, 0, 46, 40, 10, 16, {R_(S_ENV, 0, 0, P23_RD_MOD1, -70, 3), RN, RN, RN}},
    {"FRAME DRUM", ENG_BD, P23_BD_SKIN, RO_P, 5, {0, 20, 80, 0, 0, 0}, 0, 56, 50, 25, 14, {RN, RN, RN, RN}},
    {"SKIN CROSS", ENG_BD, P23_BD_SKIN, RO_N, 10, {0, 30, 70, 0, 0, 0}, 0, 44, 70, 20, 18, {R_(S_T1O, S_T3O, S_T4O, P23_RD_EXT, 70, 3), RN, RN, RN}},
    {"SURDO", ENG_BD, P23_BD_SKIN, RO_K, -3, {0, 25, 40, 1, 0, 0}, 0, 58, 70, 10, 14, {RN, RN, RN, RN}},
    {"STEP TOMS", ENG_BD, P23_BD_SKIN, RO_P, 9, {0, 45, 60, 0, 0, 0}, 0, 40, 60, 15, 16, {R_(P23_PIN_SH_3SH, P23_PIN_SH_2DIR, 0, P23_RD_DRIVE, 60, 3), R_(P23_PIN_DIV8, P23_PIN_DIV4, 0, P23_RD_VCA, -50, 8), RN, RN}},
    {"GRIND", ENG_BASS, P23_BASS_GRIND, RO_L, 0, {80, 60, 0, 70, 90, 0}, 0, 40, 70, 0, 16, {RN, RN, RN, RN}},
    {"HOWL", ENG_BASS, P23_BASS_GRIND, RO_N, 0, {110, 20, 0, 60, 115, 10}, 0, 52, 50, 20, 16, {RN, RN, RN, RN}},
    {"SCREAMER", ENG_BASS, P23_BASS_GRIND, RO_L, 12, {127, 90, 0, 90, 100, 0}, 0, 34, 90, 10, 16, {RN, RN, RN, RN}},
    {"SAW BLADE", ENG_BASS, P23_BASS_GRIND, RO_N, 7, {60, 110, 0, 100, 60, 0}, 0, 30, 80, 0, 16, {RN, RN, RN, RN}},
    {"FEEDBACK LOW", ENG_BASS, P23_BASS_GRIND, RO_B, -12, {90, 40, 0, 40, 80, 20}, 0, 44, 80, 0, 16, {RN, RN, RN, RN}},
    {"RUST", ENG_BASS, P23_BASS_GRIND, RO_N, 0, {70, 127, 0, 50, 70, 0}, 0, 26, 100, 0, 16, {RN, RN, RN, RN}},
    {"SIREN", ENG_BASS, P23_BASS_GRIND, RO_L, 12, {100, 30, 0, 80, 110, 70}, 0, 58, 50, 30, 16, {R_(S_ENV, 0, 0, P23_RD_MOD5, 60, 3), RN, RN, RN}},
    {"DRILL", ENG_BASS, P23_BASS_GRIND, RO_N, 19, {127, 60, 0, 110, 30, 0}, 0, 34, 90, 0, 16, {R_(P23_PIN_DIV16, P23_PIN_DIV8, 0, P23_RD_VCA, -60, 3), RN, RN, RN}},
    {"GRIND PUMP", ENG_BASS, P23_BASS_GRIND, RO_B, 0, {90, 50, 0, 60, 90, 0}, 0, 40, 80, 0, 16, {R_(S_T1E, 0, 0, P23_RD_VCA, -90, 3), RN, RN, RN}},
    {"STEP GRIND", ENG_BASS, P23_BASS_GRIND, RO_L, 0, {100, 40, 0, 60, 100, 0}, 0, 36, 70, 0, 18, {R_(P23_PIN_SH_3SH, P23_PIN_SH_2DIR, 0, P23_RD_MOD5, 70, 3), R_(P23_PIN_SH_1SH, 0, 0, P23_RD_MOD3, 50, 8), RN, RN}},
    {"FED GRIND", ENG_BASS, P23_BASS_GRIND, RO_N, 7, {80, 70, 0, 80, 90, 0}, 0, 40, 70, 10, 18, {R_(S_T1O, S_T3O, S_T4O, P23_RD_EXT, 60, 3), RN, RN, RN}},
    {"FM GROWL", ENG_BASS, P23_BASS_DCO, RO_B, 0, {20, 30, 90, 80, 30, 0}, 0, 44, 90, 0, 18, {R_(S_OUT, 0, 0, P23_RD_MOD4, 70, 3), RN, RN, RN}},
    {"SCREAM", ENG_BASS, P23_BASS_DCO, RO_L, 0, {110, 60, 0, 80, 90, 10}, 0, 44, 90, 20, 18, {R_(S_OUT, 0, 0, P23_RD_MOD3, 60, 3), RN, RN, RN}},
    {"WARP FUZZ", ENG_BASS, P23_BASS_DCO, RO_B, 0, {40, 120, 0, 90, 30, 0}, 0, 40, 110, 0, 14, {RN, RN, RN, RN}},
    {"NOISE BASS", ENG_BASS, P23_BASS_DCO, RO_B, 0, {64, 40, 0, 50, 80, 0}, 0, 38, 90, 0, 14, {R_(P23_PIN_NOISE, 0, 0, P23_RD_EXT, 50, 3), RN, RN, RN}},
    {"EXT LOOP", ENG_BASS, P23_BASS_DCO, RO_N, 0, {64, 40, 0, 60, 80, 0}, 0, 44, 80, 0, 18, {R_(S_OUT, 0, 0, P23_RD_EXT, 40, 3), RN, RN, RN}},
    {"CROSSMOD", ENG_BASS, P23_BASS_DCO, RO_L, 0, {30, 20, 100, 90, 20, 0}, 0, 48, 70, 10, 18, {R_(S_T1O, S_T3O, S_T4O, P23_RD_MOD4, 60, 3), RN, RN, RN}},
    {"SNARE-FM BASS", ENG_BASS, P23_BASS_DCO, RO_L, 0, {30, 30, 90, 80, 30, 0}, 0, 44, 70, 10, 18, {R_(S_T3O, 0, 0, P23_RD_MOD4, 60, 3), RN, RN, RN}},
    {"STEP SCREAM", ENG_BASS, P23_BASS_DCO, RO_B, 0, {70, 25, 0, 40, 105, 0}, 0, 32, 90, 0, 14, {R_(P23_PIN_SH_3SH, P23_PIN_SH_2DIR, 0, P23_RD_MOD5, 70, 3), RN, RN, RN}},
    {"PLANET PING", ENG_BASS, P23_BASS_DCO, RO_N, 0, {0, 20, 110, 100, 20, 10}, 0, 48, 60, 40, 18, {R_(S_ENV, 0, 0, P23_RD_MOD4, 90, 3), R_(P23_PIN_LFO_TRI, 0, 0, P23_RD_MOD2, 40, 8), RN, RN}},
    {"BOOMY PRC", ENG_BASS, P23_BASS_PRC, RO_K, -12, {19, 25, 0, 51, 76, 0}, 0, 60, 70, 0, 12, {RN, RN, RN, RN}},
    {"SCREETCH", ENG_BASS, P23_BASS_PRC, RO_N, 7, {76, 51, 0, 76, 114, 0}, 0, 56, 80, 20, 16, {RN, RN, RN, RN}},
    {"STEAM TRAIN", ENG_BASS, P23_BASS_PRC, RO_N, 0, {44, 44, 80, 76, 76, 0}, 4, 25, 70, 10, 16, {R_(P23_PIN_DIV16, P23_PIN_DIV8, 0, P23_RD_VCA, -80, 3), R_(P23_PIN_NOISE, 0, 0, P23_RD_MOD3, 40, 8), RN, RN}},
    {"METAL PRC", ENG_BASS, P23_BASS_PRC, RO_P, 0, {127, 80, 0, 110, 40, 0}, 0, 56, 80, 20, 16, {RN, RN, RN, RN}},
    {"GAMELAN", ENG_BASS, P23_BASS_PRC, RO_P, 12, {110, 30, 0, 120, 30, 0}, 0, 60, 40, 40, 14, {RN, RN, RN, RN}},
    {"SCRAP PRC", ENG_BASS, P23_BASS_PRC, RO_N, 5, {127, 127, 0, 90, 90, 0}, 0, 40, 110, 10, 18, {R_(P23_PIN_NOISE, 0, 0, P23_RD_MOD3, 60, 3), RN, RN, RN}},
    {"SITAR BUZZ", ENG_BASS, P23_BASS_PLUCK, RO_L, 12, {110, 100, 0, 127, 50, 0}, 0, 56, 80, 15, 16, {R_(P23_PIN_NOISE, 0, 0, P23_RD_EXT, 20, 3), RN, RN, RN}},
    {"BERIMBAU", ENG_BASS, P23_BASS_PLUCK, RO_L, 0, {80, 80, 0, 90, 60, 0}, 0, 48, 60, 10, 16, {R_(S_ENV, 0, 0, P23_RD_MOD5, 50, 3), RN, RN, RN}},
    {"STRING HIT", ENG_BASS, P23_BASS_PLUCK, RO_N, 0, {90, 50, 0, 100, 30, 0}, 0, 34, 110, 0, 16, {RN, RN, RN, RN}},
    {"FED STRING", ENG_BASS, P23_BASS_PLUCK, RO_N, 7, {60, 95, 0, 110, 50, 0}, 0, 52, 70, 20, 18, {R_(S_T1O, S_T2O, S_T4O, P23_RD_EXT, 60, 3), RN, RN, RN}},
    {"WIRE SNAP", ENG_BASS, P23_BASS_PLUCK, RO_P, 12, {127, 40, 0, 127, 80, 0}, 0, 30, 110, 0, 16, {RN, RN, RN, RN}},
    {"PIANO WIRE", ENG_BASS, P23_BASS_PLUCK, RO_N, -12, {100, 110, 0, 110, 60, 0}, 0, 62, 70, 30, 16, {RN, RN, RN, RN}},
    {"GONG", ENG_BASS, P23_BASS_FM, RO_N, -12, {88, 100, 0, 100, 0, 0}, 0, 66, 60, 40, 14, {RN, RN, RN, RN}},
    {"CLANG FM", ENG_BASS, P23_BASS_FM, RO_N, 0, {100, 127, 0, 110, 60, 0}, 0, 30, 100, 10, 18, {R_(P23_PIN_NOISE, 0, 0, P23_RD_MOD4, 40, 3), RN, RN, RN}},
    {"METAL TOM", ENG_BASS, P23_BASS_FM, RO_P, 0, {24, 90, 0, 100, 30, 0}, 0, 44, 80, 10, 16, {R_(S_ENV, 0, 0, P23_RD_MOD1, 30, 3), RN, RN, RN}},
    {"FM KICK", ENG_BASS, P23_BASS_FM, RO_K, -12, {0, 110, 0, 90, 0, 0}, 0, 38, 90, 0, 14, {R_(S_ENV, 0, 0, P23_RD_MOD1, 60, 3), RN, RN, RN}},
    {"FM SNAP", ENG_BASS, P23_BASS_FM, RO_H, 24, {120, 127, 0, 127, 0, 0}, 0, 8, 80, 0, 14, {RN, RN, RN, RN}},
    {"BROKEN BELL", ENG_BASS, P23_BASS_FM, RO_N, 19, {120, 127, 0, 110, 40, 0}, 0, 50, 100, 30, 16, {RN, RN, RN, RN}},
    {"LASER FM", ENG_BASS, P23_BASS_FM, RO_N, 24, {72, 127, 0, 127, 60, 0}, 0, 28, 70, 20, 16, {R_(S_ENV, 0, 0, P23_RD_MOD1, 90, 3), RN, RN, RN}},
    {"INDUSTRIAL", ENG_SD, 0, RO_S, 0, {90, 0, 127, 90, 30, 0}, 0, 50, 110, 20, 18, {R_(S_OUT, 0, 0, P23_RD_MOD1, 70, 3), RN, RN, RN}},
    {"BLOWN SNARE", ENG_SD, 0, RO_S, 12, {0, 30, 110, 55, 10, 0}, 0, 46, 100, 0, 12, {RN, RN, RN, RN}},
    {"TORN 909", ENG_SD, 0, RO_S, 16, {0, 0, 90, 70, 40, 0}, 0, 40, 90, 0, 12, {RN, RN, RN, RN}},
    {"STEEL CLAP", ENG_SD, 0, RO_S, 0, {0, 80, 40, 64, 80, 0}, 0, 25, 80, 10, 12, {RN, RN, RN, RN}},
    {"ECHO CLAP", ENG_SD, 0, RO_S, 0, {0, 110, 40, 76, 85, 0}, 0, 44, 70, 60, 14, {RN, RN, RN, RN}},
    {"GUNSHOT", ENG_SD, 0, RO_S, 20, {0, 0, 114, 114, 110, 0}, 0, 10, 110, 10, 12, {RN, RN, RN, RN}},
    {"HOLLOW BODY", ENG_SD, 0, RO_S, 4, {0, 20, 60, 40, 120, 0}, 0, 48, 70, 10, 14, {R_(S_ENV, 0, 0, P23_RD_MOD2, 40, 3), RN, RN, RN}},
    {"LASER SNARE", ENG_SD, 0, RO_S, 0, {100, 0, 110, 70, 60, 0}, 0, 38, 80, 10, 16, {R_(S_ENV, 0, 0, P23_RD_MOD1, 100, 3), RN, RN, RN}},
    {"BROKEN RADIO", ENG_SD, 0, RO_N, 0, {0, 0, 90, 60, 100, 0}, 0, 44, 80, 20, 20, {R_(P23_PIN_SH_1SH, P23_PIN_SH_3SH, P23_PIN_LFO_SQR, P23_RD_MOD2, 70, 3), RN, RN, RN}},
    {"LOFI CLAP", ENG_SD, 0, RO_S, -8, {0, 90, 20, 40, 60, 0}, 0, 30, 100, 10, 14, {RN, RN, RN, RN}},
    {"CROSS SNARE", ENG_SD, 0, RO_S, 0, {90, 10, 110, 70, 50, 0}, 0, 44, 80, 0, 16, {R_(S_T1O, S_T2O, 0, P23_RD_MOD1, 60, 3), RN, RN, RN}},
    {"SNARE HOWL", ENG_SD, 0, RO_N, 0, {0, 10, 70, 60, 120, 0}, 0, 44, 70, 30, 18, {R_(S_OUT, 0, 0, P23_RD_MOD2, 40, 3), RN, RN, RN}},
    {"NOISE WALL", ENG_SD, P23_SD_WALL, RO_N, 0, {40, 0, 40, 60, 60, 0}, 0, 44, 60, 10, 16, {RN, RN, RN, RN}},
    {"HARSH", ENG_SD, P23_SD_WALL, RO_S, 0, {70, 0, 70, 80, 90, 0}, 0, 34, 100, 0, 16, {RN, RN, RN, RN}},
    {"CHOP NOISE", ENG_SD, P23_SD_WALL, RO_N, 0, {20, 60, 30, 70, 40, 0}, 0, 54, 60, 10, 16, {RN, RN, RN, RN}},
    {"BIT STORM", ENG_SD, P23_SD_WALL, RO_S, 0, {100, 0, 110, 90, 70, 0}, 0, 30, 80, 0, 16, {RN, RN, RN, RN}},
    {"RADIO WALL", ENG_SD, P23_SD_WALL, RO_N, 0, {55, 30, 60, 40, 115, 0}, 0, 50, 60, 20, 18, {R_(P23_PIN_SH_3SH, P23_PIN_SH_1SH, 0, P23_RD_MOD2, 60, 3), RN, RN, RN}},
    {"GRAVEL", ENG_SD, P23_SD_WALL, RO_S, 0, {85, 0, 90, 50, 30, 0}, 0, 24, 90, 0, 16, {RN, RN, RN, RN}},
    {"STUTTER", ENG_SD, P23_SD_WALL, RO_N, 0, {30, 110, 20, 64, 80, 0}, 0, 58, 70, 10, 16, {RN, RN, RN, RN}},
    {"STATIC SNARE", ENG_SD, P23_SD_WALL, RO_S, 0, {60, 0, 50, 90, 100, 0}, 0, 28, 80, 0, 16, {R_(S_ENV, 0, 0, P23_RD_MOD2, 50, 3), RN, RN, RN}},
    {"FED WALL", ENG_SD, P23_SD_WALL, RO_N, 0, {50, 0, 60, 70, 90, 0}, 0, 44, 70, 10, 18, {R_(S_T1O, S_T2O, S_T4O, P23_RD_EXT, 60, 3), RN, RN, RN}},
    {"WALL PUMP", ENG_SD, P23_SD_WALL, RO_N, 0, {40, 0, 40, 64, 60, 0}, 0, 48, 80, 0, 18, {R_(P23_PIN_DIV8, P23_PIN_DIV16, 0, P23_RD_VCA, -70, 3), RN, RN, RN}},
    {"BUZZ SNARE", ENG_SD, P23_SD_WIRE, RO_S, 0, {0, 0, 90, 60, 80, 0}, 0, 40, 70, 0, 14, {RN, RN, RN, RN}},
    {"RATTLE SNR", ENG_SD, P23_SD_WIRE, RO_S, 7, {0, 20, 100, 80, 110, 0}, 0, 34, 70, 0, 14, {RN, RN, RN, RN}},
    {"TIN SNARE", ENG_SD, P23_SD_WIRE, RO_S, 19, {0, 0, 60, 100, 100, 0}, 0, 24, 80, 0, 14, {RN, RN, RN, RN}},
    {"SPRING HIT", ENG_SD, P23_SD_WIRE, RO_N, -7, {0, 0, 80, 40, 120, 0}, 0, 50, 60, 30, 16, {RN, RN, RN, RN}},
    {"COMB CLAP", ENG_SD, P23_SD_WIRE, RO_S, 5, {0, 90, 70, 70, 90, 0}, 0, 30, 70, 10, 14, {RN, RN, RN, RN}},
    {"METAL SNARE", ENG_SD, P23_SD_WIRE, RO_S, 12, {60, 0, 110, 90, 115, 0}, 0, 34, 100, 0, 16, {R_(P23_PIN_NOISE, 0, 0, P23_RD_DRIVE, 30, 3), RN, RN, RN}},
    {"FED WIRE", ENG_SD, P23_SD_WIRE, RO_N, 0, {0, 0, 70, 70, 100, 0}, 0, 42, 70, 20, 18, {R_(S_T1O, S_T2O, 0, P23_RD_EXT, 60, 3), RN, RN, RN}},
    {"SCORCHED 808", ENG_SD, P23_SD_TONE, RO_S, 0, {0, 0, 60, 70, 30, 0}, 0, 34, 100, 0, 14, {RN, RN, RN, RN}},
    {"DEEP SNARE", ENG_SD, P23_SD_TONE, RO_S, -5, {0, 0, 40, 50, 20, 0}, 0, 44, 90, 0, 14, {RN, RN, RN, RN}},
    {"ELECTRO SNR", ENG_SD, P23_SD_TONE, RO_S, 5, {0, 0, 50, 80, 50, 0}, 0, 30, 100, 0, 16, {R_(S_ENV, 0, 0, P23_RD_MOD2, 50, 3), RN, RN, RN}},
    {"TOM SNARE", ENG_SD, P23_SD_TONE, RO_P, -3, {0, 0, 15, 40, 20, 0}, 0, 40, 80, 10, 14, {RN, RN, RN, RN}},
    {"STEP SNARE", ENG_SD, P23_SD_TONE, RO_S, 2, {0, 0, 55, 70, 50, 0}, 0, 32, 80, 0, 16, {R_(P23_PIN_SH_2DIR, P23_PIN_SH_1SH, 0, P23_RD_MOD2, 60, 3), RN, RN, RN}},
    {"SHARD HAT", ENG_HHT, 0, RO_H, 16, {60, 110, 20, 0, 0, 0}, 0, 8, 60, 0, 12, {RN, RN, RN, RN}},
    {"HISS GATE", ENG_HHT, 0, RO_H, 16, {110, 95, 0, 0, 0, 0}, 0, 46, 50, 10, 12, {RN, RN, RN, RN}},
    {"SMASH", ENG_HHT, 0, RO_H, 24, {95, 89, 89, 0, 0, 0}, 0, 64, 70, 30, 12, {RN, RN, RN, RN}},
    {"DROPLETS", ENG_HHT, 0, RO_P, 8, {20, 70, 118, 0, 0, 0}, 0, 20, 30, 40, 16, {R_(P23_PIN_LFO_TRI, P23_PIN_SH_2DIR, 0, P23_RD_MOD2, 70, 3), RN, RN, RN}},
    {"TICKER", ENG_HHT, 0, RO_H, 16, {30, 100, 60, 0, 0, 0}, 0, 10, 40, 10, 14, {R_(P23_PIN_SH_1SH, P23_PIN_SH_1DIR, 0, P23_RD_MOD2, 60, 3), RN, RN, RN}},
    {"METAL PING", ENG_HHT, 0, RO_P, 4, {30, 85, 125, 0, 0, 0}, 0, 24, 50, 20, 14, {RN, RN, RN, RN}},
    {"RATTLE", ENG_HHT, 0, RO_P, -8, {44, 64, 102, 0, 0, 0}, 4, 46, 60, 10, 14, {RN, RN, RN, RN}},
    {"CRUNCH", ENG_HHT, 0, RO_N, -20, {70, 50, 20, 0, 0, 0}, 0, 30, 110, 0, 16, {RN, RN, RN, RN}},
    {"OPEN CLOSE", ENG_HHT, 0, RO_H, 16, {60, 90, 30, 0, 0, 0}, 0, 38, 50, 0, 14, {R_(P23_PIN_DIV1, P23_PIN_DIV2, 0, P23_RD_MOD2, 50, 3), RN, RN, RN}},
    {"BITCRUSH HAT", ENG_HHT, 0, RO_H, -24, {0, 80, 10, 0, 0, 0}, 0, 12, 90, 0, 14, {RN, RN, RN, RN}},
    {"STEP HATS", ENG_HHT, 0, RO_H, 12, {40, 100, 40, 0, 0, 0}, 0, 14, 60, 0, 16, {R_(P23_PIN_SH_3SH, P23_PIN_SH_2DIR, 0, P23_RD_MOD2, 60, 3), R_(P23_PIN_SH_1SH, 0, 0, P23_RD_DRIVE, 40, 8), RN, RN}},
    {"FM HAT", ENG_HHT, 0, RO_H, 8, {20, 90, 50, 0, 0, 0}, 0, 18, 60, 0, 18, {R_(S_T2O, S_T1O, S_T3O, P23_RD_MOD1, 80, 3), RN, RN, RN}},
    {"SELF HAT", ENG_HHT, 0, RO_H, 14, {30, 80, 70, 0, 0, 0}, 0, 28, 50, 10, 16, {R_(S_ENV, 0, 0, P23_RD_MOD2, 50, 3), RN, RN, RN}},
    {"STATIC", ENG_HHT, P23_HHT_STATIC, RO_H, 0, {60, 90, 60, 0, 0, 0}, 0, 30, 60, 0, 16, {RN, RN, RN, RN}},
    {"GEIGER", ENG_HHT, P23_HHT_STATIC, RO_P, 0, {20, 70, 110, 0, 0, 0}, 0, 20, 40, 10, 16, {RN, RN, RN, RN}},
    {"ARC", ENG_HHT, P23_HHT_STATIC, RO_N, 0, {110, 60, 40, 0, 0, 0}, 0, 40, 80, 10, 16, {RN, RN, RN, RN}},
    {"CONTACT MIC", ENG_HHT, P23_HHT_STATIC, RO_N, 0, {80, 40, 90, 0, 0, 0}, 0, 34, 100, 0, 16, {RN, RN, RN, RN}},
    {"SPARK HAT", ENG_HHT, P23_HHT_STATIC, RO_H, 0, {40, 110, 80, 0, 0, 0}, 0, 10, 60, 0, 16, {RN, RN, RN, RN}},
    {"CRACKLE", ENG_HHT, P23_HHT_STATIC, RO_N, 0, {127, 80, 30, 0, 0, 0}, 0, 48, 70, 10, 16, {RN, RN, RN, RN}},
    {"STEP STATIC", ENG_HHT, P23_HHT_STATIC, RO_H, 0, {70, 95, 70, 0, 0, 0}, 0, 24, 60, 0, 18, {R_(P23_PIN_SH_3SH, P23_PIN_SH_2DIR, 0, P23_RD_MOD2, 60, 3), R_(P23_PIN_DIV8, P23_PIN_DIV16, 0, P23_RD_VCA, -50, 8), RN, RN}},
    {"TRASH CYMBAL", ENG_HHT, P23_HHT_METAL, RO_H, 0, {0, 88, 10, 0, 0, 0}, 0, 62, 70, 10, 12, {RN, RN, RN, RN}},
    {"METAL HAT", ENG_HHT, P23_HHT_METAL, RO_H, 0, {20, 96, 20, 0, 0, 0}, 0, 8, 40, 0, 12, {RN, RN, RN, RN}},
    {"OPEN METAL", ENG_HHT, P23_HHT_METAL, RO_H, 0, {30, 90, 30, 0, 0, 0}, 0, 50, 40, 10, 12, {RN, RN, RN, RN}},
    {"IRON RIDE", ENG_HHT, P23_HHT_METAL, RO_H, -5, {60, 90, 60, 0, 0, 0}, 0, 60, 50, 20, 14, {RN, RN, RN, RN}},
    {"CLANG", ENG_HHT, P23_HHT_METAL, RO_N, -12, {127, 50, 80, 0, 0, 0}, 0, 40, 90, 10, 16, {RN, RN, RN, RN}},
    {"BELL TREE", ENG_HHT, P23_HHT_METAL, RO_P, 12, {90, 110, 110, 0, 0, 0}, 0, 44, 30, 40, 16, {R_(P23_PIN_LFO_TRI, P23_PIN_SH_2DIR, 0, P23_RD_MOD2, 40, 3), RN, RN, RN}},
    {"SCRAP METAL", ENG_HHT, P23_HHT_METAL, RO_N, -7, {127, 70, 100, 0, 0, 0}, 0, 36, 100, 10, 16, {RN, RN, RN, RN}},
    {"CHAIN", ENG_HHT, P23_HHT_METAL, RO_P, 5, {90, 100, 60, 0, 0, 0}, 0, 24, 70, 0, 16, {R_(P23_PIN_SH_3SH, P23_PIN_SH_1SH, 0, P23_RD_MOD2, 60, 3), RN, RN, RN}},
    {"GRIT", ENG_HHT, P23_HHT_SHAKE, RO_N, 0, {127, 90, 60, 0, 0, 0}, 6, 40, 60, 10, 16, {RN, RN, RN, RN}},
    {"GRAIN HAT", ENG_HHT, P23_HHT_SHAKE, RO_H, 0, {20, 100, 80, 0, 0, 0}, 0, 10, 50, 0, 16, {R_(P23_PIN_DIV2, P23_PIN_DIV4, 0, P23_RD_MOD2, 40, 3), RN, RN, RN}},
    {"SHAKE PUMP", ENG_HHT, P23_HHT_SHAKE, RO_H, 0, {90, 105, 30, 0, 0, 0}, 0, 24, 60, 0, 16, {R_(P23_PIN_DIV8, P23_PIN_DIV16, 0, P23_RD_VCA, -60, 3), RN, RN, RN}},
    {"SWARM", ENG_HHT, P23_HHT_SHAKE, RO_N, 0, {127, 80, 90, 0, 0, 0}, 0, 46, 80, 10, 16, {RN, RN, RN, RN}},
    {"GRAVEL HAT", ENG_HHT, P23_HHT_SHAKE, RO_H, 0, {100, 110, 40, 0, 0, 0}, 0, 16, 70, 0, 16, {RN, RN, RN, RN}},
};
#undef R_
#undef RN
#define NRECIPE (sizeof RECIPES / sizeof RECIPES[0])
static const uint8_t FAM_VOL[] = {   /* (each family's VOL, measured: its hits levelled, hats about 6 dB under) */
    88, 101, 84, 115, 93, 102, 101, 95, 94, 94, 93, 98, 95, 89, 90, 109,
    127, 122, 91, 96, 106, 87, 83, 107, 110, 117, 127, 96, 117, 92, 85, 85,
    106, 99, 95, 101, 109, 93, 94, 98, 115, 95, 83, 100, 106, 95, 111, 83,
    127, 97, 103, 94, 96, 83, 91, 98, 87, 87, 95, 104, 86, 84, 79, 127,
    79, 77, 89, 88, 101, 106, 93, 120, 88, 82, 102, 87, 103, 111, 83, 126,
    92, 96, 102, 127, 110, 127, 101, 112, 114, 117, 95, 104, 98, 109, 92, 127,
    98, 127, 90, 127, 94, 127, 110, 113, 127, 101, 127, 115, 100, 106, 90, 114,
    95, 112, 127, 89, 72, 127, 127, 127, 101, 124, 97, 127, 127, 114, 108, 106,
    127, 127, 127, 127, 120, 127, 76, 127, 97, 83, 112, 127, 120, 127, 127, 127,
    127, 121, 127};
_Static_assert(sizeof FAM_VOL == NRECIPE, "a VOL for every family");
/* the circuits' pools for the extra route a drawing may patch: sources, and where each may go */
static const uint8_t POOL_SRC[] = {P23_PIN_NOISE, P23_PIN_LFO_TRI, P23_PIN_LFO_SQR, P23_PIN_SH_3SH, P23_PIN_SH_2DIR,
    P23_PIN_SH_1SH, P23_PIN_DIV16, P23_PIN_DIV8, P23_PIN_DIV4, P23_PIN_DIV2, S_OUT, S_ENV, S_T1E, S_T2O, S_T3O,
    P23_PIN_FX_DLY_OUT};
static const uint8_t POOL_DST[4][5] = {
    {P23_RD_MOD2, P23_RD_MOD3, P23_RD_MOD4, P23_RD_MOD5, P23_RD_DRIVE},   /* BASS: SHAPE, WARP, PM, LPF, DRIVE */
    {P23_RD_MOD1, P23_RD_MOD2, P23_RD_MOD3, P23_RD_DRIVE, P23_RD_MOD2},   /* BD: MOD, WTF?, OMG!, DRIVE */
    {P23_RD_MOD1, P23_RD_MOD2, P23_RD_DRIVE, P23_RD_MOD2, P23_RD_EXT},    /* SD: TUNE, BPF, DRIVE, EXT */
    {P23_RD_MOD1, P23_RD_MOD2, P23_RD_DRIVE, P23_RD_MOD2, P23_RD_MOD1},   /* HHT: TUNE, HPF, DRIVE */
};
/* The engine profiles: each a circuit and one of its modes, chosen as one on the track's view (PRESETS). The four
 * Pulsar voices are BD, SD, HHT and BASS; the others go past the instrument. A track keeps one sound for each
 * circuit; stepping onto a profile whose mode that sound has not draws a new one of the profile's families. */
static void gen_sound(sound_t *s, uint32_t m, int32_t fam, int32_t avoid, uint32_t seed);
typedef struct { uint8_t mach, mode; const char *name; } prof_t;
static const prof_t PROFILES[] = {
    {ENG_BD, P23_BD_TRI, "BD"}, {ENG_BD, P23_BD_HAMMER, "HAMMER"}, {ENG_BD, P23_BD_SKIN, "SKIN"},
    {ENG_SD, P23_SD_NOISE, "SD"}, {ENG_SD, P23_SD_WALL, "WALL"}, {ENG_SD, P23_SD_WIRE, "WIRE"}, {ENG_SD, P23_SD_TONE, "TONE"},
    {ENG_HHT, P23_HHT_NOISE, "HHT"}, {ENG_HHT, P23_HHT_STATIC, "STATIC"}, {ENG_HHT, P23_HHT_METAL, "METAL"},
    {ENG_HHT, P23_HHT_SHAKE, "SHAKE"},
    {ENG_BASS, P23_BASS_GRIND, "GRIND"}, {ENG_BASS, P23_BASS_DCO, "BASS"}, {ENG_BASS, P23_BASS_FM, "FM"},
    {ENG_BASS, P23_BASS_PRC, "PRC"}, {ENG_BASS, P23_BASS_PLUCK, "PLUCK"},
};
#define NPROF (sizeof PROFILES / sizeof PROFILES[0])
static uint32_t snd_mode(const sound_t *s) { return (uint32_t)clampi(s->syn[P_BMODE], 0, 7); }
static uint32_t prof_of(const sound_t *s)        /* (a sound's profile: its circuit and mode) */
{
    uint32_t i;
    for (i = 0; i < NPROF; i++)
        if (PROFILES[i].mach == s->mach && PROFILES[i].mode == snd_mode(s))
            return i;
    for (i = 0; i < NPROF && PROFILES[i].mach != s->mach; i++)
        ;
    return i < NPROF ? i : 0u;
}
static const char *prof_name(const sound_t *s) { return PROFILES[prof_of(s)].name; }
/* a new sound of profile p: one of its families (not avoid, while there are others) */
static void gen_profile(sound_t *s, uint32_t p, int32_t avoid, uint32_t seed)
{
    rnd_t r;
    uint32_t k, n = 0, choice[NRECIPE];
    const prof_t *pf = &PROFILES[p % NPROF];
    rnd_seed(&r, seed ^ 0x9E3779B9u);
    for (k = 0; k < NRECIPE; k++)
        if (RECIPES[k].mach == pf->mach && RECIPES[k].mode == pf->mode && (int32_t)k != avoid)
            choice[n++] = k;
    if (!n)
        for (k = 0; k < NRECIPE; k++)
            if (RECIPES[k].mach == pf->mach && RECIPES[k].mode == pf->mode)
                choice[n++] = k;
    gen_sound(s, pf->mach, n ? (int32_t)choice[rnd_below(&r, (int32_t)n)] : -1, -1, seed);
}
/* a family by its name (the start's kit names its pieces; -1: none) */
static int32_t fam_find(const char *name)
{
    uint32_t i, j;
    for (i = 0; i < NRECIPE; i++) {
        for (j = 0; name[j] && RECIPES[i].name[j] == name[j]; j++)
            ;
        if (!name[j] && !RECIPES[i].name[j])
            return (int32_t)i;
    }
    return -1;
}
/* routes that would move a pitch: SHAOS and the clock dividers stay off them (steps in pitch are not wanted) */
static int pitch_dst(uint32_t m, uint32_t dst)
{
    return m == ENG_BASS ? dst == P23_RD_MOD1 || dst == P23_RD_MOD4
         : m == ENG_BD ? dst == P23_RD_MOD1 || dst == P23_RD_MOD2 || dst == P23_RD_MOD3 : dst == P23_RD_MOD1;
}
static int stepped_src(uint32_t src)
{ return (src >= P23_PIN_SH_MOD && src <= P23_PIN_SH_3SH) || (src >= P23_PIN_DIV16 && src <= P23_PIN_DIV025) || src == P23_PIN_CLK; }
static int32_t g_spread(rnd_t *r, int32_t v, int32_t w) { return clampi(v + rnd_below(r, 2 * w + 1) - w, 0, 127); }
static void gen_sound(sound_t *s, uint32_t m, int32_t fam, int32_t avoid, uint32_t seed);
/* (as Tonitor) a track keeps the sound of the engine it plays; the others only as the seed that will draw them,
 * drawn when the track switches to that engine */
#define FAM_LATER 0xFFu
static void snd_later(sound_t *s, uint32_t m, uint32_t seed)
{ memset(s, 0, sizeof *s); s->mach = (uint8_t)m; s->family = FAM_LATER; s->seed = seed; }
static void snd_draw(sound_t *s) { if (s->family == FAM_LATER) gen_sound(s, s->mach, -1, -1, s->seed); }
static void gen_sound(sound_t *s, uint32_t m, int32_t fam, int32_t avoid, uint32_t seed)
{
    rnd_t r;
    uint32_t k, n = 0, pick, choices[NRECIPE], used = 0;
    const recipe_t *rc;
    int32_t w;
    m %= NENG;
    rnd_seed(&r, seed);
    if (fam < 0 || (uint32_t)fam >= NRECIPE || RECIPES[fam].mach != m) {
        for (k = 0; k < NRECIPE; k++)
            if (RECIPES[k].mach == m && (int32_t)k != avoid) choices[n++] = k;
        pick = (uint32_t)rnd_below(&r, (int32_t)n);
        fam = (int32_t)choices[pick];
    }
    rc = &RECIPES[fam];
    w = rc->spread < 18 ? 18 : rc->spread;
    memset(s, 0, sizeof *s);
    s->mach = (uint8_t)m; s->family = (uint8_t)fam; s->seed = seed;
    lfos_off(s);
    s->fmach = FLT_MM; /* reserved frontend record field */
    s->amp[A_PAN] = 64; s->amp[A_VOL] = FAM_VOL[fam]; s->amp[A_RSET] = 1;
    /* the pitch offset: drums wander a little (SD and HHT more: there it is timbre), BASS keeps the note */
    s->syn[0] = (int16_t)clampi(64 + 2 * rc->off + (m == ENG_BASS ? 0 : rnd_below(&r, m == ENG_BD ? 5 : 13) - (m == ENG_BD ? 2 : 6)), 0, 127);
    for (k = 0; k < 6u; k++) {
        int32_t v = rc->k[k];
        if (!(m == ENG_BD && k + 1u == P23_BD_STAB))
            v = g_spread(&r, v, w);
        s->syn[1u + k] = (int16_t)v;
    }
    s->syn[P_BMODE] = (int16_t)rc->mode;           /* the circuit's mode */
    s->syn[P_ATT] = (int16_t)(rc->att ? g_spread(&r, rc->att, w / 2) : rnd_below(&r, 4));
    s->syn[P_REL] = (int16_t)g_spread(&r, rc->rel, w / 2);
    s->syn[P_DRIVE] = (int16_t)(rc->drive ? g_spread(&r, rc->drive, 30) : rnd_below(&r, 100) < 25 ? 20 + rnd_below(&r, 60) : 0);
    s->fx[X_DEL] = (uint8_t)clampi(rc->fx + rnd_below(&r, 21) - 10 + (rnd_below(&r, 100) < 20 ? 40 : 0), 0, 127);
    if (rnd_below(&r, 100) < 50) {               /* (about one drawing in two wrecked: one or two of FOLD, RING, CRUSH, BITS) */
        uint32_t n = 1u + (uint32_t)rnd_below(&r, 2), done = 0;
        while (n--) {
            uint32_t kind = (uint32_t)rnd_below(&r, 4);
            if (done & (1u << kind))
                continue;
            done |= 1u << kind;
            if (kind == 0) s->syn[P_WRECK] = (int16_t)(30 + rnd_below(&r, 81));
            else if (kind == 1) { s->syn[P_WRECK + 1u] = (int16_t)(40 + rnd_below(&r, 88)); s->syn[P_WRECK + 2u] = (int16_t)(20 + rnd_below(&r, 100)); }
            else if (kind == 2) s->syn[P_WRECK + 3u] = (int16_t)(16 + rnd_below(&r, 95));
            else s->syn[P_WRECK + 4u] = (int16_t)(40 + rnd_below(&r, 71));
        }
    }
    for (k = 0; k < 8u; k++)
        s->syn[P_MACRO + k] = 64;                /* (the macros: the recipe at their middle) */
    for (k = 0; k < 4u; k++) {
        const rr_t *rr = &rc->r[k];
        uint32_t c = 0, src;
        if (!rr->src[0])
            continue;
        while (c < 3u && rr->src[c])
            c++;
        src = rr->src[rnd_below(&r, (int32_t)c)];
        rt_set(s, used++, src, rr->dst, rr->depth * (70 + rnd_below(&r, 61)) / 100, rr->mac);
    }
    if (used < 4u && rnd_below(&r, 100) < 50) {  /* (a further patch, from the circuit's pool, on M8) */
        uint32_t src = POOL_SRC[rnd_below(&r, (int32_t)(sizeof POOL_SRC))], dst = POOL_DST[m][rnd_below(&r, 5)];
        int32_t depth = 25 + rnd_below(&r, 60);
        if (stepped_src(src) && pitch_dst(m, dst))
            dst = rnd_below(&r, 2) ? P23_RD_DRIVE : P23_RD_VCA;   /* (SHAOS, a divider: on the drive or the level) */
        if (dst == P23_RD_DRIVE || dst == P23_RD_EXT)
            depth /= 2;
        rt_set(s, used, src, dst, rnd_below(&r, 4) ? depth : -depth, 8);
    }
}
static const char *family_name(const sound_t *s) { return s->family < NRECIPE ? RECIPES[s->family].name : ""; }
static int lfo_rnd_dest_ok(const sound_t *s, uint32_t d)
{
    par_t p;
    return lfo_par(s, d, &p) && (p.kind == PK_NUM || p.kind == PK_BIP || p.kind == PK_HOLD);
}
static void lfo_random(sound_t *s, uint32_t seed)
{
    gctx_t g;
    uint8_t cand[LFO_D_LFO];
    uint32_t n, on, nc = 0, d, i;
    rnd_seed(&g.r, seed);
    for (d = 1; d < LFO_D_LFO; d++)
        if (lfo_rnd_dest_ok(s, d))
            cand[nc++] = (uint8_t)d;
    lfos_off(s);
    on = CH(&g, 55) ? 2u : 3u;
    for (n = 0; n < on && nc; n++) {
        lfo_t *l = &s->lfo[n];
        int32_t sp = RI(&g, 4, 44), dp;
        for (i = 0; i < 8u; i++) {               /* (a destination not taken yet) */
            uint32_t j;
            d = n && CH(&g, 22) ? LFO_D_LFO + (uint32_t)RI(&g, 0, 2 * (int32_t)n - 1)   /* (an earlier LFO's) */
                : CH(&g, 25) ? 1u + PG_MACRO * 8u : cand[RI(&g, 0, (int32_t)nc - 1)];
            if (d < LFO_D_LFO && !lfo_rnd_dest_ok(s, d))
                continue;
            for (j = 0; j < n && s->lfo[j].dest != d; j++)
                ;
            if (j == n)
                break;
        }
        l->dest = (uint8_t)d;
        l->wave = (uint8_t)RI(&g, 0, NLW - 1);
        l->spd = (uint8_t)(64 + (CH(&g, 85) ? sp : -sp));
        l->mult = (uint8_t)(CH(&g, 85) ? RI(&g, 0, 7) : 12 + RI(&g, 0, 7));   /* (with the tempo, mostly) */
        l->mode = (uint8_t)(CH(&g, 55) ? LM_FREE : RI(&g, LM_TRIG, LM_HALF));
        l->fade = (uint8_t)(CH(&g, 65) ? 64 : 64 + (CH(&g, 50) ? -1 : 1) * RI(&g, 6, 40));
        l->sph = (uint8_t)RI(&g, 0, 127);
        dp = RI(&g, 10, d >= LFO_D_LFO ? 40 : 56);
        l->dep = (uint8_t)(64 + (CH(&g, 50) ? dp : -dp));
    }
}

