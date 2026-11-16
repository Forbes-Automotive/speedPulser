'use strict';
// SpeedPulser / SpeedPulser Pro web UI: ONE file, the same in both projects (V5.09).
// Copy it to both data/ folders unchanged. It talks to the JSON API in
// speedPulser_webserver.cpp / SpeedPulserPro_wifi.cpp, which use the same names.
// Every setting control carries data-key="<API key>"; the value shown is always the
// one the unit replied it kept (clamped or refused values snap back).
// What a product or board doesn't have (Pro: CAN, GPS, tach, speed sources;
// SpeedPulser: nothing extra) simply isn't in its index.html, and every part of
// this file that needs such an element skips itself when it isn't there.

const STATUS_POLL_MS = 250;
const PWM_DUTY_MAX = 4095;
const CUSTOM_CAL_ID = 200;
const IS_PRO = /Pro/.test(document.title); // only for names: the page markup decides the rest
const PREFS_KEY = IS_PRO ? 'speedPulserProUi' : 'speedPulserUi'; // per-browser: open cards, dial gauges

let settings = {};            // last full settings from the unit
let isVoltageBoard = false;   // S3 board (buck voltage control) vs DevKit (PWM only)
let dialUnit = 'km/h';        // what the needle reads
let lastStatus = {};          // last /api/status

document.addEventListener('DOMContentLoaded', init);

async function init() {
  initTabs();
  initCards();
  initSliderTouchGuard();
  initFocusRelease();
  initSettingControls();
  initSensorPresets();
  initButtons();
  initGpsRate();
  initDriveCalibration();
  initCalibrationBuilder();
  initExportImport();
  initStartupLog();
  // Keep trying until the unit answers: until then nothing board-specific can show.
  while (!(await loadPresets()) || !(await loadSettings())) {
    showOffline();
    await new Promise((resolve) => setTimeout(resolve, 2000));
  }
  await refreshCalibrationBuilder();
  initGaugeOptions(); // after the settings: the board decides which dials are offered
  await loadCalCurve();
  await loadBoardTest();
  await loadStartupLog();
  pollStatus();
  window.addEventListener('resize', scheduleChartDraw);
}

// ===================================================================
// API
// ===================================================================
// GET when body is undefined, POST JSON otherwise. A refused POST shows the
// unit's reason. Resolves to { ok, data } and never throws.
async function api(path, body) {
  const isPost = body !== undefined;
  try {
    const response = await fetch(path, isPost
      ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
      : {});
    const data = await response.json().catch(() => ({}));
    if (!response.ok && isPost) showNotification(data.error || data.message || ('Error ' + response.status), 'error');
    return { ok: response.ok, data };
  } catch (error) {
    if (isPost) showNotification('Unit not reachable', 'error');
    return { ok: false, data: { error: 'Unit not reachable' } };
  }
}
const setSetting = (key, value) => api('/api/control', { key, value });
const runAction = (action, extra) => api('/api/action', Object.assign({ action }, extra || {}));
const calibrationOp = (op, extra) => api('/api/cal', Object.assign({ op }, extra || {}));

function showNotification(message, type = 'success') {
  const note = document.createElement('div');
  note.className = 'notification ' + (type === 'error' ? 'error' : 'ok');
  note.textContent = message;
  document.body.appendChild(note);
  setTimeout(() => { note.classList.add('fade'); setTimeout(() => note.remove(), 300); }, 3000);
}

const byId = (id) => document.getElementById(id);
function setText(id, text) { const el = byId(id); if (el) el.textContent = text; }
function setPill(el, cls, text) {
  if (!el) return;
  el.className = 'pill' + (cls ? ' ' + cls : '');
  el.textContent = text;
}
const fixed = (value, digits) => (value === undefined || value === null) ? '--' : Number(value).toFixed(digits);
const signed = (value, digits) => (Number(value) > 0 ? '+' : '') + fixed(value, digits);

// ===================================================================
// Tabs, cards, per-browser preferences
// ===================================================================
function loadPrefs() {
  try { return JSON.parse(localStorage.getItem(PREFS_KEY)) || {}; } catch (e) { return {}; }
}
function savePrefs(prefs) {
  try { localStorage.setItem(PREFS_KEY, JSON.stringify(prefs)); } catch (e) { /* private window */ }
}

function initTabs() {
  const tabs = document.querySelectorAll('.nav-tab');
  tabs.forEach((tab) => tab.addEventListener('click', () => {
    tabs.forEach((t) => t.classList.toggle('active', t === tab));
    document.querySelectorAll('.page').forEach((page) =>
      page.classList.toggle('active', page.id === tab.dataset.page + '-page'));
    if (tab.dataset.page === 'dashboard') scheduleChartDraw(); // the canvas can't measure itself while hidden
    if (tab.dataset.page === 'diagnostics') loadStartupLog();
  }));
}

// Setup cards start open (it's a walk-through); Advanced / Diagnostics start
// closed except those marked data-open-by-default. Each card remembers its state.
function initCards() {
  const prefs = loadPrefs();
  prefs.cards = prefs.cards || {};
  document.querySelectorAll('.card.collapsible').forEach((card) => {
    const defaultOpen = card.hasAttribute('data-open-by-default') || !!card.closest('#setup-page');
    const open = card.id in prefs.cards ? prefs.cards[card.id] : defaultOpen;
    card.classList.toggle('collapsed', !open);
    const heading = card.querySelector('h2');
    if (heading) heading.addEventListener('click', () => {
      card.classList.toggle('collapsed');
      prefs.cards[card.id] = !card.classList.contains('collapsed');
      savePrefs(prefs);
      scheduleChartDraw();
    });
  });
}

// ===================================================================
// Settings
// ===================================================================
const DISPLAY_FORMAT = {
  inputSmoothingMs: (v) => v + ' ms',
  feedbackDeadband: (v) => Number(v).toFixed(1) + ' Hz',
  trimGain: (v) => Number(v).toFixed(2),
  trimLimit: (v) => '±' + Math.round(Number(v) * 100) + '%',
  pwmNominal: (v) => Math.round(Number(v) * 100) + '%',
  railMinV: (v) => Number(v).toFixed(1) + ' V',
  railMaxV: (v) => Number(v).toFixed(1) + ' V',
  globalSpeedOffsetKph: (v) => (Number(v) > 0 ? '+' : '') + v,
};
for (let i = 0; i < 5; i++) DISPLAY_FORMAT['curveOffset' + i] = DISPLAY_FORMAT.globalSpeedOffsetKph;

// Sliders that act live while being dragged (one request in flight, latest wins).
const LIVE_SLIDERS = new Set(['speedTestKph', 'rpmTestRpm', 'needleSweepSpeed']);
const liveSliderState = {};

// force: also the control being edited (the reply to its own change: a refused or
// clamped value must show where it was typed).
function showSetting(key, value, force) {
  if (value === undefined || value === null) return;
  document.querySelectorAll('[data-key="' + key + '"]').forEach((el) => {
    if (el.type === 'checkbox') { el.checked = !!value; return; }
    if (!force && document.activeElement === el && el.type !== 'range') return; // don't fight typing
    if ('bool' in el.dataset) el.value = String(!!value);
    else if ('hex' in el.dataset) el.value = typeof value === 'number' ? value.toString(16).toUpperCase() : value;
    else el.value = value;
  });
  const format = DISPLAY_FORMAT[key];
  document.querySelectorAll('[data-display-for="' + key + '"]').forEach((el) => {
    el.textContent = format ? format(value) : value;
  });
}

function readControl(el) {
  if (el.type === 'checkbox') return el.checked;
  if ('bool' in el.dataset) return el.value === 'true';
  if ('hex' in el.dataset) return el.value.trim();
  if (el.type === 'range' || el.type === 'number') return Number(el.value);
  if (el.tagName === 'SELECT' && el.value !== '' && !Number.isNaN(Number(el.value))) return Number(el.value);
  return el.value; // speed / RPM source names
}

async function commitSetting(key, value) {
  const before = settings[key];
  const result = await setSetting(key, value);
  if (!result.ok) {
    showSetting(key, before, true); // refused: put the control back
    return false;
  }
  settings[key] = result.data.value;
  showSetting(key, result.data.value, true); // the kept value, also in the box being edited
  if (result.data.railMinV !== undefined) { settings.railMinV = result.data.railMinV; showSetting('railMinV', result.data.railMinV); }
  afterSettingChanged(key);
  return true;
}

function sendLiveSlider(key, value) {
  const state = liveSliderState[key] = liveSliderState[key] || {};
  state.latest = value;
  if (state.busy) return;
  state.busy = true;
  setSetting(key, state.latest).finally(() => {
    state.busy = false;
    if (state.latest !== value) sendLiveSlider(key, state.latest);
  });
}

function initSettingControls() {
  document.querySelectorAll('[data-key]').forEach((el) => {
    const key = el.dataset.key;
    el.addEventListener('change', () => {
      // An emptied box would send 0 (e.g. 100 pulses per km: the needle pinned at maximum).
      if (el.type === 'number' && (el.value.trim() === '' || !Number.isFinite(Number(el.value)))) {
        showSetting(key, settings[key], true);
        return;
      }
      const value = readControl(el);
      if (key === 'clusterInMph' && !confirmUnitChange(value)) { el.checked = !value; return; }
      commitSetting(key, value);
    });
    if (el.type === 'range') {
      el.addEventListener('input', () => {
        const format = DISPLAY_FORMAT[key];
        document.querySelectorAll('[data-display-for="' + key + '"]').forEach((d) => {
          d.textContent = format ? format(el.value) : el.value;
        });
        if (LIVE_SLIDERS.has(key)) sendLiveSlider(key, Number(el.value));
      });
    }
  });
}

// Marks are captured in the dial's unit: switching it reads them in the other.
function confirmUnitChange(toMph) {
  if (!isVoltageBoard || !(settings.driveCal || []).length) return true;
  return confirm('Your calibration marks were captured with the dial in ' + (toMph ? 'km/h' : 'mph') +
                 '. Changing the unit means capturing them again (then Auto Fill). Change it?');
}

// Changing these changes other settings too (one test mode at a time, the source
// switches, the drive range): read them all back.
const RELOAD_AFTER = new Set(['speedType', 'rpmType', 'analyzerMode', 'analyzerSerial', 'railMinV', 'railMaxV',
                              'pwmNominal', 'calibrationModeOn', 'speedTestOn', 'rpmTestOn', 'presetId', 'clusterInMph']);

// What follows a change, beyond showing the kept value.
function afterSettingChanged(key) {
  if (key === 'clusterInMph') { applyDialUnit(settings.clusterInMph); renderDriveMarks(settings.driveCal, settings.driveCalHz); }
  if (key === 'maxSpeed' || key.endsWith('PulsesPerKm')) { updateSensorHints(); applyGaugeRanges(); applySliderLimits(); }
  if (key === 'maxSpeed' || key === 'presetId') loadCalCurve();
  if (key === 'useSpeedOffsetCurve') applyOffsetMode();
  if (key === 'speedType') applySpeedSource();
  if (key === 'calibrationModeOn' && settings.calibrationModeOn) setCalibrationDrive(0, false); // the unit restarts the motor stopped
  if (key === 'speedTestOn' && !settings.speedTestOn) showSetting('speedTestKph', 0);
  if (RELOAD_AFTER.has(key)) loadSettings();
}

async function loadSettings() {
  const { ok, data } = await api('/api/settings');
  if (ok) applySettings(data);
  return ok;
}

// Show a full settings object (GET /api/settings, or any /api/control reply).
function applySettings(data) {
  settings = Object.assign(settings, data);
  isVoltageBoard = !!data.isVoltageBoard;
  if (data.fwVersion) setText('firmwareBadge', 'V' + data.fwVersion);
  applyBoardType(data);
  applyDialUnit(!!data.clusterInMph);
  Object.keys(data).forEach((key) => showSetting(key, data[key]));
  (data.speedOffsetCurveOffsets || []).forEach((v, i) => { settings['curveOffset' + i] = v; showSetting('curveOffset' + i, v); });
  const gpsRate = byId('gpsRateSelect');
  if (gpsRate && data.gpsUpdateRateHz !== undefined && document.activeElement !== gpsRate) gpsRate.value = data.gpsUpdateRateHz;
  applyOffsetMode();
  applySpeedSource();
  updateSensorHints();
  applySliderLimits();
  applyGaugeRanges();
  if (isVoltageBoard) {
    setDriveRange(data.driveMaxV);
    document.querySelectorAll('[data-key="railMinV"],[data-key="railMaxV"]').forEach((el) => {
      if (data.railFloorV !== undefined) el.min = data.railFloorV;
      if (data.railCeilV !== undefined) el.max = data.railCeilV;
    });
    renderBoardTestPill(data.boardTestCode, false);
    renderDriveMarks(data.driveCal, !!data.driveCalHz);
    setRestoreAvailable(data.calBackup);
    autoFillRailNote = Number(data.railMinV) < 4.7
      ? 'Minimum rail is ' + Number(data.railMinV).toFixed(1) + ' V: set it to 4.8 V (Advanced) before Auto Fill for smooth low speeds.'
      : '';
  } else if (data.presetId !== undefined) {
    byId('presetSelect').value = data.presetId; // value only: never re-send the preset
  }
  applyCalibrationLocks();
}

// Board-specific parts stay hidden until the board is known, then the body
// classes show the ones this board has (see style.css): .vc-only / .legacy-only
// (S3 voltage control or DevKit PWM), .vr-only (VR input), .bias-only (input bias).
function applyBoardType(data) {
  const body = document.body;
  body.classList.add('board-known');
  body.classList.toggle('voltage-board', isVoltageBoard);
  if (data.hasVR !== undefined) body.classList.toggle('has-vr', !!data.hasVR);
  if (data.hasSpeedBias !== undefined) body.classList.toggle('has-bias', !!data.hasSpeedBias);
  if (data.hasVR === false) { // a hidden <option> still shows on some phones: remove it
    const vrOption = document.querySelector('#speedSourceSelect option[value="VR"]');
    if (vrOption) vrOption.remove();
  }
  setText('boardTypeValue', data.boardName
    ? data.boardName + (isVoltageBoard ? ' (voltage control)' : ' (PWM only)')
    : (isVoltageBoard ? 'Voltage board (V5)' : 'Legacy (PWM only)'));
  setText('liveMotorLabel', isVoltageBoard ? 'Motor drive' : 'Motor duty');
  setText('liveMotorUnit', isVoltageBoard ? 'V' : '%');
  updateChartChrome();
}

function applyDialUnit(mph) {
  dialUnit = mph ? 'mph' : 'km/h';
  document.querySelectorAll('.unit-dial').forEach((el) => { el.textContent = dialUnit; });
}

function applyOffsetMode() {
  const curve = !!settings.useSpeedOffsetCurve;
  byId('globalOffsetGroup').hidden = curve;
  byId('offsetCurveGroup').hidden = !curve;
}

// Only the settings of the picked speed source are shown.
function applySpeedSource() {
  document.querySelectorAll('.source-group').forEach((group) => {
    group.hidden = group.dataset.source !== settings.speedType;
  });
}

// The test sliders can't go past what the unit accepts.
function applySliderLimits() {
  const speedSlider = document.querySelector('[data-key="speedTestKph"]');
  if (speedSlider && settings.maxSpeed) speedSlider.max = settings.maxSpeed;
  const rpmSlider = document.querySelector('[data-key="rpmTestRpm"]');
  if (rpmSlider && settings.clusterRPMLimit) rpmSlider.max = settings.clusterRPMLimit;
}

// ===== Hall / VR sensor presets =====
function initSensorPresets() {
  document.querySelectorAll('.sensor-preset').forEach((preset) => {
    const key = preset.dataset.presetFor;
    const input = document.querySelector('input[data-key="' + key + '"]');
    preset.addEventListener('change', () => {
      if (preset.value === 'custom') { input.focus(); return; }
      input.value = preset.value;
      commitSetting(key, Number(preset.value));
    });
  });
}

function updateSensorHints() {
  const maxSpeed = Number(settings.maxSpeed) || 200;
  document.querySelectorAll('.sensor-preset').forEach((preset) => {
    const key = preset.dataset.presetFor;
    if (settings[key] === undefined) return;
    const pulsesPerKm = Number(settings[key]) || 3600;
    const match = [...preset.options].find((o) => o.value !== 'custom' && Math.abs(Number(o.value) - pulsesPerKm) < 0.05);
    preset.value = match ? match.value : 'custom';
    document.querySelectorAll('[data-scale-hint-for="' + key + '"]').forEach((hint) => {
      hint.textContent = (pulsesPerKm / 36).toFixed(1) + ' Hz at 100 km/h, ' +
                         (maxSpeed * pulsesPerKm / 3600).toFixed(0) + ' Hz at Maximum speed.';
    });
  });
}

// ===================================================================
// Buttons that aren't settings
// ===================================================================
function initButtons() {
  byId('needleSweepBtn').addEventListener('click', async () => {
    if ((await runAction('needleSweep')).ok) showNotification('Needle sweep');
  });
  byId('boardTestBtn').addEventListener('click', async () => {
    if (!(await runAction('boardTest')).ok) return;
    renderBoardTestPill(0, true);
    byId('boardTestBtn').disabled = true;
    pollBoardTest();
  });
  byId('resetFeedbackBtn').addEventListener('click', async () => {
    const defaults = { feedbackDeadband: 0.3, feedbackMinSpeed: 8, trimGain: 2.0, trimLimit: 0.40 };
    let allOk = true;
    for (const [key, value] of Object.entries(defaults)) allOk = (await commitSetting(key, value)) && allOk;
    if (allOk) showNotification('Feedback defaults restored');
  });
  byId('testBannerStop').addEventListener('click', () => {
    if (lastStatus.calibrationModeOn) commitSetting('calibrationModeOn', false);
    else if (lastStatus.speedTestOn) commitSetting('speedTestOn', false);
    else if (lastStatus.rpmTestOn) commitSetting('rpmTestOn', false);
  });
}

// ===== Phone focus =====
// On a phone a slider, button or tick-box doesn't take the focus, so after typing in
// a box the box kept it and every tap elsewhere scrolled the page back to it. Any
// touch outside the box being typed in now lets it go, as a click does on a desktop.
function initFocusRelease() {
  document.addEventListener('touchstart', (event) => {
    const active = document.activeElement;
    if (!active || !active.matches('input:not([type="range"]):not([type="checkbox"]), textarea, select')) return;
    if (active === event.target || active.contains(event.target)) return;
    active.blur();
  }, { passive: true, capture: true });
}

// ===== Phone sliders =====
// A scroll that started on a slider used to move it (and send it). Now a slider
// only changes on a sideways drag: a tap or an up/down swipe leaves it as it was.
function initSliderTouchGuard() {
  let guard = null; // { el, x, y, value, drag }
  const rangeAt = (target) => target && target.closest && target.closest('input[type="range"]');
  document.addEventListener('touchstart', (event) => {
    const el = rangeAt(event.target);
    if (!el || el.disabled) return;
    const touch = event.touches[0];
    guard = { el, x: touch.clientX, y: touch.clientY, value: el.value, drag: false };
  }, { passive: true, capture: true });
  document.addEventListener('touchmove', (event) => {
    if (!guard || guard.drag) return;
    const touch = event.touches[0];
    const dx = Math.abs(touch.clientX - guard.x);
    const dy = Math.abs(touch.clientY - guard.y);
    if (dx > 10 && dx > dy * 1.5) guard.drag = true;
  }, { passive: true, capture: true });
  const hold = (event) => {
    if (!guard || guard.drag || event.target !== guard.el) return;
    event.stopImmediatePropagation(); // no display update, no request
    guard.el.value = guard.value;
  };
  document.addEventListener('input', hold, true);
  document.addEventListener('change', hold, true);
  const end = () => {
    const ended = guard;
    if (!ended) return;
    if (!ended.drag) ended.el.value = ended.value;
    setTimeout(() => { if (guard === ended) guard = null; }, 100); // the change event comes after touchend
  };
  document.addEventListener('touchend', end, true);
  document.addEventListener('touchcancel', end, true);
}

// ===== Calibration Mode locks =====
// What only works in Calibration Mode (the unit refuses it otherwise) is greyed
// out until it is ticked, and while Auto Fill drives the motor.
function applyCalibrationLocks() {
  const calibrating = !!(lastStatus.calibrationModeOn ?? settings.calibrationModeOn);
  const autoFilling = !!(lastStatus.autoFill && Number(lastStatus.autoFill.state) === 1);
  document.querySelectorAll('.needs-cal-mode').forEach((el) => { el.disabled = !calibrating || autoFilling; });
}


// ===== GPS update rate =====
let gpsRateMessageUntilMs = 0; // the reply to Set GPS Rate stays up a few seconds

function initGpsRate() {
  if (!byId('gpsRateBtn')) return; // no GPS on this product
  byId('gpsRateBtn').addEventListener('click', async () => {
    const rate = Number(byId('gpsRateSelect').value);
    setText('gpsRateMessage', 'Sending…');
    const { ok, data } = await api('/api/gpsRate', { rate });
    setText('gpsRateMessage', data.message || data.error || (ok ? 'Done' : 'Failed'));
    byId('gpsRateMessage').classList.toggle('warn', !ok);
    gpsRateMessageUntilMs = Date.now() + 4000;
    if (ok) loadSettings();
  });
}

function renderGpsRate(data) {
  const message = byId('gpsRateMessage');
  if (!message) return;
  setText('gpsFrequencyValue', fixed(data.gpsFrequency, 1));
  if (Date.now() < gpsRateMessageUntilMs) return;
  message.classList.remove('warn');
  const secs = Number(data.gpsAutoApplySecs);
  if (data.gpsUnavailable) message.textContent = 'No GPS module found.';
  else if (secs > 0) message.textContent = settings.gpsUpdateRateHz + ' Hz is set after ' + secs + ' s of steady satellites.';
  else message.textContent = ' ';
}

// ===================================================================
// Live status
// ===================================================================
let statusFailures = 0;

async function pollStatus() {
  if (!document.hidden) {
    const { ok, data } = await api('/api/status');
    if (ok) { statusFailures = 0; renderStatus(data); }
    else if (++statusFailures >= 4) showOffline(); // ~1 s without an answer
  }
  setTimeout(pollStatus, STATUS_POLL_MS);
}

// The page can't reach the unit: say so instead of showing frozen values.
function showOffline() {
  const badge = byId('modeBadge');
  badge.textContent = 'Offline';
  badge.className = 'status-badge bad';
  document.body.classList.remove('test-mode');
  byId('testBanner').hidden = true;
}


function renderStatus(data) {
  lastStatus = data;
  const modeName = data.calibrationModeOn ? 'Calibration Mode' : data.speedTestOn ? 'Speed Test' : data.rpmTestOn ? 'RPM Test' : '';
  const modeBadge = byId('modeBadge');
  modeBadge.textContent = modeName || 'Running';
  modeBadge.className = 'status-badge ' + (modeName ? 'test' : 'ok');
  document.body.classList.toggle('test-mode', !!modeName);
  byId('testBanner').hidden = !modeName;
  if (modeName) {
    setText('testBannerText', modeName + (data.speedTestOn ? ': ' + (data.needleTargetSpeed ?? '--') + ' ' + dialUnit
                                         : data.rpmTestOn ? ': ' + (data.rpmTestRpm ?? '--') + ' rpm' : ''));
  }
  const alert = byId('alertBanner');
  const alertText = data.railTrippedV !== undefined
    ? 'Motor supply switched off: the rail read ' + fixed(data.railTrippedV, 1) + ' V, well above its setting. ' +
      'Restart the unit; if it happens again the board needs checking.'
    : data.feedbackMissing && data.feedbackAvailable ? 'Motor feedback lost: running on the calibration alone.'
    : data.trimAtLimit && !data.calibrationModeOn
      ? 'Feedback is at its ' + Math.round(Number(settings.trimLimit) * 100) + '% limit: the calibration doesn’t match ' +
        'the motor at this speed. Run Auto Fill again.'
      : '';
  alert.hidden = !alertText;
  alert.textContent = alertText;
  const canBadge = byId('canBadge');
  if (canBadge) {
    canBadge.textContent = 'CAN: ' + (data.hasCAN ? 'OK' : 'none');
    canBadge.className = 'status-badge' + (data.hasCAN ? ' ok' : '');
  }
  setText('calibrationBadge', 'Cal: ' + (data.calibrationText || '--'));

  const motorPct = Number(data.motorDutyApplied) / PWM_DUTY_MAX * 100;
  setText('liveInputSpeed', fixed(data.inputSpeedKph, 0));
  setText('liveNeedleTarget', data.needleTargetSpeed ?? '--');
  setText('liveRpm', data.vehicleRPM ?? '--');
  setText('liveMotorOutput', isVoltageBoard ? fixed(data.driveV, 2) : motorPct.toFixed(1));
  setText('liveMeasuredSpeed', data.feedbackAvailable ? data.measuredSpeed : (data.feedbackMissing ? 'N/A' : '--'));
  setText('liveSourceName', data.activeSpeedSource || '--');
  setPill(byId('canPill'), data.hasCAN ? 'ok' : '', data.hasCAN ? 'Receiving' : 'No frames');
  setPill(byId('gpsPill'), data.gpsUnavailable ? '' : data.hasGPS ? 'ok' : 'warn',
          data.gpsUnavailable ? 'Not fitted' : data.hasGPS ? 'Fix, ' + (data.gpsSatellites ?? 0) + ' sats' : 'No fix');

  renderDiagnostics(data);
  setText('speedTestHint', 'Needle target: ' + (data.needleTargetSpeed ?? '--') + ' ' + dialUnit);
  applyCalibrationLocks();
  setText('customCanSpeedValue', (data.aftermarketSpeed ?? '--') + ' km/h');
  setText('dsgSpeedValue', data.dsgSpeed ?? '--');
  setText('broadcastValue', data.broadcastSpeedEnabled ? (data.broadcastSpeedValue ?? '--') : 'off');
  renderGpsRate(data);

  // Another browser (or the unit itself, e.g. Auto Fill) may change the test modes.
  ['speedTestOn', 'calibrationModeOn', 'rpmTestOn'].forEach((key) => {
    if (data[key] !== undefined && data[key] !== settings[key]) { settings[key] = data[key]; showSetting(key, data[key]); }
  });
  ['speedTestKph', 'rpmTestRpm'].forEach((key) => {
    const slider = document.querySelector('[data-key="' + key + '"]');
    if (slider && document.activeElement !== slider && !(liveSliderState[key] || {}).busy) showSetting(key, data[key]);
  });

  if (isVoltageBoard) {
    setText('boardRailValue', fixed(data.motorRailV, 2) + ' V');
    renderDriveCalibrationLive(data);
    pushChartSample({
      target: Number(data.needleTargetSpeed),
      measured: data.feedbackAvailable ? Number(data.measuredSpeed) : null,
      drive: Number(data.driveMaxV) > 0 ? Number(data.driveV) / Number(data.driveMaxV) * 100 : null,
      pwm: Number(data.pwmFrac) * 100,
    });
    if (!!data.boardTestRunning !== boardTestWasRunning) {
      boardTestWasRunning = !!data.boardTestRunning;
      if (boardTestWasRunning && !boardTestPollTimer) pollBoardTest();
    }
  } else {
    updateCalibrationDutyReadout(data.calibrationProbeDuty);
    if (data.calibrationModeOn) setChartPoint(data.calibrationProbeDuty, curveSpeedAt(data.calibrationProbeDuty));
    else setChartPoint(data.motorDutyApplied, data.needleTargetSpeed);
  }
  updateGauges();
}

function renderDiagnostics(data) {
  setText('diagInputHz', fixed(data.inputFrequencyHz, 1)); // SpeedPulser's one sensor
  setText('diagInputKph', fixed(data.inputSpeedKph, 1));
  setText('diagHall', (data.hallSpeed ?? '--') + ' (' + fixed(data.hallFrequencyHz, 1) + ' Hz)');
  setText('diagVR', (data.vrSpeed ?? '--') + ' (' + fixed(data.vrFrequencyHz, 1) + ' Hz)');
  setText('diagECU', data.ecuSpeed ?? '--');
  setText('diagABS', data.absSpeed ?? '--');
  setText('diagDSG', data.dsgSpeed ?? '--');
  setText('diagTP20', data.tp20Speed ?? '--');
  setText('diagUDS', data.udsSpeed ?? '--');
  setText('diagGPS', data.gpsSpeed ?? '--');
  setText('diagCustomCAN', data.aftermarketSpeed ?? '--');
  setText('diagRpmHall', data.hallRPM ?? '--');
  setText('diagRpmCan', data.canRPM ?? '--');
  setText('diagOffset', signed(data.speedOffsetApplied, 1));
  setText('diagNeedleTarget', (data.needleTargetSpeed ?? '--') + ' ' + dialUnit);
  setText('diagMotorDuty', (data.motorDutyApplied ?? '--') + ' (' + (Number(data.motorDutyApplied) / PWM_DUTY_MAX * 100).toFixed(1) + '%)');
  document.querySelectorAll('[data-speed-source]').forEach((el) =>
    el.classList.toggle('active', el.dataset.speedSource === data.activeSpeedSource));
  document.querySelectorAll('[data-rpm-source]').forEach((el) =>
    el.classList.toggle('active', el.dataset.rpmSource === data.activeRpmSource));
  if (isVoltageBoard) {
    setText('diagDrive', fixed(data.driveV, 2));
    setText('diagRail', fixed(data.railCmdV, 2) + ' / ' + fixed(data.motorRailV, 2) + ' V');
    setText('diagTachoHz', fixed(data.measuredFreqHz, 1));
    setText('diagTrim', signed(data.driveTrimPct, 1) + '%');
  }
}

// ===================================================================
// Voltage board (SpeedPulser V5 / Pro S3): Test Board
// ===================================================================
let boardTestWasRunning = false;
let boardTestPollTimer = null;

function renderBoardTestPill(code, running) {
  const pill = byId('boardTestPill');
  if (running) return setPill(pill, 'warn', 'Testing…');
  switch (Number(code)) {
    case 1: return setPill(pill, 'ok', 'Pass');
    case 3: case 4: case 5: return setPill(pill, 'bad', 'Fail');
    default: return setPill(pill, 'warn', 'Not run');
  }
}

async function loadBoardTest() {
  if (!isVoltageBoard) return null;
  const { ok, data } = await api('/api/boardtest');
  if (!ok) return null;
  renderBoardTestPill(data.code, data.running);
  const text = data.running ? 'Testing: motor stopped, stepping the rail…' : (data.text || '--');
  setText('boardTestText', text);
  setText('boardTestDetail', text);
  const ran = Number(data.code) >= 1 && Number(data.code) !== 2;
  byId('boardTestRows').innerHTML = (data.points || []).map((p, i) =>
    '<tr><td>' + (i + 1) + '</td><td>' + fixed(p.target, 1) + ' V</td><td>' +
    (ran ? fixed(p.measured, 2) + ' V' : '--') + '</td></tr>').join('');
  byId('boardTestBtn').disabled = !!data.running;
  return data;
}

function pollBoardTest() {
  if (boardTestPollTimer) clearTimeout(boardTestPollTimer);
  let misses = 0;
  const tick = async () => {
    const data = await loadBoardTest();
    if (data && data.running) { misses = 0; boardTestPollTimer = setTimeout(tick, 500); return; }
    if (!data && ++misses < 6) { boardTestPollTimer = setTimeout(tick, 500); return; } // ~3 s of no answer
    boardTestPollTimer = null;
    if (!data) {
      setPill(byId('boardTestPill'), 'bad', 'No answer');
      byId('boardTestBtn').disabled = false;
      return showNotification('The unit stopped answering during Test Board', 'error');
    }
    if (data) showNotification(data.text || 'Board test finished', data.pass ? 'success' : 'error');
    loadSettings();
  };
  boardTestPollTimer = setTimeout(tick, 500);
}

// ===================================================================
// Voltage board: Drive Calibration + Auto Fill
// ===================================================================
let driveMaxV = 12.0;
let calibrationDriveV = 0;          // the drive this page last set (V)
let calibrationDriveTouchedMs = 0;  // last local change: a status poll mustn't fight the user
let calibrationDriveBusy = false;   // a drive request is outstanding
let calibrationDrivePending = false;
let autoFillWasRunning = false;
let autoFillRailNote = '';

function setDriveRange(maxV) {
  if (!(Number(maxV) > 0)) return;
  driveMaxV = Number(maxV);
  byId('calibrationDriveBar').max = driveMaxV;
}

function showCalibrationDrive(volts) {
  calibrationDriveV = Math.round(Math.min(driveMaxV, Math.max(0, Number(volts) || 0)) * 100) / 100;
  byId('calibrationDriveBar').value = calibrationDriveV;
  setText('calibrationDriveValue', calibrationDriveV.toFixed(2) + ' V');
}

function setCalibrationDrive(volts, send) {
  showCalibrationDrive(volts);
  if (!send) return;
  calibrationDriveTouchedMs = Date.now();
  sendCalibrationDrive();
}

// One request in flight, always the latest value; the reply carries the drive the
// unit actually took (it raises at most 0.5 V per request).
function sendCalibrationDrive() {
  if (calibrationDriveBusy) { calibrationDrivePending = true; return; }
  calibrationDriveBusy = true;
  calibrationDrivePending = false;
  setSetting('calibrationDriveV', Math.round(calibrationDriveV * 100) / 100).then((result) => {
    calibrationDriveBusy = false;
    calibrationDriveTouchedMs = Date.now();
    if (calibrationDrivePending) sendCalibrationDrive();
    else if (result.ok) showCalibrationDrive(result.data.value);
  });
}

function renderDriveMarks(points, hzOk) {
  points = Array.isArray(points) ? points : [];
  byId('driveMarkRows').innerHTML = points.length ? points.map((p, i) =>
    '<tr><td>' + p.speed + ' ' + dialUnit + '</td>' +
    '<td><button class="pill needs-cal-mode' + (p.src === 'auto' ? '' : ' ok') + '" data-src-mark="' + i +
      '" data-auto="' + (p.src === 'auto' ? 1 : 0) + '" title="Tap to switch eye / auto">' +
      (p.src === 'auto' ? 'auto' : 'eye') + '</button></td>' +
    '<td>' + (Number(p.hz) > 0 ? fixed(p.hz, 1) : '--') + '</td>' +
    '<td>' + fixed(p.drive, 2) + ' V</td>' +
    '<td class="muted">' + fixed(p.rail, 1) + ' V × ' + Math.round(Number(p.pwm) * 100) + '%</td>' +
    '<td><button class="stepper-btn needs-cal-mode" data-delete-mark="' + i + '" title="Delete this mark">✕</button></td></tr>'
  ).join('') : '<tr><td colspan="6" class="muted">No marks yet.</td></tr>';
  applyCalibrationLocks();

  let summary = 'Not calibrated: the motor stays still until the top mark is captured.';
  if (points.length) {
    const eye = points.filter((p) => p.src !== 'auto').length;
    summary = points.length + (points.length === 1 ? ' mark, ' : ' marks, ') + points[0].speed + '–' +
      points[points.length - 1].speed + ' ' + dialUnit +
      (eye < points.length ? ' (' + eye + ' by eye, ' + (points.length - eye) + ' by Auto Fill).' : '.');
    if (points.length === 1) summary += ' Press Auto Fill to fill in the rest.';
    else if (eye === 1) summary += ' One mark by eye: a low one too makes the low end closer.';
    if (!hzOk) summary += ' No motor feedback readings, so the feedback trim and Motor speed are off.';
  }
  setText('driveCalSummary', summary);
}

function applyDriveTable(data) {
  settings.driveCal = data.driveCal;
  settings.driveCalHz = data.driveCalHz;
  renderDriveMarks(data.driveCal, data.driveCalHz);
  setRestoreAvailable(data.calBackup);
}

function setRestoreAvailable(available) {
  if (available !== undefined) {
    byId('restoreCalBtn').disabled = !available || autoFillWasRunning || !lastStatus.calibrationModeOn;
  }
}

function renderAutoFill(autoFill) {
  if (!autoFill) return;
  const state = Number(autoFill.state); // 0 idle, 1 running, 2 done, 3 failed
  const running = state === 1;
  let message = autoFill.msg || '';
  if (running && autoFill.total) {
    message = 'Auto Fill: mark ' + (Number(autoFill.index) + 1) + ' of ' + autoFill.total + ', ' +
      autoFill.speed + ' ' + dialUnit + ' (' + fixed(autoFill.hz, 1) + ' Hz). Hands off the drive.';
  }
  if (!running && autoFillRailNote && state !== 3) message = (message ? message + ' ' : '') + autoFillRailNote;
  const messageEl = byId('autoFillMessage');
  messageEl.textContent = message; // empty = the line takes no room
  messageEl.classList.toggle('warn', state === 3);
  byId('autoFillBtn').hidden = running;
  byId('autoFillStopBtn').hidden = !running;
  if (running) byId('restoreCalBtn').disabled = true;
  if (autoFillWasRunning && !running) {
    autoFillWasRunning = false;
    if (state === 2) showNotification(autoFill.msg || 'Auto Fill done');
    else if (state === 3) showNotification(autoFill.msg || 'Auto Fill stopped', 'error');
    loadSettings(); // the new table
  } else if (running) {
    autoFillWasRunning = true;
  }
}

function renderDriveCalibrationLive(data) {
  const turning = Number(data.fbHzSpread) >= 0;
  setText('driveReadout', fixed(data.driveV, 2) + ' V');
  setText('railSetReadout', fixed(data.railCmdV, 2) + ' V');
  const measured = byId('railMeasuredReadout');
  measured.textContent = fixed(data.motorRailV, 2) + ' V';
  measured.className = 'status-value ' + (data.railSenseOk ? '' : 'warn');
  setText('pwmReadout', Math.round(Number(data.pwmFrac) * 100) + '%');
  setText('motorHzReadout', fixed(data.measuredFreqHz, 1));
  const drift = Number(data.fbHzDrift) || 0;
  const steady = byId('steadyReadout');
  steady.textContent = turning ? (data.fbStable ? 'Steady' : 'Settling ' + (drift >= 0 ? '+' : '') + drift.toFixed(1) + ' Hz') : 'Stopped';
  steady.className = 'status-value ' + (turning ? (data.fbStable ? 'ok' : 'warn') : '');
  if (data.driveMaxV !== undefined) setDriveRange(data.driveMaxV);

  let hint = '';
  if (data.calibrationModeOn) {
    if (data.railLimited) hint = 'The rail can’t reach the set voltage: supply too low, or the motor is overloaded.';
    else if (data.autoFill && Number(data.autoFill.state) === 1) hint = '';
    else if (!turning) hint = 'Raise the drive until the needle moves.';
    else if (!data.fbStable) hint = 'Settling: wait for Steady.';
  }
  setText('calibrationHint', hint); // empty = the line takes no room
  if (!data.calibrationModeOn && calibrationDriveV !== 0) showCalibrationDrive(0); // nothing uses it outside the mode

  // Follow the unit's drive (Calibration Mode just started, Auto Fill) unless the user is adjusting it.
  if (!calibrationDriveBusy && !calibrationDrivePending && Date.now() - calibrationDriveTouchedMs > 1000 &&
      Math.abs(Number(data.calibrationDriveV) - calibrationDriveV) > 0.005) {
    showCalibrationDrive(data.calibrationDriveV);
  }
  renderAutoFill(data.autoFill);
  setRestoreAvailable(data.calBackup);
}

function initDriveCalibration() {
  document.querySelectorAll('[data-drive-step]').forEach((button) => button.addEventListener('click', () =>
    setCalibrationDrive(calibrationDriveV + Number(button.dataset.driveStep), true)));

  byId('captureMarkBtn').addEventListener('click', async () => {
    const speed = Number(byId('markSpeedInput').value);
    if (!(speed >= 1 && speed <= 400)) return showNotification('Enter the speed the needle is showing (1-400)', 'error');
    const result = await runAction('calAddPoint', { speed });
    if (!result.ok) return;
    applyDriveTable(result.data);
    showNotification('Captured ' + speed + ' ' + dialUnit + ' at ' + fixed(lastStatus.driveV, 2) + ' V drive');
  });

  byId('driveMarkRows').addEventListener('click', async (event) => {
    const source = event.target.closest('[data-src-mark]');
    if (source) {
      const result = await runAction('calSetSource', { index: Number(source.dataset.srcMark), auto: source.dataset.auto !== '1' });
      if (result.ok) applyDriveTable(result.data);
      return;
    }
    const button = event.target.closest('[data-delete-mark]');
    if (!button) return;
    const result = await runAction('calDeletePoint', { index: Number(button.dataset.deleteMark) });
    if (result.ok) applyDriveTable(result.data);
  });

  byId('clearMarksBtn').addEventListener('click', async () => {
    if (!confirm('Delete every mark?')) return;
    const result = await runAction('calClearPoints');
    if (result.ok) { applyDriveTable(result.data); showNotification('Marks cleared'); }
  });

  byId('autoFillBtn').addEventListener('click', async () => {
    // Say which marks it keeps: a table from before V5.05 loads with every mark "eye".
    const eye = (settings.driveCal || []).filter((p) => p.src !== 'auto').map((p) => p.speed);
    if (!confirm('Auto Fill keeps your eye marks (' + (eye.join(', ') || 'none') + ' ' + dialUnit + ') and measures ' +
                 'new auto marks around them, up to the top mark (1-2 minutes).\n\nNot all of those set by eye? ' +
                 'Cancel, and tap their eye pills to make them auto first.\n\nStopping it puts the current table back. Start?')) return;
    if ((await runAction('calAutoFill')).ok) { autoFillWasRunning = true; renderAutoFill({ state: 1, msg: 'Starting…' }); }
  });
  byId('autoFillStopBtn').addEventListener('click', () => runAction('calAutoFillStop'));
  byId('restoreCalBtn').addEventListener('click', async () => {
    if (!confirm('Put back the calibration from before the last Auto Fill? (The current one is kept, so you can swap back.)')) return;
    const result = await runAction('calRestore');
    if (result.ok) { applyDriveTable(result.data); showNotification('Previous calibration restored'); loadSettings(); }
  });
}

// ===================================================================
// Legacy board (SpeedPulser legacy / Pro DevKit): presets + Calibration Builder
// ===================================================================
const CAL_CHIP_MAX = 300;
let calibrationTargetSpeed = 10;
let calibrationChipsBuilt = false;
let calibrationPointCount = 0;
let jogBusy = false;

async function loadPresets() {
  const { ok, data } = await api('/api/calibrations');
  if (!ok || !Array.isArray(data.calibrations)) return false;
  const select = byId('presetSelect');
  // As text: the custom name comes from a typed name or an imported file.
  select.replaceChildren(...data.calibrations.map((c) => new Option(c.name, c.id)));
  if (data.selected !== undefined) select.value = data.selected;
  return true;
}

async function refreshCalibrationBuilder() {
  if (isVoltageBoard) return;
  const { ok, data } = await api('/api/cal');
  if (ok) applyCalibrationBuilderState(data);
}

function updateCalibrationDutyReadout(duty) {
  duty = Number(duty) || 0;
  setText('calDutyNow', duty);
  setText('calDutyPct', (duty / PWM_DUTY_MAX * 100).toFixed(1));
  setText('calCaptureDuty', duty);
}

function applyCalibrationBuilderState(state) {
  if (!state) return;
  updateCalibrationDutyReadout(state.duty);
  if (typeof state.clusterInMph === 'boolean' && state.clusterInMph !== !!settings.clusterInMph) {
    settings.clusterInMph = state.clusterInMph; // a custom cal captured in mph switches the dial to mph
    showSetting('clusterInMph', state.clusterInMph);
    applyDialUnit(state.clusterInMph);
  }
  const nameInput = byId('calName');
  if (document.activeElement !== nameInput) nameInput.value = state.name || '';
  buildCalibrationChips();
  const points = Array.isArray(state.points) ? state.points : [];
  calibrationPointCount = state.count ?? points.length;
  renderCalibrationPoints(points);
  scheduleChartDraw();
}

function buildCalibrationChips() {
  if (calibrationChipsBuilt) return;
  calibrationChipsBuilt = true;
  const grid = byId('calTargetChips');
  for (let speed = 10; speed <= CAL_CHIP_MAX; speed += 10) {
    const chip = document.createElement('button');
    chip.className = 'chip';
    chip.textContent = speed;
    chip.dataset.speed = speed;
    chip.addEventListener('click', () => setCalibrationTarget(speed, true));
    grid.appendChild(chip);
  }
}

function setCalibrationTarget(speed, fromChip) {
  calibrationTargetSpeed = speed;
  setText('calCaptureSpeed', speed);
  if (fromChip) byId('calTargetSpeed').value = speed;
  document.querySelectorAll('#calTargetChips .chip').forEach((c) =>
    c.classList.toggle('active', Number(c.dataset.speed) === speed));
}

function renderCalibrationPoints(points) {
  const list = byId('calPointsList');
  if (!points.length) { list.innerHTML = '<p class="hint">No points captured yet.</p>'; return; }
  list.innerHTML = '<div class="cal-point-head"><span>Speed</span><span>Duty</span><span></span></div>' +
    points.map((p, i) => '<div class="cal-point-row"><span>' + p.speed + '</span><span>' + p.duty +
      '</span><button class="cal-del needs-cal-mode" data-index="' + i + '">✕</button></div>').join('');
  applyCalibrationLocks();
}

// Press-and-hold: fires at once, then repeats faster while held.
function attachHold(element, action) {
  let timer = null;
  let delay = 320;
  const stop = () => { if (timer) { clearTimeout(timer); timer = null; } };
  element.addEventListener('pointerdown', (event) => {
    event.preventDefault();
    action();
    delay = 320;
    const repeat = () => { action(); delay = Math.max(60, delay * 0.8); timer = setTimeout(repeat, delay); };
    timer = setTimeout(repeat, delay);
  });
  ['pointerup', 'pointerleave', 'pointercancel'].forEach((name) => element.addEventListener(name, stop));
}

async function jogDuty(delta) {
  if (jogBusy) return; // one jog at a time: held buttons can't stack requests
  jogBusy = true;
  const result = await calibrationOp('jog', { delta });
  jogBusy = false;
  if (result.ok) updateCalibrationDutyReadout(result.data.duty);
}

function afterCustomCalibrationChange(result, message) {
  if (!result.ok) return;
  applyCalibrationBuilderState(result.data);
  loadPresets().then(() => {
    if (result.data.selected) { settings.presetId = CUSTOM_CAL_ID; byId('presetSelect').value = CUSTOM_CAL_ID; }
  });
  loadCalCurve();
  showNotification(message);
}

function initCalibrationBuilder() {
  document.querySelectorAll('.stepper-btn[data-jog]').forEach((button) =>
    attachHold(button, () => jogDuty(parseInt(button.dataset.jog, 10))));
  byId('calTargetSpeed').addEventListener('input', (event) =>
    setCalibrationTarget(parseInt(event.target.value, 10) || 0, false));
  byId('calCaptureBtn').addEventListener('click', async () => {
    const result = await calibrationOp('addPoint', { speed: calibrationTargetSpeed });
    if (!result.ok) return;
    applyCalibrationBuilderState(result.data);
    showNotification('Captured ' + calibrationTargetSpeed + ' @ duty ' + result.data.duty);
  });
  byId('calPointsList').addEventListener('click', async (event) => {
    const button = event.target.closest('.cal-del');
    if (!button) return;
    const result = await calibrationOp('deletePoint', { index: Number(button.dataset.index) });
    if (result.ok) applyCalibrationBuilderState(result.data);
  });
  byId('calName').addEventListener('change', (event) => calibrationOp('setName', { name: event.target.value }));
  byId('calSaveBtn').addEventListener('click', async () => {
    if (calibrationPointCount < 2) return showNotification('Capture at least 2 points first', 'error');
    const exists = [...byId('presetSelect').options].some((o) => Number(o.value) === CUSTOM_CAL_ID);
    if (exists && !confirm('A custom calibration is already saved on the unit. Overwrite it?')) return;
    // The name first (it is also sent on blur, which may still be in flight).
    await calibrationOp('setName', { name: byId('calName').value });
    afterCustomCalibrationChange(await calibrationOp('save'), 'Custom calibration saved and in use');
  });
  byId('calClearBtn').addEventListener('click', async () => {
    if (!confirm('Clear all captured calibration points?')) return;
    const result = await calibrationOp('clearPoints');
    if (result.ok) { applyCalibrationBuilderState(result.data); showNotification('Points cleared'); }
  });
}

// ===================================================================
// Export / import
// ===================================================================
function downloadTextFile(filename, text) {
  const url = URL.createObjectURL(new Blob([text], { type: 'text/plain' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  document.body.appendChild(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function calibrationFileName(suffix) {
  const name = (byId('calName').value.trim() || 'calibration').replace(/[^a-z0-9._-]+/gi, '_');
  return name + suffix;
}

function initExportImport() {
  const exportAs = (pick, fileName, message) => async () => {
    const result = await calibrationOp('export');
    if (!result.ok) return;
    const text = pick(result.data);
    if (!text) return;
    byId('calibrationText').value = text;
    downloadTextFile(fileName(), text);
    showNotification(message);
  };
  byId('exportVoltageBtn').addEventListener('click', exportAs((d) => {
    if (!d.voltageMarks) { showNotification('No marks yet: nothing to export', 'error'); return ''; }
    return d.voltage;
  }, () => (IS_PRO ? 'speedpulserpro' : 'speedpulser') + '_voltage-cal_' + new Date().toISOString().slice(0, 10) + '.txt', 'Calibration exported'));
  byId('exportPwmBtn').addEventListener('click', exportAs((d) => d.json,
    () => calibrationFileName('_pwm-cal.txt'), 'Calibration exported'));
  byId('exportCArrayBtn').addEventListener('click', exportAs((d) => d.carray,
    () => calibrationFileName('_pwm-carray.txt'), 'Exported as a firmware preset'));

  const fileInput = byId('importFile');
  byId('importBtn').addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', () => {
    const file = fileInput.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async () => {
      const text = String(reader.result || '').trim();
      byId('calibrationText').value = text;
      if (!text) return;
      // One importer for both kinds: the unit reads the file's "type".
      const result = await calibrationOp('import', { json: text });
      if (!result.ok) return;
      if (result.data.kind === 'voltage') {
        applyDriveTable(result.data);
        loadSettings(); // Nominal PWM / rails / units came with the file
        showNotification('Calibration imported (' + (result.data.driveCal || []).length + ' marks)');
      } else {
        afterCustomCalibrationChange(result, 'PWM calibration imported');
      }
    };
    reader.readAsText(file);
    fileInput.value = ''; // the same file can be picked again
  });
}

// ===================================================================
// Diagnostics: start-up log
// ===================================================================
function startupKindClass(kind) {
  switch (kind) {
    case 'powerOn': return 'ok';
    case 'software': return '';
    case 'brownout': case 'crash': case 'watchdog': return 'bad';
    default: return 'warn';
  }
}

async function loadStartupLog() {
  if (!byId('startupLogCard')) return;
  const { ok, data } = await api('/api/resetlog');
  if (!ok) return;
  setPill(byId('startupReasonPill'), startupKindClass(data.reasonKind), data.reason || '--');
  const counts = data.counts || {};
  setText('startupBoots', data.boots ?? '--');
  setText('startupShortRuns', data.shortRuns ?? '--');
  setText('startupBrownouts', counts.brownout ?? '--');
  setText('startupCrashes', (counts.crash ?? 0) + (counts.watchdog ?? 0));
  setText('startupDetail', (data.reasonDetail || '') + ' Running ' + (data.uptime ?? 0) + ' s.');
  const stableSecs = data.stableSecs ?? 10;
  const history = Array.isArray(data.history) ? data.history : [];
  byId('startupRows').innerHTML = history.length ? history.map((h) => {
    const cls = startupKindClass(h.kind);
    const ran = h.ranOk ? 'Yes' : (h.current ? 'Running…' : '<span class="pill bad">No (&lt;' + stableSecs + ' s)</span>');
    return '<tr><td>#' + h.boot + (h.current ? ' (now)' : '') + '</td><td><span class="pill' +
      (cls ? ' ' + cls : '') + '">' + h.reason + '</span></td><td>' + ran + '</td></tr>';
  }).join('') : '<tr><td colspan="3" class="muted">Nothing logged yet.</td></tr>';
}

function initStartupLog() {
  if (!byId('startupLogCard')) return;
  byId('startupRefreshBtn').addEventListener('click', loadStartupLog);
  byId('startupClearBtn').addEventListener('click', async () => {
    if (!confirm('Clear the start-up log? It restarts from this start.')) return;
    if ((await runAction('clearResetLog')).ok) showNotification('Start-up log cleared');
    loadStartupLog();
  });
}

// ===================================================================
// Dashboard chart: legacy calibration curve, or the voltage board's live strip chart
// ===================================================================
let calibrationCurve = null;   // /api/calcurve: { speed:[], duty:[], anchorSpeed:[], anchorDuty:[] }
let chartPoint = null;         // DevKit: { duty, speed } now
let chartDrawPending = false;
const CHART_HISTORY = 120;     // S3 board: 30 s at 250 ms
let chartHistory = [];         // [{ target, measured, drive, pwm }]
const CHART_COLOURS = { target: '#00D9FF', measured: '#2EA043', drive: '#FF6B35', pwm: '#A371F7' };

function legendItem(colour, label, kind, axis) {
  return '<span class="legend-item"><span class="legend-swatch ' + kind + '" style="--sw:' + colour + '"></span>' +
    label + (axis ? ' <small>(' + axis + ')</small>' : '') + '</span>';
}

function updateChartChrome() {
  setText('chartTitle', isVoltageBoard ? 'Live Control' : 'Calibration Curve');
  byId('chartLegend').innerHTML = isVoltageBoard
    ? legendItem(CHART_COLOURS.target, 'Needle target', 'dash', 'left') +
      legendItem(CHART_COLOURS.measured, 'Motor speed', 'line', 'left') +
      legendItem(CHART_COLOURS.drive, 'Drive', 'line', 'right %') +
      legendItem(CHART_COLOURS.pwm, 'PWM', 'line', 'right %')
    : legendItem(themeColour('--accent', '#24d1c4'), 'Calibration curve', 'line') +
      legendItem(themeColour('--ok', '#3fd07f'), 'Calibration points', 'dot') +
      legendItem(themeColour('--accent-2', '#ffb02e'), 'Now', 'dot');
  scheduleChartDraw();
}

function themeColour(name, fallback) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback;
}

async function loadCalCurve() {
  if (isVoltageBoard) return;
  const { ok, data } = await api('/api/calcurve');
  if (ok) calibrationCurve = data;
  scheduleChartDraw();
}

function setChartPoint(duty, speed) {
  chartPoint = { duty: Number(duty) || 0, speed: Number(speed) || 0 };
  scheduleChartDraw();
}

function pushChartSample(sample) {
  chartHistory.push(sample);
  if (chartHistory.length > CHART_HISTORY) chartHistory.shift();
  scheduleChartDraw();
}

// The curve's speed at a duty (DevKit Calibration Mode: the needle's speed isn't known).
function curveSpeedAt(duty) {
  if (!calibrationCurve || !Array.isArray(calibrationCurve.duty) || calibrationCurve.duty.length < 2) return 0;
  const d = calibrationCurve.duty, s = calibrationCurve.speed;
  if (duty <= d[0]) return s[0];
  for (let i = 1; i < d.length; i++) {
    if (duty <= d[i]) {
      const span = d[i] - d[i - 1];
      return Math.round(s[i - 1] + (span > 0 ? (duty - d[i - 1]) / span : 0) * (s[i] - s[i - 1]));
    }
  }
  return s[s.length - 1];
}

function scheduleChartDraw() {
  if (chartDrawPending) return;
  chartDrawPending = true;
  requestAnimationFrame(() => {
    chartDrawPending = false;
    if (isVoltageBoard) drawLiveChart(); else drawCalibrationCurve();
  });
}

// Size the canvas for the screen; null while its tab is hidden.
function prepareCanvas() {
  const canvas = byId('chartCanvas');
  if (!canvas || !canvas.clientWidth) return null;
  const dpr = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = Math.round(width * 0.6);
  canvas.style.height = height + 'px';
  if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  ctx.font = '10px -apple-system, "Segoe UI", Arial, sans-serif';
  return { ctx, width, height, line: themeColour('--line', '#24303b'), muted: themeColour('--muted', '#7d8ea0') };
}

function drawCalibrationCurve() {
  const c = prepareCanvas();
  if (!c) return;
  const { ctx, width, height } = c;
  const padL = 44, padR = 12, padT = 12, padB = 30;
  const plotW = width - padL - padR, plotH = height - padT - padB;
  const curve = calibrationCurve;
  let dutyMax = curve && curve.duty && curve.duty.length ? (curve.duty[curve.duty.length - 1] || 1) : 1;
  if (chartPoint && chartPoint.duty > dutyMax) dutyMax = chartPoint.duty;
  dutyMax = Math.max(1, Math.ceil(dutyMax / 100) * 100);
  let speedMax = dialMaxSpeed();
  if (chartPoint && chartPoint.speed > speedMax) speedMax = Math.ceil(chartPoint.speed / 20) * 20;
  const xOf = (duty) => padL + (Math.max(0, Math.min(duty, dutyMax)) / dutyMax) * plotW;
  const yOf = (speed) => padT + plotH - (Math.max(0, Math.min(speed, speedMax)) / speedMax) * plotH;

  ctx.strokeStyle = c.line;
  ctx.fillStyle = c.muted;
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  for (let i = 0; i <= 4; i++) {
    const speed = speedMax / 4 * i, y = yOf(speed);
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(width - padR, y); ctx.stroke();
    ctx.fillText(String(Math.round(speed)), padL - 6, y);
  }
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  for (let i = 0; i <= 5; i++) {
    const x = padL + plotW / 5 * i;
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, padT + plotH); ctx.stroke();
    ctx.fillText(String(Math.round(i / 5 * dutyMax)), x, padT + plotH + 6);
  }
  ctx.textBaseline = 'bottom';
  ctx.fillText('Motor duty', padL + plotW / 2, height);

  if (curve && curve.duty && curve.duty.length > 1) {
    ctx.strokeStyle = themeColour('--accent', '#24d1c4');
    ctx.globalAlpha = 0.6;
    ctx.lineWidth = 2;
    ctx.beginPath();
    curve.duty.forEach((duty, i) => { const x = xOf(duty), y = yOf(curve.speed[i]); if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y); });
    ctx.stroke();
    ctx.globalAlpha = 1;
    ctx.fillStyle = themeColour('--ok', '#3fd07f');
    (curve.anchorDuty || []).forEach((duty, i) => {
      ctx.beginPath(); ctx.arc(xOf(duty), yOf(curve.anchorSpeed[i]), 3.5, 0, Math.PI * 2); ctx.fill();
    });
  }
  if (chartPoint) {
    const x = xOf(chartPoint.duty), y = yOf(chartPoint.speed), colour = themeColour('--accent-2', '#ffb02e');
    ctx.strokeStyle = colour;
    ctx.globalAlpha = 0.5;
    ctx.setLineDash([4, 4]);
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(x, y); ctx.lineTo(x, padT + plotH); ctx.stroke();
    ctx.setLineDash([]);
    ctx.globalAlpha = 1;
    ctx.fillStyle = colour;
    ctx.beginPath(); ctx.arc(x, y, 5, 0, Math.PI * 2); ctx.fill();
    ctx.font = '11px -apple-system, "Segoe UI", Arial, sans-serif';
    const right = x > padL + plotW * 0.7;
    ctx.textAlign = right ? 'right' : 'left';
    ctx.fillText(chartPoint.speed + ' ' + dialUnit + ' @ ' + Math.round(chartPoint.duty), right ? x - 8 : x + 8, y - 6);
  }
}

function drawLiveChart() {
  const c = prepareCanvas();
  if (!c) return;
  const { ctx, width, height } = c;
  const padL = 40, padR = 40, padT = 12, padB = 22;
  const plotW = width - padL - padR, plotH = height - padT - padB;
  let speedMax = 20;
  chartHistory.forEach((s) => { speedMax = Math.max(speedMax, s.target || 0, s.measured || 0); });
  speedMax = Math.ceil(speedMax / 20) * 20;
  const xOf = (i) => padL + (i / (CHART_HISTORY - 1)) * plotW;
  const ySpeed = (v) => padT + plotH - (Math.max(0, Math.min(v, speedMax)) / speedMax) * plotH;
  const yPercent = (v) => padT + plotH - (Math.max(0, Math.min(v, 100)) / 100) * plotH;

  ctx.strokeStyle = c.line;
  ctx.fillStyle = c.muted;
  ctx.textBaseline = 'middle';
  for (let i = 0; i <= 4; i++) {
    const y = padT + plotH / 4 * i;
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(width - padR, y); ctx.stroke();
    ctx.textAlign = 'right';
    ctx.fillText(String(Math.round(speedMax * (1 - i / 4))), padL - 6, y);
    ctx.textAlign = 'left';
    ctx.fillText(Math.round(100 * (1 - i / 4)) + '%', width - padR + 6, y);
  }
  ctx.textAlign = 'center';
  ctx.textBaseline = 'bottom';
  ctx.fillText('time →  (' + dialUnit + ' left)', padL + plotW / 2, height);
  if (!chartHistory.length) return;

  const base = CHART_HISTORY - chartHistory.length; // newest sample at the right edge
  const plot = (key, colour, yMap, dashed) => {
    ctx.strokeStyle = colour;
    ctx.lineWidth = 2;
    ctx.setLineDash(dashed ? [4, 3] : []);
    ctx.beginPath();
    let drawing = false;
    chartHistory.forEach((sample, j) => {
      const v = sample[key];
      if (v === null || v === undefined || Number.isNaN(v)) { drawing = false; return; }
      if (drawing) ctx.lineTo(xOf(base + j), yMap(v)); else { ctx.moveTo(xOf(base + j), yMap(v)); drawing = true; }
    });
    ctx.stroke();
    ctx.setLineDash([]);
  };
  plot('drive', CHART_COLOURS.drive, yPercent, false);
  plot('pwm', CHART_COLOURS.pwm, yPercent, false);
  plot('target', CHART_COLOURS.target, ySpeed, true);
  plot('measured', CHART_COLOURS.measured, ySpeed, false);
}

// ===================================================================
// Dashboard dial gauges (Display Options)
// ===================================================================
const GAUGE_TILES = [
  { id: 'liveInputSpeed', label: 'Speed', unit: () => 'km/h', max: () => Number(settings.maxSpeed) || 200 },
  { id: 'liveNeedleTarget', label: 'Needle target', unit: () => dialUnit, max: dialMaxSpeed },
  { id: 'liveRpm', label: 'RPM', unit: () => 'rpm', max: () => Number(settings.clusterRPMLimit) || 7000 },
  { id: 'liveMeasuredSpeed', label: 'Motor speed', unit: () => dialUnit, max: dialMaxSpeed, voltageOnly: true },
];
// The tiles this page has (SpeedPulser has no RPM tile), and Motor speed only on a
// board with motor feedback (it was offered, and ticked by default, on legacy boards).
const gaugeTiles = () => GAUGE_TILES.filter((tile) => byId(tile.id) && (!tile.voltageOnly || isVoltageBoard));
const GAUGE_RADIUS = 40;
const GAUGE_CIRCUMFERENCE = 2 * Math.PI * GAUGE_RADIUS;
const GAUGE_ARC = GAUGE_CIRCUMFERENCE * 0.75;

function dialMaxSpeed() {
  const kph = Number(settings.maxSpeed) || 200;
  return settings.clusterInMph ? Math.round(kph * 0.621371) : kph;
}

function initGaugeOptions() {
  const prefs = loadPrefs();
  const chosen = new Set(prefs.gauges || ['liveInputSpeed', byId('liveRpm') ? 'liveRpm' : 'liveMeasuredSpeed']);
  const host = byId('gaugeOptions');
  gaugeTiles().forEach((tile) => {
    const label = document.createElement('label');
    label.className = 'tile-opt';
    label.innerHTML = '<input type="checkbox"' + (chosen.has(tile.id) ? ' checked' : '') + '><span>' + tile.label + '</span>';
    label.querySelector('input').addEventListener('change', (event) => {
      if (event.target.checked) chosen.add(tile.id); else chosen.delete(tile.id);
      const p = loadPrefs();
      p.gauges = [...chosen];
      savePrefs(p);
      applyGaugeChoice(chosen);
    });
    host.appendChild(label);
  });
  applyGaugeChoice(chosen);
}

function applyGaugeChoice(chosen) {
  gaugeTiles().forEach((tile) => {
    const box = byId(tile.id).closest('.gauge');
    if (!box.querySelector('.tile-gauge')) {
      const wrap = document.createElement('div');
      wrap.className = 'tile-gauge';
      wrap.innerHTML = '<svg viewBox="0 0 100 100" aria-hidden="true">' +
        '<circle class="tg-track" cx="50" cy="50" r="' + GAUGE_RADIUS + '" transform="rotate(135 50 50)" stroke-dasharray="' +
        GAUGE_ARC.toFixed(2) + ' ' + (GAUGE_CIRCUMFERENCE - GAUGE_ARC).toFixed(2) + '"/>' +
        '<circle class="tg-fill" cx="50" cy="50" r="' + GAUGE_RADIUS + '" transform="rotate(135 50 50)" stroke-dasharray="0 ' +
        GAUGE_CIRCUMFERENCE.toFixed(2) + '"/>' +
        '<text class="tg-val" x="50" y="52" text-anchor="middle">--</text>' +
        '<text class="tg-unit" x="50" y="66" text-anchor="middle"></text>' +
        '<text class="tg-min" x="24" y="92" text-anchor="middle">0</text>' +
        '<text class="tg-max" x="76" y="92" text-anchor="middle">0</text></svg>';
      box.appendChild(wrap);
    }
    box.classList.toggle('as-gauge', chosen.has(tile.id));
  });
  applyGaugeRanges();
}

function applyGaugeRanges() {
  gaugeTiles().forEach((tile) => {
    const box = byId(tile.id).closest('.gauge');
    const unit = box.querySelector('.tg-unit'), max = box.querySelector('.tg-max');
    if (unit) unit.textContent = tile.unit();
    if (max) max.textContent = tile.max();
  });
  updateGauges();
}

function updateGauges() {
  gaugeTiles().forEach((tile) => {
    const value = byId(tile.id);
    const box = value.closest('.gauge');
    if (!box.classList.contains('as-gauge')) return;
    const number = parseFloat(value.textContent);
    const valueText = box.querySelector('.tg-val'), fill = box.querySelector('.tg-fill');
    if (!valueText || !fill) return;
    valueText.textContent = Number.isNaN(number) ? '--' : value.textContent;
    const fraction = Number.isNaN(number) ? 0 : Math.max(0, Math.min(1, number / (tile.max() || 1)));
    fill.style.strokeDasharray = (GAUGE_ARC * fraction).toFixed(2) + ' ' + GAUGE_CIRCUMFERENCE.toFixed(2);
  });
}
