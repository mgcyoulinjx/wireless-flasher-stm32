#pragma once

#include <Arduino.h>
#include <Preferences.h>

class BuzzerManager {
public:
  void begin();
  void update();
  void playPrompt();
  void playBlockingPrompt();
  void playTestMelody();
  void playSuccessMelody();
  void loadSettings(Preferences &preferences);
  void saveSettings(Preferences &preferences) const;
  void setEnabled(bool enabled);
  bool enabled() const;
  void setVolume(uint8_t volume);
  uint8_t volume() const;

private:
  struct Note {
    uint16_t frequency;
    uint16_t durationMs;
  };

  static constexpr uint8_t kMaxNotes = 12;
  Note notes_[kMaxNotes] = {};
  uint8_t noteCount_ = 0;
  uint8_t noteIndex_ = 0;
  uint32_t noteStartedMs_ = 0;
  uint8_t volume_ = 40;
  bool initialized_ = false;
  bool playing_ = false;
  bool enabled_ = true;
  bool outputOn_ = false;

  void start(const Note *notes, uint8_t count);
  void playCurrentNote();
  void stop();
  uint8_t duty() const;
};
