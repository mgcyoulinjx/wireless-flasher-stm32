#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "flash_manager.h"
#include "package_store.h"
#include "stm32_swd_debug.h"
#include "stm32f1_flash.h"
#include "stm32f1_swd_backend.h"
#include "stm32f4_flash.h"
#include "stm32fx_swd_backend.h"
#include "stm32h7_flash.h"
#include "stm32h7_swd_backend.h"
#include "target_control.h"
#include "hal/swd_transport.h"
#include "ap_manager.h"
#include "display/display_manager.h"
#include "input/input_manager.h"
#include "web_server.h"
#include "buzzer_manager.h"

namespace {
Preferences preferences;
PackageStore packageStore;
TargetControl targetControl;
SwdTransport swdTransport(AppConfig::kSwdIoPin, AppConfig::kSwdClockPin);
Stm32SwdDebug stm32SwdDebug(swdTransport, targetControl);
Stm32F1Flash stm32F1Flash(stm32SwdDebug);
Stm32F4Flash stm32F4Flash(stm32SwdDebug);
Stm32H7Flash stm32H7Flash(stm32SwdDebug);
Stm32F1SwdBackend stm32F1SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32F1Flash);
Stm32FxSwdBackend stm32F4SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32F4Flash, Stm32Family::F4);
Stm32FxSwdBackend stm32F7SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32F4Flash, Stm32Family::F7);
Stm32H7SwdBackend stm32H7SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32H7Flash);
FlashManager flashManager(packageStore, targetControl, stm32SwdDebug, stm32F1SwdBackend, stm32F4SwdBackend, stm32F7SwdBackend, stm32H7SwdBackend, preferences);
AccessPointManager accessPointManager;
DisplayManager displayManager;
InputManager inputManager(packageStore, flashManager);
BuzzerManager buzzerManager;
AppWebServer webServer(accessPointManager, packageStore, flashManager, targetControl, stm32SwdDebug, buzzerManager, preferences);
uint32_t lastChipProbeMs = 0;
uint32_t lastDetectedChipId = 0;
String displayNetworkLog;
FlashState lastFlashState = FlashState::Idle;
bool lastStationConnected = false;
bool lastFlashBusy = false;

void serviceFirmwareUpdateProgress(const String &message, const String &logEntry, size_t done, size_t total) {
  displayManager.showFirmwareUpdateProgress(message, logEntry, done, total);
  buzzerManager.update();
  delay(1);
}

bool normalizeOtaPartition() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *ota0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (!running || !ota0 || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) {
    return false;
  }
  if (running->size > ota0->size) {
    serviceFirmwareUpdateProgress("OTA 分区空间异常，保留当前分区", "错误：ota_1 大于 ota_0，已停止回写", 0, 1);
    delay(1500);
    return false;
  }

  constexpr size_t kCopyChunkSize = 4096;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[kCopyChunkSize]);
  serviceFirmwareUpdateProgress("正在准备回写 ota_0，请勿断电", "检测到当前运行在 ota_1，开始回写到 ota_0", 0, running->size);
  if (esp_partition_erase_range(ota0, 0, ota0->size) != ESP_OK) {
    serviceFirmwareUpdateProgress("擦除 ota_0 失败，保留当前分区", "错误：擦除 ota_0 失败，已停止回写", 0, 1);
    delay(1500);
    return false;
  }

  for (size_t offset = 0; offset < running->size; offset += kCopyChunkSize) {
    const size_t chunkSize = min(kCopyChunkSize, running->size - offset);
    if (esp_partition_read(running, offset, buffer.get(), chunkSize) != ESP_OK ||
        esp_partition_write(ota0, offset, buffer.get(), chunkSize) != ESP_OK) {
      serviceFirmwareUpdateProgress("回写 ota_0 失败，保留当前分区", "错误：读写分区失败，已停止回写", offset, running->size);
      delay(1500);
      return false;
    }
    if ((offset % (kCopyChunkSize * 16)) == 0 || offset + chunkSize >= running->size) {
      serviceFirmwareUpdateProgress("正在回写 ota_0，请勿断电", "已回写 " + String(offset + chunkSize) + " / " + String(running->size) + " 字节", offset + chunkSize, running->size);
    }
  }

  if (esp_ota_set_boot_partition(ota0) != ESP_OK) {
    serviceFirmwareUpdateProgress("设置 ota_0 启动失败，保留当前分区", "错误：无法设置 ota_0 为启动分区", running->size, running->size);
    delay(1500);
    return false;
  }

  serviceFirmwareUpdateProgress("回写完成，正在重启", "ota_0 回写完成，已设置为启动分区", running->size, running->size);
  delay(1000);
  ESP.restart();
  return true;
}

void clearDisplayNetworkLog() {
  displayNetworkLog = "";
}

void flashAction(void *context) {
  clearDisplayNetworkLog();
  static_cast<InputManager *>(context)->flashSelectedPackage();
}

void nextAction(void *context) {
  static_cast<InputManager *>(context)->selectNextPackage();
}

void previousAction(void *context) {
  static_cast<InputManager *>(context)->selectPreviousPackage();
}

void updateNetworkLog() {
  const bool stationConnected = accessPointManager.stationConnected();
  if (stationConnected && !lastStationConnected) {
    const String ssid = accessPointManager.stationSsid();
    displayNetworkLog = "WiFi 连接成功";
    if (ssid.length()) {
      displayNetworkLog += "\nSSID: " + ssid;
    }
    displayNetworkLog += "\nIP: " + accessPointManager.stationIpAddress();
  }
  lastStationConnected = stationConnected;
}

void updateDetectedChip() {
  if (flashManager.isBusy()) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastChipProbeMs < 2000) {
    return;
  }
  lastChipProbeMs = now;

  String error;
  if (!targetControl.prepareForSwd(error) || !stm32SwdDebug.connect(error)) {
    if (lastDetectedChipId != 0) {
      lastDetectedChipId = 0;
      flashManager.clearDetectedChip();
    }
    return;
  }

  uint32_t dbgmcuIdcode = 0;
  if (!stm32SwdDebug.readStm32DebugId(dbgmcuIdcode, error)) {
    if (lastDetectedChipId != 0) {
      lastDetectedChipId = 0;
      flashManager.clearDetectedChip();
    }
    return;
  }

  const uint32_t chipId = dbgmcuIdcode & 0x0FFFU;
  if (chipId != lastDetectedChipId || !flashManager.status().detectedChip.length()) {
    lastDetectedChipId = chipId;
    flashManager.setDetectedChip(chipId);
  }
}

DisplaySnapshot makeDisplaySnapshot() {
  FlashStatus status = flashManager.status();
  const bool flashBusy = flashManager.isBusy();
  if (flashBusy && !lastFlashBusy) {
    clearDisplayNetworkLog();
  }
  if (status.state == FlashState::Success && lastFlashState != FlashState::Success) {
    buzzerManager.playSuccessMelody();
  }
  lastFlashBusy = flashBusy;
  lastFlashState = status.state;

  DisplaySnapshot snapshot;
  snapshot.stateLabel = status.stateLabel;
  snapshot.message = status.message;
  snapshot.log = status.log;
  snapshot.targetChip = status.targetChip;
  snapshot.detectedChip = status.detectedChip;
  snapshot.flashBackend = status.flashBackend;
  snapshot.selectedPackageName = inputManager.selectedPackageName();
  snapshot.selectedPackageId = inputManager.selectedPackageId();
  snapshot.selectedPackageChip = inputManager.selectedPackageChip();
  snapshot.selectedPackageAddress = inputManager.selectedPackageAddress();
  snapshot.selectedPackageCrc32 = inputManager.selectedPackageCrc32();
  snapshot.selectedPackageSize = inputManager.selectedPackageSize();
  snapshot.selectedPackageIndex = inputManager.selectedPackageIndex();
  snapshot.savedPackageCount = inputManager.savedPackageCount();
  snapshot.uiMessage = inputManager.uiMessage();
  snapshot.networkLog = displayNetworkLog;
  snapshot.targetAddress = status.targetAddress;
  snapshot.firmwareCrc32 = status.firmwareCrc32;
  snapshot.bytesWritten = status.bytesWritten;
  snapshot.totalBytes = status.totalBytes;
  snapshot.packageReady = status.packageReady;
  snapshot.flashBusy = flashBusy;
  return snapshot;
}
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Exlink STM32 Wireless Flasher");

  targetControl.begin();

  if (!packageStore.begin()) {
    Serial.println("LittleFS init failed");
  }

  String error;
  if (!accessPointManager.begin(error)) {
    Serial.print("AP start failed: ");
    Serial.println(error);
  } else {
    Serial.print("AP ready at http://");
    Serial.println(accessPointManager.ipAddress());
  }

  flashManager.begin();
  displayManager.begin();
  buzzerManager.begin();
  buzzerManager.loadSettings(preferences);
  if (normalizeOtaPartition()) {
    return;
  }
  buzzerManager.playTestMelody();
  displayManager.setBuzzerManager(&buzzerManager);
  inputManager.setBuzzerManager(&buzzerManager);
  displayManager.onFlash(flashAction, &inputManager);
  displayManager.onNext(nextAction, &inputManager);
  displayManager.onPrevious(previousAction, &inputManager);
  inputManager.begin();
  webServer.begin();
}

void loop() {
  accessPointManager.update();
  updateNetworkLog();
  webServer.handleClient();
  inputManager.update();
  updateDetectedChip();
  displayManager.update(makeDisplaySnapshot());
  buzzerManager.update();
  delay(2);
}
