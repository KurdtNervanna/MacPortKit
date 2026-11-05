# 05 — Implementing traps

Bring-up is a loop: run → first unimplemented or misbehaving trap → implement it
correctly → run again. This page is how to make each iteration right the first time.

## The trap word

```
1010 1xxx xxxx xxxx   Toolbox trap (bit 11 set)
        ^ bit 10 = auto-pop: return to the caller's caller (used by glue)
         ^^^^^^^^^^ bits 0-9 = trap number (A800..ABFF; bit 9 IS part of the number:
                    AA2A, AB1D are distinct traps)
1010 0xxx xxxx xxxx   OS trap (bit 11 clear)
        ^^ bits 9-10 = flags (e.g. NewPtr/NewHandle "clear", "sys"; HFS "async")
             ^^^^^^^^ bits 0-7 = trap number
```

Normalize before dispatching: **Toolbox → `word & ~0x0400`; OS → `word & 0xFF`**.
Falcon MC's first dispatcher masked `~0x0600` for everything and silently turned
`AA2A`/`AA46` into other traps. (`(word & 0x800) ? word & ~0x400 : word & ~0x600`.)

## The loop

1. Run headless; read the first `[trap] unimplemented XXXX at PC`.
2. Name it: `python tools/analysis/trapnames.py XXXX` (if `?`, look it up in MPW
   `traps.h` — github.com/ksherlock/mpw `macos/traps.h` — or *Inside Macintosh*,
   and add it to `trapnames.py`).
3. **Look at how the app calls it**: `python tools/analysis/disasm.py app find XXXX 10 3`.
   Count what is pushed. Note whether it reserves result space (`SUBQ #2,SP` /
   `CLR.W -(SP)` / `SUBQ #4,SP`) and what it does with the result afterwards.
4. Implement it in the right manager file with the right pop count and result slot.
5. Re-run the same script. Look at the screen.

Pop count = the bytes of arguments pushed **after** the result space. The result
space itself stays (the caller pops it). Read it from the call site; that beats any
table, including this one.

## Calling conventions (where 90% of bugs came from)

### Stack-based (Pascal) Toolbox calls
* Caller: `[reserve result]`, push args **left to right**, `_Trap`.
* So the **last** argument is at the top: `ARGx(0)`. For `Foo(a: Long; b: Integer)`,
  `b` is at offset 0, `a` at offset 2. Result slot is at offset = total arg bytes.
* `INTEGER`/`CHAR` = 2 bytes, `LONGINT`/pointers/handles = 4, `Boolean` = 2 bytes
  with the value in the **high byte**, `Point` = 4 bytes **by value** (`v` high word,
  `h` low word), `Rect` and anything > 4 bytes = **by address** (a pointer).
  `VAR` parameters = by address.
* **Functions return on the stack, not in D0** — including `TickCount` (LongInt),
  `Random` (Integer), `Button`/`StillDown`/`GetNextEvent`/`PtInRect` (Boolean, high
  byte), `FrontWindow`. Falcon MC hung in its intro because `Random` returned in D0,
  and ignored all input because `GetNextEvent`'s Boolean was written to the low byte.

### Register-based OS traps
* Args in A0/A1/D0/D1, result in D0 (and A0 for pointer results).
* **Return with condition codes = `TST.W D0`.** Compilers emit `_NewPtr; BNE error`
  with no test. Falcon MC's sound driver allocated nothing until this was fixed.
  (`trap.c` sets N and Z from D0 on every OS trap return.)
* A few Toolbox-numbered traps are register based too: `HandToHand`, `PtrToHand`,
  `PtrToXHand`, `HandAndHand`, `PtrAndHand`, `Secs2Date`, `Date2Secs`
  (`Munger`, by contrast, is stack based). `reg_tool_trap()` in `trap.c` holds these.

### Selector dispatchers
`_Pack3` (Standard File) selector on the stack; `_Pack7` (NumToString etc.) selector
pushed last; `_HFSDispatch` selector in D0; `_Gestalt` selector in D0, response via
A0; `_QDExtensions`/`_SoundDispatch` selectors in D0. Implement per selector and log
unknown selectors loudly.

## Signatures we have verified at real call sites

Pop counts in bytes. "→" = result slot at that offset.

| Trap | Word | Args (pushed order) | Pop | Result |
|---|---|---|---|---|
| TickCount | A975 | — | 0 | Long → 0 |
| Random | A861 | — | 0 | Word → 0 |
| Button / StillDown / WaitMouseUp | A974 / A973 / A977 | — | 0 | Bool → 0 |
| GetMouse | A972 | VAR pt | 4 | — |
| GetKeys | A976 | VAR KeyMap (16 bytes) | 4 | — |
| GetNextEvent | A970 | mask.W, VAR evt | 6 | Bool → 6 (evt ptr at 0, mask at 4) |
| WaitNextEvent | A860 | mask.W, VAR evt, sleep.L, mouseRgn | 14 | Bool → 14 (evt ptr at 8) |
| EventAvail | A971 | mask.W, VAR evt | 6 | Bool → 6 |
| FrontWindow | A924 | — | 0 | Long → 0 |
| FindWindow | A92C | pt (by value), VAR window | 8 | Word → 8 |
| PtInRect | A8AD | pt (by value), VAR r | 8 | Bool → 8 |
| GlobalToLocal / LocalToGlobal | A871 / A870 | VAR pt | 4 | — |
| SetOrigin | A878 | h, v | 4 | — |
| PenSize | A89B | w, h | 4 | — |
| PenMode | A89C | mode | 2 | — |
| PenNormal | A89E | — | 0 | — |
| PenPat / BackPat | A89D / A87C | VAR pat | 4 | — |
| FrameArc | A8BE | VAR r, start.W, arc.W | 8 | — |
| RGBForeColor / RGBBackColor | AA14 / AA15 | VAR RGBColor (by address!) | 4 | — |
| CopyBits | A8EC | srcBits, dstBits, VAR srcRect, VAR dstRect, mode.W, maskRgn | 22 | — |
| DrawPicture | A8F6 | picHandle, VAR dstRect | 8 | — |
| GetNewDialog | A97C | id.W, storage, behind | 10 | Long → 10 |
| GetDItem | A98D | dlg, item.W, VAR type, VAR h, VAR box | 18 | — |
| ModalDialog | A991 | filterProc, VAR itemHit | 8 | — (itemHit ptr at 0) |
| GetResource | A9A0 | type.L, id.W | 6 | Long → 6 |
| Gestalt | A1AD | D0 = selector | regs | D0 = err, A0 = response |
| Secs2Date / Date2Secs | A9C6 / A9C7 | D0 secs, A0 rec / A0 rec | regs | D0 |

(Falcon also used `A8BE` with a **rect pointer** where a table suggested a rect by
value. Always check.)

## Data structures live in emulated memory

Games poke Toolbox structures directly: `portRect` of a GrafPort, `contrlValue` in a
ControlRecord, the `visRgn` handle, `WindowRecord.refCon`, dialog item lists. So the
runtime must create **real records in their real layouts** in 68k memory
(offsets in `mac.h`: `GP_portRect = 16`, etc.), and must read them back from memory
rather than caching copies in C. When something "works in the HLE's C state but the
game sees the old value", this is why.

Things that bit us:
* `GetNewDialog` must **not** change the current port (the app saves/restores it).
* `OpenPort` must allocate fresh visRgn/clipRgn handles — reusing stack garbage
  got them freed later by `ClosePort`.
* `OffsetRect`, `InsetRect`, `SectRect`, `UnionRect`… as no-ops made every roster row
  draw at the same place. "Trivial" traps are not optional.
* `NewGWorld` must keep the caller's bounds rect (including negative origins).
* Heap: `GetHandleSize` must return the **logical** size the app asked for, not the
  rounded allocation — Falcon's settings file grew on each save until fixed.

## Stubs: when they are OK

A stub is fine when the trap has no observable effect for this app *and* you pop
exactly the right number of bytes (`POP(n)`). Log stubs the first time they are hit.
Never let a stub fall through to `default:`. Examples of fine stubs: `SystemTask`,
`InitCursor`, `MoreMasters`, `SetApplLimit`, `UnloadSeg`, `HiliteMenu` (if you draw
menus natively).

Examples of stubs that were NOT fine and cost days: `EraseRect`/`PaintRect`/`FillRect`
(splash screen colours wrong), `SetOrigin` (cockpit coordinate system shifted, canopy
missing), `ClipRect`, `OffsetRect`, `SetEntries` vs. palette, `CopyBits` writing only
8-bit destinations (all 4-bit cockpit sprites silently dropped).

## After implementing: audit

```
python tools/analysis/trap_audit.py app.bin port/src
```

Run it at milestones. Falcon MC's eventual state: 0 unhandled traps over a full
boot + attract mode (100 distinct traps, 9.2M trap calls), later 0 vs. the whole
static list.
