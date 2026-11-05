# tools/

Python 3.8+. Everything is stdlib-only except the tools marked **capstone**
(`pip install capstone`). Every tool prints its usage when run without arguments.
All of them accept MacBinary, AppleDouble, AppleSingle or raw resource forks.

| Tool | Does |
|---|---|
| `macres/rfork.py` | `info` / `list` / `extract` / `data` / `dump` — unwrap containers, list and dump resources |
| `macres/code_extract.py` | split CODE segments, decode the CODE 0 jump table, segment map, manifest, MacsBug procedure names, compiler fingerprint |
| `analysis/traps.py` **capstone** | trap histogram with first call site |
| `analysis/trap_audit.py` **capstone** | traps the app uses vs. traps implemented in `hle/src` (or your port's src) |
| `analysis/disasm.py` **capstone** | disassemble a segment; `find <trap>` shows every call site with context |
| `analysis/trapnames.py` | trap word → name table (+ CLI: `trapnames.py A9A0 A01E`) — extend it as you learn |
| `assets/pict2png.py` | PICT v1/v2 → PNG (bitmaps, indexed and direct pixmaps) |
| `assets/snd2wav.py` | `snd ` format 1/2 → WAV |
| `assets/pbm2png.py` | the runtime's 1-bit screen dump → PNG (2×) |
| `ghidra/patch_sleigh.py` | teach Ghidra's 68k decompiler about A-line traps (once per install) |
| `ghidra/build_combined.py` | lay out all segments + jump table + A5 world as one program |
| `ghidra/run_combined.sh` + `BuildCombined.java` | headless import, analysis, naming from `symbols.tsv`, decompile to one C file, code/data map |
| `ghidra/DecompileSeg.java` | single-segment decompile (rarely needed) |
| `ghidra/name_traps.py` | `MacToolboxTrap(0xA9A0)` → `_GetResource(...)` |
| `ghidra/name_thunks.py` | jump-table calls → `jt_<function>()` |
| `toolchain/setup_wsl_noroot.sh` | unar, hfsutils, m68k binutils, capstone in WSL/Linux without sudo |
| `win/capture.ps1` | PNG of a running window (PrintWindow) |
| `win/sendkeys.ps1` | send virtual-key presses to the focused window |

Typical first hour:

```
python macres/rfork.py info Game.bin
python macres/code_extract.py Game.bin ../work
python analysis/trap_audit.py Game.bin
python assets/pict2png.py Game.bin ../work/pict      # look at the art you'll need to render
python analysis/disasm.py Game.bin find A970 10 3    # how does it read events?
```
