#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

class AccessPointManager {
public:
  bool begin(String &error);
  void update();

  String ipAddress() const;
  String ssid() const;
  bool hasPassword() const;

  bool configureStation(const String &ssid, const String &password, String &error);
  bool clearStationConfig(String &error);
  bool scanNetworks(JsonArray networks, String &error);
  bool stationConfigured() const;
  bool stationConnected() const;
  bool stationConnecting() const;
  String stationSsid() const;
  String stationIpAddress() const;
  String stationStatus() const;
  int stationRssi() const;

private:
  void startStationConnect();

  Preferences preferences_;
  String stationSsid_;
  String stationPassword_;
  String stationStatus_ = "未配置 WiFi";
  uint32_t stationConnectStartedMs_ = 0;
  bool stationConnecting_ = false;
};
