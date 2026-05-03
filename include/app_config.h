#pragma once

#include <Arduino.h>

namespace AppConfig {
static constexpr const char *kFirmwareVersion = "0.1.8";
static constexpr const char *kAccessPointSsid = "Exlink-Flasher";
static constexpr const char *kAccessPointPassword = "12345678";
inline IPAddress accessPointIp() { return IPAddress(192, 168, 4, 1); }
inline IPAddress gatewayIp() { return IPAddress(192, 168, 4, 1); }
inline IPAddress subnetMask() { return IPAddress(255, 255, 255, 0); }
static constexpr int kSwdIoPin = 11;
static constexpr int kSwdClockPin = 12;
static constexpr int kSwdResetPin = -1;
static constexpr int kBuzzerPin = 3;
static constexpr int kBuzzerLedcChannel = 1;
static constexpr int kDisplayWidth = 240;
static constexpr int kDisplayHeight = 284;
static constexpr int kDisplayMosiPin = 8;
static constexpr int kDisplaySclkPin = 9;
static constexpr int kDisplayResetPin = 10;
static constexpr int kDisplayDcPin = 14;
static constexpr int kDisplayCsPin = 15;
static constexpr int kDisplayBacklightPin = -1;
static constexpr int kTouchResetPin = 16;
static constexpr int kTouchSdaPin = 18;
static constexpr int kTouchSclPin = 21;
static constexpr int kTouchInterruptPin = -1;
static constexpr int kKeyRightPin = 38;
static constexpr int kKeyLeftPin = 39;
static constexpr int kKeyPushPin = 40;
static constexpr const char *kExampleTargetChip = "stm32f103c8";
static constexpr const char *kRecommendedSwdWiringSummary = "SWD: GND + SWDIO11 + SWCLK12, no NRST";
static constexpr uint32_t kDefaultFlashAddress = 0x08000000UL;
static constexpr uint32_t kStm32F1FlashStart = 0x08000000UL;
static constexpr uint32_t kStm32F1FlashEnd = 0x08080000UL;
static constexpr size_t kStm32F1PageSize = 1024;
static constexpr size_t kMaxFirmwareSize = 2 * 1024 * 1024;
static constexpr size_t kMaxHexUploadSize = kMaxFirmwareSize * 3;
static constexpr const char *kHexTempPath = "/upload_firmware.hex.tmp";
static constexpr const char *kManifestTempPath = "/upload_manifest.tmp";
static constexpr const char *kFirmwareTempPath = "/upload_firmware.tmp";
static constexpr const char *kManifestPath = "/manifest.json";
static constexpr const char *kFirmwarePath = "/app.bin";
static constexpr const char *kSavedPackagesIndexPath = "/packages.json";
static constexpr const char *kSavedPackagesIndexTempPath = "/packages.tmp";
static constexpr const char *kSavedPackagePrefix = "/pkg_";
static constexpr const char *kSavedPackageManifestSuffix = ".json";
static constexpr const char *kSavedPackageFirmwareSuffix = ".bin";
static constexpr size_t kLittleFsSafetyMargin = 16 * 1024;
}
