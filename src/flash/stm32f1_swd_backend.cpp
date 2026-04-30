#include "stm32f1_swd_backend.h"

#include <FS.h>
#include <memory>
#include "app_config.h"
#include "hal/target_control.h"
#include "hal/swd_transport.h"
#include "stm32_swd_debug.h"
#include "stm32f1_flash.h"

Stm32F1SwdBackend::Stm32F1SwdBackend(TargetControl &targetControl,
                                     SwdTransport &transport,
                                     Stm32SwdDebug &debug,
                                     Stm32F1Flash &flash)
    : targetControl_(targetControl), transport_(transport), debug_(debug), flash_(flash) {}

FlashTransport Stm32F1SwdBackend::transport() const {
  return FlashTransport::Swd;
}

const char *Stm32F1SwdBackend::transportName() const {
  return "swd";
}

bool Stm32F1SwdBackend::flash(const FlashManifest &manifest,
                              fs::FS &fs,
                              const char *firmwarePath,
                              FlashProgressCallback progressCallback,
                              ChipDetectCallback chipDetectCallback,
                              void *context,
                              String &error) {
  if (manifest.address < AppConfig::kStm32F1FlashStart || manifest.address >= AppConfig::kStm32F1FlashEnd) {
    error = "STM32F1 SWD only supports internal flash addresses";
    return false;
  }
  if (manifest.size == 0 || manifest.address + manifest.size < manifest.address ||
      manifest.address + manifest.size > AppConfig::kStm32F1FlashEnd) {
    error = "STM32F1 SWD firmware exceeds internal flash range";
    return false;
  }

  File firmware = fs.open(firmwarePath, FILE_READ);
  if (!firmware) {
    error = "Firmware file is missing";
    return false;
  }
  if (firmware.size() != manifest.size) {
    error = "Firmware size does not match manifest";
    return false;
  }
  if ((manifest.size & 0x1U) != 0) {
    error = "STM32F1 SWD requires even firmware size";
    return false;
  }

  const unsigned long totalStarted = millis();

  if (progressCallback && !progressCallback(0, manifest.size, "SWD: preparing target pins", context)) {
    error = "Flashing cancelled";
    return false;
  }
  if (!targetControl_.prepareForSwd(error)) {
    error = "SWD prepare failed: " + error;
    return false;
  }
  String lineSample;
  debug_.sampleLineLevels(lineSample);
  if (progressCallback && !progressCallback(0, manifest.size, lineSample.c_str(), context)) {
    error = "Flashing cancelled";
    return false;
  }
  if (progressCallback && !progressCallback(0, manifest.size, "SWD: switching line and reading DPIDR", context)) {
    error = "Flashing cancelled";
    return false;
  }
  const unsigned long connectStarted = millis();
  if (!debug_.connect(error)) {
    error = "SWD connect failed: " + error;
    return false;
  }
  if (progressCallback) {
    String message = "SWD: connect took " + String(millis() - connectStarted) + " ms";
    if (!progressCallback(0, manifest.size, message.c_str(), context)) {
      error = "Flashing cancelled";
      return false;
    }
  }
  uint32_t dpId = 0;
  if (debug_.readDebugPortId(dpId, error)) {
    String message = "SWD: DPIDR 0x" + String(dpId, HEX);
    message.toUpperCase();
    if (progressCallback && !progressCallback(0, manifest.size, message.c_str(), context)) {
      error = "Flashing cancelled";
      return false;
    }
  } else {
    error = "SWD DPIDR read failed: " + error;
    return false;
  }
  if (progressCallback && !progressCallback(0, manifest.size, "SWD: halting target core", context)) {
    error = "Flashing cancelled";
    return false;
  }
  if (!debug_.halt(error)) {
    error = "SWD halt failed: " + error;
    return false;
  }

  uint32_t chipId = 0;
  if (progressCallback && !progressCallback(0, manifest.size, "SWD: reading STM32 DBGMCU_IDCODE", context)) {
    error = "Flashing cancelled";
    return false;
  }
  if (!flash_.readChipId(chipId, error)) {
    error = "SWD chip ID read failed: " + error;
    return false;
  }
  if (chipDetectCallback) {
    chipDetectCallback(chipId & 0x0FFFU, context);
  }
  String chipMessage = "SWD: DBGMCU_IDCODE 0x" + String(chipId, HEX);
  chipMessage.toUpperCase();
  if (progressCallback && !progressCallback(0, manifest.size, chipMessage.c_str(), context)) {
    error = "Flashing cancelled";
    return false;
  }

  if (progressCallback && !progressCallback(0, manifest.size, "SWD: mass erase", context)) {
    error = "Flashing cancelled";
    return false;
  }
  const unsigned long eraseStarted = millis();
  if (!flash_.massErase(error)) {
    error = "SWD mass erase failed: " + error;
    return false;
  }
  if (progressCallback) {
    String message = "SWD: mass erase took " + String(millis() - eraseStarted) + " ms";
    if (!progressCallback(0, manifest.size, message.c_str(), context)) {
      error = "Flashing cancelled";
      return false;
    }
  }

  std::unique_ptr<uint8_t[]> buffer(new uint8_t[manifest.size]);
  size_t readBytes = firmware.read(buffer.get(), manifest.size);
  firmware.close();
  if (readBytes != manifest.size) {
    error = "Failed to read firmware for SWD flashing";
    return false;
  }

  if (progressCallback && !progressCallback(0, manifest.size, "SWD: programming flash", context)) {
    error = "Flashing cancelled";
    return false;
  }
  constexpr size_t kProgramChunkSize = 1024;
  const unsigned long programStarted = millis();
  for (size_t offset = 0; offset < manifest.size; offset += kProgramChunkSize) {
    const size_t chunkSize = min(kProgramChunkSize, manifest.size - offset);
    if (!flash_.programHalfWords(manifest.address + offset, buffer.get() + offset, chunkSize, error)) {
      error = "SWD program failed at 0x" + String(manifest.address + offset, HEX) + ": " + error;
      return false;
    }
    if (progressCallback && (((offset + chunkSize) % 4096U) == 0 || offset + chunkSize == manifest.size)) {
      String message = "SWD: programmed " + String(offset + chunkSize) + " / " + String(manifest.size) + " bytes, " + String(millis() - programStarted) + " ms";
      if (!progressCallback(offset + chunkSize, manifest.size, message.c_str(), context)) {
        error = "Flashing cancelled";
        return false;
      }
    }
  }
  if (progressCallback) {
    String message = "SWD: programming complete, took " + String(millis() - programStarted) + " ms";
    if (!progressCallback(manifest.size, manifest.size, message.c_str(), context)) {
      error = "Flashing cancelled";
      return false;
    }
  }
  if (progressCallback && !progressCallback(manifest.size, manifest.size, "SWD: verifying flash", context)) {
    error = "Flashing cancelled";
    return false;
  }
  const unsigned long verifyStarted = millis();
  if (!flash_.verify(manifest.address, buffer.get(), manifest.size, error)) {
    error = "SWD verify failed: " + error;
    return false;
  }
  if (progressCallback) {
    String message = "SWD: verify took " + String(millis() - verifyStarted) + " ms";
    if (!progressCallback(manifest.size, manifest.size, message.c_str(), context)) {
      error = "Flashing cancelled";
      return false;
    }
  }
  if (progressCallback && !progressCallback(manifest.size, manifest.size, "SWD: running target", context)) {
    error = "Flashing cancelled";
    return false;
  }
  if (!debug_.run(error)) {
    error = "SWD run failed: " + error;
    return false;
  }
  if (!debug_.reset(error)) {
    error = "SWD reset failed: " + error;
    return false;
  }
  if (progressCallback) {
    String message = "SWD: reset complete, total " + String(millis() - totalStarted) + " ms";
    progressCallback(manifest.size, manifest.size, message.c_str(), context);
  }
  return true;
}
