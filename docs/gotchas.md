# Gotchas — symptom → cause → fix

Every entry cost real time in the Falcon MC or ATC port. Search this page for your
symptom before debugging. Add new entries in the same format.

## Traps and calling conventions

1. **Crash or weird behaviour long after some call; `rts` into garbage.**
   → An unimplemented trap hit `default:`, which returns *without popping the
   arguments* → stack skew. → Implement it; never leave a trap unhandled; run
   `trap_audit.py`; treat every `[trap] unimplemented` log line as a bug.
2. **A trap "works" but the game acts as if it returned nothing.**
   → Toolbox *functions* return on the **stack**, not D0 (`TickCount`, `Random`,
   `Button`, `GetNextEvent`, `FrontWindow`…). → `RETW/RETL/RETB` at the right offset.
3. **All input ignored; every Boolean is false.**
   → Mac Booleans are in the **high byte** of the result word. → `RETB` writes `0x0100`.
4. **Intro/attract loop never ends.** → `Random` returned in D0 instead of the stack;
   a probabilistic retry loop never succeeded.
5. **Fades/timeouts fire instantly or never.** → `TickCount` result in the wrong place
   (stack!), or Ticks derived from CPU cycles. → stack result; 60 Hz wall clock.
6. **`GetNextEvent` reads garbage events.** → EventRecord pointer is at `argbase+0`,
   the mask at `+4` (last pushed = top). `WaitNextEvent`: event pointer at `+8`.
7. **Trap behaves like a different trap.** → Wrong normalization: Toolbox numbers
   include bit 9 (`AA2A` ≠ `A82A`). Toolbox: `word & ~0x400`; OS: `word & 0xFF`.
8. **Tables lie.** `A924` is `FrontWindow` (a table said `SystemEvent`); `GetMouse`
   (`A972`) and `Button` (`A974`) were swapped in one table. → Verify names against
   call sites (what is pushed, what is done with the result).
9. **`RGBForeColor`/`RGBBackColor` corrupt the stack.** → The 6-byte RGBColor is
   passed **by address** (records > 4 bytes always are). Pop 4, not 6.
10. **Allocation "fails" though memory is free; error paths taken at random.**
    → OS traps must return **CCR = TST.W D0**; code does `_NewPtr; BNE` with no test.
11. **A custom drawing call crashes a frameless routine.** → Pop count wrong for an
    unusual trap (Falcon `A8BE` FrameArc: rect *pointer* + 2 words = 8). Count the
    pushes at the call site.
12. **`Secs2Date`/`Date2Secs` misbehave.** → They're register based (D0/A0) despite
    Toolbox numbers.

## Memory and low memory

13. **`StripAddress` glue returns 0 / pointers break.** → `Lo3Bytes ($31A)` must be
    `$00FFFFFF`.
14. **Game is completely silent.** → `SdVolume ($260)` = 0. Set 7.
15. **Settings file grows on each save / data misaligned.** → `GetHandleSize` returned
    the rounded allocation size. Return the logical size.
16. **Freed handles that the game never freed.** → `OpenPort` reused uninitialised
    handles for visRgn/clipRgn; `ClosePort` freed them. Allocate fresh regions.
17. **The game reads a low-memory global you never set** (`KeyMap $174`,
    `Mouse $830`, `MBState $172`, `Ticks $16A`, `ROM85 $28E`, `CurApName $910`…)
    → keep every one current in `hle_idle`, and initialise the static ones.

## Windows, dialogs, menus

18. **Dialog draws into the wrong window / game's port corrupted after a dialog.**
    → `GetNewDialog` must not change the current port (the app does `GetPort`
    around it).
19. **Every row/LED digit drawn at the same place.** → `OffsetRect` (and Inset/Sect/
    Union/SetRect) were no-ops. Implement all the rect utilities.
20. **`ModalDialog` freezes.** → It spins waiting for events while other windows need
    updates; pump the host and process update events inside it.
21. **Alerts return immediately.** → An alert must block: re-execute the trap until
    the user answers (Falcon's `g_trap_repeat`) or run a modal loop in C.
22. **Print path crashes on a B&W machine.** → `GetCWMgrPort` is called on the print
    path even without Color QD. Implement it anyway.
23. **Clicks on buttons don't register.** → Tracking loops poll `StillDown`/`WaitMouseUp`;
    the scripted click released before the first poll. Hold the button for a while.

## QuickDraw and display

24. **Sprite looks 2× wide, left-justified, every other column white.** → 8-bit pixels
    written into a 4-bit PixMap. Decode/convert to the destination's depth and pack.
25. **Instruments / sprites missing, no error.** → CopyBits only wrote 8-bit
    destinations; the game composites into a 4-bit offscreen. Implement `px_put` for
    1/2/4/8 bpp.
26. **Top of the view missing; things drawn at negative coordinates vanish.**
    → `SetOrigin` was a stub. Offset portRect *and* the PixMap bounds. Then make the
    presented region match the new coordinate system (Falcon 342 → 364 rows).
27. **HUD tape graphics come out as solid boxes.** → CopyBits ignored transfer modes;
    implement Boolean modes 0–7 (srcOr with index 0 transparent).
28. **Instrument strips show the wrong part of the art.** → CopyBits clipped srcRect
    to the source bounds; the game intentionally reads outside them. Don't clip src.
29. **Colours wrong after a picture loads (magenta drift, grey sky).** → Each PICT
    overwrote the palette. Use the screen's `pltt` (`GetNewPalette` +
    `GetEntryColor`/`SetEntries`) and map PICT colours to the nearest palette entry;
    use the standard Mac 16-colour CLUT for nil-cTab offscreens.
30. **Splash screen has the wrong background.** → `EraseRect`/`PaintRect`/`FillRect`
    were no-ops.
31. **Colour port treated as a 1-bit BitMap.** → `portVersion = 0xC001` (offset 6) not
    set by every colour port creator.
32. **Window too small/blurry on Windows; margins.** → Not DPI aware; presenting the
    whole emulated screen instead of the game's window rect.
33. **Thin 1-px lines turn grey when scaled.** → Over-eager de-dither filter. Blend only
    true checkerboards.
34. **Pen/colour from one port leaks into another.** → Pen, fore and back colour are
    per-port state; save/restore on `SetPort`.
35. **FONT text has garbage after the last glyph.** → `owTable` may lack the final -1.

## Input, timing, sound

36. **The "keyboard routine" you wired controls the camera, not the plane.**
    → Find the real input path by tracing reads. Falcon's flight keys came from a VBL
    task latching `KeyMap $174`. Run VBL tasks.
37. **Game hangs waiting for sound to finish.** → Sound never advances in the HLE;
    report "done" for status queries until real sound exists.
38. **Busy-wait burns 100% CPU.** → Detect repeated `TickCount` with no change and
    sleep 1 ms.
39. **Raising the frame rate breaks the controls.** → The game's frame limiter is part
    of its physics tuning. Keep it; interpolate presentation instead.
40. **A key needed for play is also the abort key** (Falcon: Option gated the number-
    row keys and also aborted the mission). → A documented, switchable code patch in
    the loader (never edit the user's file).

## Build, environment, process

41. **"My fix didn't change anything."** → Stale executable: `build.bat` launched from
    Git Bash silently did nothing (vcvars spawns a shell); or the exe was still running
    and the link failed. Build from PowerShell (`cmd /c ".\build.bat"`), kill the exe
    first, check the timestamp.
42. **Emulation got 40× slower "for no reason" (and slower with a bigger
    environment).** → `getenv()` in the per-instruction hook. Cache flags once.
43. **C escape sequences broken after an edit** (`"\n"` became a real newline, `"\0"` a
    NUL). → Shell heredocs through some agent tool layers mangle escapes. Write C with
    a file-writing tool, then splice; re-read the result.
44. **Headless runs pass/fail randomly.** → Fixed-time scripted clicks race the
    emulator speed. Use state-driven navigation.
45. **The emulator's floppy image can't be written / changes don't appear.** → Mini
    vMac locks its disk images while running. Shut the emulated Mac down first.
46. **Ghidra functions stop at the first trap.** → Apply `patch_sleigh.py` and delete
    the `.sla` so it recompiles.
47. **Ghidra script errors with Python.** → Ghidra 12 has no bundled Jython; use Java
    scripts. `ProgramContext` is in `ghidra.program.model.listing`.
48. **`analyzeHeadless.bat` argument quoting breaks.** → Call it directly from bash
    with `cygpath -w` paths; don't wrap in `cmd //c`.
49. **Decompiled names mislead.** → A function was named from instruction statistics
    and was wrong. Name only what you've traced.
50. **Pushing to GitHub fails with "fatal error in commit_refs_rules".** → Server-side
    transient; retry.
