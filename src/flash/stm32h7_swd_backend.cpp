#include "stm32h7_swd_backend.h"

#include <FS.h>
#include <memory>
#include "hal/target_control.h"
#include "hal/swd_transport.h"
#include "stm32_chip_info.h"
#include "stm32_swd_debug.h"
#include "stm32h7_flash.h"

Stm32H7SwdBackend::Stm32H7SwdBackend(TargetControl &targetControl,
                                     SwdTransport &transport,
                                     Stm32SwdDebug &debug,
                                     Stm32H7Flash &flash)
    : targetControl_(targetControl), transport_(transport), debug_(debug), flash_(flash) {}

const char *Stm32H7SwdBackend::transportName() const {
  return "STM32H7";
}

bool Stm32H7SwdBackend::flash(const FlashManifest &manifest,
                              fs::FS &fs,
                              const char *firmwarePath,
                              FlashProgressCallback progressCallback,
                              ChipDetectCallback chipDetectCallback,
                              void *context,
                              String &error) {
  File firmware = fs.open(firmwarePath, FILE_READ);
  if (!firmware) {
    error = "Firmware file is missing";
    return false;
  }
  if (firmware.size() != manifest.size) {
    error = "Firmware size does not match manifest";
    firmware.close();
    return false;
  }

  const size_t alignedSize = (manifest.size + 31U) & ~static_cast<size_t>(0x1FU);
  if (alignedSize == 0 || alignedSize < manifest.size) {
    error = "Firmware size is invalid";
    firmware.close();
    return false;
  }

  const unsigned long totalStarted = millis();
  if (progressCallback && !progressCallback(0, manifest.size, "SWD: preparing target pins", context)) {
    error = "Flashing cancelled";
    firmware.close();
    return false;
  }
  if (!targetControl_.prepareForSwd(error)) {
    error = "SWD prepare failed: " + error;
    firmware.close();
    return false;
  }
  String lineSample;
  debug_.sampleLineLevels(lineSample);
  if (progressCallback && !progressCallback(0, manifest.size, lineSample.c_str(), context)) {
    error = "Flashing cancelled";
    firmware.close();
    return false;
  }
  if (progressCallback && !progressCallback(0, manifest.size, "SWD: switching line and reading DPIDR", context)) {
    error = "Flashing cancelled";
    firmware.close();
    return false;
  }
  const unsigned long connectStarted = millis();
  if (!debug_.connect(error)) {
    error = "SWD connect failed: " + error;
    firmware.close();
    return false;
  }
  if (progressCallback) {
    String message = "SWD: connect took " + String(millis() - connectStarted) + " ms";
    if (!progressCallback(0, manifest.size, message.c_str(), context)) {
      error = "Flashing cancelled";
      firmware.close();
      return false;
    }
  }
  uint32_t dpId = 0;
  if (debug_.readDebugPortId(dpId, error)) {
    String message = "SWD: DPIDR 0x" + String(dpId, HEX);
    message.toUpperCase();
    if (progressCallback && !progressCallback(0, manifest.size, message.c_str(), context)) {
      error = "Flashing cancelled";
      firmware.close();
      return false;
    }
  } else {
    error = "SWD DPIDR read failed: " + error;
    firmware.close();
    return false;
  }
  if (progressCallback && !progressCallback(0, manifest.size, "SWD: halting target core", context)) {
    error = "Flashing cancelled";
    firmware.close();
    return false;
  }
  if (!debug_.halt(error)) {
    error = "SWD halt failed: " + error;
    firmware.close();
    return false;
  }

  uint32_t dbgmcuIdcode = 0;
  if (progressCallback && !progressCallback(0, manifest.size, "SWD: reading STM32 DBGMCU_IDCODE", context)) {
    error = "Flashing cancelled";
    firmware.close();
    return false;
  }
  if (!debug_.readStm32DebugId(dbgmcuIdcode, error)) {
    error = "SWD chip ID read failed: " + error;
    firmware.close();
    return false;
  }
  const uint32_t chipId = dbgmcuIdcode & 0x0FFFU;
  const Stm32ChipInfo &chip = stm32ChipInfo(chipId);
  if (chipDetectCallback) {
    chipDetectCallback(chipId, context);
  }
  if (chip.family != Stm32Family::H7) {
    error = "Detected " + stm32ChipDisplayName(chipId) + ", but selected backend is STM32H7";
    firmware.close();
    return false;
  }
  if (!stm32FamilyMatchesChipName(Stm32Family::H7, manifest.chip)) {
    error = "Firmware target " + manifest.chip + " does not match detected STM32H7";
    firmware.close();
    return false;
  }
  if (manifest.address < chip.flashStart || manifest.address + alignedSize < manifest.address ||
      manifest.address + alignedSize > chip.flashEnd) {
    error = "STM32H7 firmware exceeds detected chip flash range";
    firmware.close();
    return false;
  }

  String chipMessage = "SWD: " + stm32ChipDisplayName(chipId) + ", backend STM32H7";
  if (progressCallback && !progressCallback(0, manifest.size, chipMessage.c_str(), context)) {
    error = "Flashing cancelled";
    firmware.close();
    return false;
  }

  std::unique_ptr<uint8_t[]> buffer(new uint8_t[alignedSize]);
  memset(buffer.get(), 0xFF, alignedSize);
  const size_t readBytes = firmware.read(buffer.get(), manifest.size);
  firmware.close();
  if (readBytes != manifest.size) {
    error = "Failed to read firmware for SWD flashing";
    return false;
  }

  if (progressCallback && !progressCallback(0, manifest.size, "SWD: erasing target flash sectors", context)) {
    error = "Flashing cancelled";
    return false;
  }
  const unsigned long eraseStarted = millis();
  if (!flash_.eraseRange(manifest.address, alignedSize, chip.flashEnd, error)) {
    error = "SWD STM32H7 erase failed: " + error;
    return false;
  }
  if (progressCallback) {
    String message = "SWD: erase took " + String(millis() - eraseStarted) + " ms";
    if (!progressCallback(0, manifest.size, message.c_str(), context)) {
      error = "Flashing cancelled";
      return false;
    }
  }

  if (progressCallback && !progressCallback(0, manifest.size, "SWD: programming flash", context)) {
    error = "Flashing cancelled";
    return false;
  }
  constexpr size_t kProgramChunkSize = 4096;
  const unsigned long programStarted = millis();
  for (size_t offset = 0; offset < alignedSize; offset += kProgramChunkSize) {
    const size_t chunkSize = min(kProgramChunkSize, alignedSize - offset);
    if (!flash_.programFlashWords(manifest.address + offset, buffer.get() + offset, chunkSize, error)) {
      error = "SWD STM32H7 program failed at 0x" + String(manifest.address + offset, HEX) + ": " + error;
      return false;
    }
    const size_t written = min(offset + chunkSize, manifest.size);
    if (progressCallback) {
      String message = "SWD: programmed " + String(written) + " / " + String(manifest.size) + " bytes, " + String(millis() - programStarted) + " ms";
      if (!progressCallback(written, manifest.size, message.c_str(), context)) {
        error = "Flashing cancelled";
        return false;
      }
    }
  }

  if (progressCallback && !progressCallback(manifest.size, manifest.size, "SWD: verifying flash", context)) {
    error = "Flashing cancelled";
    return false;
  }
  const unsigned long verifyStarted = millis();
  if (!flash_.verify(manifest.address, buffer.get(), alignedSize, error)) {
    error = "SWD STM32H7 verify failed: " + error;
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
