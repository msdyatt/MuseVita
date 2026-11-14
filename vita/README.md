# Muse Vita native scaffold

This is a **native UI scaffold**, not an authenticated Muse client. It renders a Vita-specific welcome/setup screen and responds to controller input. **Do not interpret an installed VPK as a working connection to Muse.**

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

Once prerequisites are installed, look for `build-vita/muse_vita.vpk`. Transfer it with VitaShell and install.

## Controls

- X: switch welcome/setup information
- START: exit

## Not implemented

- Real-time Muse chat, token refresh, pairing transfer, direct Noise XX/WebSocket client, TLS validation, local credential storage, Wi-Fi status, IME keyboard, QR scanner or display.
- A functional website-to-Vita pairing link.

The network/crypto implementation must be tested against Meta's SDK test fixtures before credentials are ever used on the handheld.
