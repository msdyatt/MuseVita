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

## Stage a bundle for phone transfer or VitaShell

From the paired host, run this repository's validator. It does not contact
Muse or print credential values; it validates the identity, device tokens,
SDK-token format, and token hint before creating one phone-transfer bundle:

```sh
python3 tools/prepare_musevita_pairing.py \
  --state-dir /var/lib/musegadget \
  --output ./musevita-pairing.json
```

Scan the QR code on the Vita, choose `musevita-pairing.json` on the phone, and
press SELECT. The QR page downloads the Vita's non-secret `identity.json`
automatically. The phone encrypts the pairing JSON and SDK token locally before
uploading; the Vita converts them into `credentials.bin` and never serves them
back through the local URL. If you prefer VitaShell, omit `.json` from the
output path to keep the legacy directory containing `identity.json`,
`pairing.json`, and `sdk_token`.

The helper is intentionally required only for initial provisioning or a
re-pair. The normal MuseVita runtime does not depend on Linux, macOS, or a
relay process.

## Recommended: send directly from the pairing host

If the official SDK state already exists on the pairing host, no bundle file
or phone is needed. Put the Vita on the same Wi-Fi network, open MuseVita's
SETUP page, and use the manual URL shown below its QR code (for example,
`http://192.168.1.42:8768/`). Then run this on the host that owns the official
SDK state:

```sh
npm --prefix tools install
node tools/send_musevita_pairing.js \
  --state-dir /var/lib/musegadget \
  --vita-url http://192.168.1.42:8768/
```

The helper reads `identity.json`, `pairing.json`, and `sdk_token` in memory,
checks that the identity belongs to this Vita, encrypts the transfer using the
same ephemeral ECDH/HKDF/AES-GCM protocol as the QR page, and exits after one
upload. It does not print or create a credential file. Press SELECT on the
Vita when it reports that the pairing was sent.

This removes the download/upload step; it does not create a new login path.
The official SDK state must already have been created by Meta's supported
Gadget SDK pairing flow. The helper never scrapes Muse Web cookies, accepts a
Muse password, or sends credentials to a public server.

## Pair directly from macOS

The repository now includes a native CoreBluetooth companion for macOS. It
advertises the same Muse Gadget setup service as the published SDK, so the
Muse phone app can perform the approved one-time pairing and return the device
credentials. It then creates the expected `musevita-pairing.json` locally:

```sh
swift build --package-path mac/MuseVitaPair
swift run --package-path mac/MuseVitaPair musevita-pair \
  --vita-url http://VITA-IP:8768/ \
  --output ./musevita-pairing.json
```

The SDK token is read from a local file, the `MUSEGADGET_SDK_TOKEN` environment
variable, or a hidden prompt; never paste it into chat or put it in the
command line. The tool stops advertising after provisioning and writes the
file with owner-only permissions. This Mac flow is implemented and compile-
tested, but physical pairing still needs to be tested with the Muse app on a
Mac that has Bluetooth LE available.

## Pair directly from Windows

The repository also contains the Windows native source companion at `windows/MuseVitaPair`. Deploy it with its included Bluetooth-capable package manifest on Windows 10 version 1703+ or Windows 11, then use the Vita SETUP URL:

```powershell
musevita-pair --vita-url http://VITA-IP:8768/ --output .\musevita-pairing.json
```

The companion reads the Vita's non-secret identity through the one-time `/pair`/`/pair/identity` session, advertises `MuseGadgetXXXXXX`, and uses the published community v5 GATT setup exchange. It reads the user-owned SDK token from Credential Manager, `--sdk-token-file`, or a hidden prompt—not a command-line token. After the Muse app approves provisioning, it certificate-validates the returned device credential at HTTPS `/fetch_vms` and writes the one-file bundle with owner-only ACLs where Windows supports them.

This path is build- and public-vector-tested only. A real Windows Bluetooth adapter with LE peripheral/GATT-server support and an iPhone running the Muse app are still required to establish that the app can discover and pair. No permanent Windows bridge is needed after pairing; the Vita connects over Wi-Fi.

## Optional phone transfer

After the official host has produced the bundle, the Vita can transfer it
without navigating VitaShell. Connect the Vita and phone to the same Wi-Fi,
open the MuseVita SETUP page, and scan the QR code. The phone downloads the
Vita identity from that pinned local session; choose only
`musevita-pairing.json`, then press SELECT on the Vita.
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
