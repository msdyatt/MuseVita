# MuseVita architecture and protocol notes

## Target

- Device: PlayStation Vita running approved homebrew ecosystem / VitaSDK app
- Future sibling: Nintendo 3DS client named MuseDS, sharing portable protocol modules
- Model: one-time provisioning, then **direct client**; no persistent gateway
- Local networking: Vita Wi-Fi -> Meta Muse API/VM
- Distribution: user-supplied SDK token; no account credential collection on our public site

## Muse transport (documented SDK behavior)

1. Initial authorization is currently BLE-based via Muse mobile application and `pairing_client_hello` / `pairing_encrypted` / `provision_v2` exchanges.
2. Community pairing v5 includes P-256 ECDH, HKDF-SHA256 and AES-256-GCM. The SDK obtains device access/refresh credentials and VM info. SDK tokens must be user-owned.
3. Paired gadget fetches its VM and connects to `wss://<noise_host>/v1/noise?vm_id=<id>` with a per-VM bearer; upgrade is followed by Noise XX session establishment.
4. The gadget opens `POST /link-control`, sends `link.register`, then uses `POST /chat/stream` to send messages, `GET /chat/history?limit=1` for one bounded initial item, and `/chat/subscribe` for live events. The history request is optional and bounded; live subscription remains the source of new events. See the SDK's `link_client.py`, `muse_turn.py`, and `AGENTS.md` for exact record framing and behavior.
5. Token lifecycle includes refresh and reconnection; temporary setup hosts must shut down before Vita uses the same persistent identity to avoid concurrent active sessions.

**Do not guess any cryptographic framing from this document.** Port the SDK's exact fixture-tested behavior. Inspect `linux/tests/vectors/` and `linux/tests/test_link_client.py` in the current SDK source, and validate with the real service before shipping.

## Vita implementation modules

- `platform/vita_http`: libcurl TLS/WebSocket client, certificate verification,
  VM lookup, refresh, and network transport (`vita/src/muse_session.c`)
- `protocol/noise_xx`: the initiator state machine is implemented with VitaSDK
  mbedTLS and low-order-point rejection; cross-runtime fixture verification
  passes against the pinned official SDK, while a real WSS peer remains
  unverified
- `protocol/link`: shared stream framing, register/invoke/result, HTTP-over-link
  request encoding, and response decoding are implemented
- `protocol/chat`: bounded initial history request, chat send, NDJSON
  subscription, streamed event rendering, and bounded in-memory history are
  implemented; older-page pagination remains future work
- `vita/src/vita_media`: bounded one-shot Vita camera/JPEG and microphone/WAV
  capture paths that send official image/file items through the shared chat
  stream; physical capture validation remains a release gate
- `storage/credentials`: one-time plaintext import is converted to an encrypted
  local credential blob; hardware extraction review, revoke, and rotation remain
- `ui`: chat, text input, pairing QR, status, notifications

The transport entries above describe source-level implementation; live-account
and physical-Vita validation is still a separate release gate.

## Why the QR does NOT itself authorize Muse

The user-facing QR carries a temporary session reference, not a Muse account token. Current Muse mobile pairing requires BLE device advertisement and interactive confirmation; Web Bluetooth in ordinary browsers does not implement this peripheral role. Browser-to-website sessions cannot mint legitimate gadget tokens or device credentials just by generating a QR code. A one-time pairing helper is needed unless Meta officially adds QR/OAuth device provisioning.

## One-time helper

Prefer an audited OS-native BLE helper for macOS/Windows/Linux with the official flow, no embedded SDK token, and explicit confirmation. Start by proving compatible pairing on a platform that can advertise BLE; transfer state only after successful pairing, then stop/unpair the helper locally. Distribution of such a utility must follow Meta's SDK terms.

### Windows companion

`windows/MuseVitaPair` is the Windows 10 1703+/Windows 11 implementation. It uses `GattServiceProvider` for the connectable setup service and local RX/TX characteristics, plus `BluetoothLEAdvertisementPublisher` for the SDK's informational `0xffff:00` manufacturer record. Its package manifest declares the Bluetooth device capability. It fetches the one-time Vita identity from `/pair/identity` only after checking the same-host `/pair` redirect, performs the pinned v5 P-256/HKDF/AES-GCM exchange, validates `provision_v2` with HTTPS `/fetch_vms`, and saves the exact one-file bundle for MuseVita.

This is source-level implementation, not evidence that a Windows radio stack advertises the combined records the Muse mobile app requires. Physical Windows adapter and iPhone/Muse-app verification remains a release gate.

## Current one-time transfer design

Vita generates an ephemeral receiver key pair and shows `session_id` plus the
public key in a QR. The phone downloads only the Vita's non-secret identity
from the pinned local session, reads one validated `musevita-pairing.json`
bundle produced after official pairing, encrypts the pairing state only for
that Vita public key, and uploads ciphertext locally. Vita decrypts and
validates it, then accepts one redemption before storing encrypted credentials.
The session expires, the upload is single-use, and no access/refresh token or
SDK token is placed in the QR or served by the local identity endpoint. Do not
use ad hoc crypto or treat the QR as account authorization.
