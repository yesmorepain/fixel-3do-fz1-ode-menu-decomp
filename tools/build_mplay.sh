#!/bin/sh
# Rebuild mplay: src/mplay.c with the pinned flags, armlink with SDK 2.5 lib3do, audio, music, clib, swi, cstartup
# (this library order reproduces the original member layout), then modbin -stack 4096 -debug + stripaif
# (tools/aifpost.py). Output build/mplay = extracted/mplay, byte-identical (unsigned, no --sig-from).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
PY="$R/.venv/bin/python -W ignore $R/tools/mpwrun.py"
L=$R/references/sdk25_interfaces/libs
mkdir -p "$R/build"
"$R/tools/cc.sh" mplay >/dev/null 2>&1
cd "$R/build"
$PY "$R/references/sdk_mpw_armtools/armlink" -aif -r -b 0x00 "$@" \
    mplay.o $L/lib3do.lib $L/audio.lib $L/music.lib $L/clib.lib $L/swi.lib $L/cstartup.o -o mplay.aif -map -symbols mplay.sym > mplay.map
python3 "$R/tools/aifpost.py" mplay.aif mplay --stack 4096 --debug
