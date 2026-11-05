#!/usr/bin/env bash
# Headless Ghidra: build the combined image and decompile the WHOLE app to one C file.
#
#   GHIDRA_HOME=/path/to/ghidra_12.x_PUBLIC  JAVA_HOME=/path/to/jdk21  \
#     bash run_combined.sh <workdir> [symbols.tsv]
#
# Prerequisites (docs/03-ghidra-decomp.md):
#   1. python tools/macres/code_extract.py <app> <workdir>
#   2. python tools/ghidra/build_combined.py <workdir>
#   3. ONCE per Ghidra install: python tools/ghidra/patch_sleigh.py \
#        "$GHIDRA_HOME/Ghidra/Processors/68000/data/languages/68000.sinc"
#      (teaches the 68k decompiler that A-line words are Toolbox calls; without it
#       functions are truncated at the first trap)
# Output: <workdir>/ghidra_out/combined.c  and <workdir>/recon/code_map.tsv
# Then:   python name_traps.py <workdir>/ghidra_out/combined.c
#         python name_thunks.py <workdir>            (both MUST be re-run after every rebuild)
#
# Works from Git Bash on Windows (uses cygpath + analyzeHeadless.bat) and on Linux/macOS.
# Do NOT wrap analyzeHeadless.bat in `cmd //c` - the quoting breaks.
set -u
W="$(cd "${1:?usage: run_combined.sh <workdir> [symbols.tsv]}" && pwd)"
SYMS="${2:-$W/symbols.tsv}"
HERE="$(cd "$(dirname "$0")" && pwd)"
: "${GHIDRA_HOME:?set GHIDRA_HOME to the Ghidra install folder}"
if command -v cygpath >/dev/null 2>&1; then P() { cygpath -w "$1"; }; HEADLESS="$GHIDRA_HOME/support/analyzeHeadless.bat"
else P() { echo "$1"; }; HEADLESS="$GHIDRA_HOME/support/analyzeHeadless"; fi
[ -n "${JAVA_HOME:-}" ] && export PATH="$JAVA_HOME/bin:$PATH"
mkdir -p "$W/ghidra_out" "$W/ghidra_proj"
[ -f "$SYMS" ] || printf '# addr\tname\tG|F   (G = global/label, F = function)\n' > "$SYMS"

"$HEADLESS" "$(P "$W/ghidra_proj")" COMBINED \
  -import "$(P "$W/combined/dummy.bin")" \
  -processor 68000:BE:32:default -loader BinaryLoader -loader-baseAddr 0x0 \
  -scriptPath "$(P "$HERE")" \
  -postScript BuildCombined.java \
    "$(P "$W/combined/layout.tsv")" "$(P "$W/combined/entries.txt")" "$(P "$W/combined/meta.tsv")" \
    "$(P "$W/ghidra_out/combined.c")" "$(P "$W")" "$(P "$SYMS")" \
  -deleteProject 2>&1 | grep -iE "block |A5 set|entry functions|symbols applied|COMBINED_FUNCS|ERROR |Exception|severe" | head -70
