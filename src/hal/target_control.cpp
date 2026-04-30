#include "target_control.h"

TargetControl::TargetControl(HardwareSerial &serialPort) : serialPort_(serialPort) {}

void TargetControl::begin() {
  pinMode(AppConfig::kSwdClockPin, OUTPUT);
  digitalWrite(AppConfig::kSwdClockPin, HIGH);
  pinMode(AppConfig::kSwdIoPin, INPUT_PULLUP);
  if (AppConfig::kSwdResetPin >= 0) {
    pinMode(AppConfig::kSwdResetPin, OUTPUT);
    digitalWrite(AppConfig::kSwdResetPin, HIGH);
  }

  if (AppConfig::kTargetBoot0Pin >= 0) {
    pinMode(AppConfig::kTargetBoot0Pin, OUTPUT);
    digitalWrite(AppConfig::kTargetBoot0Pin, LOW);
  }

  if (AppConfig::kTargetResetPin >= 0) {
    pinMode(AppConfig::kTargetResetPin, OUTPUT);
    digitalWrite(AppConfig::kTargetResetPin, HIGH);
  }
}

HardwareSerial &TargetControl::serial() {
  return serialPort_;
}

bool TargetControl::isAutomaticAvailable() const {
  return AppConfig::kTargetBoot0Pin >= 0 && AppConfig::kTargetResetPin >= 0;
}

bool TargetControl::prepareForBootloader(TargetControlMode mode, String &error) {
  if (mode == TargetControlMode::Manual) {
    serialPort_.flush();
    while (serialPort_.available() > 0) {
      serialPort_.read();
    }
    return true;
  }

  if (!isAutomaticAvailable()) {
    error = "Automatic mode requires BOOT0 and RESET pins";
    return false;
  }

  digitalWrite(AppConfig::kTargetBoot0Pin, HIGH);
  delay(20);
  pulseReset();
  delay(120);
  return true;
}

bool TargetControl::prepareForSwd(String &error) {
  (void)error;
  if (AppConfig::kSwdResetPin >= 0) {
    digitalWrite(AppConfig::kSwdResetPin, HIGH);
  }
  delay(2);
  return true;
}

void TargetControl::holdSwdReset() {
  if (AppConfig::kSwdResetPin >= 0) {
    digitalWrite(AppConfig::kSwdResetPin, LOW);
  }
}

void TargetControl::releaseSwdReset() {
  if (AppConfig::kSwdResetPin >= 0) {
    digitalWrite(AppConfig::kSwdResetPin, HIGH);
  }
}

void TargetControl::bootApplication(TargetControlMode mode) {
  if (mode == TargetControlMode::Automatic && isAutomaticAvailable()) {
    digitalWrite(AppConfig::kTargetBoot0Pin, LOW);
    delay(20);
    pulseReset();
    delay(80);
  }
}

void TargetControl::resetTarget(ResetKind kind) {
  if (kind == ResetKind::Swd) {
    if (AppConfig::kSwdResetPin >= 0) {
      digitalWrite(AppConfig::kSwdResetPin, LOW);
      delay(20);
      digitalWrite(AppConfig::kSwdResetPin, HIGH);
      delay(20);
    }
    return;
  }
  pulseReset();
}

const char *TargetControl::modeName(TargetControlMode mode) const {
  return mode == TargetControlMode::Automatic ? "automatic" : "manual";
}

void TargetControl::pulseReset() {
  if (AppConfig::kTargetResetPin < 0) {
    return;
  }

  digitalWrite(AppConfig::kTargetResetPin, LOW);
  delay(50);
  digitalWrite(AppConfig::kTargetResetPin, HIGH);
}
