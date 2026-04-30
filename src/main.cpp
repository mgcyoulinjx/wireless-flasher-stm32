#include <Arduino.h>
#include <Preferences.h>

#include "flash_manager.h"
#include "package_store.h"
#include "stm32_flasher.h"
#include "stm32_swd_debug.h"
#include "stm32f1_flash.h"
#include "stm32f1_swd_backend.h"
#include "target_control.h"
#include "uart_boot_backend.h"
#include "hal/swd_transport.h"
#include "ap_manager.h"
#include "display/display_manager.h"
#include "web_server.h"

namespace {
Preferences preferences;
PackageStore packageStore;
TargetControl targetControl(Serial1);
Stm32Flasher stm32Flasher;
SwdTransport swdTransport(AppConfig::kSwdIoPin, AppConfig::kSwdClockPin);
Stm32SwdDebug stm32SwdDebug(swdTransport, targetControl);
Stm32F1Flash stm32F1Flash(stm32SwdDebug);
UartBootBackend uartBootBackend(targetControl.serial(), stm32Flasher);
Stm32F1SwdBackend stm32F1SwdBackend(targetControl, swdTransport, stm32SwdDebug, stm32F1Flash);
FlashManager flashManager(packageStore, targetControl, uartBootBackend, stm32F1SwdBackend, preferences);
AccessPointManager accessPointManager;
DisplayManager displayManager;
AppWebServer webServer(accessPointManager, packageStore, flashManager, targetControl, stm32SwdDebug, stm32F1Flash);

DisplaySnapshot makeDisplaySnapshot() {
  FlashStatus status = flashManager.status();
  DisplaySnapshot snapshot;
  snapshot.ssid = accessPointManager.ssid();
  snapshot.ipAddress = accessPointManager.ipAddress();
  snapshot.stateLabel = status.stateLabel;
  snapshot.message = status.message;
  snapshot.transport = status.transport;
  snapshot.targetMode = status.targetMode;
  snapshot.targetChip = status.targetChip;
  snapshot.detectedChip = status.detectedChip;
  snapshot.wiringSummary = status.transport == "swd" ? AppConfig::kRecommendedSwdWiringSummary : AppConfig::kRecommendedWiringSummary;
  snapshot.targetAddress = status.targetAddress;
  snapshot.firmwareCrc32 = status.firmwareCrc32;
  snapshot.bytesWritten = status.bytesWritten;
  snapshot.totalBytes = status.totalBytes;
  snapshot.targetBaudRate = AppConfig::kTargetBaudRate;
  snapshot.automaticAvailable = status.automaticAvailable;
  snapshot.packageReady = status.packageReady;
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
  webServer.begin();
}

void loop() {
  webServer.handleClient();
  displayManager.update(makeDisplaySnapshot());
  delay(2);
}
