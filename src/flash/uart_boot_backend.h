#pragma once

#include "flash_backend.h"

class HardwareSerial;
class Stm32Flasher;

class UartBootBackend : public FlashBackend {
public:
  UartBootBackend(HardwareSerial &serialPort, Stm32Flasher &flasher);

  FlashTransport transport() const override;
  const char *transportName() const override;
  bool flash(const FlashManifest &manifest,
             fs::FS &fs,
             const char *firmwarePath,
             FlashProgressCallback progressCallback,
             ChipDetectCallback chipDetectCallback,
             void *context,
             String &error) override;

private:
  HardwareSerial &serialPort_;
  Stm32Flasher &flasher_;
};
