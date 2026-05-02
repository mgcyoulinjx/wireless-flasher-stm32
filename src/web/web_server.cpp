#include "web_server.h"

#include <LittleFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include "app_config.h"
#include "ap_manager.h"
#include "flash_manager.h"
#include "package_store.h"
#include "hal/target_control.h"
#include "flash/stm32_swd_debug.h"
#include "flash/stm32_chip_info.h"

namespace {
bool isHexUploadName(const String &filename) {
  String lower = filename;
  lower.toLowerCase();
  return lower.endsWith(".hex") || lower.endsWith(".ihx");
}

const char kEmbeddedIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Exlink STM32 无线烧录器</title>
    <style>
      body { font-family: sans-serif; margin: 0; background: #0f172a; color: #e2e8f0; }
      main { max-width: 760px; margin: 0 auto; padding: 24px; }
      .card { background: #111827; border: 1px solid #334155; border-radius: 12px; padding: 16px; margin-bottom: 16px; }
      h1,h2 { margin-top: 0; }
      button { background: #2563eb; color: white; border: 0; padding: 10px 14px; border-radius: 8px; cursor: pointer; }
      button.secondary { background: #475569; }
      button.danger { background: #dc2626; }
      input, select { box-sizing: border-box; max-width: 100%; width: 100%; margin: 8px 0 12px; padding: 10px; border-radius: 8px; border: 1px solid #475569; background: #020617; color: #e2e8f0; }
      .row { display: flex; gap: 12px; flex-wrap: wrap; }
      .row > * { flex: 1 1 200px; }
      progress { width: 100%; height: 18px; }
      pre { white-space: pre-wrap; word-break: break-word; background: #020617; padding: 12px; border-radius: 8px; }
      .log-window { position: relative; margin: 1em 0; }
      .log-window pre { margin: 0; padding-top: 42px; }
      .copy-log-button { position: absolute; top: 8px; right: 8px; background: #334155; padding: 6px 10px; font-size: 12px; }
      .copy-log-button.copied { background: #16a34a; }
      .muted { color: #94a3b8; }
    </style>
  </head>
  <body>
    <main>
      <h1>Exlink STM32 无线烧录器</h1>
      <p class="muted">内置网页版本：embedded-upload-log-v3</p>
      <div class="card">
        <div class="row">
          <h2>设备信息</h2>
          <button id="refreshDeviceButton" class="secondary">刷新设备</button>
          <button id="readChipButton" class="secondary">读取芯片信息</button>
        </div>
        <div id="deviceInfo" class="muted">正在加载...</div>
        <pre id="chipInfo">未读取芯片信息。</pre>
      </div>
      <div class="card">
        <h2>默认接线模板</h2>
        <pre id="wiringTemplate">正在加载接线信息...</pre>
      </div>
      <div class="card">
        <h2>上传固件包</h2>
        <p class="muted">上传 Intel HEX 文件，设备会自动解析地址并生成烧录包。</p>
        <label>Intel HEX</label>
        <input id="hexFile" type="file" accept=".hex,.ihx" />
        <div class="row">
          <button id="uploadButton" onclick="document.getElementById('uploadLog').textContent='按钮已点击，内置页面事件已触发。';">上传并校验</button>
          <button id="deleteButton" class="secondary">删除当前固件包</button>
        </div>
        <h3>上传校验日志</h3>
        <pre id="uploadLog">等待上传。</pre>
      </div>
      <div class="card">
        <h2>固件保存列表</h2>
        <p class="muted">当前固件会直接用于烧录；保存列表可在以后重新选择，无需再次上传。</p>
        <div id="storageInfo" class="muted">正在加载存储空间...</div>
        <label>保存名称</label>
        <input id="firmwareNameInput" type="text" placeholder="例如：出厂固件 v1" />
        <div class="row">
          <button id="savePackageButton" class="secondary">保存当前固件</button>
        </div>
        <label>已保存固件</label>
        <select id="savedPackageSelect"></select>
        <div class="row">
          <button id="deleteSavedPackageButton" class="danger">删除已保存固件</button>
        </div>
        <pre id="savedPackageLog">等待操作。</pre>
      </div>
      <div class="card">
        <h2>开始烧录</h2>
        <label>烧录固件</label>
        <select id="flashPackageSelect"></select>
        <p id="flashHint" class="muted">当前只保留 SWD：请连接 SWDIO11 / SWCLK12 / GND，不接 NRST。</p>
        <div class="row">
          <button id="flashButton">开始烧录</button>
          <button id="cancelButton" class="danger">取消</button>
        </div>
      </div>
      <div class="card">
        <h2>状态</h2>
        <div id="statusSummary">-</div>
        <progress id="progressBar" value="0" max="100"></progress>
        <pre id="statusLog">等待操作。</pre>
      </div>
    </main>
)HTML";

const char kEmbeddedIndexHtmlSuffix[] PROGMEM = R"HTML(  </body>
</html>
)HTML";

const char kEmbeddedAppJs[] PROGMEM = R"JS(const deviceInfo = document.getElementById('deviceInfo');
const chipInfo = document.getElementById('chipInfo');
const statusSummary = document.getElementById('statusSummary');
const statusLog = document.getElementById('statusLog');
const uploadLog = document.getElementById('uploadLog');
const progressBar = document.getElementById('progressBar');
const hexFile = document.getElementById('hexFile');
const refreshDeviceButton = document.getElementById('refreshDeviceButton');
const readChipButton = document.getElementById('readChipButton');
const flashPackageSelect = document.getElementById('flashPackageSelect');
const flashHint = document.getElementById('flashHint');
const wiringTemplate = document.getElementById('wiringTemplate');
const storageInfo = document.getElementById('storageInfo');
const firmwareNameInput = document.getElementById('firmwareNameInput');
const savedPackageSelect = document.getElementById('savedPackageSelect');
const savedPackageLog = document.getElementById('savedPackageLog');
const savePackageButton = document.getElementById('savePackageButton');
const deleteSavedPackageButton = document.getElementById('deleteSavedPackageButton');

let latestPackageReady = false;

async function copyText(text) {
  if (navigator.clipboard && window.isSecureContext) {
    await navigator.clipboard.writeText(text);
    return;
  }
  const textArea = document.createElement('textarea');
  textArea.value = text;
  textArea.style.position = 'fixed';
  textArea.style.left = '-9999px';
  document.body.appendChild(textArea);
  textArea.focus();
  textArea.select();
  document.execCommand('copy');
  textArea.remove();
}

function setupLogCopyButtons() {
  for (const log of document.querySelectorAll('pre')) {
    if (log.parentElement && log.parentElement.classList.contains('log-window')) {
      continue;
    }
    const wrapper = document.createElement('div');
    wrapper.className = 'log-window';
    log.parentNode.insertBefore(wrapper, log);
    wrapper.appendChild(log);
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'copy-log-button';
    button.textContent = '复制';
    button.addEventListener('click', async () => {
      try {
        await copyText(log.textContent || '');
        button.textContent = '已复制';
        button.classList.add('copied');
        setTimeout(() => {
          button.textContent = '复制';
          button.classList.remove('copied');
        }, 1200);
      } catch (error) {
        button.textContent = '复制失败';
        setTimeout(() => { button.textContent = '复制'; }, 1200);
      }
    });
    wrapper.appendChild(button);
  }
}

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

function updateFlashControls() {
  updateFlashHint();
}

async function refreshInfo() {
  try {
    const info = await request('/api/info');
    latestPackageReady = Boolean(info.packageReady);
    const packageText = info.packageReady
      ? ` | 固件: ${info.targetChip || '-'} @ 0x${Number(info.targetAddress || 0).toString(16).toUpperCase()} | ${info.totalBytes} bytes | CRC32 0x${Number(info.firmwareCrc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`
      : ' | 暂无已校验固件包';
    const detectedText = info.detectedChip ? ` | 当前芯片: ${info.detectedChip}` : ' | 当前芯片: 未检测';
    deviceInfo.textContent = `热点: ${info.ssid} | 地址: ${info.ip} | 接口: SWD${detectedText} | 当前状态: ${info.state}${packageText}`;
    wiringTemplate.textContent = `GND  -> STM32 GND\nGPIO${info.swdIoPin} -> STM32 SWDIO\nGPIO${info.swdClockPin} -> STM32 SWCLK`;
    updateFlashControls(info);
  } catch (error) {
    deviceInfo.textContent = error.message;
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
        option.dataset.name = item.name || '';
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
    statusSummary.textContent = `${status.state} | 接口: SWD | 芯片: ${status.targetChip || '-'} | 地址: ${addressHex} | CRC32: ${crcHex} | ${status.bytesWritten}/${status.totalBytes}`;
    progressBar.value = percent;
    statusLog.textContent = status.log || (status.detectedChip ? `${status.message}\n${status.detectedChip}` : status.message);
  } catch (error) {
    statusLog.textContent = error.message;
  }
}

async function uploadSingle(file, uploadName = file.name) {
  setUploadLog(`准备上传: ${file.name}, ${file.size} bytes, 服务器文件名: ${uploadName}`);
  const startedAt = performance.now();
  const formData = new FormData();
  formData.append('file', file, uploadName);
  setUploadLog('发送 /api/upload 请求...');
  const response = await fetch('/api/upload', { method: 'POST', body: formData });
  setUploadLog(`/api/upload 响应: HTTP ${response.status}, 耗时 ${Math.round(performance.now() - startedAt)} ms`);
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
    if (!hexFile.files[0]) {
      throw new Error('请选择 Intel HEX 文件');
    }
    setUploadLog('进入 Intel HEX 上传流程');
    await uploadSingle(hexFile.files[0], 'firmware.hex');
    setUploadLog('HEX 上传完成，开始请求 /api/upload/hex/finalize...');
    const finalizeStartedAt = performance.now();
    const result = await request('/api/upload/hex/finalize', { method: 'POST' });
    setUploadLog(`/api/upload/hex/finalize 完成，耗时 ${Math.round(performance.now() - finalizeStartedAt)} ms`);
    setUploadLog(`HEX 校验完成: ${result.message || 'ok'}`);
    if (result.hexSize) {
      setUploadLog(`HEX 原始大小: ${result.hexSize} bytes`);
    }
    if (result.size) {
      setUploadLog(`生成固件: ${result.size} bytes @ 0x${Number(result.address || 0).toString(16).toUpperCase()}, CRC32 0x${Number(result.crc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`);
    }
    if (result.freeBytes !== undefined) {
      setUploadLog(`LittleFS: free ${result.freeBytes} bytes, used ${result.usedBytes} / ${result.totalBytes}`);
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
      body: JSON.stringify({ savedPackageId })
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
    await refreshPackages();
    const requestedName = firmwareNameInput.value.trim();
    let replaceId = '';
    const duplicateOption = requestedName
      ? Array.from(savedPackageSelect.options).find(option => option.dataset.name === requestedName)
      : null;
    if (duplicateOption) {
      const replace = confirm(`已存在名为“${requestedName}”的固件。\n\n确定：替换已有固件\n取消：共存并自动添加序号`);
      if (replace) {
        replaceId = duplicateOption.value;
      }
    }
    const result = await request('/api/packages/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: requestedName, replaceId })
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
      `族/后端: ${info.family || '-'}\n` +
      `Flash: 0x${Number(info.flashStart || 0).toString(16).toUpperCase()} - 0x${Number(info.flashEnd || 0).toString(16).toUpperCase()}\n` +
      `DPIDR: 0x${Number(info.dpidr || 0).toString(16).toUpperCase()}\n` +
      `DBGMCU_IDCODE: 0x${Number(info.dbgmcuIdcode || 0).toString(16).toUpperCase()}\n` +
      `CHIP_ID: 0x${Number(info.chipId || 0).toString(16).toUpperCase()}\n` +
      `${info.lineSample || ''}`;
  } catch (error) {
    chipInfo.textContent = error.message;
  }
});

setupLogCopyButtons();
refreshInfo();
refreshStatus();
refreshPackages();
updateFlashControls();
setInterval(refreshStatus, 1000);
setInterval(refreshInfo, 5000);
setInterval(refreshPackages, 5000);
)JS";
}

AppWebServer::AppWebServer(AccessPointManager &apManager,
                           PackageStore &packageStore,
                           FlashManager &flashManager,
                           TargetControl &targetControl,
                           Stm32SwdDebug &swdDebug)
    : apManager_(apManager),
      packageStore_(packageStore),
      flashManager_(flashManager),
      targetControl_(targetControl),
      swdDebug_(swdDebug) {}

void AppWebServer::begin() {
  server_ = new WebServer(80);
  configureRoutes();
  server_->begin();
}

void AppWebServer::handleClient() {
  if (server_) {
    server_->handleClient();
  }
}

void AppWebServer::configureRoutes() {
  server_->on("/", HTTP_GET, [this]() { handleIndex(); });
  server_->on("/app.js", HTTP_GET, [this]() {
    server_->sendHeader("Cache-Control", "no-store");
    server_->send_P(200, "application/javascript", kEmbeddedAppJs);
  });

  server_->on("/api/info", HTTP_GET, [this]() { handleInfo(); });
  server_->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_->on("/api/flash/start", HTTP_POST, [this]() { handleFlashStart(); });
  server_->on("/api/flash/cancel", HTTP_POST, [this]() { handleFlashCancel(); });
  server_->on("/api/package", HTTP_DELETE, [this]() { handleDeletePackage(); });
  server_->on(
      "/api/upload", HTTP_POST,
      [this]() {
        JsonDocument doc;
        doc["ok"] = true;
        doc["message"] = "File received";
        String payload;
        serializeJson(doc, payload);
        sendJson(200, payload);
      },
      [this]() {
        HTTPUpload &upload = server_->upload();
        String error;

        if (upload.status == UPLOAD_FILE_START) {
          if (!isHexUploadName(upload.filename)) {
            sendError(400, "Only Intel HEX files are supported");
            return;
          }
        }

        if (upload.status == UPLOAD_FILE_WRITE) {
          const bool reset = upload.totalSize == 0;
          if (!packageStore_.appendIntelHexChunk(upload.buf, upload.currentSize, reset, error)) {
            sendError(400, error);
          }
        }

        if (upload.status == UPLOAD_FILE_END) {
          return;
        }

        if (upload.status == UPLOAD_FILE_ABORTED) {
          return;
        }
      });
  server_->on("/api/upload/hex/finalize", HTTP_POST, [this]() { handleHexUploadFinalize(); });
  server_->on("/api/packages", HTTP_GET, [this]() { handlePackages(); });
  server_->on("/api/packages/save", HTTP_POST, [this]() { handleSavePackage(); });
  server_->on("/api/packages/select", HTTP_POST, [this]() { handleSelectPackage(); });
  server_->on("/api/packages/delete", HTTP_POST, [this]() { handleDeleteSavedPackage(); });
  server_->on("/api/target/chip-info", HTTP_POST, [this]() { handleChipInfo(); });
}

void AppWebServer::handleIndex() {
  server_->sendHeader("Cache-Control", "no-store");
  server_->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_->send(200, "text/html; charset=utf-8", "");
  server_->sendContent_P(kEmbeddedIndexHtml);
  server_->sendContent("    <script>\n");
  server_->sendContent_P(kEmbeddedAppJs);
  server_->sendContent("\n    </script>\n");
  server_->sendContent_P(kEmbeddedIndexHtmlSuffix);
}

void AppWebServer::handleInfo() {
  JsonDocument doc;
  FlashStatus status = flashManager_.status();
  doc["name"] = "Exlink STM32 Wireless Flasher";
  doc["ssid"] = apManager_.ssid();
  doc["ip"] = apManager_.ipAddress();
  doc["passwordRequired"] = apManager_.hasPassword();
  doc["packageReady"] = status.packageReady;
  doc["swdIoPin"] = AppConfig::kSwdIoPin;
  doc["swdClockPin"] = AppConfig::kSwdClockPin;
  doc["swdResetPin"] = AppConfig::kSwdResetPin;
  doc["recommendedWiring"] = AppConfig::kRecommendedSwdWiringSummary;
  doc["targetChip"] = status.targetChip;
  doc["detectedChip"] = status.detectedChip;
  doc["detectedChipId"] = status.detectedChipId;
  doc["flashBackend"] = status.flashBackend;
  doc["targetAddress"] = status.targetAddress;
  doc["firmwareCrc32"] = status.firmwareCrc32;
  doc["totalBytes"] = status.totalBytes;
  doc["storageTotalBytes"] = packageStore_.totalBytes();
  doc["storageUsedBytes"] = packageStore_.usedBytes();
  doc["storageFreeBytes"] = packageStore_.freeBytes();
  doc["maxFirmwareSize"] = AppConfig::kMaxFirmwareSize;
  doc["maxHexUploadSize"] = AppConfig::kMaxHexUploadSize;
  doc["state"] = status.stateLabel;
  doc["message"] = status.message;
  doc["log"] = status.log;

  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleStatus() {
  JsonDocument doc;
  FlashStatus status = flashManager_.status();
  doc["state"] = status.stateLabel;
  doc["message"] = status.message;
  doc["log"] = status.log;
  doc["targetChip"] = status.targetChip;
  doc["detectedChip"] = status.detectedChip;
  doc["detectedChipId"] = status.detectedChipId;
  doc["flashBackend"] = status.flashBackend;
  doc["targetAddress"] = status.targetAddress;
  doc["firmwareCrc32"] = status.firmwareCrc32;
  doc["bytesWritten"] = status.bytesWritten;
  doc["totalBytes"] = status.totalBytes;
  doc["packageReady"] = status.packageReady;

  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleHexUploadFinalize() {
  size_t hexSize = 0;
  if (LittleFS.exists(AppConfig::kHexTempPath)) {
    File hexFile = LittleFS.open(AppConfig::kHexTempPath, FILE_READ);
    if (hexFile) {
      hexSize = hexFile.size();
      hexFile.close();
    }
  }
  String error;
  if (!packageStore_.finalizeIntelHexPackage(error)) {
    sendError(400, error);
    return;
  }
  if (!flashManager_.setPackageReady(error)) {
    sendError(400, error);
    return;
  }

  FlashStatus status = flashManager_.status();
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Intel HEX converted successfully";
  doc["hexSize"] = hexSize;
  doc["freeBytes"] = packageStore_.freeBytes();
  doc["usedBytes"] = packageStore_.usedBytes();
  doc["totalBytes"] = packageStore_.totalBytes();
  doc["chip"] = status.targetChip;
  doc["address"] = status.targetAddress;
  doc["size"] = status.totalBytes;
  doc["crc32"] = status.firmwareCrc32;
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleChipInfo() {
  FlashStatus status = flashManager_.status();
  if (FlashManager::isBusyState(status.state)) {
    sendError(409, "Flash job is busy");
    return;
  }

  String error;
  if (!targetControl_.prepareForSwd(error)) {
    sendError(500, "SWD prepare failed: " + error);
    return;
  }

  String lineSample;
  swdDebug_.sampleLineLevels(lineSample);

  if (!swdDebug_.connect(error)) {
    sendError(500, "SWD connect failed: " + error);
    return;
  }

  uint32_t dpId = 0;
  if (!swdDebug_.readDebugPortId(dpId, error)) {
    sendError(500, "SWD DPIDR read failed: " + error);
    return;
  }

  if (!swdDebug_.halt(error)) {
    sendError(500, "SWD halt failed: " + error);
    return;
  }

  uint32_t dbgmcuIdcode = 0;
  if (!swdDebug_.readStm32DebugId(dbgmcuIdcode, error)) {
    sendError(500, "SWD chip ID read failed: " + error);
    return;
  }

  const uint32_t chipId = dbgmcuIdcode & 0x0FFFU;
  const Stm32ChipInfo &chip = stm32ChipInfo(chipId);
  flashManager_.setDetectedChip(chipId);

  JsonDocument doc;
  doc["ok"] = true;
  doc["dpidr"] = dpId;
  doc["dbgmcuIdcode"] = dbgmcuIdcode;
  doc["chipId"] = chipId;
  doc["chipName"] = stm32ChipDisplayName(chipId);
  doc["family"] = stm32FamilyName(chip.family);
  doc["flashStart"] = chip.flashStart;
  doc["flashEnd"] = chip.flashEnd;
  doc["lineSample"] = lineSample;
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handlePackages() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["storageTotalBytes"] = packageStore_.totalBytes();
  doc["storageUsedBytes"] = packageStore_.usedBytes();
  doc["storageFreeBytes"] = packageStore_.freeBytes();
  doc["maxFirmwareSize"] = AppConfig::kMaxFirmwareSize;
  doc["maxHexUploadSize"] = AppConfig::kMaxHexUploadSize;
  String error;
  doc["selectedId"] = packageStore_.selectedSavedPackageId(error);
  if (!error.isEmpty()) {
    sendError(400, error);
    return;
  }
  JsonArray packages = doc["packages"].to<JsonArray>();
  if (!packageStore_.listSavedPackages(packages, error)) {
    sendError(400, error);
    return;
  }
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleSavePackage() {
  FlashStatus status = flashManager_.status();
  if (FlashManager::isBusyState(status.state)) {
    sendError(409, "Flash job is busy");
    return;
  }

  String name;
  String replaceId;
  if (server_->hasArg("plain")) {
    JsonDocument request;
    if (deserializeJson(request, server_->arg("plain")) == DeserializationError::Ok) {
      name = request["name"] | "";
      replaceId = request["replaceId"] | "";
    }
  }

  String error;
  SavedPackageInfo info;
  if (!packageStore_.saveActivePackage(name, info, error, replaceId)) {
    sendError(400, error);
    return;
  }

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Firmware saved";
  doc["id"] = info.id;
  doc["name"] = info.name;
  doc["chip"] = info.chip;
  doc["address"] = info.address;
  doc["size"] = info.size;
  doc["crc32"] = info.crc32;
  doc["storageFreeBytes"] = packageStore_.freeBytes();
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleSelectPackage() {
  FlashStatus status = flashManager_.status();
  if (FlashManager::isBusyState(status.state)) {
    sendError(409, "Flash job is busy");
    return;
  }

  JsonDocument request;
  if (!server_->hasArg("plain") || deserializeJson(request, server_->arg("plain")) != DeserializationError::Ok) {
    sendError(400, "Saved package id is required");
    return;
  }
  String id = request["id"] | "";

  String error;
  if (id.isEmpty()) {
    if (!packageStore_.clearSelectedSavedPackage(error)) {
      sendError(400, error);
      return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["message"] = "Active firmware selected";
    String payload;
    serializeJson(doc, payload);
    sendJson(200, payload);
    return;
  }

  if (!packageStore_.restoreSavedPackage(id, error)) {
    sendError(400, error);
    return;
  }
  if (!flashManager_.setPackageReady(error)) {
    sendError(400, error);
    return;
  }

  status = flashManager_.status();
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Saved firmware selected";
  doc["chip"] = status.targetChip;
  doc["address"] = status.targetAddress;
  doc["size"] = status.totalBytes;
  doc["crc32"] = status.firmwareCrc32;
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleDeleteSavedPackage() {
  FlashStatus status = flashManager_.status();
  if (FlashManager::isBusyState(status.state)) {
    sendError(409, "Flash job is busy");
    return;
  }

  JsonDocument request;
  if (!server_->hasArg("plain") || deserializeJson(request, server_->arg("plain")) != DeserializationError::Ok) {
    sendError(400, "Saved package id is required");
    return;
  }
  String id = request["id"] | "";

  String error;
  const bool wasSelected = packageStore_.selectedSavedPackageId(error) == id;
  if (!error.isEmpty()) {
    sendError(400, error);
    return;
  }
  if (!packageStore_.removeSavedPackage(id, error)) {
    sendError(400, error);
    return;
  }
  if (wasSelected) {
    flashManager_.clearPackageState();
  }

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Saved firmware deleted";
  doc["storageFreeBytes"] = packageStore_.freeBytes();
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleFlashStart() {
  String error;
  String savedPackageId;
  if (server_->hasArg("plain")) {
    JsonDocument request;
    if (deserializeJson(request, server_->arg("plain")) == DeserializationError::Ok) {
      savedPackageId = request["savedPackageId"] | "";
    }
  }

  if (!savedPackageId.isEmpty()) {
    FlashStatus status = flashManager_.status();
    if (FlashManager::isBusyState(status.state)) {
      sendError(409, "Flash job is busy");
      return;
    }
    if (!packageStore_.restoreSavedPackage(savedPackageId, error)) {
      sendError(400, error);
      return;
    }
    if (!flashManager_.setPackageReady(error)) {
      sendError(400, error);
      return;
    }
  }

  if (!flashManager_.startFlash(error)) {
    sendError(400, error);
    return;
  }

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = savedPackageId.isEmpty() ? "Flash job started" : "Saved firmware selected and flash job started";
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleFlashCancel() {
  flashManager_.cancel();
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Cancel requested";
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleDeletePackage() {
  String error;
  if (!packageStore_.removePackage(error)) {
    sendError(400, error);
    return;
  }
  flashManager_.clearPackageState();

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Package deleted";
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::sendJson(int statusCode, const String &payload) {
  server_->send(statusCode, "application/json", payload);
}

void AppWebServer::sendError(int statusCode, const String &message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  String payload;
  serializeJson(doc, payload);
  sendJson(statusCode, payload);
}
