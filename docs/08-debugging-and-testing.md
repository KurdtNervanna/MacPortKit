# 08 — Debugging and testing

The runtime is a debugger you control completely: you see every instruction, every
trap and every memory write. Build the instruments once; keep them behind cached
flags; commit them.

## Headless scripted runs (the default way to test)

```powershell
$env:HLE_HIDDEN = "1"
$env:HLE_SAVE   = "C:\scratch\save"          # never test against the real save folder
$env:HLE_SCRIPT = "3000:click 256 200;6000:key \r;9000:dump C:\scratch\a.pbm;9500:quit"
.\build\hle.exe "C:\games\MyGame"
python tools\assets\pbm2png.py C:\scratch\a.pbm C:\scratch\a.png
```

Script actions: `click x y`, `down x y`, `move x y`, `up x y`, `key c` (`\r` `\t` `\b`),
`cmd c` (Command-key), `dump file`, `quit`. Times are ms since start.

* **Fixed times race the emulator.** When timings get tight, add *state-driven*
  steps: "wait until global X == screen id N", "wait until pixel (x,y) is colour C".
  Falcon's `AUTOFLY` navigated menus by detecting each screen and reached the
  cockpit reliably in ~15 s; fixed-slice clicks broke whenever performance changed.
* Make a headless "fast" mode that presents rarely (Falcon: present every 32 slices
  → ~1,300 slices/s instead of ~200).
* Long runs: Windows tool shells often cap foreground commands at 2 minutes; run the
  game from PowerShell with a longer timeout, or in the background with a log file.
* Don't steal the user's focus: a `NOFOCUS`/hidden flag so headless runs don't pop
  the game window over their desktop.

## Baseline regression test

Keep one known-good scripted run per milestone and re-run it after every change to
the runtime — e.g. for this kit: ATC title → main board (`hle/` must still produce
the main board screenshot). If a "fix" for one screen breaks another, you see it the
same minute. Falcon had a SetOrigin fix that improved the cockpit and broke the title
screen until it was scoped to the flight screen; only a re-run of the title script
caught it.

## When it crashes: the bad-jump report

`trap.c` checks every PC. Jumping outside code prints:

```
*** bad jump to 00000000 (last good 1F0632: rts)
recent traps: A9A0@1F05F0 A029@1F05F8 ... (last 24)
A5=E80000 SP=DFFF20 A6=DFFF40 A7(0..3)=...
```

Read it backwards: **an `rts` into garbage = the stack was skewed** → look at the
last few traps' pop counts. The single most common cause in both ports.

## Instruments worth adding (all used in anger)

| Instrument | How | Found |
|---|---|---|
| Trap trace | `HLE_TRACE=1` (every trap with PC, SP) | wrong pop counts, missing traps |
| Instruction log | `HLE_PCLOG=<trapcount>,<n>`: n instructions with D0/D1/A0/A1/SP after N traps | what a busy loop polls |
| Trap histogram | count per trap word, print top 30 at exit | what the game is "doing" while stuck |
| **PC counters** | count hits of a few PCs (frame start, renderer, draw-line), print at exit | "is this code path reached at all?" — Falcon: renderer=0 → the scene wasn't drawn because the flight ended on frame 1 |
| **Write-watch** | in the memory write hook, if address ∈ [lo,hi) log PC + value (arm after frame N) | who draws a pixel; who clobbers a global |
| Read-watch | same for reads | which global a routine actually consumes |
| Offscreen dump | write any PixMap as BMP/PNG | sprite/depth bugs |
| CopyBits audit | per blit: src/dst base, depth, rects, mode, pixels written | the 4-bit "wrote=0" bug |
| Fill/clip/line monitors | log rects of fills, ClipRect, lines in a phase | missing background, clip regions |
| Resource log | every GetResource type/id/result | missing or wrong resources |
| File log | every File Manager call with path | save/load problems |
| Sampling profiler | sample PC from a timer thread; histogram by function | hot paths; the getenv-in-hook disaster |

Gate each with a flag read **once** at start (an env cache). Per-instruction cost of
a disabled instrument must be a single predictable branch.

## Method for hard visual bugs (from Falcon's cockpit)

1. Get a reference image of the same moment (user's emulator screenshot).
2. Diff concretely: which pixels/rows/objects differ.
3. Dump the offscreen the game draws into — is the missing thing *in the buffer*?
   * Not in the buffer → it was never drawn: find the drawing code (write-watch on
     where it should be, PC counters on candidate routines).
   * In the buffer but not on screen → presenting/compositing bug (CopyBits, palette,
     region, scaling).
4. For "never drawn": a stubbed trap upstream is the usual cause (PaintRect,
   SetOrigin, ClipRect…). List every trap called during the frame with a stub.
5. Rule out with data and **write each ruled-out hypothesis down**. Falcon's notes
   show ~35 flight captures where each one eliminated a cause (resolution, GDevice
   bounds, view mode, flight state) — the remaining gaps were then known to be in the
   game's own asm shape-drawing code, not configuration.

## When the game seems to be wrong

It almost never is. Before patching game code, prove the original behaves
differently on a real Mac (reference screenshot or emulator). Falcon's "Instant
Action ends immediately because the mission has no enemies" theory was wrong; the
real cause was a collision check failing on frame 1 because of a wrong start
altitude — itself a symptom of HLE behaviour. A temporary *diagnostic* patch (skip
the collision check) proved the renderer worked; it was then removed.

## Capturing the real window

`tools/win/capture.ps1 <pid> out.png` (PrintWindow) grabs the actual window,
including scaling and the host presenter — dumps of the emulated framebuffer don't
show presenter bugs. `tools/win/sendkeys.ps1` pokes keys at a running window.
Desktop-automation agents often can't control a loose `.exe` (not a registered app)
— rely on the built-in script hooks instead.
