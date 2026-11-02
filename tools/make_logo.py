#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# QUASAR: the logo. The name in capitals drawn here as straight-edged polygons (no font: an extended, machined face
# with chamfered corners), and the quasar hidden in the Q -- a white-hot core in its counter, the approaching jet
# (blueshifted) leaving as the Q's tail, the receding one (redshifted) a short spike the other way. Not a picture of
# space: a point of energy breaking out of the letter.
#   python3 tools/make_logo.py        writes docs/assets/mark.svg, logo-dark.svg, logo-light.svg, hero.svg
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "assets"

INK_DARK = "#f2f4f8"     # the letters on a dark ground
INK_LIGHT = "#0b0c10"    # .. on a light one
BLUE = "#19e3ff"         # the approaching jet (blueshift)
RED = "#ff3d1f"          # the receding jet (redshift)
CORE = "#ffffff"

H = 100.0                # the capitals' height
S = 21.0                 # stem
C = 17.0                 # outer chamfer
W = 96.0                 # a letter's width
GAP = 20.0               # between letters


def path(pts, dx=0.0):
    return "M" + " L".join(f"{x + dx:.2f},{y:.2f}" for x, y in pts) + " Z"


def octagon(x0, y0, x1, y1, c):
    return [(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c), (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)]


def letter_q(w):
    ci = C * 0.42
    outer = octagon(0, 0, w, H, C)
    inner = octagon(S, S, w - S, H - S, ci)[::-1]
    return [(outer, inner)]


def letter_u(w):
    return [([(0, 0), (S, 0), (S, H - S), (w - S, H - S), (w - S, 0), (w, 0), (w, H - C), (w - C, H), (C, H), (0, H - C)], None)]


def letter_a(w):
    b = 50.0
    c2 = C * 0.45
    outer = [(0, H), (0, C), (C, 0), (w - C, 0), (w, C), (w, H), (w - S, H), (w - S, b + S), (S, b + S), (S, H)]
    inner = [(S, S + c2), (S + c2, S), (w - S - c2, S), (w - S, S + c2), (w - S, b), (S, b)][::-1]
    return [(outer, inner)]


def letter_s(w):
    m = (H - S) / 2
    return [([(C, 0), (w, 0), (w, S), (S, S), (S, m), (w - C, m), (w, m + C), (w, H - C), (w - C, H), (0, H), (0, H - S),
              (w - S, H - S), (w - S, m + S), (C, m + S), (0, m + S - C), (0, C)], None)]


def letter_r(w):
    b = 46.0
    c2 = C * 0.45
    outer = [(0, 0), (w - C, 0), (w, C), (w, b + S - C), (w - C, b + S), (S, b + S), (S, H), (0, H)]
    inner = [(S, S), (w - S - c2, S), (w - S, S + c2), (w - S, b), (S, b)][::-1]
    leg = [(w * 0.50, b + S), (w * 0.50 + S * 1.12, b + S), (w, H), (w - S * 1.12, H)]
    return [(outer, inner), (leg, None)]


LETTERS = [("Q", letter_q, 100.0), ("U", letter_u, W), ("A", letter_a, W), ("S", letter_s, W), ("A", letter_a, W),
           ("R", letter_r, W)]


def spike(x0, y0, x1, y1, w0, w1=0.0):
    """a beam from (x0, y0) to (x1, y1): w0 wide at its root, w1 at its end"""
    dx, dy = x1 - x0, y1 - y0
    n = (dx * dx + dy * dy) ** 0.5
    nx, ny = -dy / n, dx / n
    return [(x0 + nx * w0 / 2, y0 + ny * w0 / 2), (x1 + nx * w1 / 2, y1 + ny * w1 / 2), (x1 - nx * w1 / 2, y1 - ny * w1 / 2),
            (x0 - nx * w0 / 2, y0 - ny * w0 / 2)]


def flare(cx, cy, r, w):
    """a four-pointed glare: two thin diamonds crossed"""
    return [[(cx - r, cy), (cx, cy - w), (cx + r, cy), (cx, cy + w)], [(cx, cy - r), (cx + w, cy), (cx, cy + r), (cx - w, cy)]]


def quasar(cx, cy, k=1.0):
    """the core in the Q's counter, its jet one beam through it: out of the letter down and to the right (the Q's
    tail), and the counter-jet up and to the left, as far as the counter's edge"""
    ux, uy = 0.80, 0.60                          # (the beam's direction)
    out = [f'<path d="{path(spike(cx, cy, cx + 74 * k * ux, cy + 74 * k * uy, 6.5 * k, 2.5 * k))}" fill="{BLUE}"/>',
           f'<path d="{path(spike(cx, cy, cx - 33 * k * ux, cy - 33 * k * uy, 5 * k, 1.2 * k))}" fill="{BLUE}"/>']
    for f in flare(cx, cy, 15 * k, 2.2 * k):
        out.append(f'<path d="{path(f)}" fill="{CORE}"/>')
    out.append(f'<path d="{path([(cx, cy - 5 * k), (cx + 5 * k, cy), (cx, cy + 5 * k), (cx - 5 * k, cy)])}" fill="{CORE}"/>')
    return out


def wordmark(ink):
    shapes, x = [], 0.0
    for _, fn, w in LETTERS:
        for outer, inner in fn(w):
            d = path(outer, x) + (" " + path(inner, x) if inner else "")
            shapes.append(f'<path d="{d}" fill="{ink}" fill-rule="evenodd"/>')
        x += w + GAP
    shapes += quasar(50.0, 50.0)
    return shapes, x - GAP


def svg(view, body, label):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view}" role="img" aria-label="{label}">\n'
            + "\n".join(body) + "\n</svg>\n")


def hero():
    """the home page's art: the moment of release -- a white core blowing out a hard burst of rays, the two jets as
    beams breaking into blocks of data as they go (blue the one coming at you, red the one going away), the disc
    edge-on as hard lines; scales and labels as on a test sheet. Energy, not scenery"""
    import random
    rnd = random.Random(23)
    w, h = 1200, 440
    cx, cy = 560, 230
    body = [f'<rect width="{w}" height="{h}" fill="#050507"/>']
    for gx in range(0, w + 1, 40):
        body.append(f'<line x1="{gx}" y1="0" x2="{gx}" y2="{h}" stroke="#0e1017" stroke-width="1"/>')
    for gy in range(30, h, 40):
        body.append(f'<line x1="0" y1="{gy}" x2="{w}" y2="{gy}" stroke="#0e1017" stroke-width="1"/>')
    # the burst: rays of every length out of the core, hard-edged
    import math
    for i in range(90):
        a = rnd.uniform(0, 2 * math.pi)
        r0, r1 = rnd.uniform(18, 40), rnd.uniform(60, 260) * (1.0 if rnd.random() < 0.8 else 1.7)
        g = rnd.choice(["#ffffff", "#e9ecf5", "#9aa3c2", "#5b6380", "#3a4058"])
        body.append(f'<path d="{path(spike(cx + r0 * math.cos(a), cy + r0 * math.sin(a), cx + r1 * math.cos(a), cy + r1 * math.sin(a), rnd.uniform(1.2, 4.5), 0.4))}" fill="{g}"/>')
    # the disc, edge-on
    for rx, ry, col, sw in [(330, 30, "#232842", 2), (250, 22, "#3a4262", 2), (175, 15, "#6e789e", 2.5), (105, 9, "#c9d0ec", 3)]:
        body.append(f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="none" stroke="{col}" stroke-width="{sw}" '
                    f'transform="rotate(-6 {cx} {cy})"/>')
    # the jets: a solid beam, then blocks breaking off it, as a signal into data
    def jet(x1, y1, col, w0, n):
        body.append(f'<path d="{path(spike(cx, cy, x1, y1, w0, 2))}" fill="{col}"/>')
        dx, dy = x1 - cx, y1 - cy
        L = math.hypot(dx, dy)
        ux, uy, nx, ny = dx / L, dy / L, -dy / L, dx / L
        for _ in range(n):
            t = rnd.uniform(0.25, 1.02)
            off = rnd.gauss(0, 10 + 30 * t)
            bw, bh = rnd.uniform(4, 26) * (1.2 - t * 0.5), rnd.uniform(2, 9)
            x, y = cx + dx * t + nx * off, cy + dy * t + ny * off
            pts = [(x, y), (x + ux * bw, y + uy * bw), (x + ux * bw + nx * bh, y + uy * bw + ny * bh), (x + nx * bh, y + ny * bh)]
            body.append(f'<path d="{path(pts)}" fill="{col}"/>')
    jet(1196, 4, BLUE, 30, 70)
    jet(4, 436, RED, 20, 55)
    for f in flare(cx, cy, 120, 5):
        body.append(f'<path d="{path(f)}" fill="{CORE}"/>')
    body.append(f'<path d="{path([(cx, cy - 22), (cx + 22, cy), (cx, cy + 22), (cx - 22, cy)])}" fill="{CORE}"/>')
    mono = 'font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace" font-size="15" letter-spacing="1.5"'
    def label(x, y, text, col, n):                # (on a black tab, so nothing runs through it)
        body.append(f'<rect x="{x - 8}" y="{y - 17}" width="{n * 10.6 + 16:.0f}" height="25" fill="#050507" stroke="{col}" stroke-width="1"/>')
        body.append(f'<text x="{x}" y="{y}" fill="{col}" {mono}>{text}</text>')
    label(cx + 250, cy - 150, "JET  0.99c", BLUE, 10)
    label(cx - 470, cy + 190, "COUNTER-JET", RED, 11)
    label(w - 250, h - 20, "L &gt; 10^40 W", "#6e789e", 12)
    for i in range(0, 15):
        x = w - 250 + i * 16
        body.append(f'<line x1="{x}" y1="{h - 44}" x2="{x}" y2="{h - (58 if i % 5 == 0 else 52)}" stroke="#3a4058" stroke-width="2"/>')
    return svg(f"0 0 {w} {h}", body, "A quasar at the moment of release: a white core bursting with rays, a disc seen "
               "edge-on, a blue jet and a red counter-jet breaking into blocks")


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    dark, wd = wordmark(INK_DARK)
    light, _ = wordmark(INK_LIGHT)
    pad = 4
    view = f"{-pad} {-pad} {wd + 2 * pad:.0f} {H + 2 * pad + 6:.0f}"
    (OUT / "logo-dark.svg").write_text(svg(view, dark, "Quasar"))
    (OUT / "logo-light.svg").write_text(svg(view, light, "Quasar"))
    q = [f'<rect x="-14" y="-14" width="132" height="132" rx="10" fill="#050507"/>']
    for outer, inner in letter_q(100.0):
        q.append(f'<path d="{path(outer)} {path(inner)}" fill="{INK_DARK}" fill-rule="evenodd"/>')
    q += quasar(50.0, 50.0)
    (OUT / "mark.svg").write_text(svg("-14 -14 132 132", q, "Quasar"))
    (OUT / "hero.svg").write_text(hero())
    print(f"logo: {OUT}/mark.svg, logo-dark.svg, logo-light.svg, hero.svg")


if __name__ == "__main__":
    main()
