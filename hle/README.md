# hle/ — a starter Mac Toolbox HLE runtime

A small (~6 kLOC) C program that runs an **unmodified classic 68k Macintosh
application** on Windows, macOS or Linux:

* the app's own 68k code executes on [Musashi](https://github.com/kstenerud/Musashi) (a 68020 core),
* every Toolbox/OS call it makes (an *A-line trap*) is answered by C code in `src/`,
* all Mac data structures (GrafPorts, windows, menus, regions, handles, low-memory
  globals) live in an emulated 16 MB address space **in their real Mac layouts**,
  because games read and write their fields directly.

This is the runtime from the *Air Traffic Controller 5.03* port, with the game's name
and file names moved into `src/config.h`. As shipped it runs ATC end to end, so it
is a known-good baseline: **when your game misbehaves, the bug is in a trap the
baseline never needed, or in one your game calls differently.**

It is a 1-bit (black & white, System 6 style) QuickDraw. Colour games need Color
QuickDraw added — see [docs/06-quickdraw-display.md](../docs/06-quickdraw-display.md)
for exactly what Falcon MC needed.

## Build

| Host | Command |
|---|---|
| Windows (dev, console logs) | from PowerShell: `cmd /c ".\build.bat"` → `build\hle.exe` |
| Windows (release) | `cmake -S . -B b -A x64 && cmake --build b --config Release` |
| macOS / Linux | `cmake -S . -B b -DCMAKE_BUILD_TYPE=Release && cmake --build b` (SDL2 is fetched and linked statically) |

Per-game identity: edit `src/config.h`, or pass `HLE_DEFS=/DHLE_APP_NAME=\"My Game\"`
(build.bat) / `-DHLE_APP_NAME="My Game"` (CMake).

## Run

```
hle.exe <folder with the game files>
```

The folder needs the application (raw resource fork, AppleDouble or MacBinary — found
by content, any name) and a **System 6 System file** (for its fonts). If no folder is
given, the runtime looks in `$HLE_GAME`, `game/` next to the exe, the per-user data
folder, and finally asks.

## Source map

| File | Job |
|---|---|
| `main.c` | find game files, Segment Loader (CODE 0 → A5 world + jump table), low-memory globals, main loop, `HLE_SCRIPT` test driver, screen dump |
| `trap.c` | A-line dispatch (RTE-frame editing), `call68k()` callbacks into 68k code, OS traps (Memory Mgr, misc), utility Toolbox traps, bad-jump diagnostics |
| `mem.c` | handles/pointers in emulated memory (master pointers + 8-byte block headers) |
| `rsrc.c` | Resource Manager over a chain of forks (System → app → overlay), writes go to the overlay |
| `qd.c` `rgn.c` `font.c` | QuickDraw: ports, patterns, transfer modes, lines/rects/ovals/round-rects, CopyBits, PICT, real regions, System 6 bitmap fonts |
| `wm.c` | Window Manager with real visRgns/updateRgns; runs the app's own WDEFs as 68k |
| `menu.c` `dlg.c` `ctl.c` | Menus (MBAR/MENU, MenuSelect, MenuKey), Dialogs/Alerts (DLOG/DITL/ALRT, ModalDialog, edit text), Controls (CNTL, TrackControl with 68k action procs) |
| `evt.c` | Event queue fed by the host: GetNextEvent/WaitNextEvent/EventAvail, KeyMap, mouse |
| `fs.c` | File Manager on a host folder: data fork = file, resource fork = `.rsrc/name`, Finder info = `.finf/name` |
| `pack.c` | Packages: SANE, Standard File (→ native dialogs), Sound (.Sound driver / SndPlay), PrGlue (prints "unavailable") |
| `host_win.c` `host_sdl.c` `plat.c` `audio_win.c` | the only platform code: window, present, input, cursor, dialogs, audio |
| `config.h` | per-game names and defaults |

## Debug environment variables

All are prefixed `HLE_` (change in config.h). The main ones:

| Var | Effect |
|---|---|
| `HLE_SCRIPT="ms:action;..."` | scripted input for headless tests: `click x y`, `down x y`, `up x y`, `move x y`, `key c` (`\r` `\t` `\b`), `cmd c`, `dump file.pbm`, `quit` |
| `HLE_HIDDEN=1` | don't show the window (headless runs) |
| `HLE_DUMP=f.pbm` | dump the screen on exit (convert with `tools/assets/pbm2png.py`) |
| `HLE_MAXSLICES=n` | stop after n execution slices |
| `HLE_TRACE=1` | log every trap (slow) |
| `HLE_PCLOG=traps,count` | after `traps` traps, log `count` instructions with registers |
| `HLE_SEGLOG=1` | where each CODE segment was loaded |
| `HLE_EVLOG` `HLE_WINLOG` `HLE_TEXTLOG` `HLE_CBLOG` `HLE_COMPLOG` | event / window / text / CopyBits / window-compose logs |
| `HLE_SAVE=dir` | where the File Manager keeps the app's files |
| `HLE_GAME=dir` | game folder |
| `HLE_SCALE=n` | initial window scale |
| `HLE_NOSOUND=1`, `HLE_WAVDUMP=f.wav` | audio off / also record audio |

**Never call `getenv()` per instruction or per trap.** Cache it once (see `PCLOG` in
`trap.c`). Falcon MC ran ~40× slower from one getenv in the instruction hook.

## Licence

Port code: MIT. Musashi: MIT (`musashi/readme.txt`). SoftFloat: its own permissive
notice (`musashi/softfloat/README.txt`). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
