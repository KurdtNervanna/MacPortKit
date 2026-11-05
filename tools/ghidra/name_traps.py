#!/usr/bin/env python3
"""Post-process Ghidra output: MacToolboxTrap(0xA9A0) -> _GetResource(/*0xA9A0*/).

  python name_traps.py <file.c> [more.c ...] [--report trap_usage.md]

Needs the SLEIGH patch (patch_sleigh.py) so Ghidra emits MacToolboxTrap(word) at all.
Unknown words stay as MacToolboxTrap(0x....) with the dispatch index in a comment, so
nothing is ever mislabeled. Re-run after EVERY Ghidra rebuild (the rebuild regenerates
the raw calls). Run it before name_thunks.py.
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis'))
from trapnames import name  # noqa: E402

PAT = re.compile(r'MacToolboxTrap\((0x[0-9a-fA-F]+)\)')


def main(argv):
    files, report = [], None
    it = iter(argv[1:])
    for a in it:
        if a == '--report':
            report = next(it)
        else:
            files.append(a)
    if not files:
        print(__doc__)
        return 1
    usage, named, unknown = collections.Counter(), 0, 0

    def repl(m):
        nonlocal named, unknown
        w = int(m.group(1), 16)
        usage[w] += 1
        nm = name(w)
        if nm != '?':
            named += 1
            return f'_{nm}(/*{m.group(1)}*/)'
        unknown += 1
        kind = f'TB#0x{w & 0x3FF:03X}' if w >= 0xA800 else f'OS#0x{w & 0xFF:02X}'
        return f'MacToolboxTrap({m.group(1)})/*{kind}*/'

    for f in files:
        t = open(f, encoding='utf-8', errors='replace').read()
        n = PAT.sub(repl, t)
        if n != t:
            open(f, 'w', encoding='utf-8').write(n)
    if report:
        with open(report, 'w') as f:
            f.write(f'# Trap usage\n\n{sum(usage.values())} call sites, {len(usage)} distinct, '
                    f'{named} named, {unknown} unknown\n\n| count | word | name |\n|--:|---|---|\n')
            for w, c in usage.most_common():
                f.write(f'| {c} | 0x{w:04X} | {name(w)} |\n')
    print(f'named {named} trap call sites, {unknown} unknown')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
