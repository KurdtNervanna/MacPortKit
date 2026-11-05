# 03 — Whole-program decompilation with Ghidra

You don't need a decompile to boot a game in the HLE. You need one the first time
you ask "*why* does the game do that?" — and from then on, constantly. Build it
early; it costs a few minutes of machine time.

## Setup (once)

* Ghidra 11/12 + a JDK 21. Ghidra 12 dropped bundled Jython → **scripts are Java**
  (`tools/ghidra/*.java`), not Python.
* Patch the 68k language so A-line words decode as Toolbox calls:

  ```
  python tools/ghidra/patch_sleigh.py "$GHIDRA_HOME/Ghidra/Processors/68000/data/languages/68000.sinc"
  ```

  It adds `define pcodeop MacToolboxTrap;` and a constructor
  `:A_trap op015 is op=0xA & op015 { MacToolboxTrap(op015:2); }`, then deletes the
  compiled `.sla` so Ghidra recompiles the language on next start. **Without this,
  every function is truncated at its first trap** ("bad instruction data") — on
  Falcon MC that was 31 truncated files; after the patch, 1.

## Build

```bash
python tools/macres/code_extract.py app.bin work/
python tools/ghidra/build_combined.py work/
GHIDRA_HOME=... JAVA_HOME=... bash tools/ghidra/run_combined.sh work/
python tools/ghidra/name_traps.py work/ghidra_out/combined.c --report work/recon/trap_usage.md
python tools/ghidra/name_thunks.py work/
```

Result: `work/ghidra_out/combined.c`, one C file for the whole program, and
`work/recon/code_map.tsv` (which bytes Ghidra considers instructions vs. data).
On ATC: 206 functions, 1,361 traps named, 414 cross-segment calls resolved, in
about two minutes. On Falcon MC: 1,043 functions, ~42k lines.

### Why "combined"

Decompiling segments one by one gives you code where every cross-segment call is
`(*(code *)(unaff_A5 + 0x3a2))()` and every global is `*(short *)(unaff_A5 + -0x2258)`.
Useless. `build_combined.py` + `BuildCombined.java` instead:

* place `CODE n` at `0x100000 + n*0x10000` (so an address *tells you* the segment:
  `0x1F1522` = CODE 15 + 0x1522 = offset 0x1522 in `segments/CODE_15.bin`),
* create the app-globals block below A5 and the A5 world above it,
* **synthesize the loaded jump table** (`[off][JMP][abs]`), disassemble each JMP,
* **pin A5 = 0x800000** across all code (`ProgramContext.setValue`), so
  `-0x2258(A5)` becomes `DAT_007fdda8`,
* create functions at all jump-table targets, run auto-analysis (which finds the
  internal functions), apply `symbols.tsv`, decompile everything.

Then `name_traps.py` turns `MacToolboxTrap(0xa9a0)` into `_GetResource(/*0xa9a0*/)`
and `name_thunks.py` turns `(*_LAB_00800172)()` into `jt_FUN_0011e2a0()` (or
`jt_drawPictById()` once named).

**Both post-processors must be re-run after every Ghidra rebuild**, in that order.

## Naming: `symbols.tsv`

```
# addr      name             G|F
0x1F1522    sceneDrawDispatch  F
0x7FB83C    g_altitude         G
```

Tab-separated; `F` = function, `G` = global/label. Add a line, rebuild, and the name
propagates through all of `combined.c`. Seed it from `recon/macsbug_symbols.tsv`
when the compiler embedded names (convert segment+offset → `0x100000+seg*0x10000+offset`).

Rules that kept Falcon's names honest:

* Name only what you traced. Write *why* in `PORT_NOTES.md` (who calls it, what it
  reads/writes).
* Don't name by "feel" (number of multiplies, size). One such name was wrong and
  misled two sessions.
* Prefer role names for globals once proven: `g_frameTick`, `g_viewMode`, `g_screenId`.

## Mapping Ghidra ↔ runtime ↔ files

| You have | Convert |
|---|---|
| Ghidra address `0x1Fxxxx` | segment = `(addr-0x100000)>>16`, offset = `addr & 0xFFFF` (CODE-resource offset, header included) |
| Global `DAT_007fxxxx` | `A5-relative = addr - 0x800000` (negative) → in the runtime: `A5_BASE + that` |
| Runtime PC | `HLE_SEGLOG=1` logs where each segment's handle data starts; PC - (start) = CODE offset |

Many ports simplify this by loading segments in the runtime at the **same**
addresses as the Ghidra layout (Falcon's HLE did: seg N at 0x100000+N*0x10000 and
A5 at 0x800000) so that addresses are identical everywhere. Worth considering if
you'll debug a lot; the ATC-derived runtime allocates segments in its heap instead.

## Known limitations and workarounds

* **Register-argument functions.** Hand-written asm often takes arguments in
  D0-D2/A0. Ghidra doesn't know the convention, so callers look like they pass
  nothing, and xrefs from the real callers may be missing. Find consumers through
  the jump-table thunks (`recon/thunk_map.tsv`) or by searching the disassembly.
* **Switch tables / data in code** produce "unknown operator" noise; `code_map.tsv`
  tells you which ranges are data.
* **Pascal functions** (Lightspeed/THINK Pascal) have callee-pops-args conventions;
  Ghidra's output is readable but stack offsets look odd. Trust the disassembly.
* **Decompiled C is a reading aid, not a compilable port.** Use it to understand,
  then verify against the disassembly (`tools/analysis/disasm.py`).

## Keeping it private

`work/` contains the program's bytes and derived code. Git-ignore it. If you want
to publish RE results, publish your *notes and tools*, never `combined.c`.
