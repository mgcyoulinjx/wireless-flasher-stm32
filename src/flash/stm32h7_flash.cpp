#include "stm32h7_flash.h"

#include "stm32_swd_debug.h"

namespace {
constexpr uint32_t kFlashKey1 = 0x45670123;
constexpr uint32_t kFlashKey2 = 0xCDEF89AB;
constexpr uint32_t kFlashBank1Keyr = 0x52002004;
constexpr uint32_t kFlashBank1Sr = 0x52002010;
constexpr uint32_t kFlashBank1Cr = 0x5200200C;
constexpr uint32_t kFlashBank1Ccr = 0x52002014;
constexpr uint32_t kFlashBank2Keyr = 0x52002104;
constexpr uint32_t kFlashBank2Sr = 0x52002110;
constexpr uint32_t kFlashBank2Cr = 0x5200210C;
constexpr uint32_t kFlashBank2Ccr = 0x52002114;
constexpr uint32_t kFlashCrLock = 0x00000001;
constexpr uint32_t kFlashCrPg = 0x00000002;
constexpr uint32_t kFlashCrSer = 0x00000004;
constexpr uint32_t kFlashCrStart = 0x00000080;
constexpr uint32_t kFlashSrBsy = 0x00000001;
constexpr uint32_t kFlashSrErrors = 0x0C73FA00;
constexpr uint32_t kFlashStart = 0x08000000UL;
constexpr uint32_t kBankSize = 0x00100000UL;
constexpr uint32_t kSectorSize = 0x00020000UL;
constexpr size_t kFlashWordSize = 32;
}

Stm32H7Flash::Stm32H7Flash(Stm32SwdDebug &debug) : debug_(debug) {}

bool Stm32H7Flash::eraseRange(uint32_t address, size_t length, uint32_t flashEnd, String &error) {
  if (address < kFlashStart || length == 0 || address + length < address || address + length > flashEnd) {
    error = "STM32H7 firmware exceeds internal flash range";
    return false;
  }
  if (!unlock(kFlashBank1Keyr, kFlashBank1Cr, error)) {
    return false;
  }
  if (flashEnd > kFlashStart + kBankSize && !unlock(kFlashBank2Keyr, kFlashBank2Cr, error)) {
    return false;
  }

  const uint32_t end = address + length;
  const int firstSector = sectorForAddress(address, flashEnd);
  const int lastSector = sectorForAddress(end - 1, flashEnd);
  if (firstSector < 0 || lastSector < 0) {
    error = "STM32H7 flash sector is not supported";
    return false;
  }
  for (int sector = firstSector; sector <= lastSector; ++sector) {
    if (!eraseSector(sector, error)) {
      return false;
    }
  }
  return debug_.writeMemory32(kFlashBank1Cr, 0, error);
}

bool Stm32H7Flash::programFlashWords(uint32_t address, const uint8_t *data, size_t length, String &error) {
  if ((address & 0x1FU) != 0 || (length & 0x1FU) != 0) {
    error = "STM32H7 flash writes require 32-byte alignment";
    return false;
  }
  if (!unlock(kFlashBank1Keyr, kFlashBank1Cr, error)) {
    return false;
  }
  const uint32_t end = address + length;
  if (end > kFlashStart + kBankSize && !unlock(kFlashBank2Keyr, kFlashBank2Cr, error)) {
    return false;
  }

  size_t offset = 0;
  while (offset < length) {
    const uint32_t currentAddress = address + offset;
    const bool bank2 = currentAddress >= kFlashStart + kBankSize;
    const uint32_t cr = bank2 ? kFlashBank2Cr : kFlashBank1Cr;
    const uint32_t sr = bank2 ? kFlashBank2Sr : kFlashBank1Sr;
    if (!waitReady(sr, bank2 ? kFlashBank2Ccr : kFlashBank1Ccr, error)) {
      return false;
    }
    if (!debug_.writeMemory32(cr, kFlashCrPg, error)) {
      return false;
    }
    if (!debug_.writeMemory32Block(currentAddress, data + offset, kFlashWordSize, error)) {
      return false;
    }
    if (!waitReady(sr, bank2 ? kFlashBank2Ccr : kFlashBank1Ccr, error)) {
      return false;
    }
    if (!debug_.writeMemory32(cr, 0, error)) {
      return false;
    }
    offset += kFlashWordSize;
  }
  return true;
}

bool Stm32H7Flash::verify(uint32_t address, const uint8_t *data, size_t length, String &error) {
  constexpr size_t kVerifyChunkSize = 2048;
  uint8_t verifyBuffer[kVerifyChunkSize];
  size_t offset = 0;
  for (; offset + kVerifyChunkSize <= length; offset += kVerifyChunkSize) {
    if (!debug_.readMemory32Block(address + offset, verifyBuffer, kVerifyChunkSize, error)) {
      return false;
    }
    if (memcmp(verifyBuffer, data + offset, kVerifyChunkSize) != 0) {
      error = "Flash verification failed";
      return false;
    }
  }
  const size_t tail = length - offset;
  if (tail > 0) {
    uint8_t expected[32];
    uint8_t actual[32];
    memset(expected, 0xFF, sizeof(expected));
    memcpy(expected, data + offset, tail);
    if (!debug_.readMemory32Block(address + offset, actual, sizeof(actual), error)) {
      return false;
    }
    if (memcmp(actual, expected, sizeof(actual)) != 0) {
      error = "Flash verification failed";
      return false;
    }
  }
  return true;
}

bool Stm32H7Flash::unlock(uint32_t keyRegister, uint32_t controlRegister, String &error) {
  uint32_t cr = 0;
  if (!debug_.readMemory32(controlRegister, cr, error)) {
    return false;
  }
  if ((cr & kFlashCrLock) == 0) {
    return true;
  }
  if (!debug_.writeMemory32(keyRegister, kFlashKey1, error)) {
    return false;
  }
  return debug_.writeMemory32(keyRegister, kFlashKey2, error);
}

bool Stm32H7Flash::waitReady(uint32_t statusRegister, uint32_t clearRegister, String &error) {
  uint32_t sr = 0;
  const unsigned long started = millis();
  do {
    if (!debug_.readMemory32(statusRegister, sr, error)) {
      return false;
    }
    if ((sr & kFlashSrBsy) == 0) {
      if (sr & kFlashSrErrors) {
        const uint32_t errors = sr & kFlashSrErrors;
        debug_.writeMemory32(clearRegister, errors, error);
        error = "STM32H7 flash controller reported an error";
        return false;
      }
      return true;
    }
    delay(1);
  } while (millis() - started < 60000UL);
  error = "Timed out waiting for STM32H7 flash ready";
  return false;
}

int Stm32H7Flash::sectorForAddress(uint32_t address, uint32_t flashEnd) const {
  if (address < kFlashStart || address >= flashEnd) {
    return -1;
  }
  return static_cast<int>((address - kFlashStart) / kSectorSize);
}

bool Stm32H7Flash::eraseSector(int sector, String &error) {
  const bool bank2 = sector >= 8;
  const uint32_t cr = bank2 ? kFlashBank2Cr : kFlashBank1Cr;
  const uint32_t sr = bank2 ? kFlashBank2Sr : kFlashBank1Sr;
  const uint32_t bankSector = static_cast<uint32_t>(sector % 8);
  if (!waitReady(sr, bank2 ? kFlashBank2Ccr : kFlashBank1Ccr, error)) {
    return false;
  }
  const uint32_t value = kFlashCrSer | (bankSector << 8);
  if (!debug_.writeMemory32(cr, value, error)) {
    return false;
  }
  if (!debug_.writeMemory32(cr, value | kFlashCrStart, error)) {
    return false;
  }
  if (!waitReady(sr, bank2 ? kFlashBank2Ccr : kFlashBank1Ccr, error)) {
    return false;
  }
  return debug_.writeMemory32(cr, 0, error);
}
