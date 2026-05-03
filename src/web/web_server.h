#pragma once

#include <Arduino.h>
#include <Preferences.h>

class WebServer;
class AccessPointManager;
class PackageStore;
class FlashManager;
class TargetControl;
class Stm32SwdDebug;
class BuzzerManager;
class AppWebServer {
public:
  AppWebServer(AccessPointManager &apManager,
               PackageStore &packageStore,
               FlashManager &flashManager,
               TargetControl &targetControl,
               Stm32SwdDebug &swdDebug,
               BuzzerManager &buzzerManager,
               Preferences &preferences);
  void begin();
  void handleClient();

private:
  AccessPointManager &apManager_;
  PackageStore &packageStore_;
  FlashManager &flashManager_;
  TargetControl &targetControl_;
  Stm32SwdDebug &swdDebug_;
  BuzzerManager &buzzerManager_;
  Preferences &preferences_;
  WebServer *server_ = nullptr;

  void configureRoutes();
  void handleIndex();
  void handleInfo();
  void handleWifiConfig();
  void handleWifiForget();
  void handleWifiScan();
  void handleStatus();
  void handleHexUploadFinalize();
  void handlePackages();
  void handlePackagesVersion();
  void handleSavePackage();
  void handleSelectPackage();
  void handleDeleteSavedPackage();
  void handleChipInfo();
  void handleBuzzerConfig();
  void handleSaveBuzzerConfig();
  void handleEsp32OtaFinalize();
  void handleEsp32OtaUpload();
  void handleEsp32OtaStatus();
  void handleFlashStart();
  void handleFlashCancel();
  void handleDeletePackage();
  void sendJson(int statusCode, const String &payload);
  void sendError(int statusCode, const String &message);
};
