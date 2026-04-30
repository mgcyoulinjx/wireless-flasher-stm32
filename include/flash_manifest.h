#pragma once

#include <Arduino.h>

struct FlashManifest {
  String target;
  String chip;
  uint32_t address = 0;
  size_t size = 0;
  uint32_t crc32 = 0;
};
