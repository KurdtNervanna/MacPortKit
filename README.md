# MacPortKit

**Turn an old black-and-white or colour Macintosh program from the late 1980s–90s
into a normal app that runs on today's Windows, macOS or Linux — without rewriting
the game and without installing a whole fake Macintosh.**

MacPortKit is a set of tools, a ready-made program you build on, and a written-down
method for doing this. It is designed so that a person working **with an AI coding
assistant** (such as Claude Code) can carry out a port even without deep
reverse-engineering experience — the AI follows the playbook in this repo, and you
supply the game and the judgement calls.

It is distilled from two complete ports done exactly this way:

| Original | What it was | What it became |
|---|---|---|
| **Air Traffic Controller 5.03** (Walter J. Biess, 1993) | black-and-white air-traffic sim, lots of Mac windows/menus/dialogs | native **Windows, macOS and Linux** app — [see it running](https://github.com/KurdtNervanna/AirTrafficController-Port#screenshots) |
| **Falcon MC** (Spectrum HoloByte, 1992) | colour F-16 flight sim with its own 3-D engine and sound | native Windows app running the original code, plus a GPU remaster |

---

## Start here

- **Porting a game with an AI assistant?** Point it at **[AGENTS.md](AGENTS.md)** —
  that is the step-by-step operating manual written for the AI. Section
  ["Using this with an AI agent"](#using-this-with-an-ai-agent) below has a prompt
  you can paste to kick it off.
- **Doing it yourself / just browsing?** Read
  ["How it works"](#how-it-works) then ["Quick start"](#quick-start-do-it-yourself),
  and keep [docs/gotchas.md](docs/gotchas.md) handy.

## What problem this solves

Classic Macintosh software is trapped. The programs were built for a computer that
no longer exists, store their contents in a "resource fork" that modern file systems
throw away, and run on a Motorola 68000-family chip that your computer doesn't have.
The usual way to run them today is an **emulator** — a program that pretends to be a
whole old Macintosh, needing a copy of Apple's ROM and a System install, and wrapping
the game in a dead operating system's desktop.

MacPortKit takes a different route that produces a **real, modern app**: it runs the
game's *own* original code and rewrites only the handful of Apple system services the
game actually asks for. The result opens in a normal resizable window, takes a modern
keyboard, mouse and game controller, saves files where your OS expects, and needs no
ROM and no System install. The gameplay is the genuine original, down to the physics
and the quirks, because the original program is still the thing running.

## How it works

The technical name is **high-level emulation (HLE)**. In one paragraph:

> Run the game's own 68k machine code on a small embedded CPU core
> ([Musashi](https://github.com/kstenerud/Musashi)), and re-implement — in C — only
> the Mac "Toolbox / OS" calls the game actually makes. Every time the old program
> asks the system to draw a window, play a sound, or read the mouse, that request is
> caught and answered by modern code. You reverse-engineer only as deep as you need
> to make each call behave correctly. A decompiler (Ghidra) is a microscope you point
> at the spots where behaviour is wrong — not a prerequisite.

<details>
<summary><b>Plain-language glossary</b> (click to expand)</summary>

| Term | Meaning |
|---|---|
| **68k** | the Motorola 680x0 processor old Macs used; the game's code is in this machine language |
| **Toolbox / OS trap** | a call the program makes into the Mac system (draw a menu, open a file…). Each one is a single special instruction the CPU "traps" on — the one doorway between the game and the system |
| **HLE** | high-level emulation: re-implement those system calls in modern code instead of emulating the whole machine |
| **Musashi** | a small, well-tested 68k CPU emulator (included); it runs the game's instructions |
| **resource fork** | where a Mac file keeps its code, art, sounds and layouts; needs unwrapping on a modern system |
| **Ghidra** | free reverse-engineering tool that turns machine code back into readable C, to understand tricky parts |
| **the runtime (`hle/`)** | the ready-made program in this repo that does all of the above; you point it at your game and extend it |

</details>

## What you need to bring

* **Your own copy of the game.** MacPortKit ships *no* game data — you supply the
  original program you legally own (often a `.sit`, `.dsk`/`.img`, `.dmg` or `.bin`).
* **A System 6 "System" file** from a Mac system install you own, if the game needs
  its fonts. (Only the fonts are used; it is not redistributed.)
* **A C compiler** to build the runtime (Visual Studio / Build Tools on Windows,
  Xcode command-line tools or gcc/clang + CMake on macOS/Linux), and **Python 3.8+**
  with `capstone` (`pip install capstone`) for the analysis tools.

## Using this with an AI agent

The whole repo is written to be driven by an AI coding assistant. The AI's manual is
**[AGENTS.md](AGENTS.md)** (and [CLAUDE.md](CLAUDE.md) points Claude Code to it
automatically). To start a port, open this repo in your AI coding tool and paste
something like:

> I want to port a classic 68k Macintosh program to a native app using this
> repository (MacPortKit). Read **AGENTS.md** first and follow it. I have the game
> file at `<path>` and a System 6 System file at `<path>`. Begin with the first-session
> checklist: run recon, tell me what the program is (compiler, segments, graphics,
> sound, input, anything unusual), then copy `hle/` into a new port folder, point
> `src/config.h` at my game, build it, and run it — and show me how far it gets.
> Keep a `PORT_NOTES.md`, never commit my game's data, and ask me before publishing
> anything.

From there the AI works the core loop — run the game, find the first thing that is
wrong, fix the matching system call, re-run, compare to a reference screenshot — and
stops to ask you at the decisions that are yours (which platforms, faithful vs.
enhanced, when to publish). AGENTS.md spells out the rules, the priorities, and the
dozens of specific pitfalls that otherwise cost hours each.

## Quick start (do it yourself)

```bash
pip install capstone

# 1. What is this program? (works on .bin / .sit-extracted / raw resource forks)
python tools/macres/rfork.py        info         "MyGame.bin"
python tools/macres/code_extract.py "MyGame.bin" work/     # segments, jump table, compiler
python tools/analysis/trap_audit.py "MyGame.bin"           # which system calls the runtime still lacks

# 2. Does it already run? Build the runtime and point it at a folder
#    containing the game + a System file.
cd hle
cmd /c ".\build.bat"                                        # Windows; or: cmake -S . -B b && cmake --build b
build\hle.exe "C:\path\to\folder with MyGame + System file"
```

Then follow the method in order: [docs/00-strategy.md](docs/00-strategy.md) →
[docs/02-recon.md](docs/02-recon.md) → [docs/04-hle-runtime.md](docs/04-hle-runtime.md)
→ [docs/05-implementing-traps.md](docs/05-implementing-traps.md). Read
[docs/gotchas.md](docs/gotchas.md) before you debug anything.

## What's in the repo

| Path | What |
|---|---|
| [`AGENTS.md`](AGENTS.md) | **The AI operating manual** — the loop, the rules, what to verify, when to ask you. (`CLAUDE.md` points to it.) |
| [`docs/`](docs/) | The method, phase by phase (`00`–`10`), the [gotchas](docs/gotchas.md) bug list, [legal](docs/legal.md) rules, and two [case studies](docs/case-studies/) |
| [`hle/`](hle/) | A **working starter runtime** (C, ~6 kLOC + Musashi). Runs Air Traffic Controller end-to-end as shipped; point `src/config.h` at your game |
| [`tools/macres/`](tools/macres/) | Unwrap MacBinary/AppleDouble/AppleSingle/raw forks; list & dump resources; split the code, decode the jump table, find embedded function names, identify the compiler |
| [`tools/analysis/`](tools/analysis/) | System-call histogram, coverage audit against the runtime, disassembler whose `find` mode shows every call site of a given call |
| [`tools/assets/`](tools/assets/) | Pictures → PNG (B&W and colour), sounds → WAV, screen-dump → PNG |
| [`tools/ghidra/`](tools/ghidra/) | Whole-program decompile: the patch that teaches Ghidra about Mac traps, the combined-image layout, and automatic naming of calls |
| [`tools/toolchain/`](tools/toolchain/) | No-admin WSL/Linux setup for `unar`, `hfsutils`, the m68k assembler and capstone |
| [`tools/win/`](tools/win/) | Capture a running window to PNG; send keystrokes to it |
| [`templates/`](templates/) | `PORT_NOTES.md` (the lab notebook every port keeps) and a `symbols.tsv` starter |

## Is my software supported?

* **Yes:** 68k Mac applications from roughly System 4–7 (1986–1995), built with THINK
  C/Pascal, Lightspeed, MPW or early CodeWarrior. Games and productivity software alike.
* **With extra work:** colour (Color QuickDraw) games — Falcon MC shows how; apps that
  drive the sound hardware directly; apps using far-model (large) code segments.
* **Not (yet):** PowerPC or "fat" apps (their code lives in the data fork as PEF),
  programs that need real networking hardware (AppleTalk), or ones that are mostly a
  front end for a ROM routine there's no documentation to re-implement.

If you're unsure, run `python tools/macres/rfork.py info <file>` and
`python tools/macres/code_extract.py <file> work/` — they'll tell you whether it's a
68k app and print its shape. (Or just ask your AI assistant to.)

## Legal

Every tool here works on **your own copy** and writes derived data (code segments,
decompiled C, extracted art) into a work folder you keep **private**. Nothing
copyrighted belongs in a port's public repository: you ship your runtime, the user
supplies their own game. Full rules in [docs/legal.md](docs/legal.md).

## Licence and credits

This kit's own code and documentation are **MIT** ([LICENSE](LICENSE)). The included
[Musashi](https://github.com/kstenerud/Musashi) 68k core is MIT; SoftFloat has its own
permissive notice — see [hle/THIRD_PARTY_NOTICES.md](hle/THIRD_PARTY_NOTICES.md).

Built by [KurdtNervanna](https://github.com/KurdtNervanna) from the Falcon MC and
[Air Traffic Controller](https://github.com/KurdtNervanna/AirTrafficController-Port)
ports. Not affiliated with Apple or with the original games' authors.
