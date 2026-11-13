#!/usr/bin/env bash
#
# One-time setup for the portable GIMX bundle. Run it as your normal user, it asks for sudo when needed.
#
#   ./setup.sh            # everything below, asking for confirmation first
#   ./setup.sh --yes      # same, without asking
#   ./setup.sh --deps     # only install the missing runtime packages (dnf)
#
# What it does:
#   1. installs the runtime packages that a minimal Fedora install may lack (GTK 3, xterm...)
#   2. lets your user create virtual controllers: udev rule for /dev/uinput, uinput module loaded at boot
#   3. adds your user to the "input" group (read keyboards/mice/gamepads, needed to use them as inputs)
#      and to the "dialout" group (talk to a GIMX hardware adapter over USB serial)
#   4. adds "GIMX" to your applications menu, pointing to this folder
#
# Log out and back in after the first run, for the group changes to apply.
#

set -euo pipefail

ROOT="$(dirname "$(readlink -f "$0")")"
TARGET_USER="${SUDO_USER:-$(id -un)}"

RUNTIME_PACKAGES=(gtk3 libX11 libXi ncurses-libs libxml2 libcurl libusb1 bluez-libs xterm)

UDEV_RULE_FILE=/etc/udev/rules.d/60-gimx-uinput.rules
UDEV_RULE='KERNEL=="uinput", SUBSYSTEM=="misc", MODE="0660", GROUP="input", OPTIONS+="static_node=uinput", TAG+="uaccess"'

SUDO=""
if [ "$(id -u)" != 0 ]; then
  SUDO="sudo"
fi

install_deps() {
  if ! command -v rpm >/dev/null 2>&1 || ! command -v dnf >/dev/null 2>&1; then
    echo "Not a dnf-based distribution: make sure GTK 3, libXi, ncurses, libxml2, libcurl, libusb, bluez-libs and xterm are installed."
    return
  fi
  local missing=()
  for package in "${RUNTIME_PACKAGES[@]}"; do
    rpm -q "$package" >/dev/null 2>&1 || missing+=("$package")
  done
  if [ ${#missing[@]} -eq 0 ]; then
    echo "Runtime packages: all installed."
  else
    echo "Installing: ${missing[*]}"
    $SUDO dnf install -y "${missing[@]}"
  fi
}

setup_uinput() {
  echo "Allowing virtual controllers (/dev/uinput)..."
  echo "$UDEV_RULE" | $SUDO tee "$UDEV_RULE_FILE" >/dev/null
  echo uinput | $SUDO tee /etc/modules-load.d/gimx-uinput.conf >/dev/null
  $SUDO modprobe uinput || true
  $SUDO udevadm control --reload-rules
  $SUDO udevadm trigger --sysname-match=uinput || true
}

setup_groups() {
  for group in input dialout; do
    if id -nG "$TARGET_USER" | tr ' ' '\n' | grep -qx "$group"; then
      echo "User $TARGET_USER is already in group $group."
    else
      echo "Adding user $TARGET_USER to group $group..."
      $SUDO usermod -aG "$group" "$TARGET_USER"
      NEED_RELOGIN=1
    fi
  done
}

setup_desktop_entry() {
  local home
  home="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
  local dir="$home/.local/share/applications"
  mkdir -p "$dir"
  cat > "$dir/gimx-launcher.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=GIMX
Comment=Use your keyboard, mouse and gamepads as a game controller
Exec="$ROOT/bin/gimx-launcher"
Icon=$ROOT/share/icons/gimx-launcher.png
Terminal=false
Categories=Game;Utility;
EOF
  if [ "$(id -u)" = 0 ] && [ -n "${SUDO_USER:-}" ]; then
    chown "$SUDO_USER": "$dir/gimx-launcher.desktop"
  fi
  echo "Added GIMX to the applications menu ($dir/gimx-launcher.desktop)."
}

case "${1:-}" in
  --deps)
    install_deps
    exit 0
    ;;
  --yes)
    ;;
  "")
    echo "This will:"
    echo "  - install missing runtime packages (dnf)"
    echo "  - allow your user to create virtual controllers (udev rule for /dev/uinput)"
    echo "  - add $TARGET_USER to the input and dialout groups"
    echo "    (note: members of the input group can read every keyboard and mouse)"
    echo "  - add GIMX to your applications menu"
    read -r -p "Continue? [y/N] " answer
    case "$answer" in
      y|Y|yes|YES) ;;
      *) exit 1 ;;
    esac
    ;;
  *)
    echo "usage: $0 [--yes|--deps]" >&2
    exit 1
    ;;
esac

install_deps
setup_uinput
setup_groups
setup_desktop_entry

echo
echo "Done."
if [ -n "${NEED_RELOGIN:-}" ]; then
  echo "Log out and back in (or reboot) so that the group changes apply."
fi
echo "Start GIMX from the applications menu, or run: $ROOT/bin/gimx-launcher"
