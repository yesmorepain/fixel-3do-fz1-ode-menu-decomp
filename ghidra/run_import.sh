#!/bin/sh
# Import the four AIF programs into ghidra/fz1menu.gpr, analyse, export C to ghidra/<name>_decompiled.c.
set -e
cd "$(dirname "$0")"
# Needs GHIDRA_HOME (Ghidra 11.x install folder) and a Java 21 on PATH or in JAVA_HOME.
: "${GHIDRA_HOME:?set GHIDRA_HOME to the Ghidra installation folder}"
G=$GHIDRA_HOME/support/analyzeHeadless
X=../extracted
for f in LaunchMe System/Drivers/ode mplay assets/storagetuner; do
  n=$(basename $f)
  $G . fz1menu -import $X/$f -overwrite -processor ARM:BE:32:v4 -loader BinaryLoader -loader-baseAddr 0 \
     -scriptPath scripts -preScript AifSetup.java -postScript Portfolio.java "$PWD/../tools/swi_table.tsv" -postScript ApplyNames.java "$PWD/../tools/names/common.tsv" -postScript ApplyNames.java "$PWD/../tools/names/matched/$n.tsv" -postScript ApplyNames.java "$PWD/../tools/names/linked/$n.tsv" -postScript ApplyNames.java "$PWD/../tools/names/$n.tsv" -postScript ExportIndex.java "$PWD/${n}_functions.tsv" -postScript ExportListing.java "$PWD/${n}_disasm.s" -postScript ExportDecomp.java "$PWD/${n}_decompiled.c" 2>&1 \
     | grep -E "AIF|decompiled|ERROR|Import succeeded" || true
done
