# Muse Vita: agent instructions

Project owner and lead developer: **Sam Dyatt** (GitHub `@msdyatt`).
Intended repository: `msdyatt/muse-vita`. License: MIT.
This is an **unofficial** Meta Muse client for homebrew-capable PlayStation Vita consoles.

## Product goal

Build a native PS Vita client that, after approved one-time provisioning,
connects **directly to the user's existing Meta Muse account** over Wi-Fi.
Do not require any always-on Mac, Raspberry Pi, or Linux bridge.
Plan for a web-based QR onboarding experience, but do not claim QR codes can
replace Meta's BLE authorization protocol without official support.

## Current status and non-negotiable honesty

The existing `vita/src/main.c` is a UI **scaffold only**.
The `web/` setup page generates QR demo sessions **only**.
No real Muse pairing, credential transfer, cloud auth, Noise transport,
chat, or VPK compilation has been verified in this package.
Update README feature status whenever you implement and test something.

## Reference implementations

- https://github.com/facebookincubator/muse-gadget-sdk
- `linux/AGENTS.md`, `linux/src/musegadget/link_client.py`
- `linux/src/musegadget/identity.py`, `pairing.py`, `ble_setup.py`
- `linux/tests/vectors/` for protocol test vectors
- `linux/docs/raspberry-pi-2-headless.md` for no-BLE initial pairing
- https://vitasdk.org/

Read upstream source and tests before porting. Pin audited protocol behavior
to a documented upstream commit when implementation begins. Meta's Apache-2.0
SDK license does **not** grant permission to embed a private SDK token.

## Hard security requirements

- Never commit or print credentials: access tokens, refresh tokens, SDK
  tokens, device pairing state or personal information.
- Don't weaken TLS validation or invent a new custom crypto scheme.
- Check certificate chain, hostname and clock on Vita. Test against fixtures.
- A QR code may contain a short-lived non-secret session reference and Vita
  public key, NEVER Muse account tokens or refresh tokens.
- Never build a public server that stores plaintext user credentials.
- Respect gadget token terms and ensure every user provides their own token.
- The device identity must be stable across reboots and unique per device.
- After transferring paired credentials, the temporary pairing host must stop
  holding live control sessions for that identity.
- Honor Meta's supported pairing procedures; never bypass account auth.

## Engineering workflow

1. Review `README.md`, `docs/ARCHITECTURE.md`, and `docs/SECURITY.md`.
2. Run `python3 -m unittest discover -s tests -v` before and after edits.
3. Validate web demo using local development only; do not deploy it publicly.
4. Configure with `VITASDK` using `cmake -S vita -B build-vita` where available;
   run CMake build and check VPK output on a real toolchain.
5. Keep modules small, isolate Vita-specific APIs from protocol code, write
   host-native test harnesses for framing and cryptography.
6. Submit incremental changes on branches or PRs; note hardware tests needed.
7. Do not claim a real working Muse connection until it has been verified on
   real hardware with valid user-owned gadget credentials.

## First Codex milestone

Make the Vita UI buildable, testable and more than a static splash screen.
Verify the VitaSDK CMake wiring. Add a native connection-status screen and
pluggable network-state interface (mockable on a host) WITHOUT handling any
Muse credentials yet. Prepare an implementation plan for the official
Noise XX + WSS protocol with reproducible tests against Meta's published
vectors. Preserve controller navigation and clear offline error states.

## Attribution

Keep Sam Dyatt credited as the original developer/maintainer in README,
LICENSE, package metadata and releases. Credit contributors separately.
Do not misrepresent the code's functional completeness or Meta/Sony affiliation.
