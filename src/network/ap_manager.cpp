#include "ap_manager.h"

#include <WiFi.h>
#include "app_config.h"

namespace {
constexpr const char *kPrefsNamespace = "wifi";
constexpr const char *kPrefsSsidKey = "ssid";
constexpr const char *kPrefsPasswordKey = "password";
constexpr uint32_t kStationConnectTimeoutMs = 15000;
}

bool AccessPointManager::begin(String &error) {
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(AppConfig::accessPointIp(), AppConfig::gatewayIp(), AppConfig::subnetMask())) {
    error = "Failed to configure the access point";
    return false;
  }

  if (!WiFi.softAP(AppConfig::kAccessPointSsid, AppConfig::kAccessPointPassword)) {
    error = "Failed to start the access point";
    return false;
  }

  preferences_.begin(kPrefsNamespace, false);
  stationSsid_ = preferences_.getString(kPrefsSsidKey, "");
  stationPassword_ = preferences_.getString(kPrefsPasswordKey, "");
  if (stationConfigured()) {
    startStationConnect();
  } else {
    stationStatus_ = "未配置 WiFi，热点模式可用";
  }

  return true;
}

void AccessPointManager::update() {
  if (!stationConfigured()) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    stationConnecting_ = false;
    stationStatus_ = "已连接";
    return;
  }

  if (stationConnecting_ && millis() - stationConnectStartedMs_ > kStationConnectTimeoutMs) {
    stationConnecting_ = false;
    WiFi.disconnect(false, true);
    stationStatus_ = "连接失败，热点模式仍可用";
  }
}

String AccessPointManager::ipAddress() const {
  return WiFi.softAPIP().toString();
}

String AccessPointManager::ssid() const {
  return String(AppConfig::kAccessPointSsid);
}

bool AccessPointManager::hasPassword() const {
  return strlen(AppConfig::kAccessPointPassword) > 0;
}

bool AccessPointManager::configureStation(const String &ssid, const String &password, String &error) {
  String trimmedSsid = ssid;
  trimmedSsid.trim();
  if (trimmedSsid.isEmpty()) {
    error = "WiFi 名称不能为空";
    return false;
  }

  stationSsid_ = trimmedSsid;
  stationPassword_ = password;
  preferences_.putString(kPrefsSsidKey, stationSsid_);
  preferences_.putString(kPrefsPasswordKey, stationPassword_);
  startStationConnect();
  return true;
}

bool AccessPointManager::clearStationConfig(String &error) {
  (void)error;
  WiFi.disconnect(false, true);
  preferences_.remove(kPrefsSsidKey);
  preferences_.remove(kPrefsPasswordKey);
  stationSsid_ = "";
  stationPassword_ = "";
  stationConnecting_ = false;
  stationStatus_ = "未配置 WiFi，热点模式可用";
  return true;
}

bool AccessPointManager::scanNetworks(JsonArray networks, String &error) {
  WiFi.scanDelete();
  const int count = WiFi.scanNetworks(false, true);
  if (count < 0) {
    error = "WiFi 扫描失败";
    return false;
  }

  for (int i = 0; i < count; ++i) {
    String networkSsid = WiFi.SSID(i);
    if (networkSsid.isEmpty()) {
      continue;
    }

    bool exists = false;
    for (JsonObject network : networks) {
      if (network["ssid"].as<String>() == networkSsid) {
        const int rssi = WiFi.RSSI(i);
        if (rssi > (network["rssi"] | -999)) {
          network["rssi"] = rssi;
          network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        exists = true;
        break;
      }
    }
    if (exists) {
      continue;
    }

    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = networkSsid;
    network["rssi"] = WiFi.RSSI(i);
    network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }

  WiFi.scanDelete();
  if (stationConfigured() && !stationConnected()) {
    startStationConnect();
  }
  return true;
}

bool AccessPointManager::stationConfigured() const {
  return !stationSsid_.isEmpty();
}

bool AccessPointManager::stationConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool AccessPointManager::stationConnecting() const {
  return stationConnecting_;
}

String AccessPointManager::stationSsid() const {
  return stationSsid_;
}

String AccessPointManager::stationIpAddress() const {
  return stationConnected() ? WiFi.localIP().toString() : String();
}

String AccessPointManager::stationStatus() const {
  return stationStatus_;
}

int AccessPointManager::stationRssi() const {
  return stationConnected() ? WiFi.RSSI() : 0;
}

void AccessPointManager::startStationConnect() {
  WiFi.disconnect(false, true);
  WiFi.begin(stationSsid_.c_str(), stationPassword_.c_str());
  stationConnectStartedMs_ = millis();
  stationConnecting_ = true;
  stationStatus_ = "正在连接 WiFi";
}
