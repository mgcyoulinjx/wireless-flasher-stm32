#pragma once

#include "flash_backend.h"

class TargetControl;
class SwdTransport;
class Stm32SwdDebug;
class Stm32F1Flash;

class Stm32F1SwdBackend : public FlashBackend {
public:
  Stm32F1SwdBackend(TargetControl &targetControl,
                    SwdTransport &transport,
                    Stm32SwdDebug &debug,
                    Stm32F1Flash &flash);

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
  TargetControl &targetControl_;
  SwdTransport &transport_;
  Stm32SwdDebug &debug_;
  Stm32F1Flash &flash_;
};
