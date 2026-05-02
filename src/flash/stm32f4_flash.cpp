#include "stm32f4_flash.h"

#include "stm32_swd_debug.h"

namespace {
constexpr uint32_t kFlashKeyr = 0x40023C04;
constexpr uint32_t kFlashSr = 0x40023C0C;
constexpr uint32_t kFlashCr = 0x40023C10;
constexpr uint32_t kFlashKey1 = 0x45670123;
constexpr uint32_t kFlashKey2 = 0xCDEF89AB;
constexpr uint32_t kFlashCrPg = 0x00000001;
constexpr uint32_t kFlashCrSer = 0x00000002;
constexpr uint32_t kFlashCrStrt = 0x00010000;
constexpr uint32_t kFlashCrLock = 0x80000000;
constexpr uint32_t kFlashCrPsize32 = 0x00000200;
constexpr uint32_t kFlashSrBsy = 0x00010000;
constexpr uint32_t kFlashSrErrors = 0x000000F2;
constexpr uint32_t kFlashStart = 0x08000000UL;

constexpr uint32_t kSectorStarts[] = {
    0x08000000UL, 0x08004000UL, 0x08008000UL, 0x0800C000UL,
    0x08010000UL, 0x08020000UL, 0x08040000UL, 0x08060000UL,
    0x08080000UL, 0x080A0000UL, 0x080C0000UL, 0x080E0000UL,
    0x08100000UL, 0x08104000UL, 0x08108000UL, 0x0810C000UL,
    0x08110000UL, 0x08120000UL, 0x08140000UL, 0x08160000UL,
    0x08180000UL, 0x081A0000UL, 0x081C0000UL, 0x081E0000UL,
};
}

Stm32F4Flash::Stm32F4Flash(Stm32SwdDebug &debug) : debug_(debug) {}

bool Stm32F4Flash::eraseRange(uint32_t address, size_t length, uint32_t flashEnd, Stm32Family family, String &error) {
  if (address < kFlashStart || length == 0 || address + length < address || address + length > flashEnd) {
    error = "STM32F4/F7 firmware exceeds internal flash range";
    return false;
  }
  if (!unlock(error)) {
    return false;
  }

  const uint32_t end = address + length;
  const int firstSector = sectorForAddress(address, flashEnd);
  const int lastSector = sectorForAddress(end - 1, flashEnd);
  if (firstSector < 0 || lastSector < 0) {
    error = "STM32F4/F7 flash sector is not supported";
    return false;
  }
  for (int sector = firstSector; sector <= lastSector; ++sector) {
    if (!eraseSector(sector, family, error)) {
      return false;
    }
  }
  return debug_.writeMemory32(kFlashCr, 0, error);
}

bool Stm32F4Flash::programWords(uint32_t address, const uint8_t *data, size_t length, String &error) {
  if ((address & 0x3U) != 0 || (length & 0x3U) != 0) {
    error = "STM32F4/F7 flash writes require word alignment";
    return false;
  }
  if (!unlock(error)) {
    return false;
  }
  if (!debug_.writeMemory32(kFlashCr, kFlashCrPg | kFlashCrPsize32, error)) {
    return false;
  }
  constexpr size_t kChunkSize = 1024;
  for (size_t offset = 0; offset < length; offset += kChunkSize) {
    const size_t chunkSize = min(kChunkSize, length - offset);
    if (!debug_.writeMemory32Block(address + offset, data + offset, chunkSize, error)) {
      return false;
    }
    if (!waitReady(error)) {
      return false;
    }
  }
  return debug_.writeMemory32(kFlashCr, 0, error);
}

bool Stm32F4Flash::verify(uint32_t address, const uint8_t *data, size_t length, String &error) {
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
    uint8_t expected[4];
    memset(expected, 0xFF, sizeof(expected));
    memcpy(expected, data + offset, tail);
    uint32_t actual = 0;
    if (!debug_.readMemory32(address + offset, actual, error)) {
      return false;
    }
    const uint32_t expectedWord = static_cast<uint32_t>(expected[0]) |
                                  (static_cast<uint32_t>(expected[1]) << 8) |
                                  (static_cast<uint32_t>(expected[2]) << 16) |
                                  (static_cast<uint32_t>(expected[3]) << 24);
    if (actual != expectedWord) {
      error = "Flash verification failed";
      return false;
    }
  }
  return true;
}

bool Stm32F4Flash::unlock(String &error) {
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

bool Stm32F4Flash::waitReady(String &error) {
  uint32_t sr = 0;
  const unsigned long started = millis();
  do {
    if (!debug_.readMemory32(kFlashSr, sr, error)) {
      return false;
    }
    if ((sr & kFlashSrBsy) == 0) {
      if (sr & kFlashSrErrors) {
        debug_.writeMemory32(kFlashSr, kFlashSrErrors, error);
        error = "STM32F4/F7 flash controller reported an error";
        return false;
      }
      return true;
    }
    delay(1);
  } while (millis() - started < 60000UL);
  error = "Timed out waiting for STM32F4/F7 flash ready";
  return false;
}

int Stm32F4Flash::sectorForAddress(uint32_t address, uint32_t flashEnd) const {
  if (address < kFlashStart || address >= flashEnd) {
    return -1;
  }
  for (int index = static_cast<int>(sizeof(kSectorStarts) / sizeof(kSectorStarts[0])) - 1; index >= 0; --index) {
    if (address >= kSectorStarts[index]) {
      return kSectorStarts[index] < flashEnd ? index : -1;
    }
  }
  return -1;
}

uint32_t Stm32F4Flash::sectorStart(int sector) const {
  return kSectorStarts[sector];
}

bool Stm32F4Flash::eraseSector(int sector, Stm32Family family, String &error) {
  if (!waitReady(error)) {
    return false;
  }
  if (sector > 11 && family == Stm32Family::F4) {
    error = "STM32F4 sector is outside the supported single-bank layout";
    return false;
  }
  const uint32_t sectorNumber = family == Stm32Family::F7 ? static_cast<uint32_t>(sector) : (static_cast<uint32_t>(sector) & 0x0FU);
  const uint32_t cr = kFlashCrSer | (sectorNumber << 3) | kFlashCrPsize32;
  if (!debug_.writeMemory32(kFlashCr, cr, error)) {
    return false;
  }
  if (!debug_.writeMemory32(kFlashCr, cr | kFlashCrStrt, error)) {
    return false;
  }
  return waitReady(error);
}
