#include "buzzer_manager.h"

#include "app_config.h"

namespace {
constexpr const char *kBuzzerEnabledKey = "buzzerEnabled";
constexpr const char *kBuzzerVolumeKey = "buzzerVolume";
}

void BuzzerManager::begin() {
  if (initialized_) {
    return;
  }

  pinMode(AppConfig::kBuzzerPin, OUTPUT);
  ledcAttachPin(AppConfig::kBuzzerPin, AppConfig::kBuzzerLedcChannel);
  ledcSetup(AppConfig::kBuzzerLedcChannel, 1000, 8);
  ledcWrite(AppConfig::kBuzzerLedcChannel, 0);
  initialized_ = true;
}

void BuzzerManager::update() {
  if (!playing_) {
    return;
  }

  if (millis() - noteStartedMs_ < notes_[noteIndex_].durationMs) {
    return;
  }

  ++noteIndex_;
  if (noteIndex_ >= noteCount_) {
    stop();
    return;
  }

  playCurrentNote();
}

void BuzzerManager::playPrompt() {
  static constexpr Note prompt[] = {{1000, 50}};
  start(prompt, sizeof(prompt) / sizeof(prompt[0]));
}

void BuzzerManager::playBlockingPrompt() {
  if (!enabled_ || volume_ == 0) {
    stop();
    return;
  }
  if (!initialized_) {
    begin();
  }

  playing_ = false;
  ledcSetup(AppConfig::kBuzzerLedcChannel, 1000, 8);
  ledcWrite(AppConfig::kBuzzerLedcChannel, duty());
  delay(50);
  ledcWrite(AppConfig::kBuzzerLedcChannel, 0);
}

void BuzzerManager::playTestMelody() {
  static constexpr Note melody[] = {
      {494, 105},
      {0, 13},
      {659, 105},
      {0, 13},
      {494, 105},
      {0, 13},
      {740, 105},
      {0, 421},
      {659, 105},
      {988, 421},
      {988, 53},
  };
  start(melody, sizeof(melody) / sizeof(melody[0]));
}

void BuzzerManager::playSuccessMelody() {
  static constexpr Note melody[] = {
      {523, 90},
      {0, 20},
      {659, 90},
      {0, 20},
      {784, 140},
      {0, 30},
      {1047, 220},
  };
  start(melody, sizeof(melody) / sizeof(melody[0]));
}

void BuzzerManager::loadSettings(Preferences &preferences) {
  enabled_ = preferences.getBool(kBuzzerEnabledKey, enabled_);
  setVolume(preferences.getUChar(kBuzzerVolumeKey, volume_));
}

void BuzzerManager::saveSettings(Preferences &preferences) const {
  preferences.putBool(kBuzzerEnabledKey, enabled_);
  preferences.putUChar(kBuzzerVolumeKey, volume_);
}

void BuzzerManager::setEnabled(bool enabled) {
  enabled_ = enabled;
  if (!enabled_) {
    stop();
  }
}

bool BuzzerManager::enabled() const {
  return enabled_;
}

void BuzzerManager::setVolume(uint8_t volume) {
  volume_ = min<uint8_t>(volume, 100);
}

uint8_t BuzzerManager::volume() const {
  return volume_;
}

void BuzzerManager::start(const Note *notes, uint8_t count) {
  if (!enabled_ || volume_ == 0) {
    stop();
    return;
  }
  if (!initialized_) {
    begin();
  }

  noteCount_ = min<uint8_t>(count, kMaxNotes);
  for (uint8_t i = 0; i < noteCount_; ++i) {
    notes_[i] = notes[i];
  }
  noteIndex_ = 0;
  playing_ = noteCount_ > 0;
  if (playing_) {
    playCurrentNote();
  }
}

void BuzzerManager::playCurrentNote() {
  const Note &note = notes_[noteIndex_];
  if (enabled_ && note.frequency > 0 && volume_ > 0) {
    ledcSetup(AppConfig::kBuzzerLedcChannel, note.frequency, 8);
    ledcWrite(AppConfig::kBuzzerLedcChannel, duty());
  } else {
    ledcWrite(AppConfig::kBuzzerLedcChannel, 0);
  }
  noteStartedMs_ = millis();
}

void BuzzerManager::stop() {
  ledcWrite(AppConfig::kBuzzerLedcChannel, 0);
  playing_ = false;
  outputOn_ = false;
}

uint8_t BuzzerManager::duty() const {
  if (volume_ == 0) {
    return 0;
  }
  return static_cast<uint8_t>(1 + ((static_cast<uint16_t>(volume_) - 1) * 3) / 99);
}
