# Changelog

Builds are numbered; package identity is `FM-1_7` and build in 2 digits (build 3: `FM-1_703`).

## Build 3 -- November 2026

1st public build. Pulsar-23 recreated as whole instrument, then pushed toward experimental IDM.

- **Engine.** Every module of Pulsar-23 modelled on its 0–10 V scale: clock and divider array, 4
  looper-recorders, envelopes, BD, BASS, SD, HHT, SHAOS, LFO, FX processor, parallel distortion, VCAs,
  inverters, switches, attenuators, pulse converters, MIDI-CV and touch CV, noise, diode and 2 capacitors, and
  virtual patchbay of 32 cables. Fixed-point throughout, rendered on both of FM-1's cores.
- **16 engine profiles.** 4 circuits in 16 modes: BD, HAMMER, SKIN; SD, WALL, WIRE, TONE; HHT, STATIC, METAL,
  SHAKE; GRIND, BASS, FM, PRC, PLUCK. Pitch from note played (no TUNE).
- **Sounds.** 147 families drawn at random, all percussive; 4 macros on main knobs; 4
  internal routes per sound (SHAOS, LFO, dividers, noise, FX outputs, any track's output or envelope);
  per-voice drive; WRECK (fold, ring, crush, bits) after every voice.
- **TRANSFORM** on 6th black key: SHIFT and key held put random stack of DRIVE, FOLD, RING, CRUSH and BITS
  over 2 to 6 tracks, without touching steps. Its page has TRACKS, STACK, DEPTH and MIX (at first ALL, 2, 60,
  100), each with RND first. Key is lit dimly. (It replaces STUTTER of
  first build 3 package.)
- **BREAK** has 10 ways (REPEAT, HALF TIME, DOUBLE, REVERSE, CHOP, ROLL, SHUFFLE, POLY, JUMP, SKIP) and layers drawn
  at random: ratchets up to 8, octave leaps, flams, accents, RISE, WRECK, XFM (TRANSFORM for a beat or so), GHOST. Nothing drops out.
- **Phrases**: polymeters, tracks on their own clock and direction, crossed Euclidean layers, ratchets, hits late or
  graced, walking velocities; their locks reach WRECK.
- **PERFORMANCE** on 7th black key: state kept, everything changed put back as it goes off; main knobs
  move every track with notes (CTRL ALL). Each session's CTRL sets KNOBS or ALL (phrases, locks, directions, lengths,
  clocks, transposition, new sounds and EDIT pages on every track). Pattern saved while it is on keeps what plays,
  and sounds of that moment stay in session.
- **Sessions** on grid like patterns': white key loads its session, held it saves there.
- **Patterns**: saved into another slot, that slot plays from then on, and pattern saved from keeps what it held,
  its mutes included.
- Pages Quasar cannot use without cables (CLOCK, LOOPER, ATTEN, CV, PATCH) are hidden; clock follows
  session tempo.
- Output is written at full scale into soft clip: Quasar is loud.
- Pages come up faster: screen is drawn into two buffers in turn, one drawn while other is sent.
  Engine divides by shifts where FM-1's compiler kept slow divide instructions; sound is same, bit for bit.

## Builds 1 and 2 -- October 2026

Development builds, not published. Build 1 joined Pulsar-23 circuits to Bigorna engine and was withdrawn;
build 2 replaced it with independent engine (per-track circuits, 4 fixed modulation cables), which build 3
replaced in turn.
