#!/usr/bin/env python3
"""Stage an official Muse Gadget SDK pairing for one-time VitaShell import.

This tool never contacts Muse, prints credentials, or performs pairing. Run it
on the BLE-capable host *after* the official SDK has paired the seeded Vita
identity. It validates the official state and can create either the three-file
VitaShell bundle or one `musevita-pairing.json` phone-transfer input file.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

MAC_RE = re.compile(r"[0-9a-f]{2}(?::[0-9a-f]{2}){5}\Z")
SDK_TOKEN_RE = re.compile(r"mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]\Z")
FILES = ("identity.json", "pairing.json", "sdk_token")
BUNDLE_FORMAT = "musevita-pairing-v1"


class PairingBundleError(Exception):
    pass


def _read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise PairingBundleError(f"cannot read {path.name}") from exc
    if not isinstance(value, dict):
        raise PairingBundleError(f"{path.name} must contain a JSON object")
    return value


def validate_state(state_dir: Path) -> None:
    identity = _read_json(state_dir / "identity.json")
    mac = identity.get("mac")
    if not isinstance(mac, str) or MAC_RE.fullmatch(mac) is None:
        raise PairingBundleError("identity.json has an invalid device identity")

    pairing = _read_json(state_dir / "pairing.json")
    for key in ("access_token", "refresh_token"):
        value = pairing.get(key)
        if not isinstance(value, str) or not value.strip():
            raise PairingBundleError("pairing.json is missing device credentials")

    try:
        sdk_token = (state_dir / "sdk_token").read_text(encoding="utf-8").strip()
    except (OSError, UnicodeError) as exc:
        raise PairingBundleError("cannot read sdk_token") from exc
    if SDK_TOKEN_RE.fullmatch(sdk_token) is None:
        raise PairingBundleError("sdk_token is not a valid Muse Gadget SDK token")

    hint = pairing.get("sdk_token_hint")
    if hint is not None and hint != sdk_token[:12]:
        raise PairingBundleError("pairing.json and sdk_token do not match")


def stage_bundle(state_dir: Path, output_dir: Path) -> None:
    validate_state(state_dir)
    if output_dir.exists():
        raise PairingBundleError("output directory already exists; choose a new empty path")
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.mkdir(output_dir, 0o700)
        for name in FILES:
            source = state_dir / name
            destination = output_dir / name
            with source.open("rb") as source_file, destination.open("xb", buffering=0) as destination_file:
                shutil.copyfileobj(source_file, destination_file)
                destination_file.flush()
                os.fsync(destination_file.fileno())
            os.chmod(destination, 0o600)
    except (OSError, ValueError) as exc:
        shutil.rmtree(output_dir, ignore_errors=True)
        raise PairingBundleError("could not stage the pairing bundle") from exc


def stage_bundle_file(state_dir: Path, output_file: Path) -> None:
    """Create one JSON file suitable for the phone-assisted QR transfer."""
    validate_state(state_dir)
    if output_file.exists():
        raise PairingBundleError("output file already exists; choose a new path")
    output_file.parent.mkdir(parents=True, exist_ok=True)
    try:
        payload = {
            "format": BUNDLE_FORMAT,
            "pairing": (state_dir / "pairing.json").read_text(encoding="utf-8"),
            "sdk_token": (state_dir / "sdk_token").read_text(encoding="utf-8").strip(),
        }
        with output_file.open("x", encoding="utf-8") as destination:
            json.dump(payload, destination, separators=(",", ":"))
            destination.write("\n")
            destination.flush()
            os.fsync(destination.fileno())
        os.chmod(output_file, 0o600)
    except (OSError, UnicodeError, ValueError) as exc:
        try:
            output_file.unlink()
        except OSError:
            pass
        raise PairingBundleError("could not create the pairing bundle file") from exc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path,
                        default=Path(os.environ.get("MUSEGADGET_STATE_DIR", "/var/lib/musegadget")),
                        help="official Muse Gadget SDK state directory")
    parser.add_argument("--output", type=Path, required=True,
                        help="new directory, or a .json file for the one-file phone transfer")
    parser.add_argument("--one-file", action="store_true",
                        help="write one musevita-pairing.json file instead of a directory")
    args = parser.parse_args(argv)
    try:
        if args.one_file or args.output.suffix.lower() == ".json":
            stage_bundle_file(args.state_dir, args.output)
        else:
            stage_bundle(args.state_dir, args.output)
    except PairingBundleError as exc:
        print(f"MuseVita pairing bundle error: {exc}", file=sys.stderr)
        return 2
    if args.one_file or args.output.suffix.lower() == ".json":
        print(f"Created one phone-transfer bundle at {args.output}")
        print("Scan the MuseVita QR code, choose this file, and press SELECT on the Vita.")
    else:
        print(f"Staged {len(FILES)} files in {args.output}")
        print("Copy these files to ux0:data/MuseVita/ with VitaShell, then launch MuseVita once.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
