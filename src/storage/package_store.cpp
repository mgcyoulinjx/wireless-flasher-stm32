#include "package_store.h"

#include <ArduinoJson.h>
#include "app_config.h"

namespace {
constexpr uint32_t kCrc32Polynomial = 0xEDB88320UL;
constexpr uint32_t kInvalidAddress = 0xFFFFFFFFUL;

int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool parseHexByte(const String &line, size_t offset, uint8_t &value) {
  if (offset + 1 >= line.length()) {
    return false;
  }
  int high = hexNibble(line[offset]);
  int low = hexNibble(line[offset + 1]);
  if (high < 0 || low < 0) {
    return false;
  }
  value = static_cast<uint8_t>((high << 4) | low);
  return true;
}

bool readHexLine(File &file, String &line) {
  line = "";
  while (file.available() > 0) {
    char c = static_cast<char>(file.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      return true;
    }
    line += c;
  }
  return line.length() > 0;
}
}

bool PackageStore::begin() {
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    return false;
  }

  manifestExists_ = LittleFS.exists(AppConfig::kManifestPath);
  firmwareExists_ = LittleFS.exists(AppConfig::kFirmwarePath);
  firmwareSize_ = firmwareExists_ ? LittleFS.open(AppConfig::kFirmwarePath, FILE_READ).size() : 0;
  JsonDocument index;
  String error;
  if (!loadSavedIndex(index, error)) {
    return false;
  }
  updateSavedIndexCache(index);
  return true;
}

bool PackageStore::hasPackage() const {
  return manifestExists_ && firmwareExists_ && firmwareSize_ > 0;
}

bool PackageStore::savedPackagesDirty() const {
  return savedPackagesDirty_;
}

void PackageStore::clearSavedPackagesDirty() {
  savedPackagesDirty_ = false;
}

bool PackageStore::removePackage(String &error) {
  if (LittleFS.exists(AppConfig::kHexTempPath) && !LittleFS.remove(AppConfig::kHexTempPath)) {
    error = "Failed to remove temporary HEX";
    return false;
  }
  if (LittleFS.exists(AppConfig::kManifestTempPath) && !LittleFS.remove(AppConfig::kManifestTempPath)) {
    error = "Failed to remove temporary manifest";
    return false;
  }
  if (LittleFS.exists(AppConfig::kFirmwareTempPath) && !LittleFS.remove(AppConfig::kFirmwareTempPath)) {
    error = "Failed to remove temporary firmware";
    return false;
  }
  if (LittleFS.exists(AppConfig::kManifestPath) && !LittleFS.remove(AppConfig::kManifestPath)) {
    error = "Failed to remove manifest";
    return false;
  }
  if (LittleFS.exists(AppConfig::kFirmwarePath) && !LittleFS.remove(AppConfig::kFirmwarePath)) {
    error = "Failed to remove firmware";
    return false;
  }

  manifestExists_ = false;
  firmwareExists_ = false;
  firmwareSize_ = 0;
  return true;
}

bool PackageStore::appendIntelHexChunk(const uint8_t *data, size_t length, bool reset, String &error) {
  if (!data || length == 0) {
    error = "Intel HEX chunk is empty";
    return false;
  }

  if (reset) {
    LittleFS.remove(AppConfig::kHexTempPath);
    LittleFS.remove(AppConfig::kManifestTempPath);
    LittleFS.remove(AppConfig::kFirmwareTempPath);
  }
  if (!ensureFreeSpace(length, error)) {
    return false;
  }

  File file = LittleFS.open(AppConfig::kHexTempPath, reset ? FILE_WRITE : FILE_APPEND);
  if (!file) {
    error = "Failed to open Intel HEX file, LittleFS used " + String(LittleFS.usedBytes()) + " / " + String(LittleFS.totalBytes()) + " bytes";
    return false;
  }
  if (file.size() + length > AppConfig::kMaxHexUploadSize) {
    file.close();
    error = "Intel HEX file is too large";
    return false;
  }
  if (file.write(data, length) != length) {
    file.close();
    error = "Failed to append Intel HEX chunk";
    return false;
  }

  file.close();
  return true;
}

bool PackageStore::finalizeIntelHexPackage(String &error) {
  if (!LittleFS.exists(AppConfig::kHexTempPath)) {
    error = "Intel HEX file has not been uploaded";
    return false;
  }

  LittleFS.remove(AppConfig::kManifestTempPath);
  LittleFS.remove(AppConfig::kFirmwareTempPath);

  FlashManifest manifest;
  if (!convertIntelHexToBinary(manifest, error)) {
    return false;
  }
  if (!writeGeneratedManifest(manifest, error)) {
    return false;
  }
  if (!finalizePackage(error)) {
    return false;
  }
  LittleFS.remove(AppConfig::kHexTempPath);
  return true;
}

bool PackageStore::finalizePackage(String &error) {
  if (!LittleFS.exists(AppConfig::kManifestTempPath) || !LittleFS.exists(AppConfig::kFirmwareTempPath)) {
    error = "Manifest and firmware must both be uploaded";
    return false;
  }

  FlashManifest manifest;
  File manifestFile = LittleFS.open(AppConfig::kManifestTempPath, FILE_READ);
  if (!manifestFile) {
    error = "Failed to open temporary manifest";
    return false;
  }

  String manifestJson = manifestFile.readString();
  manifestFile.close();

  if (!parseManifest(manifestJson, manifest, error)) {
    return false;
  }

  File firmwareFile = LittleFS.open(AppConfig::kFirmwareTempPath, FILE_READ);
  if (!firmwareFile) {
    error = "Failed to open temporary firmware";
    return false;
  }

  size_t size = firmwareFile.size();
  firmwareFile.close();

  if (size == 0 || size > AppConfig::kMaxFirmwareSize) {
    error = "Firmware size is invalid";
    return false;
  }

  if (manifest.size != size) {
    error = "Manifest size does not match firmware size";
    return false;
  }

  uint32_t crc32 = computeCrc32(AppConfig::kFirmwareTempPath, error);
  if (!error.isEmpty()) {
    return false;
  }

  if (manifest.crc32 != crc32) {
    error = "Firmware checksum does not match manifest: expected 0x" + String(manifest.crc32, HEX) +
            ", actual 0x" + String(crc32, HEX) + ", size " + String(size) + " bytes";
    error.toUpperCase();
    return false;
  }

  LittleFS.remove(AppConfig::kManifestPath);
  LittleFS.remove(AppConfig::kFirmwarePath);

  if (!LittleFS.rename(AppConfig::kManifestTempPath, AppConfig::kManifestPath)) {
    error = "Failed to save manifest";
    return false;
  }

  if (!LittleFS.rename(AppConfig::kFirmwareTempPath, AppConfig::kFirmwarePath)) {
    error = "Failed to save firmware";
    return false;
  }

  manifestExists_ = true;
  firmwareExists_ = true;
  firmwareSize_ = size;
  return true;
}

bool PackageStore::loadManifest(FlashManifest &manifest, String &error) const {
  if (!LittleFS.exists(AppConfig::kManifestPath)) {
    error = "No manifest has been uploaded";
    return false;
  }

  File file = LittleFS.open(AppConfig::kManifestPath, FILE_READ);
  if (!file) {
    error = "Failed to open manifest";
    return false;
  }

  String json = file.readString();
  file.close();
  return parseManifest(json, manifest, error);
}

String PackageStore::firmwarePath() const {
  return String(AppConfig::kFirmwarePath);
}

size_t PackageStore::firmwareSize() const {
  return firmwareSize_;
}

size_t PackageStore::totalBytes() const {
  return LittleFS.totalBytes();
}

size_t PackageStore::usedBytes() const {
  return LittleFS.usedBytes();
}

size_t PackageStore::freeBytes() const {
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  return used <= total ? total - used : 0;
}

bool PackageStore::listSavedPackages(JsonArray array, String &error) const {
  error = "";
  for (const SavedPackageInfo &package : savedPackagesCache_) {
    JsonObject item = array.add<JsonObject>();
    item["id"] = package.id;
    item["name"] = package.name;
    item["chip"] = package.chip;
    item["address"] = package.address;
    item["size"] = package.size;
    item["crc32"] = package.crc32;
  }
  return true;
}

bool PackageStore::listSavedPackages(std::vector<SavedPackageInfo> &packages, String &error) const {
  error = "";
  packages = savedPackagesCache_;
  return true;
}

String PackageStore::selectedSavedPackageId(String &error) const {
  error = "";
  return selectedSavedPackageIdCache_;
}

uint32_t PackageStore::savedPackagesVersion() const {
  return savedPackagesVersion_;
}

bool PackageStore::selectSavedPackage(const String &id, String &error) {
  return setSelectedSavedPackageId(id, error);
}

bool PackageStore::clearSelectedSavedPackage(String &error) {
  return setSelectedSavedPackageId("", error);
}

bool PackageStore::saveActivePackage(const String &name, SavedPackageInfo &info, String &error, const String &replaceId) {
  if (!hasPackage()) {
    error = "No active firmware package to save";
    return false;
  }

  FlashManifest manifest;
  if (!loadManifest(manifest, error)) {
    return false;
  }

  File manifestFile = LittleFS.open(AppConfig::kManifestPath, FILE_READ);
  File firmwareFile = LittleFS.open(AppConfig::kFirmwarePath, FILE_READ);
  if (!manifestFile || !firmwareFile) {
    if (manifestFile) {
      manifestFile.close();
    }
    if (firmwareFile) {
      firmwareFile.close();
    }
    error = "Failed to open active package";
    return false;
  }
  const size_t manifestSize = manifestFile.size();
  const size_t firmwareSize = firmwareFile.size();
  manifestFile.close();
  firmwareFile.close();

  if (firmwareSize != manifest.size) {
    error = "Active firmware size does not match manifest";
    return false;
  }

  String crcError;
  const uint32_t crc32 = computeCrc32(AppConfig::kFirmwarePath, crcError);
  if (!crcError.isEmpty()) {
    error = crcError;
    return false;
  }
  if (crc32 != manifest.crc32) {
    error = "Active firmware checksum does not match manifest";
    return false;
  }

  if (replaceId.isEmpty() && !ensureFreeSpace(manifestSize + firmwareSize, error)) {
    return false;
  }

  JsonDocument index;
  if (!loadSavedIndex(index, error)) {
    return false;
  }

  String displayName = sanitizePackageName(name);
  if (displayName.isEmpty()) {
    String address = String(manifest.address, HEX);
    address.toUpperCase();
    displayName = manifest.chip + " 0x" + address + " " + String(manifest.size) + " bytes";
  }

  JsonArray packages = index["packages"].as<JsonArray>();
  JsonObject item;
  String id = replaceId;
  if (!id.isEmpty()) {
    for (JsonObject package : packages) {
      if (String(package["id"] | "") == id) {
        item = package;
        break;
      }
    }
    if (item.isNull()) {
      error = "Saved package was not found";
      return false;
    }
  } else {
    String uniqueName = displayName;
    for (int suffix = 1;; ++suffix) {
      bool exists = false;
      for (JsonObject package : packages) {
        if (String(package["name"] | "") == uniqueName) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        displayName = uniqueName;
        break;
      }
      uniqueName = displayName + " (" + String(suffix + 1) + ")";
    }
    id = generatePackageId();
    item = packages.add<JsonObject>();
    item["id"] = id;
    item["createdMs"] = millis();
  }

  const String manifestPath = savedManifestPath(id);
  const String firmwarePath = savedFirmwarePath(id);
  if (!id.isEmpty() && replaceId.length()) {
    LittleFS.remove(manifestPath);
    LittleFS.remove(firmwarePath);
  }
  if (!copyFile(AppConfig::kManifestPath, manifestPath.c_str(), error)) {
    return false;
  }
  if (!copyFile(AppConfig::kFirmwarePath, firmwarePath.c_str(), error)) {
    LittleFS.remove(manifestPath);
    return false;
  }

  item["name"] = displayName;
  item["chip"] = manifest.chip;
  item["address"] = manifest.address;
  item["size"] = manifest.size;
  item["crc32"] = manifest.crc32;
  item["updatedMs"] = millis();

  if (!saveSavedIndex(index, error)) {
    LittleFS.remove(manifestPath);
    LittleFS.remove(firmwarePath);
    return false;
  }

  info.id = id;
  info.name = displayName;
  info.chip = manifest.chip;
  info.address = manifest.address;
  info.size = manifest.size;
  info.crc32 = manifest.crc32;
  return true;
}

bool PackageStore::restoreSavedPackage(const String &id, String &error) {
  if (id.isEmpty()) {
    error = "Saved package id is required";
    return false;
  }

  JsonDocument index;
  if (!loadSavedIndex(index, error)) {
    return false;
  }

  bool found = false;
  JsonArray packages = index["packages"].as<JsonArray>();
  for (JsonObject package : packages) {
    if (String(package["id"] | "") == id) {
      found = true;
      break;
    }
  }
  if (!found) {
    error = "Saved package was not found";
    return false;
  }

  const String manifestPath = savedManifestPath(id);
  const String firmwarePath = savedFirmwarePath(id);
  File manifestFile = LittleFS.open(manifestPath, FILE_READ);
  File firmwareFile = LittleFS.open(firmwarePath, FILE_READ);
  if (!manifestFile || !firmwareFile) {
    if (manifestFile) {
      manifestFile.close();
    }
    if (firmwareFile) {
      firmwareFile.close();
    }
    error = "Saved package files are missing";
    return false;
  }

  const String manifestJson = manifestFile.readString();
  const size_t manifestSize = manifestFile.size();
  const size_t firmwareSize = firmwareFile.size();
  manifestFile.close();
  firmwareFile.close();

  FlashManifest manifest;
  if (!parseManifest(manifestJson, manifest, error)) {
    return false;
  }
  if (firmwareSize != manifest.size) {
    error = "Saved firmware size does not match manifest";
    return false;
  }

  String crcError;
  const uint32_t crc32 = computeCrc32(firmwarePath.c_str(), crcError);
  if (!crcError.isEmpty()) {
    error = crcError;
    return false;
  }
  if (crc32 != manifest.crc32) {
    error = "Saved firmware checksum does not match manifest";
    return false;
  }

  if (!removeUploadTemps(error)) {
    return false;
  }
  if (!ensureFreeSpace(manifestSize + firmwareSize, error)) {
    return false;
  }
  if (!copyFile(manifestPath.c_str(), AppConfig::kManifestTempPath, error)) {
    return false;
  }
  if (!copyFile(firmwarePath.c_str(), AppConfig::kFirmwareTempPath, error)) {
    LittleFS.remove(AppConfig::kManifestTempPath);
    return false;
  }
  if (!finalizePackage(error)) {
    return false;
  }
  return setSelectedSavedPackageId(id, error);
}

bool PackageStore::removeSavedPackage(const String &id, String &error) {
  if (id.isEmpty()) {
    error = "Saved package id is required";
    return false;
  }

  JsonDocument index;
  if (!loadSavedIndex(index, error)) {
    return false;
  }

  JsonArray packages = index["packages"].as<JsonArray>();
  bool removed = false;
  for (size_t i = 0; i < packages.size(); ++i) {
    if (String(packages[i]["id"] | "") == id) {
      packages.remove(i);
      removed = true;
      break;
    }
  }
  if (!removed) {
    error = "Saved package was not found";
    return false;
  }
  if (String(index["selectedId"] | "") == id) {
    index["selectedId"] = "";
  }

  const String manifestPath = savedManifestPath(id);
  const String firmwarePath = savedFirmwarePath(id);
  if (LittleFS.exists(manifestPath) && !LittleFS.remove(manifestPath)) {
    error = "Failed to remove saved manifest";
    return false;
  }
  if (LittleFS.exists(firmwarePath) && !LittleFS.remove(firmwarePath)) {
    error = "Failed to remove saved firmware";
    return false;
  }
  return saveSavedIndex(index, error);
}

bool PackageStore::parseManifest(const String &json, FlashManifest &manifest, String &error) const {
  JsonDocument doc;
  DeserializationError parseError = deserializeJson(doc, json);
  if (parseError) {
    error = "Manifest JSON is invalid";
    return false;
  }

  manifest.target = doc["target"] | "";
  manifest.chip = doc["chip"] | "";
  manifest.address = doc["address"].as<uint32_t>() ? doc["address"].as<uint32_t>() : AppConfig::kDefaultFlashAddress;
  manifest.size = doc["size"].as<size_t>();
  manifest.crc32 = doc["crc32"].as<uint32_t>();

  if (manifest.target != "stm32") {
    error = "Manifest target must be stm32";
    return false;
  }
  if (manifest.chip.isEmpty()) {
    error = "Manifest chip is required";
    return false;
  }
  if (manifest.size == 0 || manifest.size > AppConfig::kMaxFirmwareSize) {
    error = "Manifest size is invalid";
    return false;
  }

  return true;
}

bool PackageStore::scanIntelHex(uint32_t &minAddress, uint32_t &maxAddressExclusive, String &error) const {
  File file = LittleFS.open(AppConfig::kHexTempPath, FILE_READ);
  if (!file) {
    error = "Failed to open Intel HEX for parsing";
    return false;
  }

  minAddress = kInvalidAddress;
  maxAddressExclusive = 0;
  uint32_t upperAddress = 0;
  bool sawEof = false;
  bool sawData = false;
  String line;
  size_t lineNumber = 0;
  while (readHexLine(file, line)) {
    ++lineNumber;
    line.trim();
    if (line.isEmpty()) {
      continue;
    }
    if (sawEof) {
      error = "Intel HEX has data after EOF";
      file.close();
      return false;
    }
    if (line[0] != ':' || line.length() < 11) {
      error = "Intel HEX line " + String(lineNumber) + " is malformed";
      file.close();
      return false;
    }

    uint8_t byteCount = 0;
    uint8_t addressHigh = 0;
    uint8_t addressLow = 0;
    uint8_t recordType = 0;
    if (!parseHexByte(line, 1, byteCount) || !parseHexByte(line, 3, addressHigh) ||
        !parseHexByte(line, 5, addressLow) || !parseHexByte(line, 7, recordType)) {
      error = "Intel HEX line " + String(lineNumber) + " contains invalid hex";
      file.close();
      return false;
    }
    const size_t expectedLength = 11 + static_cast<size_t>(byteCount) * 2;
    if (line.length() != expectedLength) {
      error = "Intel HEX line " + String(lineNumber) + " length is invalid";
      file.close();
      return false;
    }

    uint8_t sum = byteCount + addressHigh + addressLow + recordType;
    for (uint8_t index = 0; index < byteCount; ++index) {
      uint8_t dataByte = 0;
      if (!parseHexByte(line, 9 + index * 2, dataByte)) {
        error = "Intel HEX line " + String(lineNumber) + " contains invalid data";
        file.close();
        return false;
      }
      sum += dataByte;
    }
    uint8_t checksum = 0;
    if (!parseHexByte(line, 9 + byteCount * 2, checksum) || static_cast<uint8_t>(sum + checksum) != 0) {
      error = "Intel HEX line " + String(lineNumber) + " checksum is invalid";
      file.close();
      return false;
    }

    uint16_t offset = static_cast<uint16_t>((addressHigh << 8) | addressLow);
    if (recordType == 0x00) {
      if (byteCount == 0) {
        continue;
      }
      uint32_t start = upperAddress + offset;
      uint32_t end = start + byteCount;
      if (end < start) {
        error = "Intel HEX address overflow";
        file.close();
        return false;
      }
      if (start < AppConfig::kDefaultFlashAddress) {
        error = "Intel HEX data is outside STM32 internal flash address range";
        file.close();
        return false;
      }
      if (start < minAddress) {
        minAddress = start;
      }
      if (end > maxAddressExclusive) {
        maxAddressExclusive = end;
      }
      sawData = true;
    } else if (recordType == 0x01) {
      if (byteCount != 0 || offset != 0) {
        error = "Intel HEX EOF record is invalid";
        file.close();
        return false;
      }
      sawEof = true;
    } else if (recordType == 0x04) {
      if (byteCount != 2) {
        error = "Intel HEX extended linear address record is invalid";
        file.close();
        return false;
      }
      uint8_t high = 0;
      uint8_t low = 0;
      parseHexByte(line, 9, high);
      parseHexByte(line, 11, low);
      upperAddress = static_cast<uint32_t>((high << 8) | low) << 16;
    } else if (recordType == 0x03 || recordType == 0x05) {
      continue;
    } else if (recordType == 0x02) {
      error = "Intel HEX extended segment address records are not supported";
      file.close();
      return false;
    } else {
      error = "Intel HEX record type " + String(recordType, HEX) + " is not supported";
      file.close();
      return false;
    }
  }
  file.close();

  if (!sawEof) {
    error = "Intel HEX EOF record is missing";
    return false;
  }
  if (!sawData || minAddress == kInvalidAddress || maxAddressExclusive <= minAddress) {
    error = "Intel HEX contains no data records";
    return false;
  }
  return true;
}

bool PackageStore::convertIntelHexToBinary(FlashManifest &manifest, String &error) const {
  uint32_t minAddress = 0;
  uint32_t maxAddressExclusive = 0;
  if (!scanIntelHex(minAddress, maxAddressExclusive, error)) {
    return false;
  }

  size_t binarySize = maxAddressExclusive - minAddress;
  if (binarySize & 0x1U) {
    ++binarySize;
  }
  if (binarySize == 0 || binarySize > AppConfig::kMaxFirmwareSize) {
    error = "Intel HEX converted firmware size is invalid";
    return false;
  }
  if (minAddress + binarySize < minAddress) {
    error = "Intel HEX converted firmware address overflows";
    return false;
  }

  File output = LittleFS.open(AppConfig::kFirmwareTempPath, FILE_WRITE);
  if (!output) {
    error = "Failed to create converted firmware";
    return false;
  }
  uint8_t fill[256];
  memset(fill, 0xFF, sizeof(fill));
  size_t remaining = binarySize;
  while (remaining > 0) {
    size_t chunk = remaining > sizeof(fill) ? sizeof(fill) : remaining;
    if (output.write(fill, chunk) != chunk) {
      output.close();
      error = "Failed to initialize converted firmware";
      return false;
    }
    remaining -= chunk;
  }
  output.close();

  File input = LittleFS.open(AppConfig::kHexTempPath, FILE_READ);
  output = LittleFS.open(AppConfig::kFirmwareTempPath, "r+");
  if (!input || !output) {
    if (input) {
      input.close();
    }
    if (output) {
      output.close();
    }
    error = "Failed to reopen HEX conversion files";
    return false;
  }

  uint32_t upperAddress = 0;
  bool sawEof = false;
  String line;
  while (readHexLine(input, line)) {
    line.trim();
    if (line.isEmpty()) {
      continue;
    }
    uint8_t byteCount = 0;
    uint8_t addressHigh = 0;
    uint8_t addressLow = 0;
    uint8_t recordType = 0;
    parseHexByte(line, 1, byteCount);
    parseHexByte(line, 3, addressHigh);
    parseHexByte(line, 5, addressLow);
    parseHexByte(line, 7, recordType);
    uint16_t offset = static_cast<uint16_t>((addressHigh << 8) | addressLow);

    if (recordType == 0x00 && byteCount > 0) {
      uint32_t start = upperAddress + offset;
      if (!output.seek(start - minAddress, SeekSet)) {
        input.close();
        output.close();
        error = "Failed to seek converted firmware";
        return false;
      }
      for (uint8_t index = 0; index < byteCount; ++index) {
        uint8_t dataByte = 0;
        parseHexByte(line, 9 + index * 2, dataByte);
        if (output.write(&dataByte, 1) != 1) {
          input.close();
          output.close();
          error = "Failed to write converted firmware";
          return false;
        }
      }
    } else if (recordType == 0x01) {
      sawEof = true;
      break;
    } else if (recordType == 0x04) {
      uint8_t high = 0;
      uint8_t low = 0;
      parseHexByte(line, 9, high);
      parseHexByte(line, 11, low);
      upperAddress = static_cast<uint32_t>((high << 8) | low) << 16;
    }
  }
  input.close();
  output.close();

  if (!sawEof) {
    error = "Intel HEX EOF record is missing";
    return false;
  }

  uint32_t crc32 = computeCrc32(AppConfig::kFirmwareTempPath, error);
  if (!error.isEmpty()) {
    return false;
  }
  manifest.target = "stm32";
  manifest.chip = "STM32";
  manifest.address = minAddress;
  manifest.size = binarySize;
  manifest.crc32 = crc32;
  return true;
}

bool PackageStore::writeGeneratedManifest(const FlashManifest &manifest, String &error) const {
  JsonDocument doc;
  doc["target"] = manifest.target;
  doc["chip"] = manifest.chip;
  doc["address"] = manifest.address;
  doc["size"] = manifest.size;
  doc["crc32"] = manifest.crc32;

  File file = LittleFS.open(AppConfig::kManifestTempPath, FILE_WRITE);
  if (!file) {
    error = "Failed to create generated manifest";
    return false;
  }
  serializeJson(doc, file);
  file.close();
  return true;
}

bool PackageStore::ensureFreeSpace(size_t bytes, String &error) const {
  const size_t required = bytes + AppConfig::kLittleFsSafetyMargin;
  const size_t free = freeBytes();
  if (free >= required) {
    return true;
  }
  error = "LittleFS space is insufficient: free " + String(free) + " bytes, required " + String(required) +
          " bytes, used " + String(usedBytes()) + " / " + String(totalBytes()) + " bytes";
  return false;
}

void PackageStore::updateSavedIndexCache(JsonDocument &doc) {
  savedPackagesCache_.clear();
  JsonArray packages = doc["packages"].as<JsonArray>();
  selectedSavedPackageIdCache_ = doc["selectedId"] | "";
  bool selectedExists = selectedSavedPackageIdCache_.isEmpty();
  for (JsonObject package : packages) {
    SavedPackageInfo info;
    info.id = package["id"] | "";
    info.name = package["name"] | "";
    info.chip = package["chip"] | "";
    info.address = package["address"] | 0;
    info.size = package["size"] | 0;
    info.crc32 = package["crc32"] | 0;
    if (info.id == selectedSavedPackageIdCache_) {
      selectedExists = true;
    }
    savedPackagesCache_.push_back(info);
  }
  if (!selectedExists) {
    selectedSavedPackageIdCache_ = "";
  }
}

bool PackageStore::loadSavedIndex(JsonDocument &doc, String &error) const {
  doc.clear();
  if (!LittleFS.exists(AppConfig::kSavedPackagesIndexPath)) {
    doc["packages"].to<JsonArray>();
    return true;
  }

  File file = LittleFS.open(AppConfig::kSavedPackagesIndexPath, FILE_READ);
  if (!file) {
    error = "Failed to open saved package index";
    return false;
  }
  DeserializationError parseError = deserializeJson(doc, file);
  file.close();
  if (parseError) {
    error = "Saved package index is invalid";
    return false;
  }
  if (!doc["packages"].is<JsonArray>()) {
    doc["packages"].to<JsonArray>();
  }
  return true;
}

bool PackageStore::saveSavedIndex(JsonDocument &doc, String &error) {
  File file = LittleFS.open(AppConfig::kSavedPackagesIndexTempPath, FILE_WRITE);
  if (!file) {
    error = "Failed to write saved package index";
    return false;
  }
  if (serializeJson(doc, file) == 0) {
    file.close();
    error = "Failed to serialize saved package index";
    return false;
  }
  file.close();
  LittleFS.remove(AppConfig::kSavedPackagesIndexPath);
  if (!LittleFS.rename(AppConfig::kSavedPackagesIndexTempPath, AppConfig::kSavedPackagesIndexPath)) {
    error = "Failed to save package index";
    return false;
  }
  updateSavedIndexCache(doc);
  savedPackagesDirty_ = true;
  ++savedPackagesVersion_;
  return true;
}

bool PackageStore::setSelectedSavedPackageId(const String &id, String &error) {
  JsonDocument index;
  if (!loadSavedIndex(index, error)) {
    return false;
  }
  if (!id.isEmpty()) {
    bool found = false;
    JsonArray packages = index["packages"].as<JsonArray>();
    for (JsonObject package : packages) {
      if (String(package["id"] | "") == id) {
        found = true;
        break;
      }
    }
    if (!found) {
      error = "Saved package was not found";
      return false;
    }
  }
  index["selectedId"] = id;
  return saveSavedIndex(index, error);
}

bool PackageStore::copyFile(const char *sourcePath, const char *destPath, String &error) const {
  File source = LittleFS.open(sourcePath, FILE_READ);
  if (!source) {
    error = "Failed to open source file";
    return false;
  }
  File dest = LittleFS.open(destPath, FILE_WRITE);
  if (!dest) {
    source.close();
    error = "Failed to open destination file";
    return false;
  }

  uint8_t buffer[512];
  while (source.available() > 0) {
    const size_t readBytes = source.read(buffer, sizeof(buffer));
    if (readBytes > 0 && dest.write(buffer, readBytes) != readBytes) {
      source.close();
      dest.close();
      error = "Failed to copy file";
      return false;
    }
  }
  source.close();
  dest.close();
  return true;
}

bool PackageStore::removeUploadTemps(String &error) {
  if (LittleFS.exists(AppConfig::kHexTempPath) && !LittleFS.remove(AppConfig::kHexTempPath)) {
    error = "Failed to remove temporary HEX";
    return false;
  }
  if (LittleFS.exists(AppConfig::kManifestTempPath) && !LittleFS.remove(AppConfig::kManifestTempPath)) {
    error = "Failed to remove temporary manifest";
    return false;
  }
  if (LittleFS.exists(AppConfig::kFirmwareTempPath) && !LittleFS.remove(AppConfig::kFirmwareTempPath)) {
    error = "Failed to remove temporary firmware";
    return false;
  }
  return true;
}

String PackageStore::sanitizePackageName(const String &name) const {
  String cleaned = name;
  cleaned.trim();
  if (cleaned.length() > 48) {
    cleaned = cleaned.substring(0, 48);
  }
  return cleaned;
}

String PackageStore::generatePackageId() const {
  String id = String(millis(), HEX);
  for (int suffix = 0; suffix < 1000; ++suffix) {
    String candidate = suffix == 0 ? id : id + "_" + String(suffix);
    if (!LittleFS.exists(savedManifestPath(candidate)) && !LittleFS.exists(savedFirmwarePath(candidate))) {
      return candidate;
    }
  }
  return id + "_x";
}

String PackageStore::savedManifestPath(const String &id) const {
  return String(AppConfig::kSavedPackagePrefix) + id + AppConfig::kSavedPackageManifestSuffix;
}

String PackageStore::savedFirmwarePath(const String &id) const {
  return String(AppConfig::kSavedPackagePrefix) + id + AppConfig::kSavedPackageFirmwareSuffix;
}

uint32_t PackageStore::computeCrc32(const char *path, String &error) const {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    error = "Failed to open firmware for checksum";
    return 0;
  }

  uint32_t crc = 0xFFFFFFFFUL;
  uint8_t buffer[256];
  while (file.available() > 0) {
    size_t readBytes = file.read(buffer, sizeof(buffer));
    for (size_t index = 0; index < readBytes; ++index) {
      crc ^= buffer[index];
      for (int bit = 0; bit < 8; ++bit) {
        if (crc & 1U) {
          crc = (crc >> 1) ^ kCrc32Polynomial;
        } else {
          crc >>= 1;
        }
      }
    }
  }
  file.close();

  return ~crc;
}
