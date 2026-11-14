const $ = id => document.getElementById(id);
const params = new URLSearchParams(location.search);
let activeUrl = "";

async function createSession() {
  $('notice').textContent = '';
  $('create').disabled = true;
  try {
    const response = await fetch('/api/sessions', { method:'POST' });
    if (!response.ok) throw Error(`Server responded ${response.status}`);
    const s = await response.json();
    showSession(s);
  } catch (err) {
    $('notice').textContent = `Could not start demo: ${err.message}`;
  } finally {
    $('create').disabled = false;
  }
}

function showSession(s) {
  $('empty').classList.add('hidden');
  $('session').classList.remove('hidden');
  $('qr').src = s.qr_url ?? `/api/qr.svg?code=${encodeURIComponent(s.code)}`;
  $('code').textContent = s.code;
  $('expiry').textContent = `Expires ${new Date(s.expires_at*1000).toLocaleTimeString()}`;
  $('state').textContent = 'Setup session ready · not paired';
  activeUrl = new URL(`/pair?code=${encodeURIComponent(s.code)}`, location.origin).href;
}

$('create').addEventListener('click', createSession);
$('copy').addEventListener('click', async () => {
  if (!activeUrl) return;
  try { await navigator.clipboard.writeText(activeUrl); $('notice').textContent = 'Demo link copied.'; }
  catch { $('notice').textContent = `Copy this link: ${activeUrl}`; }
});

const incomingCode = params.get('code');
if (incomingCode) {
  fetch(`/api/sessions/${encodeURIComponent(incomingCode)}`)
    .then(async r => { if(!r.ok) throw Error('This code is expired or unknown.'); return r.json(); })
    .then(showSession)
    .catch(err => { $('notice').textContent = err.message; });
}
