#pragma once

#include <Arduino.h>
#include <FS.h>
#include "flash_manifest.h"

using FlashProgressCallback = bool (*)(size_t bytesWritten, size_t totalBytes, const char *message, void *context);
using ChipDetectCallback = void (*)(uint32_t chipId, void *context);

enum class FlashTransport {
  Uart,
  Swd,
};

class FlashBackend {
public:
  virtual ~FlashBackend() = default;
  virtual FlashTransport transport() const = 0;
  virtual const char *transportName() const = 0;
  virtual bool flash(const FlashManifest &manifest,
                     fs::FS &fs,
                     const char *firmwarePath,
                     FlashProgressCallback progressCallback,
                     ChipDetectCallback chipDetectCallback,
                     void *context,
                     String &error) = 0;
};
