#pragma once

#include <Arduino.h>
#include "stm32_chip_info.h"

class Stm32SwdDebug;

class Stm32F4Flash {
public:
  explicit Stm32F4Flash(Stm32SwdDebug &debug);

  bool eraseRange(uint32_t address, size_t length, uint32_t flashEnd, Stm32Family family, String &error);
  bool programWords(uint32_t address, const uint8_t *data, size_t length, String &error);
  bool verify(uint32_t address, const uint8_t *data, size_t length, String &error);

private:
  Stm32SwdDebug &debug_;

  bool unlock(String &error);
  bool waitReady(String &error);
  int sectorForAddress(uint32_t address, uint32_t flashEnd) const;
  uint32_t sectorStart(int sector) const;
  bool eraseSector(int sector, Stm32Family family, String &error);
};
