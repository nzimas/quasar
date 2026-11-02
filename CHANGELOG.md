# Changelog

Builds are numbered; the package identity is `FM-1_7` and the build in two digits (build 3: `FM-1_703`).

## Build 3 -- November 2026

The first public build. The Pulsar-23 recreated as a whole instrument, then pushed toward experimental IDM.

- **Engine.** Every module of the Pulsar-23 modelled on its 0–10 V scale: clock and divider array, four
  looper-recorders, envelopes, BD, BASS, SD, HHT, SHAOS, LFO, the FX processor, the parallel distortion, VCAs,
  inverters, switches, attenuators, pulse converters, MIDI-CV and touch CV, noise, a diode and two capacitors, and a
  virtual patchbay of 32 cables. Fixed-point throughout, rendered on both of the FM-1's cores.
- **16 engine profiles.** The four circuits in 16 modes: BD, HAMMER, SKIN; SD, WALL, WIRE, TONE; HHT, STATIC, METAL,
  SHAKE; GRIND, BASS, FM, PRC, PLUCK. Pitch from the note played (no TUNE).
- **Sounds.** 147 families drawn at random, every one percussive and hard; four macros on the main knobs; four
  internal routes per sound (SHAOS, the LFO, the dividers, noise, FX outputs, any track's output or envelope); a
  per-voice drive; WRECK (fold, ring, crush, bits) after every voice.
- **STUTTER** on the 6th black key, apart from the FX: SHIFT and the key held repeat the slice just heard; its page
  has SLICE, RATE, MODE, DECAY (each with RND), DRIFT, ROLL and MIX. The key is lit dimly.
- **BREAK** has ten ways (REPEAT, HALF TIME, DOUBLE, REVERSE, CHOP, ROLL, SHUFFLE, POLY, JUMP, SKIP) and layers drawn
  at random: ratchets up to 8, octave leaps, flams, accents, RISE, WRECK, STUT, GHOST. Nothing drops out.
- **Phrases**: polymeters, tracks on their own clock and direction, crossed Euclidean layers, ratchets, hits late or
  graced, walking velocities; their locks reach WRECK.
- **PERFORMANCE** on the 7th black key: the state kept, everything changed put back as it goes off; the main knobs
  move every track with notes (CTRL ALL). Each session's CTRL sets KNOBS or ALL (phrases, locks, directions, lengths,
  clocks, transposition, new sounds and EDIT pages on every track). A pattern saved while it is on keeps what plays,
  and the sounds of that moment stay in the session.
- **Sessions** on a grid like the patterns': a white key loads its session, held it saves there.
- The pages Quasar cannot use without cables (CLOCK, LOOPER, ATTEN, CV, PATCH) are hidden; the clock follows the
  session tempo.
- The output is written at full scale into a soft clip: Quasar is loud.

## Builds 1 and 2 -- October 2026

Development builds, not published. Build 1 joined Pulsar-23 circuits to the Bigorna engine and was withdrawn;
build 2 replaced it with an independent engine (per-track circuits, four fixed modulation cables), which build 3
replaced in turn.
