const speedEl = document.getElementById('speed');
const limitEl = document.getElementById('limit');
const statusEl = document.getElementById('status');
const violationsEl = document.getElementById('violations');
const lastViolationEl = document.getElementById('lastViolation');
const maxSpeedEl = document.getElementById('maxSpeed');
const measurementsEl = document.getElementById('measurements');
const limitInput = document.getElementById('limitInput');
const updateLimitButton = document.getElementById('updateLimit');
const messageEl = document.getElementById('message');
const historyRowsEl = document.getElementById('historyRows');
const emptyHistoryEl = document.getElementById('emptyHistory');
const refreshHistoryButton = document.getElementById('refreshHistory');
const chartCountEl = document.getElementById('chartCount');
const speedChart = document.getElementById('speedChart');
const chartContext = speedChart.getContext('2d');

let latestLimit = 60;
let historyItems = [];

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

function formatTime(epoch) {
  const epochNumber = Number(epoch);

  if (!Number.isFinite(epochNumber) || epochNumber <= 0) {
    return 'Chưa đồng bộ giờ';
  }

  return new Date(epochNumber * 1000).toLocaleString('vi-VN');
}

function formatSpeed(value) {
  const number = Number(value);
  return Number.isFinite(number) ? number.toFixed(1) : '0.0';
}

function statusLabel(status) {
  return statusText[status] || status;
}

function statusClass(status) {
  if (status === 'OVER_SPEED') {
    return 'over';
  }

  if (status === 'SAFE') {
    return 'safe';
  }

  return 'neutral';
}

function prepareChart() {
  const rect = speedChart.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(300, Math.floor(rect.width));
  const height = Math.max(220, Math.floor(rect.height || 260));
  const targetWidth = Math.floor(width * dpr);
  const targetHeight = Math.floor(height * dpr);

  if (speedChart.width !== targetWidth || speedChart.height !== targetHeight) {
    speedChart.width = targetWidth;
    speedChart.height = targetHeight;
  }

  chartContext.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { width, height };
}

function drawChart() {
  const { width, height } = prepareChart();
  const ctx = chartContext;
  const items = historyItems.slice(-30);
  const padding = { top: 18, right: 18, bottom: 34, left: 48 };
  const chartWidth = width - padding.left - padding.right;
  const chartHeight = height - padding.top - padding.bottom;

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = '#ffffff';
  ctx.fillRect(0, 0, width, height);

  if (items.length === 0) {
    ctx.fillStyle = '#64748b';
    ctx.font = '700 15px Arial';
    ctx.textAlign = 'center';
    ctx.fillText('Chưa có dữ liệu đo', width / 2, height / 2);
    return;
  }

  const maxItemSpeed = Math.max(...items.map((item) => Number(item.speed) || 0));
  const yMax = Math.max(10, Math.ceil(Math.max(maxItemSpeed, latestLimit) * 1.2 / 10) * 10);

  ctx.strokeStyle = '#d8e1ea';
  ctx.lineWidth = 1;
  ctx.fillStyle = '#64748b';
  ctx.font = '700 12px Arial';
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';

  for (let i = 0; i <= 4; i++) {
    const ratio = i / 4;
    const y = padding.top + chartHeight * ratio;
    const value = yMax - yMax * ratio;

    ctx.beginPath();
    ctx.moveTo(padding.left, y);
    ctx.lineTo(width - padding.right, y);
    ctx.stroke();
    ctx.fillText(value.toFixed(0), padding.left - 8, y);
  }

  const xFor = (index) => {
    if (items.length === 1) {
      return padding.left + chartWidth / 2;
    }

    return padding.left + (chartWidth * index) / (items.length - 1);
  };

  const yFor = (speed) => padding.top + chartHeight - (chartHeight * speed) / yMax;

  const limitY = yFor(latestLimit);
  ctx.save();
  ctx.setLineDash([6, 6]);
  ctx.strokeStyle = '#b91c1c';
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.moveTo(padding.left, limitY);
  ctx.lineTo(width - padding.right, limitY);
  ctx.stroke();
  ctx.restore();

  ctx.fillStyle = '#b91c1c';
  ctx.textAlign = 'left';
  ctx.fillText(`Giới hạn ${latestLimit.toFixed(1)}`, padding.left + 6, Math.max(14, limitY - 10));

  ctx.strokeStyle = '#2563eb';
  ctx.lineWidth = 3;
  ctx.beginPath();
  items.forEach((item, index) => {
    const x = xFor(index);
    const y = yFor(Number(item.speed) || 0);

    if (index === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  });
  ctx.stroke();

  items.forEach((item, index) => {
    const x = xFor(index);
    const y = yFor(Number(item.speed) || 0);

    ctx.fillStyle = item.status === 'OVER_SPEED' ? '#b91c1c' : '#15803d';
    ctx.beginPath();
    ctx.arc(x, y, 4, 0, Math.PI * 2);
    ctx.fill();
  });

  ctx.fillStyle = '#64748b';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  ctx.fillText(`#${items[0].id}`, xFor(0), height - padding.bottom + 12);
  ctx.fillText(`#${items[items.length - 1].id}`, xFor(items.length - 1), height - padding.bottom + 12);
}

function renderHistory(items) {
  const latestItems = items.slice().reverse().slice(0, 12);
  emptyHistoryEl.hidden = latestItems.length > 0;
  historyRowsEl.innerHTML = '';

  for (const item of latestItems) {
    const row = document.createElement('tr');
    row.innerHTML = `
      <td>#${item.id}</td>
      <td>${formatTime(item.epoch)}</td>
      <td class="${item.status === 'OVER_SPEED' ? 'speed-danger' : ''}">${formatSpeed(item.speed)} km/h</td>
      <td>${formatSpeed(item.limit)} km/h</td>
      <td><span class="status-pill ${statusClass(item.status)}">${statusLabel(item.status)}</span></td>
    `;
    historyRowsEl.appendChild(row);
  }
}

async function loadData() {
  try {
    const response = await fetch('/data', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error('Không đọc được dữ liệu');
    }

    const data = await response.json();
    latestLimit = Number(data.limit) || latestLimit;

    speedEl.textContent = formatSpeed(data.speed);
    limitEl.textContent = latestLimit.toFixed(1);
    violationsEl.textContent = data.violations;
    lastViolationEl.textContent = formatSpeed(data.lastViolation);
    maxSpeedEl.textContent = formatSpeed(data.maxSpeed);
    measurementsEl.textContent = data.measurements || 0;
    setStatus(data.status);
    drawChart();
  } catch (error) {
    setMessage(error.message, 'error');
  }
}

async function loadHistory() {
  try {
    const response = await fetch('/history', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error('Không đọc được lịch sử đo');
    }

    const data = await response.json();
    historyItems = Array.isArray(data.items) ? data.items : [];
    chartCountEl.textContent = `${data.count || 0} điểm đo`;
    renderHistory(historyItems);
    drawChart();
  } catch (error) {
    setMessage(error.message, 'error');
  }
}

async function syncDeviceTime() {
  const epoch = Math.floor(Date.now() / 1000);

  try {
    await fetch(`/syncTime?epoch=${encodeURIComponent(epoch)}`, { cache: 'no-store' });
  } catch (error) {
    console.warn('Không thể đồng bộ giờ với ESP32', error);
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

    latestLimit = value;
    setMessage('Đã cập nhật giới hạn tốc độ.', 'ok');
    await loadData();
    await loadHistory();
  } catch (error) {
    setMessage(error.message, 'error');
  }
}

updateLimitButton.addEventListener('click', updateLimit);
refreshHistoryButton.addEventListener('click', loadHistory);
limitInput.addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    updateLimit();
  }
});

window.addEventListener('resize', drawChart);

syncDeviceTime();
loadData();
loadHistory();
setInterval(loadData, 500);
setInterval(loadHistory, 1000);
