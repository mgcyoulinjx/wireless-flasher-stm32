#include "stm32f1_flash.h"

#include "stm32_swd_debug.h"

namespace {
constexpr uint32_t kDbgmcuIdcode = 0xE0042000;
constexpr uint32_t kFlashKeyr = 0x40022004;
constexpr uint32_t kFlashSr = 0x4002200C;
constexpr uint32_t kFlashCr = 0x40022010;
constexpr uint32_t kFlashAr = 0x40022014;
constexpr uint32_t kFlashKey1 = 0x45670123;
constexpr uint32_t kFlashKey2 = 0xCDEF89AB;
constexpr uint32_t kFlashCrPg = 0x00000001;
constexpr uint32_t kFlashCrPer = 0x00000002;
constexpr uint32_t kFlashCrMer = 0x00000004;
constexpr uint32_t kFlashCrStrt = 0x00000040;
constexpr uint32_t kFlashCrLock = 0x00000080;
constexpr uint32_t kFlashSrBsY = 0x00000001;
constexpr uint32_t kFlashSrPgErr = 0x00000004;
constexpr uint32_t kFlashSrWrPrtErr = 0x00000010;
constexpr uint32_t kFlashSrEop = 0x00000020;
}

Stm32F1Flash::Stm32F1Flash(Stm32SwdDebug &debug) : debug_(debug) {}

bool Stm32F1Flash::readChipId(uint32_t &chipId, String &error) {
  return debug_.readMemory32(kDbgmcuIdcode, chipId, error);
}

bool Stm32F1Flash::massErase(String &error) {
  if (!unlock(error)) {
    return false;
  }

  if (!debug_.writeMemory32(kFlashCr, kFlashCrMer, error)) {
    return false;
  }
  if (!debug_.writeMemory32(kFlashCr, kFlashCrMer | kFlashCrStrt, error)) {
    return false;
  }
  if (!waitReady(error)) {
    return false;
  }
  return debug_.writeMemory32(kFlashCr, 0, error);
}

bool Stm32F1Flash::programHalfWords(uint32_t address, const uint8_t *data, size_t length, String &error) {
  if ((address & 0x1U) != 0 || (length & 0x1U) != 0) {
    error = "STM32F1 flash writes require half-word alignment";
    return false;
  }
  if (!unlock(error)) {
    return false;
  }

  if (!debug_.writeMemory32(kFlashCr, kFlashCrPg, error)) {
    return false;
  }
  if (!debug_.writeMemory16Block(address, data, length, error)) {
    return false;
  }
  if (!waitReady(error)) {
    return false;
  }

  return debug_.writeMemory32(kFlashCr, 0, error);
}

bool Stm32F1Flash::verify(uint32_t address, const uint8_t *data, size_t length, String &error) {
  constexpr size_t kVerifyChunkSize = 1024;
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
  for (; offset < length; offset += 4) {
    uint32_t expected = 0xFFFFFFFFUL;
    for (size_t i = 0; i < 4 && offset + i < length; ++i) {
      expected &= ~(0xFFUL << (i * 8));
      expected |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    }
    uint32_t actual = 0;
    if (!debug_.readMemory32(address + offset, actual, error)) {
      return false;
    }
    if (actual != expected) {
      error = "Flash verification failed";
      return false;
    }
  }
  return true;
}

bool Stm32F1Flash::unlock(String &error) {
  uint32_t cr = 0;
  if (!debug_.readMemory32(kFlashCr, cr, error)) {
    return false;
  }
  if ((cr & kFlashCrLock) == 0) {
    return true;
  }
  if (!debug_.writeMemory32(kFlashKeyr, kFlashKey1, error)) {
    return false;
  }
  return debug_.writeMemory32(kFlashKeyr, kFlashKey2, error);
}

bool Stm32F1Flash::waitReady(String &error) {
  uint32_t sr = 0;
  unsigned long started = millis();
  do {
    if (!debug_.readMemory32(kFlashSr, sr, error)) {
      return false;
    }
    if ((sr & kFlashSrBsY) == 0) {
      if (sr & (kFlashSrPgErr | kFlashSrWrPrtErr)) {
        error = "STM32F1 flash controller reported an error";
        return false;
      }
      return true;
    }
    delay(1);
  } while (millis() - started < 30000UL);

  error = "Timed out waiting for STM32F1 flash ready";
  return false;
}
