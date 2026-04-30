#pragma once

#include <Arduino.h>

class Stm32SwdDebug;

class Stm32F1Flash {
public:
  explicit Stm32F1Flash(Stm32SwdDebug &debug);

  bool readChipId(uint32_t &chipId, String &error);
  bool massErase(String &error);
  bool programHalfWords(uint32_t address, const uint8_t *data, size_t length, String &error);
  bool verify(uint32_t address, const uint8_t *data, size_t length, String &error);

private:
  Stm32SwdDebug &debug_;

  bool unlock(String &error);
  bool waitReady(String &error);
};
