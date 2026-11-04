#!/usr/bin/env bash
# Windows build recipe for SwitchU in this checkout (Git Bash + devkitPro MSYS2).
#
# Requirements that are easy to get wrong, all learned the hard way:
#
#  1. xmake must be the DEV channel (3.1.2+dev). Release builds fail while loading
#     the package repos: 3.x chokes on `list()` in PoloNX/switch-repo, and 2.9.8
#     chokes on `set_project()` in this repo's own xmake.lua. Upstream CI uses
#     `xmake-version: branch@dev` for the same reason.
#
#  2. devkitPro lives under msys64/opt/devkitpro here, and devkitA64 is NOT at
#     /opt/devkitpro unless you set DEVKITPRO. `uam` (shader compiler) is in
#     $DEVKITPRO/tools/bin and must be on PATH or shader targets fail.
#
#  3. The native eSpeak NG data step shells out to CMake, which otherwise picks
#     the NMake generator and dies with "CMAKE_C_COMPILER not set". Point it at
#     the MSYS2 UCrt64 gcc + MSYS make instead.
#
#  4. NEVER wrap xmake in `timeout`. Doing so launches it with cwd C:\Windows,
#     so xmake's relative global dir resolves to C:\Windows\.xmake and the build
#     dies with "cannot open file: C:\Windows\.xmake\...\.mark".
#
# Usage:
#   ./build_windows.sh                 # production daemon + external menu (nsp)
#   HOMEBREW=y ./build_windows.sh      # standalone .nro homebrew build
#   ./build_windows.sh clean           # xmake clean
#   ./build_windows.sh reconfigure     # drop cached config and the native cmake cache

set -euo pipefail

# xmake needs a Windows-style DEVKITPRO; PATH needs the MSYS form, because a
# Windows drive letter's ':' would split the POSIX PATH list into fragments.
DEVKITPRO_WIN="${DEVKITPRO:-C:/devkitPro/msys64/opt/devkitpro}"
DEVKITPRO_SH="${DEVKITPRO_SH:-/c/devkitPro/msys64/opt/devkitpro}"
export DEVKITPRO="$DEVKITPRO_WIN"
export PATH="$DEVKITPRO_SH/tools/bin:/c/devkitPro/msys64/usr/bin:$PATH:/c/devkitPro/msys64/ucrt64/bin"

# Host toolchain for the native eSpeak NG data generation step.
export CMAKE_GENERATOR="Unix Makefiles"
export CC="C:/devkitPro/msys64/ucrt64/bin/gcc.exe"
export CXX="C:/devkitPro/msys64/ucrt64/bin/g++.exe"
export CMAKE_MAKE_PROGRAM="C:/devkitPro/msys64/usr/bin/make.exe"

XMAKE="${XMAKE:-/c/Program Files/xmake/xmake.exe}"
HOMEBREW="${HOMEBREW:-n}"
BACKEND="${BACKEND:-deko3d}"

die() { echo "error: $*" >&2; exit 1; }

[ -x "$XMAKE" ] || die "xmake not found at '$XMAKE' (set XMAKE=/path/to/xmake.exe)"
[ -d "$DEVKITPRO_SH/devkitA64" ] || die "devkitA64 missing under $DEVKITPRO_SH (install with: pacman -S switch-dev)"
command -v uam >/dev/null 2>&1 || die "uam not on PATH (expected $DEVKITPRO_SH/tools/bin/uam.exe)"

case "${1:-}" in
    clean)       exec "$XMAKE" clean -y ;;
    reconfigure) rm -rf build/espeak-ng-native build/cross; echo "caches dropped" ;;
esac

echo "== xmake: $("$XMAKE" --version | head -1)"
echo "== DEVKITPRO: $DEVKITPRO"
echo "== mode: homebrew=$HOMEBREW backend=$BACKEND"

"$XMAKE" f -p cross --toolchain=devkita64 --homebrew="$HOMEBREW" --backend="$BACKEND" -y
"$XMAKE" -y

echo "== build finished; outputs under build/cross/aarch64/"
find build/cross/aarch64 -maxdepth 3 \( -name '*.nro' -o -name '*.nsp' \) -print 2>/dev/null || true
