# AGENTS.md — playbook for an AI agent porting classic Mac software

You are helping a person port a classic 68k Macintosh program to a modern OS using
this kit. This file is your operating manual. It is written from two real ports
(Falcon MC, Air Traffic Controller); every rule below exists because breaking it
cost hours. Read it fully once, then keep `docs/gotchas.md` open while debugging.

---

## 0. First session checklist

1. **Find out what the human has.** The game file(s) in some container (`.sit`,
   `.dsk`/`.img`, `.dmg`, `.bin`, AppleDouble pairs), a System 6/7 System file (for
   fonts), the manual, and — very valuable — **reference screenshots or a working
   emulator** (Mini vMac / Basilisk II) to compare against. Ask for all of these.
2. **Create the port repo and a `PORT_NOTES.md`** from `templates/PORT_NOTES.md`.
   This is your lab notebook; you will lose context between sessions, the notes
   must not. Write every finding there *as you make it*, with addresses.
3. **Put all game-derived data in a git-ignored folder** (`game/`, `work/`) before
   the first commit. See `docs/legal.md`.
4. **Recon** (`docs/02-recon.md`):
   ```
   python tools/macres/rfork.py info <app>
   python tools/macres/code_extract.py <app> work/
   python tools/analysis/traps.py <app> > work/recon/traps.txt
   python tools/analysis/trap_audit.py <app>
   ```
   Report to the human: compiler, segment count, A5 globals size, distinct traps,
   colour or B&W, sound approach, anything unusual (far segments, code resources
   like WDEF/MDEF/'Jnth', direct hardware access).
5. **Copy `hle/` into the port repo** (it is the baseline), set `src/config.h`, build,
   and run it on the game. Whatever happens is your starting point.

## 1. The core loop (90% of the work)

```
run the game (headless, scripted)  ->  observe (log + screen dump)
   -> find the FIRST thing that is wrong  ->  explain it from evidence
   -> fix it in the right manager file  ->  rebuild  ->  re-run the SAME script
   -> compare with the previous dump / the reference  ->  note it  ->  commit
```

* **Fix the first wrong thing, not the most visible one.** A skewed stack from one
  unhandled trap shows up as a crash thousands of instructions later.
* **One cause per commit.** Each commit message says what was wrong and how you
  proved it. Small commits make bisecting regressions trivial.
* **Re-run the same script after every change** and look at the image. "It builds"
  is not "it works".

## 2. Hard rules

### Evidence before edits
* **Read the call site before implementing a trap.** `python tools/analysis/disasm.py
  <app> find <WORD> 10 3` shows what the app pushes. Count the bytes: that is your
  pop count. Never guess a signature from memory — the same trap number can mean
  different things in your table and in reality (Falcon: A924 is `FrontWindow`, an
  early table said `SystemEvent`; GetMouse/Button were swapped in another table).
* **Pascal calling convention** for stack-based Toolbox traps: the caller reserves
  space for the result *first*, pushes arguments left to right, the trap pops the
  arguments and leaves the result. Booleans/bytes live in the **high byte** of their
  word (`RETB` does this). Functions like `TickCount`, `Random`, `Button` return on
  the stack, not in D0. Records larger than 4 bytes are passed **by address**.
  Details: `docs/05-implementing-traps.md`.
* **OS traps (word bit 11 clear) are register based** and must return with the
  condition codes set from `TST.W D0` — apps do `_NewPtr` then `BNE` with no test.
* **Unknown trap = silent stack skew.** The fallback returns without popping the
  arguments. Treat every `[trap] unimplemented` line as a bug, and run
  `trap_audit.py` whenever behaviour is weird.

### Verify, don't assume
* **Verify the build actually happened.** On Windows run `build.bat` from PowerShell
  (`cmd /c ".\build.bat"`); from Git Bash it can silently no-op and leave a stale exe.
  Kill the running exe before linking. If a fix "does nothing", check the exe
  timestamp first.
* **Compare pictures, not feelings.** Dump the screen (`HLE_SCRIPT=...;dump f.pbm`),
  convert (`tools/assets/pbm2png.py`), *look at it*, and compare to a reference
  screenshot from the human or an emulator. Describe differences concretely
  ("bow arc missing, rows 0-20 are sky colour").
* **Don't name functions from heuristics.** Naming `FUN_1f3d84` "sceneTransform3D"
  because it multiplies a lot was wrong in Falcon. Name only what you traced; mark
  guesses with `?` in `symbols.tsv` comments.
* **Mark what you didn't verify.** "Compiles, not execution-tested" is a fine status;
  presenting an untested path as working is not.

### Performance hygiene
* **Never call `getenv()` (or anything slow) in the per-instruction hook or per
  trap.** Cache env flags once. One getenv in Falcon's hook = 40× slowdown that also
  grew with environment size.
* Keep debug instrumentation behind cached flags, default off, and commit it — it
  will be needed again.

### Fidelity
* **The original code is the spec.** Don't "fix" game logic or patch game bytes to
  get past a problem until you know *why* the original works on a real Mac. Most
  "the game is broken here" moments were HLE bugs (Falcon's "missing pilot data"
  turned out to be a collision check against a wrong altitude caused by a missing
  trap behaviour).
* When you *do* patch game code (e.g. a gate that can't be satisfied on a PC
  keyboard), make it a named, documented, switchable patch in the loader, never an
  edit to the user's file.
* Remasters/enhancements go in a separate line (branch) from the faithful port.

### People and publishing
* Don't push, publish, make repos public, or create releases without the human's
  explicit OK. Use the human's chosen git identity; set it per repo.
* Never commit game bytes, decompiler output, extracted art/sound, ROMs, System
  files or fonts. Copy-protection analysis stays private (see docs/legal.md).
* When blocked on something only the human can do (click inside an emulator, provide
  a file, test on real hardware), say exactly what you need and why, then continue
  with whatever else is unblocked.

## 3. Choosing the next task

Priority order when the game "doesn't work":

1. **Crash / bad jump / fatal** → read the bad-jump report (recent traps, registers),
   find the last trap before it, check its pop count and return convention.
2. **Hangs** (busy loop) → it is waiting for something: TickCount not advancing,
   a sound "done" flag, a mouse-up, an event, a VBL task. `HLE_PCLOG` the loop,
   see what it polls.
3. **Wrong picture** → which drawing primitive? Log the drawing traps around it
   (CopyBits, fills, text), dump the offscreen buffers, watch writes to a pixel.
4. **Input does nothing** → find how the game reads input (GetNextEvent? GetKeys?
   low-memory KeyMap/mouse? a VBL task?) — trace before wiring.
5. **Missing feature** → trap_audit, then the specific manager.

Write the hypothesis in PORT_NOTES before testing it; record the result either way.
Disproved hypotheses are valuable — they stop the next session repeating them.

## 4. Using Ghidra (the microscope)

Build the combined decompilation once recon is done (`docs/03-ghidra-decomp.md`).
Use it to answer specific questions: "who writes global X", "what does the loop at
PC Y wait for", "what does this trap's caller do with the result". Keep a
`symbols.tsv` of confirmed names and rebuild; re-run `name_traps.py` and
`name_thunks.py` after every rebuild. Runtime addresses in the HLE differ from the
Ghidra layout: convert via segment number + offset (the HLE logs segment load
addresses with `HLE_SEGLOG=1`).

## 5. Testing infrastructure to build early

* **Scripted, headless runs** (`HLE_HIDDEN=1 HLE_SCRIPT="ms:click x y;...;dump f.pbm;quit"`).
  Prefer *state-driven* navigation (wait until the screen shows X) to fixed delays
  once timing gets flaky — fixed-time clicks race the emulator's speed.
* **Frame dumps** of the screen *and* of any offscreen buffer the game draws into.
* **Counters at PCs** (how often did the renderer run this frame?) — instantly
  tells you whether a code path is reached at all.
* **Write-watch on a memory range** with the writing PC — finds who draws a pixel
  or who clobbers a global.
* **A trap ring buffer** printed on every fatal (already in `trap.c`).

## 6. When to stop and ask the human

* A choice that changes the product (faithful vs. enhanced, which platforms,
  public vs. private, releasing).
* Needing files you don't have (reference screenshots, a manual page, a different
  version of the game, a System file).
* After two or three disproved hypotheses on the same bug: summarize what is known,
  what was ruled out (with data), and offer options with your recommendation.

## 7. Finishing a port

`docs/09-packaging-release.md`: find-game-by-content, per-user save folder, native
dialogs, CI builds for each OS, a README that tells players where to get the game
legally, third-party notices. Then, optionally, `docs/10-remaster-and-beyond.md`.

---

## Index of the docs

| Doc | When |
|---|---|
| [00-strategy](docs/00-strategy.md) | choosing HLE vs. emulator vs. rewrite |
| [01-acquire-and-extract](docs/01-acquire-and-extract.md) | getting files out of .sit/.dsk/.dmg |
| [02-recon](docs/02-recon.md) | CODE 0, A5 world, segments, compiler, trap inventory |
| [03-ghidra-decomp](docs/03-ghidra-decomp.md) | whole-program decompile |
| [04-hle-runtime](docs/04-hle-runtime.md) | how the runtime works inside |
| [05-implementing-traps](docs/05-implementing-traps.md) | the trap loop, calling conventions, cheat sheet |
| [06-quickdraw-display](docs/06-quickdraw-display.md) | 1-bit & colour QuickDraw, PICT, palettes, presenting |
| [07-input-sound-files](docs/07-input-sound-files.md) | events, keyboard/mouse, VBL tasks, sound, File Manager |
| [08-debugging-and-testing](docs/08-debugging-and-testing.md) | headless scripts, dumps, traces, watches |
| [09-packaging-release](docs/09-packaging-release.md) | shipping |
| [10-remaster-and-beyond](docs/10-remaster-and-beyond.md) | GPU remaster, widescreen, byte-matching decomp |
| [gotchas](docs/gotchas.md) | the bug list — read before debugging |
| [legal](docs/legal.md) | what may and may not be published |
| [case-studies](docs/case-studies/) | Falcon MC, Air Traffic Controller timelines |
