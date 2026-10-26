#!/bin/sh
# Rebuild LaunchMe: compile src/*.c and the SDK 2.5 ExamplesLib files with the pinned flags, link with MPW armlink.
# Output: build/LaunchMe.aif (before modbin). Usage: tools/build_launchme.sh [extra armlink args]
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
PY="$R/.venv/bin/python -W ignore $R/tools/mpwrun.py"
L=$R/references/sdk25_interfaces/libs
mkdir -p "$R/build/exampleslib"
cp "$R/references/sdk25_examples/ExamplesLib/"*.[ch] "$R/build/exampleslib/"
for f in controlpad getvideoinfo; do
    (cd "$R/build/exampleslib" && $PY "$R/references/sdk_mpw_armtools/armcc" -i "$R/references/sdk25_interfaces/includes" \
        -fa -zps0 -za1 -g -d DEBUG=1 -c -o "$R/build/$f.o" ":$f.c" >/dev/null 2>&1)
done
for f in main ode ui crc rampatch; do "$R/tools/cc.sh" $f >/dev/null 2>&1; done
cd "$R/build"
$PY "$R/references/sdk_mpw_armtools/armlink" -aif -r -b 0x00 "$@" \
    main.o controlpad.o getvideoinfo.o ode.o ui.o crc.o rampatch.o \
    $L/lib3do.lib $L/filesystem.lib $L/graphics.lib $L/input.lib $L/clib.lib $L/swi.lib $L/cstartup.o \
    -o LaunchMe.aif -map -symbols LaunchMe.sym > LaunchMe.map
# SDK debug build post-processing: modbin -stack 4096 -debug, then stripaif
python3 "$R/tools/aifpost.py" LaunchMe.aif LaunchMe --stack 4096 --debug
