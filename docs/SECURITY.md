# Security constraints for public distribution

The present website is a **local QR-session demonstration only**. It never
accepts pairing credentials, does not call Muse APIs, and does not authenticate
devices. The native Vita build currently accepts only a user-provided official
`identity.json`/`pairing.json` import through local Vita storage; it does not
perform BLE pairing or browser-cookie login.

Before any public deployment or real credential transfer:

1. Confirm current Meta Gadget SDK Terms allow the planned distribution and user-auth flow. Require each user to provide their own SDK token without sharing it with the service operator.
2. Keep the browser wizard non-custodial: no tokens or secrets submitted to our web backend. The current Vita import converts the official plaintext pairing file to an authenticated encrypted local blob and deletes the plaintext copy; this is a containment measure, not a substitute for hardware-backed secret storage.
3. Add authenticated possession proof to setup sessions (a random code alone cannot securely bind an account to a console), replay prevention, one-time consumption, account/device binding, expiry and anti-CSRF/rate limits.
4. Test the Vita-side TLS trust root configuration, system clock, certificate hostname verification, WebSocket upgrade and Noise XX with the Meta SDK test vectors. No `verify=False`, hard-coded root credentials or implicit HTTP downgrades.
5. Store long-lived secrets in protected app-local storage where possible; assess jailbreak/homebrew extraction risks and provide clear unpair/revoke controls.
6. Treat the pairing helper as privileged and untrusted input as potentially hostile. Protect against token leakage through telemetry, screenshots, crash logs, browser URLs, referrers and clipboard history.
7. Ensure one device identity only has one live registration session. Revoke/unpair safely, including old pairing host copies.
8. Do a security review before public launch. The starter web server is not production ready and should not be internet exposed.
