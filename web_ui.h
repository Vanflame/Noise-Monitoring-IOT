#pragma once

// Single-page UI served at '/'
// Stored outside the .ino to avoid Arduino sketch preprocessor issues with raw string literals.
static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Noise Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root { --bg:#0b1220; --card:#0f172a; --muted:#94a3b8; --text:#e5e7eb; --accent:#2563eb; --ok:#16a34a; --warn:#f59e0b; --bad:#dc2626; }
    * { box-sizing: border-box; }
    body { margin:0; font-family: 'Segoe UI', Arial, sans-serif; background: linear-gradient(180deg,#0b1220,#060a13); color: var(--text); padding: 18px; }
    .wrap { max-width: 520px; margin: 0 auto; }
    .card { background: rgba(15,23,42,0.9); border: 1px solid rgba(148,163,184,0.15); border-radius: 14px; padding: 16px; margin-bottom: 14px; box-shadow: 0 10px 30px rgba(0,0,0,0.35); }
    h1 { font-size: 20px; margin: 0 0 10px 0; }
    h2 { font-size: 16px; margin: 0 0 10px 0; color: #cbd5e1; }
    .row { display:flex; gap:10px; width: 100%; align-items:center; justify-content:space-between; flex-wrap:wrap; }
    .row-actions { justify-content: flex-start; }
    .row-actions .btn { flex: 1 1 140px; }
    .muted { color: var(--muted); font-size: 12px; }
    .pill { display:inline-flex; gap:8px; align-items:center; padding: 6px 10px; border-radius: 999px; background: rgba(148,163,184,0.12); border: 1px solid rgba(148,163,184,0.18); font-size: 12px; }
    .btn { background: var(--accent); color: white; border: none; padding: 10px 12px; border-radius: 10px; cursor: pointer; font-size: 14px; }
    .btn:disabled { opacity: 0.6; cursor: not-allowed; }
    .btn.gray { background: rgba(148,163,184,0.22); }
    input { width: 100%; padding: 10px; border-radius: 10px; border: 1px solid rgba(148,163,184,0.25); background: rgba(2,6,23,0.6); color: var(--text); }
    label { font-size: 12px; color: #cbd5e1; display:block; margin-bottom: 6px; }
    .grid { display:grid; grid-template-columns: 1fr; gap: 10px; }
    .list { display:flex; flex-direction:column; gap:8px; }
    .net { display:flex; align-items:center; justify-content:space-between; gap:10px; padding: 10px; border-radius: 12px; border: 1px solid rgba(148,163,184,0.18); background: rgba(2,6,23,0.35); cursor: pointer; }
    .net:hover { border-color: rgba(37,99,235,0.6); }
    .right { display:flex; gap:8px; align-items:center; }
    .badge { padding: 4px 8px; border-radius: 999px; font-size: 12px; }
    .ok { background: rgba(22,163,74,0.22); border: 1px solid rgba(22,163,74,0.35); }
    .bad { background: rgba(220,38,38,0.22); border: 1px solid rgba(220,38,38,0.35); }
    .warn { background: rgba(245,158,11,0.22); border: 1px solid rgba(245,158,11,0.35); }
    pre { background: rgba(2,6,23,0.55); border: 1px solid rgba(148,163,184,0.18); border-radius: 12px; padding: 10px; max-height: 180px; overflow:auto; font-size: 12px; white-space: pre-wrap; }
    .hide { display:none; }
    .topbar { display:flex; justify-content:space-between; align-items:center; gap:10px; }
    .player { display:flex; gap:10px; align-items:center; flex-wrap:wrap; padding: 10px; border-radius: 12px; border: 1px solid rgba(148,163,184,0.18); background: rgba(2,6,23,0.35); }
    .player .tracks { display:flex; gap:8px; flex-wrap:wrap; }
    .player .meta { flex: 1 1 160px; }
    .range { display:flex; align-items:center; gap:10px; width: 100%; justify-content:flex-start; }
    input[type=range] { width: 100%; }
    .range .pill { min-width: 78px; justify-content: center; }
    .range input[type=checkbox] { width:auto; }
    .logline { display:block; padding: 2px 0; }
    .log-time { color: #86efac; }
    .log-ok { color: #22c55e; }
    .log-warn { color: #fbbf24; }
    .log-bad { color: #f87171; }
    .log-muted { color: rgba(148,163,184,0.9); }
    .toast { position: fixed; left: 50%; transform: translateX(-50%); top: 12px; width: min(520px, calc(100% - 24px)); background: rgba(15,23,42,0.95); border: 1px solid rgba(37,99,235,0.55); border-radius: 14px; padding: 12px; box-shadow: 0 18px 40px rgba(0,0,0,0.5); display:none; z-index: 9999; }
    .toast .title { font-weight: 800; margin-bottom: 6px; }
    .toast .row { justify-content: flex-end; }
    .alertbar { margin-top: 12px; padding: 10px 12px; border-radius: 12px; border: 1px solid rgba(148,163,184,0.18); background: rgba(2,6,23,0.35); display:none; }
    .alertbar.bad { display:block; background: rgba(220,38,38,0.18); border-color: rgba(220,38,38,0.35); }
    .alertbar.warn { display:block; background: rgba(245,158,11,0.16); border-color: rgba(245,158,11,0.35); }
    .alertbar .title { font-weight: 800; margin-bottom: 4px; }
    .alertbar .muted { font-size: 12px; }
    .spinner { width: 14px; height: 14px; border: 2px solid rgba(255,255,255,0.35); border-top-color: rgba(255,255,255,0.95); border-radius: 50%; display:inline-block; vertical-align: middle; animation: spin 0.9s linear infinite; }
    @keyframes spin { to { transform: rotate(360deg); } }

    .switch { position: relative; display: inline-block; width: 46px; height: 26px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background: rgba(148,163,184,0.22); transition: .2s; border-radius: 999px; border: 1px solid rgba(148,163,184,0.25); }
    .slider:before { position: absolute; content: ""; height: 20px; width: 20px; left: 3px; bottom: 2px; background: white; transition: .2s; border-radius: 50%; }
    .switch input:checked + .slider { background: rgba(37,99,235,0.85); }
    .switch input:checked + .slider:before { transform: translateX(20px); }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="card">
      <div class="topbar">
        <div>
          <h1 style="margin:0 0 6px 0;">ESP32 Noise Monitor</h1>
          <div class="muted" id="subtitle">Loading...</div>
        </div>
        <button class="btn gray" id="topLogoutBtn">Logout</button>
      </div>
    </div>

    <div class="card" id="loginCard">
      <h2>Admin Login</h2>
      <div class="grid">
        <div>
          <label>Email</label>
          <input id="email" type="email" placeholder="admin@example.com" />
        </div>
        <div>
          <label>Password</label>
          <input id="password" type="password" placeholder="Your password" />
        </div>
        <div class="row row-actions">
          <button class="btn" id="loginBtn">Login</button>
          <button class="btn gray" id="logoutBtn">Logout</button>
        </div>
        <div class="muted" id="loginMsg"></div>
      </div>
    </div>

    <div class="card" id="dashCard">
      <div class="row">
        <h2>Network</h2>
        <div class="pill" id="netPill">...</div>
      </div>
      <div class="muted" id="netDetails">...</div>
      <div class="row" style="margin-top:10px;">
        <button class="btn" id="scanBtn">Scan Wi-Fi</button>
        <button class="btn gray" id="disconnectBtn">Disconnect</button>
      </div>
      <div class="list" id="netList" style="margin-top:10px;"></div>
      <div class="muted" id="netMsg" style="margin-top:8px;"></div>

      <div class="player hide" id="manualWifi" style="margin-top:10px;">
        <div class="meta">
          <div style="font-weight:700;">Manual Wi-Fi</div>
          <div class="muted">If scan finds no networks, enter SSID and password here.</div>
        </div>
        <div style="flex: 1 1 260px; width:100%;">
          <label>SSID</label>
          <input id="manualSsid" type="text" placeholder="Your Wi-Fi name" />
          <div style="height:8px"></div>
          <label>Password</label>
          <input id="manualPw" type="password" placeholder="Wi-Fi password" />
          <div style="height:10px"></div>
          <div class="row row-actions">
            <button class="btn" id="manualSaveBtn">Save Wi-Fi</button>
            <button class="btn gray" id="manualHideBtn">Hide</button>
          </div>
          <div class="muted" id="manualMsg" style="margin-top:8px;"></div>
        </div>
      </div>
    </div>

    <div class="card hide" id="controlsCard">
      <h2>Admin Controls</h2>
      <div class="alertbar" id="errBanner">
        <div class="title" id="errBannerTitle">System Alert</div>
        <div class="muted" id="errBannerBody"></div>
      </div>
      <div class="grid">
        <div>
          <label>Yellow Threshold</label>
          <input id="yellow" type="number" min="0" max="100" />
        </div>
        <div>
          <label>Red Threshold</label>
          <input id="red" type="number" min="0" max="100" />
        </div>
        <div class="row">
          <button class="btn" id="saveThreshBtn">Save Thresholds</button>
          <button class="btn gray" id="speakerBtn">Toggle Speaker</button>
        </div>
        <div class="pill" id="speakerPill">Speaker: ...</div>
        <div class="muted" id="speakerDisableReason" style="display:none;"></div>

        <div>
          <label>MP3 Player</label>
          <div class="player">
            <div class="meta">
              <div style="font-weight:700;" id="mp3Now">Ready</div>
              <div class="muted">Play warning tracks or stop</div>
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">Vol: <span id="volVal">30</span></div>
              <input id="vol" type="range" min="0" max="30" step="1" value="30" />
            </div>
            <div class="tracks">
              <button class="btn" id="mp3Play01">Play 01</button>
              <button class="btn" id="mp3Play02">Play 02</button>
              <button class="btn" id="mp3Play03">Play 03</button>
              <button class="btn gray" id="mp3Stop">Stop</button>
            </div>
          </div>
        </div>

        <div>
          <label>LED Brightness</label>
          <div class="player">
            <div class="meta">
              <div style="font-weight:700;">Noise LEDs</div>
              <div class="muted">Adjust brightness for Green/Yellow/Red indicators</div>
            </div>
            <div class="row" style="gap:12px;">
              <div class="pill">Noise LEDs</div>
              <label class="switch">
                <input id="nleden" type="checkbox" />
                <span class="slider"></span>
              </label>
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">G: <span id="ngVal">40</span></div>
              <input id="ng" type="range" min="0" max="255" step="1" value="40" />
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">Y: <span id="nyVal">40</span></div>
              <input id="ny" type="range" min="0" max="255" step="1" value="40" />
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">R: <span id="nrVal">80</span></div>
              <input id="nr" type="range" min="0" max="255" step="1" value="80" />
            </div>
          </div>

          <div class="player" style="margin-top:10px;">
            <div class="meta">
              <div style="font-weight:700;">Status RGB LED</div>
              <div class="muted">Scales the brightness used for Network/System status</div>
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">Status: <span id="stVal">40</span></div>
              <input id="st" type="range" min="0" max="255" step="1" value="40" />
            </div>
          </div>

          <details class="player" style="margin-top:10px;">
            <summary style="cursor:pointer; font-weight:700;">Advanced: Status LED Colors</summary>
            <div class="muted" style="margin-top:8px;">Pick exact RGB color for each network/system state.</div>
            <div class="grid" style="margin-top:10px; width:100%;">
              <div class="row">
                <div class="pill">Boot</div>
                <input id="sr_boot" type="color" value="#0000ff" style="width:52px; height:34px; padding:0;" />
                <input id="sr_boot_r" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_boot_g" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_boot_b" type="number" min="0" max="255" step="1" style="width:70px;" />
              </div>
              <div class="row">
                <div class="pill">AP Mode</div>
                <input id="sr_ap" type="color" value="#0000ff" style="width:52px; height:34px; padding:0;" />
                <input id="sr_ap_r" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_ap_g" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_ap_b" type="number" min="0" max="255" step="1" style="width:70px;" />
              </div>
              <div class="row">
                <div class="pill">Wi-Fi OK</div>
                <input id="sr_wifi" type="color" value="#00ff00" style="width:52px; height:34px; padding:0;" />
                <input id="sr_wifi_r" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_wifi_g" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_wifi_b" type="number" min="0" max="255" step="1" style="width:70px;" />
              </div>
              <div class="row">
                <div class="pill">No Internet</div>
                <input id="sr_noi" type="color" value="#ffff00" style="width:52px; height:34px; padding:0;" />
                <input id="sr_noi_r" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_noi_g" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_noi_b" type="number" min="0" max="255" step="1" style="width:70px;" />
              </div>
              <div class="row">
                <div class="pill">Offline</div>
                <input id="sr_off" type="color" value="#ff0000" style="width:52px; height:34px; padding:0;" />
                <input id="sr_off_r" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_off_g" type="number" min="0" max="255" step="1" style="width:70px;" />
                <input id="sr_off_b" type="number" min="0" max="255" step="1" style="width:70px;" />
              </div>
            </div>
          </details>
        </div>

        <div>
          <label>System</label>
          <div class="player">
            <div class="meta">
              <div style="font-weight:700;">Monitoring</div>
              <div class="muted">Disable MIC processing or Serial printing</div>
            </div>
            <div class="row" style="gap:12px; justify-content:flex-start;">
              <div class="pill">Microphone</div>
              <label class="switch">
                <input id="micen" type="checkbox" />
                <span class="slider"></span>
              </label>
            </div>
            <div class="muted" id="micDisableReason" style="display:none;"></div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">Serial Log</div>
              <label class="switch">
                <input id="serlog" type="checkbox" />
                <span class="slider"></span>
              </label>
            </div>
          </div>
        </div>

        <div>
          <label>dB Series Logging</label>
          <div class="player">
            <div class="meta">
              <div style="font-weight:700;">Time-series</div>
              <div class="muted">Logs to SD on change (dB change threshold) or heartbeat, then bulk uploads</div>
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">Sample ms</div>
              <input id="db_samp" type="number" min="50" max="5000" step="10" style="max-width:140px;" />
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">dB change</div>
              <input id="db_thr" type="number" min="0.1" max="20" step="0.1" style="max-width:140px;" />
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">Heartbeat s</div>
              <input id="db_hb" type="number" min="1" max="600" step="1" style="max-width:140px;" />
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <div class="pill">Upload (min)</div>
              <input id="db_up" type="number" min="1" max="1440" step="1" style="max-width:140px;" />
            </div>
            <div class="range" style="flex: 1 1 220px;">
              <button class="btn gray" id="dbSave">Save</button>
            </div>
            <div class="muted" id="dbDisableReason" style="display:none;"></div>
          </div>
        </div>

        <div>
          <label>Live Monitor</label>
          <pre id="monitor">Loading...</pre>
        </div>
        <div class="muted" id="ctrlMsg"></div>
      </div>
    </div>

    <div class="card">
      <h2>Logs</h2>
      <pre id="events">Loading...</pre>
    </div>
  </div>

  <div class="toast" id="toast">
    <div class="title" id="toastTitle">Connected</div>
    <div class="muted" id="toastBody"></div>
    <div class="row" style="margin-top:10px;">
      <button class="btn gray" id="toastOk">OK</button>
    </div>
  </div>

<script>
const SUPABASE_URL = '__SUPABASE_URL__';
const SUPABASE_ANON_KEY = '__SUPABASE_ANON_KEY__';

let accessToken = localStorage.getItem('sb_access_token') || '';
let userId = localStorage.getItem('sb_user_id') || '';
let isAdmin = false;
let lastApGrace = false;
let toastRedirectUrl = '';

let scanRequested = false;
let manualWifiDismissed = false;

let lastConnected = false;
let lastConnectedIp = '';

function setBtnLoading(btn, loading, label) {
  if (!btn) return;
  if (!btn.dataset.label) btn.dataset.label = btn.textContent;
  if (label) btn.dataset.label = label;
  if (loading) {
    btn.disabled = true;
    btn.innerHTML = '<span class="spinner"></span> ' + btn.dataset.label;
  } else {
    btn.disabled = false;
    btn.textContent = btn.dataset.label;
  }
}

function wifiBarsSvg(rssi) {
  let bars = 0;
  if (typeof rssi !== 'number') bars = 0;
  else if (rssi >= -55) bars = 4;
  else if (rssi >= -65) bars = 3;
  else if (rssi >= -75) bars = 2;
  else if (rssi >= -85) bars = 1;
  else bars = 0;

  const cOn = '#22c55e';
  const cOff = 'rgba(148,163,184,0.35)';
  const b = [bars>=1, bars>=2, bars>=3, bars>=4];
  return `\n  <svg width="22" height="18" viewBox="0 0 22 18" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">\n    <rect x="1" y="11" width="3" height="6" rx="1" fill="${b[0]?cOn:cOff}"/>\n    <rect x="6" y="8" width="3" height="9" rx="1" fill="${b[1]?cOn:cOff}"/>\n    <rect x="11" y="5" width="3" height="12" rx="1" fill="${b[2]?cOn:cOff}"/>\n    <rect x="16" y="2" width="3" height="15" rx="1" fill="${b[3]?cOn:cOff}"/>\n  </svg>`;
}

async function apiGet(path) {
  const res = await fetch(path, { cache: 'no-store' });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  return await res.json();
}

async function apiText(path) {
  const r = await fetch(path, { cache: 'no-store' });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return await r.text();
}

const editing = new Set();
const lastEditedMs = {};
function watchEdit(id) {
  const el = document.getElementById(id);
  if (!el) return;
  const mark = () => {
    editing.add(id);
    lastEditedMs[id] = Date.now();
  };
  el.addEventListener('focus', mark);
  el.addEventListener('input', mark);
  el.addEventListener('change', mark);
  el.addEventListener('blur', () => {
    lastEditedMs[id] = Date.now();
    setTimeout(() => {
      if ((Date.now() - (lastEditedMs[id] || 0)) >= 1200) {
        editing.delete(id);
      }
    }, 1300);
  });
}

function safeSetValue(id, val) {
  if (editing.has(id)) return;
  const t = lastEditedMs[id] || 0;
  if (t && (Date.now() - t) < 4000) return;
  const el = document.getElementById(id);
  if (!el) return;
  if (String(el.value) === String(val)) return;
  el.value = val;
}

function renderLog(preId, text) {
  const pre = document.getElementById(preId);
  const lines = String(text || '').split('\n');
  let html = '';
  for (const raw of lines) {
    const line = raw.trimEnd();
    if (!line) continue;
    const lower = line.toLowerCase();
    let cls = 'log-muted';
    if (lower.includes('failed') || lower.includes('error') || lower.includes('not authorized') || lower.includes('disconnect')) cls = 'log-bad';
    else if (lower.includes('warning') || lower.includes('no internet') || lower.includes('connecting')) cls = 'log-warn';
    else if (lower.includes('connected') || lower.includes('saved') || lower.includes('updated') || lower.includes('time synchronized')) cls = 'log-ok';
    if (line.match(/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}/)) cls = 'log-time';
    html += `<span class="logline ${cls}">${line.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;')}</span>`;
  }
  pre.innerHTML = html || '<span class="logline log-muted">(empty)</span>';
}

function showToast(title, body, redirectUrl) {
  const t = document.getElementById('toast');
  document.getElementById('toastTitle').textContent = title;
  document.getElementById('toastBody').textContent = body;
  toastRedirectUrl = redirectUrl || '';
  t.style.display = 'block';
  clearTimeout(window.__toastTimer);
  window.__toastTimer = setTimeout(() => { t.style.display = 'none'; }, 12000);
}

async function refreshMonitor() {
  try {
    renderLog('monitor', await apiText('/monitor'));
  } catch (e) {
    renderLog('monitor', 'Fetch failed');
  }
}

async function refreshEvents() {
  try {
    renderLog('events', await apiText('/events'));
  } catch (e) {}
}

async function getStatus() {
  try {
    return await apiGet('/status');
  } catch (e) {
    return { connected:false, internet:false, ssid:'', rssi: null, ip:'', gw:'', apip:'', yellow:0, red:0, ngbrt:40, nybrt:40, nrbrt:80, stbrt:40, nleden:true, micen:true, serlog:true, sr_boot:255, sr_ap:255, sr_wifi:65280, sr_noi:16776960, sr_off:16711680, db_samp:100, db_thr10:10, db_hb:8000, db_up:3600000, mp3vol:30, speaker:false };
  }
}

const COLOR_PRESETS = [
  { v: 0, t: 'Off' },
  { v: 1, t: 'Red' },
  { v: 2, t: 'Green' },
  { v: 3, t: 'Blue' },
  { v: 4, t: 'Yellow' },
  { v: 5, t: 'Cyan' },
  { v: 6, t: 'Magenta' },
  { v: 7, t: 'White' },
];

function fillPresetSelect(id) {
  const sel = document.getElementById(id);
  if (!sel) return;
  sel.innerHTML = '';
  for (const p of COLOR_PRESETS) {
    const o = document.createElement('option');
    o.value = String(p.v);
    o.textContent = p.t;
    sel.appendChild(o);
  }
}

function setSubtitle(s) {
  document.getElementById('subtitle').textContent = s;
}

function show(el, on) {
  if (on) el.classList.remove('hide');
  else el.classList.add('hide');
}

async function supabaseLogin(email, password) {
  const url = SUPABASE_URL + '/auth/v1/token?grant_type=password';
  const res = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'apikey': SUPABASE_ANON_KEY
    },
    body: JSON.stringify({ email, password })
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error_description || data.msg || 'login_failed');
  accessToken = data.access_token;
  userId = data.user && data.user.id ? data.user.id : '';
  localStorage.setItem('sb_access_token', accessToken);
  localStorage.setItem('sb_user_id', userId);
  return { accessToken, userId };
}

async function supabaseLogout() {
  accessToken = '';
  userId = '';
  isAdmin = false;
  localStorage.removeItem('sb_access_token');
  localStorage.removeItem('sb_user_id');
}

async function fetchRole() {
  if (!accessToken || !userId) return '';
  const url = SUPABASE_URL + '/rest/v1/profiles?select=role&id=eq.' + encodeURIComponent(userId) + '&limit=1';
  const res = await fetch(url, {
    headers: {
      'apikey': SUPABASE_ANON_KEY,
      'Authorization': 'Bearer ' + accessToken
    }
  });
  const data = await res.json();
  if (!res.ok) return '';
  if (!Array.isArray(data) || data.length === 0) return '';
  return data[0].role || '';
}

async function refreshUI() {
  const st = await getStatus();

  document.getElementById('topLogoutBtn').style.display = (accessToken && userId) ? '' : 'none';
  document.getElementById('logoutBtn').style.display = (accessToken && userId) ? '' : 'none';

  const netPill = document.getElementById('netPill');
  const netDetails = document.getElementById('netDetails');
  const controlsCard = document.getElementById('controlsCard');
  const loginCard = document.getElementById('loginCard');

  const netState = st.connected ? (st.internet ? 'Connected + Internet' : 'Connected (No Internet)') : 'Not connected';
  const badge = st.connected ? (st.internet ? 'ok' : 'warn') : 'bad';
  const ssid = st.ssid || '-';
  const icon = wifiBarsSvg(typeof st.rssi === 'number' ? st.rssi : null);
  netPill.innerHTML = icon + `<span>${ssid}</span> <span class="badge ${badge}">${netState}</span>`;
  netDetails.textContent = `STA IP: ${st.ip || '-'} | GW: ${st.gw || '-'} | AP IP: ${st.apip || '-'}`;
  setSubtitle(st.connected ? (st.internet ? 'Device online' : 'Device connected (no internet)') : 'Device offline');

  document.getElementById('disconnectBtn').disabled = !st.connected;

  const needWifiFix = (!st.connected) || (!st.internet);
  show(document.getElementById('netList'), needWifiFix);
  show(document.getElementById('netMsg'), needWifiFix);
  document.getElementById('scanBtn').disabled = !needWifiFix;

  const manualWifi = document.getElementById('manualWifi');
  if (st.connected) {
    scanRequested = false;
    manualWifiDismissed = false;
    if (manualWifi) {
      manualWifi.classList.add('hide');
      manualWifi.style.display = 'none';
    }
  } else if (manualWifiDismissed) {
    if (manualWifi) {
      manualWifi.classList.add('hide');
      manualWifi.style.display = 'none';
    }
  }

  const netMsg = document.getElementById('netMsg');
  if (st.apGrace && st.ip) {
    netMsg.textContent = `Connected to Wi-Fi. Setup AP will turn off soon. Connect your phone to the same Wi-Fi and open: http://${st.ip}/`;
    if (!lastApGrace) {
      showToast('Wi-Fi Connected', `Now join "${st.ssid || 'your Wi-Fi'}" then open the device via its router IP (it will show here).`, `http://${st.ip}/`);
    }
  } else if (!needWifiFix && st.connected) {
    netMsg.textContent = 'Connected to Wi-Fi: ' + ssid;
  }
  lastApGrace = !!st.apGrace;

  if (st.connected && st.ip) {
    if (!lastConnected || (lastConnectedIp !== st.ip)) {
      showToast('Wi-Fi Connected', `Open the device at: http://${st.ip}/`, `http://${st.ip}/`);
    }
  }
  lastConnected = !!st.connected;
  lastConnectedIp = st.ip || '';

  if (!accessToken || !userId) {
    isAdmin = false;
    show(loginCard, true);
    show(controlsCard, false);
    if (!st.internet) {
      document.getElementById('loginMsg').textContent = 'Login requires internet. Fix Wi-Fi first.';
    }
    return;
  }

  const role = await fetchRole();
  isAdmin = (role === 'admin');
  if (!isAdmin) {
    show(loginCard, true);
    show(controlsCard, false);
    document.getElementById('loginMsg').textContent = 'Not authorized: role=' + (role || 'unknown');
    return;
  }

  show(loginCard, false);
  show(controlsCard, true);

  const errBanner = document.getElementById('errBanner');
  const errBannerTitle = document.getElementById('errBannerTitle');
  const errBannerBody = document.getElementById('errBannerBody');
  const errs = [];
  if (st.sderr) errs.push('SD card not detected');
  if (st.micerr) errs.push('MIC not detected / stuck at 0');
  if (st.supaerr) errs.push('Supabase error (recent post/upload failed)');
  if (st.mp3err) errs.push('MP3 player not detected');
  if (errBanner && errBannerTitle && errBannerBody) {
    errBanner.classList.remove('bad');
    errBanner.classList.remove('warn');
    if (errs.length) {
      errBannerTitle.textContent = 'System Alert';
      errBannerBody.textContent = errs.join(' | ');
      errBanner.classList.add(st.sderr ? 'bad' : 'warn');
    } else {
      errBanner.style.display = 'none';
    }
    if (errs.length) {
      errBanner.style.display = 'block';
    }
  }

  safeSetValue('yellow', (typeof st.yellow === 'number') ? st.yellow : '');
  safeSetValue('red', (typeof st.red === 'number') ? st.red : '');
  document.getElementById('speakerPill').textContent = 'Speaker: ' + (st.speaker ? 'ON' : 'OFF');

  const speakerBtn = document.getElementById('speakerBtn');
  const speakerReason = document.getElementById('speakerDisableReason');
  if (speakerBtn) {
    const disabled = !!st.mp3err;
    speakerBtn.disabled = disabled;
    if (speakerReason) {
      speakerReason.textContent = disabled ? 'MP3 player not detected' : '';
      speakerReason.style.display = disabled ? 'block' : 'none';
    }
  }

  const playDisabled = (!st.speaker) || (!!st.mp3err);
  document.getElementById('mp3Play01').disabled = playDisabled;
  document.getElementById('mp3Play02').disabled = playDisabled;
  document.getElementById('mp3Play03').disabled = playDisabled;
  if (typeof st.mp3vol === 'number') {
    safeSetValue('vol', st.mp3vol);
    document.getElementById('volVal').textContent = String(st.mp3vol);
  }

  if (typeof st.ngbrt === 'number') {
    safeSetValue('ng', st.ngbrt);
    document.getElementById('ngVal').textContent = String(st.ngbrt);
  }
  if (typeof st.nybrt === 'number') {
    safeSetValue('ny', st.nybrt);
    document.getElementById('nyVal').textContent = String(st.nybrt);
  }
  if (typeof st.nrbrt === 'number') {
    safeSetValue('nr', st.nrbrt);
    document.getElementById('nrVal').textContent = String(st.nrbrt);
  }
  if (typeof st.stbrt === 'number') {
    safeSetValue('st', st.stbrt);
    document.getElementById('stVal').textContent = String(st.stbrt);
  }

  if (typeof st.nleden === 'boolean') {
    const cb = document.getElementById('nleden');
    cb.checked = st.nleden;
  }

  if (typeof st.micen === 'boolean') {
    const micCb = document.getElementById('micen');
    micCb.checked = st.micen;
    micCb.disabled = !!st.micerr;
    const micReason = document.getElementById('micDisableReason');
    if (micReason) {
      micReason.textContent = st.micerr ? 'MIC not detected' : '';
      micReason.style.display = st.micerr ? 'block' : 'none';
    }
  }
  if (typeof st.serlog === 'boolean') {
    document.getElementById('serlog').checked = st.serlog;
  }

  if (typeof st.db_samp === 'number') safeSetValue('db_samp', String(st.db_samp));
  if (typeof st.db_thr10 === 'number') safeSetValue('db_thr', String((st.db_thr10 / 10.0).toFixed(1)));
  if (typeof st.db_hb === 'number') safeSetValue('db_hb', String(Math.round(st.db_hb / 1000)));
  if (typeof st.db_up === 'number') safeSetValue('db_up', String(Math.round(st.db_up / 60000)));

  const sdOff = !!st.sderr;
  const dbSampEl = document.getElementById('db_samp');
  const dbThrEl = document.getElementById('db_thr');
  const dbHbEl = document.getElementById('db_hb');
  const dbUpEl = document.getElementById('db_up');
  const dbSaveEl = document.getElementById('dbSave');
  if (dbSampEl) dbSampEl.disabled = sdOff;
  if (dbThrEl) dbThrEl.disabled = sdOff;
  if (dbHbEl) dbHbEl.disabled = sdOff;
  if (dbUpEl) dbUpEl.disabled = sdOff;
  if (dbSaveEl) dbSaveEl.disabled = sdOff;
  const dbReason = document.getElementById('dbDisableReason');
  if (dbReason) {
    dbReason.textContent = sdOff ? 'SD card not detected' : '';
    dbReason.style.display = sdOff ? 'block' : 'none';
  }

  function intToHex(v) {
    let s = (v >>> 0).toString(16);
    while (s.length < 6) s = '0' + s;
    return '#' + s.slice(-6);
  }

  function setRgbInputs(prefix, rgbInt) {
    const r = (rgbInt >> 16) & 0xFF;
    const g = (rgbInt >> 8) & 0xFF;
    const b = (rgbInt) & 0xFF;
    safeSetValue(prefix, intToHex(rgbInt));
    safeSetValue(prefix + '_r', String(r));
    safeSetValue(prefix + '_g', String(g));
    safeSetValue(prefix + '_b', String(b));
  }

  if (typeof st.sr_boot === 'number') setRgbInputs('sr_boot', st.sr_boot);
  if (typeof st.sr_ap === 'number') setRgbInputs('sr_ap', st.sr_ap);
  if (typeof st.sr_wifi === 'number') setRgbInputs('sr_wifi', st.sr_wifi);
  if (typeof st.sr_noi === 'number') setRgbInputs('sr_noi', st.sr_noi);
  if (typeof st.sr_off === 'number') setRgbInputs('sr_off', st.sr_off);

  await refreshMonitor();
}

async function scanAndRender() {
  const list = document.getElementById('netList');
  const msg = document.getElementById('netMsg');
  const manual = document.getElementById('manualWifi');
  list.innerHTML = '';
  msg.textContent = '';
  try {
    let nets = await apiGet('/scan');
    let tries = 0;
    while ((!nets || nets.length === 0) && tries < 10) {
      msg.textContent = 'Scanning...';
      await new Promise(r => setTimeout(r, 700));
      nets = await apiGet('/scan');
      tries++;
    }
    for (const n of nets) {
      const el = document.createElement('div');
      el.className = 'net';
      const icon = wifiBarsSvg(n.rssi);
      el.innerHTML = `<div>\n        <div style="font-weight:700;">${n.ssid}</div>\n        <div class="muted">${n.secure ? 'Secured' : 'Open'} | ${n.rssi} dBm</div>\n      </div>\n      <div class="right">${icon}</div>`;

      el.addEventListener('click', async () => {
        const pw = n.secure ? prompt('Password for ' + n.ssid + ':') : '';
        if (n.secure && (!pw || pw.length < 1)) return;
        msg.textContent = 'Saving Wi-Fi...';
        const form = new URLSearchParams();
        form.append('ssid', n.ssid);
        form.append('password', pw || '');
        await fetch('/save', { method:'POST', body: form });
        msg.textContent = 'Saved. Connecting...';
        showToast('Wi-Fi Saved', `If connection succeeds, join "${n.ssid}" then open the device via its router IP (it will show here).`, '');
        setTimeout(refreshUI, 1200);
      });

      list.appendChild(el);
    }
    if (!nets || nets.length === 0) {
      msg.textContent = 'No networks found.';
      if (manual && scanRequested && !manualWifiDismissed) {
        manual.classList.remove('hide');
        manual.style.display = 'flex';
      }
    } else {
      if (manual) {
        manual.classList.add('hide');
        manual.style.display = 'none';
      }
    }
  } catch (e) {
    msg.textContent = 'Scan failed.';
    if (manual && scanRequested && !manualWifiDismissed) {
      manual.classList.remove('hide');
      manual.style.display = 'flex';
    }
  }
}

async function manualSaveWifi() {
  const ssid = (document.getElementById('manualSsid').value || '').trim();
  const pw = (document.getElementById('manualPw').value || '');
  const msg = document.getElementById('manualMsg');
  const btn = document.getElementById('manualSaveBtn');
  if (!ssid) {
    msg.textContent = 'SSID is required.';
    return;
  }
  setBtnLoading(btn, true, 'Save Wi-Fi');
  msg.textContent = 'Saving Wi-Fi...';
  try {
    const form = new URLSearchParams();
    form.append('ssid', ssid);
    form.append('password', pw || '');
    await fetch('/save', { method:'POST', body: form });
    msg.textContent = 'Saved. Connecting...';
    showToast('Wi-Fi Saved', 'Trying to connect. If it succeeds, join the same Wi-Fi and open the device via its router IP (it will show above).', '');
    setTimeout(refreshUI, 1200);
  } catch (e) {
    msg.textContent = 'Save failed.';
  }
  setBtnLoading(btn, false, 'Save Wi-Fi');
}

document.getElementById('loginBtn').addEventListener('click', async () => {
  const btn = document.getElementById('loginBtn');
  const msg = document.getElementById('loginMsg');
  setBtnLoading(btn, true, 'Login');
  msg.textContent = 'Logging in...';
  try {
    await supabaseLogin(document.getElementById('email').value.trim(), document.getElementById('password').value);
    msg.textContent = 'Logged in.';
    await refreshUI();
  } catch (e) {
    msg.textContent = String(e.message || e);
  } finally {
    setBtnLoading(btn, false, 'Login');
  }
  document.getElementById('logoutBtn').style.display = (accessToken && userId) ? '' : 'none';
});

document.getElementById('logoutBtn').addEventListener('click', async () => {
  await supabaseLogout();
  document.getElementById('loginMsg').textContent = 'Logged out.';
  await refreshUI();
  document.getElementById('logoutBtn').style.display = (accessToken && userId) ? '' : 'none';
});

document.getElementById('topLogoutBtn').addEventListener('click', async () => {
  await supabaseLogout();
  document.getElementById('loginMsg').textContent = 'Logged out.';
  await refreshUI();
});

document.getElementById('scanBtn').addEventListener('click', async () => {
  const btn = document.getElementById('scanBtn');
  setBtnLoading(btn, true, 'Scan Wi-Fi');
  scanRequested = true;
  manualWifiDismissed = false;
  try { await scanAndRender(); } finally { setBtnLoading(btn, false, 'Scan Wi-Fi'); }
});

document.getElementById('manualSaveBtn').addEventListener('click', async () => {
  await manualSaveWifi();
});

document.getElementById('manualHideBtn').addEventListener('click', async (e) => {
  if (e && e.preventDefault) e.preventDefault();
  const manual = document.getElementById('manualWifi');
  manualWifiDismissed = true;
  if (manual) {
    manual.classList.add('hide');
    manual.style.display = 'none';
  }
});

document.getElementById('disconnectBtn').addEventListener('click', async () => {
  const btn = document.getElementById('disconnectBtn');
  setBtnLoading(btn, true, 'Disconnect');
  try { await fetch('/disconnect'); } catch (e) {}
  setTimeout(async () => { setBtnLoading(btn, false, 'Disconnect'); await refreshUI(); }, 900);
});

document.getElementById('saveThreshBtn').addEventListener('click', async () => {
  const y = document.getElementById('yellow').value;
  const r = document.getElementById('red').value;
  const msg = document.getElementById('ctrlMsg');
  const btn = document.getElementById('saveThreshBtn');
  setBtnLoading(btn, true, 'Save Thresholds');
  msg.textContent = 'Saving...';
  try {
    await fetch('/setThresholds?yellow=' + encodeURIComponent(y) + '&red=' + encodeURIComponent(r));
    msg.textContent = 'Saved.';
  } catch (e) {
    msg.textContent = 'Save failed.';
  }
  setBtnLoading(btn, false, 'Save Thresholds');
});

document.getElementById('speakerBtn').addEventListener('click', async () => {
  const msg = document.getElementById('ctrlMsg');
  const btn = document.getElementById('speakerBtn');
  setBtnLoading(btn, true, 'Toggle Speaker');
  msg.textContent = 'Updating...';
  try {
    const st = await getStatus();
    const next = st.speaker ? '0' : '1';
    await fetch('/setSpeaker?enabled=' + next);
    msg.textContent = 'Updated.';
  } catch (e) {
    msg.textContent = 'Failed.';
  }
  setBtnLoading(btn, false, 'Toggle Speaker');
  setTimeout(refreshUI, 600);
});

async function mp3Action(btn, label, url, nowText) {
  setBtnLoading(btn, true, label);
  document.getElementById('mp3Now').textContent = nowText;
  try {
    await fetch(url);
  } catch (e) {
    document.getElementById('mp3Now').textContent = 'Failed';
  }
  setBtnLoading(btn, false, label);
}

let volTimer = 0;
async function setVolumeDebounced(v) {
  clearTimeout(volTimer);
  volTimer = setTimeout(async () => {
    try { await fetch('/setMp3Volume?vol=' + encodeURIComponent(v)); } catch (e) {}
  }, 120);
}

let ledTimer = 0;
async function setLedBrightnessDebounced(params) {
  clearTimeout(ledTimer);
  ledTimer = setTimeout(async () => {
    const qs = new URLSearchParams(params);
    try { await fetch('/setLedBrightness?' + qs.toString()); } catch (e) {}
  }, 120);
}

let scTimer = 0;
async function setStatusColorsDebounced(params) {
  clearTimeout(scTimer);
  scTimer = setTimeout(async () => {
    const qs = new URLSearchParams(params);
    try { await fetch('/setStatusColors?' + qs.toString()); } catch (e) {}
  }, 160);
}

let srTimer = 0;
async function setStatusRgbDebounced(params) {
  clearTimeout(srTimer);
  srTimer = setTimeout(async () => {
    const qs = new URLSearchParams(params);
    try { await fetch('/setStatusRgb?' + qs.toString()); } catch (e) {}
  }, 160);
}

document.getElementById('mp3Play01').addEventListener('click', async () => {
  await mp3Action(document.getElementById('mp3Play01'), 'Play 01', '/playTest001', 'Playing 01...');
});
document.getElementById('mp3Play02').addEventListener('click', async () => {
  await mp3Action(document.getElementById('mp3Play02'), 'Play 02', '/playTest002', 'Playing 02...');
});
document.getElementById('mp3Play03').addEventListener('click', async () => {
  await mp3Action(document.getElementById('mp3Play03'), 'Play 03', '/playTest003', 'Playing 03...');
});
document.getElementById('mp3Stop').addEventListener('click', async () => {
  await mp3Action(document.getElementById('mp3Stop'), 'Stop', '/stopMp3', 'Stopped');
});

document.getElementById('vol').addEventListener('input', async (e) => {
  const v = parseInt(e.target.value || '0', 10);
  document.getElementById('volVal').textContent = String(v);
  await setVolumeDebounced(v);
});

function hookLedSlider(id, valId, paramName) {
  const el = document.getElementById(id);
  if (!el) return;
  el.addEventListener('input', async (e) => {
    const v = parseInt(e.target.value || '0', 10);
    const vEl = document.getElementById(valId);
    if (vEl) vEl.textContent = String(v);
    const p = {};
    p[paramName] = String(v);
    await setLedBrightnessDebounced(p);
  });
}

hookLedSlider('ng', 'ngVal', 'ng');
hookLedSlider('ny', 'nyVal', 'ny');
hookLedSlider('nr', 'nrVal', 'nr');
hookLedSlider('st', 'stVal', 'st');

document.getElementById('nleden').addEventListener('change', async (e) => {
  const on = e.target.checked ? '1' : '0';
  try { await fetch('/setNoiseLedsEnabled?enabled=' + on); } catch (err) {}
});

document.getElementById('micen').addEventListener('change', async (e) => {
  const on = e.target.checked ? '1' : '0';
  try { await fetch('/setMicEnabled?enabled=' + on); } catch (err) {}
});

document.getElementById('serlog').addEventListener('change', async (e) => {
  const on = e.target.checked ? '1' : '0';
  try { await fetch('/setSerialLogging?enabled=' + on); } catch (err) {}
});

document.getElementById('dbSave').addEventListener('click', async () => {
  const samp = parseInt(document.getElementById('db_samp').value || '100', 10);
  const thr = parseFloat(document.getElementById('db_thr').value || '1.0');
  const hb = parseInt(document.getElementById('db_hb').value || '8', 10);
  const up = parseInt(document.getElementById('db_up').value || '60', 10);
  const thr10 = Math.max(1, Math.round(thr * 10.0));
  const hbMs = hb * 1000;
  const upMs = up * 60 * 1000;
  try {
    await fetch('/setDbLogConfig?samp=' + samp + '&thr10=' + thr10 + '&hb=' + hbMs + '&up=' + upMs);
  } catch (e) {}
});

function hookStatusRgbColor(id, paramName) {
  const el = document.getElementById(id);
  if (!el) return;
  el.addEventListener('input', async (e) => {
    const p = {};
    p[paramName] = String(e.target.value || '#000000');
    await setStatusRgbDebounced(p);
  });
}

function hookStatusRgbNumbers(prefix, paramName) {
  function clamp255(v) {
    v = parseInt(v || '0', 10);
    if (isNaN(v)) v = 0;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return v;
  }
  async function sendFromNumbers() {
    const r = clamp255(document.getElementById(prefix + '_r').value);
    const g = clamp255(document.getElementById(prefix + '_g').value);
    const b = clamp255(document.getElementById(prefix + '_b').value);
    const hex = '#' + [r,g,b].map(x => x.toString(16).padStart(2,'0')).join('');
    safeSetValue(prefix, hex);
    const p = {};
    p[paramName] = hex;
    await setStatusRgbDebounced(p);
  }
  document.getElementById(prefix + '_r').addEventListener('change', sendFromNumbers);
  document.getElementById(prefix + '_g').addEventListener('change', sendFromNumbers);
  document.getElementById(prefix + '_b').addEventListener('change', sendFromNumbers);
}

hookStatusRgbColor('sr_boot', 'boot');
hookStatusRgbColor('sr_ap', 'ap');
hookStatusRgbColor('sr_wifi', 'wifi');
hookStatusRgbColor('sr_noi', 'noi');
hookStatusRgbColor('sr_off', 'off');

hookStatusRgbNumbers('sr_boot', 'boot');
hookStatusRgbNumbers('sr_ap', 'ap');
hookStatusRgbNumbers('sr_wifi', 'wifi');
hookStatusRgbNumbers('sr_noi', 'noi');
hookStatusRgbNumbers('sr_off', 'off');

document.getElementById('toastOk').addEventListener('click', () => {
  document.getElementById('toast').style.display = 'none';
  if (toastRedirectUrl) {
    window.location.assign(toastRedirectUrl);
  } else {
    window.location.reload();
  }
});

async function boot() {
  const manualWifi = document.getElementById('manualWifi');
  scanRequested = false;
  manualWifiDismissed = false;
  if (manualWifi) {
    manualWifi.classList.add('hide');
    manualWifi.style.display = 'none';
  }

  // Track fields user edits so refreshUI won't overwrite them mid-typing
  watchEdit('yellow');
  watchEdit('red');
  watchEdit('ng');
  watchEdit('ny');
  watchEdit('nr');
  watchEdit('st');
  watchEdit('db_samp');
  watchEdit('db_thr');
  watchEdit('db_hb');
  watchEdit('db_up');
  watchEdit('sr_boot');
  watchEdit('sr_boot_r');
  watchEdit('sr_boot_g');
  watchEdit('sr_boot_b');
  watchEdit('sr_ap');
  watchEdit('sr_ap_r');
  watchEdit('sr_ap_g');
  watchEdit('sr_ap_b');
  watchEdit('sr_wifi');
  watchEdit('sr_wifi_r');
  watchEdit('sr_wifi_g');
  watchEdit('sr_wifi_b');
  watchEdit('sr_noi');
  watchEdit('sr_noi_r');
  watchEdit('sr_noi_g');
  watchEdit('sr_noi_b');
  watchEdit('sr_off');
  watchEdit('sr_off_r');
  watchEdit('sr_off_g');
  watchEdit('sr_off_b');

  await refreshUI();
  await refreshEvents();
  setInterval(refreshEvents, 800);
  setInterval(async () => { await refreshUI(); }, 2500);
  setInterval(async () => {
    if (!isAdmin) return;
    const st = await getStatus();
    if (!st.micen) return;
    await refreshMonitor();
  }, 800);
}
boot();
</script>
</body>
</html>)HTMLPAGE";
