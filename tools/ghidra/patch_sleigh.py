#!/usr/bin/env python3
"""Patch Ghidra's 68000 SLEIGH to decode Mac A-line Toolbox traps (0xA000-0xAFFF)
as an opaque dispatched call, so the decompiler flows through trap sites instead
of halting on "bad instruction data". Idempotent."""
import sys, glob, os

SINC = sys.argv[1]
src = open(SINC, "r", newline="").read()

PCODEOP = "define pcodeop MacToolboxTrap;"
CTOR = (
    "\n# --- MacPortKit: Macintosh A-line Toolbox/OS trap -------------------\n"
    "# Any 0xA000-0xAFFF word is a dispatched Toolbox/OS call. Model it as an opaque\n"
    "# side-effecting pcodeop so decompilation flows through (full word passed so the\n"
    "# trap can be named in post-processing).\n"
    ":A_trap op015\tis op=0xA & op015\t{ MacToolboxTrap(op015:2); }\n"
)

changed = False
if PCODEOP not in src:
    anchor = "define pcodeop __m68k_trapv;"
    assert anchor in src, "pcodeop anchor not found"
    src = src.replace(anchor, anchor + "\n" + PCODEOP, 1)
    changed = True

if ":A_trap" not in src:
    anchor = ":illegal\t\t\tis d16=0x4afc\t\tunimpl"
    if anchor not in src:
        # fall back to a looser anchor
        import re
        m = re.search(r":illegal\b.*?unimpl", src)
        assert m, "illegal ctor anchor not found"
        anchor = m.group(0)
    src = src.replace(anchor, anchor + "\n" + CTOR, 1)
    changed = True

if changed:
    open(SINC, "w", newline="").write(src)
    print("PATCHED", SINC)
else:
    print("ALREADY PATCHED", SINC)

# Force recompile: remove compiled .sla so Ghidra rebuilds from .slaspec/.sinc
langdir = os.path.dirname(SINC)
for sla in glob.glob(os.path.join(langdir, "*.sla")):
    os.remove(sla)
    print("removed", os.path.basename(sla))
