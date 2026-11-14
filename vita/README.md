# MuseVita native application

This is a native UI and protocol-framing foundation, not yet an authenticated Muse client. It renders a Vita-specific status/setup/chat screen, reads the Vita Wi-Fi state, and responds to controller input. **Do not interpret an installed VPK as a verified connection to Muse.**

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

- X: next page
- Circle: previous page
- START: exit

## Not implemented

- Real-time Muse chat, token refresh, pairing transfer, WSS session integration, local credential storage, IME keyboard, QR scanner or display.
- A functional website-to-Vita pairing link. The web page remains a local demo only.

The portable `protocol/control_framing.*` module is shared-code groundwork for
MuseVita and the planned MuseDS client. It implements the official SDK's
little-endian length-prefixed control-message framing and is host-tested. The
portable `protocol/noise_xx.*` module implements the official Noise XX
initiator with VitaSDK mbedTLS and is included in the VPK build; it still needs
fixture interoperability tests and a real WSS peer. Neither module handles
pairing credentials.

The network/crypto implementation must be tested against Meta's SDK test fixtures before credentials are ever used on the handheld.
