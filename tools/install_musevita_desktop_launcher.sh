#!/usr/bin/env bash

# Install a temporary Ubuntu desktop shortcut pointing back to this USB.
set -Eeuo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_PATH="$SCRIPT_DIR/musevita-linux-pair.sh"
DESKTOP_DIR="${XDG_DESKTOP_DIR:-$HOME/Desktop}"
DESKTOP_FILE="$DESKTOP_DIR/MuseVita Pairing.desktop"

[ -x "$SCRIPT_PATH" ] || chmod +x "$SCRIPT_PATH"
mkdir -p -- "$DESKTOP_DIR"

cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=MuseVita Pairing
Comment=Pair MuseVita using Meta's official Linux Gadget SDK
Terminal=true
Path=$SCRIPT_DIR
Exec=/usr/bin/env bash "$SCRIPT_PATH"
Icon=utilities-terminal
Categories=Utility;
EOF

chmod +x "$DESKTOP_FILE"
if command -v gio >/dev/null 2>&1; then
    gio set "$DESKTOP_FILE" metadata::trusted true 2>/dev/null || true
fi

printf 'Created desktop shortcut: %s\n' "$DESKTOP_FILE"
printf 'Double-click MuseVita Pairing on the desktop to begin.\n'
