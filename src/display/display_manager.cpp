#include "display_manager.h"

#include <CST816T.h>
#include <TFT_eSPI.h>
#include "app_config.h"

LV_FONT_DECLARE(lv_font_harmonyos_sans_sc_16_gb2312);

namespace {
constexpr int kBufferLines = 40;
constexpr int kBatteryAdcPin = 2;
constexpr uint32_t kBatteryUpdateIntervalMs = 1000;
constexpr int kMaxLogLines = 36;
constexpr float kBatteryMinVoltage = 3.30f;
constexpr float kBatteryMaxVoltage = 4.20f;
constexpr float kBatteryDividerScale = 6.6f;
constexpr float kChargeInstantRiseVoltage = 0.010f;
constexpr float kChargeCumulativeRiseVoltage = 0.08f;
constexpr float kChargeReferenceDropResetVoltage = 0.01f;
constexpr float kChargeFullVoltage = 4.12f;
constexpr float kChargeClearVoltage = 4.05f;
constexpr uint32_t kChargeInstantHoldMs = 8000;
constexpr uint32_t kChargeCumulativeHoldMs = 12000;
constexpr uint32_t kChargeReferenceWindowMs = 5000;

TFT_eSPI tft;
CST816T touch(AppConfig::kTouchSdaPin, AppConfig::kTouchSclPin, AppConfig::kTouchResetPin, AppConfig::kTouchInterruptPin);
lv_disp_draw_buf_t drawBuf;
lv_color_t drawBuffer[AppConfig::kDisplayWidth * kBufferLines];

void flushDisplay(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  const uint32_t width = area->x2 - area->x1 + 1;
  const uint32_t height = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, width, height);
  tft.pushColors(reinterpret_cast<uint16_t *>(&colorP->full), width * height, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

uint16_t clampTouch(uint16_t value, uint16_t maxValue) {
  return value >= maxValue ? maxValue - 1 : value;
}

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
  TouchInfos info = touch.GetTouchInfo();
  if (!info.isValid || !info.touching) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  const uint16_t rawX = clampTouch(info.x, TouchWidth);
  const uint16_t rawY = clampTouch(info.y, TouchHeight);
  const int16_t rotatedX = rawY;
  const int16_t rotatedY = (TouchWidth - 1) - rawX;

  data->state = LV_INDEV_STATE_PR;
  data->point.x = constrain(rotatedX, 0, tft.width() - 1);
  data->point.y = constrain(rotatedY, 0, tft.height() - 1);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  return label;
}

String trimLogLines(const String &text) {
  int start = text.length();
  int lines = 0;
  while (start > 0 && lines < kMaxLogLines) {
    --start;
    if (text[start] == '\n') {
      ++lines;
    }
  }
  if (start > 0) {
    ++start;
  }
  return text.substring(start);
}

lv_obj_t *makeCard(lv_obj_t *parent, int16_t x, int16_t y, int16_t width, int16_t height) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, width, height);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x111827), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x334155), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

}

void DisplayManager::begin() {
  if (initialized_) {
    return;
  }

  tft.init();
  tft.setRotation(3);
  touch.begin();
  analogReadResolution(12);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, drawBuffer, nullptr, AppConfig::kDisplayWidth * kBufferLines);

  static lv_disp_drv_t displayDriver;
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = tft.width();
  displayDriver.ver_res = tft.height();
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuf;
  lv_disp_drv_register(&displayDriver);

  static lv_indev_drv_t inputDriver;
  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readTouch;
  lv_indev_drv_register(&inputDriver);

  createPage();
  lastTickMs_ = millis();
  initialized_ = true;
}

void DisplayManager::update(const DisplaySnapshot &snapshot) {
  if (!initialized_) {
    return;
  }

  const uint32_t now = millis();
  const uint32_t elapsed = now - lastTickMs_;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    lastTickMs_ = now;
  }

  updateBattery();

  if (firstFrame_ || shouldUpdateWidgets(snapshot)) {
    updateWidgets(snapshot);
    lastSnapshot_ = snapshot;
    firstFrame_ = false;
  }

  lv_timer_handler();
}

void DisplayManager::onFlash(ActionCallback callback, void *context) {
  flashCallback_ = callback;
  flashContext_ = context;
}

void DisplayManager::onNext(ActionCallback callback, void *context) {
  nextCallback_ = callback;
  nextContext_ = context;
}

void DisplayManager::onPrevious(ActionCallback callback, void *context) {
  previousCallback_ = callback;
  previousContext_ = context;
}

bool DisplayManager::shouldUpdateWidgets(const DisplaySnapshot &snapshot) const {
  return snapshot.stateLabel != lastSnapshot_.stateLabel || snapshot.message != lastSnapshot_.message ||
         snapshot.targetChip != lastSnapshot_.targetChip || snapshot.detectedChip != lastSnapshot_.detectedChip ||
         snapshot.flashBackend != lastSnapshot_.flashBackend ||
         snapshot.selectedPackageName != lastSnapshot_.selectedPackageName ||
         snapshot.selectedPackageId != lastSnapshot_.selectedPackageId ||
         snapshot.selectedPackageChip != lastSnapshot_.selectedPackageChip ||
         snapshot.uiMessage != lastSnapshot_.uiMessage || snapshot.log != lastSnapshot_.log ||
         snapshot.selectedPackageAddress != lastSnapshot_.selectedPackageAddress ||
         snapshot.selectedPackageCrc32 != lastSnapshot_.selectedPackageCrc32 ||
         snapshot.targetAddress != lastSnapshot_.targetAddress || snapshot.firmwareCrc32 != lastSnapshot_.firmwareCrc32 ||
         snapshot.selectedPackageSize != lastSnapshot_.selectedPackageSize ||
         snapshot.bytesWritten != lastSnapshot_.bytesWritten || snapshot.totalBytes != lastSnapshot_.totalBytes ||
         snapshot.savedPackageCount != lastSnapshot_.savedPackageCount ||
         snapshot.selectedPackageIndex != lastSnapshot_.selectedPackageIndex ||
         snapshot.packageReady != lastSnapshot_.packageReady || snapshot.flashBusy != lastSnapshot_.flashBusy;
}

void DisplayManager::createPage() {
  const int16_t width = tft.width();
  const int16_t height = tft.height();
  const int16_t margin = 6;
  const int16_t contentWidth = width - margin * 2;
  const int16_t buttonHeight = 34;
  const int16_t buttonY = 42;
  const int16_t progressLabelWidth = 44;
  const int16_t progressGap = 8;
  const int16_t progressBarWidth = contentWidth - 92;
  const int16_t progressGroupWidth = progressBarWidth + progressGap + progressLabelWidth;
  const int16_t progressX = (width - progressGroupWidth) / 2 + 4;
  const int16_t progressY = height - margin - 16;
  const int16_t logY = 148;
  const int16_t logHeight = progressY - logY - 8;

  screen_ = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x020617), 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_scr_load(screen_);

  header_ = lv_obj_create(screen_);
  lv_obj_set_pos(header_, margin, margin);
  lv_obj_set_size(header_, contentWidth, 30);
  lv_obj_set_style_radius(header_, 8, 0);
  lv_obj_set_style_bg_color(header_, lv_color_hex(0x06B6D4), 0);
  lv_obj_set_style_border_width(header_, 0, 0);
  lv_obj_clear_flag(header_, LV_OBJ_FLAG_SCROLLABLE);

  batteryIconLabel_ = makeLabel(header_, LV_SYMBOL_BATTERY_EMPTY, &lv_font_montserrat_14, lv_color_hex(0x020617));
  lv_obj_align(batteryIconLabel_, LV_ALIGN_LEFT_MID, 3, 0);

  batteryVoltageLabel_ = makeLabel(header_, "0.00V", &lv_font_montserrat_14, lv_color_hex(0x020617));
  lv_obj_align_to(batteryVoltageLabel_, batteryIconLabel_, LV_ALIGN_OUT_RIGHT_MID, 2, 0);

  chipLabel_ = makeLabel(header_, "未连接", &lv_font_harmonyos_sans_sc_16_gb2312, lv_color_hex(0x020617));
  lv_obj_set_width(chipLabel_, 104);
  lv_label_set_long_mode(chipLabel_, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(chipLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(chipLabel_, LV_ALIGN_CENTER, 0, 0);

  stateLabel_ = makeLabel(header_, "空闲", &lv_font_harmonyos_sans_sc_16_gb2312, lv_color_hex(0x020617));
  lv_obj_set_width(stateLabel_, 42);
  lv_obj_set_style_text_align(stateLabel_, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(stateLabel_, LV_ALIGN_RIGHT_MID, -2, 0);

  lv_obj_t *packageCard = makeCard(screen_, margin, 84, contentWidth, 58);
  packageInfoLabel_ = makeLabel(packageCard, "无已保存固件", &lv_font_harmonyos_sans_sc_16_gb2312, lv_color_hex(0xF8FAFC));
  lv_obj_set_width(packageInfoLabel_, contentWidth - 16);
  lv_label_set_long_mode(packageInfoLabel_, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(packageInfoLabel_, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(packageInfoLabel_, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *logCard = makeCard(screen_, margin, logY, contentWidth, logHeight);
  lv_obj_add_flag(logCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(logCard, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(logCard, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_pad_bottom(logCard, 8, 0);
  messageLabel_ = makeLabel(logCard, "日志就绪\n请选择固件后点击烧录", &lv_font_harmonyos_sans_sc_16_gb2312, lv_color_hex(0xCBD5E1));
  lv_obj_set_width(messageLabel_, contentWidth - 20);
  lv_label_set_long_mode(messageLabel_, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(messageLabel_, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(messageLabel_, LV_ALIGN_TOP_LEFT, 4, 4);

  progressBar_ = lv_bar_create(screen_);
  lv_obj_set_pos(progressBar_, progressX, progressY);
  lv_obj_set_size(progressBar_, progressBarWidth, 16);
  lv_bar_set_range(progressBar_, 0, 100);
  lv_obj_set_style_radius(progressBar_, 6, LV_PART_MAIN);
  lv_obj_set_style_border_width(progressBar_, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(progressBar_, lv_color_hex(0x64748B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(progressBar_, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(progressBar_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(progressBar_, lv_color_hex(0x2563EB), LV_PART_INDICATOR);

  progressLabel_ = makeLabel(screen_, "0%", &lv_font_montserrat_14, lv_color_hex(0xF8FAFC));
  lv_obj_set_width(progressLabel_, progressLabelWidth);
  lv_obj_align_to(progressLabel_, progressBar_, LV_ALIGN_OUT_RIGHT_MID, progressGap, 0);

  const int16_t gap = 6;
  const int16_t sideButtonWidth = 46;
  const int16_t flashButtonWidth = contentWidth - sideButtonWidth * 2 - gap * 2;
  prevButton_ = lv_btn_create(screen_);
  lv_obj_set_pos(prevButton_, margin, buttonY);
  lv_obj_set_size(prevButton_, sideButtonWidth, buttonHeight);
  lv_obj_add_event_cb(prevButton_, handlePreviousEvent, LV_EVENT_CLICKED, this);
  lv_obj_t *prevLabel = makeLabel(prevButton_, "<", &lv_font_montserrat_18, lv_color_hex(0xFFFFFF));
  lv_obj_center(prevLabel);

  flashButton_ = lv_btn_create(screen_);
  lv_obj_set_pos(flashButton_, margin + sideButtonWidth + gap, buttonY);
  lv_obj_set_size(flashButton_, flashButtonWidth, buttonHeight);
  lv_obj_set_style_bg_color(flashButton_, lv_color_hex(0x2563EB), 0);
  lv_obj_add_event_cb(flashButton_, handleFlashEvent, LV_EVENT_CLICKED, this);
  flashButtonLabel_ = makeLabel(flashButton_, "烧录", &lv_font_harmonyos_sans_sc_16_gb2312, lv_color_hex(0xFFFFFF));
  lv_obj_center(flashButtonLabel_);

  nextButton_ = lv_btn_create(screen_);
  lv_obj_set_pos(nextButton_, margin + sideButtonWidth + gap + flashButtonWidth + gap, buttonY);
  lv_obj_set_size(nextButton_, sideButtonWidth, buttonHeight);
  lv_obj_add_event_cb(nextButton_, handleNextEvent, LV_EVENT_CLICKED, this);
  lv_obj_t *nextLabel = makeLabel(nextButton_, ">", &lv_font_montserrat_18, lv_color_hex(0xFFFFFF));
  lv_obj_center(nextLabel);
}

void DisplayManager::updateBattery() {
  const uint32_t now = millis();
  if (batteryFilterInitialized_ && now - lastBatteryUpdateMs_ < kBatteryUpdateIntervalMs) {
    return;
  }
  lastBatteryUpdateMs_ = now;

  const float voltage = (static_cast<float>(analogRead(kBatteryAdcPin)) / 4095.0f) * kBatteryDividerScale;
  if (!batteryFilterInitialized_) {
    filteredBatteryVoltage_ = voltage;
    previousFilteredBatteryVoltage_ = voltage;
    batteryRiseReferenceVoltage_ = voltage;
    batteryRiseReferenceMs_ = now;
    chargeIconHoldUntilMs_ = now;
    chargeIconActive_ = false;
    batteryFilterInitialized_ = true;
  } else {
    filteredBatteryVoltage_ = filteredBatteryVoltage_ * 0.85f + voltage * 0.15f;
  }

  const float riseDelta = filteredBatteryVoltage_ - previousFilteredBatteryVoltage_;
  previousFilteredBatteryVoltage_ = filteredBatteryVoltage_;

  if (riseDelta >= kChargeInstantRiseVoltage) {
    chargeIconActive_ = true;
    chargeIconHoldUntilMs_ = now + kChargeInstantHoldMs;
  }

  if (filteredBatteryVoltage_ < batteryRiseReferenceVoltage_ - kChargeReferenceDropResetVoltage) {
    batteryRiseReferenceVoltage_ = filteredBatteryVoltage_;
    batteryRiseReferenceMs_ = now;
  }

  if (static_cast<int32_t>(now - batteryRiseReferenceMs_) > static_cast<int32_t>(kChargeReferenceWindowMs)) {
    batteryRiseReferenceVoltage_ = filteredBatteryVoltage_;
    batteryRiseReferenceMs_ = now;
  }

  if ((filteredBatteryVoltage_ - batteryRiseReferenceVoltage_) >= kChargeCumulativeRiseVoltage) {
    chargeIconActive_ = true;
    chargeIconHoldUntilMs_ = now + kChargeCumulativeHoldMs;
    batteryRiseReferenceVoltage_ = filteredBatteryVoltage_;
    batteryRiseReferenceMs_ = now;
  }

  if (filteredBatteryVoltage_ >= kChargeFullVoltage) {
    chargeIconActive_ = true;
  }

  if (chargeIconActive_ && static_cast<int32_t>(now - chargeIconHoldUntilMs_) >= 0 && filteredBatteryVoltage_ <= kChargeClearVoltage) {
    chargeIconActive_ = false;
  }

  float levelVoltage = filteredBatteryVoltage_;
  if (levelVoltage < kBatteryMinVoltage) {
    levelVoltage = kBatteryMinVoltage;
  }
  if (levelVoltage > kBatteryMaxVoltage) {
    levelVoltage = kBatteryMaxVoltage;
  }

  const int level = static_cast<int>(((levelVoltage - kBatteryMinVoltage) / (kBatteryMaxVoltage - kBatteryMinVoltage)) * 10.0f + 0.5f);
  const char *symbol = LV_SYMBOL_BATTERY_EMPTY;
  if (level >= 8) {
    symbol = LV_SYMBOL_BATTERY_FULL;
  } else if (level >= 5) {
    symbol = LV_SYMBOL_BATTERY_3;
  } else if (level >= 3) {
    symbol = LV_SYMBOL_BATTERY_2;
  } else if (level >= 1) {
    symbol = LV_SYMBOL_BATTERY_1;
  }

  lv_label_set_text(batteryIconLabel_, chargeIconActive_ ? LV_SYMBOL_CHARGE : symbol);
  lv_label_set_text(batteryVoltageLabel_, (String(filteredBatteryVoltage_, 2) + "V").c_str());
}

void DisplayManager::updateWidgets(const DisplaySnapshot &snapshot) {
  const int progress = snapshot.totalBytes > 0 ? min(100, static_cast<int>((snapshot.bytesWritten * 100U) / snapshot.totalBytes)) : 0;
  lv_obj_set_style_bg_color(header_, headerColor(snapshot), 0);
  lv_label_set_text(stateLabel_, stateText(snapshot).c_str());
  lv_label_set_text(chipLabel_, chipHeaderText(snapshot).c_str());
  lv_label_set_text(packageInfoLabel_, packageInfoText(snapshot).c_str());

  lv_bar_set_value(progressBar_, progress, LV_ANIM_OFF);
  lv_label_set_text(progressLabel_, (String(progress) + "%").c_str());
  if (snapshot.flashBusy && !lastFlashBusy_) {
    logText_ = "";
    lastLogEntry_ = "";
  }
  lastFlashBusy_ = snapshot.flashBusy;

  const String logEntry = trimLogLines(messageText(snapshot));
  if (logEntry != lastLogEntry_) {
    logText_ = logEntry;
    lastLogEntry_ = logEntry;
    lv_label_set_text(messageLabel_, logText_.c_str());
    lv_obj_t *logCard = lv_obj_get_parent(messageLabel_);
    lv_obj_update_layout(logCard);
    lv_obj_scroll_to_y(logCard, LV_COORD_MAX, LV_ANIM_OFF);
  }

  const bool canFlash = !snapshot.flashBusy && (snapshot.packageReady || snapshot.selectedPackageIndex >= 0);
  if (snapshot.flashBusy) {
    lv_label_set_text(flashButtonLabel_, "忙碌");
  } else {
    lv_label_set_text(flashButtonLabel_, "烧录");
  }
  lv_obj_add_state(prevButton_, snapshot.flashBusy ? LV_STATE_DISABLED : 0);
  lv_obj_add_state(nextButton_, snapshot.flashBusy ? LV_STATE_DISABLED : 0);
  if (canFlash) {
    lv_obj_clear_state(flashButton_, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(flashButton_, LV_STATE_DISABLED);
  }
  if (!snapshot.flashBusy) {
    lv_obj_clear_state(prevButton_, LV_STATE_DISABLED);
    lv_obj_clear_state(nextButton_, LV_STATE_DISABLED);
  }
}

String DisplayManager::packageInfoText(const DisplaySnapshot &snapshot) const {
  if (snapshot.selectedPackageIndex < 0 && !snapshot.packageReady) {
    return "名称: 无\n序号: 0/0";
  }

  const String name = snapshot.selectedPackageName.length() ? snapshot.selectedPackageName : "当前固件";
  const String chip = snapshot.selectedPackageChip.length() ? snapshot.selectedPackageChip :
                                                       (snapshot.targetChip.length() ? snapshot.targetChip : "STM32");
  const uint32_t address = snapshot.selectedPackageAddress ? snapshot.selectedPackageAddress : snapshot.targetAddress;
  const uint32_t crc = snapshot.selectedPackageCrc32 ? snapshot.selectedPackageCrc32 : snapshot.firmwareCrc32;
  const size_t size = snapshot.selectedPackageSize ? snapshot.selectedPackageSize : snapshot.totalBytes;

  String text = "名称: ";
  text += name;
  text += "\n";
  if (snapshot.savedPackageCount > 0 && snapshot.selectedPackageIndex >= 0) {
    text += "#" + String(snapshot.selectedPackageIndex + 1) + "/" + String(snapshot.savedPackageCount);
  } else {
    text += "当前";
  }
  text += "  ";
  text += chip;
  text += "  ";
  text += String(size);
  text += "B";
  if (snapshot.flashBackend.length()) {
    text += "  ";
    text += snapshot.flashBackend;
  }
  text += "\n";
  text += formatHex(address, 8);
  text += "  CRC ";
  text += formatHex(crc, 8);
  return text;
}

String DisplayManager::chipHeaderText(const DisplaySnapshot &snapshot) const {
  if (!snapshot.detectedChip.length()) {
    return "未连接";
  }
  const int paren = snapshot.detectedChip.indexOf('(');
  String name = paren > 0 ? snapshot.detectedChip.substring(0, paren) : snapshot.detectedChip;
  name.trim();
  name.replace("medium-density", "MD");
  name.replace("low-density", "LD");
  name.replace("high-density", "HD");
  name.replace("connectivity line", "CL");
  name.replace("value line", "VL");
  name.replace("small", "SM");
  return name;
}

String DisplayManager::stateText(const DisplaySnapshot &snapshot) const {
  String state = snapshot.stateLabel.length() ? snapshot.stateLabel : "idle";
  if (state == "idle") {
    return "空闲";
  }
  if (state == "upload_ready") {
    return "就绪";
  }
  if (state == "preparing_target") {
    return "准备";
  }
  if (state == "connecting_swd") {
    return "连接";
  }
  if (state == "halting_target") {
    return "暂停";
  }
  if (state == "erasing") {
    return "擦除";
  }
  if (state == "writing") {
    return "写入";
  }
  if (state == "verifying") {
    return "校验";
  }
  if (state == "success") {
    return "成功";
  }
  if (state == "error") {
    return "错误";
  }
  if (state == "cancelled") {
    return "取消";
  }
  return state;
}

lv_color_t DisplayManager::headerColor(const DisplaySnapshot &snapshot) const {
  const String state = snapshot.stateLabel.length() ? snapshot.stateLabel : "idle";
  if (state == "preparing_target" || state == "connecting_swd" || state == "halting_target" || state == "erasing") {
    return lv_color_hex(0x2563EB);
  }
  if (state == "writing" || state == "verifying") {
    return lv_color_hex(0xFACC15);
  }
  if (state == "success") {
    return lv_color_hex(0x22C55E);
  }
  if (state == "error" || state == "cancelled") {
    return lv_color_hex(0xEF4444);
  }
  return lv_color_hex(0x06B6D4);
}

String DisplayManager::messageText(const DisplaySnapshot &snapshot) const {
  String text;
  if (snapshot.log.length()) {
    text = snapshot.log;
  } else if (snapshot.detectedChip.length()) {
    text = snapshot.message + "\n" + snapshot.detectedChip;
  } else {
    text = snapshot.message;
  }

  text.replace("Ready", "就绪");
  text.replace("Flash job started", "烧录任务已启动");
  text.replace("Saved firmware selected and flash job started", "已选择保存固件并启动烧录");
  text.replace("Preparing target", "正在准备目标芯片");
  text.replace("Connecting SWD", "正在连接 SWD");
  text.replace("Halting target", "正在暂停目标芯片");
  text.replace("Erasing target", "正在擦除目标芯片");
  text.replace("Writing firmware", "正在写入固件");
  text.replace("Verifying firmware", "正在校验固件");
  text.replace("Flash complete", "烧录完成");
  text.replace("Flash cancelled", "烧录已取消");
  text.replace("Flash job is busy", "烧录任务忙碌");
  text.replace("No firmware selected", "未选择固件");
  text.replace("No saved firmware", "无已保存固件");
  text.replace("Selected ", "已选择 ");
  text.replace("Flashing current firmware", "正在烧录当前固件");
  text.replace("Flashing ", "正在烧录 ");
  text.replace("SWD: connect took", "SWD: 连接耗时");
  text.replace("SWD: mass erase took", "SWD: 擦除耗时");
  text.replace("SWD: programmed", "SWD: 已写入");
  text.replace("SWD: programming complete, took", "SWD: 写入完成，耗时");
  text.replace("SWD: verify took", "SWD: 校验耗时");
  text.replace("SWD: reset complete, total", "SWD: 复位完成，总耗时");
  text.replace("bytes", "字节");
  text.replace("ms", "毫秒");
  return text;
}

String DisplayManager::formatHex(uint32_t value, uint8_t minWidth) const {
  String hex = String(value, HEX);
  hex.toUpperCase();
  while (hex.length() < minWidth) {
    hex = "0" + hex;
  }
  return "0x" + hex;
}

void DisplayManager::invokeFlash() {
  if (flashCallback_) {
    flashCallback_(flashContext_);
  }
}

void DisplayManager::invokeNext() {
  if (nextCallback_) {
    nextCallback_(nextContext_);
  }
}

void DisplayManager::invokePrevious() {
  if (previousCallback_) {
    previousCallback_(previousContext_);
  }
}

void DisplayManager::handleFlashEvent(lv_event_t *event) {
  static_cast<DisplayManager *>(lv_event_get_user_data(event))->invokeFlash();
}

void DisplayManager::handleNextEvent(lv_event_t *event) {
  static_cast<DisplayManager *>(lv_event_get_user_data(event))->invokeNext();
}

void DisplayManager::handlePreviousEvent(lv_event_t *event) {
  static_cast<DisplayManager *>(lv_event_get_user_data(event))->invokePrevious();
}
