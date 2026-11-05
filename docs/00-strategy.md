# 00 — Strategy: how to port a classic Mac program

## The options

| Approach | What you ship | Fidelity | Effort | Verdict |
|---|---|---|---|---|
| **Emulator bundle** (Mini vMac / Basilisk II + ROM + System + game) | an emulator, a ROM you may not redistribute, a System disk | perfect | low | Fine for personal use. Not a port: Mac UI around the game, ROM licensing, no modern input/resolution. Falcon MC started here (Mini vMac + AntiMicroX for a joystick). |
| **Full rewrite** from the decompiled C | new code | as good as your understanding | very high | Only worth it for small pure-logic programs. Every subtle behaviour must be rediscovered. |
| **Byte-matching decomp** (N64-style) then port | recompiled original source | perfect | extreme, needs the original compiler in a loop | Excellent for preservation, poor value per hour for a playable port. See `10-remaster-and-beyond.md`. |
| **HLE: original code + re-implemented Toolbox** | your runtime; the user supplies the game | original game logic exactly | moderate, front-loaded | **What this kit does.** |

## Why HLE wins for classic Mac software

Classic Mac apps are unusually well suited to high-level emulation:

1. **All OS access is through A-line traps.** Every Toolbox/OS call is a single
   16-bit instruction word `0xAxxx` that the 68k raises as an exception. One hook
   catches every interaction between the program and the system. There is (almost)
   no other way in.
2. **The interface is documented.** *Inside Macintosh* specifies every trap,
   structure layout and low-memory global. Your C code implements a spec, not a guess.
3. **Programs use a small subset.** Falcon MC calls ~200 distinct traps, ATC ~205.
   The same ~150 cover most games (Memory, Resources, QuickDraw, Windows, Menus,
   Dialogs, Events, Files, Sound, utilities).
4. **No ROM needed.** You never run Apple code; you only replace it. The user's own
   System file is read for its *fonts* (data), nothing else.
5. **The game's logic is untouched**, so physics, AI, timing quirks and scoring are
   the originals. You only fix the platform layer.

The cost is front-loaded: the first game teaches you the Toolbox. That is why this
kit ships a runtime that already runs one complete game.

## The phases

| Phase | Output | Doc |
|---|---|---|
| Acquire | the app's resource fork + a System file, readable on your PC | 01 |
| Recon | segments, jump table, globals size, compiler, trap list | 02 |
| (optional now, essential later) Decompile | one C file of the whole program with traps/calls named | 03 |
| Bring-up | the runtime boots the app to its first screen | 04, 05 |
| Make it right | every screen matches the reference | 05, 06 |
| Make it playable | input, sound, saving | 07 |
| Make it robust | headless tests, crash diagnostics | 08 |
| Ship | builds for each OS, no game data inside | 09 |
| Beyond | remaster rendering, widescreen, controllers | 10 |

Typical effort with an AI agent doing the typing: a B&W Toolbox-heavy app like ATC
reached "fully playable, packaged for three OSes" in about a day of focused
sessions once the runtime existed. Falcon MC (colour, custom renderer, custom sound
driver, first time building any of this) took a few weeks of sessions to v1.0.

## Decision checks before you start

* **Is it 68k?** `code_extract.py` says "No CODE 0" for PowerPC apps (their code is
  a PEF container in the data fork). Fat apps have both — use the 68k part.
* **Near or far model?** Far-model segments (header word 0xFFFF) need loader work.
* **B&W or colour?** Colour games need Color QuickDraw in the runtime (Falcon MC's
  list in `06-quickdraw-display.md`).
* **Does it touch hardware?** Look for writes to `$EFE1FE` (VIA), `SoundBase ($266)`
  buffers, SCC, or ROM addresses. Falcon fills the Mac Plus sound buffer directly;
  the HLE had to model that buffer and the VBL that drains it.
* **Copy protection?** Manual look-up questions are just dialogs and run fine; key
  disks / nibble checks need thought. Check what the user owns and keep any analysis
  private.
* **Does a version exist without speech/network dependencies?** (ATC 5.03 was chosen
  over Near Miss 1.0 because it doesn't need MacinTalk.)
