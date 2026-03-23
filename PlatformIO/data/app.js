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
  initOta();
  fetchCalibrations().then(fetchSettings);  // Load calibration options then settings
  startStatusPolling();
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
    return;
  }

  incomingSpeedLabelEl.textContent = 'Incoming Speed';
  motorDutyLabelEl.textContent = 'Motor Duty';
  incomingSpeedEl.textContent = speedValue ?? '--';
  motorDutyEl.textContent = dutyRaw;
  incomingSpeedEl.classList.remove('test-active');
  motorDutyEl.classList.remove('test-active');
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
  const configInputs = ['hasNeedleSweep', 'sweepSpeed', 'motorCalSelection', 'maxSpeed', 'maxFreqHall', 'speedOffsetPositive', 'speedOffset', 'convertToMPH', 'averageFilter'];
  configInputs.forEach(id => {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener('change', () => {
        const value = el.type === 'checkbox' ? el.checked : el.value;
        pushControl(id, value);

        if (id === 'motorCalSelection') {
          const selectedOption = el.options[el.selectedIndex];
          if (selectedOption) {
            document.getElementById('calibrationStatus').textContent = `Cal: ${selectedOption.textContent}`;
          }
        }
      });
      // For sliders, also update live display
      if (el.type === 'range') {
        el.addEventListener('input', () => {
          updateSliderDisplay(id, el.value);
        });
      }
    }
  });

  const curveInputs = ['useSpeedOffsetCurve', 'curveOffset0', 'curveOffset1', 'curveOffset2', 'curveOffset3', 'curveOffset4'];
  curveInputs.forEach(id => {
    const el = document.getElementById(id);
    if (!el) {
      return;
    }

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
  const testCalEl = document.getElementById('testCal');
  if (testCalEl) {
    testCalEl.addEventListener('change', () => {
      const value = testCalEl.checked;
      pushControl('testCal', value);
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


async function fetchSettings() {
  try {
    const response = await fetch('/api/settings');
    const data = await response.json();

    // Load all settings from API
    document.getElementById('hasNeedleSweep').checked = data.hasNeedleSweep || false;
    document.getElementById('sweepSpeed').value = data.sweepSpeed || 18;
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

    document.getElementById('useSpeedOffsetCurve').checked = data.useSpeedOffsetCurve || false;

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

    // Update firmware info on OTA page
    try {
      const verResponse = await fetch('/api/version');
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
      return TEST_STATUS_POLL_MS;
    }

    updateDashboard(data.dutyCycleIncoming, data.appliedDutyCycle, false);

    // Advanced status monitor
    document.getElementById('motorPerformanceVal').textContent = data.motorPerformanceVal ?? '--';
    document.getElementById('dutyCycleIncoming').textContent = data.dutyCycleIncoming ?? '--';
    document.getElementById('rawCount').textContent = data.rawCount ?? '--';
    document.getElementById('ledCounter').textContent = data.ledCounter ?? '--';

    // Calibration status
    document.getElementById('calibrationDuty').textContent = (data.tempDutyCycle || 0) + ' / 385';
    document.getElementById('calibrationStatus').textContent = 'Cal: ' + (data.calibrationText || '--');

    return STATUS_POLL_MS;
  } catch (error) {
    console.log('Error fetching status:', error);
    return speedTestActive ? TEST_STATUS_POLL_MS : STATUS_POLL_MS;
  } finally {
    fetchStatusInFlight = false;
  }
}

function pushControl(key, value) {
  fetch('/api/control', {
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

// ===== OTA UPDATE =====
function initOta() {
  const dropZone    = document.getElementById('otaDropZone');
  const fileInput   = document.getElementById('otaFile');
  const fileNameEl  = document.getElementById('otaFileName');
  const uploadBtn   = document.getElementById('otaUploadBtn');
  const progressWrap = document.getElementById('otaProgressWrap');
  const progressBar = document.getElementById('otaProgressBar');
  const progressLbl = document.getElementById('otaProgressLabel');
  const statusEl    = document.getElementById('otaStatus');

  const chooseBtn   = document.getElementById('otaChooseBtn');

  if (!dropZone) return;

  // Choose File button opens native file picker
  chooseBtn.addEventListener('click', (e) => {
    e.stopPropagation();
    fileInput.click();
  });

  // Drag-and-drop visual feedback
  dropZone.addEventListener('dragover', (e) => {
    e.preventDefault();
    dropZone.classList.add('drag-over');
  });
  dropZone.addEventListener('dragleave', () => dropZone.classList.remove('drag-over'));
  dropZone.addEventListener('drop', (e) => {
    e.preventDefault();
    dropZone.classList.remove('drag-over');
    const file = e.dataTransfer.files[0];
    if (file) selectFile(file);
  });

  fileInput.addEventListener('change', () => {
    if (fileInput.files[0]) selectFile(fileInput.files[0]);
  });

  function selectFile(file) {
    if (!file.name.endsWith('.bin')) {
      setOtaStatus('Please select a .bin firmware file.', 'error');
      return;
    }
    fileInput._selectedFile = file;
    fileNameEl.textContent = file.name + ' (' + (file.size / 1024).toFixed(1) + ' KB)';
    dropZone.classList.add('file-selected');
    uploadBtn.disabled = false;
    setOtaStatus('');
  }

  uploadBtn.addEventListener('click', () => {
    const file = fileInput._selectedFile;
    if (!file) return;

    const formData = new FormData();
    formData.append('firmware', file, file.name);

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
        if (resp.status === 'ok') {
          setOtaStatus(resp.message || 'Update complete. Device is rebooting...', 'success');
          uploadBtn.disabled = true;
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

    xhr.open('POST', '/api/ota-update');
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
