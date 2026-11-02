# Changelog

Builds are numbered; the package identity is `FM-1_7` and the build in 2 digits (build 3: `FM-1_703`).

## Build 3 -- November 2026

The 1st public build. The Pulsar-23 recreated as a whole instrument, then pushed toward experimental IDM.

- **Engine.** Every module of the Pulsar-23 modelled on its 0–10 V scale: clock and divider array, 4
  looper-recorders, envelopes, BD, BASS, SD, HHT, SHAOS, LFO, the FX processor, the parallel distortion, VCAs,
  inverters, switches, attenuators, pulse converters, MIDI-CV and touch CV, noise, a diode and 2 capacitors, and a
  virtual patchbay of 32 cables. Fixed-point throughout, rendered on both of the FM-1's cores.
- **16 engine profiles.** The 4 circuits in 16 modes: BD, HAMMER, SKIN; SD, WALL, WIRE, TONE; HHT, STATIC, METAL,
  SHAKE; GRIND, BASS, FM, PRC, PLUCK. Pitch from the note played (no TUNE).
- **Sounds.** 147 families drawn at random, all percussive; 4 macros on the main knobs; 4
  internal routes per sound (SHAOS, the LFO, the dividers, noise, FX outputs, any track's output or envelope); a
  per-voice drive; WRECK (fold, ring, crush, bits) after every voice.
- **TRANSFORM** on the 6th black key: SHIFT and the key held put a random stack of DRIVE, FOLD, RING, CRUSH and BITS
  over 2 to 6 tracks, without touching the steps. Its page has TRACKS, STACK, DEPTH and MIX (at first ALL, 2, 60,
  100), each with RND first. The key is lit dimly. (It replaces the STUTTER of
  the first build 3 package.)
- **BREAK** has 10 ways (REPEAT, HALF TIME, DOUBLE, REVERSE, CHOP, ROLL, SHUFFLE, POLY, JUMP, SKIP) and layers drawn
  at random: ratchets up to 8, octave leaps, flams, accents, RISE, WRECK, XFM (TRANSFORM for a beat or so), GHOST. Nothing drops out.
- **Phrases**: polymeters, tracks on their own clock and direction, crossed Euclidean layers, ratchets, hits late or
  graced, walking velocities; their locks reach WRECK.
- **PERFORMANCE** on the 7th black key: the state kept, everything changed put back as it goes off; the main knobs
  move every track with notes (CTRL ALL). Each session's CTRL sets KNOBS or ALL (phrases, locks, directions, lengths,
  clocks, transposition, new sounds and EDIT pages on every track). A pattern saved while it is on keeps what plays,
  and the sounds of that moment stay in the session.
- **Sessions** on a grid like the patterns': a white key loads its session, held it saves there.
- **Patterns**: saved into another slot, that slot plays from then on, and the pattern saved from keeps what it held,
  its mutes included.
- The pages Quasar cannot use without cables (CLOCK, LOOPER, ATTEN, CV, PATCH) are hidden; the clock follows the
  session tempo.
- The output is written at full scale into a soft clip: Quasar is loud.
- Pages come up faster: the screen is drawn into two buffers in turn, one drawn while the other is sent. The
  engine divides by shifts where the FM-1's compiler kept slow divide instructions; the sound is the same, bit for bit.

## Builds 1 and 2 -- October 2026

Development builds, not published. Build 1 joined Pulsar-23 circuits to the Bigorna engine and was withdrawn;
build 2 replaced it with an independent engine (per-track circuits, 4 fixed modulation cables), which build 3
replaced in turn.
