#pragma once

#include <Arduino.h>

class Stm32SwdDebug;

class Stm32H7Flash {
public:
  explicit Stm32H7Flash(Stm32SwdDebug &debug);

  bool eraseRange(uint32_t address, size_t length, uint32_t flashEnd, String &error);
  bool programFlashWords(uint32_t address, const uint8_t *data, size_t length, String &error);
  bool verify(uint32_t address, const uint8_t *data, size_t length, String &error);

private:
  Stm32SwdDebug &debug_;

  bool unlock(uint32_t keyRegister, uint32_t controlRegister, String &error);
  bool waitReady(uint32_t statusRegister, uint32_t clearRegister, String &error);
  int sectorForAddress(uint32_t address, uint32_t flashEnd) const;
  bool eraseSector(int sector, String &error);
};
