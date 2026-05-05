#include "web_server.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include "app_config.h"
#include "ap_manager.h"
#include "flash_manager.h"
#include "package_store.h"
#include "hal/target_control.h"
#include "flash/stm32_swd_debug.h"
#include "flash/stm32_chip_info.h"
#include "buzzer_manager.h"

namespace {
constexpr int kBatteryAdcPin = 2;
constexpr float kBatteryMinVoltage = 3.30f;
constexpr float kBatteryMaxVoltage = 4.20f;
constexpr float kBatteryDividerScale = 6.6f;

struct Esp32OtaStatus {
  bool active = false;
  bool ok = false;
  bool error = false;
  size_t written = 0;
  size_t total = 0;
  String phase = "idle";
  String message = "等待上传。";
};

Esp32OtaStatus esp32OtaStatus;

void resetEsp32OtaStatus(const String &phase, const String &message, size_t total = 0) {
  esp32OtaStatus.active = true;
  esp32OtaStatus.ok = false;
  esp32OtaStatus.error = false;
  esp32OtaStatus.written = 0;
  esp32OtaStatus.total = total;
  esp32OtaStatus.phase = phase;
  esp32OtaStatus.message = message;
}

float readBatteryVoltage() {
  return (static_cast<float>(analogRead(kBatteryAdcPin)) / 4095.0f) * kBatteryDividerScale;
}

int batteryPercent(float voltage) {
  const float clamped = constrain(voltage, kBatteryMinVoltage, kBatteryMaxVoltage);
  return static_cast<int>(((clamped - kBatteryMinVoltage) / (kBatteryMaxVoltage - kBatteryMinVoltage)) * 100.0f + 0.5f);
}

const char *partitionTypeName(esp_partition_type_t type) {
  switch (type) {
    case ESP_PARTITION_TYPE_APP:
      return "app";
    case ESP_PARTITION_TYPE_DATA:
      return "data";
    default:
      return "unknown";
  }
}

void addPartitionInfo(JsonArray partitions) {
  esp_partition_iterator_t iterator = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (iterator) {
    const esp_partition_t *partition = esp_partition_get(iterator);
    JsonObject item = partitions.add<JsonObject>();
    item["label"] = partition->label;
    item["type"] = partitionTypeName(partition->type);
    item["subtype"] = partition->subtype;
    item["address"] = partition->address;
    item["size"] = partition->size;
    iterator = esp_partition_next(iterator);
  }
  esp_partition_iterator_release(iterator);
}

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
      :root { color-scheme: light; --text: #0f172a; --muted: #64748b; --card: rgba(255,255,255,0.68); --border: rgba(148,163,184,0.26); --primary: #2563eb; --primary-dark: #1d4ed8; --danger: #dc2626; --success: #16a34a; --shadow: 0 18px 45px rgba(15,23,42,0.12); }
      * { box-sizing: border-box; }
      body { font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; position: relative; margin: 0; min-height: 100vh; overflow-x: hidden; background: linear-gradient(135deg, #eef6ff 0%, #e7efff 44%, #f3e8ff 100%); color: var(--text); animation: pageIn 520ms ease-out; }
      .particle-bg { position: fixed; inset: 0; z-index: 0; overflow: hidden; pointer-events: none; background: radial-gradient(circle at 18% 18%, rgba(0,194,255,0.28), transparent 19rem), radial-gradient(circle at 82% 24%, rgba(255,0,153,0.24), transparent 20rem), radial-gradient(circle at 45% 82%, rgba(255,184,0,0.22), transparent 21rem), radial-gradient(circle at 68% 64%, rgba(102,0,255,0.18), transparent 18rem); }
      .particle-bg span { position: absolute; top: 50%; left: 50%; width: 3em; height: 3em; color: transparent; font-size: 104px; line-height: 1; opacity: 1; mix-blend-mode: normal; animation: particleDriftA 44s -27s infinite ease-in-out alternate; }
      .particle-bg span:nth-child(1) { text-shadow: -1.2em -0.7em 4px rgba(0,102,255,.95), .9em -1.1em 4px rgba(0,209,255,.95), 1.3em .2em 4px rgba(128,0,255,.92), -.8em 1.1em 4px rgba(0,196,86,.9), .2em 1.4em 4px rgba(255,0,153,.92), -1.5em .4em 4px rgba(255,112,0,.9), 1.7em 1.1em 4px rgba(255,205,0,.9), -.2em -1.6em 4px rgba(0,174,255,.95), .5em -.3em 4px rgba(201,0,255,.9), -1.8em -1.2em 4px rgba(0,184,148,.92), 1.9em -.8em 4px rgba(255,0,76,.9), -.9em -.1em 4px rgba(40,90,255,.95); }
      .particle-bg span:nth-child(4) { text-shadow: -.9em -.4em 4px rgba(0,102,255,.88), .7em -.6em 4px rgba(0,209,255,.88), .95em .25em 4px rgba(128,0,255,.82), -.55em .7em 4px rgba(0,196,86,.82), .18em .9em 4px rgba(255,0,153,.84), -1.05em .28em 4px rgba(255,112,0,.82), 1.1em .75em 4px rgba(255,205,0,.82), -.1em -1em 4px rgba(0,174,255,.88); animation-name: particleDriftB; animation-duration: 28s; animation-delay: -7s; }
      .particle-bg span:nth-child(5) { text-shadow: -.65em .58em 4px rgba(0,140,255,.84), .26em -.92em 4px rgba(255,0,204,.82), .95em -.08em 4px rgba(0,214,120,.78), -.7em -.82em 4px rgba(255,178,0,.82), .5em .86em 4px rgba(64,80,255,.84), -1.1em .05em 4px rgba(255,70,0,.78), .82em 1.03em 4px rgba(0,224,255,.82), -.16em 1.1em 4px rgba(153,0,255,.78); animation-name: particleDriftC; animation-duration: 31s; animation-delay: -14s; }
      main { position: relative; z-index: 1; max-width: 860px; margin: 0 auto; padding: 32px 22px 40px; }
      .card { position: relative; z-index: 1; background: var(--card); border: 1px solid var(--border); border-radius: 22px; padding: 20px; margin-bottom: 18px; box-shadow: var(--shadow); backdrop-filter: blur(14px); animation: cardIn 520ms ease-out both; transition: transform 180ms ease, box-shadow 180ms ease, border-color 180ms ease; }
      .card.dropdown-open { z-index: 60; }
      .card:hover { transform: translateY(-2px); box-shadow: 0 24px 60px rgba(15,23,42,0.15); border-color: rgba(37,99,235,0.25); }
      h1 { margin: 0 0 8px; font-size: clamp(28px, 5vw, 42px); letter-spacing: -0.04em; }
      h2 { margin-top: 0; letter-spacing: -0.02em; }
      h3 { margin-bottom: 8px; }
      label { display: inline-block; margin-top: 6px; font-weight: 650; color: #334155; }
      button { background: linear-gradient(135deg, var(--primary), #38bdf8); color: white; border: 0; padding: 11px 16px; border-radius: 12px; cursor: pointer; font-weight: 700; box-shadow: 0 10px 22px rgba(37,99,235,0.25); transition: transform 160ms ease, box-shadow 160ms ease, filter 160ms ease; }
      button:hover { transform: translateY(-1px); box-shadow: 0 14px 28px rgba(37,99,235,0.30); filter: brightness(1.03); }
      button:active { transform: translateY(1px) scale(0.99); }
      button.secondary { background: #ffffff; color: var(--primary-dark); border: 1px solid rgba(37,99,235,0.22); box-shadow: 0 8px 18px rgba(15,23,42,0.08); }
      button.danger { background: linear-gradient(135deg, var(--danger), #fb7185); box-shadow: 0 10px 22px rgba(220,38,38,0.22); }
      button:disabled { cursor: not-allowed; opacity: 0.72; transform: none; filter: none; }
      button.loading { display: inline-flex; align-items: center; justify-content: center; gap: 8px; }
      button.loading::before { content: ""; width: 14px; height: 14px; border: 2px solid rgba(37,99,235,0.25); border-top-color: var(--primary-dark); border-radius: 50%; animation: spin 780ms linear infinite; }
      input, select { max-width: 100%; width: 100%; margin: 8px 0 12px; padding: 12px 13px; border-radius: 13px; border: 1px solid #cbd5e1; background: rgba(255,255,255,0.92); color: var(--text); outline: none; transition: border-color 160ms ease, box-shadow 160ms ease, transform 160ms ease; }
      input:focus, select:focus { border-color: #60a5fa; box-shadow: 0 0 0 4px rgba(96,165,250,0.22); transform: translateY(-1px); }
      input[type="checkbox"] { width: auto; transform: none; }
      input[type="range"] { padding: 0; }
      select.native-hidden { display: none; }
      .tabs { position: sticky; top: 0; z-index: 50; display: flex; gap: 8px; margin: 18px 0; padding: 8px; border: 1px solid var(--border); border-radius: 18px; background: rgba(255,255,255,0.72); backdrop-filter: blur(14px); box-shadow: 0 12px 30px rgba(15,23,42,0.10); overflow-x: auto; -webkit-overflow-scrolling: touch; scrollbar-width: none; }
      .tabs::-webkit-scrollbar { display: none; }
      .tab-button { flex: 0 0 auto; min-width: 92px; color: var(--primary-dark); background: transparent; border: 1px solid transparent; box-shadow: none; padding: 10px 14px; -webkit-tap-highlight-color: transparent; user-select: none; }
      .tab-button:hover, .tab-button:active, .tab-button:focus { transform: none; box-shadow: none; filter: none; outline: none; }
      .tab-button:focus-visible { border-color: rgba(37,99,235,0.32); box-shadow: 0 0 0 3px rgba(96,165,250,0.20); }
      .tab-button.active { color: white; background: linear-gradient(135deg, var(--primary), #38bdf8); box-shadow: 0 10px 22px rgba(37,99,235,0.25); }
      .tab-button.active:hover, .tab-button.active:active, .tab-button.active:focus { box-shadow: 0 10px 22px rgba(37,99,235,0.25); }
      .fragment { display: none; }
      .fragment.active { display: block; animation: cardIn 240ms ease-out; }
      .setting-line { display: flex; gap: 12px; align-items: center; justify-content: space-between; margin: 10px 0; flex-wrap: wrap; }
      .setting-line > span { flex: 1 1 220px; }
      .metric-block { white-space: pre-wrap; line-height: 1.65; font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
      .inline-select { position: relative; margin: 8px 0 12px; }
      .inline-select-toggle { width: 100%; text-align: left; color: var(--text); background: rgba(255,255,255,0.84); border: 1px solid #cbd5e1; box-shadow: 0 8px 18px rgba(15,23,42,0.08); padding: 12px 42px 12px 13px; border-radius: 13px; font-weight: 650; }
      .inline-select-toggle::after { content: "⌄"; position: absolute; right: 15px; color: var(--primary-dark); font-size: 18px; }
      .inline-select.open .inline-select-toggle { border-color: #60a5fa; box-shadow: 0 0 0 4px rgba(96,165,250,0.18), 0 14px 30px rgba(15,23,42,0.12); }
      .inline-select.open .inline-select-toggle::after { content: "⌃"; }
      .inline-select-menu { display: none; position: absolute; z-index: 20; left: 0; right: 0; top: calc(100% + 6px); max-height: 260px; overflow-y: auto; padding: 6px; border-radius: 14px; border: 1px solid rgba(148,163,184,0.35); background: rgba(255,255,255,0.96); box-shadow: 0 18px 40px rgba(15,23,42,0.18); backdrop-filter: blur(12px); }
      .inline-select.open .inline-select-menu { display: block; animation: dropdownIn 150ms ease-out; }
      .inline-select-option { width: 100%; text-align: left; color: var(--text); background: transparent; border: 0; box-shadow: none; padding: 10px 11px; border-radius: 10px; font-weight: 600; line-height: 1.35; }
      .inline-select-option:hover, .inline-select-option.selected { background: rgba(37,99,235,0.10); color: var(--primary-dark); transform: none; box-shadow: none; }
      .inline-select-option:disabled { color: #94a3b8; cursor: default; }
      .row { display: flex; gap: 12px; flex-wrap: wrap; align-items: center; }
      .row > * { flex: 1 1 200px; }
      progress { width: 100%; height: 18px; overflow: hidden; border: 0; border-radius: 999px; background: #e2e8f0; }
      progress::-webkit-progress-bar { background: #e2e8f0; border-radius: 999px; }
      progress::-webkit-progress-value { background: linear-gradient(90deg, var(--primary), #22c55e); border-radius: 999px; transition: width 60ms linear; }
      progress::-moz-progress-bar { background: linear-gradient(90deg, var(--primary), #22c55e); border-radius: 999px; }
      pre { white-space: pre-wrap; word-break: break-word; background: rgba(248,250,252,0.95); color: #1e293b; padding: 14px; border: 1px solid #e2e8f0; border-radius: 14px; box-shadow: inset 0 1px 0 rgba(255,255,255,0.75); max-height: 260px; overflow: auto; }
      .log-window { position: relative; margin: 1em 0; }
      .log-window pre { margin: 0; padding-top: 34px; }
      .copy-log-button { position: absolute; top: 7px; right: 7px; width: auto; min-width: 0; display: inline-flex; align-items: center; justify-content: center; background: rgba(255,255,255,0.92); color: var(--primary-dark); border: 1px solid rgba(37,99,235,0.18); padding: 3px 7px; font-size: 11px; line-height: 1.2; border-radius: 8px; box-shadow: 0 5px 12px rgba(15,23,42,0.08); }
      .copy-log-button.copied { background: linear-gradient(135deg, var(--success), #22c55e); color: white; animation: copiedPulse 480ms ease; }
      .muted { color: var(--muted); }
      footer { margin-top: 24px; text-align: center; color: var(--muted); font-size: 14px; }
      footer a { color: var(--primary-dark); font-weight: 700; text-decoration: none; }
      footer a:hover { text-decoration: underline; }
      @keyframes pageIn { from { opacity: 0; } to { opacity: 1; } }
      .particle-bg span:nth-child(2) { text-shadow: -1.7em .9em 4px rgba(0,140,255,.92), .4em -1.5em 4px rgba(255,0,204,.9), 1.8em -.2em 4px rgba(0,214,120,.88), -1.1em -1.6em 4px rgba(255,178,0,.9), .8em 1.3em 4px rgba(64,80,255,.92), -2em .1em 4px rgba(255,70,0,.88), 1.4em 1.6em 4px rgba(0,224,255,.9), -.3em 1.9em 4px rgba(153,0,255,.86), .1em -.7em 4px rgba(102,210,0,.88), 2em .7em 4px rgba(255,0,126,.88), -1.6em -0.4em 4px rgba(0,92,255,.92), .7em .2em 4px rgba(255,132,0,.88); animation-name: particleDriftB; animation-duration: 41s; animation-delay: -19s; }
      .particle-bg span:nth-child(3) { text-shadow: -1.9em -1.7em 5px rgba(255,0,0,.75), -0.2em -2em 5px rgba(0,255,204,.82), 1.8em -1.4em 5px rgba(255,0,255,.78), 2.1em .3em 5px rgba(0,94,255,.82), 1em 2em 5px rgba(255,214,0,.78), -1.4em 1.7em 5px rgba(0,220,80,.78), -2.2em .6em 5px rgba(255,84,0,.76), .1em .2em 5px rgba(128,0,255,.8); animation-name: particleDriftC; animation-duration: 37s; animation-delay: -11s; }
      @keyframes particleDriftA { 0% { transform: translate3d(-42vw, -28vh, 0) scale(12); } 25% { transform: translate3d(26vw, -37vh, 0) scale(14); } 52% { transform: translate3d(38vw, 18vh, 0) scale(13); } 78% { transform: translate3d(-18vw, 34vh, 0) scale(15); } 100% { transform: translate3d(-48vw, 6vh, 0) scale(12); } }
      @keyframes particleDriftB { 0% { transform: translate3d(36vw, -34vh, 0) scale(13); } 30% { transform: translate3d(-32vw, -18vh, 0) scale(15); } 58% { transform: translate3d(-8vw, 38vh, 0) scale(12); } 82% { transform: translate3d(44vw, 22vh, 0) scale(14); } 100% { transform: translate3d(18vw, -42vh, 0) scale(13); } }
      @keyframes particleDriftC { 0% { transform: translate3d(-12vw, 42vh, 0) scale(12); } 22% { transform: translate3d(44vw, 4vh, 0) scale(14); } 50% { transform: translate3d(12vw, -40vh, 0) scale(13); } 74% { transform: translate3d(-46vw, -6vh, 0) scale(15); } 100% { transform: translate3d(-22vw, 30vh, 0) scale(12); } }
      @keyframes cardIn { from { opacity: 0; transform: translateY(16px) scale(0.98); } to { opacity: 1; transform: translateY(0) scale(1); } }
      @keyframes dropdownIn { from { opacity: 0; transform: translateY(-6px) scale(0.98); } to { opacity: 1; transform: translateY(0) scale(1); } }
      @keyframes copiedPulse { 0% { transform: scale(1); } 50% { transform: scale(1.08); } 100% { transform: scale(1); } }
      @keyframes spin { to { transform: rotate(360deg); } }
      @media (max-width: 640px) { main { padding: 22px 14px 32px; } .particle-bg { background: radial-gradient(circle at 18% 16%, rgba(0,194,255,0.36), transparent 10rem), radial-gradient(circle at 82% 22%, rgba(255,0,153,0.32), transparent 11rem), radial-gradient(circle at 42% 62%, rgba(255,184,0,0.26), transparent 10rem), radial-gradient(circle at 70% 78%, rgba(102,0,255,0.26), transparent 11rem); } .particle-bg span { font-size: 48px; filter: blur(0.4px); animation-duration: 22s; animation-delay: -6s; } .particle-bg span:nth-child(2) { animation-duration: 24s; animation-delay: -12s; } .particle-bg span:nth-child(3) { animation-duration: 20s; animation-delay: -17s; } .particle-bg span:nth-child(4), .particle-bg span:nth-child(5) { display: block; } .tabs { margin-left: -2px; margin-right: -2px; padding: 7px; border-radius: 16px; } .tab-button { min-width: 78px; padding: 10px 12px; } .card { border-radius: 18px; padding: 16px; } button:not(.copy-log-button):not(.tab-button) { width: 100%; } }
      @media (prefers-reduced-motion: reduce) { *, *::before, *::after { animation-duration: 1ms !important; transition-duration: 1ms !important; scroll-behavior: auto !important; } }
    </style>
  </head>
  <body>
    <div class="particle-bg" aria-hidden="true"><span>.</span><span>.</span><span>.</span><span>.</span><span>.</span></div>
    <main>
      <h1>Exlink STM32 无线烧录器</h1>
      <nav class="tabs" aria-label="功能分类">
        <button class="tab-button active" data-fragment-tab="device" type="button" onclick="showFragment('device')">设备</button>
        <button class="tab-button" data-fragment-tab="network" type="button" onclick="showFragment('network')">网络</button>
        <button class="tab-button" data-fragment-tab="firmware" type="button" onclick="showFragment('firmware')">固件</button>
        <button class="tab-button" data-fragment-tab="flash" type="button" onclick="showFragment('flash')">烧录</button>
        <button class="tab-button" data-fragment-tab="settings" type="button" onclick="showFragment('settings')">设置</button>
      </nav>
      <section class="fragment active" data-fragment="device">
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
      </section>
      <section class="fragment" data-fragment="network">
        <div class="card">
          <h2>联网设置</h2>
          <p class="muted">热点会一直保留；配置路由器 WiFi 后，设备会同时连接到局域网。</p>
          <div id="wifiInfo" class="muted">正在加载 WiFi 状态...</div>
          <div class="row">
            <button id="scanWifiButton" class="secondary">扫描 WiFi</button>
          </div>
          <label>扫描到的 WiFi</label>
          <select id="wifiNetworkSelect"></select>
          <label>WiFi 名称</label>
          <input id="wifiSsidInput" type="text" placeholder="输入或选择路由器 WiFi 名称" autocomplete="off" />
          <label>WiFi 密码</label>
          <input id="wifiPasswordInput" type="password" placeholder="输入 WiFi 密码" autocomplete="off" />
          <div class="row">
            <button id="saveWifiButton" class="secondary">保存并连接 WiFi</button>
            <button id="forgetWifiButton" class="danger">清除 WiFi 配置</button>
          </div>
          <pre id="wifiLog">等待操作。</pre>
        </div>
      </section>
      <section class="fragment" data-fragment="firmware">
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
      </section>
      <section class="fragment" data-fragment="flash">
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
      </section>
      <section class="fragment" data-fragment="settings">
        <div class="card">
          <h2>设备状态</h2>
          <div id="settingsDeviceInfo" class="muted metric-block">正在加载设备状态...</div>
        </div>
        <div class="card">
          <h2>ESP32 固件升级 · 当前版本 0.1.13</h2>
          <p class="muted">上传 PlatformIO 生成的 firmware.bin，只更新设备本身固件，不会擦除 LittleFS 中保存的 STM32 固件包。</p>
          <label>ESP32 固件 .bin</label>
          <input id="esp32OtaFile" type="file" accept=".bin" />
          <div class="row">
            <button id="esp32OtaButton" class="secondary">上传并升级 ESP32</button>
          </div>
          <pre id="esp32OtaLog">等待上传。</pre>
        </div>
        <div class="card">
          <h2>提示音设置</h2>
          <div class="setting-line">
            <span>
              <strong>启用按键和触摸提示音</strong><br />
              <span class="muted">关闭后硬件按键和屏幕触摸提示都不会发声。</span>
            </span>
            <label><input id="buzzerEnabledInput" type="checkbox" /> 启用</label>
          </div>
          <label>蜂鸣器音量：<span id="buzzerVolumeValue">-</span>%</label>
          <input id="buzzerVolumeInput" type="range" min="0" max="100" step="1" value="40" />
          <pre id="buzzerLog">正在加载提示音设置...</pre>
        </div>
      </section>
      <footer>作者：YooLiny · <a href="https://github.com/mgcyoulinjx/wireless-flasher-stm32" target="_blank" rel="noopener noreferrer">GitHub 项目</a></footer>
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
const wifiInfo = document.getElementById('wifiInfo');
const wifiNetworkSelect = document.getElementById('wifiNetworkSelect');
const wifiSsidInput = document.getElementById('wifiSsidInput');
const wifiPasswordInput = document.getElementById('wifiPasswordInput');
const wifiLog = document.getElementById('wifiLog');
const saveWifiButton = document.getElementById('saveWifiButton');
const forgetWifiButton = document.getElementById('forgetWifiButton');
const scanWifiButton = document.getElementById('scanWifiButton');
const storageInfo = document.getElementById('storageInfo');
const firmwareNameInput = document.getElementById('firmwareNameInput');
const savedPackageSelect = document.getElementById('savedPackageSelect');
const savedPackageLog = document.getElementById('savedPackageLog');
const savePackageButton = document.getElementById('savePackageButton');
const deleteSavedPackageButton = document.getElementById('deleteSavedPackageButton');
const buzzerEnabledInput = document.getElementById('buzzerEnabledInput');
const buzzerVolumeInput = document.getElementById('buzzerVolumeInput');
const buzzerVolumeValue = document.getElementById('buzzerVolumeValue');
const buzzerLog = document.getElementById('buzzerLog');
const settingsDeviceInfo = document.getElementById('settingsDeviceInfo');
const esp32OtaFile = document.getElementById('esp32OtaFile');
const esp32OtaButton = document.getElementById('esp32OtaButton');
const esp32OtaLog = document.getElementById('esp32OtaLog');

let latestPackageReady = false;
let flashBusy = false;
let wifiScanInProgress = false;
let wifiConnectPending = false;
let lastPackageVersion = -1;
let lastEsp32OtaUploadPercent = -1;
let lastEsp32OtaDevicePercent = -1;
let lastEsp32OtaPhase = '';
let esp32OtaUploadCompleteLogged = false;
const customSelects = [];
const fragmentTabs = Array.from(document.querySelectorAll('[data-fragment-tab]'));
const fragments = Array.from(document.querySelectorAll('[data-fragment]'));

function showFragment(name) {
  for (const tab of fragmentTabs) {
    tab.classList.toggle('active', tab.dataset.fragmentTab === name);
  }
  for (const fragment of fragments) {
    fragment.classList.toggle('active', fragment.dataset.fragment === name);
  }
  closeCustomSelects();
  try {
    localStorage.setItem('activeFragment', name);
  } catch (error) {
  }
}

window.showFragment = showFragment;

function closeCustomSelects(except = null) {
  for (const item of customSelects) {
    if (item.wrapper !== except) {
      item.wrapper.classList.remove('open');
      item.wrapper.closest('.card')?.classList.remove('dropdown-open');
      item.button.setAttribute('aria-expanded', 'false');
    }
  }
}

function createInlineSelect(select) {
  select.classList.add('native-hidden');
  const wrapper = document.createElement('div');
  wrapper.className = 'inline-select';
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'inline-select-toggle';
  button.setAttribute('aria-haspopup', 'listbox');
  button.setAttribute('aria-expanded', 'false');
  const menu = document.createElement('div');
  menu.className = 'inline-select-menu';
  menu.setAttribute('role', 'listbox');
  select.parentNode.insertBefore(wrapper, select.nextSibling);
  wrapper.appendChild(button);
  wrapper.appendChild(menu);

  button.addEventListener('click', () => {
    const willOpen = !wrapper.classList.contains('open');
    closeCustomSelects(wrapper);
    wrapper.classList.toggle('open', willOpen);
    wrapper.closest('.card')?.classList.toggle('dropdown-open', willOpen);
    button.setAttribute('aria-expanded', willOpen ? 'true' : 'false');
  });

  const item = { select, wrapper, button, menu };
  customSelects.push(item);
  return item;
}

const savedPackageDropdown = createInlineSelect(savedPackageSelect);
const flashPackageDropdown = createInlineSelect(flashPackageSelect);
const wifiNetworkDropdown = createInlineSelect(wifiNetworkSelect);

function resetWifiNetworkSelect(message = '请先扫描 WiFi') {
  wifiNetworkSelect.innerHTML = '';
  const option = document.createElement('option');
  option.value = '';
  option.textContent = message;
  wifiNetworkSelect.appendChild(option);
  syncInlineSelect(wifiNetworkDropdown);
}

function setWifiScanButtonLoading(loading) {
  wifiScanInProgress = loading;
  scanWifiButton.disabled = loading;
  scanWifiButton.classList.toggle('loading', loading);
  scanWifiButton.textContent = loading ? '正在扫描.........' : '扫描 WiFi';
}

document.addEventListener('click', (event) => {
  if (!event.target.closest('.inline-select')) {
    closeCustomSelects();
  }
});

function syncInlineSelect(item) {
  const { select, button, menu, wrapper } = item;
  menu.innerHTML = '';
  const selectedOption = select.options[select.selectedIndex];
  button.textContent = selectedOption ? selectedOption.textContent.replace(/^已选择：/, '') : '请选择';
  button.disabled = select.disabled;
  wrapper.classList.toggle('disabled', select.disabled);
  for (const option of Array.from(select.options)) {
    const optionButton = document.createElement('button');
    optionButton.type = 'button';
    optionButton.className = 'inline-select-option';
    optionButton.textContent = option.textContent;
    optionButton.disabled = option.disabled || option.value === '';
    optionButton.setAttribute('role', 'option');
    optionButton.setAttribute('aria-selected', option.selected ? 'true' : 'false');
    optionButton.classList.toggle('selected', option.selected);
    optionButton.addEventListener('click', () => {
      if (optionButton.disabled) {
        return;
      }
      select.value = option.value;
      select.dispatchEvent(new Event('change'));
      closeCustomSelects();
      syncInlineSelect(item);
    });
    menu.appendChild(optionButton);
  }
}

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

function setWifiLog(message) {
  const timestamp = new Date().toLocaleTimeString();
  wifiLog.textContent = `[${timestamp}] ${message}`;
}

function setBuzzerLog(message) {
  const timestamp = new Date().toLocaleTimeString();
  buzzerLog.textContent = `[${timestamp}] ${message}`;
}

function setEsp32OtaLog(message) {
  const timestamp = new Date().toLocaleTimeString();
  esp32OtaLog.textContent = esp32OtaLog.textContent && esp32OtaLog.textContent !== '等待上传。'
    ? `${esp32OtaLog.textContent}\n[${timestamp}] ${message}`
    : `[${timestamp}] ${message}`;
  esp32OtaLog.scrollTop = esp32OtaLog.scrollHeight;
}

function resetEsp32OtaLogState() {
  lastEsp32OtaUploadPercent = -1;
  lastEsp32OtaDevicePercent = -1;
  lastEsp32OtaPhase = '';
  esp32OtaUploadCompleteLogged = false;
}

function percentOf(done, total) {
  const totalBytes = Number(total || 0);
  if (totalBytes <= 0) return -1;
  return Math.min(100, Math.floor((Number(done || 0) * 100) / totalBytes));
}

function phaseLabel(phase) {
  if (phase === 'starting') return '检查文件';
  if (phase === 'writing') return '刷写 OTA 分区';
  if (phase === 'finalizing') return '校验收尾';
  if (phase === 'done') return '写入完成';
  if (phase === 'rebooting') return '准备重启';
  if (phase === 'aborted') return '已中止';
  return phase || '等待';
}

function logEsp32OtaStatus(status, force = false) {
  const phase = status.phase || '';
  const written = Number(status.written || 0);
  const total = Number(status.total || 0);
  const percent = percentOf(written, total);
  if (force || phase !== lastEsp32OtaPhase) {
    setEsp32OtaLog(`设备阶段：${phaseLabel(phase)}，${status.message || '-'}`);
    lastEsp32OtaPhase = phase;
  }
  if (percent >= 0 && (force || percent === 100 || percent - lastEsp32OtaDevicePercent >= 5)) {
    setEsp32OtaLog(`设备刷写：${formatBytes(written)} / ${formatBytes(total)} (${percent}%)`);
    lastEsp32OtaDevicePercent = percent;
  }
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

function formatUsage(used, total) {
  const totalBytes = Number(total || 0);
  const usedBytes = Number(used || 0);
  const percent = totalBytes > 0 ? Math.round((usedBytes * 100) / totalBytes) : 0;
  return `${formatBytes(usedBytes)} / ${formatBytes(totalBytes)} (${percent}%)`;
}

function updateSettingsDeviceInfo(info) {
  const storageTotal = Number(info.storageTotalBytes || 0);
  const storageUsed = Number(info.storageUsedBytes || 0);
  const flashTotal = Number(info.flashChipSize || 0);
  const partitions = (info.partitions || [])
    .map(partition => {
      const address = `0x${Number(partition.address || 0).toString(16).toUpperCase()}`;
      return `  ${partition.label || '-'} (${partition.type || '-'}/${partition.subtype}): ${formatBytes(partition.size)} @ ${address}`;
    })
    .join('\n');
  const psramText = Number(info.psramTotalBytes || 0) > 0
    ? `PSRAM: ${formatUsage(Number(info.psramTotalBytes || 0) - Number(info.psramFreeBytes || 0), info.psramTotalBytes)}，剩余 ${formatBytes(info.psramFreeBytes)}`
    : 'PSRAM: 未检测到';
  settingsDeviceInfo.textContent = [
    `剩余电量: ${Number(info.batteryPercent || 0)}% (${Number(info.batteryVoltage || 0).toFixed(2)}V)`,
    `整片 Flash: ${formatBytes(flashTotal)}`,
    `固件分区: 当前固件 ${formatBytes(info.sketchSize)}，可用 OTA 空间 ${formatBytes(info.freeSketchSpace)}`,
    `LittleFS: 已用 ${formatUsage(storageUsed, storageTotal)}，剩余 ${formatBytes(info.storageFreeBytes)}`,
    `RAM: ${formatUsage(Number(info.heapTotalBytes || 0) - Number(info.heapFreeBytes || 0), info.heapTotalBytes)}，剩余 ${formatBytes(info.heapFreeBytes)}，历史最低 ${formatBytes(info.heapMinFreeBytes)}，最大连续 ${formatBytes(info.heapMaxAllocBytes)}`,
    psramText,
    `分区列表:\n${partitions || '  -'}`
  ].join('\n');
}

function updateWifiInfo(info) {
  const stationLine = info.stationConfigured
    ? `路由器 WiFi: ${info.stationSsid || '-'} | ${info.stationStatus || '-'}${info.stationIp ? ` | 地址: ${info.stationIp}` : ''}${info.stationRssi ? ` | 信号: ${info.stationRssi} dBm` : ''}`
    : `路由器 WiFi: 未配置`;
  wifiInfo.textContent = `热点: ${info.ssid} | 地址: ${info.ip} | ${stationLine}`;
  if (info.stationConfigured && !wifiSsidInput.value) {
    wifiSsidInput.value = info.stationSsid || '';
  }
  if (wifiConnectPending && info.stationConnected) {
    wifiConnectPending = false;
    setWifiLog(`WiFi 连接成功：${info.stationSsid || '-'}，局域网地址 ${info.stationIp || '-'}`);
  } else if (wifiConnectPending && info.stationConfigured && !info.stationConnecting && !info.stationConnected) {
    wifiConnectPending = false;
    setWifiLog(info.stationStatus || 'WiFi 连接失败，热点模式仍可用。');
  }
}

async function refreshInfo() {
  try {
    const info = await request('/api/info');
    latestPackageReady = Boolean(info.packageReady);
    const packageText = info.packageReady
      ? ` | 固件: ${info.targetChip || '-'} @ 0x${Number(info.targetAddress || 0).toString(16).toUpperCase()} | ${info.totalBytes} bytes | CRC32 0x${Number(info.firmwareCrc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`
      : ' | 暂无已校验固件包';
    const detectedText = info.detectedChip ? ` | 当前芯片: ${info.detectedChip}` : ' | 当前芯片: 未检测';
    deviceInfo.textContent = `热点: ${info.ssid} | 地址: ${info.ip}${info.stationIp ? ` | 局域网: ${info.stationIp}` : ''} | 接口: SWD${detectedText} | 当前状态: ${info.state}${packageText}`;
    updateWifiInfo(info);
    updateSettingsDeviceInfo(info);
    wiringTemplate.textContent = `GND  -> STM32 GND\nGPIO${info.swdIoPin} -> STM32 SWDIO\nGPIO${info.swdClockPin} -> STM32 SWCLK`;
    updateFlashControls(info);
  } catch (error) {
    deviceInfo.textContent = error.message;
  }
}

async function refreshPackageVersion() {
  try {
    const data = await request('/api/packages/version');
    if (Number(data.version || 0) !== lastPackageVersion) {
      await refreshPackages();
    }
  } catch (error) {
  }
}

async function refreshPackages() {
  try {
    const data = await request('/api/packages');
    lastPackageVersion = Number(data.version || 0);
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
    syncInlineSelect(savedPackageDropdown);
    syncInlineSelect(flashPackageDropdown);
    savePackageButton.disabled = false;
    deleteSavedPackageButton.disabled = packages.length === 0;
  } catch (error) {
    storageInfo.textContent = error.message;
  }
}

function setLogText(log, text) {
  if (log.textContent !== text) {
    log.textContent = text;
    log.scrollTop = log.scrollHeight;
  }
}

async function refreshBuzzerConfig() {
  try {
    const config = await request('/api/buzzer');
    buzzerEnabledInput.checked = Boolean(config.enabled);
    buzzerVolumeInput.value = Number(config.volume || 0);
    buzzerVolumeValue.textContent = buzzerVolumeInput.value;
    setBuzzerLog(`提示音${config.enabled ? '已启用' : '已关闭'}，音量 ${buzzerVolumeInput.value}%`);
  } catch (error) {
    setBuzzerLog(error.message);
  }
}

async function saveBuzzerConfig() {
  try {
    const config = await request('/api/buzzer', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled: buzzerEnabledInput.checked, volume: Number(buzzerVolumeInput.value) })
    });
    buzzerEnabledInput.checked = Boolean(config.enabled);
    buzzerVolumeInput.value = Number(config.volume || 0);
    buzzerVolumeValue.textContent = buzzerVolumeInput.value;
    setBuzzerLog('提示音设置已保存。');
  } catch (error) {
    setBuzzerLog(error.message);
  }
}

function scheduleBuzzerSave() {
  clearTimeout(scheduleBuzzerSave.timer);
  scheduleBuzzerSave.timer = setTimeout(saveBuzzerConfig, 180);
}

async function refreshStatus() {
  try {
    const status = await request('/api/status');
    flashBusy = Boolean(status.flashBusy);
    document.getElementById('flashButton').disabled = flashBusy;
    const progressMax = Math.max(Number(status.totalBytes || 0), 1);
    const progressValue = Math.min(Number(status.bytesWritten || 0), progressMax);
    const addressHex = `0x${Number(status.targetAddress || 0).toString(16).toUpperCase()}`;
    const crcHex = `0x${Number(status.firmwareCrc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`;
    const summaryText = `${status.state} | 接口: SWD | 芯片: ${status.targetChip || '-'} | 地址: ${addressHex} | CRC32: ${crcHex} | ${status.bytesWritten}/${status.totalBytes}`;
    const logText = status.log || (status.detectedChip ? `${status.message}\n${status.detectedChip}` : status.message);
    if (statusSummary.textContent !== summaryText) {
      statusSummary.textContent = summaryText;
    }
    if (progressBar.max !== progressMax) {
      progressBar.max = progressMax;
    }
    if (progressBar.value !== progressValue) {
      progressBar.value = progressValue;
    }
    if (statusLog.textContent !== logText) {
      setLogText(statusLog, logText);
    }
  } catch (error) {
    setLogText(statusLog, error.message);
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
    if (!hexFile.files[0]) {
      throw new Error('请选择 Intel HEX 文件');
    }
    setUploadLog('进入 Intel HEX 上传流程');
    await uploadSingle(hexFile.files[0], 'firmware.hex');
    setUploadLog('HEX 上传完成，开始请求 /api/upload/hex/finalize...');
    const result = await request('/api/upload/hex/finalize', { method: 'POST' });
    setUploadLog(`HEX 校验完成: ${result.message || 'ok'}`);
    if (result.size) {
      setUploadLog(`生成固件: ${result.size} bytes @ 0x${Number(result.address || 0).toString(16).toUpperCase()}, CRC32 0x${Number(result.crc32 || 0).toString(16).toUpperCase().padStart(8, '0')}`);
    }
    setUploadLog('刷新设备信息...');
    await refreshInfo();
    await refreshStatus();
    await refreshPackages();
    setUploadLog('上传并校验流程完成');
    setLogText(statusLog, '上传并校验完成，详细过程见上传校验日志。');
  } catch (error) {
    setUploadLog(`错误: ${error.message}`);
  }
});

document.getElementById('flashButton').addEventListener('click', async () => {
  if (flashBusy) {
    return;
  }
  try {
    flashBusy = true;
    document.getElementById('flashButton').disabled = true;
    const selectedFirmware = flashPackageSelect.value || 'active';
    const savedPackageId = selectedFirmware.startsWith('saved:') ? selectedFirmware.substring(6) : '';
    const response = await request('/api/flash/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ savedPackageId })
    });
    setLogText(statusLog, response.message);
    await refreshStatus();
  } catch (error) {
    setLogText(statusLog, error.message);
  }
});

document.getElementById('cancelButton').addEventListener('click', async () => {
  try {
    const response = await request('/api/flash/cancel', { method: 'POST' });
    setLogText(statusLog, response.message);
    await refreshStatus();
  } catch (error) {
    setLogText(statusLog, error.message);
  }
});

document.getElementById('deleteButton').addEventListener('click', async () => {
  try {
    const response = await request('/api/package', { method: 'DELETE' });
    setLogText(statusLog, response.message);
    progressBar.value = 0;
    await refreshInfo();
    await refreshStatus();
    await refreshPackages();
  } catch (error) {
    setLogText(statusLog, error.message);
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
  setLogText(statusLog, id ? '正在选择烧录固件...' : '正在选择当前固件...');
  try {
    await request('/api/packages/select', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id })
    });
    setLogText(statusLog, id ? '已选择保存固件，开始烧录时将加载。' : '已选择当前固件。');
    await refreshPackages();
  } catch (error) {
    setLogText(statusLog, error.message);
  }
}

flashPackageSelect.addEventListener('change', async () => {
  const selectedFirmware = flashPackageSelect.value || 'active';
  const id = selectedFirmware.startsWith('saved:') ? selectedFirmware.substring(6) : '';
  await switchFlashFirmware(id);
});

scanWifiButton.addEventListener('click', async () => {
  if (wifiScanInProgress) {
    return;
  }
  setWifiLog('正在扫描 WiFi...');
  setWifiScanButtonLoading(true);
  try {
    const result = await request('/api/wifi/scan');
    wifiNetworkSelect.innerHTML = '';
    const networks = result.networks || [];
    if (networks.length === 0) {
      resetWifiNetworkSelect('未扫描到 WiFi');
      setWifiLog('未扫描到 WiFi。');
      return;
    }
    const placeholder = document.createElement('option');
    placeholder.value = '';
    placeholder.textContent = '请选择扫描到的 WiFi';
    wifiNetworkSelect.appendChild(placeholder);
    for (const network of networks) {
      const option = document.createElement('option');
      option.value = network.ssid || '';
      option.textContent = `${network.ssid || '-'} | ${network.rssi || 0} dBm${network.secure ? ' | 加密' : ' | 开放'}`;
      wifiNetworkSelect.appendChild(option);
    }
    syncInlineSelect(wifiNetworkDropdown);
    setWifiLog(`扫描完成，发现 ${networks.length} 个 WiFi。`);
  } catch (error) {
    setWifiLog(error.message);
  } finally {
    setWifiScanButtonLoading(false);
  }
});

wifiNetworkSelect.addEventListener('change', () => {
  if (wifiNetworkSelect.value) {
    wifiSsidInput.value = wifiNetworkSelect.value;
  }
  syncInlineSelect(wifiNetworkDropdown);
});

saveWifiButton.addEventListener('click', async () => {
  setWifiLog('正在保存并连接 WiFi...');
  try {
    const result = await request('/api/wifi/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: wifiSsidInput.value, password: wifiPasswordInput.value })
    });
    wifiPasswordInput.value = '';
    wifiConnectPending = true;
    setWifiLog(result.message || 'WiFi 配置已保存，等待连接结果...');
    await refreshInfo();
  } catch (error) {
    setWifiLog(error.message);
  }
});

forgetWifiButton.addEventListener('click', async () => {
  setWifiLog('正在清除 WiFi 配置...');
  try {
    const result = await request('/api/wifi/forget', { method: 'POST' });
    wifiConnectPending = false;
    wifiSsidInput.value = '';
    wifiPasswordInput.value = '';
    setWifiLog(result.message || '已清除 WiFi 配置');
    await refreshInfo();
  } catch (error) {
    setWifiLog(error.message);
  }
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

esp32OtaButton.addEventListener('click', async () => {
  esp32OtaLog.textContent = '';
  resetEsp32OtaLogState();
  let polling = false;
  let pollPromise = Promise.resolve();
  try {
    const file = esp32OtaFile.files[0];
    if (!file) {
      throw new Error('请选择 ESP32 firmware.bin 文件');
    }
    if (!file.name.toLowerCase().endsWith('.bin')) {
      throw new Error('只支持 .bin 固件文件');
    }
    esp32OtaButton.disabled = true;
    setEsp32OtaLog(`准备升级：${file.name}，${formatBytes(file.size)}`);
    const formData = new FormData();
    formData.append('firmware', file, file.name);

    polling = true;
    const pollStatus = async () => {
      while (polling) {
        try {
          const status = await request('/api/esp32/ota/status');
          logEsp32OtaStatus(status);
          if (status.error) {
            polling = false;
          }
        } catch (error) {
        }
        await new Promise(resolve => setTimeout(resolve, 400));
      }
    };
    pollPromise = pollStatus();

    const result = await new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/api/esp32/ota');
      xhr.upload.onprogress = event => {
        if (!event.lengthComputable) {
          return;
        }
        const percent = percentOf(event.loaded, event.total);
        if (percent === 100 || percent - lastEsp32OtaUploadPercent >= 5) {
          setEsp32OtaLog(`浏览器上传：${formatBytes(event.loaded)} / ${formatBytes(event.total)} (${percent}%)`);
          lastEsp32OtaUploadPercent = percent;
        }
        if (percent === 100 && !esp32OtaUploadCompleteLogged) {
          setEsp32OtaLog('固件上传成功，设备正在自动升级。');
          setEsp32OtaLog('升级期间请勿突然断电，设备会自动重启。');
          esp32OtaUploadCompleteLogged = true;
        }
      };
      xhr.onload = () => {
        let data = {};
        try {
          data = xhr.responseText ? JSON.parse(xhr.responseText) : {};
        } catch (error) {
          reject(new Error(`升级响应不是 JSON: ${xhr.responseText}`));
          return;
        }
        if (xhr.status < 200 || xhr.status >= 300 || data.ok === false) {
          reject(new Error(data.error || 'ESP32 固件升级失败'));
          return;
        }
        resolve(data);
      };
      xhr.onerror = () => reject(new Error('ESP32 OTA 上传连接失败'));
      xhr.onabort = () => reject(new Error('ESP32 OTA 上传已中止'));
      xhr.send(formData);
    });

    polling = false;
    await pollPromise;
    try {
      const status = await request('/api/esp32/ota/status');
      logEsp32OtaStatus(status, true);
    } catch (error) {
    }
    setEsp32OtaLog(result.message || '升级成功，设备即将重启。');
  } catch (error) {
    polling = false;
    await pollPromise;
    setEsp32OtaLog(error.message);
    esp32OtaButton.disabled = false;
  }
});

buzzerEnabledInput.addEventListener('change', saveBuzzerConfig);
buzzerVolumeInput.addEventListener('input', () => {
  buzzerVolumeValue.textContent = buzzerVolumeInput.value;
  scheduleBuzzerSave();
});
for (const tab of fragmentTabs) {
  tab.addEventListener('click', () => showFragment(tab.dataset.fragmentTab));
}

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

setupLogCopyButtons();
let activeFragment = 'device';
try {
  activeFragment = localStorage.getItem('activeFragment') || 'device';
} catch (error) {
}
showFragment(activeFragment);
resetWifiNetworkSelect();
refreshInfo();
refreshStatus();
refreshPackages();
refreshBuzzerConfig();
updateFlashControls();
setInterval(refreshStatus, 300);
setInterval(refreshInfo, 5000);
setInterval(refreshPackageVersion, 500);
)JS";
}

AppWebServer::AppWebServer(AccessPointManager &apManager,
                           PackageStore &packageStore,
                           FlashManager &flashManager,
                           TargetControl &targetControl,
                           Stm32SwdDebug &swdDebug,
                           BuzzerManager &buzzerManager,
                           Preferences &preferences)
    : apManager_(apManager),
      packageStore_(packageStore),
      flashManager_(flashManager),
      targetControl_(targetControl),
      swdDebug_(swdDebug),
      buzzerManager_(buzzerManager),
      preferences_(preferences) {}

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

bool AppWebServer::hasActiveUpload() const {
  return uploadActive_;
}

void AppWebServer::configureRoutes() {
  server_->on("/", HTTP_GET, [this]() { handleIndex(); });
  server_->on("/app.js", HTTP_GET, [this]() {
    server_->sendHeader("Cache-Control", "no-store");
    server_->send_P(200, "application/javascript", kEmbeddedAppJs);
  });

  server_->on("/api/info", HTTP_GET, [this]() { handleInfo(); });
  server_->on("/api/wifi/config", HTTP_POST, [this]() { handleWifiConfig(); });
  server_->on("/api/wifi/forget", HTTP_POST, [this]() { handleWifiForget(); });
  server_->on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
  server_->on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_->on("/api/flash/start", HTTP_POST, [this]() { handleFlashStart(); });
  server_->on("/api/flash/cancel", HTTP_POST, [this]() { handleFlashCancel(); });
  server_->on("/api/buzzer", HTTP_GET, [this]() { handleBuzzerConfig(); });
  server_->on("/api/buzzer", HTTP_POST, [this]() { handleSaveBuzzerConfig(); });
  server_->on(
      "/api/esp32/ota", HTTP_POST,
      [this]() { handleEsp32OtaFinalize(); },
      [this]() { handleEsp32OtaUpload(); });
  server_->on("/api/esp32/ota/status", HTTP_GET, [this]() { handleEsp32OtaStatus(); });
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
          uploadActive_ = true;
        }

        if (upload.status == UPLOAD_FILE_WRITE) {
          const bool reset = upload.totalSize == 0;
          if (!packageStore_.appendIntelHexChunk(upload.buf, upload.currentSize, reset, error)) {
            sendError(400, error);
          }
        }

        if (upload.status == UPLOAD_FILE_END) {
          uploadActive_ = false;
          return;
        }

        if (upload.status == UPLOAD_FILE_ABORTED) {
          uploadActive_ = false;
          return;
        }
      });
  server_->on("/api/upload/hex/finalize", HTTP_POST, [this]() { handleHexUploadFinalize(); });
  server_->on("/api/packages", HTTP_GET, [this]() { handlePackages(); });
  server_->on("/api/packages/version", HTTP_GET, [this]() { handlePackagesVersion(); });
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
  doc["firmwareVersion"] = AppConfig::kFirmwareVersion;
  doc["passwordRequired"] = apManager_.hasPassword();
  doc["stationConfigured"] = apManager_.stationConfigured();
  doc["stationConnected"] = apManager_.stationConnected();
  doc["stationConnecting"] = apManager_.stationConnecting();
  doc["stationSsid"] = apManager_.stationSsid();
  doc["stationIp"] = apManager_.stationIpAddress();
  doc["stationStatus"] = apManager_.stationStatus();
  doc["stationRssi"] = apManager_.stationRssi();
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
  const float batteryVoltage = readBatteryVoltage();
  doc["batteryVoltage"] = serialized(String(batteryVoltage, 2));
  doc["batteryPercent"] = batteryPercent(batteryVoltage);
  doc["storageTotalBytes"] = packageStore_.totalBytes();
  doc["storageUsedBytes"] = packageStore_.usedBytes();
  doc["storageFreeBytes"] = packageStore_.freeBytes();
  doc["flashChipSize"] = ESP.getFlashChipSize();
  doc["sketchSize"] = ESP.getSketchSize();
  doc["freeSketchSpace"] = ESP.getFreeSketchSpace();
  doc["heapTotalBytes"] = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  doc["heapFreeBytes"] = ESP.getFreeHeap();
  doc["heapMinFreeBytes"] = ESP.getMinFreeHeap();
  doc["heapMaxAllocBytes"] = ESP.getMaxAllocHeap();
  doc["psramTotalBytes"] = ESP.getPsramSize();
  doc["psramFreeBytes"] = ESP.getFreePsram();
  addPartitionInfo(doc["partitions"].to<JsonArray>());
  doc["maxFirmwareSize"] = AppConfig::kMaxFirmwareSize;
  doc["maxHexUploadSize"] = AppConfig::kMaxHexUploadSize;
  doc["state"] = status.stateLabel;
  doc["message"] = status.message;
  doc["log"] = status.log;

  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleWifiConfig() {
  JsonDocument request;
  if (!server_->hasArg("plain") || deserializeJson(request, server_->arg("plain")) != DeserializationError::Ok) {
    sendError(400, "WiFi 配置请求无效");
    return;
  }

  String ssid = request["ssid"] | "";
  String password = request["password"] | "";
  String error;
  if (!apManager_.configureStation(ssid, password, error)) {
    sendError(400, error);
    return;
  }

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "WiFi 配置已保存，正在连接";
  doc["stationSsid"] = apManager_.stationSsid();
  doc["stationStatus"] = apManager_.stationStatus();
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleWifiForget() {
  String error;
  if (!apManager_.clearStationConfig(error)) {
    sendError(400, error);
    return;
  }

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "已清除 WiFi 配置，热点模式仍可用";
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleWifiScan() {
  JsonDocument doc;
  doc["ok"] = true;
  JsonArray networks = doc["networks"].to<JsonArray>();
  String error;
  if (!apManager_.scanNetworks(networks, error)) {
    sendError(500, error);
    return;
  }

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
  doc["flashBusy"] = FlashManager::isBusyState(status.state);

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

void AppWebServer::handleBuzzerConfig() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["enabled"] = buzzerManager_.enabled();
  doc["volume"] = buzzerManager_.volume();
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void AppWebServer::handleSaveBuzzerConfig() {
  JsonDocument request;
  if (!server_->hasArg("plain") || deserializeJson(request, server_->arg("plain")) != DeserializationError::Ok) {
    sendError(400, "Buzzer settings are required");
    return;
  }

  if (request["enabled"].is<bool>()) {
    buzzerManager_.setEnabled(request["enabled"].as<bool>());
  }
  if (request["volume"].is<int>()) {
    const int volume = request["volume"].as<int>();
    buzzerManager_.setVolume(static_cast<uint8_t>(constrain(volume, 0, 100)));
  }
  buzzerManager_.saveSettings(preferences_);
  handleBuzzerConfig();
}

void AppWebServer::handleEsp32OtaFinalize() {
  JsonDocument doc;
  if (Update.hasError() || esp32OtaStatus.error) {
    esp32OtaStatus.active = false;
    esp32OtaStatus.error = true;
    esp32OtaStatus.ok = false;
    if (!esp32OtaStatus.message.length() || esp32OtaStatus.message == "等待上传。") {
      esp32OtaStatus.message = Update.errorString();
    }
    doc["ok"] = false;
    doc["error"] = esp32OtaStatus.message;
    String payload;
    serializeJson(doc, payload);
    sendJson(500, payload);
    return;
  }

  esp32OtaStatus.active = false;
  esp32OtaStatus.ok = true;
  esp32OtaStatus.phase = "rebooting";
  esp32OtaStatus.message = "ESP32 固件升级成功，设备即将重启";
  doc["ok"] = true;
  doc["message"] = esp32OtaStatus.message;
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
  delay(500);
  ESP.restart();
}

void AppWebServer::handleEsp32OtaUpload() {
  HTTPUpload &upload = server_->upload();
  if (upload.status == UPLOAD_FILE_START) {
    uploadActive_ = true;
    String filename = upload.filename;
    filename.toLowerCase();
    resetEsp32OtaStatus("starting", "正在检查 ESP32 固件文件", upload.totalSize);
    if (!filename.endsWith(".bin")) {
      esp32OtaStatus.active = false;
      esp32OtaStatus.error = true;
      esp32OtaStatus.message = "只支持 .bin 固件文件";
      Update.abort();
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      esp32OtaStatus.active = false;
      esp32OtaStatus.error = true;
      esp32OtaStatus.message = Update.errorString();
      return;
    }
    esp32OtaStatus.phase = "writing";
    esp32OtaStatus.message = "正在写入 ESP32 OTA 分区";
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.isRunning()) {
      const size_t written = Update.write(upload.buf, upload.currentSize);
      esp32OtaStatus.total = upload.totalSize;
      esp32OtaStatus.written += written;
      esp32OtaStatus.phase = "writing";
      esp32OtaStatus.message = "正在写入 ESP32 OTA 分区";
      if (written != upload.currentSize) {
        esp32OtaStatus.active = false;
        esp32OtaStatus.error = true;
        esp32OtaStatus.message = Update.errorString();
        Update.abort();
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    uploadActive_ = false;
    if (Update.isRunning()) {
      esp32OtaStatus.phase = "finalizing";
      esp32OtaStatus.message = "正在校验并完成 ESP32 OTA";
      if (Update.end(true)) {
        esp32OtaStatus.written = upload.totalSize;
        esp32OtaStatus.total = upload.totalSize;
        esp32OtaStatus.phase = "done";
        esp32OtaStatus.message = "ESP32 OTA 写入完成";
      } else {
        esp32OtaStatus.active = false;
        esp32OtaStatus.error = true;
        esp32OtaStatus.message = Update.errorString();
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    uploadActive_ = false;
    esp32OtaStatus.active = false;
    esp32OtaStatus.error = true;
    esp32OtaStatus.phase = "aborted";
    esp32OtaStatus.message = "ESP32 OTA 上传已中止";
    Update.abort();
  }
}

void AppWebServer::handleEsp32OtaStatus() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["active"] = esp32OtaStatus.active;
  doc["success"] = esp32OtaStatus.ok;
  doc["error"] = esp32OtaStatus.error;
  doc["phase"] = esp32OtaStatus.phase;
  doc["message"] = esp32OtaStatus.message;
  doc["written"] = esp32OtaStatus.written;
  doc["total"] = esp32OtaStatus.total;
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
  doc["version"] = packageStore_.savedPackagesVersion();
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

void AppWebServer::handlePackagesVersion() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["version"] = packageStore_.savedPackagesVersion();
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
    doc["selectedId"] = "";
    String payload;
    serializeJson(doc, payload);
    sendJson(200, payload);
    return;
  }

  if (!packageStore_.selectSavedPackage(id, error)) {
    sendError(400, error);
    return;
  }

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "Saved firmware selected";
  doc["selectedId"] = id;
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
  if (!packageStore_.removeSavedPackage(id, error)) {
    sendError(400, error);
    return;
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
