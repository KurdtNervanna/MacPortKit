# CLAUDE.md

This repository is a toolkit for porting classic 68k Macintosh software.

**If you are helping someone port a game with this kit, your manual is
[AGENTS.md](AGENTS.md).** Read its "TL;DR — what you are doing" section first, then the
rest, and keep [docs/gotchas.md](docs/gotchas.md) open while debugging. Follow it; the
notes below are only for editing this kit itself.

Quick facts for working *on this kit itself*:

* `hle/` must keep building on Windows (`cmd /c ".\build.bat"` from PowerShell) and
  via CMake on macOS/Linux; it should still boot Air Traffic Controller 5.03 as a
  regression test (see docs/08-debugging-and-testing.md, "baseline test").
* Python tools are stdlib-only except `capstone` (analysis/ghidra tools).
* Never add game data, ROMs, System files, fonts, decompiler output or extracted
  assets to this repository.
* When you learn something new while porting a game, add it to `docs/gotchas.md`
  (symptom → cause → fix) and, if it is a trap, to `tools/analysis/trapnames.py`.
