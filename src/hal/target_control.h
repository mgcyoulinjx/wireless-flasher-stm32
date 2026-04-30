#pragma once

#include <Arduino.h>
#include "app_config.h"

enum class TargetControlMode {
  Manual,
  Automatic,
};

enum class ResetKind {
  UartBootloader,
  Swd,
};

class TargetControl {
public:
  explicit TargetControl(HardwareSerial &serialPort);

  void begin();
  HardwareSerial &serial();
  bool isAutomaticAvailable() const;
  bool prepareForBootloader(TargetControlMode mode, String &error);
  bool prepareForSwd(String &error);
  void holdSwdReset();
  void releaseSwdReset();
  void bootApplication(TargetControlMode mode);
  void resetTarget(ResetKind kind);
  const char *modeName(TargetControlMode mode) const;

private:
  HardwareSerial &serialPort_;

  void pulseReset();
};
