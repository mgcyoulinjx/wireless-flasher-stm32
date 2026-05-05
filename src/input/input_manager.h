#pragma once

#include <Arduino.h>
#include <vector>
#include "package_store.h"

class FlashManager;
class BuzzerManager;

class InputManager {
public:
  InputManager(PackageStore &packageStore, FlashManager &flashManager);

  void begin();
  void update();
  void setBuzzerManager(BuzzerManager *buzzerManager);

  String selectedPackageName() const;
  String selectedPackageId() const;
  String selectedPackageChip() const;
  uint32_t selectedPackageAddress() const;
  uint32_t selectedPackageCrc32() const;
  size_t selectedPackageSize() const;
  int selectedPackageIndex() const;
  size_t savedPackageCount() const;
  String uiMessage() const;
  bool hasSavedSelection() const;
  void selectNextPackage();
  void selectPreviousPackage();
  void flashSelectedPackage();

private:
  struct ButtonState {
    int pin = -1;
    bool stablePressed = false;
    bool lastRawPressed = false;
    uint32_t changedMs = 0;
    uint32_t pressedMs = 0;
    bool longPressSent = false;
  };

  PackageStore &packageStore_;
  FlashManager &flashManager_;
  BuzzerManager *buzzerManager_ = nullptr;
  std::vector<SavedPackageInfo> packages_;
  int selectedIndex_ = -1;
  String uiMessage_;
  uint32_t lastRefreshMs_ = 0;
  uint32_t lastSeenPackagesVersion_ = 0;
  ButtonState leftButton_;
  ButtonState rightButton_;
  ButtonState pushButton_;

  void refreshPackages(bool preserveSelection = true);
  void selectNext();
  void selectPrevious();
  void confirmFlash();
  void cancelOrRefresh();
  void beginButton(ButtonState &button, int pin);
  bool updateButton(ButtonState &button, bool &longPress);
  void handleKeys();
  void setMessage(const String &message);
  void playPrompt();
};
