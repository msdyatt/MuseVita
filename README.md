# Muse Vita

**Created and maintained by [Sam Dyatt](https://github.com/msdyatt).**

**Repository (planned):** `https://github.com/msdyatt/muse-vita`  
**License:** MIT · Copyright © 2026 Sam Dyatt and Muse Vita contributors

**An unofficial, open-source PlayStation Vita client for Meta Muse.**

> **Status: v0.1 source prototype, not an operational Muse client.** The native Vita UI and QR-session demonstration are implemented. Pairing, credential transfer and native communication with the Muse cloud have **not** been implemented or tested. No VPK is included because VitaSDK is not available in this build environment.

## Goal

A homebrew Vita app that exchanges messages directly with the user's existing Meta Muse main chat over Wi-Fi, after a one-time approved provisioning step. No persistent Linux relay, Mac, or Raspberry Pi required for normal use. A web wizard and QR codes will streamline the one-time setup.

## What exists today

| Component | Status |
|---|---|
| Native Vita homebrew UI shell (welcome/setup, controller navigation) | Source implemented; Vita toolchain build unverified |
| VitaSDK/CMake VPK packaging configuration | Source implemented; build unverified |
| Browser-based setup-session demonstration | Implemented, local-development only |
| Short-lived QR codes for non-secret session URLs | Implemented |
| Python unit tests for session lifetime | Implemented |
| Secure end-to-end provisioning transfer | Not implemented |
| Initial Meta-authorized BLE pairing helper | Not implemented |
| Vita Noise XX / TLS / WebSockets / token refresh | Not implemented |
| Actual send/receive Muse messages | Not implemented |
| Chat history, streaming replies, voice, images | Not implemented |

## Try the website

Requirements: Python 3.10+ and `pip`.

```sh
python3 -m pip install -r web/requirements.txt
python3 web/server.py
```

Visit <http://localhost:8777>. Click **Create demo QR session**. The generated QR opens the *same demo site* and shows a ten-minute session code. It does not pair a Vita.

For local network testing, use `HOST=0.0.0.0 PORT=8777 MUSE_VITA_PUBLIC_URL=http://YOUR_COMPUTER_IP:8777 python3 web/server.py` on a **trusted LAN only**. This development server has no production TLS, authentication, rate limiting, or persistence.

Run tests:

```sh
python3 -m unittest discover -s tests -v
```

## Build the Vita shell

See [vita/README.md](vita/README.md) for VitaSDK prerequisites and installation. Build with `cmake -S vita -B build-vita` after setting `VITASDK`. Requires `vita2d` in the SDK. Use the resulting VPK only on a homebrew-capable PS Vita.

## Roadmap / acceptance criteria

### Milestone 1: Native app runs

- Vita renders a functional UI, supports gamepad controls, closes safely.
- Verify a VPK builds with VitaSDK and installs through VitaShell.
- No network secrets, debug passwords or SDK tokens are embedded.

### Milestone 2: Direct transport

- Implement HTTPS/TLS verification with certificate bundle and correct Vita system clock handling.
- Implement WSS and the SDK's Noise XX session with interoperable test vectors.
- Implement VM discovery, device registration, refresh-token rotation and reconnect/retry behavior.
- Send one main-chat message and subscribe to the streamed response **from the Vita directly**, with no server in the middle.

### Milestone 3: One-time authorization and QR onboarding

- Provision via an approved Muse Gadget pairing process (currently documented as BLE through Meta's mobile app).
- Pair **the Vita's persistent identity** on another machine and transfer credentials once, as documented for Bluetooth-less devices.
- Build an ephemeral end-to-end encrypted credential transfer that binds to a Vita-generated public key, with short TTL, one-time redemption, explicit consent, no credential logging and no plaintext server copies.
- Web-hosted QR wizard displays status and instructions; no permanent relay needed after setup.

### Milestone 4: Polished chat client

- Text entry via Vita IME, history paging, follow-up conversations, streaming responses, agent status and reconnect.
- Add optional microphone, images and settings after core text chat works reliably.

## SDK references

- [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk)
- [Linux SDK pairing and chat protocol](https://github.com/facebookincubator/muse-gadget-sdk/blob/main/linux/AGENTS.md)
- [Linux SDK README](https://github.com/facebookincubator/muse-gadget-sdk/blob/main/linux/README.md)
- [Headless/no-BLE pairing transfer example](https://github.com/facebookincubator/muse-gadget-sdk/blob/main/linux/docs/raspberry-pi-2-headless.md)
- [Gadget device identity/guide protocol](https://github.com/facebookincubator/muse-gadget-sdk/blob/main/protocols/DEVICE.md)
- [VitaSDK and installation](https://vitasdk.org/)

## Third-party platform and trademarks

This project is not affiliated with Meta or Sony. Meta's software SDK is Apache-2.0, but its Gadget SDK token usage and distribution terms are separate. Public releases must not bundle developer SDK tokens or personal account credentials, and the distribution model needs an up-to-date review of [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms).

The code in this package is MIT licensed; no Meta SDK source or bundled proprietary fonts/assets are redistributed.


## Developing with Codex

[AGENTS.md](AGENTS.md) describes project constraints, commands, and acceptance
criteria for coding agents. [CODEX_START_HERE.md](CODEX_START_HERE.md) is the
first-task brief. Keep the scaffold's implemented/unimplemented claims truthful.

## Publishing to GitHub

See [PUBLISHING.md](PUBLISHING.md) for the initial one-time repository creation
and push workflow. **This source package is not already published on GitHub.**
