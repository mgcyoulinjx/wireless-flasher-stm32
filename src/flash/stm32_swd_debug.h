#pragma once

#include <Arduino.h>

class SwdTransport;
class TargetControl;

class Stm32SwdDebug {
public:
  Stm32SwdDebug(SwdTransport &transport, TargetControl &targetControl);

  bool connect(String &error);
  void sampleLineLevels(String &message);
  bool readDebugPortId(uint32_t &idcode, String &error);
  bool readMemory32(uint32_t address, uint32_t &value, String &error);
  bool readMemory32Block(uint32_t address, uint8_t *data, size_t length, String &error);
  bool writeMemory32(uint32_t address, uint32_t value, String &error);
  bool writeMemory16(uint32_t address, uint16_t value, String &error);
  bool writeMemory16Block(uint32_t address, const uint8_t *data, size_t length, String &error);
  bool halt(String &error);
  bool run(String &error);
  bool reset(String &error);

private:
  SwdTransport &transport_;
  TargetControl &targetControl_;

  uint32_t cachedDpId_ = 0;
  bool hasCachedDpId_ = false;

  bool selectApBank(uint8_t apBank, String &error);
  bool readApRegister(uint8_t address, uint32_t &value, String &error);
  bool writeApRegister(uint8_t address, uint32_t value, String &error);
};
