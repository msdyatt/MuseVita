#!/usr/bin/env python3
"""Stage an official Muse Gadget SDK pairing for one-time VitaShell import.

This tool never contacts Muse, prints credentials, or performs pairing. Run it
on the BLE-capable host *after* the official SDK has paired the seeded Vita
identity. It validates and copies only the three files MuseVita imports.
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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path,
                        default=Path(os.environ.get("MUSEGADGET_STATE_DIR", "/var/lib/musegadget")),
                        help="official Muse Gadget SDK state directory")
    parser.add_argument("--output", type=Path, required=True,
                        help="new directory in which to stage the VitaShell bundle")
    args = parser.parse_args(argv)
    try:
        stage_bundle(args.state_dir, args.output)
    except PairingBundleError as exc:
        print(f"MuseVita pairing bundle error: {exc}", file=sys.stderr)
        return 2
    print(f"Staged {len(FILES)} files in {args.output}")
    print("Copy these files to ux0:data/MuseVita/ with VitaShell, then launch MuseVita once.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
