#!/usr/bin/env bash
#
# Build GIMX for Windows.
#
# Run it from an MSYS2 "MINGW64" shell (https://www.msys2.org/):
#
#   scripts/build-windows.sh --deps   # first time: install the build dependencies with pacman
#   scripts/build-windows.sh          # build
#
# The result (gimx.exe, gimx-launcher.exe, ... and the DLLs they need) is put in the setup/ folder.
#
# The virtual controller (--port virtual, "Virtual controller (no adapter)" in the launcher) is only
# built in when the ViGEmBus client library sources are in shared/ViGEmClient
# (https://github.com/nefarius/ViGEmClient). It also needs the ViGEmBus driver installed on the PC
# that runs GIMX (https://github.com/nefarius/ViGEmBus/releases).
#

set -euo pipefail

cd "$(dirname "$0")/.."

if [ "${MSYSTEM:-}" != "MINGW64" ]; then
  echo "error: run this script from an MSYS2 MINGW64 shell" >&2
  exit 1
fi

# Package names change over time in MSYS2: each line lists alternatives, the first available one is installed.
# Lines starting with '?' are optional.
REQUIREMENTS=(
  "make"
  "git"
  "gettext"
  "mingw-w64-x86_64-gcc"
  "mingw-w64-x86_64-pkgconf"
  "mingw-w64-x86_64-SDL2"
  "mingw-w64-x86_64-libxml2"
  "mingw-w64-x86_64-curl"
  "mingw-w64-x86_64-wxwidgets3.2-msw mingw-w64-x86_64-wxwidgets3.3-msw mingw-w64-x86_64-wxWidgets3.2 mingw-w64-x86_64-wxWidgets"
  "mingw-w64-x86_64-ncurses"
  "mingw-w64-x86_64-libusb mingw-w64-x86_64-libusb1 mingw-w64-x86_64-libusb-1.0"
  "mingw-w64-x86_64-gettext-runtime mingw-w64-x86_64-gettext"
  "mingw-w64-x86_64-gettext-tools mingw-w64-x86_64-gettext"
  "mingw-w64-x86_64-ntldd mingw-w64-x86_64-ntldd-git"
  "?mingw-w64-x86_64-avrdude avrdude" # only used to flash the hardware adapter
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
      if pacman -Si "$candidate" >/dev/null 2>&1; then
        found="$candidate"
        break
      fi
    done
    if [ -n "$found" ]; then
      PACKAGES+=("$found")
    else
      first="${requirement%% *}"
      echo "warning: no package found for: $requirement" >&2
      echo "  close matches:" >&2
      pacman -Ssq "${first#mingw-w64-x86_64-}" 2>/dev/null | grep -i "^mingw-w64-x86_64-" | head -n 10 | sed 's/^/    /' >&2 || true
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
cat > "$TOOLS/gcc-gimx" <<'EOF'
#!/bin/sh
exec gcc -std=gnu17 "$@" -Wno-error
EOF
cat > "$TOOLS/g++-gimx" <<'EOF'
#!/bin/sh
exec g++ "$@" -Wno-error
EOF
chmod +x "$TOOLS/gcc-gimx" "$TOOLS/g++-gimx"

# wxWidgets 3.2 packages may only provide a versioned wx-config.
if ! command -v wx-config >/dev/null 2>&1; then
  WXCONFIG="$(ls /mingw64/bin/wx-config* 2>/dev/null | head -n 1 || true)"
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
