# 04 — Inside the HLE runtime

`hle/` is small enough to read in an afternoon, and you should: every port extends it.

## Memory map (emulated, 16 MB, 24-bit addressing)

```
000000  low memory: exception vectors, Mac low-memory globals ($16A Ticks, $174 KeyMap,
        $830 Mouse, $904 CurrentA5, $A5x Resource Mgr, $9D6 WindowList ...)
100000  HEAP_BASE   master pointers (first 128 KB), then heap blocks
D00000  HEAP_END
E00000  STACK_TOP   stack grows down
E80000  A5_BASE     app globals below, jump table + QD globals pointer above
F00000  SCREEN_BASE 1-bit screen bitmap
FE0000  THUNK_BASE  [A-line word][RTS] per trap (GetTrapAddress results)
FFF000  MAGIC_ALINE RTE - the A-line vector points here
FFF010  MAGIC_RET   return address for C -> 68k callbacks
```

All addresses are masked to 24 bits (`MA()`), exactly like a 24-bit Mac: handle
master pointers keep their lock/purge/resource flags in the high byte and the app
may rely on that (`StripAddress` must mask with `Lo3Bytes` = `$00FFFFFF`).

Musashi's memory callbacks read/write this big-endian array directly (`mem.c`).

## Start-up (`main.c`)

1. Find the game folder; open the System file (fonts) and the app's fork into the
   Resource Manager chain (System → app → settings overlay).
2. `init_lowmem()` — set every low-memory global the app might read. Values
   matter: `ROM85 = $7FFF` (128K ROMs, **no** Color QD — change for colour games),
   `SdVolume ($260) = 7` (at 0 ATC never plays sound), `Lo3Bytes = $00FFFFFF`,
   `CurApName`, `MemTop`, `ApplZone`, `TheZone`, `RndSeed`, `DoubleTime`, `CaretTime`…
3. `load_segments()` — CODE 0 → zero the globals, copy each CODE resource into a
   locked handle, write loaded jump-table entries (`[seg][4EF9][addr]`), set A5,
   `CurrentA5`, PC = entry 0.
4. Main loop: `m68k_execute(100000)` then `hle_idle()` (pump host events, update
   Ticks/Time/Mouse/KeyMap low-memory, present the screen if dirty, run deferred
   completions, service audio).

## How a trap is handled (`trap.c`)

The A-line exception vector (`$28`) points at `MAGIC_ALINE`, which contains an `RTE`.
Musashi's instruction hook sees the CPU arrive there *before* executing the RTE:

```
on_aline():
  sp   = A7                 exception frame: [SR.W][PC.L][format/vector.W]
  pc   = rl(sp+2)           address of the A-line instruction itself
  word = rw(pc)             the trap word, e.g. A9A0
  if OS trap (bit 11 clear):    register-based
      os_trap(word)          args in A0/D0/D1..., results in D0/A0
      set CCR from TST.W D0  (callers branch on it without testing!)
      return to pc+2
  else Toolbox trap:            stack-based (Pascal)
      handler(word, &t)      t.sp = sp+8 = the caller's stack (args start here)
      handler sets t.pop = bytes of arguments to remove (RETW/RETL/RETB/POP)
      move the 8-byte exception frame UP by t.pop, so RTE resumes with args popped
      if auto-pop bit (0x0400): also drop the caller's return address and return there
```

Handlers use these macros (`mac.h`):

| Macro | Meaning |
|---|---|
| `ARGW(o)`, `ARGL(o)` | word / long argument at offset `o` from the top of the caller's stack (last pushed = offset 0) |
| `POP(n)` | procedure: pop `n` bytes of arguments |
| `RETW(n,v)` / `RETL(n,v)` | function: write the result into the slot just above the `n` argument bytes, pop `n` |
| `RETB(n,v)` | Boolean function: result in the **high byte** of the slot (`0x0100` = true) |

Toolbox dispatch goes through the managers in order (`misc_trap`, `qd_trap`,
`wm_trap`, `menu_trap`, `dlg_trap`, `ctl_trap`, `evt_trap`, `rs_trap`, `pack_trap`);
each sets `t->handled = 0` in its `default:` so the next one can try. If nobody
handles it: `[trap] unimplemented XXXX at PC` is logged **and nothing is popped** —
see gotchas #1.

## Calling the game from C: `call68k()`

Some Toolbox calls must run the app's code: control action procs (TrackControl),
WDEF/CDEF/MDEF definition procs, dialog filter procs, sound completion routines,
VBL tasks. `call68k(addr, resultBytes, "LWB", args...)` saves the CPU context, builds
a Pascal call frame on the stack (result space, args, return address = `MAGIC_RET`),
runs the CPU until it returns there, restores the context and returns the result.
It nests (up to 63 deep) and keeps pumping the host while the 68k code runs.

## Deferred completions

`hle_defer(addr, a0, dueTick)` queues a 68k routine to run later (e.g. an async
sound completion). `run_deferred()` calls them from `hle_idle()`.

## What the baseline does NOT do (yet)

These are the first things a different game is likely to need:

* **Color QuickDraw**: `CGrafPort`/`PixMap`/`GDevice`/`CTab`, `GetNewCWindow`,
  `NewGWorld`, `RGBForeColor`, palettes. `ROM85` says "no colour", so well-behaved
  apps don't try. See `06-quickdraw-display.md`.
* **VBL tasks**: `VInstall` returns noErr but never runs the task. Falcon MC reads its
  flight keys in a VBL task — see `07-input-sound-files.md` for the working design.
* **Time Manager** tasks (`InsTime`/`PrimeTime`): stubs.
* **Direct sound hardware** (SoundBase buffer, VIA): not modelled.
* **Gestalt**: answers are minimal; extend for what your app asks.
* **Far-model segments** and `_LoadSeg` of purged segments: all segments are
  preloaded and locked, which is what you want.

## Host layer

`host_win.c` (Win32 + GDI) and `host_sdl.c` (SDL2: macOS/Linux) implement the same
small API in `mac.h` (`host_init`, `host_present`, `host_pump`, `host_mouse`,
`host_keymap`, `host_cursor`, `host_file_dialog`, …). The emulator core never calls
the OS directly — keep it that way and new platforms stay cheap.

`host_present()` receives the 1-bit screen; it scales by whole pixels. For colour
games replace it with an 8-bit indexed path + palette (Falcon did) or a GPU
presenter (`10-remaster-and-beyond.md`).
