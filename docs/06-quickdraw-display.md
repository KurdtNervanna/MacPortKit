# 06 — QuickDraw, colour, pictures and presenting

## 1-bit QuickDraw (the baseline runtime)

`hle/src/qd.c`, `rgn.c`, `font.c` implement classic QuickDraw over 1-bit bitmaps:

* **Ports** are real 108-byte GrafPorts in emulated memory. `thePort` lives in the
  QD globals at `*(A5)`. Every drawing call reads the port's fields from memory
  (portBits, portRect, clip/vis regions, pen, text state, fg/bk colour).
* **Regions are real** (10-byte header + data); clipping = portRect ∩ visRgn ∩
  clipRgn. The Window Manager maintains visRgns, so overlapping windows and update
  events behave exactly as on a Mac (ATC's schedule window relies on this).
* **Patterns** are 8×8, aligned to the port's origin; **transfer modes** srcCopy,
  srcOr, srcXor, srcBic and the not- variants, pattern modes for fills.
* **Fonts** are the System file's bitmap `FONT` resources (Chicago 12, Geneva 9,
  Monaco 9…), drawn with the font's own strike, location and offset/width tables —
  pixel-identical text. Gotcha: some FONT `owTable`s lack the final `-1` entry.
* **Pictures**: `DrawPicture` parses PICT v1/v2 opcodes (bitmaps + common vectors).

## Adding Color QuickDraw (what Falcon MC needed)

Set `ROM85` to indicate Color QD (`$3FFF`), answer `SysEnvirons`/`Gestalt` with a
colour machine, then implement, in roughly this order:

1. **GDevice + main screen**: `GetMainDevice`/`GetGDevice`/`SetGDevice`,
   `TestDeviceAttribute`, `gdRect`, a PixMap for the screen (8-bit is simplest).
2. **CGrafPort**: `OpenCPort`, `GetNewCWindow`/`NewCWindow`. Colour ports are
   detected by `portVersion` = **`0xC001`** at offset 6 (the high two bits of the
   word where a BitMap would have `rowBytes`). Every creator of a colour port must
   set it, or CopyBits will treat the port as a 1-bit BitMap.
3. **PixMaps of every depth** (1/2/4/8): implement `px_get(base,rowBytes,depth,x,y)`
   and **`px_put` for every depth**. Falcon drew its whole cockpit as ~900 small
   CopyBits into a **4-bit** offscreen; a blitter that only wrote 8-bit destinations
   silently dropped every instrument.
4. **Colour state**: `RGBForeColor`/`RGBBackColor` (pointer to a 6-byte RGBColor!),
   `ForeColor`/`BackColor` with the old QD colour constants (30 = white, 33 = black,
   205 red, 341 green, 409 blue, 273 cyan, 137 magenta, 69 yellow) mapped to the
   nearest palette index. Pen/fore/back are **per port** — save/restore on SetPort.
   `PenPixPat` (colour pen patterns) mattered for Falcon's gauge needles.
5. **Offscreens**: `NewGWorld` (allocate PixMap + buffer from the caller's bounds,
   keep the bounds exactly, return via VAR), `GetGWorld`/`SetGWorld`, `LockPixels`.
6. **Palettes**: `GetNewPalette('pltt')`, `GetEntryColor`, `SetEntries`, `ProtectEntry`,
   `GetCTSeed`. Falcon is a 16-colours-per-screen design: each screen loads its own
   `pltt`; PICTs are mapped to the nearest colour of the *current* palette, not
   allowed to overwrite it. In flight, the offscreen has a nil colour table → the
   **standard Mac 16-colour palette** is correct.
7. **CopyBits for real**: all depths, scaling src→dst rects, Boolean modes 0–7 (srcOr
   with index-0-transparent made the HUD tapes work), 1-bit sources colourised with
   fg/bk, mask regions. And **do not clip srcRect to the source bounds** — Falcon's
   instrument strips deliberately read outside them.
8. **SetOrigin**: offset the port's portRect *and* its PixMap bounds. Falcon's
   cockpit sets origin (0,-42) and draws the canopy at negative rows; with a stub, the
   whole top of the cockpit vanished. And the presented height had to grow with it
   (342 → 364) — change coordinate systems and presentation together.
9. **PICT decode into the destination depth.** Decoding an 8-bit PICT into a 4-bit
   sprite PixMap without packing produced "2× wide, left-justified, every other
   column white" sprites. Symptom → cause is so specific it is worth memorizing.

Shape traps (FrameOval/PaintArc/…) may not be enough: Falcon draws most gauges with
its **own** code writing pixels into the offscreen. That is good news — it just works
once the offscreen and CopyBits are right.

## PICT decoding

`tools/assets/pict2png.py` is a readable reference decoder (v1 and v2, BitsRect,
PackBitsRect, DirectBits 16/32, colour tables, op-length table for skipping vectors).
Port the same logic to C for `DrawPicture`. Key details:

* v1: 1-byte opcodes; v2: `0x0011 0x02FF`, 2-byte opcodes, **data padded to even**.
* BitMap vs PixMap: high bit of `rowBytes`. PixMap → 46-byte record + colour table.
* PackBits row byte-count is 2 bytes if `rowBytes > 250`, else 1; rows with
  `rowBytes < 8` are stored unpacked.
* Colour table: `ctFlags & 0x8000` → index = position, else index = `value` field.

## Presenting to the host window

* Keep the Mac framebuffer at its native size (512×342 classic, 640×480 colour) and
  **scale on present**. Integer scaling for 1-bit (sharp); for colour, Falcon used
  `StretchDIBits` and later an OpenGL presenter.
* **DPI awareness** on Windows (`SetProcessDPIAware`) or the window is blurry and
  mis-sized.
* **Present only the game's active region** (its main window rect), not the whole
  emulated screen, so the window fills correctly with the right aspect.
* **Dithered art** (checkerboards meant to look grey on a CRT) looks harsh when
  scaled. Blend only true 2×2 checkerboards; a general smoothing filter greyed every
  1-pixel line in Falcon's HUD.
* Present when dirty and at most ~60 Hz; never block the emulation on vsync in the
  same thread without pacing.

## Debug tools for drawing problems

* Dump the **offscreen** buffers, not just the screen (`dump_pixmap_bmp(base,
  rowBytes, depth, w, h)` — write one).
* Log every CopyBits with src/dst ports, rects, depth, mode and pixels actually
  written (`HLE_CBLOG`; Falcon's `CBAUDIT` counted writes per blit and exposed the
  4-bit no-op immediately).
* **Write-watch** a pixel/row range: log the PC of every write. That's how Falcon's
  sky-fill routine and the missing canopy were traced (docs/08).
* Compare against a reference screenshot of the same scene. Describe the diff in
  numbers (rows, columns, colours).
