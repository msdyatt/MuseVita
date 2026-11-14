# One-time MuseVita pairing

MuseVita does not collect a Muse password or browser cookie. The official Muse
Gadget SDK pairing is a BLE GATT setup flow. The Vita client uses the resulting
device credentials over Wi-Fi afterward; the BLE-capable helper is not a
permanent bridge.

## Pair once on a supported BLE host

1. Install MuseVita and launch it once. Exit, then use VitaShell to copy
   `ux0:data/MuseVita/identity.json` to the state directory of a supported
   BLE-capable host running the official Muse Gadget SDK.
2. Put your own Gadget SDK token in that host's `sdk_token` file. Never put it
   in the QR demo or commit it.
3. Start the official SDK pairing flow on that host and complete **Settings >
   Devices > Add Device** in the Muse mobile app. The host must use the Vita's
   seeded identity, so do not let it generate a second identity.
4. Stop the helper's gadget service after the app reports the device paired.
   Only one live registration should use this identity.

## Stage the files for VitaShell

From the paired host, run this repository's validator. It does not contact
Muse or print credential values; it validates the identity, device tokens,
SDK-token format, and token hint before copying exactly three files:

```sh
python3 tools/prepare_musevita_pairing.py \
  --state-dir /var/lib/musegadget \
  --output ./musevita-pairing
```

Copy `identity.json`, `pairing.json`, and `sdk_token` from that new directory
to `ux0:data/MuseVita/` with VitaShell. On first launch, MuseVita encrypts the
credentials locally into `credentials.bin` and removes the plaintext import
files. From then on, the Vita connects directly to Muse over Wi-Fi.

The helper is intentionally required only for initial provisioning or a
re-pair. The normal MuseVita runtime does not depend on Linux, macOS, or a
relay process.

## Optional phone transfer

After the official host has produced the three files, the Vita can transfer
them without navigating VitaShell. Connect the Vita and phone to the same
Wi-Fi, open the MuseVita SETUP page, scan the QR code, select `identity.json`,
`pairing.json`, and `sdk_token` on the phone, then press SELECT on the Vita.
The phone page uses browser WebCrypto when available, with a bundled
pure-JavaScript fallback for local HTTP browsers, using
ECDH/HKDF-SHA-256/AES-256-GCM against the ephemeral public key pinned in the QR
URL. The Vita accepts one matching session only, keeps the decrypted bundle in
memory until SELECT, and then stores only the encrypted credential record. The
QR never contains a Muse access token, refresh token, SDK token, password, or
browser cookie.

This is a local-network convenience path, not a replacement for the official
BLE/account authorization step. Physical phone/Vita validation remains a
release gate.

## Hosting boundary

The pairing page is intentionally served by the Vita at the one-time local
URL in the QR code. A public HTTPS copy on `samdyatt.com` can provide help or
download the VPK, but it cannot transparently replace the local page: browser
mixed-content and cross-origin protections prevent a hosted page from being a
general-purpose HTTP client for the Vita. Adding a public relay would also
change the non-custodial security model, so it is not part of this release.
