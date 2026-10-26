#!/bin/sh
# Compile src/<name>.c with the original toolchain and pinned flags -> build/<name>.o
# Usage: tools/cc.sh <name> [extra armcc args]
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
n=$1; shift
cd "$R/src"
"$R/.venv/bin/python" -W ignore "$R/tools/mpwrun.py" "$R/references/sdk_mpw_armtools/armcc" \
    -i "$R/references/sdk25_interfaces/includes" -i "$R/references/sdk25_examples/ExamplesLib" -fa -zps0 -za1 -g -d DEBUG=1 "$@" -c -o "$R/build/$n.o" ":$n.c"
