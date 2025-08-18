static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Trailer Level</title>
<style>
  :root {
    --bg:#000; --fg:#fff; --muted:#9aa0a6;
    --green:#00c853; --green-dim:#0a4023; --red:#ff1744;
    --card:#111; --line:#222; --white:#fff;
  }
  * { box-sizing:border-box; }
  html,body { margin:0; padding:0; background:var(--bg); color:var(--fg);
    font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Cantarell,Noto Sans,sans-serif; }
  .wrap { max-width: 960px; margin:0 auto; padding:8px 12px 28px; }
  header { display:flex; align-items:center; justify-content:space-between; gap:8px; padding:8px 0 16px; flex-wrap:wrap; }
  h1 { font-size:20px; margin:0; font-weight:600; letter-spacing:.3px; }
  .header-actions { display:flex; gap:8px; flex-wrap:wrap; }
  .grid { display:grid; grid-template-columns:1fr; gap:12px; }
  @media (min-width:720px){ .grid { grid-template-columns:1fr 1fr; } #card-level { grid-column: span 2; } }
  .card { background:var(--card); border:1px solid var(--line); border-radius:14px; padding:10px; }
  .row { display:flex; align-items:center; justify-content:space-between; gap:8px; margin-bottom:8px; flex-wrap:wrap; }
  .title { font-size:15px; font-weight:600; color:var(--fg); }
  .sub { font-size:12px; color:var(--muted); }
  .btns { display:flex; gap:8px; flex-wrap:wrap; }
  button { background:var(--green-dim); color:var(--white); border:1px solid #1b5e20; border-radius:10px; padding:8px 12px; font-size:14px; cursor:pointer; }
  button:hover { filter:brightness(1.2); }
  .btn-quiet { background:#0b0b0b; border-color:#2a2a2a; }
  canvas { width:100%; height:auto; display:block; background:#000; border-radius:10px; }
  #status { font-size:12px; color:var(--muted); padding:4px 8px; border:1px solid #1a1a1a; border-radius:999px; }
  .modal-backdrop { position:fixed; inset:0; background:rgba(0,0,0,.6); display:none; align-items:center; justify-content:center; z-index:10; }
  .modal { width:min(92vw,480px); background:var(--card); border:1px solid var(--line); border-radius:12px; padding:14px; }
  .modal h2 { margin:0 0 12px; font-size:16px; }
  .form { display:grid; gap:10px; }
  .form-row { display:grid; grid-template-columns:1fr; gap:6px; }
  label { font-size:13px; color:var(--muted); }
  input,select { width:100%; padding:10px; border-radius:8px; border:1px solid #2a2a2a; background:#0b0b0b; color:var(--fg); font-size:14px; }
  .modal .actions { display:flex; justify-content:flex-end; gap:8px; margin-top:10px; }
</style>
</head>
<body>
  <div class="wrap">
    <header>
      <h1>Trailer Level Dashboard</h1>
      <div class="header-actions">
        <button id="btn-wifi" class="btn-quiet">Wi-Fi</button>
        <span id="status">connecting...</span>
      </div>
    </header>

    <div class="grid">
      <div id="card-level" class="card">
        <div class="row">
          <div class="title">Leveling</div>
          <div class="btns">
            <button id="btn-cal">Calibrate</button>
            <button id="btn-lev-prec" class="btn-quiet">Precision: …</button>
          </div>
        </div>
        <canvas id="cv-level" width="800" height="600"></canvas>
        <div class="sub" id="txt-level">pitch 0°, roll 0°</div>
      </div>

      <div class="card">
        <div class="row">
          <div class="title">Motion</div>
          <div class="btns">
            <button id="btn-orient">Orientation</button>
            <button id="btn-mot-prec" class="btn-quiet">Motion: …</button>
          </div>
        </div>
        <canvas id="cv-motion" width="800" height="600"></canvas>
        <div class="sub" id="txt-motion">accel f/r/u: 0, 0, 0</div>
      </div>

      <div class="card">
        <div class="row">
          <div class="title">Roll</div>
          <div class="btns">
            <button id="btn-roll-prec" class="btn-quiet">Roll: …</button>
          </div>
        </div>
        <canvas id="cv-roll" width="800" height="600"></canvas>
        <div class="sub" id="txt-roll">gyro pitch/roll/turn: 0, 0, 0</div>
      </div>
    </div>
  </div>

  <!-- Orientation Modal -->
  <div id="orient-backdrop" class="modal-backdrop">
    <div class="modal">
      <h2>Sensor Orientation</h2>
      <div class="form">
        <div class="form-row">
          <label for="sel-forward">Forward axis</label>
          <select id="sel-forward"></select>
        </div>
        <div class="form-row">
          <label for="sel-right">Right axis</label>
          <select id="sel-right"></select>
        </div>
        <div class="form-row">
          <label for="sel-up">Up axis</label>
          <select id="sel-up"></select>
        </div>
      </div>
      <div class="actions">
        <button id="btn-cancel">Cancel</button>
        <button id="btn-save">Save</button>
      </div>
    </div>
  </div>

  <!-- Wi-Fi Modal -->
  <div id="wifi-backdrop" class="modal-backdrop">
    <div class="modal">
      <h2>Wi-Fi Settings</h2>
      <div class="form">
        <div class="form-row">
          <label for="ssid">SSID</label>
          <input id="ssid" placeholder="New SSID">
        </div>
        <div class="form-row">
          <label for="pwd">Password</label>
          <input id="pwd" placeholder="New password" type="password">
        </div>
      </div>
      <div class="actions">
        <button id="btn-wifi-cancel">Cancel</button>
        <button id="btn-wifi-save">Save</button>
      </div>
      <div class="sub" style="margin-top:6px">Device may change AP shortly.</div>
    </div>
  </div>

<script>
(function(){
  // -------- Zoom / precision --------------------------------------------------
  function autoScale(){
    const w = Math.min(window.innerWidth, document.documentElement.clientWidth || 9999);
    if (w < 340) return 0.65;
    if (w < 380) return 0.72;
    if (w < 420) return 0.78;
    if (w < 520) return 0.85;
    if (w < 620) return 0.92;
    return 1.0;
  }
  const params = new URLSearchParams(location.search);
  let manualScale = parseFloat(params.get('scale') || localStorage.getItem('scale') || '1');
  if (!isFinite(manualScale) || manualScale <= 0.3 || manualScale > 2) manualScale = 1;
  function totalScale(){ return 0.8 * autoScale() * manualScale; } // base 0.8, then auto, then manual

  const LEVEL_STEPS  = [45,20,10,5,2,1];               // degrees to ring edge
  const MOTION_STEPS = [1.0,0.5,0.25,0.10,0.05,0.02];  // g full-scale
  const ROLL_STEPS   = [180,120,90,60,30,15];          // dps full-scale

  let levIdx = +(localStorage.getItem('levIdx') || 2);
  let motIdx = +(localStorage.getItem('motIdx') || 0);
  let rolIdx = +(localStorage.getItem('rolIdx') || 2);
  const wrap = (n, m)=> (n % m + m) % m;
  levIdx = wrap(levIdx, LEVEL_STEPS.length);
  motIdx = wrap(motIdx, MOTION_STEPS.length);
  rolIdx = wrap(rolIdx, ROLL_STEPS.length);

  // -------- DOM --------------------------------------------------------------
  const $ = (sel)=>document.querySelector(sel);
  const statusEl = $("#status");
  const cvLevel = $("#cv-level"), ctxL = cvLevel.getContext("2d");
  const cvMotion = $("#cv-motion"), ctxM = cvMotion.getContext("2d");
  const cvRoll = $("#cv-roll"), ctxR = cvRoll.getContext("2d");
  let lastOkTs = 0;

  // Orientation selects
  const axisOptions = ["X","-X","Y","-Y","Z","-Z"];
  function fillSelect(el){ el.innerHTML = axisOptions.map(a=>`<option value="${a}">${a}</option>`).join(""); }
  fillSelect($("#sel-forward")); fillSelect($("#sel-right")); fillSelect($("#sel-up"));

  // Precision buttons
  const btnLev = $("#btn-lev-prec");
  const btnMot = $("#btn-mot-prec");
  const btnRol = $("#btn-roll-prec");
  function refreshPrecButtons(){
    btnLev.textContent = `Precision: ${LEVEL_STEPS[levIdx]}°`;
    btnMot.textContent = `Motion: ±${MOTION_STEPS[motIdx]}g`;
    btnRol.textContent = `Roll: ±${ROLL_STEPS[rolIdx]}°/s`;
  }
  refreshPrecButtons();
  btnLev.addEventListener("click", ()=>{ levIdx=(levIdx+1)%LEVEL_STEPS.length; localStorage.setItem('levIdx',levIdx); refreshPrecButtons(); });
  btnMot.addEventListener("click", ()=>{ motIdx=(motIdx+1)%MOTION_STEPS.length; localStorage.setItem('motIdx',motIdx); refreshPrecButtons(); });
  btnRol.addEventListener("click", ()=>{ rolIdx=(rolIdx+1)%ROLL_STEPS.length; localStorage.setItem('rolIdx',rolIdx); refreshPrecButtons(); });

  // Wi-Fi modal
  const wifiModal = $("#wifi-backdrop");
  $("#btn-wifi").addEventListener("click", ()=>{ wifiModal.style.display="flex"; });
  $("#btn-wifi-cancel").addEventListener("click", ()=>{ wifiModal.style.display="none"; });
  $("#btn-wifi-save").addEventListener("click", async ()=>{
    const ssid = $("#ssid").value.trim();
    const pwd  = $("#pwd").value;
    if (!ssid) { alert("SSID required"); return; }
    try {
      const r = await fetch("/wifi", { method:"POST", headers:{ "Content-Type":"application/json" }, body: JSON.stringify({ ssid, password: pwd }) });
      if (r.ok) {
        const j = await r.json();
        statusEl.textContent = `AP restarting → reconnect to "${j.ssid}"`;
      } else {
        statusEl.textContent = "Wi-Fi save failed";
      }
    } catch { statusEl.textContent = "Wi-Fi save failed"; }
    wifiModal.style.display="none";
  });

  // Orientation modal
  const orientModal = $("#orient-backdrop");
  function openModal(){ orientModal.style.display="flex"; }
  function closeModal(){ orientModal.style.display="none"; }
  $("#btn-orient").addEventListener("click", async ()=>{
    const o = await getOrientation();
    if (o && o.legend) {
      $("#sel-forward").value = o.legend.forward || "X";
      $("#sel-right").value   = o.legend.right   || "Y";
      $("#sel-up").value      = o.legend.up      || "Z";
    }
    openModal();
  });
  $("#btn-cancel").addEventListener("click", closeModal);
  $("#btn-save").addEventListener("click", async ()=>{
    const ok = await setOrientation($("#sel-forward").value, $("#sel-right").value, $("#sel-up").value);
    statusEl.textContent = ok ? "orientation saved" : "save failed";
    setTimeout(()=>statusEl.textContent="ok", 1200);
    closeModal();
  });

  // API helpers
  async function getOrientation(){
    try { const r = await fetch("/orientation"); if(!r.ok) throw 0; return await r.json(); } catch { return null; }
  }
  async function setOrientation(forward,right,up){
    const body = { forward, right, up };
    const r = await fetch("/orientation",{ method:"POST", headers:{ "Content-Type":"application/json" }, body: JSON.stringify(body) });
    return r.ok;
  }
  async function calibrate(){
    statusEl.textContent = "calibrating...";
    try { const r = await fetch("/calibrate",{ method:"POST" }); if(r.ok){ statusEl.textContent = "calibrated"; setTimeout(()=>statusEl.textContent="ok", 1200);} else throw 0; }
    catch { statusEl.textContent = "cal failed"; }
  }
  $("#btn-cal").addEventListener("click", calibrate);

  // Utils
  function lerp(a,b,t){ return a + (b-a)*t; }
  function clamp(v, lo, hi){ return Math.min(hi, Math.max(lo, v)); }
  function map01(v, fullScale){ return Math.min(1, Math.max(0, Math.abs(v)/fullScale)); }
  function mix3(a,b,t){ return Math.round(lerp(a,b,t)); }

  // -------- Level gauge ------------------------------------------------------
  function drawLeveling(d){
    const W = cvLevel._w, H = cvLevel._h;
    const SCALE = totalScale();
    const s = Math.min(W,H) * SCALE;
    const cx = W/2, cy = H/2;
    const R = s*0.35;
    const fullDeg = LEVEL_STEPS[levIdx];
    const pitch = d.pos_pitch_calibrated || 0;
    const roll  = d.pos_roll_calibrated  || 0;

    ctxL.clearRect(0,0,W,H);

    // ring
    ctxL.beginPath(); ctxL.arc(cx, cy, R, 0, Math.PI*2);
    ctxL.fillStyle = "#001c0e"; ctxL.fill();
    ctxL.strokeStyle = "#1f1f1f"; ctxL.lineWidth = 2; ctxL.stroke();

    // cross
    ctxL.strokeStyle = "rgba(255,255,255,0.25)"; ctxL.lineWidth = 1;
    ctxL.beginPath(); ctxL.moveTo(cx-R, cy); ctxL.lineTo(cx+R, cy); ctxL.stroke();
    ctxL.beginPath(); ctxL.moveTo(cx, cy-R); ctxL.lineTo(cx, cy+R); ctxL.stroke();

    // Labels: half the previous "double" size, but still pushed far out
    ctxL.fillStyle = "#ffffff";
    ctxL.textAlign = "center"; ctxL.textBaseline = "middle";
    ctxL.font = Math.round(s*0.110)+"px system-ui, sans-serif"; // half of 0.220
    const off = s*0.200;                                       // keep far to avoid chevrons

    ctxL.fillText(pitch.toFixed(1)+"°",     cx, cy - R - off);
    ctxL.fillText((-pitch).toFixed(1)+"°",  cx, cy + R + off);

    ctxL.save(); ctxL.translate(cx - R - off, cy); ctxL.rotate(-Math.PI/2);
    ctxL.fillText((-roll).toFixed(1)+"°", 0, 0); ctxL.restore();

    ctxL.save(); ctxL.translate(cx + R + off, cy); ctxL.rotate(Math.PI/2);
    ctxL.fillText((roll).toFixed(1)+"°", 0, 0); ctxL.restore();

    // chevrons (outside)
    const near = (v)=>Math.abs(v) < 0.5;
    function chevron(x, y, dir){
      const g = near(dir==="pt"?pitch:roll) ? "#00c853" : "#0a4023";
      ctxL.fillStyle = g;
      const w = s*0.05, h = s*0.035;
      ctxL.beginPath(); ctxL.moveTo(x, y);
      if (dir==="pt") { ctxL.lineTo(x-w, y-h); ctxL.lineTo(x+w, y-h); }
      if (dir==="pb") { ctxL.lineTo(x-w, y+h); ctxL.lineTo(x+w, y+h); }
      if (dir==="rl") { ctxL.lineTo(x-h, y-w); ctxL.lineTo(x-h, y+w); }
      if (dir==="rr") { ctxL.lineTo(x+h, y-w); ctxL.lineTo(x+h, y+w); }
      ctxL.closePath(); ctxL.fill();
    }
    chevron(cx, cy - R - s*0.035, "pt");
    chevron(cx, cy + R + s*0.035, "pb");
    chevron(cx - R - s*0.035, cy, "rl");
    chevron(cx + R + s*0.035, cy, "rr");

    // red dot mapping: fullDeg hits ~edge
    const k = R*0.85 / fullDeg;
    let dx = clamp(roll * k,  -R*0.85, R*0.85);
    let dy = clamp(pitch * k, -R*0.85, R*0.85);
    const mag = Math.hypot(dx, dy);
    if (mag > R*0.85) { dx *= (R*0.85/mag); dy *= (R*0.85/mag); }

    ctxL.beginPath(); ctxL.arc(cx+dx, cy+dy, s*0.02, 0, Math.PI*2);
    ctxL.fillStyle = "#ff1744"; ctxL.shadowColor = "#ff1744"; ctxL.shadowBlur = 8; ctxL.fill(); ctxL.shadowBlur = 0;

    $("#txt-level").textContent = `pitch ${pitch.toFixed(2)}°, roll ${roll.toFixed(2)}° (±${fullDeg}° view)`;
  }

  // -------- Shared arrows (motion & roll) with big numbers -------------------
  function drawArrows(ctx, W, H, SCALE, values, labelEl, labelText, numbers, unit){
    const s = Math.min(W,H) * SCALE;
    const cx = W/2, cy = H/2;
    ctx.clearRect(0,0,W,H);

    // crosshair
    ctx.strokeStyle = "#1f1f1f"; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(cx - s*0.4, cy); ctx.lineTo(cx + s*0.4, cy); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(cx, cy - s*0.4); ctx.lineTo(cx, cy + s*0.4); ctx.stroke();

    function cardArrow(angleRad, t){
      // tiny when idle
      const len = lerp(s*0.02, s*0.34, clamp(t,0,1));
      const w  = lerp(s*0.006, s*0.018, clamp(t,0,1));
      const x2 = cx + Math.cos(angleRad)*len;
      const y2 = cy + Math.sin(angleRad)*len;

      // color: black -> green -> white
      const mid = 0.6;
      const t1 = clamp(t/mid, 0, 1);
      const t2 = clamp((t-mid)/(1-mid), 0, 1);
      const gR = mix3(0,0,t1) + mix3(0,255,t2);
      const gG = mix3(0,200,t1) + mix3(200,255,t2);
      const gB = mix3(0,80,t1) + mix3(80,255,t2);
      const col = `rgb(${gR},${gG},${gB})`;

      ctx.strokeStyle = col; ctx.lineWidth = w; ctx.lineCap = "round";
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(x2, y2); ctx.stroke();

      // head
      const head = lerp(s*0.012, s*0.03, clamp(t,0,1));
      const th = Math.atan2(y2-cy, x2-cx);
      ctx.beginPath();
      ctx.moveTo(x2, y2);
      ctx.lineTo(x2 - Math.cos(th - 0.6)*head, y2 - Math.sin(th - 0.6)*head);
      ctx.lineTo(x2 - Math.cos(th + 0.6)*head, y2 - Math.sin(th + 0.6)*head);
      ctx.closePath();
      ctx.fillStyle = col;
      ctx.fill();
    }

    // arrows
    cardArrow(-Math.PI/2, values.up);
    cardArrow( Math.PI/2, values.down);
    cardArrow( 0,         values.right);
    cardArrow( Math.PI,   values.left);

    // Numbers like Leveling (half the previous double size) & pushed far out
    if (numbers) {
      ctx.fillStyle = "#ffffff";
      ctx.textAlign = "center"; ctx.textBaseline = "middle";
      ctx.font = Math.round(s*0.110)+"px system-ui, sans-serif"; // half of 0.220
      const L = s*0.40;       // crosshair half-length
      const off = s*0.200;    // keep outward beyond arrowheads

      const fmt = (v)=>{
        const av = Math.abs(v);
        let d = 2;
        if (unit === "°/s") d = (av>=100)?0:1;
        if (unit === " g")  d = 2;
        return v.toFixed(d) + unit;
      };

      ctx.fillText(fmt(numbers.top),    cx, cy - L - off);
      ctx.fillText(fmt(numbers.bottom), cx, cy + L + off);

      ctx.save(); ctx.translate(cx - L - off, cy); ctx.rotate(-Math.PI/2);
      ctx.fillText(fmt(numbers.left), 0, 0); ctx.restore();

      ctx.save(); ctx.translate(cx + L + off, cy); ctx.rotate(Math.PI/2);
      ctx.fillText(fmt(numbers.right), 0, 0); ctx.restore();
    }

    if (labelEl) labelEl.textContent = labelText;
  }

  // -------- Motion (accelerometer) ------------------------------------------
  function drawMotion(d){
    const W = cvMotion._w, H = cvMotion._h;
    const SCALE = totalScale();
    const gFS = MOTION_STEPS[motIdx];

    const fwd = +(d.accel_forward || 0);
    const rgt = +(d.accel_right   || 0);

    const tF = map01(Math.max(0,  fwd), gFS);
    const tB = map01(Math.max(0, -fwd), gFS);
    const tR = map01(Math.max(0,  rgt), gFS);
    const tL = map01(Math.max(0, -rgt), gFS);

    drawArrows(
      ctxM, W, H, SCALE,
      { up:tF, down:tB, left:tL, right:tR },
      $("#txt-motion"),
      `accel f/r/u: ${fwd.toFixed(2)}, ${rgt.toFixed(2)}, ${+(d.accel_up||0).toFixed(2)} (±${gFS}g view)`,
      { top:fwd, bottom:-fwd, left:-rgt, right:rgt },
      " g"
    );
  }

  // -------- Roll (gyro) ------------------------------------------------------
  function drawRoll(d){
    const W = cvRoll._w, H = cvRoll._h;
    const SCALE = totalScale();
    const dpsFS = ROLL_STEPS[rolIdx];

    const pitchRate = (+(d.gyro_pitchup||0)) - (+(d.gyro_pitchdown||0));
    const rollRate  = (+(d.gyro_rollright||0)) - (+(d.gyro_rollleft||0));

    const tPU = map01(Math.max(0,  pitchRate), dpsFS);
    const tPD = map01(Math.max(0, -pitchRate), dpsFS);
    const tRR = map01(Math.max(0,  rollRate),  dpsFS);
    const tRL = map01(Math.max(0, -rollRate),  dpsFS);

    drawArrows(
      ctxR, W, H, SCALE,
      { up:tPU, down:tPD, left:tRL, right:tRR },
      $("#txt-roll"),
      `gyro pitch/roll/turn dps: ${pitchRate.toFixed(1)}, ${rollRate.toFixed(1)}, ${((+(d.gyro_turnright||0))-(+(d.gyro_turnleft||0))).toFixed(1)} (±${dpsFS}°/s view)`,
      { top:pitchRate, bottom:-pitchRate, left:-rollRate, right:rollRate },
      "°/s"
    );

    // U-shaped roll overlays: black→green→white, invisible when 0
    const s = Math.min(W,H) * SCALE;
    const cx = W/2, cy = H/2;
    function uShape(x,y,rot, t){
      const rOuter = s*0.28, rInner = s*0.18, start = Math.PI*0.2, end = Math.PI*1.8;
      const mid = 0.6;
      const t1 = clamp(t/mid, 0, 1);
      const t2 = clamp((t-mid)/(1-mid), 0, 1);
      const gR = mix3(0,0,t1) + mix3(0,255,t2);
      const gG = mix3(0,200,t1) + mix3(200,255,t2);
      const gB = mix3(0,80,t1) + mix3(80,255,t2);
      ctxR.save(); ctxR.translate(x,y); ctxR.rotate(rot);
      ctxR.beginPath(); ctxR.arc(0,0,rOuter, start, end); ctxR.arc(0,0,rInner, end, start, true); ctxR.closePath();
      ctxR.fillStyle = `rgb(${gR},${gG},${gB})`;
      ctxR.globalAlpha = t;  // fades to invisible at 0
      ctxR.fill(); ctxR.globalAlpha = 1;
      ctxR.restore();
    }
    uShape(cx, cy - s*0.02, 0,          tPU);
    uShape(cx, cy + s*0.02, Math.PI,    tPD);
    uShape(cx - s*0.02, cy, -Math.PI/2, tRL);
    uShape(cx + s*0.02, cy,  Math.PI/2, tRR);
  }

  // -------- Poll loop --------------------------------------------------------
  async function tick(){
    try{
      const r = await fetch("/sensor", { cache:"no-store" });
      if (!r.ok) throw 0;
      const d = await r.json();
      lastOkTs = performance.now();
      statusEl.textContent = "ok";
      drawLeveling(d);
      drawMotion(d);
      drawRoll(d);
    } catch {
      const dt = (performance.now()-lastOkTs)/1000;
      statusEl.textContent = dt > 3 ? "offline" : "connecting...";
    }
  }

  // -------- Canvas sizing ----------------------------------------------------
  function fitCanvas(cv){
    const dpr = window.devicePixelRatio || 1;
    const rect = cv.getBoundingClientRect();
    const w = Math.max(260, Math.floor(rect.width));
    const h = Math.max(240, Math.floor(rect.width * 0.65)); // tall enough for labels
    cv.width = Math.round(w*dpr);
    cv.height = Math.round(h*dpr);
    cv._w = w; cv._h = h; // CSS px size
    cv.getContext("2d").setTransform(dpr,0,0,dpr,0,0);
  }
  function resizeAll(){ [cvLevel, cvMotion, cvRoll].forEach(fitCanvas); }
  window.addEventListener("resize", ()=>{ resizeAll(); tick(); });

  // Boot
  resizeAll();
  const POLL_MS = Math.max(100, parseInt(params.get('ms') || '250', 10));
  setInterval(tick, POLL_MS);
  tick();
})();
</script>
</body>
</html>
)HTML";
