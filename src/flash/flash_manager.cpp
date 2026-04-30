#include "flash_manager.h"

#include <LittleFS.h>
#include "app_config.h"
#include "package_store.h"

namespace {
constexpr const char *kPrefsNamespace = "flasher";
constexpr const char *kPrefsModeKey = "target_mode";
constexpr const char *kPrefsTransportKey = "transport";
}

FlashManager::FlashManager(PackageStore &packageStore,
                           TargetControl &targetControl,
                           FlashBackend &uartBackend,
                           FlashBackend &swdBackend,
                           Preferences &preferences)
    : packageStore_(packageStore), targetControl_(targetControl), uartBackend_(uartBackend), swdBackend_(swdBackend),
      preferences_(preferences) {}

void FlashManager::begin() {
  preferences_.begin(kPrefsNamespace, false);
  status_.automaticAvailable = targetControl_.isAutomaticAvailable();
  jobTransport_ = FlashTransport::Swd;
  jobMode_ = TargetControlMode::Manual;
  updateTransport(jobTransport_);
  updateTargetMode(jobMode_);
  status_.packageReady = packageStore_.hasPackage();
  if (status_.packageReady) {
    FlashManifest manifest;
    String error;
    if (packageStore_.loadManifest(manifest, error)) {
      portENTER_CRITICAL(&mutex_);
      status_.targetChip = manifest.chip;
      status_.detectedChip = "";
      status_.targetAddress = manifest.address;
      status_.firmwareCrc32 = manifest.crc32;
      status_.totalBytes = manifest.size;
      portEXIT_CRITICAL(&mutex_);
    }
    setState(FlashState::UploadReady, "Firmware package is ready");
  }
  xTaskCreatePinnedToCore(&FlashManager::workerEntry, "flash_worker", 8192, this, 1, &taskHandle_, ARDUINO_RUNNING_CORE);
}

FlashStatus FlashManager::status() {
  portENTER_CRITICAL(&mutex_);
  FlashStatus copy = status_;
  portEXIT_CRITICAL(&mutex_);
  return copy;
}

bool FlashManager::setPackageReady(String &error) {
  if (!packageStore_.hasPackage()) {
    error = "Firmware package is incomplete";
    return false;
  }

  FlashManifest manifest;
  if (!packageStore_.loadManifest(manifest, error)) {
    return false;
  }

  portENTER_CRITICAL(&mutex_);
  status_.packageReady = true;
  status_.targetChip = manifest.chip;
  status_.detectedChip = "";
  status_.targetAddress = manifest.address;
  status_.firmwareCrc32 = manifest.crc32;
  status_.bytesWritten = 0;
  status_.totalBytes = manifest.size;
  portEXIT_CRITICAL(&mutex_);
  setState(FlashState::UploadReady, "Firmware package is ready");
  return true;
}

bool FlashManager::startFlash(FlashTransport transport, TargetControlMode mode, String &error) {
  (void)transport;
  (void)mode;
  transport = FlashTransport::Swd;
  mode = TargetControlMode::Manual;
  FlashStatus snapshot = status();
  if (!snapshot.packageReady) {
    error = "Upload a valid package first";
    return false;
  }
  if (snapshot.state == FlashState::PreparingTarget || snapshot.state == FlashState::ConnectingBootloader ||
      snapshot.state == FlashState::ConnectingSwd || snapshot.state == FlashState::HaltingTarget ||
      snapshot.state == FlashState::Erasing || snapshot.state == FlashState::Writing || snapshot.state == FlashState::Verifying) {
    error = "A flash job is already running";
    return false;
  }
  mode = TargetControlMode::Manual;

  jobTransport_ = transport;
  jobMode_ = mode;
  persistMode(transport, mode);
  updateTransport(transport);
  updateTargetMode(mode);
  cancelRequested_ = false;
  jobQueued_ = true;
  if (transport == FlashTransport::Swd) {
    setState(FlashState::PreparingTarget, "Preparing target for SWD");
  } else {
    setState(FlashState::PreparingTarget,
             mode == TargetControlMode::Automatic ? "Preparing target automatically" : "Waiting for the target in bootloader mode");
  }
  return true;
}

void FlashManager::clearPackageState() {
  portENTER_CRITICAL(&mutex_);
  status_.packageReady = false;
  status_.targetChip = "";
  status_.detectedChip = "";
  status_.targetAddress = 0;
  status_.firmwareCrc32 = 0;
  status_.bytesWritten = 0;
  status_.totalBytes = 0;
  portEXIT_CRITICAL(&mutex_);
  setState(FlashState::Idle, "Ready");
}

void FlashManager::cancel() {
  cancelRequested_ = true;
}

void FlashManager::workerEntry(void *context) {
  static_cast<FlashManager *>(context)->workerLoop();
}

void FlashManager::workerLoop() {
  for (;;) {
    if (!jobQueued_) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    jobQueued_ = false;
    cancelRequested_ = false;

    FlashManifest manifest;
    String error;
    if (!packageStore_.loadManifest(manifest, error)) {
      setState(FlashState::Error, error);
      continue;
    }

    portENTER_CRITICAL(&mutex_);
    status_.targetChip = manifest.chip;
    status_.detectedChip = "";
    status_.targetAddress = manifest.address;
    status_.firmwareCrc32 = manifest.crc32;
    status_.bytesWritten = 0;
    status_.totalBytes = manifest.size;
    portEXIT_CRITICAL(&mutex_);

    if (cancelRequested_) {
      setState(FlashState::Cancelled, "Flash job cancelled");
      continue;
    }

    jobTransport_ = FlashTransport::Swd;
    jobMode_ = TargetControlMode::Manual;
    FlashBackend &backend = backendFor(jobTransport_);
    updateTransport(jobTransport_);
    updateTargetMode(jobMode_);

    if (jobTransport_ == FlashTransport::Swd) {
      setState(FlashState::PreparingTarget, "Preparing target for SWD");
      if (!targetControl_.prepareForSwd(error)) {
        setState(FlashState::Error, error);
        continue;
      }
    } else {
      setState(FlashState::PreparingTarget,
               jobMode_ == TargetControlMode::Automatic ? "Preparing target automatically" : "Waiting for manual bootloader mode");
      if (!targetControl_.prepareForBootloader(jobMode_, error)) {
        setState(FlashState::Error, error);
        continue;
      }
    }

    if (cancelRequested_) {
      setState(FlashState::Cancelled, "Flash job cancelled");
      continue;
    }

    if (jobTransport_ == FlashTransport::Swd) {
      setState(FlashState::ConnectingSwd, "Connecting to the STM32 SWD port");
    } else {
      setState(FlashState::ConnectingBootloader, "Connecting to the STM32 bootloader");
    }
    setState(FlashState::Erasing, "Erasing target flash");

    if (!backend.flash(manifest, LittleFS, AppConfig::kFirmwarePath,
                       [](size_t bytesWritten, size_t totalBytes, const char *message, void *context) {
                         return static_cast<FlashManager *>(context)->flashProgress(bytesWritten, totalBytes, message);
                       },
                       [](uint32_t chipId, void *context) {
                         static_cast<FlashManager *>(context)->updateDetectedChip(chipId);
                       },
                       this, error)) {
      if (error == "Flashing cancelled") {
        setState(FlashState::Cancelled, "Flash job cancelled");
      } else {
        setState(FlashState::Error, error);
      }
      continue;
    }

    setState(FlashState::Success, "Firmware flashed successfully");
  }
}

FlashBackend &FlashManager::backendFor(FlashTransport transport) {
  return transport == FlashTransport::Swd ? swdBackend_ : uartBackend_;
}

bool FlashManager::flashProgress(size_t bytesWritten, size_t totalBytes, const char *message) {
  if (cancelRequested_) {
    return false;
  }

  portENTER_CRITICAL(&mutex_);
  status_.bytesWritten = bytesWritten;
  status_.totalBytes = totalBytes;
  if (bytesWritten > 0 || totalBytes == 0) {
    status_.state = FlashState::Writing;
    status_.stateLabel = stateName(FlashState::Writing);
  }
  status_.message = message && message[0] ? message : "Writing firmware";
  if (message && message[0]) {
    if (status_.log.length() > 0) {
      status_.log += "\n";
    }
    status_.log += message;
  }
  portEXIT_CRITICAL(&mutex_);
  return true;
}

void FlashManager::updateDetectedChip(uint32_t chipId) {
  const String name = chipName(chipId);
  portENTER_CRITICAL(&mutex_);
  status_.detectedChip = name;
  status_.message = "Connected: " + name;
  if (status_.log.length() > 0) {
    status_.log += "\n";
  }
  status_.log += "Detected: " + name;
  portEXIT_CRITICAL(&mutex_);
}

String FlashManager::chipName(uint32_t chipId) const {
  switch (chipId) {
    case 0x0410:
      return "STM32F1 medium-density (0x0410)";
    case 0x0412:
      return "STM32F1 low-density (0x0412)";
    case 0x0414:
      return "STM32F1 high-density (0x0414)";
    case 0x0418:
      return "STM32F1 connectivity line (0x0418)";
    case 0x0420:
      return "STM32F1 value line (0x0420)";
    case 0x0413:
      return "STM32F4 (0x0413)";
    case 0x0419:
      return "STM32F42x/F43x (0x0419)";
    case 0x0411:
      return "STM32F2 (0x0411)";
    case 0x0433:
      return "STM32F4/F7 (0x0433)";
    case 0x0444:
      return "STM32F0 (0x0444)";
    case 0x0440:
      return "STM32F0 small (0x0440)";
    case 0x0448:
      return "STM32F0 value line (0x0448)";
    case 0x0442:
      return "STM32F0x2 (0x0442)";
    case 0x0445:
      return "STM32F04/F07 (0x0445)";
    case 0x0416:
      return "STM32L1 medium-density (0x0416)";
    case 0x0429:
      return "STM32L1 Cat.5 (0x0429)";
    case 0x0415:
      return "STM32L1 high-density (0x0415)";
    case 0x0435:
      return "STM32L4 (0x0435)";
    case 0x0461:
      return "STM32L4+ (0x0461)";
    case 0x0450:
      return "STM32H7 (0x0450)";
    default: {
      String hex = String(chipId, HEX);
      hex.toUpperCase();
      while (hex.length() < 4) {
        hex = "0" + hex;
      }
      return "STM32 chip ID 0x" + hex;
    }
  }
}

void FlashManager::setState(FlashState state, const String &message) {
  portENTER_CRITICAL(&mutex_);
  status_.state = state;
  status_.stateLabel = stateName(state);
  status_.message = message;
  if (state == FlashState::PreparingTarget) {
    status_.log = message;
  } else if (message.length() > 0) {
    if (status_.log.length() > 0) {
      status_.log += "\n";
    }
    status_.log += message;
  }
  status_.automaticAvailable = false;
  portEXIT_CRITICAL(&mutex_);
}

void FlashManager::updateTransport(FlashTransport transport) {
  portENTER_CRITICAL(&mutex_);
  status_.transport = "swd";
  portEXIT_CRITICAL(&mutex_);
}

void FlashManager::updateTargetMode(TargetControlMode mode) {
  portENTER_CRITICAL(&mutex_);
  status_.targetMode = mode == TargetControlMode::Automatic ? targetControl_.modeName(mode) : "manual";
  portEXIT_CRITICAL(&mutex_);
}

void FlashManager::persistMode(FlashTransport transport, TargetControlMode mode) {
  preferences_.putUChar(kPrefsTransportKey, transport == FlashTransport::Swd ? 1 : 0);
  preferences_.putUChar(kPrefsModeKey, mode == TargetControlMode::Automatic ? 1 : 0);
}

FlashTransport FlashManager::restoreTransport() const {
  return FlashTransport::Swd;
}

TargetControlMode FlashManager::restoreMode() const {
  return TargetControlMode::Manual;
}

const char *FlashManager::stateName(FlashState state) const {
  switch (state) {
    case FlashState::Idle:
      return "idle";
    case FlashState::UploadReady:
      return "upload_ready";
    case FlashState::PreparingTarget:
      return "preparing_target";
    case FlashState::ConnectingBootloader:
      return "connecting_bootloader";
    case FlashState::ConnectingSwd:
      return "connecting_swd";
    case FlashState::HaltingTarget:
      return "halting_target";
    case FlashState::Erasing:
      return "erasing";
    case FlashState::Writing:
      return "writing";
    case FlashState::Verifying:
      return "verifying";
    case FlashState::Success:
      return "success";
    case FlashState::Error:
      return "error";
    case FlashState::Cancelled:
      return "cancelled";
  }
  return "unknown";
}
