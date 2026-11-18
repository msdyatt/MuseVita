const { p256 } = require('@noble/curves/nist.js');
const { gcm } = require('@noble/ciphers/aes.js');
const { hkdf } = require('@noble/hashes/hkdf.js');
const { sha256 } = require('@noble/hashes/sha2.js');

const encoder = new TextEncoder();
const INFO = encoder.encode('MuseVita pairing transfer v1');

function seal(vitaPublicKey, sessionId, bundle) {
  if (!globalThis.crypto || typeof globalThis.crypto.getRandomValues !== 'function') {
    throw new Error('This browser cannot provide secure random values.');
  }

  const ephemeral = p256.keygen();
  const sharedPoint = p256.getSharedSecret(ephemeral.secretKey, vitaPublicKey, false);
  const sharedX = sharedPoint.slice(1, 33);
  const key = hkdf(sha256, sharedX, encoder.encode(sessionId), INFO, 32);
  const iv = globalThis.crypto.getRandomValues(new Uint8Array(12));
  const aad = encoder.encode(sessionId);
  const plaintext = encoder.encode(JSON.stringify(bundle));
  const ciphertext = gcm(key, iv, aad).encrypt(plaintext);

  return {
    epk: ephemeral.publicKey.length === 65
      ? ephemeral.publicKey
      : p256.getPublicKey(ephemeral.secretKey, false),
    iv,
    ct: ciphertext,
  };
}

module.exports = { seal };
