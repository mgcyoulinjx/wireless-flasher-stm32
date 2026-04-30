#include "ap_manager.h"

#include <WiFi.h>
#include "app_config.h"

bool AccessPointManager::begin(String &error) {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(AppConfig::accessPointIp(), AppConfig::gatewayIp(), AppConfig::subnetMask())) {
    error = "Failed to configure the access point";
    return false;
  }

  if (!WiFi.softAP(AppConfig::kAccessPointSsid, AppConfig::kAccessPointPassword)) {
    error = "Failed to start the access point";
    return false;
  }

  return true;
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
