# 02 — Recon: understand the program's shape

```
python tools/macres/code_extract.py app.bin work/
python tools/analysis/traps.py app.bin > work/recon/traps.txt
python tools/analysis/trap_audit.py app.bin
```

## The 68k application model in five facts

1. **CODE 0 is the jump table.** Header: `aboveA5.L, belowA5.L, jtSize.L, jtOffset.L`,
   then 8-byte entries. On disk each entry is *unloaded*:
   `[offset.W][MOVE.W #seg,-(SP) = 3F3C][seg.W][_LoadSeg = A9F0]`.
   The Segment Loader rewrites a loaded entry as `[seg.W][JMP abs.L = 4EF9][address.L]`.
   Callers `JSR jtOffset+idx*8+2(A5)` — i.e. they jump *into* the entry at +2.
2. **CODE 1..n are segments.** Near-model header: `[offset of first JT entry.W][count.W]`,
   then code. **Entry offsets in CODE 0 are relative to the code start (+4).**
   Near segments are ≤ 32 KB and use only PC-relative branches inside a segment, so
   a segment can be loaded anywhere.
3. **A5 is the world pointer.** Application globals live *below* A5 (`belowA5` bytes),
   the jump table and QuickDraw globals pointer *above*. `A5-0x1234` = a global;
   `0(A5)` holds a pointer to QuickDraw's globals (`thePort` is at that address).
4. **Entry point = jump table entry 0** (`A5 + jtOffset + 2` after loading).
5. **Everything else is a trap.** `traps.txt` lists them, with the first call site.

The runtime's `load_segments()` (in `hle/src/main.c`) does exactly this: copies each
`CODE` resource into a locked handle, writes loaded entries, sets A5 and PC.

## Reading `code_extract.py` output

* `recon/segmap.md` — sizes and entry points per segment. Big segments with many
  entries are engine code; segments full of Dialog/Menu traps are UI.
* `recon/fingerprint.txt` — compiler hints:
  * `%_Static_Constructor`, `__CplusInit` → **THINK C 5+/Symantec C++** (Falcon MC).
  * `%A5Init`, `_DataInit` → MPW.
  * MacsBug names present → you get function names for free (`recon/macsbug_symbols.tsv`;
    feed them to `symbols.tsv`).
  * count of `LINK A6` → number of compiled (framed) functions; frameless code
    between them is hand-written assembly or leaf routines.
  * far-model warning → segments with header `0xFFFF` need a different loader.
* `recon/jumptable.tsv` — used by the Ghidra tools.

## Size the work with the trap audit

`trap_audit.py` compares the traps the app uses with those implemented in
`hle/src` and prints three lists:

* **NOT IMPLEMENTED** — real work items. Order doesn't matter yet; the runtime log
  will tell you which are hit first.
* **UNKNOWN WORDS** — A-line-looking words with no name. Usually *data in code*
  (tables, constants: `A0FF`, `A2FF`, `AEFF` …). Confirm with
  `disasm.py app find <word>`: real traps sit in instruction streams after pushes.
* **SELECTOR DISPATCHERS** — `_Pack0..7`, `_HFSDispatch`, `_Gestalt`, `_PrGlue`…
  one trap word, many functions chosen by a selector in a register or on the stack.
  Check each selector the app uses.

Static scanning misses traps called via `GetTrapAddress` + `JSR (A0)` and traps
inside non-CODE code resources (`WDEF`, `CDEF`, `MDEF`, `LDEF`, private ones like
Falcon's `'Jnth'`/`'MDRV'` sound driver code that executes from the heap). The
runtime's `[trap] unimplemented XXXX at PC` log catches those.

## Other things to look for

* **Code resources** other than CODE (`rfork.py info`): the runtime must load and
  run them as 68k (ATC's round-window `WDEF 128` runs as 68k code in `wm.c`).
* **Hardware addresses**: search the disassembly for absolute addresses in the
  `$EFxxxx` (VIA), `$9FFFF8`/`SoundBase` range, or low-memory globals the app pokes
  directly (`$16A Ticks`, `$174 KeyMap`, `$830 Mouse`, `$824 ScrnBase`, `$266 SoundBase`).
  Every low-memory global the app reads must be kept current by the runtime.
* **Resources the app writes**: `AddResource`/`ChangedResource`/`WriteResource` on
  its *own* file = settings saved inside the app. The runtime redirects these to an
  overlay file so the user's copy is never modified.
* **Data formats**: private resource types (Falcon's `OBJS` 3-D models, `SINE`
  tables) — decode only if you need them (for a remaster or debugging).

## Write it down

Start `PORT_NOTES.md` (template in `templates/`) with: app/version/compiler,
segment table, globals size, trap count, unusual features, and a list of open
questions. Update it every session.
