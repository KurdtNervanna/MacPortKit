# 01 — Acquire the files and get the resource fork out

A classic Mac file has two forks. For an application, **everything that matters is
in the resource fork**: `CODE` segments, `DLOG`/`DITL`/`MENU`/`WIND`, `PICT`, `snd `,
`STR#`, fonts, the app's private types. The data fork is usually empty. Modern file
systems drop the resource fork unless the file is wrapped. Your job here: end with a
file the tools can read.

## Containers you will meet

| You have | Get the fork with |
|---|---|
| `.sit` / `.sea` / `.cpt` / `.hqx` | `unar -k visible file.sit` → writes `name` + an **AppleDouble** companion (`._name` or `name.rsrc`). Use the AppleDouble file directly. (`unar`/`lsar` = The Unarchiver CLI.) |
| HFS disk image `.dsk` / `.img` / `.image` / `.hfs` (400K/800K/1.4M floppies, small HD images) | hfsutils: `hmount disk.dsk; hls; hcd "Folder"; hcopy -m "App" app.bin; humount`. **`-m` = MacBinary**, which keeps the resource fork. |
| `.dmg` (modern) | 7-Zip or `hdiutil attach`; inside you often find a `.dsk` or files with forks |
| Disk Copy 4.2 image (84-byte header) | strip the header (`dd bs=84 skip=1`) → raw HFS → hfsutils |
| MacBinary `.bin` | use directly |
| A file already on an emulated Mac | export as MacBinary from the emulator (Mini vMac's ExportFl, Basilisk II's extfs) |

**7-Zip reads HFS+, GPT and DMG but not old HFS floppies and not StuffIt** — that's
why the kit installs `unar` and `hfsutils`.

`tools/toolchain/setup_wsl_noroot.sh` installs `unar`, `hfsutils`, m68k binutils and
capstone in WSL/Linux **without sudo** (downloads `.deb`s with `apt-get download`,
unpacks with `dpkg -x`, chases missing `.so`s). Then `source ~/macport-tools/env.sh`.
WSL's `/tmp` may be wiped between commands — work under `~`.

## Check what you got

```
python tools/macres/rfork.py info app.bin
```

prints the container type, Finder type/creator (`APPL`/`F16C` for Falcon MC) and a
table of resource types. You want to see `CODE` with an id 0. Convert any container
to a raw fork if another tool needs one: `rfork.py extract app.bin app.rsrc`.

`rfork.py dump app.bin work/res` writes every resource to its own file — handy for
looking at `STR#`, `DITL` and private types in a hex editor.

## The System file

The runtime needs a **System 6 System file** for its bitmap fonts (Chicago, Geneva,
Monaco and friends). Get it from a System 6.0.x install disk the user owns, with
`hcopy -m "System Folder:System" System.bin`. System 7 also works for fonts but its
font resources differ ('NFNT'/'FOND' more than 'FONT'); test.

Don't ship it. The runtime finds it by content (any file with `FONT` resources).

## Version hunting

Before investing, collect every version the user has: patches (Falcon MC had a 1.02
patcher whose only change was AV-Mac sound compatibility — irrelevant to a port),
demos, "lite" versions, and the manual (PDF or TeachText). The manual's
keyboard reference card saved hours when mapping controls.

## Reference material to capture now

While the user still has an emulator set up, ask them to capture screenshots of
every screen of the original (title, menus, dialogs, gameplay, high scores). These
become the acceptance tests in later phases. Falcon MC's cockpit was only fixable
because there was a real-emulator screenshot of the same scene to diff against.
