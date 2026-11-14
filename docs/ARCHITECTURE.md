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
4. The gadget opens `POST /link-control`, sends `link.register`, then uses `POST /chat/stream` to send messages and `/chat/subscribe` for live events; `/chat/history` supports history reads. See Linux SDK's `link_client.py`, `muse_turn.py`, and `AGENTS.md` for exact record framing and behavior.
5. Token lifecycle includes refresh and reconnection; temporary setup hosts must shut down before Vita uses the same persistent identity to avoid concurrent active sessions.

**Do not guess any cryptographic framing from this document.** Port the SDK's exact fixture-tested behavior. Inspect `linux/tests/vectors/` and `linux/tests/test_link_client.py` in the current SDK source, and validate with the real service before shipping.

## Vita implementation modules

- `platform/vita_http`: libcurl TLS/WebSocket client, certificate verification,
  VM lookup, refresh, and network transport (`vita/src/muse_session.c`)
- `protocol/noise_xx`: the initiator state machine is implemented with VitaSDK
  mbedTLS and low-order-point rejection; cross-runtime fixture verification remains
- `protocol/link`: shared stream framing, register/invoke/result, HTTP-over-link
  request encoding, and response decoding are implemented
- `protocol/chat`: chat send, NDJSON subscription, and streamed event rendering
  are implemented; history pagination remains and transport reconnect now
  retries after a bounded delay
- `storage/credentials`: one-time plaintext import is converted to an encrypted
  local credential blob; hardware extraction review, revoke, and rotation remain
- `ui`: chat, text input, pairing QR, status, notifications

The transport entries above describe source-level implementation; live-account
and physical-Vita validation is still a separate release gate.

## Why the QR does NOT itself authorize Muse

The user-facing QR carries a temporary session reference, not a Muse account token. Current Muse mobile pairing requires BLE device advertisement and interactive confirmation; Web Bluetooth in ordinary browsers does not implement this peripheral role. Browser-to-website sessions cannot mint legitimate gadget tokens or device credentials just by generating a QR code. A one-time pairing helper is needed unless Meta officially adds QR/OAuth device provisioning.

## One-time helper

Prefer an audited OS-native BLE helper for macOS/Windows/Linux with the official flow, no embedded SDK token, and explicit confirmation. Start by proving compatible pairing on a platform that can advertise BLE; transfer state only after successful pairing, then stop/unpair the helper locally. Distribution of such a utility must follow Meta's SDK terms.

## Transfer design for future milestone

Vita should generate an ephemeral receiver key pair and show `session_id` plus public key in a QR. Pairing helper verifies the session and user confirmation, encrypts only for that Vita's public key and uploads ciphertext to a short-lived relay. Vita downloads ciphertext and decrypts locally. Enforce single redemption, expiry, rate limits, origin checks, HTTPS/HSTS and no logs of secrets or ciphertext. Do not use ad hoc crypto or put an access/refresh token in a QR.
