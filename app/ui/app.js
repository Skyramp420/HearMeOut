// HearMeOut – UI (téma Čistá, WheresTheApp)
'use strict';

const TAURI = window.__TAURI__;
const IS_APP = !!TAURI;
const $ = (s, r = document) => r.querySelector(s);
const h = (tag, attrs = {}, ...kids) => {
  const e = tag === 'svg' ? document.createElementNS('http://www.w3.org/2000/svg', 'svg') : document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') e.setAttribute('class', v);
    else if (k === 'html') e.innerHTML = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'style') e.style.cssText = v;
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const c of kids.flat()) if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(c));
  return e;
};
const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
const round = (x, d = 1) => Math.round(x * 10 ** d) / 10 ** d;

// ------------------------------------------------------------------ ikony (16×16, stroke)
const P = {
  mic: '<rect x="5.5" y="1.75" width="5" height="8" rx="2.5"/><path d="M3.25 7.75a4.75 4.75 0 0 0 9.5 0M8 12.5v1.75"/>',
  presets: '<path d="M2.5 4.5h7M12.5 4.5h1M2.5 11.5h1M6.5 11.5h7"/><circle cx="11" cy="4.5" r="1.5"/><circle cx="5" cy="11.5" r="1.5"/>',
  board: '<rect x="2" y="2" width="5" height="5" rx="1.4"/><rect x="9" y="2" width="5" height="5" rx="1.4"/><rect x="2" y="9" width="5" height="5" rx="1.4"/><path d="M11.5 9.5v4M9.5 11.5h4"/>',
  gear: '<circle cx="8" cy="8" r="2.2"/><path d="M8 1.8v1.6M8 12.6v1.6M3.6 3.6l1.1 1.1M11.3 11.3l1.1 1.1M1.8 8h1.6M12.6 8h1.6M3.6 12.4l1.1-1.1M11.3 4.7l1.1-1.1"/>',
  clean: '<path d="M1.5 8h1.2l1.2-3 1.6 6 1.6-8 1.6 9 1.4-5.5 1 2.5h3.4"/>',
  dyn: '<path d="M8 1.75v4.5M5.75 4 8 6.25 10.25 4M8 14.25v-4.5M5.75 12 8 9.75 10.25 12M2.5 8h11"/>',
  tone: '<path d="M2 10c2-6 4-6 6 0s4 6 6 0"/>',
  sib: '<path d="M10.5 4.25C10 3.2 9.1 2.75 8 2.75c-1.6 0-2.75.9-2.75 2.25C5.25 8 10.75 7 10.75 10.5c0 1.6-1.3 2.75-2.85 2.75-1.3 0-2.3-.6-2.8-1.75"/>',
  out: '<path d="M2.5 6v4h2.5L8.5 13V3L5 6Z"/><path d="M11 5.5a3.5 3.5 0 0 1 0 5M12.75 3.75a6 6 0 0 1 0 8.5"/>',
  phones: '<path d="M2.75 10V8a5.25 5.25 0 0 1 10.5 0v2"/><rect x="2" y="9.5" width="3" height="4.25" rx="1.2"/><rect x="11" y="9.5" width="3" height="4.25" rx="1.2"/>',
  chev: '<path d="m6 3.5 4.5 4.5L6 12.5"/>',
  left: '<path d="M10 3.5 5.5 8l4.5 4.5"/>',
  right: '<path d="m6 3.5 4.5 4.5L6 12.5"/>',
  down: '<path d="m3.5 6 4.5 4.5L12.5 6"/>',
  check: '<path d="m3.5 8.5 3 3 6-7"/>',
  warn: '<path d="M8 2 14.5 13.5h-13Z"/><path d="M8 6.5v3M8 11.75v.01"/>',
  plus: '<path d="M8 3v10M3 8h10"/>',
  trash: '<path d="M3 4.5h10M6.5 4.5V3h3v1.5M4.5 4.5l.6 8.5h5.8l.6-8.5"/>',
  folder: '<path d="M2 4.5V12a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V6a1 1 0 0 0-1-1H8L6.5 3.5H3a1 1 0 0 0-1 1Z"/>',
  sparkle: '<path d="M8 1.5c.6 3.4 1.9 4.9 5.5 5.5-3.6.6-4.9 2.1-5.5 5.5-.6-3.4-1.9-4.9-5.5-5.5 3.6-.6 4.9-2.1 5.5-5.5Z"/>',
  wand: '<path d="m2.5 13.5 8-8M9 3l.4 1.1L10.5 4.5l-1.1.4L9 6l-.4-1.1-1.1-.4 1.1-.4ZM12.5 7l.3.8.7.2-.7.2-.3.8-.3-.8-.7-.2.7-.2Z"/>',
  input: '<path d="M2 8h7M6.5 5.5 9 8l-2.5 2.5"/><rect x="10" y="3" width="4" height="10" rx="2"/>',
  sliders: '<path d="M4 2.5v11M8 2.5v11M12 2.5v11"/><rect x="2.5" y="9" width="3" height="2.2" rx=".8"/><rect x="6.5" y="4.5" width="3" height="2.2" rx=".8"/><rect x="10.5" y="7.5" width="3" height="2.2" rx=".8"/>',
  x: '<path d="m4 4 8 8M12 4l-8 8"/>',
  open: '<path d="M9.5 2.5h4v4M13.5 2.5 8 8M11.5 9.5v3a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1v-7a1 1 0 0 1 1-1h3"/>',
  plug: '<path d="M6 1.75v3M10 1.75v3M4.25 4.75h7.5v2.5a3.75 3.75 0 0 1-7.5 0ZM8 11v3.25"/>',
};
const icon = (n, cls = 'i') => { const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); s.setAttribute('viewBox', '0 0 16 16'); s.setAttribute('class', cls); s.innerHTML = P[n] || ''; return s; };

// ------------------------------------------------------------------ schéma (zhodná s APO) + makrá appky [ui]
const SCHEMA = {
  'global.bypass': [0, 'bool'], 'global.engine': [0, 'int'], 'global.input_channel': [0, 'int'], 'global.input_gain_db': [0], 'global.output_gain_db': [0],
  'highpass.enabled': [1, 'bool'], 'highpass.freq': [90],
  'denoise.enabled': [1, 'bool'], 'denoise.reduction_db': [24], 'denoise.strength': [1.8],
  'gate.enabled': [1, 'bool'], 'gate.threshold_db': [-50], 'gate.ratio': [2.5], 'gate.range_db': [16],
  'gate.attack_ms': [1], 'gate.hold_ms': [90], 'gate.release_ms': [140],
  'eq.enabled': [1, 'bool'], 'eq.low_freq': [120], 'eq.low_gain_db': [2], 'eq.mud_freq': [300], 'eq.mud_gain_db': [-3], 'eq.mud_q': [1],
  'eq.presence_freq': [3800], 'eq.presence_gain_db': [2.5], 'eq.presence_q': [0.9], 'eq.air_freq': [11000], 'eq.air_gain_db': [1.5],
  'compressor.enabled': [1, 'bool'], 'compressor.mode': [2, 'int'], 'compressor.threshold_db': [-25], 'compressor.ratio': [3.6],
  'compressor.knee_db': [6], 'compressor.attack_ms': [6], 'compressor.release_ms': [110],
  'compressor.auto_makeup': [1, 'bool'], 'compressor.makeup_db': [0],
  'saturation.enabled': [1, 'bool'], 'saturation.drive': [40],
  'deesser.enabled': [1, 'bool'], 'deesser.freq': [6500], 'deesser.threshold_db': [-30], 'deesser.range_db': [8],
  'limiter.enabled': [1, 'bool'], 'limiter.ceiling_db': [-1], 'limiter.release_ms': [60],
  // makrá – APO ich ignoruje, appka si podľa nich pamätá polohu hlavných posuvníkov
  'ui.clean': [55, 'int'], 'ui.dyn_char': [1, 'int'], 'ui.dyn_amount': [60, 'int'], 'ui.color': [40, 'int'],
  'ui.tone_char': [4, 'int'], 'ui.sib': [55, 'int'],
};
// nastavenia viazané na mikrofón/miestnosť – presety ich nemenia
const DEVICE_KEYS = ['global.bypass', 'global.engine', 'global.input_channel', 'global.input_gain_db', 'gate.threshold_db'];
const defaults = () => Object.fromEntries(Object.entries(SCHEMA).map(([k, [d]]) => [k, d]));

function fromIni(map) {
  const v = defaults();
  for (const [k, [, t]] of Object.entries(SCHEMA)) {
    if (!(k in map)) continue;
    const s = String(map[k]).trim().toLowerCase();
    if (t === 'bool') v[k] = ['1', 'true', 'on', 'yes'].includes(s) ? 1 : 0;
    else { const n = parseFloat(s); if (Number.isFinite(n)) v[k] = t === 'int' ? Math.round(n) : n; }
  }
  return v;
}
const toIni = v => Object.fromEntries(Object.entries(SCHEMA).map(([k]) => [k, String(+(+v[k]).toFixed(3))]));

// ------------------------------------------------------------------ makrá -> parametre
const DYN_CHARS = [
  { name: 'Jemná', sub: 'Opto', mode: 0, attack: 12, release: 180, knee: 8, rmax: 3.5 },
  { name: 'Úderná', sub: 'Opto + FET', mode: 2, attack: 6, release: 110, knee: 6, rmax: 5 },
  { name: 'Rádio', sub: 'FET', mode: 1, attack: 1.5, release: 70, knee: 3, rmax: 8 },
];
const TONE_CHARS = [
  { name: 'Prirodzený', low: [1, 110], mud: [-2, 300], pres: [1.5, 4000], air: [1, 11000] },
  { name: 'Teplý', low: [3, 120], mud: [-3, 280], pres: [1, 3500], air: [0, 12000] },
  { name: 'Jasný', low: [0.5, 110], mud: [-2.5, 320], pres: [3, 4200], air: [3, 10000] },
  { name: 'Rádio', low: [2.5, 130], mud: [-3.5, 350], pres: [4, 3200], air: [1.5, 9000] },
  { name: 'Štúdio', low: [2, 120], mud: [-3, 300], pres: [2.5, 3800], air: [1.5, 11000] },
];
const MACRO = {
  clean(v) { const s = v['ui.clean'] / 100; v['denoise.reduction_db'] = Math.round(10 + 26 * s); v['denoise.strength'] = round(1.1 + 1.4 * s, 2); },
  dyn(v) {
    const c = DYN_CHARS[clamp(v['ui.dyn_char'], 0, 2)], a = v['ui.dyn_amount'] / 100;
    Object.assign(v, { 'compressor.mode': c.mode, 'compressor.attack_ms': c.attack, 'compressor.release_ms': c.release, 'compressor.knee_db': c.knee,
      'compressor.threshold_db': Math.round(-12 - 22 * a), 'compressor.ratio': round(1.4 + (c.rmax - 1.4) * a, 1), 'compressor.auto_makeup': 1 });
  },
  color(v) { v['saturation.drive'] = v['ui.color']; v['saturation.enabled'] = v['ui.color'] > 0 && v['compressor.enabled'] ? 1 : 0; },
  tone(v) {
    const c = TONE_CHARS[v['ui.tone_char']]; if (!c) return;
    Object.assign(v, { 'eq.low_gain_db': c.low[0], 'eq.low_freq': c.low[1], 'eq.mud_gain_db': c.mud[0], 'eq.mud_freq': c.mud[1],
      'eq.presence_gain_db': c.pres[0], 'eq.presence_freq': c.pres[1], 'eq.air_gain_db': c.air[0], 'eq.air_freq': c.air[1] });
  },
  sib(v) { const d = v['ui.sib'] / 100; v['deesser.threshold_db'] = Math.round(-18 - 22 * d); v['deesser.range_db'] = Math.round(4 + 8 * d); },
};
const applyAllMacros = v => { for (const f of Object.values(MACRO)) f(v); return v; };

// ------------------------------------------------------------------ presety (ladené na Scarlett CM25 Mk3)
const BUILTIN = [
  { name: 'Punchy Štúdio', desc: 'Úderný, hutný hlas s farbou – v duchu tvojho xVox presetu.',
    m: { 'ui.clean': 55, 'ui.dyn_char': 1, 'ui.dyn_amount': 62, 'ui.color': 55, 'ui.tone_char': 4, 'ui.sib': 60 }, v: { 'highpass.freq': 100 } },
  { name: 'Prirodzený', desc: 'Znieš ako ty, len čistejšie a vyrovnanejšie.',
    m: { 'ui.clean': 40, 'ui.dyn_char': 0, 'ui.dyn_amount': 35, 'ui.color': 15, 'ui.tone_char': 0, 'ui.sib': 35 }, v: { 'gate.range_db': 12 } },
  { name: 'Podcast', desc: 'Teplý a blízky hlas, príjemný na dlhé počúvanie.',
    m: { 'ui.clean': 55, 'ui.dyn_char': 0, 'ui.dyn_amount': 55, 'ui.color': 30, 'ui.tone_char': 1, 'ui.sib': 50 } },
  { name: 'Stream', desc: 'Hlasný, stále rovnako silný a jasný – presadí sa cez hru.',
    m: { 'ui.clean': 60, 'ui.dyn_char': 2, 'ui.dyn_amount': 65, 'ui.color': 45, 'ui.tone_char': 0, 'ui.sib': 60 }, v: { 'eq.presence_gain_db': 2.5, 'limiter.ceiling_db': -1.5 } },
  { name: 'Hlučná izba', desc: 'Naplno čistí – ventilátor, PC, ulica za oknom.',
    m: { 'ui.clean': 90, 'ui.dyn_char': 1, 'ui.dyn_amount': 50, 'ui.color': 30, 'ui.tone_char': 0, 'ui.sib': 45 }, v: { 'gate.range_db': 24, 'highpass.freq': 120 } },
  { name: 'Len čistenie', desc: 'Ako tvoj Speachy „DeNoise“ – iba šum a ticho, zvuk nechá tak.',
    m: { 'ui.clean': 60, 'ui.dyn_char': 1, 'ui.dyn_amount': 0, 'ui.color': 0, 'ui.tone_char': 0, 'ui.sib': 0 },
    v: { 'compressor.enabled': 0, 'saturation.enabled': 0, 'eq.enabled': 0, 'deesser.enabled': 0 } },
];
function presetValues(p, cur) {
  const v = { ...defaults(), ...p.m };
  applyAllMacros(v);
  Object.assign(v, p.v || {});
  for (const k of DEVICE_KEYS) v[k] = cur[k];
  return v;
}
const SOUND_KEYS = Object.keys(SCHEMA).filter(k => !DEVICE_KEYS.includes(k) && !k.startsWith('ui.'));
const sameSound = (a, b) => SOUND_KEYS.every(k => Math.abs(+a[k] - +b[k]) < 1e-3);

// ------------------------------------------------------------------ backend (alebo mock v prehliadači)
const mockState = { registered: true, payload_ok: true, version: '0.4.0', data_dir: 'C:\\ProgramData\\HearMeOut',
  endpoints: [{ id: '{57f146ab-a401-4aac-b5dc-cf18538cb86d}', name: 'Analogue 1 + 2', adapter: 'Focusrite USB Audio', installed: true },
              { id: '{0a1b}', name: 'Mikrofón', adapter: 'SteelSeries Arctis Nova 7', installed: false }] };
let mockPresets = [];
let mockChain = [{ uid: 'a1', enabled: true, path: 'C:\\Program Files\\Common Files\\VST3\\Speachy.vst3' }, { uid: 'b2', enabled: true, path: 'C:\\Program Files\\Common Files\\VST3\\Nuro Audio\\Xvox Pro.vst3' }];
const invoke = IS_APP ? TAURI.core.invoke : async (cmd, a) => {
  await new Promise(r => setTimeout(r, 60));
  if (cmd === 'get_state') return mockState;
  if (cmd === 'load_config') return {};
  if (cmd === 'list_presets') return mockPresets;
  if (cmd === 'save_preset') { mockPresets = mockPresets.filter(p => p.name !== a.name).concat([{ name: a.name, values: a.values }]); return; }
  if (cmd === 'delete_preset') { mockPresets = mockPresets.filter(p => p.name !== a.name); return; }
  if (cmd === 'vst_chain_get') return mockChain;
  if (cmd === 'vst_chain_set') { mockChain = a.slots; return; }
  if (cmd === 'host_status') return { running: true, bridge: true, rate: 48000, underruns: 0, mem_mb: 212, slots: mockChain.map((c, i) => ({ uid: c.uid, name: c.path.split('\\').pop().replace('.vst3', ''), vendor: ['Neverdie Audio', 'Nuro Audio'][i % 2], ok: true, enabled: c.enabled })) };
  if (cmd === 'vst_scan') return [{ name: 'Speachy', path: 'C:\\Program Files\\Common Files\\VST3\\Speachy.vst3', folder: 'VST3' }, { name: 'Xvox Pro', path: 'C:\\Program Files\\Common Files\\VST3\\Nuro Audio\\Xvox Pro.vst3', folder: 'Nuro Audio' }, { name: 'FabFilter Pro-Q 3', path: 'C:\\x\\FabFilter Pro-Q 3.vst3', folder: 'FabFilter' }];
  if (cmd === 'update_check') return { current: '0.4.0', latest: '0.4.0', available: false, notes: '', url: '', page: '' };
  if (cmd === 'monitor_start') return 'Analogue 1 + 2 (Focusrite USB Audio) → SteelSeries Sonar - Gaming';
  if (cmd === 'install_on' || cmd === 'uninstall_all') { await new Promise(r => setTimeout(r, 1500)); return { ok: true, code: 0, log: '' }; }
  return null;
};

// ------------------------------------------------------------------ stav
const S = { values: defaults(), app: null, presets: [], view: 'mic', selDev: null, monitoring: false,
  meter: { connected: false, active: false, in_db: -120, out_db: -120, gate_db: 0, comp_gr_db: 0, deess_db: 0, sample_rate: 0, latency_ms: 0 } };
let saveTimer = 0;
const bound = [];   // funkcie na prekreslenie ovládačov po zmene hodnôt
const repaint = () => bound.forEach(f => f());
function setVal(k, v, { save = true } = {}) {
  S.values[k] = v;
  if (save) scheduleSave();
}
function scheduleSave() { clearTimeout(saveTimer); saveTimer = setTimeout(saveNow, 90); }
async function saveNow() {
  try { await invoke('save_config', { values: toIni(S.values) }); }
  catch (e) { toast(String(e), true); }
  updatePresetName();
}
const endpointId = () => (S.app?.endpoints.find(e => e.installed) || {}).id;

// ------------------------------------------------------------------ navigácia
const VIEWS = [['mic', 'Mikrofón', 'mic'], ['presets', 'Presety', 'presets'], ['board', 'Soundboard', 'board'], ['settings', 'Nastavenia', 'gear']];
function buildNav() {
  const nav = $('#nav');
  for (const [id, label, ic] of VIEWS)
    nav.append(h('button', { 'data-v': id, onclick: () => go(id) }, icon(ic), label, id === 'board' ? h('span', { class: 'badge' }, 'Čoskoro') : null));
}
function go(id) {
  S.view = id;
  for (const [v] of VIEWS) $('#v-' + v).hidden = v !== id;
  const btns = [...document.querySelectorAll('#nav button')];
  btns.forEach(b => b.classList.toggle('on', b.dataset.v === id));
  const b = btns.find(b => b.dataset.v === id);
  $('#navInd').style.transform = `translateY(${b.offsetTop}px)`;
  $('#main').scrollTop = 0;
  if (id === 'presets') renderPresets();
  if (id === 'settings') renderSettings();
}

// ------------------------------------------------------------------ ovládacie prvky
const fmt = (v, unit) => {
  const n = +v, s = Number.isInteger(n) ? String(n) : n.toFixed(Math.abs(n) < 10 ? 1 : 0);
  if (unit === ':1') return s + ':1';
  if (unit === '%') return Math.round(n) + ' %';
  if (unit === 'Hz' && n >= 1000) return (n / 1000).toFixed(n % 1000 ? 1 : 0) + ' kHz';
  return unit ? `${n > 0 && unit === 'dB' ? '+' : ''}${s} ${unit}` : s;
};
// klasický riadok: popis | posuvník | hodnota
function slider(c) {
  const inp = h('input', { type: 'range', min: c.min, max: c.max, step: c.step, 'aria-label': c.label });
  const val = h('span', { class: 'v' });
  const mark = c.mark ? h('i', { class: 'mark', id: c.mark }) : null;
  const paint = () => {
    const v = S.values[c.key];
    inp.value = v;
    inp.style.setProperty('--p', ((v - c.min) / (c.max - c.min)) * 100 + '%');
    val.textContent = fmt(v, c.unit);
  };
  inp.addEventListener('input', () => { setVal(c.key, +inp.value); if (c.after) c.after(); paint(); });
  bound.push(paint); paint();
  return h('div', { class: 'field' }, h('label', {}, c.label), h('div', { class: 'rng' }, inp, mark), val);
}
// veľký hlavný posuvník (makro)
function macro(c) {
  const inp = h('input', { type: 'range', min: 0, max: 100, step: 1, 'aria-label': c.label, class: 'big' });
  const val = h('b', { class: 'mv' });
  const paint = () => {
    const v = S.values[c.key];
    inp.value = v; inp.style.setProperty('--p', v + '%');
    val.textContent = c.word ? c.word(v) : v + ' %';
  };
  inp.addEventListener('input', () => { S.values[c.key] = +inp.value; c.apply(S.values); scheduleSave(); repaint(); });
  bound.push(paint); paint();
  return h('div', { class: 'macro' },
    h('div', { class: 'mh2' }, h('span', {}, c.label), val),
    h('div', { class: 'rng' }, inp),
    c.hint ? h('div', { class: 'mhint' }, h('span', {}, c.hint[0]), h('span', {}, c.hint[1])) : null);
}
const amountWord = v => v === 0 ? 'vypnuté' : v < 25 ? 'jemne' : v < 50 ? 'stredne' : v < 75 ? 'výrazne' : 'naplno';
function segment(opts, get, set, cls = '') {
  const wrap = h('div', { class: 'seg ' + cls }, h('i', { class: 'knob' }));
  const btns = opts.map(([v, l, sub]) => h('button', { onclick: () => { set(v); paint(); } }, l, sub ? h('small', {}, sub) : null));
  wrap.append(...btns);
  const paint = () => {
    const cur = get();
    btns.forEach((b, i) => b.classList.toggle('on', opts[i][0] === cur));
    requestAnimationFrame(() => {
      const on = btns.find(b => b.classList.contains('on'));
      const k = wrap.querySelector('.knob');
      k.style.opacity = on ? 1 : 0;
      if (on) { k.style.width = on.offsetWidth + 'px'; k.style.transform = `translateX(${on.offsetLeft - 2}px)`; }
    });
  };
  bound.push(paint); paint();
  return wrap;
}
function toggle(keys, onChange, label = 'Zapnúť') {
  keys = [].concat(keys);
  const t = h('button', { class: 'sw', role: 'switch', 'aria-label': label });
  const paint = () => { const on = !!S.values[keys[0]]; t.classList.toggle('on', on); t.setAttribute('aria-checked', on); onChange && onChange(on); };
  t.addEventListener('click', () => { const nv = S.values[keys[0]] ? 0 : 1; keys.forEach(k => S.values[k] = nv); MACRO.color(S.values); scheduleSave(); repaint(); });
  bound.push(paint); paint();
  return t;
}
function details(controls) {
  const adv = h('div', { class: 'adv' }, ...controls);
  const more = h('button', { class: 'more' }, icon('chev'), 'Podrobnosti');
  more.addEventListener('click', () => { more.classList.toggle('open'); adv.classList.toggle('open'); });
  return [more, adv];
}
function card(m, i, ...body) {
  const c = h('div', { class: 'card mod land' + (m.wide ? ' wide' : ''), style: `--i:${i}` });
  c.append(h('div', { class: 'mh' }, h('div', { class: 'ic' }, icon(m.icon)),
    h('div', {}, h('h3', {}, m.title, m.extra || null), h('div', { class: 'd' }, m.desc)),
    m.tog ? toggle(m.tog, on => c.classList.toggle('off', !on)) : null));
  c.append(h('div', { class: 'ctl' }, ...body));
  return c;
}

// ------------------------------------------------------------------ krivka tónu (rovnaké vzorce ako C++ jadro)
function biquadMag(type, fs, f, q, g, w) {
  const A = 10 ** (g / 40), w0 = 2 * Math.PI * clamp(f, 10, .45 * fs) / fs, cs = Math.cos(w0), sn = Math.sin(w0);
  let b0, b1, b2, a0, a1, a2;
  if (type === 'hp') { const al = sn / (2 * q); [b0, b1, b2, a0, a1, a2] = [(1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al]; }
  else if (type === 'peak') { const al = sn / (2 * q); [b0, b1, b2, a0, a1, a2] = [1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A]; }
  else {
    const al = sn / 2 * Math.SQRT2, sA = 2 * Math.sqrt(A) * al;
    [b0, b1, b2, a0, a1, a2] = type === 'ls'
      ? [A * ((A + 1) - (A - 1) * cs + sA), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sA), (A + 1) + (A - 1) * cs + sA, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sA]
      : [A * ((A + 1) + (A - 1) * cs + sA), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sA), (A + 1) - (A - 1) * cs + sA, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sA];
  }
  const c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
  const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
  return Math.sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}
function toneCurve() {
  const fs = 48000, v = S.values;
  const svg = h('svg', { class: 'curve' });
  let W = 360, H = 150;
  const x = f => Math.log10(f / 20) / Math.log10(1000) * W;
  const y = db => H / 2 - db / 12 * (H / 2 - 8);
  const paint = () => {
    if (!svg.isConnected) return;
    const r = svg.getBoundingClientRect();
    if (r.width > 10) { W = r.width; H = r.height; }
    svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
    let d = '';
    const N = Math.max(60, Math.round(W / 3));
    for (let i = 0; i <= N; i++) {
      const f = 20 * 1000 ** (i / N), w = 2 * Math.PI * f / fs;
      let m = 1;
      if (v['highpass.enabled']) m *= biquadMag('hp', fs, v['highpass.freq'], .5412, 0, w) * biquadMag('hp', fs, v['highpass.freq'], 1.3066, 0, w);
      if (v['eq.enabled']) m *= biquadMag('ls', fs, v['eq.low_freq'], 0, v['eq.low_gain_db'], w) * biquadMag('peak', fs, v['eq.mud_freq'], v['eq.mud_q'], v['eq.mud_gain_db'], w)
        * biquadMag('peak', fs, v['eq.presence_freq'], v['eq.presence_q'], v['eq.presence_gain_db'], w) * biquadMag('hs', fs, v['eq.air_freq'], 0, v['eq.air_gain_db'], w);
      d += (i ? 'L' : 'M') + x(f).toFixed(1) + ' ' + clamp(y(20 * Math.log10(m)), 1, H - 1).toFixed(1);
    }
    const grid = [100, 1000, 10000].map(f => `<line x1="${x(f)}" x2="${x(f)}" y1="0" y2="${H}" class="g"/><text x="${x(f) + 4}" y="${H - 6}">${f >= 1000 ? f / 1000 + 'k' : f}</text>`).join('')
      + [6, -6].map(db => `<line x1="0" x2="${W}" y1="${y(db)}" y2="${y(db)}" class="g"/><text x="4" y="${y(db) - 3}">${db > 0 ? '+' : ''}${db}</text>`).join('')
      + `<line x1="0" x2="${W}" y1="${y(0)}" y2="${y(0)}" class="g0"/>`;
    svg.innerHTML = grid + `<path class="fill" d="${d}L${W} ${y(0)}L0 ${y(0)}Z"/><path class="line" d="${d}"/>`;
  };
  new ResizeObserver(() => paint()).observe(svg);
  bound.push(paint); requestAnimationFrame(paint);
  return svg;
}

// ------------------------------------------------------------------ gombík (SVG, ťahanie, koliesko, dvojklik = predvolené)
function knob(c) {
  // c: { key, min, max, step, size, label, fmt(v), def, onChange(v) }
  const size = c.size || 84, r = size / 2 - 7, cx = size / 2, cy = size / 2;
  const A0 = 135, SWEEP = 270;
  const pt = a => [cx + r * Math.cos(a * Math.PI / 180), cy + r * Math.sin(a * Math.PI / 180)];
  const arc = (a1, a2) => { const [x1, y1] = pt(a1), [x2, y2] = pt(a2); return `M${x1.toFixed(2)} ${y1.toFixed(2)}A${r} ${r} 0 ${a2 - a1 > 180 ? 1 : 0} 1 ${x2.toFixed(2)} ${y2.toFixed(2)}`; };
  const svg = h('svg', { class: 'knob-svg', viewBox: `0 0 ${size} ${size}`, width: size, height: size });
  const wrap = h('div', { class: 'knob' + (c.small ? ' small' : ''), tabindex: 0, role: 'slider', 'aria-label': c.label, title: 'Ťahaj hore/dole · Shift = jemne · dvojklik = predvolené' }, svg);
  const val = h('div', { class: 'kval' });
  const lbl = h('div', { class: 'klbl' }, c.label);
  const span = c.max - c.min;
  const center = c.min < 0 && c.max > 0;   // bipolárny (napr. -12…+12)
  const paint = () => {
    const v = S.values[c.key], t = clamp((v - c.min) / span, 0, 1), a = A0 + t * SWEEP;
    const a0 = center ? A0 + ((0 - c.min) / span) * SWEEP : A0;
    const [px, py] = pt(a), [ix, iy] = [cx + (r - 13) * Math.cos(a * Math.PI / 180), cy + (r - 13) * Math.sin(a * Math.PI / 180)];
    svg.innerHTML = `<circle class="kbody" cx="${cx}" cy="${cy}" r="${r - 7}"/>`
      + `<path class="ktrack" d="${arc(A0, A0 + SWEEP)}"/>`
      + (Math.abs(a - a0) > .5 ? `<path class="kfill" d="${arc(Math.min(a, a0), Math.max(a, a0))}"/>` : '')
      + `<line class="kptr" x1="${ix.toFixed(2)}" y1="${iy.toFixed(2)}" x2="${(cx + (ix - cx) * .45).toFixed(2)}" y2="${(cy + (iy - cy) * .45).toFixed(2)}"/>`
      + `<circle class="kdot" cx="${px.toFixed(2)}" cy="${py.toFixed(2)}" r="${c.small ? 2.6 : 3.2}"/>`;
    val.textContent = c.fmt ? c.fmt(v) : v;
    wrap.setAttribute('aria-valuenow', v);
  };
  const set = v => {
    v = clamp(Math.round(v / (c.step || 1)) * (c.step || 1), c.min, c.max);
    v = +v.toFixed(3);
    if (v === S.values[c.key]) return;
    S.values[c.key] = v;
    c.onChange ? c.onChange(v) : scheduleSave();
    repaint();
  };
  let y0 = 0, v0 = 0;
  wrap.addEventListener('pointerdown', e => {
    if (e.button !== 0) return;
    wrap.setPointerCapture(e.pointerId); y0 = e.clientY; v0 = S.values[c.key]; wrap.classList.add('drag');
  });
  wrap.addEventListener('pointermove', e => {
    if (!wrap.hasPointerCapture(e.pointerId)) return;
    const d = (y0 - e.clientY) / (e.shiftKey ? 900 : 180) * span;
    set(v0 + d);
  });
  const end = e => { if (wrap.hasPointerCapture(e.pointerId)) wrap.releasePointerCapture(e.pointerId); wrap.classList.remove('drag'); };
  wrap.addEventListener('pointerup', end); wrap.addEventListener('pointercancel', end);
  wrap.addEventListener('dblclick', () => set(c.def ?? SCHEMA[c.key]?.[0] ?? c.min));
  wrap.addEventListener('wheel', e => { e.preventDefault(); set(S.values[c.key] - Math.sign(e.deltaY) * (c.step || 1) * (e.shiftKey ? 1 : (span > 50 ? 2 : 1))); }, { passive: false });
  wrap.addEventListener('keydown', e => {
    const st = (c.step || 1) * (e.shiftKey ? 5 : 1);
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { set(S.values[c.key] + st); e.preventDefault(); }
    if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { set(S.values[c.key] - st); e.preventDefault(); }
  });
  bound.push(paint); paint();
  return h('div', { class: 'kwrap' + (c.small ? ' small' : '') }, wrap, val, lbl);
}

// zvislý posuvník (tón)
function vslider(c) {
  const inp = h('input', { type: 'range', min: c.min, max: c.max, step: c.step, class: 'vs', 'aria-label': c.label, orient: 'vertical' });
  const val = h('div', { class: 'vsval' });
  const paint = () => {
    const v = S.values[c.key]; inp.value = v;
    const t = (v - c.min) / (c.max - c.min), z = (0 - c.min) / (c.max - c.min);
    inp.style.setProperty('--a', Math.min(t, z) * 100 + '%'); inp.style.setProperty('--b', Math.max(t, z) * 100 + '%');
    val.textContent = (v > 0 ? '+' : '') + (+v).toFixed(1);
  };
  inp.addEventListener('input', () => { S.values[c.key] = +inp.value; c.after && c.after(); scheduleSave(); repaint(); });
  inp.addEventListener('dblclick', () => { S.values[c.key] = 0; c.after && c.after(); scheduleSave(); repaint(); });
  bound.push(paint); paint();
  return h('div', { class: 'vscol' }, val, h('div', { class: 'vsbox' }, inp), h('div', { class: 'vslbl' }, c.label));
}

// malý výber charakteru (‹ Úderná ›)
function charPicker(list, key, onSet) {
  const name = h('span', { class: 'cpname' });
  const sub = h('span', { class: 'cpsub' });
  const paint = () => { const it = list[S.values[key]]; name.textContent = it ? it.name : 'Vlastný'; sub.textContent = it?.sub || ''; };
  const step = d => { const n = list.length, i = S.values[key] < 0 ? 0 : (S.values[key] + d + n) % n; S.values[key] = i; onSet(i); scheduleSave(); repaint(); };
  bound.push(paint); paint();
  return h('div', { class: 'cpick' }, h('button', { class: 'cparrow', onclick: () => step(-1), 'aria-label': 'Predchádzajúci' }, icon('left')),
    h('div', { class: 'cpmid' }, name, sub), h('button', { class: 'cparrow', onclick: () => step(1), 'aria-label': 'Ďalší' }, icon('right')));
}

// zvislý meter (vstup/výstup)
function vmeter(id, label) {
  const ticks = [0, -6, -12, -24, -36, -48, -60];
  return h('div', { class: 'vmeter' },
    h('div', { class: 'vmtop' }, h('b', { id: id + 'Val' }, '–∞'), h('span', {}, 'dB')),
    h('div', { class: 'vmbody' },
      h('div', { class: 'vmbar' }, h('i', { class: 'vmfill', id: id + 'Fill' }), h('i', { class: 'vmpeak', id: id + 'Peak' })),
      h('div', { class: 'vmscale' }, ...ticks.map(t => h('span', { style: `top:${(-t / 60) * 100}%` }, t)))),
    h('div', { class: 'vmlbl' }, label));
}

// ------------------------------------------------------------------ pohľad: Mikrofón (pult)
const MODS = {
  clean: { title: 'Čistenie', icon: 'clean', tog: ['denoise.enabled', 'gate.enabled'] },
  dyn: { title: 'Dynamika', icon: 'dyn', tog: ['compressor.enabled'] },
  tone: { title: 'Tón', icon: 'tone', tog: ['eq.enabled'] },
  sib: { title: 'Sykavky', icon: 'sib', tog: ['deesser.enabled'] },
  out: { title: 'Výstup', icon: 'out' },
};
let openDrawer = null;
function moduleShell(id, extraClass, ...body) {
  const m = MODS[id];
  const el = h('section', { class: 'mod2 ' + (extraClass || ''), 'data-mod': id });
  const more = h('button', { class: 'mbtn' + (openDrawer === id ? ' on' : ''), title: 'Podrobnosti', onclick: () => { openDrawer = openDrawer === id ? null : id; renderDrawer(); document.querySelectorAll('.mbtn').forEach(b => b.classList.toggle('on', b.closest('.mod2')?.dataset.mod === openDrawer)); } }, icon('sliders'));
  el.append(h('header', {}, m.tog ? toggle(m.tog, on => el.classList.toggle('off', !on), m.title) : h('span', { class: 'swspace' }),
    h('h3', {}, m.title), more), h('div', { class: 'mbody' }, ...body));
  return el;
}
const pct = v => Math.round(v) + ' %';

function renderMic() {
  const v = $('#v-mic'); v.innerHTML = ''; bound.length = 0;
  const V = S.values;
  const vst = V['global.engine'] === 1;

  // ---- lišta
  const master = h('div', { class: 'abtoggle', title: 'Porovnanie s efektmi a bez nich (klávesa B)' }, h('span', { id: 'masterLbl' }), (() => {
    const t = h('button', { class: 'sw', role: 'switch', 'aria-label': 'Efekty zapnuté' });
    const paint = () => { const on = !V['global.bypass']; t.classList.toggle('on', on); $('#masterLbl') && ($('#masterLbl').textContent = on ? 'Efekty' : 'Bez efektov'); updateStatus(); };
    t.addEventListener('click', () => toggleBypass());
    bound.push(paint); setTimeout(paint);
    return t;
  })());
  const monBtn = h('button', { class: 'tbtn mon', id: 'monBtn', onclick: toggleMonitor, title: 'Počuť sa v slúchadlách aj s efektmi' }, icon('phones'), h('span', {}, 'Odposluch'));
  const monVol = h('input', { type: 'range', min: 0, max: 150, step: 1, class: 'monvol', id: 'monVol', 'aria-label': 'Hlasitosť odposluchu' });
  let mv = 80; try { mv = +(localStorage.getItem('hmo.monvol') || 80); } catch { }
  monVol.value = mv; monVol.style.setProperty('--p', mv / 1.5 + '%');
  monVol.addEventListener('input', () => { monVol.style.setProperty('--p', monVol.value / 1.5 + '%'); invoke('monitor_volume', { v: monVol.value / 100 }); try { localStorage.setItem('hmo.monvol', monVol.value); } catch { } });
  const engine = segment([[0, 'Vlastné efekty'], [1, 'VST pluginy']], () => V['global.engine'], e => setEngine(e), 'engine');

  v.append(h('div', { class: 'toolbar' },
    !vst ? presetSwitcher() : h('div', { class: 'pswitch vstlbl' }, icon('plug'), h('span', {}, 'Reťazec pluginov')), engine, h('div', { class: 'tspacer' }),
    h('div', { class: 'monwrap' + (S.monitoring ? ' on' : ''), id: 'monWrap' }, monBtn, monVol), master));

  if (S.app && !S.app.registered) {
    v.append(h('div', { class: 'card banner land' }, h('div', { class: 'ic' }, icon('sparkle')),
      h('div', {}, h('b', {}, 'Ešte treba jeden krok'), h('p', {}, 'Nainštaluj efekt na svoj mikrofón – potom sa tu všetko rozbehne.')),
      h('button', { class: 'btn primary', onclick: () => go('settings') }, 'Nastaviť mikrofón')));
  }

  // ---- pult
  const mods = vst ? vstSlotsView() : builtinModules();
  v.append(h('div', { class: 'rack land' },
    vmeter('in', 'Vstup'),
    h('div', { class: 'rackmid' }, mods,
      h('div', { class: 'idlebox', id: 'idleBox' }, icon('mic'), h('span', { id: 'idleText' }), h('button', { class: 'btn small', onclick: toggleMonitor }, icon('phones'), 'Zapnúť odposluch'))),
    vmeter('out', 'Výstup')));
  v.append(h('div', { id: 'drawer' }));
  renderDrawer();
  paintMonitorState();
  if (vst) refreshHost();
}

function builtinModules() {
  const V = S.values;
  const grid = h('div', { class: 'mods' });
  grid.append(moduleShell('clean', '',
    knob({ key: 'ui.clean', min: 0, max: 100, step: 1, size: 96, label: 'Sila', fmt: pct, onChange: () => { MACRO.clean(V); scheduleSave(); } }),
    h('div', { class: 'mrow2' },
      h('div', { class: 'led-row' }, h('i', { class: 'led', id: 'gateLed' }), h('span', { id: 'gateTxt' }, 'Ticho')),
      h('button', { class: 'iconbtn', title: 'Nastaviť prah podľa ticha v izbe', onclick: calibrateGate }, icon('wand'))),
    h('div', { class: 'mfoot' }, h('span', {}, 'Tlmiť v tichu'), toggle('gate.enabled'))));

  grid.append(moduleShell('dyn', '',
    charPicker(DYN_CHARS, 'ui.dyn_char', () => MACRO.dyn(V)),
    knob({ key: 'ui.dyn_amount', min: 0, max: 100, step: 1, size: 96, label: 'Množstvo', fmt: pct, onChange: () => { MACRO.dyn(V); scheduleSave(); } }),
    h('div', { class: 'mrow2' },
      knob({ key: 'ui.color', min: 0, max: 100, step: 1, size: 54, small: true, label: 'Farba', fmt: pct, onChange: () => { MACRO.color(V); scheduleSave(); } }),
      h('div', { class: 'grmini' }, h('div', { class: 'grlbl' }, 'Stlačenie'), h('div', { class: 'grbar2' }, h('i', { id: 'grFill' })), h('b', { id: 'grVal' }, '0.0')))));

  const custom = () => { V['ui.tone_char'] = -1; };
  grid.append(moduleShell('tone', 'wide',
    charPicker(TONE_CHARS, 'ui.tone_char', () => MACRO.tone(V)),
    h('div', { class: 'tonebox' }, toneCurve(), h('div', { class: 'vsrow' },
      vslider({ key: 'eq.low_gain_db', label: 'Telo', min: -6, max: 8, step: .5, after: custom }),
      vslider({ key: 'eq.mud_gain_db', label: 'Krabica', min: -10, max: 4, step: .5, after: custom }),
      vslider({ key: 'eq.presence_gain_db', label: 'Jas', min: -4, max: 8, step: .5, after: custom }),
      vslider({ key: 'eq.air_gain_db', label: 'Vzduch', min: -4, max: 8, step: .5, after: custom })))));

  grid.append(moduleShell('sib', '',
    knob({ key: 'ui.sib', min: 0, max: 100, step: 1, size: 96, label: 'Zjemnenie', fmt: pct, onChange: () => { MACRO.sib(V); scheduleSave(); } }),
    h('div', { class: 'grmini' }, h('div', { class: 'grlbl' }, 'Stlmenie s/š'), h('div', { class: 'grbar2' }, h('i', { id: 'dsFill' })), h('b', { id: 'dsVal' }, '0.0'))));

  grid.append(moduleShell('out', 'narrow',
    knob({ key: 'global.output_gain_db', min: -12, max: 12, step: .5, size: 72, label: 'Hlasitosť', fmt: v => (v > 0 ? '+' : '') + (+v).toFixed(1) + ' dB', def: 0 }),
    h('div', { class: 'mfoot' }, h('span', {}, 'Ochrana'), toggle('limiter.enabled'))));
  return grid;
}

// panel podrobností pod pultom
function renderDrawer() {
  const d = $('#drawer'); if (!d) return;
  if (d._own) { for (const f of d._own) { const i = bound.indexOf(f); if (i >= 0) bound.splice(i, 1); } d._own = null; }
  const mark = bound.length;
  d.innerHTML = '';
  if (!openDrawer || S.values['global.engine'] === 1) { d.className = ''; return; }
  const V = S.values;
  const sets = {
    clean: [
      slider({ key: 'gate.threshold_db', label: 'Prah ticha', min: -80, max: -20, step: 1, unit: 'dB', mark: 'gateMark' }),
      slider({ key: 'denoise.reduction_db', label: 'Potlačenie šumu', min: 0, max: 40, step: 1, unit: 'dB' }),
      slider({ key: 'denoise.strength', label: 'Agresivita', min: 1, max: 3, step: .1 }),
      slider({ key: 'gate.range_db', label: 'Stlmenie v tichu', min: 0, max: 40, step: 1, unit: 'dB' }),
      slider({ key: 'gate.hold_ms', label: 'Podržanie', min: 0, max: 300, step: 5, unit: 'ms' }),
      slider({ key: 'gate.release_ms', label: 'Doznievanie', min: 20, max: 500, step: 5, unit: 'ms' }),
      h('div', { class: 'field sw-f' }, h('label', {}, 'Odstrániť dunenie'), h('div', {}, toggle('highpass.enabled')), h('span')),
      slider({ key: 'highpass.freq', label: 'Dunenie pod', min: 40, max: 200, step: 5, unit: 'Hz' })],
    dyn: [
      slider({ key: 'compressor.threshold_db', label: 'Prah', min: -40, max: 0, step: 1, unit: 'dB' }),
      slider({ key: 'compressor.ratio', label: 'Pomer', min: 1, max: 10, step: .1, unit: ':1' }),
      slider({ key: 'compressor.attack_ms', label: 'Nábeh', min: .5, max: 50, step: .5, unit: 'ms' }),
      slider({ key: 'compressor.release_ms', label: 'Uvoľnenie', min: 20, max: 500, step: 5, unit: 'ms' }),
      slider({ key: 'compressor.knee_db', label: 'Mäkkosť', min: 0, max: 12, step: .5, unit: 'dB' }),
      slider({ key: 'compressor.makeup_db', label: 'Korekcia hlasitosti', min: -12, max: 12, step: .5, unit: 'dB' }),
      slider({ key: 'saturation.drive', label: 'Farba – drive', min: 0, max: 100, step: 1, unit: '%', after: () => { V['ui.color'] = V['saturation.drive']; MACRO.color(V); } })],
    tone: [
      slider({ key: 'eq.low_freq', label: 'Telo – frekv.', min: 60, max: 250, step: 5, unit: 'Hz', after: () => V['ui.tone_char'] = -1 }),
      slider({ key: 'eq.mud_freq', label: 'Krabica – frekv.', min: 150, max: 800, step: 10, unit: 'Hz', after: () => V['ui.tone_char'] = -1 }),
      slider({ key: 'eq.mud_q', label: 'Krabica – šírka', min: .3, max: 3, step: .1, unit: 'Q', after: () => V['ui.tone_char'] = -1 }),
      slider({ key: 'eq.presence_freq', label: 'Jas – frekv.', min: 1500, max: 6000, step: 50, unit: 'Hz', after: () => V['ui.tone_char'] = -1 }),
      slider({ key: 'eq.presence_q', label: 'Jas – šírka', min: .3, max: 3, step: .1, unit: 'Q', after: () => V['ui.tone_char'] = -1 }),
      slider({ key: 'eq.air_freq', label: 'Vzduch – frekv.', min: 6000, max: 16000, step: 250, unit: 'Hz', after: () => V['ui.tone_char'] = -1 })],
    sib: [
      slider({ key: 'deesser.freq', label: 'Od frekvencie', min: 3000, max: 10000, step: 100, unit: 'Hz' }),
      slider({ key: 'deesser.threshold_db', label: 'Prah', min: -50, max: -10, step: 1, unit: 'dB' }),
      slider({ key: 'deesser.range_db', label: 'Max. stlmenie', min: 0, max: 18, step: 1, unit: 'dB' })],
    out: [
      slider({ key: 'limiter.ceiling_db', label: 'Strop limitera', min: -12, max: 0, step: .5, unit: 'dB' }),
      slider({ key: 'limiter.release_ms', label: 'Uvoľnenie', min: 10, max: 300, step: 5, unit: 'ms' })],
  };
  d.className = 'drawer land';
  d.append(h('div', { class: 'dhead' }, h('div', { class: 'ic' }, icon(MODS[openDrawer].icon)), h('h3', {}, MODS[openDrawer].title, h('span', {}, ' · podrobnosti')),
    h('button', { class: 'iconbtn', title: 'Zavrieť', onclick: () => { openDrawer = null; renderDrawer(); document.querySelectorAll('.mbtn').forEach(b => b.classList.remove('on')); } }, icon('x'))),
    h('div', { class: 'dgrid' }, ...sets[openDrawer]));
  d._own = bound.slice(mark);
}

// ------------------------------------------------------------------ motor: vlastné efekty / VST
async function setEngine(e) {
  S.values['global.engine'] = e;
  saveNow();
  try {
    if (e === 1) { await invoke('host_start'); await invoke('host_autostart', { on: true }); }
    else { await invoke('host_stop'); await invoke('host_autostart', { on: false }); }
  } catch (err) { toast(String(err), true); }
  renderMic();
}

let hostState = { running: false, slots: [] }, chain = [];
async function refreshHost() {
  try { chain = await invoke('vst_chain_get') || []; } catch { chain = []; }
  try { hostState = await invoke('host_status') || {}; } catch { hostState = {}; }
  paintSlots();
}
function slotInfo(uid) { return (hostState.slots || []).find(s => s.uid === uid) || {}; }
function vstSlotsView() {
  return h('div', { class: 'vstwrap' }, h('div', { class: 'hostbar', id: 'hostBar' }), h('div', { class: 'slots', id: 'slots' }), h('div', { class: 'vsttip', id: 'vstTip' }));
}
function paintSlots() {
  const bar = $('#hostBar'), box = $('#slots'); if (!bar || !box) return;
  const hs = hostState;
  bar.innerHTML = '';
  const st = !hs.running ? ['rec', 'Host nebeží', h('button', { class: 'btn small', onclick: async () => { try { await invoke('host_start'); } catch (e) { toast(String(e), true); } setTimeout(refreshHost, 600); } }, 'Spustiť')]
    : !hs.bridge ? ['amber', 'Host beží – čaká na mikrofón (spusti odposluch alebo Discord)']
    : ['mint', `Host beží · ${(hs.rate / 1000 || 0).toFixed(1)} kHz${hs.mem_mb ? ` · RAM ${hs.mem_mb} MB` : ''}${hs.underruns ? ` · výpadky ${hs.underruns}` : ''}`];
  bar.append(h('span', { class: 'dot ' + st[0] }), h('span', {}, st[1]));
  if (st[2]) bar.append(st[2]);
  box.innerHTML = '';
  chain.forEach((c, i) => {
    const info = slotInfo(c.uid);
    const name = info.name || c.path.split(/[\\/]/).pop().replace(/\.vst3$/i, '');
    const sw = h('button', { class: 'sw' + (c.enabled ? ' on' : ''), role: 'switch', 'aria-label': 'Zapnúť plugin',
      onclick: () => { c.enabled = !c.enabled; saveChain(); } });
    box.append(h('div', { class: 'slot' + (c.enabled ? '' : ' off') + (info.ok === false ? ' err' : '') },
      h('div', { class: 'snum' }, i + 1),
      h('div', { class: 'sinfo' }, h('b', {}, name), h('small', {}, info.ok === false ? (info.error || 'Chyba') : (info.vendor || (hs.running ? 'načítavam…' : '')))),
      h('div', { class: 'sact' },
        h('button', { class: 'iconbtn', title: 'Posunúť vľavo', disabled: i === 0, onclick: () => { [chain[i - 1], chain[i]] = [chain[i], chain[i - 1]]; saveChain(); } }, icon('left')),
        h('button', { class: 'iconbtn', title: 'Posunúť vpravo', disabled: i === chain.length - 1, onclick: () => { [chain[i + 1], chain[i]] = [chain[i], chain[i + 1]]; saveChain(); } }, icon('right')),
        h('button', { class: 'iconbtn', title: 'Odstrániť', onclick: () => { chain.splice(i, 1); saveChain(); } }, icon('x'))),
      h('button', { class: 'btn small sopen', onclick: async () => { try { await invoke('host_open_editor', { uid: c.uid }); } catch (e) { toast(String(e), true); } } }, icon('open'), 'Otvoriť okno'),
      sw));
  });
  box.append(h('button', { class: 'slot add', onclick: addPluginDialog }, icon('plus'), h('span', {}, 'Pridať plugin')));
  const tip = $('#vstTip');
  if (tip) tip.textContent = chain.length
    ? 'Zvuk ide cez pluginy zľava doprava. V okne pluginu si načítaj rovnaký preset ako v Light Hoste – nastavenie sa uloží samo.'
    : 'Pridaj pluginy v takom poradí, ako ich máš v Light Hoste (napr. Speachy → Xvox Pro).';
}
async function saveChain() {
  try { await invoke('vst_chain_set', { slots: chain }); } catch (e) { toast(String(e), true); }
  paintSlots(); setTimeout(refreshHost, 700);
}
async function addPluginDialog() {
  let list = [];
  try { list = await invoke('vst_scan') || []; } catch { }
  const q = h('input', { class: 'txt', placeholder: 'Hľadať plugin…' });
  const ul = h('div', { class: 'plist' });
  const draw = () => {
    ul.innerHTML = '';
    const f = q.value.trim().toLowerCase();
    const items = list.filter(p => !f || p.name.toLowerCase().includes(f) || p.folder.toLowerCase().includes(f));
    if (!items.length) ul.append(h('div', { class: 'note' }, list.length ? 'Nič sa nenašlo.' : 'Nenašiel som žiadne VST3 pluginy v štandardných priečinkoch.'));
    for (const p of items) ul.append(h('button', { class: 'pitem', onclick: () => { m.close(); chain.push({ uid: Math.random().toString(16).slice(2, 10), enabled: true, path: p.path }); saveChain(); } },
      icon('plug'), h('div', {}, h('b', {}, p.name), h('small', {}, p.folder))));
  };
  q.addEventListener('input', draw); draw();
  const m = modal(h('div', {}, h('h3', {}, 'Pridať VST3 plugin'), h('p', {}, 'Napríklad Speachy a Xvox – zvuk aj licencia zostanú ako v Light Hoste.'), q, ul), [['Zavrieť', null]]);
  setTimeout(() => q.focus(), 50);
}

// ------------------------------------------------------------------ prepínač presetov (‹ názov ›)
function allPresets() {
  return [...BUILTIN.map(p => ({ name: p.name, desc: p.desc, builtin: true, values: presetValues(p, S.values) })),
          ...S.presets.map(p => { const v = fromIni(p.values); for (const k of DEVICE_KEYS) v[k] = S.values[k]; return { name: p.name, desc: 'Tvoj preset', values: v }; })];
}
function currentPresetIndex(list) { return list.findIndex(p => sameSound(p.values, S.values)); }
function presetSwitcher(vstMode) {
  const name = h('button', { class: 'pname', id: 'presetName', onclick: e => presetMenu(e.currentTarget) });
  const step = d => { const list = allPresets(); let i = currentPresetIndex(list); i = i < 0 ? (d > 0 ? 0 : list.length - 1) : (i + d + list.length) % list.length; applyPreset(list[i]); };
  setTimeout(updatePresetName);
  return h('div', { class: 'pswitch' }, h('button', { class: 'parrow', 'aria-label': 'Predchádzajúci preset', onclick: () => step(-1) }, icon('left')),
    name, h('button', { class: 'parrow', 'aria-label': 'Ďalší preset', onclick: () => step(1) }, icon('right')));
}
function updatePresetName() {
  const el = $('#presetName'); if (!el) return;
  const list = allPresets(), i = currentPresetIndex(list);
  el.innerHTML = ''; el.append(h('span', {}, i >= 0 ? list[i].name : 'Vlastné nastavenie'), icon('down'));
  document.querySelectorAll('.preset').forEach(c => c.classList.toggle('cur', !!c._values && sameSound(c._values, S.values)));
}
function applyPreset(p) {
  S.values = { ...p.values };
  saveNow(); repaint();
  toast(`Preset „${p.name}“`);
}
function presetMenu(anchor) {
  const list = allPresets(), cur = currentPresetIndex(list);
  const r = anchor.getBoundingClientRect();
  const menu = h('div', { class: 'pop-menu', style: `left:${r.left}px;top:${r.bottom + 6}px` },
    ...list.map((p, i) => h('button', { class: i === cur ? 'on' : '', onclick: () => { close(); applyPreset(p); } },
      h('span', {}, p.name), p.builtin ? null : h('small', {}, 'tvoj'))),
    h('div', { class: 'sep' }), h('button', { onclick: () => { close(); savePresetDialog(); } }, icon('plus'), h('span', {}, 'Uložiť aktuálne…')));
  const scrim = h('div', { class: 'menuscrim', onclick: () => close() });
  const close = () => { menu.classList.add('out'); setTimeout(() => { menu.remove(); scrim.remove(); }, 150); };
  document.body.append(scrim, menu);
}

// ------------------------------------------------------------------ A/B a odposluch
function toggleBypass() {
  S.values['global.bypass'] = S.values['global.bypass'] ? 0 : 1;
  saveNow(); repaint(); updateStatus();
}
async function toggleMonitor() {
  if (S.monitoring) { await invoke('monitor_stop'); S.monitoring = false; paintMonitorState(); return; }
  const id = endpointId();
  if (!id) { toast('Najprv nainštaluj efekt na mikrofón (Nastavenia).', true); return; }
  let ok = false; try { ok = localStorage.getItem('hmo.phones') === '1'; } catch { }
  if (!ok) {
    const yes = await confirmBox('Máš nasadené slúchadlá?', 'Odposluch púšťa tvoj mikrofón do predvoleného výstupu. Cez reproduktory by vzniklo pískanie (spätná väzba).', 'Áno, mám', 'primary');
    if (!yes) return;
    try { localStorage.setItem('hmo.phones', '1'); } catch { }
  }
  try {
    const route = await invoke('monitor_start', { endpointId: id });
    await invoke('monitor_volume', { v: +($('#monVol')?.value || 80) / 100 });
    S.monitoring = true; paintMonitorState();
    toast('Odposluch: ' + route.split('→').pop().trim());
  } catch (e) { toast(String(e), true); }
}
function paintMonitorState() {
  $('#monWrap')?.classList.toggle('on', S.monitoring);
  const b = $('#monBtn span'); if (b) b.textContent = S.monitoring ? 'Počúvaš sa' : 'Odposluch';
}

// ------------------------------------------------------------------ meter
let peakHold = { in: -120, out: -120, tIn: 0, tOut: 0 }, meterQueued = false, calib = null;
function onMeter(m) {
  S.meter = m;
  if (IS_APP && m.monitoring !== S.monitoring) { S.monitoring = m.monitoring; paintMonitorState(); }
  if (calib) calib.samples.push(m.active ? m.in_db : null);
  if (!meterQueued) { meterQueued = true; requestAnimationFrame(paintMeter); }
}
function paintMeter() {
  meterQueued = false;
  const m = S.meter, now = performance.now();
  const lvl = db => clamp((db + 60) / 60, 0, 1);
  for (const [id, db] of [['in', m.in_db], ['out', m.out_db]]) {
    const fill = $('#' + id + 'Fill'); if (!fill) continue;
    fill.style.transform = `scaleY(${lvl(db)})`;
    fill.classList.toggle('hot', db > -6); fill.classList.toggle('clip', db > -1);
    const key = id === 'in' ? 'tIn' : 'tOut';
    if (db >= peakHold[id] || now - peakHold[key] > 1200) { peakHold[id] = db; peakHold[key] = now; }
    const pk = $('#' + id + 'Peak');
    pk.style.bottom = `calc(${lvl(peakHold[id]) * 100}% - 1px)`;
    pk.style.opacity = m.active && peakHold[id] > -60 ? .7 : 0;
    $('#' + id + 'Val').textContent = m.active && db > -90 ? db.toFixed(1) : '–∞';
  }
  const bp = S.values['global.bypass'], vst = m.engine_state === 1;
  const led = $('#gateLed');
  if (led) {
    const gating = m.active && !bp && S.values['gate.enabled'] && m.gate_db < -1;
    led.className = 'led ' + (!m.active || bp || !S.values['gate.enabled'] ? '' : gating ? 'amber' : 'mint');
    $('#gateTxt').textContent = !m.active ? 'Ticho' : bp || !S.values['gate.enabled'] ? 'Tlmenie vyp.' : gating ? `Tlmí ${m.gate_db.toFixed(0)} dB` : 'Hovoríš';
  }
  const gr = $('#grFill');
  if (gr) { gr.style.transform = `scaleX(${Math.min(1, -m.comp_gr_db / 15)})`; $('#grVal').textContent = m.active ? m.comp_gr_db.toFixed(1) : '0.0'; }
  const ds = $('#dsFill');
  if (ds) { ds.style.transform = `scaleX(${Math.min(1, -(m.deess_db || 0) / 10)})`; $('#dsVal').textContent = m.active ? (m.deess_db || 0).toFixed(1) : '0.0'; }
  const idle = $('#idleBox');
  if (idle) {
    idle.classList.toggle('gone', m.active);
    $('#idleText').textContent = !S.app?.registered ? 'Efekt ešte nie je nainštalovaný na mikrofón.' : 'Mikrofón práve nikto nepoužíva.';
  }
  const mark = $('#gateMark');
  if (mark) { mark.style.left = (clamp(m.in_db, -80, -20) + 80) / 60 * 100 + '%'; mark.classList.toggle('on', m.active && m.in_db > -90); }
  $('#logo').style.setProperty('--lvl', m.active ? lvl(m.in_db).toFixed(2) : 0);
  updateStatus();
}

// kalibrácia ticha: 3 s -> prah = šum (95. percentil) + 6 dB
function calibrateGate() {
  if (!S.meter.active) { toast('Najprv zapni odposluch alebo test v Discorde – potrebujem počuť mikrofón.', true); return; }
  const C = 2 * Math.PI * 24;
  const ring = h('svg', { class: 'ring', viewBox: '0 0 54 54', html: `<circle class="bg" cx="27" cy="27" r="24"/><circle class="fg" cx="27" cy="27" r="24" stroke-dasharray="${C}" stroke-dashoffset="${C}"/>` });
  const m = modal(h('div', {}, ring, h('h3', { style: 'text-align:center' }, 'Buď 3 sekundy ticho'),
    h('p', { style: 'text-align:center' }, 'Zmeriam, ako hlasno je v izbe, keď nerozprávaš.')), []);
  calib = { samples: [] };
  const t0 = performance.now();
  const tick = () => {
    const p = Math.min(1, (performance.now() - t0) / 3000);
    ring.querySelector('.fg').setAttribute('stroke-dashoffset', C * (1 - p));
    if (p < 1) return requestAnimationFrame(tick);
    const vals = calib.samples.filter(x => x != null && x > -120); calib = null; m.close();
    if (vals.length < 10) { toast('Meranie sa nepodarilo – mikrofón medzitým nebežal.', true); return; }
    vals.sort((a, b) => a - b);
    const noise = vals[Math.floor(vals.length * .95)];
    const thr = Math.round(clamp(noise + 6, -80, -25));
    setVal('gate.threshold_db', thr); repaint();
    toast(`Prah ticha: ${thr} dB (šum v izbe ${noise.toFixed(0)} dB)`);
  };
  requestAnimationFrame(tick);
}

// ------------------------------------------------------------------ pohľad: Presety
function presetChips(v) {
  const c = [];
  c.push(v['denoise.enabled'] ? `Čistenie ${v['ui.clean']} %` : 'Čistenie vyp.');
  c.push(v['compressor.enabled'] ? `${(DYN_CHARS[v['ui.dyn_char']] || DYN_CHARS[1]).name} ${v['ui.dyn_amount']} %` : 'Dynamika vyp.');
  if (v['saturation.enabled']) c.push(`Farba ${v['ui.color']} %`);
  c.push(v['eq.enabled'] ? `Tón: ${(TONE_CHARS[v['ui.tone_char']] || { name: 'vlastný' }).name}` : 'Tón vyp.');
  return h('div', { class: 'chips' }, ...c.map(x => h('span', {}, x)));
}
async function renderPresets() {
  const v = $('#v-presets'); v.innerHTML = '';
  try { S.presets = await invoke('list_presets'); } catch { S.presets = []; }
  v.append(h('div', { class: 'head' },
    h('div', {}, h('h1', {}, 'Presety'), h('p', {}, 'Celý zvuk jedným klikom. Prah ticha a nastavenie vstupu ostanú tvoje.')),
    h('div', { class: 'actions' }, h('button', { class: 'btn primary', onclick: savePresetDialog }, icon('plus'), 'Uložiť aktuálny'))));
  const grid = h('div', { class: 'grid' });
  allPresets().forEach((p, i) => {
    const c = h('div', { class: 'card preset land', style: `--i:${i}`, tabindex: 0 },
      h('h3', {}, p.name, p.builtin ? h('span', { class: 'badge' }, 'Vstavaný') : null),
      h('p', {}, p.desc), presetChips(p.values),
      !p.builtin ? h('button', { class: 'btn small danger del', title: 'Zmazať', onclick: e => { e.stopPropagation(); deletePreset(p.name); } }, icon('trash')) : null);
    c.addEventListener('click', () => applyPreset(p));
    c.addEventListener('keydown', e => { if (e.key === 'Enter') applyPreset(p); });
    c._values = p.values;
    grid.append(c);
  });
  v.append(grid);
  updatePresetName();
}
function savePresetDialog() {
  const inp = h('input', { class: 'txt', placeholder: 'Napr. Večerný stream', maxlength: 60 });
  const doSave = async () => {
    const name = inp.value.trim(); if (!name) return inp.focus();
    try { await invoke('save_preset', { name, values: toIni(S.values) }); m.close(); toast(`Preset „${name}“ uložený`); S.presets = await invoke('list_presets'); updatePresetName(); if (S.view === 'presets') renderPresets(); }
    catch (e) { toast(String(e), true); }
  };
  inp.addEventListener('keydown', e => e.key === 'Enter' && doSave());
  const m = modal(h('div', {}, h('h3', {}, 'Uložiť preset'), h('p', {}, 'Uloží sa aktuálne nastavenie všetkých efektov.'), inp),
    [['Zrušiť', null], ['Uložiť', doSave, 'primary']]);
  setTimeout(() => inp.focus(), 50);
}
async function deletePreset(name) {
  try { await invoke('delete_preset', { name }); toast(`Preset „${name}“ zmazaný`); renderPresets(); } catch (e) { toast(String(e), true); }
}

// ------------------------------------------------------------------ pohľad: Soundboard
function renderBoard() {
  $('#v-board').append(h('div', { class: 'head' }, h('div', {}, h('h1', {}, 'Soundboard'), h('p', {}, 'Zvuky priamo do mikrofónu.'))),
    h('div', { class: 'card soon land' }, h('div', { class: 'big' }, icon('board')), h('h3', {}, 'Čoskoro'),
      h('p', {}, 'Zvuky, ktoré pustíš klávesovou skratkou a počujú ich všetci v hovore – bez ďalšieho virtuálneho kábla. Úplne voliteľné.')));
}

// ------------------------------------------------------------------ pohľad: Nastavenia
async function renderSettings() {
  const v = $('#v-settings'); v.innerHTML = '';
  const mark = bound.length;   // ovládače tejto stránky sa po prekreslení odstránia
  try { S.app = await invoke('get_state'); } catch (e) { toast(String(e), true); }
  const app = S.app || { endpoints: [] };
  if (!S.selDev) S.selDev = (app.endpoints.find(e => e.installed) || app.endpoints.find(e => /focusrite|scarlett/i.test(e.adapter)) || app.endpoints[0] || {}).id;
  const anyInstalled = app.endpoints.some(e => e.installed);

  v.append(h('div', { class: 'head' }, h('div', {}, h('h1', {}, 'Nastavenia'), h('p', {}, 'Mikrofón, vstup, vzhľad a info o appke.'))));

  v.append(h('h2', { class: 'sec', style: 'margin-top:0' }, 'Mikrofón s efektmi'));
  const list = h('div', { class: 'list' });
  for (const e of app.endpoints) {
    const row = h('div', { class: 'dev' + (e.id === S.selDev ? ' sel' : ''), tabindex: 0, role: 'radio', 'aria-checked': e.id === S.selDev },
      h('span', { class: 'radio' }), h('div', {}, h('div', { class: 'n' }, e.adapter || e.name), h('div', { class: 'a' }, e.name)),
      e.installed ? h('span', { class: 'badge mint' }, icon('check'), 'Efekty aktívne') : null);
    const pick = () => { S.selDev = e.id; renderSettings(); };
    row.addEventListener('click', pick); row.addEventListener('keydown', ev => ev.key === 'Enter' && pick());
    list.append(row);
  }
  if (!app.endpoints.length) list.append(h('div', { class: 'note' }, 'Nenašiel som žiadny aktívny mikrofón.'));
  const sel = app.endpoints.find(e => e.id === S.selDev);
  v.append(h('div', { class: 'card land' }, list,
    h('div', { class: 'setrow' },
      h('div', {}, h('div', { class: 't' }, sel?.installed ? 'Preinštalovať' : 'Nainštalovať efekt'),
        h('div', { class: 's' }, 'Na pár sekúnd sa reštartuje zvuk vo Windows. SteelSeries Sonar sa reštartuje sám.')),
      h('div', { class: 'r' },
        anyInstalled ? h('button', { class: 'btn danger', onclick: doUninstall }, 'Odinštalovať') : null,
        h('button', { class: 'btn primary', disabled: !sel || !app.payload_ok, onclick: () => doInstall(sel) }, sel?.installed ? 'Preinštalovať' : 'Nainštalovať'))),
    !app.payload_ok ? h('div', { class: 'note' }, 'Chýba priečinok „payload“ vedľa appky – inštalácia nie je možná.') : null));

  v.append(h('h2', { class: 'sec' }, 'Vstup'));
  v.append(h('div', { class: 'card land', style: '--i:1' },
    h('div', { class: 'setrow' }, h('div', {}, h('div', { class: 't' }, 'Mikrofón je na'), h('div', { class: 's' }, 'Ktorý vstup Scarlett používaš – výsledok pôjde do oboch kanálov.')),
      h('div', { class: 'r' }, segment([[0, 'Vstup 1'], [1, 'Vstup 2'], [-1, 'Mix']], () => S.values['global.input_channel'], x => setVal('global.input_channel', x)))),
    h('div', { class: 'setrow wide' }, slider({ key: 'global.input_gain_db', label: 'Zosilnenie vstupu', min: -12, max: 24, step: .5, unit: 'dB' })),
    h('div', { class: 'note' }, 'Najlepšie je nastaviť gain priamo na Scarlett tak, aby hlasná reč bola okolo −12 dB na metri „Vstup“.')));

  v.append(h('h2', { class: 'sec' }, 'Vzhľad'));
  let theme = 'auto'; try { theme = localStorage.getItem('hmo.theme') || 'auto'; } catch { }
  let curTheme = theme;
  v.append(h('div', { class: 'card land', style: '--i:2' }, h('div', { class: 'setrow' }, h('div', {}, h('div', { class: 't' }, 'Téma')),
    h('div', { class: 'r' }, segment([['auto', 'Podľa systému'], ['light', 'Svetlá'], ['dark', 'Tmavá']], () => curTheme,
      t => { curTheme = t; try { localStorage.setItem('hmo.theme', t); } catch { } applyTheme(t); })))));

  v.append(h('h2', { class: 'sec' }, 'O appke'));
  const m = S.meter;
  v.append(h('div', { class: 'card land', style: '--i:3' },
    h('div', { class: 'setrow' }, h('div', {}, h('div', { class: 't' }, 'HearMeOut'), h('div', { class: 's' }, 'od WheresTheApp')), h('div', { class: 'r mono' }, 'v' + (app.version || '?'))),
    updateRow(),
    h('div', { class: 'setrow' }, h('div', {}, h('div', { class: 't' }, 'Zvuk'), h('div', { class: 's' }, 'Frekvencia a oneskorenie efektov')),
      h('div', { class: 'r mono' }, m.sample_rate ? `${(m.sample_rate / 1000).toFixed(1)} kHz · ${m.latency_ms.toFixed(1)} ms` : '–')),
    h('div', { class: 'setrow' }, h('div', {}, h('div', { class: 't' }, 'Súbory s nastavením'), h('div', { class: 's mono' }, app.data_dir || '')),
      h('div', { class: 'r' }, h('button', { class: 'btn small', onclick: () => invoke('open_data_dir') }, icon('folder'), 'Otvoriť')))));
  // ovládače nastavení si nechaj len počas zobrazenia
  const own = bound.splice(mark);
  v._own = own;
}

// kedy nasadiť: pri reštarte PC (bez výpadku zvuku) alebo hneď (zvuk na chvíľu vypadne, treba reštartovať Sonar)
function whenDialog(title, text) {
  return new Promise(res => {
    const m = modal(h('div', {}, h('h3', {}, title), h('p', {}, text),
      h('div', { class: 'whenlist' },
        h('button', { class: 'whenopt rec', onclick: () => { m.close(); res('later'); } }, icon('check'),
          h('div', {}, h('b', {}, 'Pri reštarte PC'), h('small', {}, 'Odporúčané – zvuk teraz nevypadne. Zmena sa prejaví po reštarte.'))),
        h('button', { class: 'whenopt', onclick: () => { m.close(); res('now'); } }, icon('warn'),
          h('div', {}, h('b', {}, 'Hneď'), h('small', {}, 'Zvuk na pár sekúnd vypadne. Ak používaš Sonar, môže byť potrebné ho reštartovať.'))))),
      [['Zrušiť', () => { m.close(); res(null); }]]);
  });
}
async function doInstall(dev) {
  if (!dev) return;
  const when = await whenDialog(dev.installed ? 'Preinštalovať efekt' : 'Nainštalovať efekt', 'Kedy sa má zmena prejaviť?');
  if (!when) return;
  const now = when === 'now';
  if (S.monitoring) { await invoke('monitor_stop'); S.monitoring = false; paintMonitorState(); }
  const m = modal(h('div', { style: 'text-align:center' }, h('div', { class: 'spin' }), h('h3', {}, 'Inštalujem efekt…'),
    h('p', {}, now ? 'Potvrď admin práva. Zvuk na chvíľu vypadne a hneď sa vráti.' : 'Potvrď admin práva. Zvuk beží ďalej.')), []);
  try {
    const r = await invoke('install_on', { endpointId: dev.id, now });
    m.close();
    if (!r.ok) showLog('Inštalácia sa nepodarila', r.log);
    else if (now) { toast('Hotovo – efekty bežia na ' + (dev.adapter || dev.name)); await refreshAll(); }
    else {
      await refreshAll();
      modal(h('div', {}, h('h3', {}, 'Hotovo – zapne sa po reštarte'), h('p', {}, 'Efekt je pripravený. Pri najbližšom reštarte PC sa nasadí nová verzia. Dovtedy beží tá, ktorá už bola nainštalovaná.')), [['OK', null, 'primary']]);
    }
  } catch (e) { m.close(); toast(String(e), true); }
}
async function doUninstall() {
  const when = await whenDialog('Odinštalovať HearMeOut?', 'Mikrofón sa vráti do pôvodného stavu. Tvoje nastavenia a presety ostanú.');
  if (!when) return;
  const now = when === 'now';
  if (S.monitoring) { await invoke('monitor_stop'); S.monitoring = false; }
  const m = modal(h('div', { style: 'text-align:center' }, h('div', { class: 'spin' }), h('h3', {}, 'Odinštalovávam…'), h('p', {}, 'Potvrď admin práva.')), []);
  try {
    const r = await invoke('uninstall_all', { now });
    m.close();
    if (r.ok) { toast(now ? 'HearMeOut odinštalovaný' : 'Odinštalované – úplne zmizne po reštarte PC'); await refreshAll(); } else showLog('Odinštalovanie sa nepodarilo', r.log);
  } catch (e) { m.close(); toast(String(e), true); }
}
function showLog(title, log) {
  modal(h('div', {}, h('h3', {}, title), h('p', {}, 'Tu je posledný výpis – pošli ho, prosím, ďalej:'), h('pre', {}, log || '(prázdny)')), [['Zavrieť', null, 'primary']]);
}

// ------------------------------------------------------------------ modal / toast
function modal(content, buttons) {
  const box = h('div', { class: 'modal', role: 'dialog' }, content);
  const scrim = h('div', { class: 'scrim' }, box);
  const close = () => { scrim.classList.add('out'); setTimeout(() => scrim.remove(), 180); };
  if (buttons.length) box.append(h('div', { class: 'acts' }, ...buttons.map(([l, fn, cls]) =>
    h('button', { class: 'btn ' + (cls || ''), onclick: () => fn ? fn() : close() }, l))));
  if (buttons.length) scrim.addEventListener('click', e => e.target === scrim && close());
  document.body.append(scrim);
  return { close };
}
function confirmBox(title, text, okLabel, okCls = 'danger') {
  return new Promise(res => {
    const m = modal(h('div', {}, h('h3', {}, title), h('p', {}, text)),
      [['Zrušiť', () => { m.close(); res(false); }], [okLabel, () => { m.close(); res(true); }, okCls]]);
  });
}
function toast(text, err = false) {
  const t = h('div', { class: 'toast' + (err ? ' err' : '') }, icon(err ? 'warn' : 'check', 'i' + (err ? '' : ' check')), text);
  $('#toasts').append(t);
  while ($('#toasts').children.length > 3) $('#toasts').firstChild.remove();
  setTimeout(() => { t.classList.add('out'); setTimeout(() => t.remove(), 260); }, err ? 4200 : 2200);
}

// ------------------------------------------------------------------ aktualizácie
let updateInfo = null;
async function checkUpdate(manual = false) {
  try {
    updateInfo = await invoke('update_check');
    paintUpdateCard();
    if (manual) toast(updateInfo.available ? `Je dostupná verzia ${updateInfo.latest}` : `Máš najnovšiu verziu (${updateInfo.current})`);
  } catch (e) { if (manual) toast(String(e), true); }
  if (S.view === 'settings') { const el = $('#updRow'); if (el) el.replaceWith(updateRow()); }
}
function paintUpdateCard() {
  const box = $('#updCard'); if (!box) return;
  box.innerHTML = '';
  if (!updateInfo?.available) { box.hidden = true; return; }
  box.hidden = false;
  box.append(h('div', { class: 'row' }, icon('sparkle'), h('span', {}, `Nová verzia ${updateInfo.latest}`)),
    h('button', { class: 'btn primary small', onclick: installUpdate }, 'Aktualizovať'));
}
async function installUpdate() {
  if (!updateInfo?.url) return;
  if (S.monitoring) { try { await invoke('monitor_stop'); } catch { } }
  modal(h('div', { style: 'text-align:center' }, h('div', { class: 'spin' }), h('h3', {}, `Sťahujem verziu ${updateInfo.latest}…`),
    h('p', {}, 'Appka sa zavrie a spustí sa inštalátor novej verzie. Efekty na mikrofóne medzitým bežia ďalej.')), []);
  try { await invoke('update_install', { url: updateInfo.url }); }
  catch (e) { document.querySelector('.scrim')?.remove(); toast(String(e), true); }
}
function updateRow() {
  const u = updateInfo;
  return h('div', { class: 'setrow', id: 'updRow' },
    h('div', {}, h('div', { class: 't' }, 'Aktualizácie'), h('div', { class: 's' }, u?.available ? `Dostupná verzia ${u.latest}` : 'Nové verzie sa stiahnu z GitHubu')),
    h('div', { class: 'r' }, u?.available ? h('button', { class: 'btn primary small', onclick: installUpdate }, 'Aktualizovať')
      : h('button', { class: 'btn small', onclick: () => checkUpdate(true) }, 'Skontrolovať')));
}

// ------------------------------------------------------------------ stav v paneli
function updateStatus() {
  const dot = $('#stDot'), txt = $('#stText'), sub = $('#stSub'), m = S.meter;
  let c = '', t = '', s = '';
  if (!S.app) { t = 'Načítavam…'; }
  else if (!S.app.registered) { c = 'amber'; t = 'Nie je nainštalované'; s = 'Nastavenia → Mikrofón'; }
  else if (S.values['global.bypass']) { c = 'amber'; t = 'Bez efektov (A/B)'; s = m.active ? 'zvuk ide bez úprav' : 'mikrofón nebeží'; }
  else if (m.active) { c = 'mint'; t = S.monitoring ? 'Efekty bežia · počúvaš' : 'Efekty bežia'; s = `${(m.sample_rate / 1000).toFixed(0)} kHz · ${m.latency_ms.toFixed(0)} ms`; }
  else { t = 'Pripravené'; s = 'mikrofón práve nebeží'; }
  dot.className = 'dot ' + c; txt.textContent = t; sub.textContent = s || '\u00a0';
}

// ------------------------------------------------------------------ téma, okno, štart
function applyTheme(t) {
  const dark = t === 'dark' || (t === 'auto' && matchMedia('(prefers-color-scheme: dark)').matches);
  document.documentElement.dataset.scheme = dark ? 'dark' : 'light';
}
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => { let t = 'auto'; try { t = localStorage.getItem('hmo.theme') || 'auto'; } catch { } applyTheme(t); });

async function refreshAll() {
  try { S.app = await invoke('get_state'); } catch { }
  try { S.presets = await invoke('list_presets'); } catch { }
  try {
    const raw = await invoke('load_config');
    S.values = fromIni(raw);
    if (!('ui.clean' in raw)) {
      // prvé spustenie novej verzie: predvolený preset Punchy Štúdio, prah ticha zachovaj
      S.values = presetValues(BUILTIN[0], S.values);
      if ('gate.threshold_db' in raw) S.values['gate.threshold_db'] = +raw['gate.threshold_db'];
      saveNow();
    }
  } catch { }
  renderMic();
  if (S.view === 'settings') renderSettings();
  if (S.view === 'presets') renderPresets();
  updateStatus();
}

document.addEventListener('keydown', e => {
  if (e.target.closest('input.txt')) return;
  if (e.key === 'b' || e.key === 'B') toggleBypass();
});

setInterval(() => { if (S.view === 'mic' && S.values['global.engine'] === 1 && !document.hidden) refreshHost(); }, 2000);

async function start() {
  buildNav();
  renderBoard();
  if (IS_APP) {
    const w = TAURI.window.getCurrentWindow();
    $('#wMin').onclick = () => w.minimize();
    $('#wMax').onclick = () => w.toggleMaximize();
    $('#wClose').onclick = () => w.close();
    TAURI.event.listen('meter', e => onMeter(e.payload));
    const vis = () => { const on = !document.hidden; invoke('set_meter_active', { on }); document.body.classList.toggle('idle', !on); };
    document.addEventListener('visibilitychange', vis);
  } else {
    let t = 0;
    setInterval(() => { t += .033; const talk = Math.sin(t * .7) > -.2; const x = talk ? -18 + 7 * Math.sin(t * 9) * Math.sin(t * 2.3) : -58;
      onMeter({ connected: true, active: true, in_db: x, out_db: talk ? x + 4 : -80, gate_db: talk ? 0 : -16, comp_gr_db: talk ? -4 - 2 * Math.sin(t * 5) : 0,
        deess_db: talk ? Math.min(0, -3 * Math.sin(t * 13)) : 0, sample_rate: 48000, latency_ms: 12.5, monitoring: S.monitoring }); }, 33);
  }
  await refreshAll();
  go('mic');
  if (IS_APP) TAURI.window.getCurrentWindow().show();
  setTimeout(() => checkUpdate(false), 2500);
}
document.addEventListener('contextmenu', e => { if (!e.target.closest('input,pre')) e.preventDefault(); });
start();
