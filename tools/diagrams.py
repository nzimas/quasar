#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# QUASAR: the lineage page's block diagrams -- the circuits as this firmware models them (firmware/engine/p23_engine.c,
# docs/ENGINE.md), drawn from the published descriptions of the Pulsar-23. They are not SOMA's schematics, which are not
# public and were not used. Styled by docs/css/site.css (.sch); tools/make_site.py puts them in docs/lineage.html.
import html


def _t(x, y, text, cls="t"):
    return f'<text class="{cls}" x="{x:g}" y="{y:g}">{html.escape(text)}</text>'


def blk(x, y, w, h, title, sub="", cls=""):
    out = [f'<rect class="blk {cls}" x="{x}" y="{y}" width="{w}" height="{h}"/>']
    if sub:
        lines = sub.split("|")                   # (a "|" breaks the line)
        top = y + h / 2 - 3 - 7 * (len(lines) - 1)
        out.append(_t(x + w / 2, top, title))
        for i, ln in enumerate(lines):
            out.append(_t(x + w / 2, top + 18 + i * 14, ln, "s"))
    else:
        out.append(_t(x + w / 2, y + h / 2 + 5, title))
    return "".join(out)


def wire(pts, cls="", label=None, lx=0, ly=-6):
    """a wire along pts, an arrowhead at its end"""
    d = " ".join(f"{x:g},{y:g}" for x, y in pts)
    (x0, y0), (x1, y1) = pts[-2], pts[-1]
    if x1 != x0:
        s = 1 if x1 > x0 else -1
        head = f"{x1:g},{y1:g} {x1 - 9 * s:g},{y1 - 5:g} {x1 - 9 * s:g},{y1 + 5:g}"
    else:
        s = 1 if y1 > y0 else -1
        head = f"{x1:g},{y1:g} {x1 - 5:g},{y1 - 9 * s:g} {x1 + 5:g},{y1 - 9 * s:g}"
    out = f'<polyline class="w {cls}" points="{d}"/><polygon class="ar {cls}" points="{head}"/>'
    if label:
        mx, my = pts[0]
        out += _t(mx + lx, my + ly, label, "lbl")
    return out


def svg(w, h, body, label):
    return (f'<svg class="sch" viewBox="0 0 {w} {h}" role="img" aria-label="{html.escape(label)}">'
            + "".join(body) + "</svg>")


def bd():
    b = [blk(20, 30, 170, 64, "TRIG", "envelope gate", "red"),
         blk(250, 30, 210, 64, "PITCH MODULATOR", "charged each strike|depth ~4 oct, 3-120 ms"),
         blk(250, 150, 210, 64, "TRIANGLE CORE", "integrator, comparator|pitch: the note"),
         blk(520, 150, 200, 64, "DRIVE", "gain 0.5-24, soft knee|tri > sine > square"),
         blk(520, 270, 200, 64, "VOICE DRIVE", "pre-emphasis, cubic|clip, de-emphasis", "new"),
         blk(250, 270, 210, 64, "WRECK", "fold, ring,|crush, bits", "new"),
         blk(20, 270, 170, 64, "VCA", "AR envelope,|percussive"),
         blk(20, 150, 170, 64, "MOD x AMT", "linear, 60 Hz/V"),
         blk(780, 30, 150, 64, "OMG! WTF?", "currents into|core, modulator", "ghost")]
    b += [wire([(190, 62), (250, 62)], "gate"),
          wire([(355, 94), (355, 150)], "cv"),
          wire([(190, 182), (250, 182)], "cv"),
          wire([(460, 182), (520, 182)]),
          wire([(620, 214), (620, 270)]),
          wire([(520, 302), (460, 302)]),
          wire([(250, 302), (190, 302)]),
          wire([(105, 334), (105, 372)], label="OUT", lx=10, ly=30),
          wire([(855, 94), (855, 125), (430, 125), (430, 150)], "cv"),
          wire([(780, 50), (460, 50)], "cv")]
    return svg(950, 385, b, "BD as modelled: the pitch modulator charged at each strike drives a triangle core; drive "
                            "shapes it toward sine and square; Quasar's voice drive and WRECK follow; then the VCA")


def bass():
    b = [blk(20, 40, 210, 64, "RECURSIVE DCO", "y = sin(phi + b * y')|SHAPE: sine > saw > sq."),
         blk(280, 40, 180, 64, "WARP", "asymmetric, loaded|saturator"),
         blk(510, 40, 190, 64, "LPF", "state-variable, band|stage saturating"),
         blk(750, 40, 180, 64, "DRIVE, WRECK", "per voice", "new"),
         blk(750, 170, 180, 64, "VCA", "AR, percussive"),
         blk(20, 170, 210, 64, "PRC", "impulse, noise into|three resonators", "ghost"),
         blk(280, 170, 180, 64, "MOD x AMT", "phase modulation"),
         blk(510, 170, 190, 64, "MORE MODES", "GRIND, FM, PLUCK", "new")]
    b += [wire([(230, 72), (280, 72)]), wire([(460, 72), (510, 72)]), wire([(700, 72), (750, 72)]),
          wire([(840, 104), (840, 170)]), wire([(840, 234), (840, 275)], label="OUT", lx=10, ly=36),
          wire([(230, 202), (255, 202), (255, 132), (605, 132), (605, 104)]),
          wire([(370, 170), (370, 150), (125, 150), (125, 104)], "cv")]
    return svg(950, 285, b, "BASS as modelled: a table-free recursive DCO (or, in PRC, three struck resonators) through "
                            "WARP and a saturating low-pass, then voice drive, WRECK and the VCA; GRIND, FM and PLUCK are "
                            "modes written for Quasar")


def sd():
    b = [blk(20, 30, 210, 64, "PINK NOISE"),
         blk(20, 150, 210, 64, "SPECTRAL NOISE", "an oscillator on the|note, swept by noise"),
         blk(290, 90, 150, 64, "MIX"),
         blk(500, 90, 190, 64, "BPF", "resonant, level held|across Q"),
         blk(500, 210, 190, 64, "CLAP", "1-4 bursts, 4-19 ms|ADD or velocity 113+", "red"),
         blk(750, 90, 180, 64, "DRIVE, WRECK", "VCA", "new")]
    b += [wire([(230, 62), (260, 62), (260, 110), (290, 110)]), wire([(230, 182), (260, 182), (260, 134), (290, 134)]),
          wire([(440, 122), (500, 122)]), wire([(595, 210), (595, 154)], "gate"), wire([(690, 122), (750, 122)]),
          wire([(840, 154), (840, 250)], label="OUT", lx=10, ly=92)]
    return svg(950, 285, b, "SD as modelled: pink noise and a pitched spectral noise mixed, a clap of bursts, a "
                            "resonant band-pass, then Quasar's drive, WRECK and the VCA")


def hht():
    b = [blk(20, 40, 170, 64, "WHITE NOISE"),
         blk(250, 40, 200, 64, "RESAMPLE", "held at a clock that|follows the note"),
         blk(510, 40, 190, 64, "WARP", "an expander, toward|sparse grains"),
         blk(760, 40, 170, 64, "HPF", "resonant, can ring|on its own"),
         blk(760, 160, 170, 64, "DRIVE, WRECK", "VCA", "new")]
    b += [wire([(190, 72), (250, 72)]), wire([(450, 72), (510, 72)]), wire([(700, 72), (760, 72)]),
          wire([(845, 104), (845, 160)]), wire([(760, 192), (660, 192)], label="OUT", lx=-140, ly=4)]
    return svg(950, 245, b, "HHT as modelled: white noise resampled at a note-following clock, WARP toward sparse grains, "
                            "a resonant high-pass, then Quasar's drive, WRECK and the VCA")


def chain():
    """the whole instrument as Quasar plays it"""
    b = [blk(20, 30, 190, 64, "STEPS, KEYS", "MIDI, BREAK, phrases", "red"),
         blk(260, 30, 200, 64, "6 TRACKS", "16 profiles on|BD, BASS, SD, HHT"),
         blk(510, 30, 180, 64, "LEVEL, PAN", "FX send post-level"),
         blk(740, 30, 190, 64, "FX PROCESSOR", "delay, hall; clock|slowed up to 7x"),
         blk(740, 160, 190, 64, "MIX", "dry and returns"),
         blk(510, 160, 180, 64, "STUTTER", "0.74 s of the mix", "new"),
         blk(260, 160, 200, 64, "DISTORTION", "parallel, asymmetric"),
         blk(20, 160, 190, 64, "SOFT CLIP", "DC blocking"),
         blk(260, 290, 200, 64, "MACROS, ROUTES", "4 of each a sound", "new"),
         blk(510, 290, 180, 64, "SHAOS, LFO", "dividers, noise,|tracks' OUT, ENV")]
    b += [wire([(210, 62), (260, 62)], "gate"), wire([(460, 62), (510, 62)]),
          wire([(690, 62), (740, 62)], label="SEND", lx=10, ly=-8),
          wire([(835, 94), (835, 160)]), wire([(600, 94), (600, 128), (790, 128), (790, 160)]),
          wire([(740, 192), (690, 192)]), wire([(510, 192), (460, 192)]), wire([(260, 192), (210, 192)]),
          wire([(115, 224), (115, 270)], label="OUT", lx=10, ly=36),
          wire([(510, 322), (460, 322)], "cv"),
          wire([(260, 322), (235, 322), (235, 128), (330, 128), (330, 94)], "cv")]
    return svg(950, 370, b, "Quasar's signal path: sequencer and keys strike six tracks; each goes through level and pan "
                            "into the mix, and after its level into the FX processor; the mix and returns pass the "
                            "stutter, the parallel distortion and the soft clip; SHAOS, the LFO, the dividers, noise and "
                            "the tracks reach the voices through each sound's macros and routes")


DIAGRAMS = {"chain": chain, "bd": bd, "bass": bass, "sd": sd, "hht": hht}
