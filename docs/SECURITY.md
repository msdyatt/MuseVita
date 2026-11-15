# Security constraints for public distribution

The `web/` website is a **local QR-session demonstration only**. It never
accepts pairing credentials, does not call Muse APIs, and does not authenticate
devices. The native Vita build also has a separate local-network transfer page:
it accepts one encrypted browser upload pinned to an ephemeral Vita P-256 key,
then validates and stores only the encrypted local credential record. It does
not perform BLE pairing or browser-cookie login.

Before any public deployment or real credential transfer:

1. Confirm current Meta Gadget SDK Terms allow the planned distribution and user-auth flow. Require each user to provide their own SDK token without sharing it with the service operator.
2. Keep the browser wizard non-custodial: no tokens or secrets submitted to our web backend. The current Vita import converts the official plaintext pairing file to an authenticated encrypted local blob and deletes the plaintext copy; this is a containment measure, not a substitute for hardware-backed secret storage.
3. The on-device transfer pins the browser upload to the public key displayed
   in the Vita QR, expires the session after ten minutes, and consumes it after
   one upload. The phone page may use its bundled pure-JavaScript crypto
   fallback when local HTTP prevents WebCrypto; it still sends only the same
   authenticated encrypted blob. Before public deployment, add a broader security review of local
   network threat assumptions, rate limits, origin checks, HTTPS/HSTS, and
   account/device binding; no service should log secrets or ciphertext.
4. Test the Vita-side TLS trust root configuration, system clock, certificate hostname verification, WebSocket upgrade and Noise XX with the Meta SDK test vectors. No `verify=False`, hard-coded root credentials or implicit HTTP downgrades.
5. Store long-lived secrets in protected app-local storage where possible; assess jailbreak/homebrew extraction risks and provide clear unpair/revoke controls.
6. Treat the pairing helper as privileged and untrusted input as potentially hostile. Protect against token leakage through telemetry, screenshots, crash logs, browser URLs, referrers and clipboard history.
7. Ensure one device identity only has one live registration session. Revoke/unpair safely, including old pairing host copies.
8. Do a security review before public launch. The starter web server is not production ready and should not be internet exposed.

The Windows pairing companion keeps an SDK token out of command-line arguments and logs, uses Credential Manager or an explicit local token file/hidden prompt, and applies an owner-only ACL to its output where supported. It accepts the Vita identity only from the one-time same-host `/pair` session, uses strict un-padded Base64URL and ordered AES-GCM records, and validates the provisioned device credential via certificate-validated HTTPS `/fetch_vms` before it writes a bundle. It does not accept a Muse password, Web cookie, or browser session, and it does not weaken certificate validation.
