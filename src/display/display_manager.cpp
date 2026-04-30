#include "display_manager.h"

#include <TFT_eSPI.h>
#include "app_config.h"

namespace {
constexpr uint16_t kColorBackground = TFT_BLACK;
constexpr uint16_t kColorPanel = 0x1082;
constexpr uint16_t kColorHeader = 0x2104;
constexpr uint16_t kColorAccent = TFT_CYAN;
constexpr uint16_t kColorSuccess = TFT_GREEN;
constexpr uint16_t kColorError = TFT_RED;
constexpr uint16_t kColorText = TFT_WHITE;
constexpr uint16_t kColorMuted = 0x8410;
constexpr uint16_t kColorWarning = TFT_YELLOW;
constexpr uint16_t kColorProgress = 0x061F;
}

void DisplayManager::begin() {
  if (initialized_) {
    return;
  }

  static TFT_eSPI tft;
  tft_ = &tft;
  tft_->init();
  tft_->setRotation(3);
  tft_->setTextColor(kColorText, kColorBackground);
  tft_->setTextDatum(TL_DATUM);
  screenWidth_ = tft_->width();
  screenHeight_ = tft_->height();
  drawFrame();
  initialized_ = true;

  DisplaySnapshot bootSnapshot;
  bootSnapshot.stateLabel = "booting";
  bootSnapshot.message = "Initializing display";
  render(bootSnapshot);
}

void DisplayManager::update(const DisplaySnapshot &snapshot) {
  if (!initialized_ || !shouldRender(snapshot)) {
    return;
  }

  render(snapshot);
  lastSnapshot_ = snapshot;
  firstFrame_ = false;
}

bool DisplayManager::shouldRender(const DisplaySnapshot &snapshot) const {
  if (firstFrame_) {
    return true;
  }

  return snapshot.ssid != lastSnapshot_.ssid ||
         snapshot.ipAddress != lastSnapshot_.ipAddress ||
         snapshot.stateLabel != lastSnapshot_.stateLabel ||
         snapshot.message != lastSnapshot_.message ||
         snapshot.transport != lastSnapshot_.transport ||
         snapshot.targetMode != lastSnapshot_.targetMode ||
         snapshot.targetChip != lastSnapshot_.targetChip ||
         snapshot.detectedChip != lastSnapshot_.detectedChip ||
         snapshot.wiringSummary != lastSnapshot_.wiringSummary ||
         snapshot.targetAddress != lastSnapshot_.targetAddress ||
         snapshot.firmwareCrc32 != lastSnapshot_.firmwareCrc32 ||
         snapshot.bytesWritten != lastSnapshot_.bytesWritten ||
         snapshot.totalBytes != lastSnapshot_.totalBytes ||
         snapshot.targetBaudRate != lastSnapshot_.targetBaudRate ||
         snapshot.automaticAvailable != lastSnapshot_.automaticAvailable ||
         snapshot.packageReady != lastSnapshot_.packageReady;
}

void DisplayManager::render(const DisplaySnapshot &snapshot) {
  uint16_t headerColor = kColorAccent;
  if (snapshot.stateLabel == "success") {
    headerColor = kColorSuccess;
  } else if (snapshot.stateLabel == "error" || snapshot.stateLabel == "cancelled") {
    headerColor = kColorError;
  } else if (snapshot.stateLabel == "erasing" || snapshot.stateLabel == "writing" || snapshot.stateLabel == "verifying") {
    headerColor = kColorWarning;
  }

  drawHeader("Exlink Flasher", headerColor);

  int progressPercent = 0;
  if (snapshot.totalBytes > 0) {
    progressPercent = static_cast<int>((snapshot.bytesWritten * 100U) / snapshot.totalBytes);
    if (progressPercent > 100) {
      progressPercent = 100;
    }
  }

  const int leftWidth = 100;
  const int rightX = 118;
  const int rightWidth = screenWidth_ - rightX - 10;

  drawLabelValue(12, 48, leftWidth, "STATE", snapshot.stateLabel.length() ? snapshot.stateLabel : "idle", headerColor);
  drawLabelValue(rightX, 48, rightWidth, "SSID", snapshot.ssid.length() ? snapshot.ssid : "AP starting", kColorText);
  drawLabelValue(12, 82, leftWidth, "IP", snapshot.ipAddress.length() ? snapshot.ipAddress : "No IP", kColorText);
  drawLabelValue(rightX, 82, rightWidth, "LINK",
                 snapshot.transport.length() ? snapshot.transport + " / " + snapshot.targetMode : snapshot.targetMode,
                 kColorText);
  drawLabelValue(12, 116, screenWidth_ - 24, "PACKAGE", summarizePackage(snapshot), kColorText);
  drawLabelValue(12, 150, screenWidth_ - 24, "DEVICE", snapshot.detectedChip.length() ? snapshot.detectedChip : "not connected", kColorText);

  tft_->fillRect(12, 184, screenWidth_ - 24, 18, kColorPanel);
  drawProgressBar(12, 184, screenWidth_ - 66, 18, progressPercent, headerColor == kColorAccent ? kColorProgress : headerColor);
  tft_->setTextColor(kColorText, kColorPanel);
  tft_->drawRightString(String(progressPercent) + "%", screenWidth_ - 12, 186, 2);

  drawLabelValue(12, 208, screenWidth_ - 24, "DETAIL", summarizeTransfer(snapshot), kColorText);
}

void DisplayManager::drawFrame() {
  tft_->fillScreen(kColorBackground);
  tft_->fillRoundRect(6, 6, screenWidth_ - 12, screenHeight_ - 12, 8, kColorHeader);
  tft_->fillRoundRect(10, 10, screenWidth_ - 20, screenHeight_ - 20, 8, kColorBackground);
  tft_->fillRoundRect(10, 10, screenWidth_ - 20, 30, 6, kColorHeader);
}

void DisplayManager::drawHeader(const String &title, uint16_t color) {
  tft_->fillRoundRect(10, 10, screenWidth_ - 20, 30, 6, color);
  tft_->setTextColor(kColorBackground, color);
  tft_->drawCentreString(title, screenWidth_ / 2, 18, 2);
}

void DisplayManager::drawLabelValue(int x, int y, int width, const String &label, const String &value, uint16_t valueColor) {
  tft_->fillRoundRect(x, y, width, 28, 4, kColorPanel);
  tft_->setTextColor(kColorMuted, kColorPanel);
  tft_->drawString(label, x + 6, y + 3, 2);
  tft_->setTextColor(valueColor, kColorPanel);
  tft_->drawString(value, x + 6, y + 14, 2);
}

void DisplayManager::drawProgressBar(int x, int y, int width, int height, int percent, uint16_t color) {
  tft_->drawRoundRect(x, y, width, height, 4, kColorMuted);
  int innerWidth = width - 4;
  int fillWidth = (innerWidth * percent) / 100;
  tft_->fillRoundRect(x + 2, y + 2, innerWidth, height - 4, 3, kColorHeader);
  if (fillWidth > 0) {
    tft_->fillRoundRect(x + 2, y + 2, fillWidth, height - 4, 3, color);
  }
}

String DisplayManager::summarizePackage(const DisplaySnapshot &snapshot) const {
  if (!snapshot.packageReady) {
    return snapshot.transport == "swd" ? "none @ SWD" : "none @ " + String(snapshot.targetBaudRate) + " baud";
  }

  String line = snapshot.targetChip.length() ? snapshot.targetChip : "-";
  line += " @ ";
  line += formatHex(snapshot.targetAddress, 8);
  return line;
}

String DisplayManager::summarizeTransfer(const DisplaySnapshot &snapshot) const {
  if (!snapshot.packageReady) {
    return "upload manifest.json + app.bin";
  }

  String line = "CRC ";
  line += formatHex(snapshot.firmwareCrc32, 8);
  line += " | ";
  line += String(snapshot.bytesWritten);
  line += "/";
  line += String(snapshot.totalBytes);
  line += " bytes";
  return line;
}

String DisplayManager::formatHex(uint32_t value, uint8_t minWidth) const {
  String hex = String(value, HEX);
  hex.toUpperCase();
  while (hex.length() < minWidth) {
    hex = "0" + hex;
  }
  return "0x" + hex;
}
