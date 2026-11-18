#!/usr/bin/env bash

# One-click Linux pairing launcher for MuseVita.
#
# Uses Meta's published Linux Gadget SDK for the one-time BLE/account
# authorization, then creates the JSON bundle that MuseVita already accepts.
# The official gadget service is stopped and disabled after pairing.

set -Eeuo pipefail
umask 077

readonly SDK_COMMIT="f2c702b123e97f4fecdaefb29ded17bb03fab0b0"
readonly SDK_REPOSITORY="https://github.com/facebookincubator/muse-gadget-sdk.git"
readonly SDK_INSTALL_URL="https://raw.githubusercontent.com/facebookincubator/muse-gadget-sdk/${SDK_COMMIT}/linux/install.sh"
readonly STATE_DIR="/var/lib/musegadget"
readonly MUSEGADGET_BIN="/opt/musegadget/venv/bin/musegadget"

RUN_AS="$(id -un)"
VITA_URL="${MUSEVITA_VITA_URL:-}"
OUTPUT_FILE="${MUSEVITA_OUTPUT:-}"
ASSUME_YES=0
KEEP_STATE=0
WORK_DIR=""

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[33mwarning:\033[0m %s\n' "$*" >&2; }
die() { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

cleanup() {
    if [ -n "$WORK_DIR" ] && [ -d "$WORK_DIR" ]; then
        rm -rf -- "$WORK_DIR"
    fi
}
trap cleanup EXIT

usage() {
    cat <<'EOF'
Usage: musevita-linux-pair.sh [options]

Options:
  --vita-url URL       Vita setup URL, for example http://192.168.1.42:8768/
  --output PATH        Output JSON path (default: Desktop/musevita-pairing.json)
  --keep-state         Keep official Linux pairing files after bundling
  --yes                Skip the installation confirmation
  -h, --help           Show this help

The SDK token is read from MUSEGADGET_SDK_TOKEN or a hidden terminal prompt.
It is never accepted as a command-line argument.
EOF
}

as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    else
        sudo "$@"
    fi
}

ask_yes_no() {
    local prompt="$1" answer
    if [ "$ASSUME_YES" -eq 1 ]; then return 0; fi
    [ -r /dev/tty ] || die "no interactive terminal is available"
    printf '%s [y/N] ' "$prompt" >/dev/tty
    IFS= read -r answer </dev/tty || true
    case "$answer" in
        y|Y|yes|YES) return 0 ;;
        *) return 1 ;;
    esac
}

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --vita-url)
                [ "$#" -ge 2 ] || die "--vita-url needs a value"
                VITA_URL="$2"; shift 2 ;;
            --output)
                [ "$#" -ge 2 ] || die "--output needs a value"
                OUTPUT_FILE="$2"; shift 2 ;;
            --keep-state) KEEP_STATE=1; shift ;;
            --yes|-y) ASSUME_YES=1; shift ;;
            --help|-h) usage; exit 0 ;;
            *) die "unknown option: $1" ;;
        esac
    done
}

check_prerequisites() {
    [ "$(uname -s)" = Linux ] || die "This launcher must run on Linux."
    [ "$(id -u)" -ne 0 ] || die "Run as your normal desktop user; sudo is used where needed."
    for command in python3 sudo; do
        command -v "$command" >/dev/null 2>&1 || die "missing required command: $command"
    done
    if ! command -v curl >/dev/null 2>&1; then
        if command -v apt-get >/dev/null 2>&1; then
            say "curl is missing; installing it for this Ubuntu session."
            as_root apt-get update
            as_root apt-get install -y curl
        else
            die "missing required command: curl; install curl with your Linux package manager"
        fi
    fi
    if [ ! -d /sys/class/bluetooth ] ||
       [ -z "$(find /sys/class/bluetooth -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
        die "no Bluetooth adapter is visible to Linux"
    fi
    if ! command -v systemctl >/dev/null 2>&1 || [ ! -d /run/systemd/system ]; then
        warn "systemd is not running; use a normal Ubuntu/Raspberry Pi boot for the pairing attempt"
    fi
}

choose_defaults() {
    if [ -z "$VITA_URL" ]; then
        [ -r /dev/tty ] || die "pass --vita-url when no terminal is available"
        printf 'Vita setup URL (for example http://192.168.1.42:8768/): ' >/dev/tty
        IFS= read -r VITA_URL </dev/tty
    fi
    VITA_URL="${VITA_URL%/}/"
    python3 - "$VITA_URL" <<'PY'
import sys
from urllib.parse import urlparse
url = urlparse(sys.argv[1])
if url.scheme != "http" or not url.hostname or url.username or url.password:
    raise SystemExit("Vita URL must be a plain local http:// URL")
PY

    if [ -z "$OUTPUT_FILE" ]; then
        desktop="${XDG_DESKTOP_DIR:-}"
        if [ -z "$desktop" ] && command -v xdg-user-dir >/dev/null 2>&1; then
            desktop="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
        fi
        [ -n "$desktop" ] || desktop="$HOME/Desktop"
        mkdir -p -- "$desktop"
        OUTPUT_FILE="$desktop/musevita-pairing.json"
    fi
    case "$OUTPUT_FILE" in
        /*) ;;
        *) OUTPUT_FILE="$PWD/$OUTPUT_FILE" ;;
    esac
    [ ! -e "$OUTPUT_FILE" ] || die "output already exists: $OUTPUT_FILE"
}

confirm_scope() {
    cat <<EOF

This will install Meta's published Linux Gadget SDK at /opt/musegadget,
install or adjust BlueZ, and open one-time BLE pairing for the Muse app.
The SDK token and resulting device credentials are sensitive. They will be
kept locally and will never be printed by this launcher.

Vita setup URL: $VITA_URL
Output file:    $OUTPUT_FILE
SDK revision:   $SDK_COMMIT

EOF
    ask_yes_no "Continue with the Linux pairing setup?" || die "cancelled"
}

read_sdk_token() {
    SDK_TOKEN="${MUSEGADGET_SDK_TOKEN:-}"
    if [ -z "$SDK_TOKEN" ]; then
        [ -r /dev/tty ] || die "set MUSEGADGET_SDK_TOKEN for non-interactive use"
        printf 'Muse SDK token (hidden): ' >/dev/tty
        IFS= read -r -s SDK_TOKEN </dev/tty || true
        printf '\n' >/dev/tty
    fi
    [[ "$SDK_TOKEN" =~ ^mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$ ]] ||
        die "SDK token format is invalid; copy a replacement from gadgets.muse.ai"
}

fetch_vita_identity() {
    local redirect sid identity_url headers
    WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/musevita-pair.XXXXXX")"
    headers="$WORK_DIR/setup.headers"
    curl --silent --show-error --max-time 15 --dump-header "$headers" \
        --output /dev/null "$VITA_URL" ||
        die "Vita setup URL could not be reached; check Wi-Fi and the Vita IP"
    read -r sid < <(python3 - "$VITA_URL" "$headers" <<'PY'
import sys
from urllib.parse import parse_qs, urljoin, urlparse
base = urlparse(sys.argv[1])
headers = open(sys.argv[2], encoding="ascii", errors="replace").read().splitlines()
locations = [line.split(":", 1)[1].strip() for line in headers
             if line.lower().startswith("location:") and ":" in line]
if not locations:
    raise SystemExit("Vita setup URL did not return a one-time pairing redirect")
pair = urlparse(urljoin(sys.argv[1], locations[-1]))
if pair.scheme != "http" or pair.netloc != base.netloc or pair.path != "/pair":
    raise SystemExit("Vita redirect was not a local /pair session")
sid = parse_qs(pair.query).get("s", [""])[0]
if not sid:
    raise SystemExit("Vita redirect did not contain a session")
print(sid)
PY
    )
    identity_url="${VITA_URL%/}/pair/identity?s=${sid}"
    curl --fail --silent --show-error --max-time 15 "$identity_url" >"$WORK_DIR/identity.json"
    python3 - "$WORK_DIR/identity.json" <<'PY'
import json, re, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
mac = value.get("mac")
if not isinstance(mac, str) or not re.fullmatch(r"[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}", mac):
    raise SystemExit("Vita returned an invalid device identity")
PY
    say "Fetched the Vita's non-secret identity."
}

install_official_sdk() {
    local installer="$WORK_DIR/muse-gadget-install.sh"
    if [ -x "$MUSEGADGET_BIN" ] && as_root test -d "$STATE_DIR"; then
        say "Official Muse Gadget SDK is already installed."
        return
    fi
    say "Downloading the pinned official Muse Gadget installer."
    curl --fail --silent --show-error --location --proto '=https' --tlsv1.2 \
        "$SDK_INSTALL_URL" -o "$installer"
    chmod 700 "$installer"
    say "Installing the official Linux Gadget SDK and BlueZ requirements."
    bash "$installer" \
        --from "git+${SDK_REPOSITORY}@${SDK_COMMIT}#subdirectory=linux" \
        --run-as "$RUN_AS" --yes --no-pair
    [ -x "$MUSEGADGET_BIN" ] || die "official SDK installation did not create $MUSEGADGET_BIN"
}

seed_identity_and_token() {
    if as_root test -s "$STATE_DIR/pairing.json"; then
        die "$STATE_DIR/pairing.json already exists; use a clean pairing host"
    fi
    as_root systemctl stop musegadget.service >/dev/null 2>&1 || true
    as_root install -d -m 0700 "$STATE_DIR"
    as_root install -m 0600 "$WORK_DIR/identity.json" "$STATE_DIR/identity.json"
    printf '%s\n' "$SDK_TOKEN" | as_root install -m 0600 /dev/stdin "$STATE_DIR/sdk_token"
    say "Seeded the official SDK with the Vita's stable identity."
}

run_official_pairing() {
    cat <<'EOF'

Now complete one-time Muse authorization on the iPhone:

  Muse app → Settings → Devices → Developer mode → Add Device

Choose the MuseGadget device printed by the official SDK and approve the
community gadget. The pairing window will close when credentials are saved.

EOF
    as_root "$MUSEGADGET_BIN" pair --force || die "official Muse BLE pairing did not complete"
    as_root test -s "$STATE_DIR/pairing.json" || die "pairing ended without official pairing.json"
}

create_bundle() {
    mkdir -p -- "$(dirname -- "$OUTPUT_FILE")"
    sudo python3 - "$STATE_DIR" "$OUTPUT_FILE" <<'PY'
import json, os, re, sys
state_dir, output = sys.argv[1:]
def read(name):
    with open(os.path.join(state_dir, name), encoding="utf-8") as handle:
        return handle.read()
identity = json.loads(read("identity.json"))
pairing_text = read("pairing.json").strip()
sdk_token = read("sdk_token").strip()
mac = identity.get("mac")
pairing = json.loads(pairing_text)
if not isinstance(mac, str) or not re.fullmatch(r"[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}", mac):
    raise SystemExit("official identity is invalid")
if not all(isinstance(pairing.get(key), str) and pairing[key].strip()
           for key in ("access_token", "refresh_token")):
    raise SystemExit("official pairing state has no device credentials")
if not re.fullmatch(r"mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]", sdk_token):
    raise SystemExit("official SDK token has an invalid format")
hint = pairing.get("sdk_token_hint")
if hint is not None and hint != sdk_token[:12]:
    raise SystemExit("official pairing state and SDK token do not match")
if os.path.exists(output):
    raise SystemExit("output already exists; refusing to overwrite it")
payload = {"format": "musevita-pairing-v1", "pairing": pairing_text, "sdk_token": sdk_token}
os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, separators=(",", ":"))
    handle.write("\n")
PY
    sudo chown "$RUN_AS:$(id -gn "$RUN_AS")" "$OUTPUT_FILE"
    chmod 600 "$OUTPUT_FILE"
    say "Created the MuseVita pairing bundle at $OUTPUT_FILE"
}

stop_pairing_host() {
    as_root systemctl disable --now musegadget.service >/dev/null 2>&1 || true
    say "Stopped the Linux gadget service; it will not remain a permanent bridge."
}

offer_cleanup() {
    [ "$KEEP_STATE" -eq 1 ] && return 0
    cat <<EOF

The Linux host still has official pairing state in $STATE_DIR. The JSON file
contains live credentials. After importing it into MuseVita, delete the Linux
copy unless you intentionally want a recovery pairing host.

EOF
    if ask_yes_no "Delete pairing.json and sdk_token from this Linux host now?"; then
        as_root rm -f -- "$STATE_DIR/pairing.json" "$STATE_DIR/sdk_token"
        say "Removed the Linux host's live credential copies."
    else
        warn "Kept the Linux credential copies; protect this boot disk."
    fi
}

main() {
    parse_args "$@"
    check_prerequisites
    choose_defaults
    confirm_scope
    read_sdk_token
    fetch_vita_identity
    install_official_sdk
    seed_identity_and_token
    run_official_pairing
    stop_pairing_host
    create_bundle
    offer_cleanup
    cat <<EOF

Next step:
  Open MuseVita SETUP on the Vita, scan its QR code with the iPhone, choose
  $OUTPUT_FILE, and press SELECT on the Vita.

The QR transfer is local and encrypted. MuseVita connects directly over Wi-Fi
after import; this Linux host is not required afterward.
EOF
}

main "$@"
