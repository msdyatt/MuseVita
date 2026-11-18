import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PairingCryptoTests(unittest.TestCase):
    def test_bundled_fallback_round_trips_with_webcrypto(self):
        node = shutil.which("node")
        asset = ROOT / "vita/assets/pairing-crypto.js"
        if node is None or not asset.exists():
            self.skipTest("Node.js and the built pairing crypto asset are required")

        script = r"""
const fs = require('fs');
const vm = require('vm');
const { webcrypto } = require('crypto');

const context = {
  crypto: webcrypto,
  JSON,
  Math,
  TextEncoder,
  TextDecoder,
  Uint8Array,
  ArrayBuffer,
  DataView,
};
context.globalThis = context;
vm.runInNewContext(fs.readFileSync(process.argv[1], 'utf8'), context);

(async () => {
  const sid = 'ABCDEFGHJKLMNPQR';
  const bundle = {
    identity: '{"mac":"02:00:00:00:00:01"}',
    pairing: '{"access_token":"synthetic-access","refresh_token":"synthetic-refresh"}',
    sdk_token: 'mgst_synthetic-token-for-test',
  };
  const vita = await webcrypto.subtle.generateKey(
    { name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveBits']);
  const vitaPublic = new Uint8Array(await webcrypto.subtle.exportKey('raw', vita.publicKey));
  const sealed = context.MuseVitaCrypto.seal(vitaPublic, sid, bundle);
  const ephemeral = await webcrypto.subtle.importKey(
    'raw', sealed.epk, { name: 'ECDH', namedCurve: 'P-256' }, false, []);
  const bits = await webcrypto.subtle.deriveBits(
    { name: 'ECDH', public: ephemeral }, vita.privateKey, 256);
  const hkdf = await webcrypto.subtle.importKey('raw', bits, 'HKDF', false, ['deriveKey']);
  const aes = await webcrypto.subtle.deriveKey(
    { name: 'HKDF', hash: 'SHA-256', salt: new TextEncoder().encode(sid),
      info: new TextEncoder().encode('MuseVita pairing transfer v1') },
    hkdf, { name: 'AES-GCM', length: 256 }, false, ['decrypt']);
  const clear = await webcrypto.subtle.decrypt(
    { name: 'AES-GCM', iv: sealed.iv, additionalData: new TextEncoder().encode(sid) },
    aes, sealed.ct);
  process.stdout.write(new TextDecoder().decode(clear));
})().catch(error => { console.error(error); process.exit(1); });
"""
        result = subprocess.run([node, "-e", script, str(asset)], check=True,
                                capture_output=True, text=True)
        self.assertIn('"sdk_token":"mgst_synthetic-token-for-test"', result.stdout)
        self.assertIn('02:00:00:00:00:01', result.stdout)


if __name__ == "__main__":
    unittest.main()
