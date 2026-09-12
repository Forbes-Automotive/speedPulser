document.addEventListener('DOMContentLoaded', initApp);

let settingsLoaded = false;
let statusPollTimer = null;
let fetchStatusInFlight = false;
let tempSpeedPushTimer = null;
let speedTestActive = false;
const STATUS_POLL_MS = 75;
const TEST_STATUS_POLL_MS = 20;
const TEMP_SPEED_PUSH_MS = 40;

// Tab navigation
function initApp() {
  initNavigation();
  initControls();
  initVoltageControls();
  initOta();
  initCollapsibleCards();
  initGaugeUI();
  initCalBuilder();
  fetchCalibrations().then(fetchSettings).then(refreshCalState).then(fetchCalCurve);  // options, settings, cal builder, curve
  startStatusPolling();
  window.addEventListener('resize', scheduleCalDraw);
}

function startStatusPolling() {
  if (statusPollTimer) {
    clearTimeout(statusPollTimer);
  }

  const poll = async () => {
    const delay = await fetchStatus();
    statusPollTimer = setTimeout(poll, delay);
  };

  poll();
}

function queueTempSpeedUpdate(value) {
  if (tempSpeedPushTimer) {
    clearTimeout(tempSpeedPushTimer);
  }

  tempSpeedPushTimer = setTimeout(() => {
    pushControl('tempSpeed', value);
    tempSpeedPushTimer = null;
  }, TEMP_SPEED_PUSH_MS);
}

function updateSliderDisplay(id, value) {
  const displayEl = document.getElementById(id + '-display');
  if (displayEl) {
    displayEl.textContent = value;
  }
}

function updateDashboard(speedValue, dutyValue, isTestMode) {
  const incomingSpeedEl = document.getElementById('incomingSpeed');
  const incomingSpeedLabelEl = document.getElementById('incomingSpeedLabel');
  const motorDutyEl = document.getElementById('motorDuty');
  const motorDutyLabelEl = document.getElementById('motorDutyLabel');
  const dutyRaw = dutyValue ?? 0;

  if (isTestMode) {
    incomingSpeedLabelEl.textContent = 'Chosen Test Speed';
    motorDutyLabelEl.textContent = 'Resulting Motor Duty';
    incomingSpeedEl.textContent = speedValue ?? 0;
    motorDutyEl.textContent = dutyRaw;
    incomingSpeedEl.classList.add('test-active');
    motorDutyEl.classList.add('test-active');
    updateTileGauges();
    return;
  }

  incomingSpeedLabelEl.textContent = 'Incoming Speed';
  motorDutyLabelEl.textContent = 'Motor Duty';
  incomingSpeedEl.textContent = speedValue ?? '--';
  motorDutyEl.textContent = dutyRaw;
  incomingSpeedEl.classList.remove('test-active');
  motorDutyEl.classList.remove('test-active');
  updateTileGauges();
}

function updateSpeedOffsetStatus(mode, offsetValue) {
  const offsetTypeEl = document.getElementById('speedOffsetType');
  const currentOffsetEl = document.getElementById('currentSpeedOffset');

  if (offsetTypeEl) {
    offsetTypeEl.textContent = mode || '--';
  }

  if (currentOffsetEl) {
    if (offsetValue === undefined || offsetValue === null || Number.isNaN(Number(offsetValue))) {
      currentOffsetEl.textContent = '--';
    } else {
      const n = Number(offsetValue);
      currentOffsetEl.textContent = (n > 0 ? '+' : '') + n;
    }
  }
}

// Show the global offset slider OR the 5-point curve, never both.
function applyOffsetCurveVisibility(enabled) {
  const globalSec = document.getElementById('globalOffsetSection');
  const curveSec = document.getElementById('offsetCurveSection');
  if (globalSec) globalSec.style.display = enabled ? 'none' : '';
  if (curveSec) curveSec.style.display = enabled ? '' : 'none';
}

async function fetchCalibrations() {
  try {
    const response = await fetch('/api/calibrations');
    const data = await response.json();
    const selectEl = document.getElementById('motorCalSelection');

    if (!selectEl || !Array.isArray(data.calibrations)) {
      return;
    }

    selectEl.innerHTML = '';
    data.calibrations.forEach((cal) => {
      const option = document.createElement('option');
      option.value = cal.id;
      option.textContent = cal.name;
      selectEl.appendChild(option);
    });
  } catch (error) {
    console.log('Error fetching calibrations:', error);
  }
}

function initNavigation() {
  const tabs = document.querySelectorAll(".nav-tab");
  const pages = document.querySelectorAll(".page");

  tabs.forEach((tab) => {
    tab.addEventListener("click", () => {
      const page = tab.dataset.page;

      tabs.forEach((t) => t.classList.remove("active"));
      tab.classList.add("active");

      pages.forEach((p) => p.classList.remove("active"));
      document.getElementById(`${page}-page`).classList.add("active");

      // The curve canvas can't measure itself while its tab is hidden, so
      // redraw once the Dashboard page becomes visible.
      if (page === 'dashboard') scheduleCalDraw();
    });
  });
}

function initControls() {
  // Dashboard action buttons
  const testNeedleSweepBtn = document.getElementById('testNeedleSweep');
  if (testNeedleSweepBtn) {
    testNeedleSweepBtn.addEventListener('click', () => pushAction('needleSweep'));
  }

  // Configuration controls
  const configInputs = ['hasNeedleSweep', 'sweepSpeed', 'motorCalSelection', 'maxSpeed', 'maxFreqHall', 'speedOffsetPositive', 'speedOffset', 'convertToMPH', 'averageFilter', 'reverseDirection', 'feedbackEnable', 'feedbackMinSpeed', 'pidKp', 'pidKi', 'pidKd', 'feedbackDeadband'];
  configInputs.forEach(id => {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener('change', () => {
        const value = el.type === 'checkbox' ? el.checked : el.value;
        const ctrlPromise = pushControl(id, value);

        if (id === 'motorCalSelection') {
          const selectedOption = el.options[el.selectedIndex];
          if (selectedOption) {
            document.getElementById('calibrationStatus').textContent = `Cal: ${selectedOption.textContent}`;
          }
          // Refresh the curve only after the device has applied the new cal.
          if (ctrlPromise && ctrlPromise.then) ctrlPromise.then(fetchCalCurve); else fetchCalCurve();
        }

        if (id === 'convertToMPH') {
          applyMphState(el.checked);
        }
      });
      // For sliders, also update live display
      if (el.type === 'range') {
        el.addEventListener('input', () => {
          updateSliderDisplay(id, el.value);

          // Sweep rate applies live so an in-progress sweep reacts while dragging.
          if (id === 'sweepSpeed') {
            pushControl('sweepSpeed', el.value);
          }
        });
      }
    }
  });

  // Calibration Builder's own "Cluster in MPH" toggle mirrors the main setting.
  const calMph = document.getElementById('calConvertToMPH');
  if (calMph) {
    calMph.addEventListener('change', () => {
      applyMphState(calMph.checked);
      pushControl('convertToMPH', calMph.checked);
    });
  }

  const curveInputs = ['useSpeedOffsetCurve', 'curveOffset0', 'curveOffset1', 'curveOffset2', 'curveOffset3', 'curveOffset4'];
  curveInputs.forEach(id => {
    const el = document.getElementById(id);
    if (!el) {
      return;
    }

    el.addEventListener('change', () => {
      const value = el.type === 'checkbox' ? el.checked : el.value;
      pushControl(id, value);
      if (id === 'useSpeedOffsetCurve') {
        applyOffsetCurveVisibility(el.checked);
      }
    });

    if (el.type === 'range') {
      el.addEventListener('input', () => {
        updateSliderDisplay(id, el.value);
        pushControl(id, el.value);
      });
    }
  });

  // Advanced test controls
  const advancedInputs = ['testSpeedo', 'tempSpeed'];
  advancedInputs.forEach(id => {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener('change', () => {
        const value = el.type === 'checkbox' ? el.checked : el.value;
        if (id === 'testSpeedo') {
          speedTestActive = !!value;
          if (!speedTestActive) {
            const tempSpeedEl = document.getElementById('tempSpeed');
            const tempSpeedDisplayEl = document.getElementById('tempSpeed-display');
            if (tempSpeedEl && tempSpeedDisplayEl) {
              tempSpeedEl.value = 0;
              tempSpeedDisplayEl.textContent = '0';
            }
            updateDashboard(0, 0, false);
            pushControl('tempSpeed', 0);
          }
        }
        pushControl(id, value);
      });
      // For sliders, also update live display
      if (el.type === 'range') {
        el.addEventListener('input', () => {
          updateSliderDisplay(id, el.value);

          // Push test-speed updates immediately while dragging
          if (id === 'tempSpeed') {
            updateDashboard(el.value, null, true);
            pushControl('tempSpeed', el.value);
          }
        });
      }
    }
  });

  // Calibration controls
  let calModePrevFeedback = false;   // restore PID state when leaving cal mode
  const testCalEl = document.getElementById('testCal');
  if (testCalEl) {
    testCalEl.addEventListener('change', () => {
      const value = testCalEl.checked;
      pushControl('testCal', value);
      // Cal mode jogs the motor open-loop, so force PID off while it's on and
      // restore it afterwards if it was originally on.
      const fbEl = document.getElementById('feedbackEnable');
      if (fbEl) {
        if (value) {
          calModePrevFeedback = fbEl.checked;
          if (fbEl.checked) {
            fbEl.checked = false;
            pushControl('feedbackEnable', false);
          }
        } else if (calModePrevFeedback) {
          fbEl.checked = true;
          pushControl('feedbackEnable', true);
          calModePrevFeedback = false;
        }
      }
    });
  }

  const resetPidBtn = document.getElementById('resetPidDefaults');
  if (resetPidBtn) {
    resetPidBtn.addEventListener('click', () => {
      const defs = { pidKp: 0.15, pidKi: 1.3, pidKd: 0, feedbackDeadband: 1.5 };
      Object.entries(defs).forEach(([id, val]) => {
        const el = document.getElementById(id);
        if (el) {
          el.value = val;
          updateSliderDisplay(id, val);
          pushControl(id, val);
        }
      });
      showNotification('PID defaults restored (Kp 0.15, Ki 1.3, Kd 0, Deadband 1.5 Hz)');
    });
  }

  const calPrevBtn = document.getElementById('calPrevious');
  if (calPrevBtn) {
    calPrevBtn.addEventListener('click', () => pushAction('calPrevious'));
  }

  const calNextBtn = document.getElementById('calNext');
  if (calNextBtn) {
    calNextBtn.addEventListener('click', () => pushAction('calNext'));
  }

  const resetSpeedLimitsBtn = document.getElementById('resetSpeedLimits');
  if (resetSpeedLimitsBtn) {
    resetSpeedLimitsBtn.addEventListener('click', () => {
      const maxSpeedEl = document.getElementById('maxSpeed');
      const maxFreqHallEl = document.getElementById('maxFreqHall');
      const maxSpeedDisplayEl = document.getElementById('maxSpeed-display');
      const maxFreqHallDisplayEl = document.getElementById('maxFreqHall-display');

      if (maxSpeedEl && maxFreqHallEl && maxSpeedDisplayEl && maxFreqHallDisplayEl) {
        maxSpeedEl.value = 200;
        maxFreqHallEl.value = 200;
        maxSpeedDisplayEl.textContent = '200';
        maxFreqHallDisplayEl.textContent = '200';

        pushControl('maxSpeed', 200);
        pushControl('maxFreqHall', 200);
      }
    });
  }
}


// ===== VOLTAGE CONTROL (V4 board) =====
// Set a slider's value and its live "-display" span in one call.
function setSlider(id, val) {
  const el = document.getElementById(id);
  if (!el) return;
  el.value = val;
  updateSliderDisplay(id, val);
}

// Show the V4 voltage-control cards on new boards, and the legacy multi-point
// Calibration Builder only on boards without voltage control.
function applyBoardVisibility(isV4) {
  const show = (id, on) => {
    const el = document.getElementById(id);
    if (el) el.style.display = on ? '' : 'none';
  };
  show('voltageCalCard', isV4);
  show('voltageControlCard', isV4);
  show('calBuilderCard', !isV4);
  boardHasVc = isV4;
  updateDashChartChrome(isV4);
  scheduleCalDraw();
}

function initVoltageControls() {
  // Tuning sliders / toggle that map 1:1 to firmware control keys.
  const vcInputs = ['voltageControlEnable', 'vcPwmNominal', 'vcPwmMin', 'vcVoltMin',
                    'vcVoltMax', 'vcVoltGain', 'vcKp', 'vcKi', 'vcKd'];
  vcInputs.forEach(id => {
    const el = document.getElementById(id);
    if (!el) return;
    el.addEventListener('change', () => {
      const value = el.type === 'checkbox' ? el.checked : el.value;
      pushControl(id, value);
    });
    if (el.type === 'range') {
      el.addEventListener('input', () => {
        updateSliderDisplay(id, el.value);
        pushControl(id, el.value);
      });
    }
  });

  // Voltage lift is shown as 0–100 % but the firmware wants a 0..1 fraction.
  const voltEl = document.getElementById('tempVoltageCmd');
  if (voltEl) {
    voltEl.addEventListener('input', () => {
      updateSliderDisplay('tempVoltageCmd', voltEl.value);
      pushControl('tempVoltageCmd', (Number(voltEl.value) / 100).toFixed(3));
    });
  }

  // PWM duty drives tempDutyCycle through the cal builder's setDuty op.
  const dutyEl = document.getElementById('vcDuty');
  if (dutyEl) {
    dutyEl.addEventListener('input', () => {
      updateSliderDisplay('vcDuty', dutyEl.value);
      calPost({ op: 'setDuty', duty: Number(dutyEl.value) }).catch(() => {});
    });
  }

  // V4 calibration-mode toggle mirrors the legacy testCal flag.
  const vcCal = document.getElementById('vcTestCal');
  if (vcCal) {
    vcCal.addEventListener('change', () => pushControl('testCal', vcCal.checked));
  }

  const captureBtn = document.getElementById('captureTopSpeedBtn');
  if (captureBtn) {
    captureBtn.addEventListener('click', () => {
      fetch('/api/action', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'captureTopSpeed' })
      }).then(async (r) => {
        if (r.ok) {
          showNotification('Top speed captured');
          fetchSettings();
          fetchCalCurve();
        } else {
          const e = await r.json().catch(() => ({}));
          showNotification(e.error || 'Capture failed — raise voltage/PWM', 'error');
        }
      }).catch(() => showNotification('Capture failed', 'error'));
    });
  }
}


function applySpeedUnitLabels(useMPH) {
  const label = useMPH ? 'MPH' : 'KMH';
  const ids = ['speedUnit', 'speedOffsetUnit', 'measuredSpeedUnit'];
  ids.forEach(id => {
    const el = document.getElementById(id);
    if (el) el.textContent = label;
  });
}


// Keep the two "Cluster in MPH" toggles (Configuration + Calibration Builder)
// and all unit labels in lock-step from a single source of truth.
function applyMphState(useMPH) {
  const main = document.getElementById('convertToMPH');
  const cal = document.getElementById('calConvertToMPH');
  if (main) main.checked = useMPH;
  if (cal) cal.checked = useMPH;
  applySpeedUnitLabels(useMPH);
  const unitEl = document.getElementById('calUnitLabel');
  if (unitEl) unitEl.textContent = useMPH ? 'mph' : 'km/h';
}


async function fetchSettings() {
  try {
    const response = await fetch('/api/settings');
    const data = await response.json();

    // Load all settings from API
    document.getElementById('hasNeedleSweep').checked = data.hasNeedleSweep || false;    document.getElementById('sweepSpeed').value = data.sweepSpeed || 18;
    document.getElementById('sweepSpeed-display').textContent = data.sweepSpeed || 18;

    document.getElementById('motorCalSelection').value = data.motorPerformanceVal || 1;
    document.getElementById('motorCalSelection').dispatchEvent(new Event('change'));
    document.getElementById('calibrationStatus').textContent = 'Cal: ' + (data.calibrationText || '--');

    document.getElementById('maxSpeed').value = data.maxSpeed || 200;
    document.getElementById('maxSpeed-display').textContent = data.maxSpeed || 200;

    document.getElementById('maxFreqHall').value = data.maxFreqHall || 200;
    document.getElementById('maxFreqHall-display').textContent = data.maxFreqHall || 200;

    document.getElementById('speedOffsetPositive').checked = data.speedOffsetPositive !== false;
    document.getElementById('speedOffset').value = data.speedOffset || 0;
    document.getElementById('speedOffset-display').textContent = data.speedOffset || 0;
    document.getElementById('convertToMPH').checked = data.convertToMPH || false;
    applyMphState(!!data.convertToMPH);

    document.getElementById('useSpeedOffsetCurve').checked = data.useSpeedOffsetCurve || false;
    applyOffsetCurveVisibility(!!data.useSpeedOffsetCurve);

    const curveOffsets = Array.isArray(data.speedOffsetCurveOffsets) ? data.speedOffsetCurveOffsets : [0, 0, 0, 0, 0];

    for (let i = 0; i < 5; i++) {
      const offsetId = 'curveOffset' + i;
      const offsetVal = curveOffsets[i] ?? 0;

      document.getElementById(offsetId).value = offsetVal;
      document.getElementById(offsetId + '-display').textContent = offsetVal;
    }

    document.getElementById('testSpeedo').checked = data.testSpeedo || false;
    speedTestActive = !!data.testSpeedo;
    document.getElementById('tempSpeed').value = data.tempSpeed || 0;
    document.getElementById('tempSpeed-display').textContent = data.tempSpeed || 0;

    document.getElementById('testCal').checked = data.testCal || false;

    document.getElementById('averageFilter').value = data.averageFilter || 6;
    document.getElementById('averageFilter-display').textContent = data.averageFilter || 6;

    document.getElementById('reverseDirection').checked = data.reverseDirection || false;
    document.getElementById('feedbackEnable').checked = data.feedbackEnable || false;
    document.getElementById('feedbackMinSpeed').value = data.feedbackMinSpeed ?? 40;
    document.getElementById('feedbackMinSpeed-display').textContent = data.feedbackMinSpeed ?? 40;
    document.getElementById('pidKp').value = data.pidKp ?? 0.15;
    document.getElementById('pidKp-display').textContent = data.pidKp ?? 0.15;
    document.getElementById('pidKi').value = data.pidKi ?? 1.3;
    document.getElementById('pidKi-display').textContent = data.pidKi ?? 1.3;
    document.getElementById('pidKd').value = data.pidKd ?? 0;
    document.getElementById('pidKd-display').textContent = data.pidKd ?? 0;
    document.getElementById('feedbackDeadband').value = data.feedbackDeadband ?? 1.5;
    document.getElementById('feedbackDeadband-display').textContent = data.feedbackDeadband ?? 1.5;

    // Voltage control (V4 board only)
    applyBoardVisibility(!!data.boardHasVoltageControl);
    const vcEnableEl = document.getElementById('voltageControlEnable');
    if (vcEnableEl) vcEnableEl.checked = data.voltageControlEnable !== false;
    setSlider('vcPwmNominal', data.vcPwmNominal ?? 0.70);
    setSlider('vcPwmMin', data.vcPwmMin ?? 0.15);
    setSlider('vcVoltMin', data.vcVoltMin ?? 0.15);
    setSlider('vcVoltMax', data.vcVoltMax ?? 1.00);
    setSlider('vcVoltGain', data.vcVoltGain ?? 0.50);
    setSlider('vcKp', data.vcKp ?? 1.50);
    setSlider('vcKi', data.vcKi ?? 2.00);
    setSlider('vcKd', data.vcKd ?? 0.00);
    const vcMaxLabel = document.getElementById('vcMaxSpeedLabel');
    if (vcMaxLabel) vcMaxLabel.textContent = data.maxSpeed ?? '--';
    const vcCalEl = document.getElementById('vcTestCal');
    if (vcCalEl) vcCalEl.checked = data.testCal || false;

    // Update firmware info on OTA page
    try {
      const verResponse = await fetch('/api/ota/info');
      const verData = await verResponse.json();
      document.getElementById('otaFwVersion').textContent = verData.version || '--';
      document.getElementById('otaHardware').textContent = verData.hardware || '--';
      document.getElementById('otaBoard').textContent = verData.board || '--';
    } catch (e) {
      document.getElementById('otaFwVersion').textContent = '--';
      document.getElementById('otaHardware').textContent = '--';
      document.getElementById('otaBoard').textContent = '--';
    }

    settingsLoaded = true;
  } catch (error) {
    console.log('Error fetching settings:', error);
  }
}

async function fetchStatus() {
  if (fetchStatusInFlight) {
    return speedTestActive ? TEST_STATUS_POLL_MS : STATUS_POLL_MS;
  }

  fetchStatusInFlight = true;

  try {
    const endpoint = speedTestActive ? '/api/test-status' : '/api/status';
    const response = await fetch(endpoint);
    const data = await response.json();

    // Feedback readouts have three states:
    //   available -> show the measured value / PID trim
    //   missing   -> motor running but no feedback signal (legacy PCB): show "N/A"
    //   unknown   -> not seen yet and motor idle: leave "--"
    const fbValue = (numeric) => {
      if (data.feedbackAvailable) return numeric;
      if (data.feedbackMissing) return 'N/A';
      return '--';
    };

    const measuredEl = document.getElementById('measuredSpeed');
    if (measuredEl) measuredEl.textContent = fbValue(data.measuredSpeed ?? '--');
    const measuredAdvEl = document.getElementById('measuredSpeedAdv');
    if (measuredAdvEl) measuredAdvEl.textContent = fbValue(data.measuredSpeed ?? '--');

    const pidNumeric = (data.pidCorrection !== undefined)
      ? ((Number(data.pidCorrection) > 0 ? '+' : '') + Number(data.pidCorrection))
      : '--';
    const pidText = fbValue(pidNumeric);
    const pidEl = document.getElementById('pidCorrection');
    if (pidEl) pidEl.textContent = pidText;
    const pidAdvEl = document.getElementById('pidCorrectionAdv');
    if (pidAdvEl) pidAdvEl.textContent = pidText;

    const freqEl = document.getElementById('measuredFreqRaw');
    if (freqEl && data.measuredFreqRawHz !== undefined) {
      freqEl.textContent = Number(data.measuredFreqRawHz).toFixed(1);
    }

    // Buck / voltage-control live readouts (V4 board)
    if (data.boardHasVoltageControl !== undefined) {
      const setTxt = (id, v) => { const el = document.getElementById(id); if (el) el.textContent = v; };
      setTxt('buckEnabledVal', data.buckEnabled ? 'Yes' : 'No');
      setTxt('voltageCmdVal', data.voltageCmd !== undefined ? Number(data.voltageCmd).toFixed(2) : '--');
      setTxt('pwmFracVal', data.pwmFrac !== undefined ? Number(data.pwmFrac).toFixed(2) : '--');

      if (data.boardHasVoltageControl) {
        const target = data.testSpeedo ? Number(data.tempSpeed) : Number(data.incomingSpeed);
        pushVcSample({
          target: Number.isFinite(target) ? target : null,
          measured: data.feedbackAvailable ? Number(data.measuredSpeed) : null,
          voltage: data.voltageCmd !== undefined ? Number(data.voltageCmd) * 100 : null,
          pwm: data.pwmFrac !== undefined ? Number(data.pwmFrac) * 100 : null
        });
        scheduleCalDraw();
      }
    }

    speedTestActive = !!data.testSpeedo;

    if (data.motorPerformanceVal !== undefined) {
      document.getElementById('motorPerformanceVal').textContent = data.motorPerformanceVal;
    }
    if (data.calibrationText !== undefined) {
      document.getElementById('calibrationStatus').textContent = 'Cal: ' + (data.calibrationText || '--');
    }

    updateSpeedOffsetStatus(data.speedOffsetType, data.currentSpeedOffset);

    if (speedTestActive) {
      updateDashboard(data.tempSpeed, data.appliedDutyCycle, true);
      setCalCurrentPoint(data.appliedDutyCycle, data.tempSpeed);
      return TEST_STATUS_POLL_MS;
    }

    updateDashboard(data.incomingSpeed, data.appliedDutyCycle, false);

    // Advanced status monitor
    document.getElementById('motorPerformanceVal').textContent = data.motorPerformanceVal ?? '--';
    document.getElementById('dutyCycleIncoming').textContent = data.dutyCycleIncoming ?? '--';
    document.getElementById('rawCount').textContent = data.rawCount ?? '--';
    document.getElementById('ledCounter').textContent = data.ledCounter ?? '--';

    // Calibration builder live duty readout
    updateCalDutyReadout(data.tempDutyCycle);
    document.getElementById('calibrationStatus').textContent = 'Cal: ' + (data.calibrationText || '--');

    // Live calibration-curve marker. In cal mode the achieved speed is on the
    // needle (unknown here), so ride the curve at the jogged duty; otherwise use
    // the incoming hall speed against the applied duty.
    const calModeEl = document.getElementById('testCal');
    if (calModeEl && calModeEl.checked) {
      setCalCurrentPoint(data.tempDutyCycle, curveSpeedAt(data.tempDutyCycle));
    } else {
      setCalCurrentPoint(data.appliedDutyCycle, data.incomingSpeed);
    }

    return STATUS_POLL_MS;
  } catch (error) {
    console.log('Error fetching status:', error);
    return speedTestActive ? TEST_STATUS_POLL_MS : STATUS_POLL_MS;
  } finally {
    fetchStatusInFlight = false;
  }
}

function pushControl(key, value) {
  return fetch('/api/control', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ key, value })
  }).catch(e => console.log('Control error:', e));
}

function pushAction(action) {
  fetch('/api/action', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ action })
  }).catch(e => console.log('Action error:', e));
}

function hex2bin(hex) {
  return ("00000000" + parseInt(hex, 16).toString(2)).substr(-8);
}

function showNotification(message, type = "success") {
  const notification = document.createElement("div");
  notification.textContent = message;
  notification.style.cssText = `
        position: fixed;
        top: 20px;
        left: 50%;
        transform: translateX(-50%);
        padding: 1rem 2rem;
        background: ${type === "error" ? "var(--danger)" : "var(--success)"};
        color: white;
        border-radius: 8px;
        z-index: 10000;
        font-weight: 600;
        box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    `;

  document.body.appendChild(notification);

  setTimeout(() => {
    notification.style.transition = "opacity 0.3s";
    notification.style.opacity = "0";
    setTimeout(() => notification.remove(), 300);
  }, 3000);
}

// ===== CALIBRATION BUILDER (SpeedPulserV2) =====
const CAL_PWM_MAX = 4095;
const CAL_CHIP_MAX = 300;   // capture targets available up to 300 km/h
let calSelectedSpeed = 0;
let calChipsMaxSpeed = -1;
let lastCalPointCount = 0;  // points in the current builder state (for the save guard)

function initCalBuilder() {
  // Jog steppers with press-and-hold repeat + acceleration
  document.querySelectorAll('.stepper-btn[data-jog]').forEach((btn) => {
    const delta = parseInt(btn.dataset.jog, 10);
    attachHold(btn, () => calJog(delta));
  });

  const targetInput = document.getElementById('calTargetSpeed');
  if (targetInput) {
    targetInput.addEventListener('input', () => setCalTarget(parseInt(targetInput.value, 10) || 0, false));
  }

  const captureBtn = document.getElementById('calCaptureBtn');
  if (captureBtn) captureBtn.addEventListener('click', calCapture);

  const nameEl = document.getElementById('calName');
  if (nameEl) {
    nameEl.addEventListener('change', () => calPost({ op: 'setName', name: nameEl.value }).catch(() => {}));
  }

  bindCal('calApplyBtn', () => calPost({ op: 'apply' }).then((s) => {
    applyCalState(s);
    refreshCalibrationsSelect();
    fetchCalCurve();
    showNotification('Calibration generated and applied');
  }));

  bindCal('calSaveBtn', () => {
    if (lastCalPointCount < 2) {
      showNotification('Capture at least 2 points before saving', 'error');
      return;
    }
    // A saved custom cal shows up as the value=200 option; warn before clobbering it.
    const sel = document.getElementById('motorCalSelection');
    const exists = sel && [...sel.options].some((o) => o.value === '200');
    if (exists && !confirm('A custom calibration is already saved on the device. Overwrite it?')) {
      return;
    }
    calPost({ op: 'save' }).then((s) => {
      applyCalState(s);
      refreshCalibrationsSelect();
      fetchCalCurve();
      showNotification('Calibration saved to device');
    });
  });

  bindCal('calClearBtn', () => {
    if (!confirm('Clear all captured calibration points?')) return;
    calPost({ op: 'clearPoints' }).then(applyCalState).then(() => showNotification('Points cleared'));
  });

  bindCal('calExportTextBtn', () => calPost({ op: 'export' }).then((r) => {
    const text = r.json || '';
    document.getElementById('calText').value = text;
    const nameEl = document.getElementById('calName');
    const base = (nameEl && nameEl.value.trim()) || 'calibration';
    const fname = base.replace(/[^a-z0-9._-]+/gi, '_') + '.txt';
    downloadTextFile(fname, text);
    showNotification('Exported to text file');
  }));

  bindCal('calExportCBtn', () => calPost({ op: 'export' }).then((r) => {
    const text = r.carray || '';
    document.getElementById('calText').value = text;
    const nameEl = document.getElementById('calName');
    const base = (nameEl && nameEl.value.trim()) || 'calibration';
    const fname = base.replace(/[^a-z0-9._-]+/gi, '_') + '_carray.txt';
    downloadTextFile(fname, text);
    showNotification('Exported firmware C-Array preset');
  }));

  bindCal('calImportBtn', () => {
    // Always let the user pick a text file to load, display and import.
    const fileEl = document.getElementById('calImportFile');
    if (fileEl) fileEl.click();
  });

  const importFileEl = document.getElementById('calImportFile');
  if (importFileEl) {
    importFileEl.addEventListener('change', () => {
      const file = importFileEl.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        const text = String(reader.result || '').trim();
        document.getElementById('calText').value = text;
        if (text) calImportText(text);
      };
      reader.readAsText(file);
      importFileEl.value = '';   // allow re-picking the same file
    });
  }
}

function calImportText(text) {
  calPost({ op: 'import', json: text }).then((s) => {
    applyCalState(s);
    refreshCalibrationsSelect();
    fetchCalCurve();
    showNotification('Calibration imported');
  });
}

// Trigger a browser download of the given text as a file.
function downloadTextFile(filename, text) {
  const blob = new Blob([text], { type: 'text/plain' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function bindCal(id, fn) {
  const el = document.getElementById(id);
  if (el) el.addEventListener('click', () => { try { fn(); } catch (e) { console.log(e); } });
}

// Press-and-hold helper: fires immediately, then repeats faster while held.
function attachHold(el, fn) {
  let timer = null;
  let delay = 320;
  const stop = () => { if (timer) { clearTimeout(timer); timer = null; } };
  const start = (e) => {
    e.preventDefault();
    fn();
    delay = 320;
    const tick = () => { fn(); delay = Math.max(60, delay * 0.8); timer = setTimeout(tick, delay); };
    timer = setTimeout(tick, delay);
  };
  el.addEventListener('pointerdown', start);
  el.addEventListener('pointerup', stop);
  el.addEventListener('pointerleave', stop);
  el.addEventListener('pointercancel', stop);
}

async function calPost(body) {
  const r = await fetch('/api/cal', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!r.ok) {
    const err = await r.json().catch(() => ({}));
    showNotification('Cal error: ' + (err.error || ('HTTP ' + r.status)), 'error');
    throw new Error(err.error || r.status);
  }
  return r.json();
}

async function refreshCalState() {
  try {
    const r = await fetch('/api/cal');
    applyCalState(await r.json());
  } catch (e) {
    console.log('Error fetching cal state:', e);
  }
}

async function calJog(delta) {
  try {
    const s = await calPost({ op: 'jog', delta });
    updateCalDutyReadout(s.duty);
  } catch (e) { /* already notified */ }
}

async function calCapture() {
  try {
    const s = await calPost({ op: 'addPoint', speed: calSelectedSpeed });
    applyCalState(s);
    showNotification('Captured ' + calSelectedSpeed + ' @ duty ' + s.duty);
  } catch (e) { /* already notified */ }
}

function updateCalDutyReadout(duty) {
  duty = duty || 0;
  const nowEl = document.getElementById('calDutyNow');
  if (!nowEl) return;
  nowEl.textContent = duty;
  const pctEl = document.getElementById('calDutyPct');
  const capDutyEl = document.getElementById('calCaptureDuty');
  if (pctEl) pctEl.textContent = (duty / CAL_PWM_MAX * 100).toFixed(1);
  if (capDutyEl) capDutyEl.textContent = duty;
}

function applyCalState(state) {
  if (!state) return;
  updateCalDutyReadout(state.duty);

  // Unit label
  const unit = state.unit === 'mph' ? 'mph' : 'km/h';
  const unitEl = document.getElementById('calUnitLabel');
  if (unitEl) unitEl.textContent = unit;

  // An MPH cal auto-enables "Cluster in MPH" on the device; mirror it in the UI.
  if (typeof state.convertToMPH === 'boolean') {
    applyMphState(state.convertToMPH);
  }

  // Name (don't clobber while the user is editing it)
  const nameEl = document.getElementById('calName');
  if (nameEl && document.activeElement !== nameEl) nameEl.value = state.name || '';

  buildCalChips(state.maxSpeed || 200);
  renderCalPoints(state.points || []);
  lastCalPointCount = state.count ?? (state.points || []).length;
  calCapturedPoints = Array.isArray(state.points) ? state.points : [];
  scheduleCalDraw();
}

function buildCalChips(maxSpeed) {
  // Always offer capture targets up to CAL_CHIP_MAX so the user can calibrate
  // beyond the configured gauge max when needed.
  const chipMax = Math.max(maxSpeed || 0, CAL_CHIP_MAX);
  if (chipMax === calChipsMaxSpeed) return;
  calChipsMaxSpeed = chipMax;
  const grid = document.getElementById('calTargetChips');
  if (!grid) return;
  grid.innerHTML = '';
  for (let s = 0; s <= chipMax; s += 10) {
    const chip = document.createElement('button');
    chip.className = 'chip';
    chip.textContent = s;
    chip.dataset.speed = s;
    chip.addEventListener('click', () => setCalTarget(s, true));
    grid.appendChild(chip);
  }
  highlightCalChip();
}

function setCalTarget(speed, fromChip) {
  calSelectedSpeed = speed;
  const capEl = document.getElementById('calCaptureSpeed');
  if (capEl) capEl.textContent = speed;
  const input = document.getElementById('calTargetSpeed');
  if (input && fromChip) input.value = speed;
  highlightCalChip();
}

function highlightCalChip() {
  document.querySelectorAll('#calTargetChips .chip').forEach((c) => {
    c.classList.toggle('active', parseInt(c.dataset.speed, 10) === calSelectedSpeed);
  });
}

function renderCalPoints(points) {
  const list = document.getElementById('calPointsList');
  if (!list) return;
  if (!points.length) {
    list.innerHTML = '<p class="hint">No points captured yet.</p>';
    return;
  }
  let html = '<div class="cal-point-head"><span>Speed</span><span>Duty</span><span></span></div>';
  points.forEach((p, i) => {
    html += '<div class="cal-point-row">' +
              '<span>' + p.speed + '</span>' +
              '<span>' + p.duty + '</span>' +
              '<button class="cal-del" data-index="' + i + '">✕</button>' +
            '</div>';
  });
  list.innerHTML = html;
  list.querySelectorAll('.cal-del').forEach((btn) => {
    btn.addEventListener('click', () => {
      const index = parseInt(btn.dataset.index, 10);
      calPost({ op: 'deletePoint', index }).then(applyCalState).catch(() => {});
    });
  });
}

// Refresh the Configuration dropdown so the custom slot appears/updates,
// then select it so the user can see it is active.
function refreshCalibrationsSelect() {
  fetchCalibrations().then(() => {
    const sel = document.getElementById('motorCalSelection');
    if (sel && [...sel.options].some((o) => o.value === '200')) {
      sel.value = '200';
    }
  });
}

// ===== CALIBRATION CURVE GRAPH =====
let calCurveData = null;          // { speed:[], duty:[], pwmMax, maxSpeed } from /api/calcurve
let calCapturedPoints = [];       // [{speed, duty}] captured anchors from the builder state
let calCurrentPoint = null;       // { duty, speed } live operating point
let calDrawPending = false;

// V4 boards repurpose the dashboard graph as a live control strip-chart.
let boardHasVc = false;
const VC_HISTORY_MAX = 180;       // rolling window of samples (~13 s @ 75 ms poll)
let vcHistory = [];               // [{ target, measured, voltage, pwm }]
const VC_COL = { target: '#00D9FF', measured: '#2EA043', voltage: '#FF6B35', pwm: '#A371F7' };

function pushVcSample(s) {
  vcHistory.push(s);
  if (vcHistory.length > VC_HISTORY_MAX) vcHistory.shift();
}

function legendItem(color, label) {
  return '<span class="legend-item"><span class="legend-swatch" style="background:' +
         color + '"></span>' + label + '</span>';
}

// Swap the dashboard graph's title + legend between the legacy calibration curve
// and the V4 live-control strip chart.
function updateDashChartChrome(isV4) {
  const title = document.getElementById('dashChartTitle');
  if (title) title.textContent = isV4 ? 'Live Control' : 'Calibration Curve';
  const legend = document.getElementById('dashChartLegend');
  if (!legend) return;
  if (isV4) {
    legend.innerHTML =
      legendItem(VC_COL.target, 'Target') +
      legendItem(VC_COL.measured, 'Measured') +
      legendItem(VC_COL.voltage, 'Voltage %') +
      legendItem(VC_COL.pwm, 'PWM %');
  } else {
    legend.innerHTML = '<span class="legend-item"><span class="legend-swatch point"></span>Current Duty/Speed</span>';
  }
}

async function fetchCalCurve() {
  try {
    const r = await fetch('/api/calcurve');
    calCurveData = await r.json();
  } catch (e) {
    console.log('Error fetching cal curve:', e);
  }
  scheduleCalDraw();
}

function setCalCurrentPoint(duty, speed) {
  calCurrentPoint = { duty: Number(duty) || 0, speed: Number(speed) || 0 };
  scheduleCalDraw();
}

// Interpolate the curve's speed at a given hardware duty (used in cal mode,
// where the true achieved speed isn't reported by the firmware).
function curveSpeedAt(duty) {
  if (!calCurveData || !Array.isArray(calCurveData.duty) || calCurveData.duty.length < 2) return 0;
  const d = calCurveData.duty, s = calCurveData.speed;
  if (duty <= d[0]) return s[0];
  for (let i = 1; i < d.length; i++) {
    if (duty <= d[i]) {
      const span = d[i] - d[i - 1];
      const frac = span > 0 ? (duty - d[i - 1]) / span : 0;
      return Math.round(s[i - 1] + frac * (s[i] - s[i - 1]));
    }
  }
  return s[s.length - 1];
}

// Coalesce redraws to one per animation frame (the status poll can fire ~50 Hz).
function scheduleCalDraw() {
  if (calDrawPending) return;
  calDrawPending = true;
  requestAnimationFrame(() => {
    calDrawPending = false;
    if (boardHasVc) drawVoltageChart(); else drawCalCurve();
  });
}

function drawCalCurve() {
  const canvas = document.getElementById('calCurveCanvas');
  if (!canvas || !canvas.clientWidth) return;   // not laid out yet (hidden tab)

  const dpr = window.devicePixelRatio || 1;
  const cssW = canvas.clientWidth;
  const cssH = Math.round(cssW * 0.6);
  canvas.style.height = cssH + 'px';
  if (canvas.width !== Math.round(cssW * dpr) || canvas.height !== Math.round(cssH * dpr)) {
    canvas.width = Math.round(cssW * dpr);
    canvas.height = Math.round(cssH * dpr);
  }

  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  const style = getComputedStyle(document.documentElement);
  const col = (name, fallback) => (style.getPropertyValue(name).trim() || fallback);
  const primary = col('--primary', '#00D9FF');
  const secondary = col('--secondary', '#FF6B35');
  const success = col('--success', '#2EA043');
  const border = col('--border', '#30363D');
  const textDim = col('--text-dim', '#8B949E');

  const padL = 44, padR = 12, padT = 12, padB = 30;
  const plotW = cssW - padL - padR;
  const plotH = cssH - padT - padB;

  const pwmMax = (calCurveData && calCurveData.pwmMax) || 4095;
  // Stretch the x-axis across only the duty range the calibration actually uses
  // (the curve tops out well below full-scale), so the trace fills the width.
  let dutyMax = 1;
  if (calCurveData && Array.isArray(calCurveData.duty) && calCurveData.duty.length) {
    dutyMax = calCurveData.duty[calCurveData.duty.length - 1] || 1;
  }
  if (calCurrentPoint && calCurrentPoint.duty > dutyMax) dutyMax = calCurrentPoint.duty;
  dutyMax = Math.max(1, Math.ceil(dutyMax / 100) * 100);

  let speedMax = (calCurveData && calCurveData.maxSpeed) || 200;
  if (calCurrentPoint && calCurrentPoint.speed > speedMax) {
    speedMax = Math.ceil(calCurrentPoint.speed / 20) * 20;   // grow if the live point overshoots
  }
  if (speedMax < 1) speedMax = 1;

  const xOf = (duty) => padL + (Math.max(0, Math.min(duty, dutyMax)) / dutyMax) * plotW;
  const yOf = (speed) => padT + plotH - (Math.max(0, Math.min(speed, speedMax)) / speedMax) * plotH;

  ctx.font = '10px -apple-system, "Segoe UI", Arial, sans-serif';
  ctx.strokeStyle = border;
  ctx.lineWidth = 1;

  // Horizontal grid (speed) + Y labels
  ctx.fillStyle = textDim;
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  const ySteps = 4;
  for (let i = 0; i <= ySteps; i++) {
    const sp = (speedMax / ySteps) * i;
    const y = yOf(sp);
    ctx.globalAlpha = 0.35;
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(cssW - padR, y); ctx.stroke();
    ctx.globalAlpha = 1;
    ctx.fillText(String(Math.round(sp)), padL - 6, y);
  }

  // Vertical grid (duty %) + X labels
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  const xSteps = 5;
  for (let i = 0; i <= xSteps; i++) {
    const x = padL + (plotW / xSteps) * i;
    ctx.globalAlpha = 0.35;
    ctx.beginPath(); ctx.moveTo(x, padT); ctx.lineTo(x, padT + plotH); ctx.stroke();
    ctx.globalAlpha = 1;
    ctx.fillText(String(Math.round((i / xSteps) * dutyMax)), x, padT + plotH + 6);
  }

  // Axis title
  ctx.fillStyle = textDim;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'bottom';
  ctx.fillText('Motor duty', padL + plotW / 2, cssH);

  // Faint calibration curve
  if (calCurveData && Array.isArray(calCurveData.duty) && calCurveData.duty.length > 1) {
    ctx.strokeStyle = primary;
    ctx.globalAlpha = 0.5;
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < calCurveData.duty.length; i++) {
      const x = xOf(calCurveData.duty[i]);
      const y = yOf(calCurveData.speed[i]);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke();
    ctx.globalAlpha = 1;
  }

  // Anchor points of the ACTIVE calibration (captured points for a custom cal,
  // or reference marks for a preset) — always sit on the blue curve.
  if (calCurveData && Array.isArray(calCurveData.anchorDuty) && calCurveData.anchorDuty.length) {
    ctx.fillStyle = success;
    for (let i = 0; i < calCurveData.anchorDuty.length; i++) {
      ctx.beginPath();
      ctx.arc(xOf(calCurveData.anchorDuty[i]), yOf(calCurveData.anchorSpeed[i]), 3.5, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  // Current operating point + crosshair
  if (calCurrentPoint) {
    const x = xOf(calCurrentPoint.duty);
    const y = yOf(calCurrentPoint.speed);

    ctx.strokeStyle = secondary;
    ctx.globalAlpha = 0.5;
    ctx.setLineDash([4, 4]);
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(x, y); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(x, padT + plotH); ctx.lineTo(x, y); ctx.stroke();
    ctx.setLineDash([]);
    ctx.globalAlpha = 1;

    ctx.fillStyle = secondary;
    ctx.beginPath();
    ctx.arc(x, y, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 1.5;
    ctx.stroke();

    const label = calCurrentPoint.speed + ' @ ' + Math.round(calCurrentPoint.duty) + ' duty';
    ctx.font = '11px -apple-system, "Segoe UI", Arial, sans-serif';
    ctx.fillStyle = secondary;
    ctx.textBaseline = 'bottom';
    const rightSide = x > padL + plotW * 0.7;
    ctx.textAlign = rightSide ? 'right' : 'left';
    ctx.fillText(label, rightSide ? x - 8 : x + 8, y - 6);
  }
}

// V4 live-control strip chart: rolling time-series of target/measured speed
// (left axis, km/h) plus buck voltage and PWM throttle (right axis, %).
function drawVoltageChart() {
  const canvas = document.getElementById('calCurveCanvas');
  if (!canvas || !canvas.clientWidth) return;   // not laid out yet (hidden tab)

  const dpr = window.devicePixelRatio || 1;
  const cssW = canvas.clientWidth;
  const cssH = Math.round(cssW * 0.6);
  canvas.style.height = cssH + 'px';
  if (canvas.width !== Math.round(cssW * dpr) || canvas.height !== Math.round(cssH * dpr)) {
    canvas.width = Math.round(cssW * dpr);
    canvas.height = Math.round(cssH * dpr);
  }

  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssW, cssH);

  const style = getComputedStyle(document.documentElement);
  const col = (name, fallback) => (style.getPropertyValue(name).trim() || fallback);
  const border = col('--border', '#30363D');
  const textDim = col('--text-dim', '#8B949E');

  const padL = 40, padR = 40, padT = 12, padB = 22;
  const plotW = cssW - padL - padR;
  const plotH = cssH - padT - padB;

  // Left axis: speed. Grow to fit the largest target/measured seen in the window.
  let speedMax = (calCurveData && calCurveData.maxSpeed) || 200;
  for (const s of vcHistory) {
    if (s.target != null && s.target > speedMax) speedMax = s.target;
    if (s.measured != null && s.measured > speedMax) speedMax = s.measured;
  }
  speedMax = Math.max(20, Math.ceil(speedMax / 20) * 20);

  const n = VC_HISTORY_MAX;
  const xOf = (i) => padL + (n <= 1 ? 0 : (i / (n - 1)) * plotW);
  const ySpeed = (v) => padT + plotH - (Math.max(0, Math.min(v, speedMax)) / speedMax) * plotH;
  const yPct = (v) => padT + plotH - (Math.max(0, Math.min(v, 100)) / 100) * plotH;

  ctx.font = '10px -apple-system, "Segoe UI", Arial, sans-serif';
  ctx.strokeStyle = border;
  ctx.lineWidth = 1;

  // Horizontal grid with speed (left) and percent (right) labels.
  const ySteps = 4;
  for (let i = 0; i <= ySteps; i++) {
    const y = padT + (plotH / ySteps) * i;
    ctx.globalAlpha = 0.35;
    ctx.beginPath(); ctx.moveTo(padL, y); ctx.lineTo(cssW - padR, y); ctx.stroke();
    ctx.globalAlpha = 1;
    ctx.fillStyle = textDim;
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'right';
    ctx.fillText(String(Math.round(speedMax * (1 - i / ySteps))), padL - 6, y);
    ctx.textAlign = 'left';
    ctx.fillText(String(Math.round(100 * (1 - i / ySteps))) + '%', cssW - padR + 6, y);
  }

  // Axis titles
  ctx.fillStyle = textDim;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'bottom';
  ctx.fillText('time \u2192', padL + plotW / 2, cssH);

  if (!vcHistory.length) {
    ctx.fillStyle = textDim;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('Waiting for data\u2026', padL + plotW / 2, padT + plotH / 2);
    return;
  }

  // Newest sample sits at the right edge; older samples trail off to the left.
  const base = n - vcHistory.length;
  const plotLine = (key, color, yMap, dashed) => {
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.globalAlpha = dashed ? 0.85 : 1;
    if (dashed) ctx.setLineDash([4, 3]); else ctx.setLineDash([]);
    ctx.beginPath();
    let started = false;
    for (let j = 0; j < vcHistory.length; j++) {
      const v = vcHistory[j][key];
      if (v == null || Number.isNaN(v)) { started = false; continue; }
      const x = xOf(base + j);
      const y = yMap(v);
      if (!started) { ctx.moveTo(x, y); started = true; } else { ctx.lineTo(x, y); }
    }
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.globalAlpha = 1;
  };

  plotLine('voltage', VC_COL.voltage, yPct, false);
  plotLine('pwm', VC_COL.pwm, yPct, false);
  plotLine('target', VC_COL.target, ySpeed, true);
  plotLine('measured', VC_COL.measured, ySpeed, false);

  // Latest-value dots at the right edge.
  const last = vcHistory[vcHistory.length - 1];
  const dot = (v, color, yMap) => {
    if (v == null || Number.isNaN(v)) return;
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(xOf(n - 1), yMap(v), 3.5, 0, Math.PI * 2);
    ctx.fill();
  };
  dot(last.voltage, VC_COL.voltage, yPct);
  dot(last.pwm, VC_COL.pwm, yPct);
  dot(last.target, VC_COL.target, ySpeed);
  dot(last.measured, VC_COL.measured, ySpeed);
}

// ===== OTA UPDATE =====
function initOta() {
  const fileInput   = document.getElementById('otaFile');
  const fileNameEl  = document.getElementById('otaFileName');
  const uploadBtn   = document.getElementById('otaUploadBtn');
  const progressWrap = document.getElementById('otaProgressWrap');
  const progressBar = document.getElementById('otaProgressBar');
  const progressLbl = document.getElementById('otaProgressLabel');
  const statusEl    = document.getElementById('otaStatus');
  const typeSelect  = document.getElementById('otaType');

  if (!fileInput || !uploadBtn) return;

  function currentType() {
    return typeSelect && typeSelect.value === 'filesystem' ? 'filesystem' : 'firmware';
  }

  // OTA sequence: resume on firmware after a filesystem upload; keep steps synced.
  if (typeSelect) {
    const doneInit = otaLoadDone();
    if (doneInit.includes('filesystem') && !doneInit.includes('firmware')) typeSelect.value = 'firmware';
    typeSelect.addEventListener('change', renderOtaSteps);
  }
  renderOtaSteps();

  fileInput.addEventListener('change', () => {
    if (fileInput.files[0]) selectFile(fileInput.files[0]);
  });

  function selectFile(file) {
    if (!file.name.toLowerCase().endsWith('.bin')) {
      setOtaStatus('Please select a .bin file.', 'error');
      return;
    }
    fileInput._selectedFile = file;
    if (fileNameEl) fileNameEl.textContent = file.name + ' (' + (file.size / 1024).toFixed(1) + ' KB)';
    uploadBtn.disabled = false;
    setOtaStatus('');
  }

  uploadBtn.addEventListener('click', () => {
    const file = fileInput._selectedFile;
    if (!file) return;

    const formData = new FormData();
    formData.append('firmware', file, file.name);

    const uploadType = currentType();
    const xhr = new XMLHttpRequest();

    xhr.upload.addEventListener('progress', (e) => {
      if (e.lengthComputable) {
        const pct = Math.round((e.loaded / e.total) * 100);
        progressBar.style.width = pct + '%';
        progressLbl.textContent = pct + '%';
      }
    });

    xhr.addEventListener('load', () => {
      try {
        const resp = JSON.parse(xhr.responseText);
        if (resp.success === true) {
          otaMarkDone(uploadType);
          if (uploadType === 'filesystem') {
            // filesystem does not reboot — advance to the firmware step
            if (typeSelect) typeSelect.value = 'firmware';
            renderOtaSteps();
            setOtaStatus('Filesystem updated. Now upload the firmware.', 'success');
            resetProgress();
            fileInput._selectedFile = null;
            fileInput.value = '';
            if (fileNameEl) fileNameEl.textContent = '';
            uploadBtn.disabled = true;
          } else {
            setOtaStatus(resp.message || 'Update complete. Device is rebooting...', 'success');
            uploadBtn.disabled = true;
          }
        } else {
          setOtaStatus('Update failed: ' + (resp.message || 'Unknown error'), 'error');
          resetProgress();
        }
      } catch (_) {
        setOtaStatus('Unexpected response from device.', 'error');
        resetProgress();
      }
    });

    xhr.addEventListener('error', () => {
      // A network error here is expected if the device reboots before replying
      setOtaStatus('Update sent. Device may be rebooting — please wait and reconnect.', 'success');
    });

    progressWrap.style.display = 'block';
    progressBar.style.width = '0%';
    progressLbl.textContent = '0%';
    uploadBtn.disabled = true;
    setOtaStatus('Uploading...');

    xhr.open('POST', uploadType === 'filesystem' ? '/api/ota/fs' : '/api/ota');
    xhr.send(formData);
  });

  function setOtaStatus(msg, type) {
    statusEl.textContent = msg;
    statusEl.className = 'ota-status' + (type ? ' ' + type : '');
  }

  function resetProgress() {
    progressBar.style.width = '0%';
    progressLbl.textContent = '0%';
    progressWrap.style.display = 'none';
    uploadBtn.disabled = false;
  }
}

// ---- OTA two-step sequence (filesystem first, then firmware) -------------
const OTA_STEPS_KEY = 'oh_ota_steps';
function otaLoadDone() {
  try {
    const raw = JSON.parse(localStorage.getItem(OTA_STEPS_KEY) || '{}');
    if (!raw.ts || Date.now() - raw.ts > 15 * 60 * 1000) return [];
    return Array.isArray(raw.done) ? raw.done : [];
  } catch (e) { return []; }
}
function otaSaveDone(done) {
  localStorage.setItem(OTA_STEPS_KEY, JSON.stringify({ done, ts: Date.now() }));
}
function renderOtaSteps() {
  const sel = document.getElementById('otaType');
  const cur = sel ? sel.value : 'filesystem';
  const done = otaLoadDone();
  document.querySelectorAll('#otaSteps .ota-step').forEach((el) => {
    const s = el.dataset.step;
    el.classList.toggle('done', done.includes(s));
    el.classList.toggle('active', s === cur && !done.includes(s));
  });
}
function otaMarkDone(type) {
  const done = otaLoadDone();
  if (!done.includes(type)) done.push(type);
  otaSaveDone(done);
  renderOtaSteps();
}

// ---- Collapsible cards (config/advanced/calibration collapse by default) --
function initCollapsibleCards() {
  ['configuration-page', 'advanced-page', 'calibration-page'].forEach((pageId) => {
    const page = document.getElementById(pageId);
    if (!page) return;
    page.querySelectorAll('.card').forEach((card) => {
      if (card.classList.contains('no-collapse')) return;
      card.classList.add('collapsible', 'collapsed');
      const h2 = card.querySelector('h2');
      if (h2) h2.addEventListener('click', () => card.classList.toggle('collapsed'));
    });
  });
}

/* =======================================================================
   Per-tile dial gauges (ported from the OpenHaldex theme). The km/h
   readouts can render as 270 degree dials, toggled in Display Options.
   ======================================================================= */
const GAUGE_TILES = [
  { id: 'incomingSpeed', label: 'Incoming Speed', min: 0, max: 300, unit: 'km/h' },
  { id: 'measuredSpeed', label: 'Measured Speed', min: 0, max: 300, unit: 'km/h' },
];
const TG_R = 40;
const TG_CIRC = 2 * Math.PI * TG_R;
const TG_ARC = TG_CIRC * 0.75;
const TG_GAP = TG_CIRC - TG_ARC;
const GAUGE_PREFS_KEY = 'speedPulserGaugePrefs';
const GAUGE_DEFAULTS = { tiles: ['incomingSpeed', 'measuredSpeed'] };
let gaugePrefs = loadGaugePrefs();

function loadGaugePrefs() {
  try {
    const raw = localStorage.getItem(GAUGE_PREFS_KEY);
    if (raw) {
      const p = JSON.parse(raw);
      return { tiles: Array.isArray(p.tiles) ? p.tiles : GAUGE_DEFAULTS.tiles.slice() };
    }
  } catch (e) { /* defaults */ }
  return { tiles: GAUGE_DEFAULTS.tiles.slice() };
}
function saveGaugePrefs() {
  try { localStorage.setItem(GAUGE_PREFS_KEY, JSON.stringify(gaugePrefs)); } catch (e) {}
}
function ensureTileGauge(tile) {
  if (tile.querySelector('.tile-gauge')) return;
  const wrap = document.createElement('div');
  wrap.className = 'tile-gauge';
  wrap.innerHTML =
    `<svg viewBox="0 0 100 100" aria-hidden="true">` +
    `<circle class="tg-track" cx="50" cy="50" r="${TG_R}" transform="rotate(135 50 50)" ` +
    `stroke-dasharray="${TG_ARC.toFixed(2)} ${TG_GAP.toFixed(2)}"/>` +
    `<circle class="tg-fill" cx="50" cy="50" r="${TG_R}" transform="rotate(135 50 50)" ` +
    `stroke-dasharray="0 ${TG_CIRC.toFixed(2)}"/>` +
    `<text class="tg-val" x="50" y="52" text-anchor="middle">--</text>` +
    `<text class="tg-unit" x="50" y="66" text-anchor="middle"></text>` +
    `<text class="tg-min" x="24" y="92" text-anchor="middle">0</text>` +
    `<text class="tg-max" x="76" y="92" text-anchor="middle">0</text>` +
    `</svg>`;
  tile.appendChild(wrap);
}
function applyGaugePrefs() {
  GAUGE_TILES.forEach((t) => {
    const el = document.getElementById(t.id);
    if (!el) return;
    const tile = el.closest('.gauge');
    if (!tile) return;
    ensureTileGauge(tile);
    const on = gaugePrefs.tiles.includes(t.id);
    tile.classList.toggle('as-gauge', on);
    if (on) {
      const unitEl = tile.querySelector('.tg-unit');
      const srcUnit = tile.querySelector('.gauge-unit');
      if (unitEl) unitEl.textContent = srcUnit ? srcUnit.textContent.trim() : t.unit;
      const minEl = tile.querySelector('.tg-min');
      const maxEl = tile.querySelector('.tg-max');
      if (minEl) minEl.textContent = t.min;
      if (maxEl) maxEl.textContent = t.max;
    }
  });
}
function updateTileGauges() {
  GAUGE_TILES.forEach((t) => {
    const el = document.getElementById(t.id);
    if (!el) return;
    const tile = el.closest('.gauge');
    if (!tile || !tile.classList.contains('as-gauge')) return;
    const raw = parseFloat(el.textContent);
    const valEl = tile.querySelector('.tg-val');
    const fillEl = tile.querySelector('.tg-fill');
    if (!valEl || !fillEl) return;
    const gaugeWrap = tile.querySelector('.tile-gauge');
    if (gaugeWrap) gaugeWrap.classList.toggle('warn', el.style.color === 'orange' || el.classList.contains('test-active'));
    if (Number.isNaN(raw)) {
      valEl.textContent = '--';
      fillEl.style.strokeDasharray = `0 ${TG_CIRC.toFixed(2)}`;
      return;
    }
    valEl.textContent = el.textContent;
    const frac = Math.max(0, Math.min(1, (raw - t.min) / (t.max - t.min || 1)));
    fillEl.style.strokeDasharray = `${(TG_ARC * frac).toFixed(2)} ${TG_CIRC.toFixed(2)}`;
  });
}
function initGaugeUI() {
  const host = document.getElementById('gaugeCustomizer');
  if (host) {
    host.innerHTML = '';
    GAUGE_TILES.forEach((t) => {
      const label = document.createElement('label');
      label.className = 'tile-opt';
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.checked = gaugePrefs.tiles.includes(t.id);
      cb.addEventListener('change', () => {
        const set = new Set(gaugePrefs.tiles);
        if (cb.checked) set.add(t.id); else set.delete(t.id);
        gaugePrefs.tiles = [...set];
        saveGaugePrefs();
        applyGaugePrefs();
      });
      const span = document.createElement('span');
      span.textContent = t.label;
      label.appendChild(cb);
      label.appendChild(span);
      host.appendChild(label);
    });
  }
  applyGaugePrefs();
}
