# 07 — Input, timing, sound and files

## Input: find out how the game reads it *before* wiring anything

Mac software reads input in at least five ways, and games mix them:

| Mechanism | Runtime must |
|---|---|
| Event Manager: `GetNextEvent`/`WaitNextEvent`/`EventAvail` | queue host events as Mac events (mouseDown/Up, keyDown/Up/autoKey, update, activate) with modifiers |
| Polling: `Button`, `StillDown`, `GetMouse`, `GetKeys` | return live state |
| **Low memory read directly**: `KeyMap $174` (16 bytes), `Mouse $830` (v,h), `MBState $172`, `Ticks $16A` | keep them current every slice (`hle_idle` does) |
| **VBL tasks** (`VInstall`) that sample input every N ticks | actually run the tasks (below) |
| Hardware (ADB, VIA) | rare in games; emulate the low-memory result instead |

Falcon MC is the cautionary tale: the obvious keyboard routine (`readKeyboard`, gated
on Option) turned out to be the *camera* keys. Real flight keys came from a **VBL
task** that ORs `KeyMap` into a latch every 3 ticks, snapshotted once per frame.
Only tracing the reads found it.

**KeyMap layout:** key code *k* → byte `k >> 3`, bit `k & 7` (bit 0 = LSB). Build a
table from host virtual keys to Mac key codes (ANSI layout: `A`=0x00, `S`=0x01, …,
Return 0x24, Space 0x31, arrows 0x7B–0x7E, Command 0x37, Shift 0x38, Option 0x3A,
Control 0x3B; keypad 0x52–0x5C). Map host Ctrl → Command and Alt → Option, and say so
in the README.

**Mouse**: convert host coordinates to Mac *global* coordinates (account for your
scaling and any presented-region offset). `GetMouse` returns *local* coordinates of
the current port; `GlobalToLocal`/`LocalToGlobal` use the port's portBits bounds.

**Clicks are not instantaneous.** Toolbox-style tracking loops (`StillDown` /
`WaitMouseUp` polling, `TrackControl`) need the button held across several polls.
Scripted test input must hold `down` for a while before `up`.

### Running VBL tasks (Falcon's design)

`VInstall(A0 = VBLTask)`: record `{qLink, qType, vblAddr, vblCount, vblPhase}`.
Every tick, decrement `vblCount`; at 0 call the task with **A0 = the task record**
via `call68k`, saving/restoring the whole CPU context (the task may run between any
two instructions of the main program). The task resets `vblCount` itself to repeat.
Use a distinct return sentinel. Do the same for Time Manager tasks if needed.

### Controllers

Once the game's input path is known, a gamepad is just another source feeding the
same KeyMap bits / mouse position (Falcon: arrows/IJLM/keypad → the game's own
pitch/roll key codes; a stick axis → mouse delta, which is what the original Gravis
MouseStick driver did).

## Time

* `Ticks` = 60 Hz **wall clock** (`now_ms * 60 / 1000`). Deriving ticks from CPU
  cycles made timeouts depend on emulation speed — and headless runs faster or slower
  than real time broke fades and attract-mode timers.
* `Time ($20C)` = seconds since 1904-01-01 (Unix time + 2082844800).
* Busy-waiting on `TickCount`: detect a spin (same tick returned repeatedly) and
  sleep a millisecond. ATC dropped from 100% to ~20% CPU.
* `Delay(ticks)`: pump the host while waiting.
* Frame limiters in the game are part of its physics. Falcon's sim is capped at 15 fps
  by its own code; raising it made controls lose authority. Keep the cap; make the
  *presentation* smooth instead (interpolation, docs/10).

## Sound

Find out which API the game uses (`traps.py`):

* **Sound Manager** (`SndPlay`, `SndNewChannel`, `SndDoCommand`…): decode `snd `
  resources (format 1/2, `bufferCmd`/`soundCmd` → SoundHeader, encode 0x00 standard /
  0xFF extended / 0xFE compressed) and mix. `tools/assets/snd2wav.py` is the reference.
* **.Sound driver** (`_Write`/`_Control` to refnum -4) — the old square-wave/four-tone/
  free-form synth API (ATC uses this path through `pack.c`/`fs.c`).
* **Direct hardware**: the game fills the 370-byte-per-tick **SoundBase ($266)**
  buffer itself (Mac Plus-style). Falcon did this with its own driver loaded from a
  code resource. Model `SoundBase` as a buffer in emulated memory, and each VBL
  (60 Hz) take 370 samples (every other byte on real hardware — check the driver)
  → host audio at 22,254 Hz (22,200 is close enough).
* **Busy-waits on "sound done"**: if sound isn't advancing, games hang in loops that
  poll a status byte. Falcon needed a status selector to report "done" before its
  sound path was real.
* Volume: `SdVolume ($260)` must be non-zero or some games stay silent by design.

Debug: dump the mixed output to a WAV (`HLE_WAVDUMP`) and listen; log every sound
call with its selector.

## Files

`fs.c` maps the File Manager onto a host folder:

* One emulated volume (`HLE_VOLNAME`). Each Mac file `Name` is stored as
  `save/Name` (data fork), `save/.rsrc/Name` (resource fork), `save/.finf/Name`
  (16-byte Finder info).
* Paths: strip volume/folder prefixes (`:`), sanitize characters illegal on the host.
* Implement both the old (`_Open`/`_Read`/`_Write`/`_GetEOF`…, param blocks in A0)
  and HFS (`_HFSDispatch` selectors) calls the app actually uses.
* **The app's own file is read-only.** Apps that save settings into their own
  resource fork (`AddResource`/`ChangedResource`/`UpdateResFile` on the app's
  refnum) get an overlay file `"<App> Settings.rsrc"` layered over the app's fork.
* `OpenResFile` of the app's own name (or of companion files that were merged into
  it — Falcon's "FoxFalc Sounds") should alias to the already-open app.
* Standard File (`_Pack3`: `SFGetFile`/`SFPutFile`) → the host's native file dialogs.
* Where to put saves: next to the game on Windows if writable, else the per-user data
  folder (`%APPDATA%`, `~/Library/Application Support`, `$XDG_DATA_HOME`).

Printing (`PrGlue`) can report "no printer" (PrError = -1 / iPrAbort) — apps handle it.
