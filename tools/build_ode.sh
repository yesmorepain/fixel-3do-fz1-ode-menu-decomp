#!/bin/sh
# Rebuild System/Drivers/ode: kernellib finditem.c/CreateItem.c (portfolio_os, super.h stand-in), src/odedrv.c,
# generated superdo/supervectors AOF objects, SDK 2.5 clib/swi/cstartup; armlink; modbin-equivalent post-processing.
# Output build/ode = extracted/System/Drivers/ode, byte-identical: the signature (0x1c0c-0x1c4c) and its header fields
# 0xb0/0xb4 are not regenerated (re-signing not tried) and are copied from the original (aifpost.py --sig-from), after checking that
# everything before them already matches. Unsigned build: build/ode.unsigned.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
PY="$R/.venv/bin/python -W ignore $R/tools/mpwrun.py"
L=$R/references/sdk25_interfaces/libs
mkdir -p "$R/build/kernellib"
cp "$R/references/portfolio_os/kernellib/finditem.c" "$R/references/portfolio_os/kernellib/CreateItem.c" \
    "$R/tools/kernellib/super.h" "$R/build/kernellib/"
for f in finditem CreateItem; do
    (cd "$R/build/kernellib" && $PY "$R/references/sdk_mpw_armtools/armcc" -i "$R/references/sdk25_interfaces/includes" \
        -fa -zps0 -za1 -g -d DEBUG=1 -c -o "$R/build/$f.o" ":$f.c" >/dev/null 2>&1)
done
"$R/tools/cc.sh" odedrv >/dev/null 2>&1
python3 "$R/tools/mkkernellib.py" "$R/references/portfolio_os/kernellib/supervectors.s" "$R/build" >/dev/null
cd "$R/build"
$PY "$R/references/sdk_mpw_armtools/armlink" -aif -r -b 0x00 "$@" \
    finditem.o CreateItem.o odedrv.o superdo.o supervectors.o $L/clib.lib $L/swi.lib $L/cstartup.o -o ode.aif -map -symbols ode.sym > ode.map
# modbin -stack 4096 (flags 0x02 privileged, name "odedrv", no -debug), then stripaif
python3 "$R/tools/aifpost.py" ode.aif ode.unsigned --stack 0x1000 --flags 0x02 --name odedrv
python3 "$R/tools/aifpost.py" ode.aif ode --stack 0x1000 --flags 0x02 --name odedrv \
    --sig-from "$R/extracted/System/Drivers/ode"
