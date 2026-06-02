const speedEl = document.getElementById('speed');
const limitEl = document.getElementById('limit');
const statusEl = document.getElementById('status');
const violationsEl = document.getElementById('violations');
const lastViolationEl = document.getElementById('lastViolation');
const limitInput = document.getElementById('limitInput');
const updateLimitButton = document.getElementById('updateLimit');
const messageEl = document.getElementById('message');

const statusText = {
  READY: 'ĐANG CHỜ',
  MEASURING: 'ĐANG ĐO',
  SAFE: 'AN TOÀN',
  OVER_SPEED: 'VƯỢT TỐC ĐỘ',
  TIMEOUT: 'HẾT THỜI GIAN'
};

function setStatus(status) {
  statusEl.textContent = statusText[status] || status;
  statusEl.classList.remove('safe', 'over', 'neutral');

  if (status === 'OVER_SPEED') {
    statusEl.classList.add('over');
    return;
  }

  if (status === 'SAFE') {
    statusEl.classList.add('safe');
    return;
  }

  statusEl.classList.add('neutral');
}

function setMessage(text, type) {
  messageEl.textContent = text;
  messageEl.classList.remove('ok', 'error');

  if (type) {
    messageEl.classList.add(type);
  }
}

async function loadData() {
  try {
    const response = await fetch('/data', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error('Không đọc được dữ liệu');
    }

    const data = await response.json();
    speedEl.textContent = Number(data.speed).toFixed(1);
    limitEl.textContent = Number(data.limit).toFixed(1);
    violationsEl.textContent = data.violations;
    lastViolationEl.textContent = Number(data.lastViolation).toFixed(1);
    setStatus(data.status);
  } catch (error) {
    setMessage(error.message, 'error');
  }
}

async function updateLimit() {
  const value = Number(limitInput.value);

  if (!Number.isFinite(value) || value <= 0 || value > 300) {
    setMessage('Giới hạn phải lớn hơn 0 và không quá 300 km/h.', 'error');
    return;
  }

  try {
    const response = await fetch(`/setLimit?value=${encodeURIComponent(value)}`);
    if (!response.ok) {
      throw new Error('Không thể cập nhật giới hạn');
    }

    setMessage('Đã cập nhật giới hạn tốc độ.', 'ok');
    await loadData();
  } catch (error) {
    setMessage(error.message, 'error');
  }
}

updateLimitButton.addEventListener('click', updateLimit);
limitInput.addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    updateLimit();
  }
});

loadData();
setInterval(loadData, 500);
