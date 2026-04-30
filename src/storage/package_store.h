#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "flash_manifest.h"

struct SavedPackageInfo {
  String id;
  String name;
  String chip;
  uint32_t address = 0;
  uint32_t crc32 = 0;
  size_t size = 0;
};

class PackageStore {
public:
  bool begin();
  bool hasPackage() const;
  bool removePackage(String &error);
  bool saveManifestJson(const uint8_t *data, size_t length, bool reset, String &error);
  bool appendFirmwareChunk(const uint8_t *data, size_t length, bool reset, String &error);
  bool appendIntelHexChunk(const uint8_t *data, size_t length, bool reset, String &error);
  bool finalizeIntelHexPackage(String &error);
  bool finalizePackage(String &error);
  bool loadManifest(FlashManifest &manifest, String &error) const;
  bool listSavedPackages(JsonArray array, String &error) const;
  String selectedSavedPackageId(String &error) const;
  bool clearSelectedSavedPackage(String &error);
  bool saveActivePackage(const String &name, SavedPackageInfo &info, String &error);
  bool restoreSavedPackage(const String &id, String &error);
  bool removeSavedPackage(const String &id, String &error);
  String firmwarePath() const;
  size_t firmwareSize() const;
  size_t totalBytes() const;
  size_t usedBytes() const;
  size_t freeBytes() const;

private:
  bool manifestExists_ = false;
  bool firmwareExists_ = false;
  size_t firmwareSize_ = 0;

  bool parseManifest(const String &json, FlashManifest &manifest, String &error) const;
  bool convertIntelHexToBinary(FlashManifest &manifest, String &error) const;
  bool scanIntelHex(uint32_t &minAddress, uint32_t &maxAddressExclusive, String &error) const;
  bool writeGeneratedManifest(const FlashManifest &manifest, String &error) const;
  uint32_t computeCrc32(const char *path, String &error) const;
  bool ensureFreeSpace(size_t bytes, String &error) const;
  bool loadSavedIndex(JsonDocument &doc, String &error) const;
  bool saveSavedIndex(JsonDocument &doc, String &error) const;
  bool setSelectedSavedPackageId(const String &id, String &error);
  bool copyFile(const char *sourcePath, const char *destPath, String &error) const;
  bool removeUploadTemps(String &error);
  String sanitizePackageName(const String &name) const;
  String generatePackageId() const;
  String savedManifestPath(const String &id) const;
  String savedFirmwarePath(const String &id) const;
};
