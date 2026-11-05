#!/usr/bin/env bash
# No-root (no sudo, no password) Linux/WSL toolchain for classic-Mac reverse engineering.
#
#   bash setup_wsl_noroot.sh          # installs under ~/macport-tools/root
#   source ~/macport-tools/env.sh     # puts the tools on PATH for this shell
#
# Fetches .deb packages with `apt-get download` (works without root) and unpacks them
# with `dpkg -x`, then chases missing shared libraries automatically. Gives you:
#   unar / lsar            StuffIt (.sit), .cpt, .hqx, BinHex, .sea  ->  files + forks
#   hfsutils (hmount, hls, hcd, hcopy, hformat, humount)
#                          read/write classic HFS disk images (.dsk/.img/.hfs) -
#                          7-Zip reads HFS+/DMG but NOT old HFS floppies or .sit
#   m68k-linux-gnu-as/objcopy/nm   68k GNU binutils (byte-match verification)
#   python3-capstone       68k disassembler for the Python tools
#
# Notes learned the hard way:
#   * WSL's /tmp can be wiped between separate tool invocations - work under ~.
#   * `hcopy -m` writes MacBinary (keeps the resource fork); plain hcopy loses it.
#   * `unar -k visible` writes AppleDouble "._" / ".rsrc" companions for forks; the
#     tools in this kit read AppleDouble directly, no conversion needed.
set -u
DEST=~/macport-tools; R="$DEST/root"; mkdir -p "$DEST"; cd "$DEST"
export LD_LIBRARY_PATH="$R/usr/lib/x86_64-linux-gnu:$R/lib/x86_64-linux-gnu:$R/usr/lib"

fetch() { apt-get download "$1" >/dev/null 2>&1 && for d in ${1}_*.deb; do dpkg -x "$d" root 2>/dev/null; done; }

echo "downloading packages (no root needed)..."
apt-get update >/dev/null 2>&1 || true   # harmless if not permitted; uses existing lists
for p in unar hfsutils binutils-m68k-linux-gnu python3-capstone libcapstone5 \
         libgnustep-base1.31 gnustep-base-runtime libobjc4 libbz2-1.0 libwavpack1 libxslt1.1; do
  fetch "$p" || echo "  (could not fetch $p - may not exist in this distro release; resolver below will try)"
done

# chase missing shared libraries for each binary, up to 15 rounds
resolve() {
  local bin="$1"
  for i in $(seq 1 15); do
    local lib; lib=$("$bin" --version 2>&1 >/dev/null | sed -n "s/.*error while loading shared libraries: \([^:]*\):.*/\1/p")
    [ -z "$lib" ] && return 0
    local pkg; pkg=$(dpkg -S "$lib" 2>/dev/null | head -1 | cut -d: -f1)
    if [ -z "$pkg" ]; then
      pkg=$(apt-file search -l "$lib" 2>/dev/null | head -1)
    fi
    [ -z "$pkg" ] && { echo "  !! cannot find a package for $lib (needed by $bin)"; return 1; }
    echo "  $bin needs $lib -> $pkg"; fetch "$pkg"
  done
}
for b in lsar unar hls m68k-linux-gnu-as; do [ -x "$R/usr/bin/$b" ] && resolve "$R/usr/bin/$b"; done

cat > "$DEST/env.sh" <<EOF
export PATH="$R/usr/bin:\$PATH"
export LD_LIBRARY_PATH="$R/usr/lib/x86_64-linux-gnu:$R/lib/x86_64-linux-gnu:$R/usr/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export PYTHONPATH="$R/usr/lib/python3/dist-packages\${PYTHONPATH:+:\$PYTHONPATH}"
EOF
. "$DEST/env.sh"
echo "verifying:"
lsar --version 2>&1 | head -1 || true
hls >/dev/null 2>&1; [ -x "$R/usr/bin/hls" ] && echo "hfsutils OK"
m68k-linux-gnu-as --version 2>&1 | head -1 || true
python3 -c "import capstone; print('capstone', capstone.__version__)" 2>&1 | head -1
echo "done. In each new shell:  source $DEST/env.sh"
