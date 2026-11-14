# MuseVita native application

This is a native Vita UI and direct Muse Gadget SDK transport client. It renders
a Vita-specific status/setup/chat screen, reads the Vita Wi-Fi state, imports
official pairing state, and uses direct HTTPS/WSS transport. **Do not interpret
an installed VPK as a verified connection to Muse until a user-owned pairing is
tested on physical hardware.**

## Build prerequisites

- A working [VitaSDK](https://vitasdk.org/) cross-compiler and CMake integration
- `vita2d` and its dependencies installed in the VitaSDK prefix (usually via `vdpm`)
- A homebrew-capable Vita to install the produced VPK

## Build

```sh
export VITASDK=/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S vita -B build-vita -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j
```

Once prerequisites are installed, look for `build-vita/MuseVita.vpk`. Transfer it with VitaShell and install.

## Controls

- X: next page; on Chat, open the Vita text composer
- Circle: previous page
- Square: send the current Chat draft
- Triangle: open the web fallback
- START: exit

## Current limitations

- On-device BLE pairing, QR scanning, chat history pagination, and
  camera/microphone capabilities are not implemented yet. Transport reconnect
  retries after a bounded delay when the imported credentials remain valid.
- Cross-runtime Noise fixture tests and validation against a real user-owned
  Muse account on a physical Vita are still required.

The portable `protocol/control_framing.*` module is shared-code groundwork for
MuseVita and the planned MuseDS client. It implements the official SDK's
little-endian length-prefixed control-message framing and is host-tested. The
portable `protocol/noise_xx.*` module implements the official Noise XX
initiator with VitaSDK mbedTLS and is included in the VPK build; it still needs
fixture interoperability tests and a real WSS peer. Credential import and
encrypted local storage live in `src/muse_session.c`.

The network/crypto implementation must be tested against Meta's SDK test fixtures before credentials are ever used on the handheld.
