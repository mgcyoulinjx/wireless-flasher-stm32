#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "flash_backend.h"
#include "flash_manifest.h"
#include "target_control.h"

class PackageStore;

enum class FlashState {
  Idle,
  UploadReady,
  PreparingTarget,
  ConnectingBootloader,
  ConnectingSwd,
  HaltingTarget,
  Erasing,
  Writing,
  Verifying,
  Success,
  Error,
  Cancelled,
};

struct FlashStatus {
  FlashState state = FlashState::Idle;
  String stateLabel = "idle";
  String message = "Ready";
  String log = "Ready";
  String transport = "swd";
  String targetMode = "manual";
  String targetChip = "";
  String detectedChip = "";
  uint32_t targetAddress = 0;
  uint32_t firmwareCrc32 = 0;
  size_t bytesWritten = 0;
  size_t totalBytes = 0;
  bool automaticAvailable = false;
  bool packageReady = false;
};

class FlashManager {
public:
  FlashManager(PackageStore &packageStore,
               TargetControl &targetControl,
               FlashBackend &uartBackend,
               FlashBackend &swdBackend,
               Preferences &preferences);

  void begin();
  FlashStatus status();
  bool setPackageReady(String &error);
  bool startFlash(FlashTransport transport, TargetControlMode mode, String &error);
  void clearPackageState();
  void cancel();

private:
  PackageStore &packageStore_;
  TargetControl &targetControl_;
  FlashBackend &uartBackend_;
  FlashBackend &swdBackend_;
  Preferences &preferences_;
  portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t taskHandle_ = nullptr;
  volatile bool jobQueued_ = false;
  volatile bool cancelRequested_ = false;
  FlashTransport jobTransport_ = FlashTransport::Swd;
  TargetControlMode jobMode_ = TargetControlMode::Manual;
  FlashStatus status_;

  static void workerEntry(void *context);
  void workerLoop();
  FlashBackend &backendFor(FlashTransport transport);
  bool flashProgress(size_t bytesWritten, size_t totalBytes, const char *message);
  void updateDetectedChip(uint32_t chipId);
  String chipName(uint32_t chipId) const;
  void setState(FlashState state, const String &message);
  void updateTransport(FlashTransport transport);
  void updateTargetMode(TargetControlMode mode);
  void persistMode(FlashTransport transport, TargetControlMode mode);
  FlashTransport restoreTransport() const;
  TargetControlMode restoreMode() const;
  const char *stateName(FlashState state) const;
};
