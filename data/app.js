'use strict';

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s).replace(/[&<>"']/g,
  (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// ---------------------------------------------------------------- helpers
async function getJson(url) {
  const r = await fetch(url, { cache: 'no-store' });
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || `HTTP ${r.status}`);
  return j;
}

async function post(url, data) {
  const body = new URLSearchParams(data || {});
  const r = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
  });
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || `HTTP ${r.status}`);
  return j;
}

function flash(el, msg, bad) {
  el.textContent = msg;
  el.classList.toggle('bad', !!bad);
  clearTimeout(el._t);
  el._t = setTimeout(() => { el.textContent = ''; el.classList.remove('bad'); }, 4000);
}

// ---------------------------------------------------------------- tabs
document.querySelectorAll('.tab').forEach((btn) => {
  btn.onclick = () => {
    document.querySelectorAll('.tab').forEach((b) => b.classList.toggle('active', b === btn));
    document.querySelectorAll('.panel').forEach((p) =>
      p.classList.toggle('active', p.id === 'tab-' + btn.dataset.tab));
  };
});

// ---------------------------------------------------------------- profiles
async function loadProfiles() {
  try {
    const d = await getJson('/api/profiles');
    $('#profiles').innerHTML = d.profiles.map((p) => `
      <button class="profile ${p.id === d.current ? 'on' : ''}" data-id="${esc(p.id)}">
        <b>${esc(p.label)}</b>
        <span>${esc(p.appearance)}</span>
      </button>`).join('');

    document.querySelectorAll('.profile').forEach((b) => {
      b.onclick = async () => {
        try {
          await post('/api/emul/profile', { id: b.dataset.id });
          await loadProfiles();
          await refresh();
          await loadIdentity();
        } catch (e) { flash($('#payloadMeta'), e.message, true); }
      };
    });
  } catch (e) {
    $('#profiles').innerHTML = `<p class="empty">${esc(e.message)}</p>`;
  }
}

// ---------------------------------------------------------------- status
function setPill(ble) {
  const pill = $('#statePill');
  const map = {
    ready:       ['ready', 'ok'],
    connected:   ['connected', 'warn'],
    advertising: ['advertising', 'warn'],
    idle:        ['idle', ''],
  };
  const [text, cls] = map[ble.state] || [ble.state, ''];
  pill.textContent = text;
  pill.className = 'pill ' + cls;
}

function row(k, v) {
  return `<dt>${esc(k)}</dt><dd>${v}</dd>`;
}

async function refresh() {
  let s;
  try {
    s = await getJson('/api/status');
  } catch (_) {
    $('#statePill').textContent = 'offline';
    $('#statePill').className = 'pill bad';
    return;
  }

  const b = s.ble;
  const p = s.payload;
  setPill(b);

  const stateTxt = b.ready ? '<b class="good">HID ready</b>'
    : b.connected ? 'connected, securing…'
    : b.advertising ? `advertising · ${b.left}s left`
    : 'not advertising';

  $('#connList').innerHTML =
    row('link', stateTxt) +
    row('profile', esc(b.profile)) +
    row('name', esc(b.name)) +
    row('appearance', esc(b.appearance)) +
    row('mode', b.mode === 'swift' ? 'Swift Pair' : 'beacon') +
    row('bonds', b.bonds) +
    row('access point', `${esc(s.ssid)}${s.open ? ' · <b class="warn">open</b>' : ''} · <span class="mono">${esc(s.ip)}</span>`);

  $('#infoList').innerHTML =
    row('version', esc(s.version)) +
    row('chip', esc(s.chip)) +
    row('ip', esc(s.ip)) +
    row('uptime', `${s.uptime} s`) +
    row('free heap', `${Math.round(s.heap / 1024)} KB`);

  // ---- payload status
  const bar = $('#progBar');
  bar.style.width = p.progress + '%';

  if (p.state === 'countdown') {
    $('#payloadMeta').textContent = `starting in ${p.countdown}s…`;
    bar.classList.add('hot');
  } else if (p.state === 'running') {
    $('#payloadMeta').textContent = `typing · ${p.progress}% (${p.current}/${p.steps})`;
    bar.classList.add('hot');
  } else if (p.state === 'error') {
    $('#payloadMeta').textContent = p.error || 'error';
    bar.classList.remove('hot');
  } else {
    $('#payloadMeta').textContent = p.state;
    bar.classList.remove('hot');
  }

  const busy = p.state === 'running' || p.state === 'countdown';
  $('#runBtn').disabled = busy || !b.connected;
  $('#stopPayloadBtn').disabled = !busy;

  $('#startBtn').disabled = b.connected || b.advertising;
  $('#stopBtn').disabled = !b.connected && !b.advertising;
  $('#forgetBtn').disabled = b.bonds === 0;
}

// ---------------------------------------------------------------- controls
$('#startBtn').onclick = async () => {
  try {
    await post('/api/emul/start', { duration: $('#duration').value });
    await refresh();
  } catch (e) { flash($('#payloadMeta'), e.message, true); }
};

$('#stopBtn').onclick = async () => {
  try { await post('/api/emul/stop'); await refresh(); }
  catch (e) { flash($('#payloadMeta'), e.message, true); }
};

$('#forgetBtn').onclick = async () => {
  if (!confirm('Clear every stored Bluetooth bond? The host will have to pair again.')) return;
  try { await post('/api/emul/forget'); await refresh(); }
  catch (e) { flash($('#payloadMeta'), e.message, true); }
};

// ---------------------------------------------------------------- payload
async function loadScript() {
  try {
    const d = await getJson('/api/payload');
    $('#script').value = d.text;
  } catch (_) { /* keep whatever is there */ }
}

$('#runBtn').onclick = async () => {
  try {
    await post('/api/payload/run', { text: $('#script').value, countdown: 3000 });
    await refresh();
  } catch (e) { flash($('#payloadMeta'), e.message, true); }
};

$('#stopPayloadBtn').onclick = async () => {
  try { await post('/api/payload/stop'); await refresh(); }
  catch (e) { flash($('#payloadMeta'), e.message, true); }
};

$('#saveBtn').onclick = async () => {
  try {
    await post('/api/payload', { text: $('#script').value });
    flash($('#payloadMeta'), 'saved');
  } catch (e) { flash($('#payloadMeta'), e.message, true); }
};

$('#helloBtn').onclick = async () => {
  try {
    const d = await getJson('/api/payload');
    $('#script').value = d.presets.hello;
  } catch (e) { flash($('#payloadMeta'), e.message, true); }
};

$('#psBtn').onclick = () => {
  const cmd = prompt('PowerShell command to type', 'Start-Process calc');
  if (cmd === null) return;
  $('#script').value =
    'REM Custom PowerShell\n' +
    'DELAY 500\n' +
    'GUI r\n' +
    'DELAY 900\n' +
    'STRING powershell\n' +
    'ENTER\n' +
    'DELAY 1500\n' +
    `STRING ${cmd}\n` +
    'ENTER\n';
};

// ---------------------------------------------------------------- settings
async function loadIdentity() {
  try {
    const s = await getJson('/api/settings');
    if (document.activeElement !== $('#name')) $('#name').value = s.name;
    if (document.activeElement !== $('#appearance')) $('#appearance').value = s.appearance;
    $('#mode').value = s.mode;
    $('#window').value = String(s.window);
    if (document.activeElement !== $('#apSsid')) $('#apSsid').value = s.ssid;
    if (document.activeElement !== $('#apPass')) $('#apPass').value = s.password;  } catch (_) { /* offline */ }
}

$('#saveIdentity').onclick = async () => {
  try {
    await post('/api/emul/name', { name: $('#name').value });
    await post('/api/emul/appearance', { appearance: $('#appearance').value });
    await post('/api/emul/mode', { mode: $('#mode').value });
    await post('/api/emul/duration', { seconds: $('#window').value });
    await loadProfiles();
    await refresh();
    flash($('#payloadMeta'), 'identity applied');
  } catch (e) { flash($('#payloadMeta'), e.message, true); }
};

$('#saveAp').onclick = async () => {
  try {
    await post('/api/settings', {
      ssid: $('#apSsid').value,
      password: $('#apPass').value,
      window: $('#window').value,
    });
    document.body.innerHTML =
      '<p class="centered">Access point updated. Rebooting…<br>' +
      `Reconnect to <b>${esc($('#apSsid').value)}</b> and open <b>http://192.168.4.1</b>.</p>`;
  } catch (e) { flash($('#payloadMeta'), e.message, true); }
};

// ---------------------------------------------------------------- boot
loadProfiles();
loadScript();
loadIdentity();
refresh();
setInterval(refresh, 1000);
