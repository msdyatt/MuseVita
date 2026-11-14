# MuseVita

**Created and maintained by [Sam Dyatt](https://github.com/msdyatt).**

**Repository:** `https://github.com/msdyatt/MuseVita`
**License:** MIT · Copyright © 2026 Sam Dyatt and MuseVita contributors

An unofficial, open-source PlayStation Vita client for Meta Muse. The planned
Nintendo 3DS client will be called MuseDS; portable protocol code is being kept
outside Vita-specific UI and networking so the two clients can share it.

> **Status: native transport-integration milestone.** MuseVita now has a real
> VitaSDK/CMake build, a packaged VPK, native status/setup/chat UI, encrypted
> local credential storage, direct HTTPS VM lookup, libcurl WSS transport,
> Noise XX, link registration, chat send, and streamed chat-event handling.
> The authenticated path still requires a real user-owned official pairing
> import and validation on a physical Vita before it can be called release-ready.

## Product goal

After an approved one-time provisioning flow, MuseVita should connect directly
from the Vita over Wi-Fi to the user's existing Meta Muse agent. Normal use
must not require an always-on Mac, Raspberry Pi, Linux relay, or other bridge.
QR onboarding remains a goal for the one-time setup; QR data must contain only
a short-lived session reference and Vita public key, never Muse credentials.

## Current implementation

| Component | Status |
|---|---|
| Native Vita UI and controller navigation | Implemented and built into the VPK |
| Vita Wi-Fi status provider | Implemented; host-mockable interface is in place |
| VitaSDK/CMake packaging | Built with VitaSDK 2026.08 and `libvita2d` |
| Shared control-stream framing, link wire builders, and decoders | Implemented and host-tested |
| Noise XX initiator using VitaSDK mbedTLS | Vita-built with low-order-point rejection; cross-runtime handshake and encrypted-payload fixture passes against the pinned official SDK |
| On-device QR pairing transfer | Implemented in the Vita build: one-time local HTTP page, QR session/key pinning, browser WebCrypto encryption; physical-phone validation pending |
| Local QR setup page | Development-only demo remains separate from the on-device transfer |
| Approved Meta pairing and one-time transfer | Official pairing-state import implemented; on-device BLE pairing is not |
| Protected local credential storage | One-time plaintext import is converted to an encrypted local blob; extraction/revocation review remains |
| HTTPS certificate validation and VM lookup | Implemented with libcurl verification; real-account validation pending |
| WSS upgrade and encrypted link session | Implemented in the native source; endpoint validation pending |
| Muse device discovery and unpair handling | Implemented: structured `device.guide`, capability metadata, and local credential retirement on `link.unpaired`/`node.unpaired`; live validation pending |
| Native chat view, bounded history, and Vita IME composer | Implemented and connected to streamed transport events |
| Direct chat history/send/subscribe and streamed replies | Implemented in the native source with a bounded initial history page; account/hardware validation pending |
| Native photo and voice-note capture | Implemented as bounded Vita camera/JPEG and microphone/WAV send paths; physical capture validation pending |
| Vita hardware installation/playability | Not tested in this environment |

The VPK is a build artifact, not evidence of a live Muse connection. Do not
enter real tokens into the demo website or commit pairing state.

## One-time official pairing import

MuseVita does not accept a Muse password, browser cookie, or invented token. Use
the official Gadget SDK flow on a supported BLE host to pair the Vita identity,
then stop the host's gadget service so only one device registration is active.
For the phone flow, run the repository helper on the supported pairing host to
create one `musevita-pairing.json` bundle. The bundle contains the official
pairing state and SDK token for that user-owned device; it is not a password,
browser cookie, or public download. The Vita's QR page downloads the Vita's
non-secret identity automatically, so the phone only needs that one bundle.
The phone encrypts it locally before transfer. On receipt MuseVita imports the
official access and refresh tokens plus SDK token into `credentials.bin` and
removes the plaintext import data.

If using VitaShell instead of the QR flow, the helper can still create the
legacy directory containing `identity.json`, `pairing.json`, and `sdk_token`.
The Vita does not currently perform the BLE peripheral pairing step itself; the
one-time BLE pairing must be completed by a supported host using the same
identity before the bundle is made.

When the Vita is on Wi-Fi, the SETUP page displays a one-time QR code. Scan it
with a phone on the same network. The page downloads the Vita's non-secret
identity automatically, so the phone needs only one validated
`musevita-pairing.json` bundle from the official SDK; press SELECT on the Vita
when it is ready. The QR contains only a local URL, an expiring session
reference, and the Vita's ephemeral P-256 public key. The phone encrypts the
bundle with browser WebCrypto when available, or the bundled pure-JavaScript
fallback, using ECDH/HKDF-SHA-256/AES-256-GCM; the Vita decrypts and validates
it locally, then immediately converts it to encrypted app storage. The local
URL never serves the pairing credentials or SDK token.
The local transfer server stops accepting uploads after the first successful
bundle. This path is source-tested and cross-compiled, but still needs a
physical phone/Vita transfer test before release claims.

## Build the Vita application

Requirements: VitaSDK, CMake, and `libvita2d`. Set `VITASDK` before configuring:

```sh
export VITASDK=/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S vita -B build-vita -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j
```

The output is `build-vita/MuseVita.vpk`. Transfer it with VitaShell and install
only on a homebrew-capable PS Vita. Hardware installation and interaction still
need to be verified and recorded separately.

## Host tests

```sh
python3 -m unittest discover -s tests -v
```

The C framing harness checks the official SDK's little-endian uint32 length
prefix, fragmented input, JSON payload delivery, and zero-length keepalives.

## Local QR demo

Requirements: Python 3.10+ and `pip`.

```sh
python3 -m pip install -r web/requirements.txt
python3 web/server.py
```

Visit <http://localhost:8777> and create a demo QR session. It does not pair a
Vita, contact Muse, mint credentials, or establish a secure transfer. For LAN
testing, bind only to a trusted network and use the `MUSE_VITA_PUBLIC_URL`
setting described in the server help; this development server has no
production TLS, authentication, rate limiting, or persistence.

## Direct protocol plan

The port must follow the official Meta Muse Gadget SDK at a pinned upstream
commit, currently reviewed at `f2c702b123e97f4fecdaefb29ded17bb03fab0b0`:

1. Use the approved Muse mobile pairing procedure and persist a stable Vita
   identity. QR onboarding may coordinate the one-time transfer but cannot
   replace BLE or account authorization.
2. Fetch the leased VM with the user-owned device access credential over
   certificate-validated HTTPS.
3. Open `wss://<noise_host>/v1/noise?vm_id=<id>` with the VM bearer, integrate
   the implemented exact Noise XX initiator, and verify it against SDK vectors
   while rejecting malformed/low-order inputs.
4. Send the protobuf service envelope over `/link-control`, register the Vita
   as a gadget, then use `/chat/history` for a bounded initial transcript,
   `/chat/stream` for text turns, and `/chat/subscribe` for NDJSON events.
   Never keep a second live control session for the same identity.

The implementation must be fixture-tested before any real credential is used.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/SECURITY.md](docs/SECURITY.md) for the security boundary and pending
work. See [docs/PAIRING.md](docs/PAIRING.md) for the one-time supported
pairing workflow. No Meta or Sony affiliation is claimed.

## Publishing

See [PUBLISHING.md](PUBLISHING.md) for the initial GitHub repository and pull
request workflow. Public releases must not bundle SDK tokens, access/refresh
tokens, or personal pairing state.
