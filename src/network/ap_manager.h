#pragma once

#include <Arduino.h>

class AccessPointManager {
public:
  bool begin(String &error);
  String ipAddress() const;
  String ssid() const;
  bool hasPassword() const;
};
