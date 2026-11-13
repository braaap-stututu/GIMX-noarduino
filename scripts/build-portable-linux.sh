#!/usr/bin/env bash
#
# Build a portable GIMX bundle for Fedora (and most other x86_64 Linux distributions):
# a tarball that runs from any folder, without installing GIMX system-wide.
#
#   scripts/build-portable-linux.sh --deps   # first time on Fedora: install the build dependencies with dnf
#   scripts/build-portable-linux.sh          # build
#
# Result: dist/gimx-portable-<version>-<arch>.tar.gz
#
# Build on the oldest Fedora release the bundle has to run on: the bundled programs need
# a C library at least as recent as the one they were built with.
#

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

SUDO=""
if [ "$(id -u)" != 0 ]; then
  SUDO="sudo"
fi

if [ "${1:-}" = "--deps" ]; then
  if ! command -v dnf >/dev/null 2>&1; then
    echo "error: --deps only knows dnf (Fedora), install the build dependencies manually" >&2
    exit 1
  fi
  $SUDO dnf install -y \
    gcc gcc-c++ make git findutils gettext \
    libxml2-devel ncurses-devel bluez-libs-devel mhash-devel libusb1-devel \
    libcurl-devel libXi-devel libX11-devel systemd-devel wxGTK-devel
fi

git submodule update --init

TOOLS="$(mktemp -d)"
trap 'rm -rf "$TOOLS"' EXIT

# The code base predates current compilers: build as C17 and don't turn their new warnings into errors.
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

# Fedora ships ncursesw6-config and may only provide a versioned wx-config.
if ! command -v ncursesw5-config >/dev/null 2>&1; then
  ln -s "$(command -v ncursesw6-config)" "$TOOLS/ncursesw5-config"
fi
if ! command -v wx-config >/dev/null 2>&1; then
  WXCONFIG="$(ls /usr/bin/wx-config* 2>/dev/null | head -n 1 || true)"
  if [ -z "$WXCONFIG" ]; then
    echo "error: wx-config not found, run: $0 --deps" >&2
    exit 1
  fi
  ln -s "$WXCONFIG" "$TOOLS/wx-config"
fi
export PATH="$TOOLS:$PATH"

make -j"$(nproc)" CC="$TOOLS/gcc-gimx" CXX="$TOOLS/g++-gimx"

#
# Stage the bundle.
#

VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo unknown)"
NAME="gimx-portable-$VERSION-$(uname -m)"
DIST="$ROOT/dist/$NAME"

rm -rf "$DIST"
mkdir -p "$DIST"/{bin,libexec,lib,share/locale,share/gimx/firmware,share/icons}

PROGRAMS=(
  core/gimx
  launcher/gimx-launcher
  config/gimx-config
  fpsconfig/gimx-fpsconfig
  loader/gimx-loader
)
for program in "${PROGRAMS[@]}"; do
  install -m 755 "$program" "$DIST/libexec/"
done
for lib in shared/*/lib*.so; do
  install -m 755 "$lib" "$DIST/lib/"
done

for translation in po/*.po; do
  lang="$(basename "$translation" .po)"
  mkdir -p "$DIST/share/locale/$lang/LC_MESSAGES"
  msgfmt -o "$DIST/share/locale/$lang/LC_MESSAGES/gimx.mo" "$translation"
done
cp loader/firmware/*.hex "$DIST/share/gimx/firmware/" 2>/dev/null || true
cp launcher/gimx-launcher.png "$DIST/share/icons/"

#
# Bundle the shared libraries the programs need, except the ones that must come from the system:
# the C library and compiler runtime, and the desktop/graphics stack (GTK, X11, OpenGL, fonts...),
# which has to match the running session, plus base libraries that are on every install and keep
# a stable ABI.
#
SYSTEM_LIBS='^(ld-linux.*|linux-vdso.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libanl|libmvec|libstdc\+\+|libgcc_s|libatomic'
SYSTEM_LIBS+='|libGL.*|libEGL|libGLX.*|libGLdispatch|libOpenGL|libgbm|libdrm.*|libvulkan|libwayland-.*|libxkbcommon.*|libX.*|libxcb.*|libSM|libICE'
SYSTEM_LIBS+='|libgtk-3|libgdk-3|libgdk_pixbuf-2\.0|libglib-2\.0|libgobject-2\.0|libgio-2\.0|libgmodule-2\.0|libgthread-2\.0'
SYSTEM_LIBS+='|libpango.*|libcairo.*|libharfbuzz.*|libfontconfig|libfreetype|libfribidi|libthai|libdatrie|libgraphite2|libpixman-1|libepoxy|libatk.*|libatspi|libcloudproviders|libtracker.*'
SYSTEM_LIBS+='|libdbus-1|libsystemd|libudev|libselinux|libmount|libblkid|libuuid|libcap|libseccomp|libffi|libpcre2-8|libexpat'
SYSTEM_LIBS+='|libz|libbz2|liblzma|libzstd|liblz4|libbrotli.*|libpng16|libjpeg|libtiff|libwebp.*|libsharpyuv|libjbig|libLerc|libdeflate'
SYSTEM_LIBS+='|libssl|libcrypto|libgnutls|libnettle|libhogweed|libgmp|libp11-kit|libtasn1|libidn2|libunistring|libgcrypt|libgpg-error'
SYSTEM_LIBS+='|libgssapi_krb5|libkrb5|libk5crypto|libkrb5support|libcom_err|libkeyutils|libtinfo|libncursesw|libncurses|libformw|libmenuw|libpanelw|libasound|libpulse.*)\.so'

bundle_deps() {
  local missing=0
  while read -r soname path; do
    if [ "$path" = "not" ]; then
      echo "error: $soname not found on the build system" >&2
      missing=1
      continue
    fi
    if [[ "$soname" =~ $SYSTEM_LIBS ]] || [ -e "$DIST/lib/$soname" ]; then
      continue
    fi
    cp -L "$path" "$DIST/lib/$soname"
    chmod 755 "$DIST/lib/$soname"
  done < <(LD_LIBRARY_PATH="$DIST/lib" ldd "$@" | awk '/=>/ { print $1, $3 }' | sort -u)
  return $missing
}
bundle_deps "$DIST"/libexec/* "$DIST"/lib/*.so*
# ldd is recursive, a second pass catches nothing new unless a bundled library was found elsewhere first
bundle_deps "$DIST"/libexec/* "$DIST"/lib/*.so*

#
# Launch wrappers: every program in bin/ is a link to the same script, which runs the
# program of the same name in libexec/ with the bundled libraries.
#
cat > "$DIST/bin/.gimx-run" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
root="$(dirname "$here")"
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# the launcher starts gimx, gimx-config... by name
export PATH="$root/bin:$PATH"
export GIMX_LOCALEDIR="$root/share/locale"
export GIMX_FIRMWARE_DIR="$root/share/gimx/firmware"
exec "$root/libexec/$(basename "$0")" "$@"
EOF
chmod 755 "$DIST/bin/.gimx-run"
for program in "${PROGRAMS[@]}"; do
  ln -s .gimx-run "$DIST/bin/$(basename "$program")"
done

install -m 755 scripts/portable/setup.sh "$DIST/setup.sh"
install -m 644 scripts/portable/README.txt "$DIST/README.txt"
cp LICENSE "$DIST/"

tar -C "$ROOT/dist" -czf "$ROOT/dist/$NAME.tar.gz" "$NAME"

echo
echo "Bundle: $ROOT/dist/$NAME.tar.gz"
echo "Bundled libraries:"
ls "$DIST/lib" | sed 's/^/  /'
