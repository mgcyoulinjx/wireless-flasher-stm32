#pragma once

#include <Arduino.h>

class WebServer;
class AccessPointManager;
class PackageStore;
class FlashManager;
class TargetControl;
class Stm32SwdDebug;
class Stm32F1Flash;

class AppWebServer {
public:
  AppWebServer(AccessPointManager &apManager,
               PackageStore &packageStore,
               FlashManager &flashManager,
               TargetControl &targetControl,
               Stm32SwdDebug &swdDebug,
               Stm32F1Flash &stm32F1Flash);
  void begin();
  void handleClient();

private:
  AccessPointManager &apManager_;
  PackageStore &packageStore_;
  FlashManager &flashManager_;
  TargetControl &targetControl_;
  Stm32SwdDebug &swdDebug_;
  Stm32F1Flash &stm32F1Flash_;
  WebServer *server_ = nullptr;

  void configureRoutes();
  void handleIndex();
  void handleInfo();
  void handleStatus();
  void handleManifestExample();
  void handleUploadFinalize();
  void handleHexUploadFinalize();
  void handlePackages();
  void handleSavePackage();
  void handleSelectPackage();
  void handleDeleteSavedPackage();
  void handleChipInfo();
  void handleFlashStart();
  void handleFlashCancel();
  void handleDeletePackage();
  void sendJson(int statusCode, const String &payload);
  void sendError(int statusCode, const String &message);
};
