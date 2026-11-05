# 10 — Beyond the faithful port: remasters, widescreen, byte-matching

Do these only after a faithful v1 ships. Keep the original look one keypress away.

## A GPU remaster on top of the HLE (Falcon MC v2)

The game logic keeps running on the 68k core; a new renderer *observes* it.

1. **Find the game's camera and geometry in memory.** Falcon: the view matrix (Q15,
   at an A5 offset), camera position, focal length and screen centre are globals the
   game computes every frame; per-face camera-space vertices pass through one
   transform routine. Locate them with the decomp + PC counters.
2. **Capture per frame** at a fixed PC (e.g. the start of the scene-draw dispatcher):
   read camera + every face the game is about to draw (vertices, colour/fill code).
3. **Render it yourself** (OpenGL 3.3 in Falcon): proper perspective, MSAA, a sky
   shader from the view matrix, lighting. Composite the game's own 2-D layer (cockpit,
   HUD) on top, with a **mask** of which pixels the game's 3-D fill wrote (track writes
   from the fill routines' PC range) so your 3-D shows through only there.
4. **Smooth motion**: the sim runs at its native rate (Falcon: 15 fps); interpolate
   between captured frames (match faces, slerp rotation) and present at the display's
   refresh. Raising the sim rate itself changed the physics — rejected.
5. **Widescreen**: a vector/wireframe or self-rendered 3-D scene extends naturally;
   fill the extra side area with generated console art.
6. Switchable looks (Original / Classic HD / Modern) and a Video menu; a GDI fallback.

Vector renderers are the best case: Falcon's 3-D was a custom wireframe/span
rasterizer, which meant resolution and aspect were "just" projection parameters.

## Upscaling 2-D art

Pixel-art scalers (Scale2x, xBR) for line art and menus; avoid them on dithered
art (they amplify the dither) — blend checkerboards instead.

## Native controllers and menus

* Map a gamepad onto whatever the game reads (KeyMap bits, mouse position).
* A native menu bar built from the app's `MBAR`/`MENU` resources, with the app's own
  menu-update routine called (via `call68k`) before showing it, makes the port feel
  native. In-game, menu picks become Command-key presses in the KeyMap.

## Asset extraction for other engines

`tools/assets` gets PICT/snd out. Private formats (Falcon's `OBJS` 3-D models: header,
16-bit fixed-point vertices, face records `[flags][colour][N][(N+1) refs, idx = ref>>4]`)
are decoded from the loader code in the decomp. Useful for remasters and for
documentation; not needed for the port.

## Byte-matching decompilation (preservation-grade)

What Falcon MC proved, for anyone who wants an N64-decomp-style project:

* **Identify the compiler** (fingerprints in `code_extract.py`): Falcon = THINK C 5.
* **Assembly-level matching is automatable**: GNU `m68k-linux-gnu-as` produces the
  same encodings as THINK C for hand-written routines, so disassembly → assembler
  source → reassemble → compare needs no emulator. Emitting every instruction with a
  label, assembling, and falling back to raw `.byte` for the few instructions GAS
  encodes differently (dual encodings like `add #imm` vs `addi`) converged to
  **46/46 segments byte-identical** — a fully recompilable assembly reconstruction.
* Gotchas for GAS: `--register-prefix-optional -m68020`; `moveq` immediates are signed;
  `exg` has no size suffix; `.b` branches are `.s`; turn PC-relative operands into
  labels; separate switch tables (data) from code using Ghidra's code map.
* **C-level matching** of compiled functions needs the original compiler in the loop
  (THINK C running inside Mini vMac, source staged onto an HFS floppy image with
  `hformat`/`hcopy`, the user clicking Build, the built app copied back out). And
  **per-function matching in isolation is nearly impossible**: only 7 of ~1,040
  Falcon functions were self-contained; the rest reference A5 globals and jump-table
  slots fixed at link time. The unit that can match is a **whole segment** (one
  translation unit), compared with relocations masked.
* THINK C inline-asm form that matches: `short f(void){ asm { ... } }` (block inside
  the body, `0x` hex, generic mnemonics), and *Remove Objects* before each build or a
  stale object is linked.

Verdict from experience: assembly-level matching is cheap and worth doing if you want
a verified disassembly; C-level matching is a long manual project. For a playable
port, the HLE path is far better value.
