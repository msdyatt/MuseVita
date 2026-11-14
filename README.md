# MuseVita

**Created and maintained by [Sam Dyatt](https://github.com/msdyatt).**

**Repository:** `https://github.com/msdyatt/MuseVita`
**License:** MIT · Copyright © 2026 Sam Dyatt and MuseVita contributors

An unofficial, open-source PlayStation Vita client for Meta Muse. The planned
Nintendo 3DS client will be called MuseDS; portable protocol code is being kept
outside Vita-specific UI and networking so the two clients can share it.

> **Status: native VPK and protocol-foundation milestone.** MuseVita now has a
> real VitaSDK/CMake build, a packaged VPK, a controller-navigable
> status/setup/chat UI, a mockable Wi-Fi state provider, host-tested
> control-stream/link builders, and a Vita-built Noise XX initiator. It is not
> yet a live Muse client: pairing, credential storage/transfer, WSS session
> integration, VM lookup, chat requests, fixture interoperability, and real
> Vita hardware testing remain outstanding.

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
| Shared control-stream framing and link wire builders | Implemented and host-tested |
| Noise XX initiator using VitaSDK mbedTLS | Source implemented and Vita-built; fixture interoperability not verified |
| Local QR setup page | Demo only; local-development use |
| Approved Meta pairing and one-time transfer | Not implemented |
| Protected local credential storage | Not implemented |
| HTTPS certificate validation and VM lookup | Not implemented |
| WSS upgrade and encrypted link session | Not implemented |
| Direct chat send/subscribe and streamed replies | Not implemented |
| Vita hardware installation/playability | Not tested in this environment |

The VPK is a build artifact, not evidence of a live Muse connection. Do not
enter real tokens into the demo website or commit pairing state.

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
   as a gadget, then use `/chat/stream` and `/chat/subscribe` for text turns and
   NDJSON events. Never keep a second live control session for the same identity.

The implementation must be fixture-tested before any real credential is used.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/SECURITY.md](docs/SECURITY.md) for the security boundary and pending
work. No Meta or Sony affiliation is claimed.

## Publishing

See [PUBLISHING.md](PUBLISHING.md) for the initial GitHub repository and pull
request workflow. Public releases must not bundle SDK tokens, access/refresh
tokens, or personal pairing state.
