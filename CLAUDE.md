# CLAUDE.md

This repository is a toolkit for porting classic 68k Macintosh software. The agent
playbook is **[AGENTS.md](AGENTS.md)** — read it before doing anything, and read
[docs/gotchas.md](docs/gotchas.md) before debugging a port.

Quick facts for working *on this kit itself*:

* `hle/` must keep building on Windows (`cmd /c ".\build.bat"` from PowerShell) and
  via CMake on macOS/Linux; it should still boot Air Traffic Controller 5.03 as a
  regression test (see docs/08-debugging-and-testing.md, "baseline test").
* Python tools are stdlib-only except `capstone` (analysis/ghidra tools).
* Never add game data, ROMs, System files, fonts, decompiler output or extracted
  assets to this repository.
* When you learn something new while porting a game, add it to `docs/gotchas.md`
  (symptom → cause → fix) and, if it is a trap, to `tools/analysis/trapnames.py`.
