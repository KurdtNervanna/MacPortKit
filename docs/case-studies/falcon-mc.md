# Case study: Falcon MC (1992) → native Windows

**Original:** Spectrum HoloByte's colour Macintosh F-16 simulator, v1.01.
THINK C 5 / Symantec C++, 46 CODE segments (~193 KB of code in a 2.4 MB resource
fork), 535 jump-table entries, 36,944 bytes of A5 globals, ~1,040 functions,
~200 distinct traps. Needs 68020, 32-bit Color QuickDraw, 256-colour 640×480.
Its own 3-D vector/span renderer, its own sound driver writing the Mac Plus sound
buffer, input latched by a VBL task. (Source repository is private.)

## Timeline (how it actually went)

1. **Emulator bundle first** (Mini vMac + Mac II ROM + System 6.0.8 + AntiMicroX
   joystick mapping). Worked, but not a port. Basilisk II was not viable (needs a
   512 KB+ ROM; only a 256 KB Mac II ROM was available).
2. **Recon + feasibility**: resource inventory, CODE 0, trap scan; data (models,
   PICTs, sounds) is portable, code is a modest ~50-60k instructions → feasible.
3. **Ghidra combined decompile** with the SLEIGH A-trap patch, jump-table synthesis
   and pinned A5: 1,043 functions, globals resolved; trap and thunk naming.
4. **Engine RE**: trig LUT (quarter-wave Q15, 0x4000 = 360°), rotation matrix,
   projection, a **vector** renderer (clipped Bresenham into a 4-bpp offscreen).
   Assets decoded (OBJS 3-D models → .obj, snd → WAV, PICT → PNG).
5. **Byte-matching detour**: assembly-level reproduction of all 46 segments,
   byte-identical, fully automatic with GNU as; C-level matching blocked on needing
   THINK C in an emulator per build and on link-time dependencies. User stopped it.
6. **HLE ("interpreter hybrid")**: Musashi + Toolbox in C. Boot → title in a day;
   then 9.2M traps with 0 unimplemented through attract mode; then rendering,
   input, menus, dialogs, the pilot roster, flight.
7. **The long tail** — each one a gotcha now:
   Pascal stack returns (Random, TickCount, GetNextEvent Boolean high byte),
   GetMouse/Button swapped, FrontWindow mis-named, CopyBits depth (4-bpp no-op),
   SetOrigin stub (canopy), palettes per screen, 4-bpp PICT packing, OS-trap CCR
   (sound driver), VBL tasks (flight keys), getenv in the hook (40× slowdown),
   window coordinates, dialogs as real records.
8. **v1.0.0 release** (faithful): exe + launcher, no game data; native menu bar from
   MBAR/MENU; save files via the File Manager mapping; credits line added in the
   credits PICT at run time.
9. **v2.0.0 remaster** on a separate branch: OpenGL renderer fed by per-frame captures
   of the game's camera and faces, MSAA, sky shader, widescreen consoles,
   interpolated smooth motion, switchable Original/Classic HD/Modern looks.

## Lessons that generalize

* The HLE got to "playable" far faster than any decomp path would have. The decomp
  was indispensable as a *reference* while debugging.
* Most "the game is broken" theories were HLE bugs. Diagnostic patches (skip the
  collision check) are fine for proving a hypothesis, then remove them.
* Reference screenshots from the real game were the only way to know the cockpit
  was incomplete.
* Write every finding down with addresses; the notes outlived many sessions.
* Remaining gap at v1: one ADI detail and a few instrument shapes drawn by
  hand-written asm whose behaviour depends on composite modes; head-to-head
  (AppleTalk) networking not implemented.
