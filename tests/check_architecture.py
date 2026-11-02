#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Enforce the explicit user requirement: independent engine architecture."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
engine=root/'firmware/engine'
for path in engine.glob('*.[ch]'):
    text=path.read_text()
    imports=re.findall(r'^\s*#include\s*[<"]([^>"\n]+)',text,re.M)
    assert set(imports)<= {'stdint.h','p23_engine.h','p23_math.h'},(path,imports)
    forbidden=r'\b(?:sound_t|ck_t|vck_t|vchain_t|vmem|vox|svf_t|svf_c|sin_ph|tanh_q15|note_inc|EXP2_FRAC|SINE|WT_DATA|vc_block|vck_cook|rv_block|ch_block|dl_block|cp_block|mangle_block)\b'
    # Only executable text, not explanatory comments, is audited for forbidden symbols.
    code=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
    assert not re.search(forbidden,code),path
for retired in ['voice.c','pulsar.c','fx.c','mangle.c','osc.c','fmt.c','fmd.c','wav.c','dsp.h']:
    assert not (root/'firmware/src'/retired).exists(),retired
build=(root/'tools/build.py').read_text()
assert 'p23_engine.o' in build and '"engine" / "p23_engine.c"' in build
assert not (root/'tools/gen_tables.py').exists()
front=(root/'firmware/src/tracks.c').read_text()
assert not re.search(r'\b(?:NVOICE|voice_pick|voice_render|vmem|vox|vck_cook|track_block|lim_block)\b',front)
print('architecture: standalone module graph; no inherited audio dependencies, voice allocator or DSP tables')
