# Case study: Air Traffic Controller 5.03 (1993) → Windows, macOS, Linux

**Original:** Walter J. Biess's freeware ATC simulator (successor of *Near Miss*).
Lightspeed Pascal, 14 CODE segments (60 KB), 128 jump-table entries, 25 KB of
globals, 205 distinct traps. 512×342 black and white, a classic Toolbox app:
multiple windows (one round, via its own WDEF), menus, many dialogs, controls,
scroll bars, regions, the .Sound driver, Standard File, printing.
Public port: <https://github.com/KurdtNervanna/AirTrafficController-Port>.

## Why this one went fast

* Falcon MC had already taught the Toolbox — but its runtime was hard-wired to
  Falcon. ATC got a **fresh, cleanly layered runtime** (one file per manager, a host
  API with Win32 and SDL2 implementations). That runtime is this kit's `hle/`.
* ATC 5.03 was chosen over Near Miss 1.0 because 5.03 doesn't need MacinTalk speech.
* Data from earlier work: the System file (fonts) came from Falcon's System 6.0.8
  disk image via `hcopy -m`; the app came out of its `.sit` with `unar` as AppleDouble.

## What it needed that Falcon didn't

* **Real regions and a real Window Manager**: visRgns/updateRgns computed front to
  back, update events in Mac priority order, the game's WDEF executed as 68k via
  `call68k`.
* **Real fonts**: System 6 `FONT` resources rendered from their strikes.
* **Dialog Manager in depth**: ALRT/DLOG/DITL, edit text with caret and selection,
  radio/check boxes, ModalDialog with filter procs.
* **Controls** with 68k action procs (scroll bar auto-repeat).
* **File Manager** for schedules/high scores, Standard File → native dialogs,
  settings written into the app's own fork → overlay file.
* **Packaging for three OSes** from one CMake file + one GitHub Actions workflow.

## Gotchas found (all in docs/gotchas.md)

`Lo3Bytes` for StripAddress glue; `SdVolume` 0 = silent; `OpenPort` needs fresh
regions; FONT `owTable` missing the final -1; `Secs2Date`/`Date2Secs` are register
traps; `ModalDialog` must pump host events while other windows need updates;
`GetCWMgrPort` is called on the print path even on a B&W machine.

## Testing approach

Everything was verified headless with `ATC_SCRIPT` (now `HLE_SCRIPT`) scripts and
PBM screen dumps; the real window was checked with PrintWindow captures. CPU use was
brought down from 100% to ~20% by sleeping when the game spins on `TickCount`.
