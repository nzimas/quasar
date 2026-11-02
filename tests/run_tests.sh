#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
cd "$(dirname "$0")/.."
PY=python3
[ ! -x .venv/bin/python ] || PY=.venv/bin/python
mkdir -p build/gen build/check build/screens build/sounds
$PY tools/gen_control_tables.py build/gen/control_tables.h
$PY tools/gen_font.py build/gen/fw_font.h >/dev/null
$PY tests/check_architecture.py
tests/check_target_ir.sh
# This executable proves the engine links without any frontend source or generated tables.
clang -DP23_HOST -O2 -std=gnu99 -Wall -Wextra -Werror \
    tests/host/engine_standalone.c firmware/engine/p23_engine.c -o build/engine-test
build/engine-test
clang -DP23_HOST -O2 -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Werror \
    -Ifirmware/src -Ibuild/gen tests/host/host.c firmware/engine/p23_engine.c -o build/host
build/host bridge
build/host screens build/screens
build/host levels | tail -1
for suite in lfo trn arp newses pstate brksave loopin lptrn patsave sesgrid; do
    build/host "$suite" build/screens
done
if command -v node >/dev/null; then node tests/web/test_backup.mjs build/host; fi
