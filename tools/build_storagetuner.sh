#!/bin/sh
# Rebuild assets/storagetuner: the SDK 2.5 Storage Tuner example (st.c stlists.c stmem.c from
# references/sdk25_examples/NVRAM/Storage_Tuner) with st.c patched by src/storagetuner_st.c.patch (PAL display
# type in CreateTunerScreen), pinned flags, storagetuner.make link order (objects, cstartup.o, Lib3DO audio music
# operamath filesystem graphics input clib), then modbin -stack 4096 -debug + stripaif (tools/aifpost.py).
# Output build/storagetuner = extracted/assets/storagetuner, byte-identical (unsigned, no --sig-from).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
PY="$R/.venv/bin/python -W ignore $R/tools/mpwrun.py"
L=$R/references/sdk25_interfaces/libs
T=$R/references/sdk25_examples/NVRAM/Storage_Tuner
W=$R/build/storagetuner.d
rm -rf "$W"; mkdir -p "$W"
for f in st.c stlists.c stmem.c st.h stlists.h stmem.h storagetuner.h; do tr '\r' '\n' < "$T/$f" > "$W/$f"; done
patch -s -d "$W" -p1 < "$R/src/storagetuner_st.c.patch"
cd "$W"
for n in st stlists stmem; do
    $PY "$R/references/sdk_mpw_armtools/armcc" -i "$R/references/sdk25_interfaces/includes" \
        -fa -zps0 -za1 -g -d DEBUG=1 -c -o $n.o :$n.c >/dev/null
done
$PY "$R/references/sdk_mpw_armtools/armlink" -aif -r -b 0x00 -d "$@" \
    st.o stlists.o stmem.o $L/cstartup.o $L/lib3do.lib $L/audio.lib $L/music.lib $L/operamath.lib \
    $L/filesystem.lib $L/graphics.lib $L/input.lib $L/clib.lib -o storagetuner.aif -map -symbols "$R/build/storagetuner.sym" > "$R/build/storagetuner.map"
python3 "$R/tools/aifpost.py" storagetuner.aif "$R/build/storagetuner" --stack 4096 --debug
