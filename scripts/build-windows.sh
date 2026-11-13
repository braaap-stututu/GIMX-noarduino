#!/usr/bin/env bash
#
# Build GIMX for Windows.
#
# Run it from an MSYS2 "UCRT64" shell (https://www.msys2.org/). "MINGW64" works too if its packages are still available.
#
#   scripts/build-windows.sh --deps   # first time: install the build dependencies with pacman
#   scripts/build-windows.sh          # build
#
# The result (gimx.exe, gimx-launcher.exe, ... and the DLLs they need) is put in the setup/ folder.
#
# The virtual controller (--port virtual, "Virtual controller (no adapter)" in the launcher) is built
# with the ViGEmBus client library (shared/ViGEmClient submodule). It needs the ViGEmBus driver
# installed on the PC that runs GIMX (https://github.com/nefarius/ViGEmBus/releases).
#

set -euo pipefail

cd "$(dirname "$0")/.."

case "${MSYSTEM:-}" in
  UCRT64|MINGW64) ;;
  *)
    echo "error: run this script from an MSYS2 UCRT64 shell" >&2
    exit 1
    ;;
esac

P="$MINGW_PACKAGE_PREFIX" # e.g. mingw-w64-ucrt-x86_64

# Package names change over time in MSYS2: each line lists alternatives, the first available one is installed.
# Names starting with '-' get the environment prefix (e.g. mingw-w64-ucrt-x86_64-gcc), the others are MSYS packages.
# Lines starting with '?' are optional.
REQUIREMENTS=(
  "make"
  "git"
  "gettext"
  "-gcc"
  "-pkgconf"
  "-SDL2"
  "-libxml2"
  "-curl"
  "-wxwidgets3.2-msw -wxwidgets3.3-msw -wxWidgets3.2 -wxWidgets"
  "-ncurses"
  "-libusb -libusb1"
  "-gettext-runtime -gettext"
  "-gettext-tools -gettext"
  "-ntldd -ntldd-git"
  "?-avrdude avrdude" # only used to flash the hardware adapter
)

if [ "${1:-}" = "--deps" ]; then
  pacman -Sy --noconfirm
  PACKAGES=()
  for requirement in "${REQUIREMENTS[@]}"; do
    optional=0
    if [ "${requirement:0:1}" = "?" ]; then
      optional=1
      requirement="${requirement:1}"
    fi
    found=""
    for candidate in $requirement; do
      if [ "${candidate:0:1}" = "-" ]; then
        candidate="$P$candidate"
      fi
      if pacman -Si "$candidate" >/dev/null 2>&1; then
        found="$candidate"
        break
      fi
    done
    if [ -n "$found" ]; then
      PACKAGES+=("$found")
    else
      first="${requirement%% *}"
      first="${first#-}"
      keyword="${first%%[0-9.-]*}"
      echo "warning: no package found for: $requirement" >&2
      echo "  packages matching '$keyword':" >&2
      pacman -Ssq "$keyword" 2>/dev/null | head -n 20 | sed 's/^/    /' >&2 || true
      if [ "$optional" = 0 ]; then
        MISSING=1
      fi
    fi
  done
  if [ -n "${MISSING:-}" ]; then
    echo "error: some required packages were not found" >&2
    exit 1
  fi
  pacman -S --needed --noconfirm "${PACKAGES[@]}"
fi

git submodule update --init

TOOLS="$(mktemp -d)"
trap 'rm -rf "$TOOLS"' EXIT

# The code base predates the current MSYS2 compilers: build as C17 and don't turn their new warnings into errors.
# -Wno-error has to come last to override the -Werror of the Makefiles.
# SDL_HAPTIC_FIRST_AXIS comes from the patched SDL2 GIMX used to ship, stock SDL2 (>= 2.26) has the equivalent
# SDL_HAPTIC_STEERING_AXIS (force applied on the first axis only, for wheels).
cat > "$TOOLS/gcc-gimx" <<'EOF'
#!/bin/sh
exec gcc -std=gnu17 -DSDL_HAPTIC_FIRST_AXIS=SDL_HAPTIC_STEERING_AXIS "$@" -Wno-error
EOF
cat > "$TOOLS/g++-gimx" <<'EOF'
#!/bin/sh
exec g++ "$@" -Wno-error
EOF
chmod +x "$TOOLS/gcc-gimx" "$TOOLS/g++-gimx"

# wxWidgets 3.2 packages may only provide a versioned wx-config.
if ! command -v wx-config >/dev/null 2>&1; then
  WXCONFIG="$(ls "$MINGW_PREFIX"/bin/wx-config* 2>/dev/null | head -n 1 || true)"
  if [ -z "$WXCONFIG" ]; then
    echo "error: wx-config not found, run: $0 --deps" >&2
    exit 1
  fi
  ln -s "$WXCONFIG" "$TOOLS/wx-config"
fi
export PATH="$TOOLS:$PATH"

if [ -f shared/ViGEmClient/src/ViGEmClient.cpp ]; then
  echo "ViGEmClient found: the virtual controller will be built in."
else
  echo "ViGEmClient not found in shared/ViGEmClient: the virtual controller will NOT be built in."
fi

MAKEARGS=(CC="$TOOLS/gcc-gimx" CXX="$TOOLS/g++-gimx")

make -j"$(nproc)" "${MAKEARGS[@]}"
make install "${MAKEARGS[@]}"

echo
echo "Build done: $(pwd)/setup"
