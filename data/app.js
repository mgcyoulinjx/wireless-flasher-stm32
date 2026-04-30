const deviceInfo = document.getElementById('deviceInfo');
const chipInfo = document.getElementById('chipInfo');
const statusSummary = document.getElementById('statusSummary');
const statusLog = document.getElementById('statusLog');
const uploadLog = document.getElementById('uploadLog');
const progressBar = document.getElementById('progressBar');
const hexFile = document.getElementById('hexFile');
const manifestFile = document.getElementById('manifestFile');
const firmwareFile = document.getElementById('firmwareFile');
const refreshDeviceButton = document.getElementById('refreshDeviceButton');
const readChipButton = document.getElementById('readChipButton');
const flashPackageSelect = document.getElementById('flashPackageSelect');
const transportSelect = document.getElementById('transportSelect');
const modeSelect = document.getElementById('modeSelect');
const flashHint = document.getElementById('flashHint');
const manifestExample = document.getElementById('manifestExample');
const wiringTemplate = document.getElementById('wiringTemplate');
const storageInfo = document.getElementById('storageInfo');
const firmwareNameInput = document.getElementById('firmwareNameInput');
const savedPackageSelect = document.getElementById('savedPackageSelect');
const savedPackageLog = document.getElementById('savedPackageLog');
const savePackageButton = document.getElementById('savePackageButton');
const deleteSavedPackageButton = document.getElementById('deleteSavedPackageButton');

let transportInitialized = false;
let latestPackageReady = false;

function setUploadLog(message) {
  const timestamp = new Date().toLocaleTimeString();
  const line = `[${timestamp}] ${message}`;
  uploadLog.textContent = uploadLog.textContent && uploadLog.textContent !== '等待上传。'
    ? `${uploadLog.textContent}\n${line}`
    : line;
}

function setSavedPackageLog(message) {
  const timestamp = new Date().toLocaleTimeString();
  savedPackageLog.textContent = `[${timestamp}] ${message}`;
}

function formatBytes(value) {
  const bytes = Number(value || 0);
  if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${bytes} B`;
}

async function request(url, options = {}) {
  const response = await fetch(url, options);
  const data = await response.json();
  if (!response.ok || data.ok === false) {
    throw new Error(data.error || data.message || '请求失败');
  }
  return data;
}

function updateFlashHint() {
  const selectedOption = flashPackageSelect.options[flashPackageSelect.selectedIndex];
  const selectedText = selectedOption ? `已选择烧录固件：${selectedOption.textContent.replace(/^已选择：/, '')}\n` : '';
  flashHint.textContent = `${selectedText}当前只保留 SWD：请连接 SWDIO11 / SWCLK12 / GND，不接 NRST。`;
}

function updateFlashControls(info = null) {
  transportSelect.value = 'swd';
  modeSelect.value = 'manual';
  updateFlashHint();
}

async function refreshInfo() {
  try {
    const info = await request('/api/info');
    if (!transportInitialized) {
      transportSelect.value = 'swd';
      transportInitialized = true;
    }
    latestPackageReady = Boolean(info.packageReady);
    const selectedTransport = 'swd';
    const packageText = info.packageReady
      ? ` | 固件: ${info.targetChip || '-'} @ 0x${Number(info.targetAddress || 0).toString(16).toUpperCase()} | ${info.totalBytes} bytes | CRC32 0x${Number(info.firmwareCrc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`
      : ' | 暂无已校验固件包';
    deviceInfo.textContent = `热点: ${info.ssid} | 地址: ${info.ip} | 选择接口: ${selectedTransport} | 当前设备接口: ${info.transport || 'swd'} | 当前状态: ${info.state}${packageText}`;
    wiringTemplate.textContent = `GND  -> STM32 GND\nGPIO${info.swdIoPin} -> STM32 SWDIO\nGPIO${info.swdClockPin} -> STM32 SWCLK`;
    updateFlashControls(info);
  } catch (error) {
    deviceInfo.textContent = error.message;
  }
}

async function refreshManifestExample() {
  try {
    const response = await fetch('/api/manifest/example');
    manifestExample.textContent = await response.text();
  } catch (error) {
    manifestExample.textContent = error.message;
  }
}

async function refreshPackages() {
  try {
    const data = await request('/api/packages');
    storageInfo.textContent = `LittleFS: 剩余 ${formatBytes(data.storageFreeBytes)} / 总 ${formatBytes(data.storageTotalBytes)}，已用 ${formatBytes(data.storageUsedBytes)}；固件上限 ${formatBytes(data.maxFirmwareSize)}，HEX 上传上限 ${formatBytes(data.maxHexUploadSize)}`;
    const savedPackageValue = savedPackageSelect.value;
    savedPackageSelect.innerHTML = '';
    flashPackageSelect.innerHTML = '';
    const packages = data.packages || [];
    const selectedId = data.selectedId || '';
    const activeOption = document.createElement('option');
    activeOption.value = 'active';
    activeOption.textContent = latestPackageReady ? '当前固件（已上传/已选择）' : '当前固件（暂无可烧录固件）';
    if (!selectedId) {
      activeOption.selected = true;
      activeOption.textContent = `已选择：${activeOption.textContent}`;
    }
    flashPackageSelect.appendChild(activeOption);
    if (packages.length === 0) {
      const option = document.createElement('option');
      option.value = '';
      option.textContent = '暂无已保存固件';
      savedPackageSelect.appendChild(option);
    } else {
      for (const item of packages) {
        const option = document.createElement('option');
        option.value = item.id;
        const address = `0x${Number(item.address || 0).toString(16).toUpperCase()}`;
        const crc = `0x${Number(item.crc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`;
        option.textContent = `${item.name || item.id} | ${formatBytes(item.size)} @ ${address} | CRC32 ${crc}`;
        savedPackageSelect.appendChild(option);
        const flashOption = document.createElement('option');
        flashOption.value = `saved:${item.id}`;
        flashOption.selected = item.id === selectedId;
        flashOption.textContent = `${item.id === selectedId ? '已选择：' : ''}已保存: ${option.textContent}`;
        flashPackageSelect.appendChild(flashOption);
      }
    }
    if (savedPackageValue && Array.from(savedPackageSelect.options).some(option => option.value === savedPackageValue)) {
      savedPackageSelect.value = savedPackageValue;
    }
    updateFlashHint();
    flashPackageSelect.disabled = !latestPackageReady && packages.length === 0;
    savePackageButton.disabled = false;
    deleteSavedPackageButton.disabled = packages.length === 0;
  } catch (error) {
    storageInfo.textContent = error.message;
  }
}

async function refreshStatus() {
  try {
    const status = await request('/api/status');
    const percent = status.totalBytes > 0 ? Math.round((status.bytesWritten / status.totalBytes) * 100) : 0;
    const addressHex = `0x${Number(status.targetAddress || 0).toString(16).toUpperCase()}`;
    const crcHex = `0x${Number(status.firmwareCrc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`;
    statusSummary.textContent = `${status.state} | 接口: ${status.transport || 'swd'} | 模式: ${status.targetMode} | 芯片: ${status.targetChip || '-'} | 地址: ${addressHex} | CRC32: ${crcHex} | ${status.bytesWritten}/${status.totalBytes}`;
    progressBar.value = percent;
    statusLog.textContent = status.log || (status.detectedChip ? `${status.message}\n${status.detectedChip}` : status.message);
  } catch (error) {
    statusLog.textContent = error.message;
  }
}

async function uploadSingle(file, uploadName = file.name) {
  setUploadLog(`准备上传: ${file.name}, ${file.size} bytes, 服务器文件名: ${uploadName}`);
  const formData = new FormData();
  formData.append('file', file, uploadName);
  setUploadLog('发送 /api/upload 请求...');
  const response = await fetch('/api/upload', { method: 'POST', body: formData });
  setUploadLog(`/api/upload 响应: HTTP ${response.status}`);
  const text = await response.text();
  setUploadLog(`/api/upload 返回: ${text || '(empty)'}`);
  let data = {};
  try {
    data = text ? JSON.parse(text) : {};
  } catch (error) {
    throw new Error(`上传响应不是 JSON: ${text}`);
  }
  if (!response.ok || data.ok === false) {
    throw new Error(data.error || '上传失败');
  }
  return data;
}

document.getElementById('uploadButton').addEventListener('click', async () => {
  uploadLog.textContent = '';
  setUploadLog('点击上传并校验');
  try {
    setUploadLog(`HEX 文件: ${hexFile.files[0] ? hexFile.files[0].name : '未选择'}`);
    setUploadLog(`manifest 文件: ${manifestFile.files[0] ? manifestFile.files[0].name : '未选择'}`);
    setUploadLog(`bin 文件: ${firmwareFile.files[0] ? firmwareFile.files[0].name : '未选择'}`);
    if (hexFile.files[0]) {
      setUploadLog('进入 Intel HEX 上传流程');
      await uploadSingle(hexFile.files[0], 'firmware.hex');
      setUploadLog('HEX 上传完成，开始请求 /api/upload/hex/finalize...');
      const result = await request('/api/upload/hex/finalize', { method: 'POST' });
      setUploadLog(`HEX 校验完成: ${result.message || 'ok'}`);
      if (result.size) {
        setUploadLog(`生成固件: ${result.size} bytes @ 0x${Number(result.address || 0).toString(16).toUpperCase()}, CRC32 0x${Number(result.crc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`);
      }
    } else {
      setUploadLog('未选择 HEX，检查旧格式 manifest.json + app.bin');
      if (!manifestFile.files[0] || !firmwareFile.files[0]) {
        throw new Error('请选择 Intel HEX，或同时选择 manifest.json 和 app.bin');
      }
      await uploadSingle(manifestFile.files[0]);
      await uploadSingle(firmwareFile.files[0]);
      setUploadLog('旧格式上传完成，开始请求 /api/upload/finalize...');
      const result = await request('/api/upload/finalize', { method: 'POST' });
      setUploadLog(`旧格式校验完成: ${result.message || 'ok'}`);
    }
    setUploadLog('刷新设备信息...');
    await refreshInfo();
    await refreshStatus();
    await refreshPackages();
    setUploadLog('上传并校验流程完成');
    statusLog.textContent = '上传并校验完成，详细过程见上传校验日志。';
  } catch (error) {
    setUploadLog(`错误: ${error.message}`);
  }
});

document.getElementById('flashButton').addEventListener('click', async () => {
  try {
    const selectedFirmware = flashPackageSelect.value || 'active';
    const savedPackageId = selectedFirmware.startsWith('saved:') ? selectedFirmware.substring(6) : '';
    const response = await request('/api/flash/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ transport: 'swd', mode: 'manual', savedPackageId })
    });
    statusLog.textContent = response.message;
    await refreshStatus();
  } catch (error) {
    statusLog.textContent = error.message;
  }
});

document.getElementById('cancelButton').addEventListener('click', async () => {
  try {
    const response = await request('/api/flash/cancel', { method: 'POST' });
    statusLog.textContent = response.message;
    await refreshStatus();
  } catch (error) {
    statusLog.textContent = error.message;
  }
});

document.getElementById('deleteButton').addEventListener('click', async () => {
  try {
    const response = await request('/api/package', { method: 'DELETE' });
    statusLog.textContent = response.message;
    progressBar.value = 0;
    await refreshInfo();
    await refreshStatus();
    await refreshPackages();
  } catch (error) {
    statusLog.textContent = error.message;
  }
});

savePackageButton.addEventListener('click', async () => {
  setSavedPackageLog('正在保存当前固件...');
  try {
    const result = await request('/api/packages/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: firmwareNameInput.value })
    });
    setSavedPackageLog(`已保存: ${result.name || result.id}`);
    firmwareNameInput.value = '';
    await refreshPackages();
  } catch (error) {
    setSavedPackageLog(error.message);
  }
});

async function switchFlashFirmware(id) {
  statusLog.textContent = id ? '正在切换烧录固件...' : '正在切换到当前固件...';
  try {
    const result = await request('/api/packages/select', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id })
    });
    statusLog.textContent = id ? `已切换烧录固件: ${result.chip || '-'} ${formatBytes(result.size)}。` : '已切换到当前固件。';
    await refreshInfo();
    await refreshStatus();
    await refreshPackages();
  } catch (error) {
    statusLog.textContent = error.message;
  }
}

flashPackageSelect.addEventListener('change', async () => {
  const selectedFirmware = flashPackageSelect.value || 'active';
  const id = selectedFirmware.startsWith('saved:') ? selectedFirmware.substring(6) : '';
  await switchFlashFirmware(id);
});

deleteSavedPackageButton.addEventListener('click', async () => {
  const id = savedPackageSelect.value;
  if (!id) {
    setSavedPackageLog('请选择一个已保存固件。');
    return;
  }
  setSavedPackageLog('正在删除已保存固件...');
  try {
    const result = await request('/api/packages/delete', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id })
    });
    setSavedPackageLog(result.message || '已删除已保存固件');
    await refreshPackages();
  } catch (error) {
    setSavedPackageLog(error.message);
  }
});

transportSelect.addEventListener('change', () => updateFlashControls());
refreshDeviceButton.addEventListener('click', async () => {
  await refreshInfo();
  await refreshStatus();
});

readChipButton.addEventListener('click', async () => {
  chipInfo.textContent = '正在通过 SWD 读取芯片信息...';
  try {
    const info = await request('/api/target/chip-info', { method: 'POST' });
    chipInfo.textContent =
      `芯片: ${info.chipName || '-'}\n` +
      `DPIDR: 0x${Number(info.dpidr || 0).toString(16).toUpperCase()}\n` +
      `DBGMCU_IDCODE: 0x${Number(info.dbgmcuIdcode || 0).toString(16).toUpperCase()}\n` +
      `CHIP_ID: 0x${Number(info.chipId || 0).toString(16).toUpperCase()}\n` +
      `${info.lineSample || ''}`;
  } catch (error) {
    chipInfo.textContent = error.message;
  }
});

refreshInfo();
refreshStatus();
refreshPackages();
refreshManifestExample();
updateFlashControls();
setInterval(refreshStatus, 1000);
setInterval(refreshInfo, 5000);
setInterval(refreshPackages, 5000);
