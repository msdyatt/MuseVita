# MuseVita implementation brief

Act as a senior Vita homebrew developer and security-conscious systems engineer.

Repository maintainer: **Sam Dyatt** (`@msdyatt`).
Target project: native Muse client for PlayStation Vita, with direct internet
connectivity and one-time approved gadget provisioning. No always-on bridge.

1. Read `AGENTS.md`, `README.md`, `docs/ARCHITECTURE.md`, `docs/SECURITY.md`,
   and the official Meta Muse Gadget SDK.
2. Audit the current VitaSDK CMake and `vita/src/main.c` for compile issues.
3. In a reproducible toolchain, compile a VPK and fix concrete build errors.
4. Implement a Vita-native chat UI skeleton with a useful offline state and
   controller/IME input, keeping network features mockable.
5. Add host-side tests for any reusable protocol/parser code.
6. Do not collect or hard-code credentials. Do not deploy the demo web server.
7. Document exactly what compiles and is hardware-tested, and what's pending.
8. Open a PR with a test summary and screenshots if Vita hardware is available.

Do **not** simulate a connection and claim it is a live Muse chat. After the
UI builds, the next PR should port the exact Noise XX/WSS transport from the
official SDK and verify it with fixture-based tests.

The final result should leave a clean main branch, an understandable MVP roadmap,
and a native Vita app that can grow into a real Muse client.
