#pragma once

#include <Arduino.h>
#include <FS.h>
#include "flash_backend.h"
#include "flash_manifest.h"

class Stm32Flasher {
public:
  bool flash(HardwareSerial &serialPort,
             const FlashManifest &manifest,
             fs::FS &fs,
             const char *firmwarePath,
             FlashProgressCallback progressCallback,
             ChipDetectCallback chipDetectCallback,
             void *context,
             String &error);

private:
  static constexpr uint8_t kAck = 0x79;
  static constexpr uint8_t kNack = 0x1F;

  bool sync(HardwareSerial &serialPort, String &error);
  bool getChipId(HardwareSerial &serialPort, uint16_t &chipId, String &error);
  bool erase(HardwareSerial &serialPort, String &error);
  bool writeMemory(HardwareSerial &serialPort, uint32_t address, const uint8_t *data, size_t length, String &error);
  bool go(HardwareSerial &serialPort, uint32_t address, String &error);
  bool expectAck(HardwareSerial &serialPort, uint32_t timeoutMs, String &error);
  bool sendCommand(HardwareSerial &serialPort, uint8_t command, String &error);
  bool sendAddress(HardwareSerial &serialPort, uint32_t address, String &error);
};
