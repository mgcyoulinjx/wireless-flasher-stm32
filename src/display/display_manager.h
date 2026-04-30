#pragma once

#include <Arduino.h>
#include "flash/flash_manager.h"

class TFT_eSPI;

struct DisplaySnapshot {
  String ssid;
  String ipAddress;
  String stateLabel;
  String message;
  String transport;
  String targetMode;
  String targetChip;
  String detectedChip;
  String wiringSummary;
  uint32_t targetAddress = 0;
  uint32_t firmwareCrc32 = 0;
  size_t bytesWritten = 0;
  size_t totalBytes = 0;
  uint32_t targetBaudRate = 0;
  bool automaticAvailable = false;
  bool packageReady = false;
};

class DisplayManager {
public:
  void begin();
  void update(const DisplaySnapshot &snapshot);

private:
  TFT_eSPI *tft_ = nullptr;
  bool initialized_ = false;
  bool firstFrame_ = true;
  int screenWidth_ = 0;
  int screenHeight_ = 0;
  DisplaySnapshot lastSnapshot_;

  bool shouldRender(const DisplaySnapshot &snapshot) const;
  void render(const DisplaySnapshot &snapshot);
  void drawFrame();
  void drawHeader(const String &title, uint16_t color);
  void drawLabelValue(int x, int y, int width, const String &label, const String &value, uint16_t valueColor);
  void drawMessageBox(const String &message);
  void drawProgressBar(int x, int y, int width, int height, int percent, uint16_t color);
  String summarizePackage(const DisplaySnapshot &snapshot) const;
  String summarizeTransfer(const DisplaySnapshot &snapshot) const;
  String formatHex(uint32_t value, uint8_t minWidth = 0) const;
};
