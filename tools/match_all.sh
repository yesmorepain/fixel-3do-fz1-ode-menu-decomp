#!/bin/sh
# Regenerate tools/names/matched/<prog>.tsv: original SDK 2.5 libs + startup objects (references/sdk25_interfaces),
# then curated devkit libs not in that set (without pgl/examples; devkit clib.lib/codec.lib differ from 2.5, skipped).
# Then tools/names/linked/<prog>.tsv: library names from the byte-identical rebuilds' link maps (build/<prog>.map/.sym,
# written by tools/build_<prog>.sh); objects reconstructed in src/ are skipped.
cd "$(dirname "$0")/.."
S=references/sdk25_interfaces/libs
L=references/3do-devkit/lib/3do
LIBS="$S/*.lib $(ls $L/*.lib | grep -v -E 'pgl|exampleslib' | while read f; do [ -f $S/$(basename $f) ] || echo $f; done) $S/*.o"
for p in LaunchMe System/Drivers/ode mplay assets/storagetuner; do
  n=$(basename $p); printf "%s: " $n
  python3 -I tools/match_libs.py extracted/$p $LIBS --tsv tools/names/matched/$n.tsv
done
mkdir -p tools/names/linked
for o in LaunchMe:main.o,ode.o,ui.o,crc.o,rampatch.o ode:odedrv.o,superdo.o mplay:mplay.o storagetuner:; do
  n=${o%%:*}; printf "%s linked: " $n
  if [ -f build/$n.map ] && [ -f build/$n.sym ]; then
    python3 -I tools/linkmap_names.py build/$n.map build/$n.sym --own "${o#*:}" --include references/sdk25_interfaces/includes --tsv tools/names/linked/$n.tsv
  else
    echo "no build/$n.map/.sym (run tools/build_*.sh first), kept old"
  fi
done
