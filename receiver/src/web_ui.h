#pragma once

static const char INDEX_HTML[] = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV Ambilight</title>
<style>
:root{--bg:#101114;--card:#1a1c21;--line:#2b2e36;--text:#e6e6e6;--dim:#8b8f99;--accent:#4da3ff;--ok:#3ecf8e;--warn:#f5a524}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.4 system-ui,sans-serif}
main{max-width:980px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 16px;display:flex;align-items:center;gap:10px}
h2{font-size:15px;margin:0 0 10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:14px}
.hint{color:var(--dim);font-size:13px;margin:8px 0 0}
code{background:#0d0e11;border:1px solid var(--line);border-radius:4px;padding:1px 5px}
canvas{width:100%;display:block;border-radius:6px;background:#000}
.pill{font-size:12px;font-weight:500;border-radius:99px;padding:2px 10px;border:1px solid var(--line);color:var(--dim)}
.pill.ok{color:var(--ok);border-color:var(--ok)}
.pill.warn{color:var(--warn);border-color:var(--warn)}
.stats{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.stat b{display:block;font-size:16px;font-weight:600;font-variant-numeric:tabular-nums}
.stat span{color:var(--dim);font-size:12px}
.bar{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
button{background:transparent;border:1px solid var(--line);color:var(--text);border-radius:6px;padding:6px 12px;font:inherit;cursor:pointer}
button:hover{border-color:var(--accent)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:8px 22px}
.row{display:grid;grid-template-columns:130px 1fr 52px;align-items:center;gap:8px}
.row input[type=range]{width:100%}
.row input[type=number],.row select{width:100%;background:#0d0e11;color:var(--text);border:1px solid var(--line);border-radius:5px;padding:4px}
.val{color:var(--dim);text-align:right;font-variant-numeric:tabular-nums}
</style></head><body><main>
<h1>TV Ambilight <span id="state" class="pill">...</span></h1>

<div class="card">
  <div class="stats">
    <div class="stat"><b id="s-client">-</b><span>Grabber</span></div>
    <div class="stat"><b id="s-fps">-</b><span>Frames / s</span></div>
    <div class="stat"><b id="s-size">-</b><span>Frame size</span></div>
    <div class="stat"><b id="s-wifi">-</b><span>Wi-Fi signal</span></div>
  </div>
  <p class="hint">In the grabber app on the TV, set host <code id="s-host">tv-ambilight.local</code> and port <code>19400</code>.</p>
</div>

<div class="card">
  <h2>Live view</h2>
  <canvas id="view"></canvas>
  <div class="bar">
    <button data-test="corners">Test: corners</button>
    <button data-test="rainbow">Test: rainbow</button>
    <button data-test="off">Stop test</button>
  </div>
  <p class="hint">The picture is what the TV sends; the shaded bands are what each side samples.
  "Corners" lights the first LED red, the last LED blue and the ends of each side white.</p>
</div>

<div class="card"><h2>Colour</h2><div class="grid" id="colour"></div></div>
<div class="card"><h2>Strip layout</h2><div class="grid" id="layout"></div></div>
</main>
<script>
const $ = id => document.getElementById(id);
let cfg = null, pending = {}, timer = 0;
let frame = null;  // {w, h, bitmap}
let ledPx = new Uint8Array(0);

const GROUPS = {
  colour: [
    ['enabled', 'LEDs on', 'check'],
    ['brightness', 'Brightness', 0, 255, 1],
    ['saturation', 'Saturation', 0, 3, 0.05],
    ['gamma', 'Gamma', 1, 3, 0.05],
    ['smoothing_ms', 'Smoothing ms', 0, 500, 5],
    ['gain_r', 'Red', 0, 1.5, 0.01],
    ['gain_g', 'Green', 0, 1.5, 0.01],
    ['gain_b', 'Blue', 0, 1.5, 0.01],
    ['max_milliamps', 'Max current mA', 500, 9500, 100],
    ['timeout_s', 'Off after (s)', 1, 60, 1],
  ],
  layout: [
    ['n_left', 'LEDs left', 'number'],
    ['n_top', 'LEDs top', 'number'],
    ['n_right', 'LEDs right', 'number'],
    ['start_corner', 'Strip starts', 'select', ['Bottom-left', 'Bottom-right']],
    ['depth', 'Sample depth', 0.02, 0.4, 0.01],
  ],
};

function send(patch) {
  Object.assign(cfg, patch);
  Object.assign(pending, patch);
  clearTimeout(timer);
  timer = setTimeout(() => {
    const body = JSON.stringify(pending);
    pending = {};
    fetch('/api/config', {method: 'POST', body});
  }, 120);
}

function buildControls() {
  for (const [group, items] of Object.entries(GROUPS)) {
    const box = $(group);
    box.innerHTML = '';
    for (const [key, label, a, b, step] of items) {
      const row = document.createElement('div');
      row.className = 'row';
      row.innerHTML = `<label>${label}</label>`;
      const val = document.createElement('span');
      val.className = 'val';
      let input;
      if (a === 'check') {
        input = document.createElement('input');
        input.type = 'checkbox';
        input.checked = cfg[key];
        input.onchange = () => send({[key]: input.checked});
      } else if (a === 'select') {
        input = document.createElement('select');
        b.forEach((t, i) => input.add(new Option(t, i)));
        input.value = cfg[key];
        input.onchange = () => { send({[key]: +input.value}); layout(); };
      } else if (a === 'number') {
        input = document.createElement('input');
        input.type = 'number'; input.min = 0; input.max = 300;
        input.value = cfg[key];
        input.onchange = () => { send({[key]: +input.value}); layout(); };
      } else {
        input = document.createElement('input');
        input.type = 'range'; input.min = a; input.max = b; input.step = step;
        input.value = cfg[key];
        val.textContent = cfg[key];
        input.oninput = () => { val.textContent = input.value; send({[key]: +input.value}); };
      }
      row.append(input, val);
      box.append(row);
    }
  }
}

// ---- live view: received frame as the "TV", LEDs around it
const view = $('view'), ctx = view.getContext('2d');
let slots = [], tv = [0, 0, 0, 0], ledSize = 4;

function layout() {
  const dpr = devicePixelRatio || 1;
  view.width = view.clientWidth * dpr;
  view.height = view.clientWidth * 0.55 * dpr;
  const W = view.width, H = view.height;
  const tvW = W * 0.78, tvH = Math.min(tvW * 9 / 16, H * 0.8);
  const x0 = (W - tvW) / 2, y0 = H - tvH - H * 0.03;
  tv = [x0, y0, tvW, tvH];
  const gap = W * 0.018, s = ledSize = Math.max(3, W * 0.011);
  const nl = cfg.n_left, nt = cfg.n_top, nr = cfg.n_right;
  const left = k => [x0 - gap - s, y0 + (k + 0.5) * tvH / nl - s / 2];
  const top = k => [x0 + (k + 0.5) * tvW / nt - s / 2, y0 - gap - s];
  const right = k => [x0 + tvW + gap, y0 + (k + 0.5) * tvH / nr - s / 2];
  slots = [];
  if (cfg.start_corner == 0) {
    for (let k = nl - 1; k >= 0; k--) slots.push(left(k));
    for (let k = 0; k < nt; k++) slots.push(top(k));
    for (let k = 0; k < nr; k++) slots.push(right(k));
  } else {
    for (let k = nr - 1; k >= 0; k--) slots.push(right(k));
    for (let k = nt - 1; k >= 0; k--) slots.push(top(k));
    for (let k = 0; k < nl; k++) slots.push(left(k));
  }
}

function draw() {
  const [x0, y0, w, h] = tv;
  ctx.fillStyle = '#000'; ctx.fillRect(0, 0, view.width, view.height);
  ctx.fillStyle = '#16181d'; ctx.fillRect(x0, y0, w, h);
  if (frame) {
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(frame.bitmap, x0, y0, w, h);
  }
  // sampling bands
  const dx = cfg.depth * w, dy = cfg.depth * h;
  ctx.fillStyle = 'rgba(77,163,255,0.22)';
  if (cfg.n_left) ctx.fillRect(x0, y0, dx, h);
  if (cfg.n_top) ctx.fillRect(x0, y0, w, dy);
  if (cfg.n_right) ctx.fillRect(x0 + w - dx, y0, dx, h);
  ctx.strokeStyle = '#333'; ctx.strokeRect(x0, y0, w, h);
  for (let i = 0; i < slots.length && i * 3 + 2 < ledPx.length; i++) {
    const c = `rgb(${ledPx[i * 3]},${ledPx[i * 3 + 1]},${ledPx[i * 3 + 2]})`;
    ctx.shadowColor = c; ctx.shadowBlur = ledSize * 2;
    ctx.fillStyle = c;
    ctx.fillRect(slots[i][0], slots[i][1], ledSize, ledSize);
  }
  ctx.shadowBlur = 0;
}

async function pollLeds() {
  try {
    ledPx = new Uint8Array(await (await fetch('/api/leds')).arrayBuffer());
    draw();
  } catch (e) {}
  setTimeout(pollLeds, 100);
}

async function pollFrame() {
  try {
    const r = await fetch('/api/frame');
    const w = +r.headers.get('X-Width'), h = +r.headers.get('X-Height');
    const px = new Uint8Array(await r.arrayBuffer());
    if (w && h && px.length >= w * h * 3) {
      const img = new ImageData(w, h);
      for (let i = 0, j = 0; i < w * h; i++, j += 3) {
        img.data[i * 4] = px[j]; img.data[i * 4 + 1] = px[j + 1];
        img.data[i * 4 + 2] = px[j + 2]; img.data[i * 4 + 3] = 255;
      }
      frame = {w, h, bitmap: await createImageBitmap(img)};
    }
  } catch (e) {}
  setTimeout(pollFrame, 250);
}

async function pollStatus() {
  try {
    const s = await (await fetch('/api/status')).json();
    const st = $('state');
    if (s.signal) { st.textContent = 'Receiving'; st.className = 'pill ok'; }
    else if (s.client) { st.textContent = 'Connected, no frames'; st.className = 'pill warn'; }
    else { st.textContent = 'Waiting for grabber'; st.className = 'pill'; }
    $('s-client').textContent = s.client || 'none';
    $('s-fps').textContent = s.signal ? s.fps : '-';
    $('s-size').textContent = s.width ? `${s.width} x ${s.height}` : '-';
    $('s-wifi').textContent = `${s.rssi} dBm`;
    $('s-host').textContent = `tv-ambilight.local (${s.ip})`;
    if (!s.signal) frame = null;
  } catch (e) {}
  setTimeout(pollStatus, 1000);
}

document.querySelectorAll('[data-test]').forEach(b => {
  b.onclick = () => fetch('/api/test', {method: 'POST', body: JSON.stringify({pattern: b.dataset.test})});
});
addEventListener('resize', () => { if (cfg) layout(); });

(async () => {
  cfg = await (await fetch('/api/config')).json();
  buildControls();
  layout();
  pollLeds();
  pollFrame();
  pollStatus();
})();
</script></body></html>)html";
