#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tables for graphics and the retained control LFOs only; never audio oscillators."""
import math,sys
from pathlib import Path
arrays={"SINE":("int16_t",[round(32767*math.sin(2*math.pi*i/1024)) for i in range(1025)]),
"EXP2_FRAC":("uint32_t",[round(65536*2**(i/256)) for i in range(257)]),
"LOG2_FRAC":("uint16_t",[min(65535,round(65536*math.log2(1+i/256))) for i in range(257)])}
lines=["/* UI/control LFO data; no synthesis wave banks. */","#pragma once","#include <stdint.h>","#define FS 44100","#define CTL 32"]
for name,(kind,values) in arrays.items():
 lines.append(f"static const {kind} {name}[{len(values)}] = {{")
 for i in range(0,len(values),12):lines.append("    "+", ".join(map(str,values[i:i+12]))+",")
 lines.append("};")
Path(sys.argv[1]).write_text("\n".join(lines)+"\n")
