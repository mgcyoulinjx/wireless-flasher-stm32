#include "input_manager.h"

#include "app_config.h"
#include "buzzer_manager.h"
#include "flash_manager.h"

namespace {
constexpr uint32_t kRefreshIntervalMs = 30000;
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kLongPressMs = 900;

int findPackageIndex(const std::vector<SavedPackageInfo> &packages, const String &id) {
  if (id.isEmpty()) {
    return -1;
  }
  for (size_t index = 0; index < packages.size(); ++index) {
    if (packages[index].id == id) {
      return static_cast<int>(index);
    }
  }
  return -1;
}
}

InputManager::InputManager(PackageStore &packageStore, FlashManager &flashManager)
    : packageStore_(packageStore), flashManager_(flashManager) {}

void InputManager::begin() {
  beginButton(leftButton_, AppConfig::kKeyLeftPin);
  beginButton(rightButton_, AppConfig::kKeyRightPin);
  beginButton(pushButton_, AppConfig::kKeyPushPin);
  refreshPackages(false);
}

void InputManager::update() {
  const uint32_t version = packageStore_.savedPackagesVersion();
  if (version != lastSeenPackagesVersion_ || millis() - lastRefreshMs_ > kRefreshIntervalMs) {
    refreshPackages(true);
  }
  handleKeys();
}

void InputManager::setBuzzerManager(BuzzerManager *buzzerManager) {
  buzzerManager_ = buzzerManager;
}

String InputManager::selectedPackageName() const {
  if (!hasSavedSelection()) {
    return "";
  }
  return packages_[selectedIndex_].name;
}

String InputManager::selectedPackageId() const {
  if (!hasSavedSelection()) {
    return "";
  }
  return packages_[selectedIndex_].id;
}

String InputManager::selectedPackageChip() const {
  if (!hasSavedSelection()) {
    return "";
  }
  return packages_[selectedIndex_].chip;
}

uint32_t InputManager::selectedPackageAddress() const {
  if (!hasSavedSelection()) {
    return 0;
  }
  return packages_[selectedIndex_].address;
}

uint32_t InputManager::selectedPackageCrc32() const {
  if (!hasSavedSelection()) {
    return 0;
  }
  return packages_[selectedIndex_].crc32;
}

size_t InputManager::selectedPackageSize() const {
  if (!hasSavedSelection()) {
    return 0;
  }
  return packages_[selectedIndex_].size;
}

int InputManager::selectedPackageIndex() const {
  return selectedIndex_;
}

size_t InputManager::savedPackageCount() const {
  return packages_.size();
}

String InputManager::uiMessage() const {
  return uiMessage_;
}

bool InputManager::hasSavedSelection() const {
  return selectedIndex_ >= 0 && static_cast<size_t>(selectedIndex_) < packages_.size();
}

void InputManager::selectNextPackage() {
  selectNext();
}

void InputManager::selectPreviousPackage() {
  selectPrevious();
}

void InputManager::flashSelectedPackage() {
  confirmFlash();
}

void InputManager::cancelOrRefreshPackages() {
  cancelOrRefresh();
}

void InputManager::refreshPackages(bool preserveSelection) {
  const String previousId = preserveSelection ? selectedPackageId() : "";
  String error;
  std::vector<SavedPackageInfo> packages;
  if (!packageStore_.listSavedPackages(packages, error)) {
    setMessage(error);
    lastRefreshMs_ = millis();
    return;
  }

  packages_ = packages;
  selectedIndex_ = -1;

  const String selectedId = packageStore_.selectedSavedPackageId(error);
  if (!error.isEmpty()) {
    setMessage(error);
  }
  if (!selectedId.isEmpty()) {
    selectedIndex_ = findPackageIndex(packages_, selectedId);
  }
  if (selectedIndex_ < 0 && preserveSelection && !selectedId.isEmpty()) {
    selectedIndex_ = findPackageIndex(packages_, previousId);
  }

  lastSeenPackagesVersion_ = packageStore_.savedPackagesVersion();
  lastRefreshMs_ = millis();
}

void InputManager::selectNext() {
  if (flashManager_.isBusy()) {
    setMessage("Flash job is busy");
    return;
  }
  if (packages_.empty()) {
    setMessage("No saved firmware");
    return;
  }
  const int nextIndex = (selectedIndex_ + 1) % static_cast<int>(packages_.size());
  String error;
  if (!packageStore_.selectSavedPackage(packages_[nextIndex].id, error)) {
    setMessage(error);
    return;
  }
  selectedIndex_ = nextIndex;
  setMessage("Selected " + packages_[selectedIndex_].name);
}

void InputManager::selectPrevious() {
  if (flashManager_.isBusy()) {
    setMessage("Flash job is busy");
    return;
  }
  if (packages_.empty()) {
    setMessage("No saved firmware");
    return;
  }
  const int previousIndex = selectedIndex_ <= 0 ? static_cast<int>(packages_.size()) - 1 : selectedIndex_ - 1;
  String error;
  if (!packageStore_.selectSavedPackage(packages_[previousIndex].id, error)) {
    setMessage(error);
    return;
  }
  selectedIndex_ = previousIndex;
  setMessage("Selected " + packages_[selectedIndex_].name);
}

void InputManager::confirmFlash() {
  if (flashManager_.isBusy()) {
    setMessage("Flash job is busy");
    return;
  }

  String error;
  if (hasSavedSelection()) {
    if (!packageStore_.restoreSavedPackage(packages_[selectedIndex_].id, error)) {
      setMessage(error);
      refreshPackages(true);
      return;
    }
    if (!flashManager_.setPackageReady(error)) {
      setMessage(error);
      return;
    }
  } else if (!flashManager_.status().packageReady) {
    setMessage("No firmware selected");
    refreshPackages(true);
    return;
  }

  if (!flashManager_.startFlash(error)) {
    setMessage(error);
    return;
  }
  setMessage(hasSavedSelection() ? "Flashing " + packages_[selectedIndex_].name : "Flashing current firmware");
}

void InputManager::cancelOrRefresh() {
  if (flashManager_.isBusy()) {
    flashManager_.cancel();
    setMessage("Cancel requested");
    return;
  }
  refreshPackages(true);
  setMessage("Firmware list refreshed");
}

void InputManager::beginButton(ButtonState &button, int pin) {
  button.pin = pin;
  button.stablePressed = false;
  button.lastRawPressed = false;
  button.changedMs = millis();
  button.pressedMs = 0;
  button.longPressSent = false;
  if (pin >= 0) {
    pinMode(pin, INPUT_PULLUP);
  }
}

bool InputManager::updateButton(ButtonState &button, bool &longPress) {
  longPress = false;
  if (button.pin < 0) {
    return false;
  }

  const uint32_t now = millis();
  const bool rawPressed = digitalRead(button.pin) == LOW;
  if (rawPressed != button.lastRawPressed) {
    button.lastRawPressed = rawPressed;
    button.changedMs = now;
  }

  bool released = false;
  if (now - button.changedMs >= kDebounceMs && rawPressed != button.stablePressed) {
    button.stablePressed = rawPressed;
    if (button.stablePressed) {
      button.pressedMs = now;
      button.longPressSent = false;
    } else {
      released = !button.longPressSent;
    }
  }

  if (button.stablePressed && !button.longPressSent && now - button.pressedMs >= kLongPressMs) {
    button.longPressSent = true;
    longPress = true;
  }

  return released;
}

void InputManager::handleKeys() {
  bool longPress = false;
  if (updateButton(leftButton_, longPress)) {
    selectPrevious();
    playPrompt();
  }
  if (longPress) {
    playPrompt();
    cancelOrRefresh();
  }

  if (updateButton(rightButton_, longPress)) {
    selectNext();
    playPrompt();
  }
  if (longPress) {
    playPrompt();
    cancelOrRefresh();
  }

  if (updateButton(pushButton_, longPress)) {
    confirmFlash();
    playPrompt();
  }
  if (longPress) {
    playPrompt();
    cancelOrRefresh();
  }
}

void InputManager::setMessage(const String &message) {
  uiMessage_ = message;
}

void InputManager::playPrompt() {
  if (buzzerManager_) {
    buzzerManager_->playPrompt();
  }
}

void InputManager::playBlockingPrompt() {
  if (buzzerManager_) {
    buzzerManager_->playBlockingPrompt();
  }
}
