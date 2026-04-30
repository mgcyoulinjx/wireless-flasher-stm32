#include "stm32_flasher.h"

namespace {
constexpr size_t kChunkSize = 256;
}

bool Stm32Flasher::flash(HardwareSerial &serialPort,
                         const FlashManifest &manifest,
                         fs::FS &fs,
                         const char *firmwarePath,
                         FlashProgressCallback progressCallback,
                         ChipDetectCallback chipDetectCallback,
                         void *context,
                         String &error) {
  File firmware = fs.open(firmwarePath, FILE_READ);
  if (!firmware) {
    error = "Firmware file is missing";
    return false;
  }

  if (firmware.size() != manifest.size) {
    error = "Firmware size does not match manifest";
    return false;
  }

  while (serialPort.available() > 0) {
    serialPort.read();
  }

  if (!sync(serialPort, error)) {
    return false;
  }

  uint16_t chipId = 0;
  if (!getChipId(serialPort, chipId, error)) {
    return false;
  }
  if (chipDetectCallback) {
    chipDetectCallback(chipId, context);
  }

  if (!erase(serialPort, error)) {
    return false;
  }

  uint8_t buffer[kChunkSize];
  size_t totalWritten = 0;
  uint32_t address = manifest.address;

  while (totalWritten < manifest.size) {
    size_t bytesToRead = manifest.size - totalWritten;
    if (bytesToRead > kChunkSize) {
      bytesToRead = kChunkSize;
    }

    size_t readBytes = firmware.read(buffer, bytesToRead);
    if (readBytes != bytesToRead) {
      error = "Failed to read the firmware file";
      return false;
    }

    if (!writeMemory(serialPort, address, buffer, readBytes, error)) {
      return false;
    }

    totalWritten += readBytes;
    address += static_cast<uint32_t>(readBytes);

    if (progressCallback && !progressCallback(totalWritten, manifest.size, "Writing firmware over UART", context)) {
      error = "Flashing cancelled";
      return false;
    }
  }

  if (!go(serialPort, manifest.address, error)) {
    return false;
  }

  return true;
}

bool Stm32Flasher::sync(HardwareSerial &serialPort, String &error) {
  serialPort.write(0x7F);
  return expectAck(serialPort, 1000, error);
}

bool Stm32Flasher::getChipId(HardwareSerial &serialPort, uint16_t &chipId, String &error) {
  if (!sendCommand(serialPort, 0x02, error)) {
    return false;
  }

  int length = serialPort.readBytes(reinterpret_cast<char *>(&chipId), 0);
  (void)length;

  uint32_t start = millis();
  while (serialPort.available() < 3) {
    if (millis() - start > 1000) {
      error = "Timed out while reading chip ID";
      return false;
    }
    delay(1);
  }

  uint8_t count = serialPort.read();
  if (count < 1) {
    error = "Unexpected chip ID response";
    return false;
  }

  uint8_t msb = serialPort.read();
  uint8_t lsb = serialPort.read();
  chipId = static_cast<uint16_t>((msb << 8) | lsb);

  if (!expectAck(serialPort, 1000, error)) {
    return false;
  }

  return true;
}

bool Stm32Flasher::erase(HardwareSerial &serialPort, String &error) {
  if (sendCommand(serialPort, 0x44, error)) {
    uint8_t payload[] = {0xFF, 0xFF, 0x00};
    serialPort.write(payload, sizeof(payload));
    if (expectAck(serialPort, 30000, error)) {
      return true;
    }
  }

  error = "Extended erase failed, trying global erase";
  if (!sendCommand(serialPort, 0x43, error)) {
    return false;
  }

  uint8_t payload[] = {0xFF, 0x00};
  serialPort.write(payload, sizeof(payload));
  return expectAck(serialPort, 30000, error);
}

bool Stm32Flasher::writeMemory(HardwareSerial &serialPort, uint32_t address, const uint8_t *data, size_t length, String &error) {
  if (length == 0 || length > kChunkSize) {
    error = "Invalid write size";
    return false;
  }

  if (!sendCommand(serialPort, 0x31, error)) {
    return false;
  }

  if (!sendAddress(serialPort, address, error)) {
    return false;
  }

  uint8_t header = static_cast<uint8_t>(length - 1);
  uint8_t checksum = header;
  serialPort.write(header);

  for (size_t index = 0; index < length; ++index) {
    serialPort.write(data[index]);
    checksum ^= data[index];
  }

  serialPort.write(checksum);
  return expectAck(serialPort, 5000, error);
}

bool Stm32Flasher::go(HardwareSerial &serialPort, uint32_t address, String &error) {
  if (!sendCommand(serialPort, 0x21, error)) {
    return false;
  }

  return sendAddress(serialPort, address, error);
}

bool Stm32Flasher::expectAck(HardwareSerial &serialPort, uint32_t timeoutMs, String &error) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (serialPort.available() > 0) {
      uint8_t response = static_cast<uint8_t>(serialPort.read());
      if (response == kAck) {
        return true;
      }
      if (response == kNack) {
        error = "Target bootloader rejected the command";
        return false;
      }
      error = "Unexpected response from target bootloader";
      return false;
    }
    delay(1);
  }

  error = "Timed out waiting for target bootloader";
  return false;
}

bool Stm32Flasher::sendCommand(HardwareSerial &serialPort, uint8_t command, String &error) {
  uint8_t payload[] = {command, static_cast<uint8_t>(command ^ 0xFF)};
  serialPort.write(payload, sizeof(payload));
  return expectAck(serialPort, 1000, error);
}

bool Stm32Flasher::sendAddress(HardwareSerial &serialPort, uint32_t address, String &error) {
  uint8_t bytes[5] = {
      static_cast<uint8_t>((address >> 24) & 0xFF),
      static_cast<uint8_t>((address >> 16) & 0xFF),
      static_cast<uint8_t>((address >> 8) & 0xFF),
      static_cast<uint8_t>(address & 0xFF),
      0,
  };
  bytes[4] = bytes[0] ^ bytes[1] ^ bytes[2] ^ bytes[3];
  serialPort.write(bytes, sizeof(bytes));
  return expectAck(serialPort, 1000, error);
}
