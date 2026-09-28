#pragma once

static const char INDEX_HTML[] = R"html(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TV Ambilight</title>
<style>
:root{--bg:#101114;--card:#1a1c21;--line:#2b2e36;--text:#e6e6e6;--dim:#8b8f99;--accent:#4da3ff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.4 system-ui,sans-serif}
main{max-width:980px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:4px 0 16px}
h2{font-size:15px;margin:0 0 10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:14px}
.hint{color:var(--dim);font-size:13px;margin:6px 0 0}
canvas{width:100%;display:block;border-radius:6px;background:#000;touch-action:none}
.bar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:10px}
button{background:var(--accent);color:#fff;border:0;border-radius:6px;padding:7px 14px;font:inherit;cursor:pointer}
button.ghost{background:transparent;border:1px solid var(--line);color:var(--text)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:8px 22px}
.row{display:grid;grid-template-columns:130px 1fr 48px;align-items:center;gap:8px}
.row input[type=range]{width:100%}
.row input[type=number],.row select{width:100%;background:#0d0e11;color:var(--text);border:1px solid var(--line);border-radius:5px;padding:4px}
.val{color:var(--dim);text-align:right;font-variant-numeric:tabular-nums}
#fps{color:var(--dim);margin-left:auto}
</style></head><body><main>
<h1>TV Ambilight</h1>

<div class="card">
  <h2>1. Calibrate</h2>
  <canvas id="cam"></canvas>
  <div class="bar">
    <button id="refresh" class="ghost">Refresh image</button>
    <label><input type="checkbox" id="live"> Live</label>
  </div>
  <p class="hint">Drag the 4 handles onto the corners of the picture area (inside the bezel).
  The shaded bands are what each side of the strip samples.</p>
</div>

<div class="card">
  <h2>2. LED preview <span id="fps"></span></h2>
  <canvas id="prev"></canvas>
  <p class="hint">What the strip is showing right now, even if it isn't wired yet.</p>
</div>

<div class="card"><h2>Colour</h2><div class="grid" id="colour"></div></div>
<div class="card"><h2>Camera</h2><div class="grid" id="camera"></div>
  <p class="hint">Turn auto exposure off and lower Exposure until bright scenes aren't blown out.</p></div>
<div class="card"><h2>Strip layout</h2><div class="grid" id="layout"></div></div>
</main>
<script>
const $ = id => document.getElementById(id);
let cfg = null, pending = {}, timer = 0;

const GROUPS = {
  colour: [
    ['enabled', 'LEDs on', 'check'],
    ['brightness', 'Brightness', 0, 255, 1],
    ['saturation', 'Saturation', 0, 3, 0.05],
    ['gamma', 'Gamma', 1, 3, 0.05],
    ['smoothing', 'Smoothing', 0, 0.95, 0.01],
    ['black_level', 'Black level', 0, 80, 1],
    ['gain_r', 'Red', 0, 1.5, 0.01],
    ['gain_g', 'Green', 0, 1.5, 0.01],
    ['gain_b', 'Blue', 0, 1.5, 0.01],
    ['max_milliamps', 'Max current mA', 500, 9500, 100],
  ],
  camera: [
    ['aec', 'Auto exposure', 'check'],
    ['exposure', 'Exposure', 0, 1200, 1],
    ['agc', 'Auto gain', 'check'],
    ['gain', 'Gain', 0, 30, 1],
    ['awb', 'Auto white bal.', 'check'],
    ['vflip', 'Flip vertical', 'check'],
    ['hmirror', 'Mirror', 'check'],
  ],
  layout: [
    ['n_left', 'LEDs left', 'number'],
    ['n_top', 'LEDs top', 'number'],
    ['n_right', 'LEDs right', 'number'],
    ['start_corner', 'Strip starts', 'select', ['Bottom-left', 'Bottom-right']],
    ['depth', 'Sample depth', 0.03, 0.3, 0.01],
    ['inset', 'Bezel inset', 0, 0.1, 0.005],
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
        input.onchange = () => { send({[key]: input.checked}); drawCam(); };
      } else if (a === 'select') {
        input = document.createElement('select');
        b.forEach((t, i) => input.add(new Option(t, i)));
        input.value = cfg[key];
        input.onchange = () => { send({[key]: +input.value}); layoutChanged(); };
      } else if (a === 'number') {
        input = document.createElement('input');
        input.type = 'number'; input.min = 0; input.max = 300;
        input.value = cfg[key];
        input.onchange = () => { send({[key]: +input.value}); layoutChanged(); };
      } else {
        input = document.createElement('input');
        input.type = 'range'; input.min = a; input.max = b; input.step = step;
        input.value = cfg[key];
        val.textContent = cfg[key];
        input.oninput = () => {
          val.textContent = input.value;
          send({[key]: +input.value});
          if (group === 'layout') drawCam();
        };
      }
      row.append(input, val);
      box.append(row);
    }
  }
}

// ---- homography: unit square (u,v) -> TV quad in canvas pixels
function homography(p) {
  const [x0, y0, x1, y1, x2, y2, x3, y3] = p;
  const dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
  const dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
  const det = dx1 * dy2 - dx2 * dy1;
  const g = Math.abs(det) < 1e-9 ? 0 : (dx3 * dy2 - dx2 * dy3) / det;
  const h = Math.abs(det) < 1e-9 ? 0 : (dx1 * dy3 - dx3 * dy1) / det;
  const a = x1 - x0 + g * x1, b = x3 - x0 + h * x3, c = x0;
  const d = y1 - y0 + g * y1, e = y3 - y0 + h * y3, f = y0;
  return (u, v) => { const w = g * u + h * v + 1; return [(a * u + b * v + c) / w, (d * u + e * v + f) / w]; };
}

// ---- calibration canvas
const cam = $('cam'), cctx = cam.getContext('2d');
const img = new Image();
let drag = -1;

function sizeCanvas(c, aspect) {
  const w = c.clientWidth, dpr = devicePixelRatio || 1;
  c.width = w * dpr; c.height = w * aspect * dpr;
  return dpr;
}

function drawCam() {
  if (!cfg) return;
  const W = cam.width, H = cam.height, dpr = devicePixelRatio || 1;
  cctx.fillStyle = '#000'; cctx.fillRect(0, 0, W, H);
  if (img.complete && img.naturalWidth) cctx.drawImage(img, 0, 0, W, H);
  const pts = cfg.corners.map((v, i) => v * (i % 2 ? H : W));
  const m = homography(pts);

  // sampling bands
  const d0 = cfg.inset, d1 = cfg.inset + cfg.depth;
  cctx.fillStyle = 'rgba(77,163,255,0.28)';
  const band = (u0, u1, v0, v1) => {
    cctx.beginPath();
    [[u0, v0], [u1, v0], [u1, v1], [u0, v1]].forEach(([u, v], i) => {
      const [x, y] = m(u, v); i ? cctx.lineTo(x, y) : cctx.moveTo(x, y);
    });
    cctx.fill();
  };
  if (cfg.n_left) band(d0, d1, 0, 1);
  if (cfg.n_top) band(0, 1, d0, d1);
  if (cfg.n_right) band(1 - d1, 1 - d0, 0, 1);

  // outline + handles
  cctx.strokeStyle = '#4da3ff'; cctx.lineWidth = 2 * dpr;
  cctx.beginPath();
  for (let i = 0; i < 4; i++) cctx[i ? 'lineTo' : 'moveTo'](pts[i * 2], pts[i * 2 + 1]);
  cctx.closePath(); cctx.stroke();
  ['TL', 'TR', 'BR', 'BL'].forEach((t, i) => {
    const x = pts[i * 2], y = pts[i * 2 + 1];
    cctx.fillStyle = drag === i ? '#fff' : '#4da3ff';
    cctx.beginPath(); cctx.arc(x, y, 7 * dpr, 0, 7); cctx.fill();
    cctx.fillStyle = '#fff'; cctx.font = `${11 * dpr}px system-ui`;
    cctx.fillText(t, x + 10 * dpr, y - 8 * dpr);
  });
}

function canvasPoint(e) {
  const r = cam.getBoundingClientRect();
  return [(e.clientX - r.left) / r.width, (e.clientY - r.top) / r.height];
}

cam.addEventListener('pointerdown', e => {
  const [u, v] = canvasPoint(e), r = cam.getBoundingClientRect();
  let best = -1, bestD = 30;
  for (let i = 0; i < 4; i++) {
    const d = Math.hypot((cfg.corners[i * 2] - u) * r.width, (cfg.corners[i * 2 + 1] - v) * r.height);
    if (d < bestD) { bestD = d; best = i; }
  }
  if (best >= 0) { drag = best; cam.setPointerCapture(e.pointerId); drawCam(); }
});
cam.addEventListener('pointermove', e => {
  if (drag < 0) return;
  const [u, v] = canvasPoint(e);
  cfg.corners[drag * 2] = Math.min(1, Math.max(0, u));
  cfg.corners[drag * 2 + 1] = Math.min(1, Math.max(0, v));
  drawCam();
});
cam.addEventListener('pointerup', () => {
  if (drag < 0) return;
  drag = -1; drawCam();
  send({corners: cfg.corners.map(v => +v.toFixed(4))});
});

let liveTimer = 0;
function refreshImage() {
  img.src = '/capture?t=' + Date.now();
}
img.onload = () => {
  drawCam();
  if ($('live').checked) liveTimer = setTimeout(refreshImage, 400);
};
img.onerror = () => { if ($('live').checked) liveTimer = setTimeout(refreshImage, 1000); };
$('refresh').onclick = refreshImage;
$('live').onchange = () => { clearTimeout(liveTimer); if ($('live').checked) refreshImage(); };

// ---- LED preview
const prev = $('prev'), pctx = prev.getContext('2d');
let slots = [];  // canvas rect per LED, strip order

function layoutChanged() {
  const W = prev.width, H = prev.height;
  const tvW = W * 0.72, tvH = tvW * 9 / 16, x0 = (W - tvW) / 2, y0 = H - tvH - H * 0.04;
  const gap = W * 0.02, s = Math.max(3, W * 0.012);
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
  slots.tv = [x0, y0, tvW, tvH]; slots.s = s;
  drawCam();
}

async function pollLeds() {
  try {
    const r = await fetch('/api/leds');
    const px = new Uint8Array(await r.arrayBuffer());
    $('fps').textContent = r.headers.get('X-FPS') + ' fps';
    pctx.fillStyle = '#000'; pctx.fillRect(0, 0, prev.width, prev.height);
    const [x0, y0, w, h] = slots.tv || [0, 0, 0, 0];
    pctx.fillStyle = '#16181d'; pctx.fillRect(x0, y0, w, h);
    pctx.strokeStyle = '#333'; pctx.strokeRect(x0, y0, w, h);
    const s = slots.s;
    for (let i = 0; i < slots.length && i * 3 < px.length; i++) {
      const c = `rgb(${px[i * 3]},${px[i * 3 + 1]},${px[i * 3 + 2]})`;
      pctx.shadowColor = c; pctx.shadowBlur = s * 2;
      pctx.fillStyle = c;
      pctx.fillRect(slots[i][0], slots[i][1], s, s);
    }
    pctx.shadowBlur = 0;
  } catch (e) {}
  setTimeout(pollLeds, 100);
}

function resize() {
  sizeCanvas(cam, 3 / 4);
  sizeCanvas(prev, 0.5);
  if (cfg) layoutChanged();
}
addEventListener('resize', resize);

(async () => {
  cfg = await (await fetch('/api/config')).json();
  buildControls();
  resize();
  refreshImage();
  pollLeds();
})();
</script></body></html>)html";
