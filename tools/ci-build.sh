#!/usr/bin/env bash
# Builds lua-ftxui for one Lua version and runs the test-suite against it.
# Used by the CI workflows; also works locally (Linux / Git Bash on Windows).
#
#   tools/ci-build.sh 5.4
#
# Steps: fetch the stock Lua sources from lua.org -> build an interpreter laid
# out like the LuaBinaries packages (see tools/lua) -> build the module against
# those headers / import library -> run tests/test.lua (+ the PTY tests on
# Linux). Results end up in build/lua<ver>/{lua,module}/.
# Set GENERATOR=Ninja on Windows (MSVC environment must be active).
set -euo pipefail

ver="${1:?usage: ci-build.sh <5.1|5.2|5.3|5.4|5.5>}"
cd "$(dirname "$0")/.."
root="$(pwd -W 2>/dev/null || pwd)"   # mixed-style path on Windows, plain on Linux

case "$ver" in
  5.1) full=5.1.5 ;;
  5.2) full=5.2.4 ;;
  5.3) full=5.3.6 ;;
  5.4) full=5.4.8 ;;
  5.5) full=5.5.0 ;;
  *) echo "unsupported Lua version: $ver" >&2; exit 2 ;;
esac
nodot="${ver/./}"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) windows=1; exe=lua.exe; ext=dll ;;
  *)                    windows=0; exe=lua;     ext=so  ;;
esac

work="$root/build/lua$ver"
mkdir -p "$work"
if [ ! -f "$work/src/src/lua.h" ]; then
  curl -fsSL --retry 5 --retry-delay 5 -o "$work/lua.tar.gz" \
    "https://www.lua.org/ftp/lua-$full.tar.gz"
  mkdir -p "$work/src"
  # relative paths: GNU tar reads "D:/..." as host:path
  (cd "$work" && tar -xzf lua.tar.gz -C src --strip-components=1)
fi

gen=()
[ -n "${GENERATOR:-}" ] && gen=(-G "$GENERATOR")

cmake -S "$root/tools/lua" -B "$work/lua" "${gen[@]}" \
  -DLUA_SRC="$work/src" -DLUA_VERSION="$ver" -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/lua" --config Release

lib=()
[ "$windows" = 1 ] && lib=(-DLUA_LIBRARY="$work/lua/out/lua$nodot.lib")

cmake -S "$root" -B "$work/module" "${gen[@]}" \
  -DLUA_VERSION="$ver" -DLUA_INCLUDE_DIR="$work/src/src" "${lib[@]}" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/module" --config Release

lua="$work/lua/out/$exe"
module="$work/module/ftxui.$ext"
ls -la "$work/lua/out" "$work/module" | head -30
test -f "$module"
test -f "$lua" || { echo "interpreter not found: $lua" >&2; exit 1; }

"$lua" tests/test.lua "$work/module/?.$ext"
if [ "$windows" = 0 ] && command -v python3 >/dev/null; then
  python3 tests/pty_test.py "$lua" "$work/module/?.$ext"
fi
echo "built $module"
