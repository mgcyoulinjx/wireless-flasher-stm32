#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "flash/flash_manager.h"

struct DisplaySnapshot {
  String stateLabel;
  String message;
  String targetChip;
  String detectedChip;
  String flashBackend;
  String selectedPackageName;
  String selectedPackageId;
  String selectedPackageChip;
  String uiMessage;
  String log;
  uint32_t selectedPackageAddress = 0;
  uint32_t selectedPackageCrc32 = 0;
  uint32_t targetAddress = 0;
  uint32_t firmwareCrc32 = 0;
  size_t selectedPackageSize = 0;
  size_t bytesWritten = 0;
  size_t totalBytes = 0;
  size_t savedPackageCount = 0;
  int selectedPackageIndex = -1;
  bool packageReady = false;
  bool flashBusy = false;
};

class DisplayManager {
public:
  using ActionCallback = void (*)(void *);

  void begin();
  void update(const DisplaySnapshot &snapshot);
  void onFlash(ActionCallback callback, void *context);
  void onNext(ActionCallback callback, void *context);
  void onPrevious(ActionCallback callback, void *context);

private:
  bool initialized_ = false;
  bool firstFrame_ = true;
  uint32_t lastTickMs_ = 0;
  DisplaySnapshot lastSnapshot_;
  String logText_;
  String lastLogEntry_;
  bool lastFlashBusy_ = false;
  uint32_t lastBatteryUpdateMs_ = 0;
  float filteredBatteryVoltage_ = 0.0f;
  float previousFilteredBatteryVoltage_ = 0.0f;
  float batteryRiseReferenceVoltage_ = 0.0f;
  uint32_t batteryRiseReferenceMs_ = 0;
  uint32_t chargeIconHoldUntilMs_ = 0;
  bool batteryFilterInitialized_ = false;
  bool chargeIconActive_ = false;

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *header_ = nullptr;
  lv_obj_t *batteryIconLabel_ = nullptr;
  lv_obj_t *batteryVoltageLabel_ = nullptr;
  lv_obj_t *chipLabel_ = nullptr;
  lv_obj_t *stateLabel_ = nullptr;
  lv_obj_t *packageInfoLabel_ = nullptr;
  lv_obj_t *progressBar_ = nullptr;
  lv_obj_t *progressLabel_ = nullptr;
  lv_obj_t *messageLabel_ = nullptr;
  lv_obj_t *prevButton_ = nullptr;
  lv_obj_t *flashButton_ = nullptr;
  lv_obj_t *flashButtonLabel_ = nullptr;
  lv_obj_t *nextButton_ = nullptr;

  ActionCallback flashCallback_ = nullptr;
  void *flashContext_ = nullptr;
  ActionCallback nextCallback_ = nullptr;
  void *nextContext_ = nullptr;
  ActionCallback previousCallback_ = nullptr;
  void *previousContext_ = nullptr;

  bool shouldUpdateWidgets(const DisplaySnapshot &snapshot) const;
  void createPage();
  void updateWidgets(const DisplaySnapshot &snapshot);
  void updateBattery();
  String packageInfoText(const DisplaySnapshot &snapshot) const;
  String chipHeaderText(const DisplaySnapshot &snapshot) const;
  String stateText(const DisplaySnapshot &snapshot) const;
  lv_color_t headerColor(const DisplaySnapshot &snapshot) const;
  String messageText(const DisplaySnapshot &snapshot) const;
  String formatHex(uint32_t value, uint8_t minWidth = 0) const;
  void invokeFlash();
  void invokeNext();
  void invokePrevious();

  static void handleFlashEvent(lv_event_t *event);
  static void handleNextEvent(lv_event_t *event);
  static void handlePreviousEvent(lv_event_t *event);
};
