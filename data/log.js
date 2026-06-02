const logCountEl = document.getElementById('logCount');
const timeStatusEl = document.getElementById('timeStatus');
const logRowsEl = document.getElementById('logRows');
const emptyLogsEl = document.getElementById('emptyLogs');
const refreshLogsButton = document.getElementById('refreshLogs');

function formatUptime(ms) {
  const totalSeconds = Math.floor(Number(ms) / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;

  return [
    String(hours).padStart(2, '0'),
    String(minutes).padStart(2, '0'),
    String(seconds).padStart(2, '0')
  ].join(':');
}

function formatTime(epoch) {
  const epochNumber = Number(epoch);

  if (!Number.isFinite(epochNumber) || epochNumber <= 0) {
    return 'Chưa đồng bộ giờ';
  }

  return new Date(epochNumber * 1000).toLocaleString('vi-VN');
}

async function syncDeviceTime() {
  const epoch = Math.floor(Date.now() / 1000);

  try {
    await fetch(`/syncTime?epoch=${encodeURIComponent(epoch)}`, { cache: 'no-store' });
  } catch (error) {
    console.warn('Không thể đồng bộ giờ với ESP32', error);
  }
}

function renderLogs(data) {
  const items = Array.isArray(data.items) ? data.items.slice().reverse() : [];
  logCountEl.textContent = data.count || 0;
  timeStatusEl.textContent = data.timeSynced ? 'Đã đồng bộ' : 'Chưa đồng bộ';

  emptyLogsEl.hidden = items.length > 0;
  logRowsEl.innerHTML = '';

  for (const item of items) {
    const row = document.createElement('tr');
    row.innerHTML = `
      <td>#${item.id}</td>
      <td>${formatTime(item.epoch)}</td>
      <td>${formatUptime(item.uptimeMs)}</td>
      <td class="speed-danger">${Number(item.speed).toFixed(1)} km/h</td>
      <td>${Number(item.limit).toFixed(1)} km/h</td>
    `;
    logRowsEl.appendChild(row);
  }
}

async function loadLogs() {
  try {
    const response = await fetch('/logs', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error('Không đọc được nhật ký');
    }

    const data = await response.json();
    renderLogs(data);
  } catch (error) {
    timeStatusEl.textContent = error.message;
  }
}

refreshLogsButton.addEventListener('click', loadLogs);

syncDeviceTime().then(loadLogs);
setInterval(loadLogs, 1000);
