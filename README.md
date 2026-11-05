# MacPortKit

**Tools, a working runtime and a written-down method for porting classic 68k
Macintosh games and software to modern Windows, macOS and Linux — designed to be
driven by an AI coding agent (Claude Code or similar) as much as by a person.**

It is distilled from two complete ports done this way:

| Port | Original | Result |
|---|---|---|
| **Falcon MC** | Spectrum HoloByte, 1992. Colour F‑16 sim, THINK C, 46 code segments, ~1,000 functions, its own 3‑D vector renderer and sound driver | Native Windows exe running the original code; later a GPU remaster on top |
| **Air Traffic Controller 5.03** | Walter J. Biess, 1993. B&W, Lightspeed Pascal, 14 segments, heavy Toolbox UI (windows, menus, dialogs, controls, regions) | Native Windows/macOS/Linux builds — [public repo](https://github.com/KurdtNervanna/AirTrafficController-Port) |

## The approach in one paragraph

Don't rewrite the game and don't ship a whole-Mac emulator. **Run the game's own
68k machine code on an embedded CPU core (Musashi), and re-implement — in C — only
the Mac Toolbox/OS calls it actually makes** ("high-level emulation", HLE). The
gameplay is then bit-for-bit the original; there is no ROM, no System to install,
and the result is a normal native program with a native window, controller and
file dialogs. You reverse-engineer only as deep as you need to: usually just enough
to implement each trap correctly. Decompilation (Ghidra) is a microscope you point
at the places where behaviour is wrong, not a prerequisite.

## What's here

| Path | What |
|---|---|
| [`AGENTS.md`](AGENTS.md) | **Start here if you are an AI agent.** The operating playbook: loop, rules, what to verify, what to ask the human. (`CLAUDE.md` points to it.) |
| [`docs/`](docs/) | The method, phase by phase, plus the hard-won [gotchas](docs/gotchas.md) list and two [case studies](docs/case-studies/) |
| [`hle/`](hle/) | A **working starter runtime** (C, ~6 kLOC + Musashi). Boots ATC end to end as shipped; point `src/config.h` at your app |
| [`tools/macres/`](tools/macres/) | Unwrap MacBinary/AppleDouble/AppleSingle/raw forks; list/dump resources; split CODE, decode the jump table, find MacsBug names, fingerprint the compiler |
| [`tools/analysis/`](tools/analysis/) | Trap histogram, trap‑coverage audit against `hle/src`, quick disassembler with named traps (`find` mode shows every call site) |
| [`tools/assets/`](tools/assets/) | PICT → PNG (v1 B&W + v2 colour), `snd ` → WAV, screen-dump → PNG |
| [`tools/ghidra/`](tools/ghidra/) | Whole-program Ghidra decompile: SLEIGH patch for A‑line traps, combined image with resolved jump table and A5 globals, trap/thunk naming |
| [`tools/toolchain/`](tools/toolchain/) | No-sudo WSL/Linux setup for `unar`, `hfsutils`, m68k binutils, capstone |
| [`tools/win/`](tools/win/) | Capture a window to PNG; send keys to a running port |
| [`templates/`](templates/) | `PORT_NOTES.md` (the living lab notebook every port should keep) and `symbols.tsv` |

## Quick start

```bash
pip install capstone
# 1. What is it?
python tools/macres/rfork.py info "MyGame.bin"
python tools/macres/code_extract.py "MyGame.bin" work/        # segments, jump table, fingerprint
python tools/analysis/trap_audit.py "MyGame.bin"              # what the runtime still lacks
# 2. Does it already run?
cd hle && cmd /c ".\build.bat"                                # or cmake on macOS/Linux
build\hle.exe "C:\path\to\folder with MyGame + System file"
```

Then follow [docs/00-strategy.md](docs/00-strategy.md) → [docs/04-hle-runtime.md](docs/04-hle-runtime.md)
→ [docs/05-implementing-traps.md](docs/05-implementing-traps.md), and read
[docs/gotchas.md](docs/gotchas.md) before you debug anything.

## Scope

* **Yes:** 68k Mac applications from roughly System 4–7 (1986–1995), near-model
  CODE segments, compiled with THINK C/Pascal, Lightspeed, MPW or early CodeWarrior.
  Games and productivity software alike.
* **With work:** Color QuickDraw games (Falcon MC shows the way), apps that drive
  sound hardware directly, apps that use far-model (32-bit) segments.
* **No:** PowerPC/fat apps (code in the data fork as PEF), apps that need networking
  hardware (AppleTalk), or that are mostly a front end for a ROM routine you can't
  re-implement from documentation.

## Legal

Every tool here works on **your own copy** of the software and writes derived data
(segments, decompiled C, extracted art) into a work folder you must keep private.
Nothing copyrighted belongs in a port's public repository: ship your runtime, let the
user supply the game. Read [docs/legal.md](docs/legal.md).

## Licence

MIT for this kit's own code and documentation ([LICENSE](LICENSE)). The vendored
Musashi core is MIT; SoftFloat has its own notice — see
[hle/THIRD_PARTY_NOTICES.md](hle/THIRD_PARTY_NOTICES.md).
