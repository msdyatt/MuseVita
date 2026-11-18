#!/usr/bin/env node

/*
 * Send an already-approved official Muse Gadget pairing directly to MuseVita.
 *
 * This is deliberately a local helper, not a login client. It reads the
 * official SDK state on the pairing host, verifies that its identity matches
 * the Vita's one-time local session, encrypts the state for the Vita's
 * ephemeral P-256 key, and then exits. No credential value is printed or
 * written to a new file.
 */

const fs = require('fs');
const path = require('path');
const { webcrypto } = require('crypto');

if (!globalThis.crypto) globalThis.crypto = webcrypto;
const { seal } = require('./pairing_crypto_entry.js');

const BUNDLE_FORMAT = 'musevita-pairing-v1';
const TOKEN_RE = /^mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$/;
const MAC_RE = /^[0-9a-f]{2}(?::[0-9a-f]{2}){5}$/i;
const INFO = 'MuseVita pairing transfer v1';

function usage() {
  process.stderr.write(
    'Usage: node tools/send_musevita_pairing.js --vita-url http://VITA-IP:8768/ ' +
    '[--state-dir /var/lib/musegadget]\n'
  );
}

function fail(message) {
  throw new Error(message);
}

function parseArgs(argv) {
  const options = {
    stateDir: process.env.MUSEGADGET_STATE_DIR || '/var/lib/musegadget',
    vitaUrl: null,
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--state-dir' && argv[i + 1]) options.stateDir = argv[++i];
    else if (arg === '--vita-url' && argv[i + 1]) options.vitaUrl = argv[++i];
    else if (arg === '--help' || arg === '-h') return null;
    else fail(`unknown option: ${arg}`);
  }
  if (!options.vitaUrl) fail('--vita-url is required');
  return options;
}

function readText(stateDir, name) {
  try {
    return fs.readFileSync(path.join(stateDir, name), 'utf8');
  } catch (_error) {
    fail(`official SDK state is missing ${name}`);
  }
}

function readState(stateDir) {
  const identityText = readText(stateDir, 'identity.json');
  const pairingText = readText(stateDir, 'pairing.json');
  const token = readText(stateDir, 'sdk_token').trim();
  let identity;
  let pairing;
  try {
    identity = JSON.parse(identityText);
    pairing = JSON.parse(pairingText);
  } catch (_error) {
    fail('official SDK identity or pairing state is not valid JSON');
  }
  if (!identity || typeof identity.mac !== 'string' || !MAC_RE.test(identity.mac)) {
    fail('official SDK identity has an invalid device identity');
  }
  if (!pairing || typeof pairing.access_token !== 'string' ||
      !pairing.access_token.trim() || typeof pairing.refresh_token !== 'string' ||
      !pairing.refresh_token.trim()) {
    fail('official SDK pairing state is missing device credentials');
  }
  if (!TOKEN_RE.test(token)) fail('official SDK token has an invalid format');
  if (pairing.sdk_token_hint !== undefined && pairing.sdk_token_hint !== token.slice(0, 12)) {
    fail('official SDK pairing state and SDK token do not match');
  }
  return { identityText, identity, pairingText, token };
}

function base64Url(bytes) {
  return Buffer.from(bytes).toString('base64')
    .replaceAll('+', '-').replaceAll('/', '_').replaceAll('=', '');
}

function decodeBase64Url(value) {
  return Uint8Array.from(Buffer.from(value.replaceAll('-', '+').replaceAll('_', '/'), 'base64'));
}

function localHttpUrl(value) {
  let url;
  try {
    url = new URL(value);
  } catch (_error) {
    fail('Vita URL is not valid');
  }
  if (url.protocol !== 'http:' || !url.hostname || url.username || url.password) {
    fail('Vita URL must be a plain local http:// setup URL');
  }
  return url;
}

async function getSetup(baseUrl) {
  const response = await fetch(baseUrl, { redirect: 'manual' });
  if (response.status < 300 || response.status >= 400) {
    fail(`Vita setup URL did not redirect (HTTP ${response.status})`);
  }
  const location = response.headers.get('location');
  if (!location) fail('Vita setup URL returned no pairing session');
  const pairUrl = new URL(location, baseUrl);
  if (pairUrl.protocol !== 'http:' || pairUrl.host !== baseUrl.host || pairUrl.pathname !== '/pair') {
    fail('Vita setup URL redirected outside the local pairing server');
  }
  const sid = pairUrl.searchParams.get('s');
  const publicKey = pairUrl.searchParams.get('k');
  if (!sid || !publicKey) fail('Vita pairing session is incomplete');
  return { origin: pairUrl.origin, sid, publicKey };
}

async function getJson(url, description) {
  const response = await fetch(url);
  if (!response.ok) fail(`${description} failed (HTTP ${response.status})`);
  try {
    return await response.json();
  } catch (_error) {
    fail(`${description} returned invalid JSON`);
  }
}

async function getText(url, description) {
  const response = await fetch(url);
  if (!response.ok) fail(`${description} failed (HTTP ${response.status})`);
  return response.text();
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options === null) {
    usage();
    return;
  }
  const state = readState(options.stateDir);
  const baseUrl = localHttpUrl(options.vitaUrl);
  const setup = await getSetup(baseUrl);
  const configUrl = new URL('/pair/config', setup.origin);
  configUrl.searchParams.set('s', setup.sid);
  const config = await getJson(configUrl, 'Vita pairing configuration');
  if (config.sid !== setup.sid || config.pub !== setup.publicKey) {
    fail('Vita pairing session changed; reopen SETUP and try again');
  }

  const identityUrl = new URL('/pair/identity', setup.origin);
  identityUrl.searchParams.set('s', setup.sid);
  const vitaIdentityText = await getText(identityUrl, 'Vita identity');
  let vitaIdentity;
  try {
    vitaIdentity = JSON.parse(vitaIdentityText);
  } catch (_error) {
    fail('Vita identity is not valid JSON');
  }
  if (!vitaIdentity || typeof vitaIdentity.mac !== 'string' ||
      vitaIdentity.mac.toLowerCase() !== state.identity.mac.toLowerCase()) {
    fail('official SDK state belongs to a different device identity');
  }

  const bundle = {
    format: BUNDLE_FORMAT,
    identity: vitaIdentityText,
    pairing: state.pairingText,
    sdk_token: state.token,
  };
  const sealed = seal(decodeBase64Url(setup.publicKey), setup.sid, bundle);
  const uploadUrl = new URL('/pair/upload', setup.origin);
  uploadUrl.searchParams.set('s', setup.sid);
  const response = await fetch(uploadUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      sid: setup.sid,
      epk: base64Url(sealed.epk),
      iv: base64Url(sealed.iv),
      ct: base64Url(sealed.ct),
    }),
  });
  if (!response.ok) fail(`Vita rejected the pairing transfer (HTTP ${response.status})`);
  process.stdout.write('Pairing sent securely. Press SELECT on the Vita.\n');
}

main().catch(error => {
  process.stderr.write(`MuseVita pairing: ${error.message}\n`);
  process.exitCode = 1;
});

