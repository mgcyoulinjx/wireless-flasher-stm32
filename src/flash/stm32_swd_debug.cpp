#include "stm32_swd_debug.h"

#include "hal/swd_transport.h"
#include "hal/target_control.h"

namespace {
constexpr uint8_t kApCsw = 0x00;
constexpr uint8_t kApTar = 0x04;
constexpr uint8_t kApDrw = 0x0C;
constexpr uint32_t kCswHalfWord = 0x23000051;
constexpr uint32_t kCswWord = 0x23000052;
constexpr uint32_t kDhcsr = 0xE000EDF0;
constexpr uint32_t kAircr = 0xE000ED0C;
constexpr uint32_t kDbgmcuIdcode = 0xE0042000;
}

Stm32SwdDebug::Stm32SwdDebug(SwdTransport &transport, TargetControl &targetControl)
    : transport_(transport), targetControl_(targetControl) {}

bool Stm32SwdDebug::connect(String &error) {
  transport_.begin();
  transport_.setFastMode(false);
  uint32_t idcode = 0;
  String lastError;
  String history;

  for (int attempt = 0; attempt < 6; ++attempt) {
    transport_.switchToSwd();
    if (transport_.readDp(0x00, idcode, lastError)) {
      cachedDpId_ = idcode;
      hasCachedDpId_ = true;
      if (!transport_.powerUpDebug(error)) {
        return false;
      }
      transport_.setFastMode(true);
      return true;
    }
    history += "try" + String(attempt + 1) + " read: " + lastError + "; ";
    delay(5);
  }

  error = history;
  return false;
}

void Stm32SwdDebug::sampleLineLevels(String &message) {
  transport_.sampleLineLevels(message);
}

bool Stm32SwdDebug::readDebugPortId(uint32_t &idcode, String &error) {
  if (hasCachedDpId_) {
    idcode = cachedDpId_;
    return true;
  }
  return transport_.readDp(0x00, idcode, error);
}

bool Stm32SwdDebug::readStm32DebugId(uint32_t &idcode, String &error) {
  return readMemory32(kDbgmcuIdcode, idcode, error);
}

bool Stm32SwdDebug::readMemory32(uint32_t address, uint32_t &value, String &error) {
  if (!writeApRegister(kApCsw, kCswWord, error)) {
    return false;
  }
  if (!writeApRegister(kApTar, address, error)) {
    return false;
  }
  return readApRegister(kApDrw, value, error);
}

bool Stm32SwdDebug::readMemory32Block(uint32_t address, uint8_t *data, size_t length, String &error) {
  if ((address & 0x3U) != 0 || (length & 0x3U) != 0) {
    error = "MEM-AP word block reads require alignment";
    return false;
  }
  if (length == 0) {
    return true;
  }
  if (!writeApRegister(kApCsw, kCswWord, error)) {
    return false;
  }

  constexpr size_t kReadBatchWords = 128;
  uint32_t words[kReadBatchWords];
  size_t offset = 0;
  while (offset < length) {
    const uint32_t currentAddress = address + offset;
    size_t segmentBytes = min(length - offset, static_cast<size_t>(1024 - (currentAddress & 0x3FFU)));
    segmentBytes &= ~static_cast<size_t>(0x3U);
    if (segmentBytes == 0) {
      error = "MEM-AP word block segment is empty";
      return false;
    }
    if (!writeApRegister(kApTar, currentAddress, error)) {
      return false;
    }
    if (!selectApBank(kApDrw >> 4, error)) {
      return false;
    }

    size_t segmentOffset = 0;
    while (segmentOffset < segmentBytes) {
      const size_t remainingWords = (segmentBytes - segmentOffset) / 4;
      const size_t batchWords = min(kReadBatchWords, remainingWords);
      if (!transport_.readApBlock(kApDrw & 0x0C, words, batchWords, error)) {
        return false;
      }
      for (size_t index = 0; index < batchWords; ++index) {
        const uint32_t value = words[index];
        const size_t dataOffset = offset + segmentOffset + index * 4;
        data[dataOffset] = static_cast<uint8_t>(value & 0xFFU);
        data[dataOffset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
        data[dataOffset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
        data[dataOffset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
      }
      segmentOffset += batchWords * 4;
    }
    offset += segmentBytes;
  }
  return true;
}

bool Stm32SwdDebug::writeMemory32(uint32_t address, uint32_t value, String &error) {
  if (!writeApRegister(kApCsw, kCswWord, error)) {
    return false;
  }
  if (!writeApRegister(kApTar, address, error)) {
    return false;
  }
  return writeApRegister(kApDrw, value, error);
}

bool Stm32SwdDebug::writeMemory32Block(uint32_t address, const uint8_t *data, size_t length, String &error) {
  if ((address & 0x3U) != 0 || (length & 0x3U) != 0) {
    error = "MEM-AP word block writes require alignment";
    return false;
  }
  if (length == 0) {
    return true;
  }
  if (!writeApRegister(kApCsw, kCswWord, error)) {
    return false;
  }

  constexpr size_t kWriteBatchWords = 128;
  uint32_t words[kWriteBatchWords];
  size_t offset = 0;
  while (offset < length) {
    const uint32_t currentAddress = address + offset;
    size_t segmentBytes = min(length - offset, static_cast<size_t>(1024 - (currentAddress & 0x3FFU)));
    segmentBytes &= ~static_cast<size_t>(0x3U);
    if (segmentBytes == 0) {
      error = "MEM-AP word block segment is empty";
      return false;
    }
    if (!writeApRegister(kApTar, currentAddress, error)) {
      return false;
    }
    if (!selectApBank(kApDrw >> 4, error)) {
      return false;
    }

    size_t segmentOffset = 0;
    while (segmentOffset < segmentBytes) {
      const size_t remainingWords = (segmentBytes - segmentOffset) / 4;
      const size_t batchWords = min(kWriteBatchWords, remainingWords);
      for (size_t index = 0; index < batchWords; ++index) {
        const size_t dataOffset = offset + segmentOffset + index * 4;
        words[index] = static_cast<uint32_t>(data[dataOffset]) |
                       (static_cast<uint32_t>(data[dataOffset + 1]) << 8) |
                       (static_cast<uint32_t>(data[dataOffset + 2]) << 16) |
                       (static_cast<uint32_t>(data[dataOffset + 3]) << 24);
      }
      if (!transport_.writeApBlock(kApDrw & 0x0C, words, batchWords, error)) {
        return false;
      }
      segmentOffset += batchWords * 4;
    }
    offset += segmentBytes;
  }
  return true;
}

bool Stm32SwdDebug::writeMemory16(uint32_t address, uint16_t value, String &error) {
  if (!writeApRegister(kApCsw, kCswHalfWord, error)) {
    return false;
  }
  if (!writeApRegister(kApTar, address, error)) {
    return false;
  }
  const uint32_t shiftedValue = static_cast<uint32_t>(value) << ((address & 0x2U) * 8U);
  return writeApRegister(kApDrw, shiftedValue, error);
}

bool Stm32SwdDebug::writeMemory16Block(uint32_t address, const uint8_t *data, size_t length, String &error) {
  if ((address & 0x1U) != 0 || (length & 0x1U) != 0) {
    error = "MEM-AP half-word block writes require alignment";
    return false;
  }
  if (length == 0) {
    return true;
  }
  if (!writeApRegister(kApCsw, kCswHalfWord, error)) {
    return false;
  }

  constexpr size_t kWriteBatchHalfWords = 128;
  uint32_t values[kWriteBatchHalfWords];
  size_t offset = 0;
  while (offset < length) {
    const uint32_t currentAddress = address + offset;
    size_t segmentBytes = min(length - offset, static_cast<size_t>(1024 - (currentAddress & 0x3FFU)));
    segmentBytes &= ~static_cast<size_t>(0x1U);
    if (segmentBytes == 0) {
      error = "MEM-AP half-word block segment is empty";
      return false;
    }
    if (!writeApRegister(kApTar, currentAddress, error)) {
      return false;
    }
    if (!selectApBank(kApDrw >> 4, error)) {
      return false;
    }

    size_t segmentOffset = 0;
    while (segmentOffset < segmentBytes) {
      const size_t remainingHalfWords = (segmentBytes - segmentOffset) / 2;
      const size_t batchHalfWords = min(kWriteBatchHalfWords, remainingHalfWords);
      for (size_t index = 0; index < batchHalfWords; ++index) {
        const size_t dataOffset = offset + segmentOffset + index * 2;
        const uint32_t halfWord = static_cast<uint32_t>(data[dataOffset]) |
                                  (static_cast<uint32_t>(data[dataOffset + 1]) << 8);
        values[index] = halfWord << (((currentAddress + segmentOffset + index * 2) & 0x2U) * 8U);
      }
      if (!transport_.writeApBlock(kApDrw & 0x0C, values, batchHalfWords, error)) {
        return false;
      }
      segmentOffset += batchHalfWords * 2;
    }
    offset += segmentBytes;
  }
  return true;
}

bool Stm32SwdDebug::halt(String &error) {
  return writeMemory32(kDhcsr, 0xA05F0003, error);
}

bool Stm32SwdDebug::run(String &error) {
  return writeMemory32(kDhcsr, 0xA05F0001, error);
}

bool Stm32SwdDebug::reset(String &error) {
  targetControl_.resetTarget();
  return writeMemory32(kAircr, 0x05FA0004, error);
}

bool Stm32SwdDebug::selectApBank(uint8_t apBank, String &error) {
  return transport_.writeDp(0x08, static_cast<uint32_t>(apBank) << 4, error);
}

bool Stm32SwdDebug::readApRegister(uint8_t address, uint32_t &value, String &error) {
  if (!selectApBank(address >> 4, error)) {
    return false;
  }
  return transport_.readAp(address & 0x0C, value, error);
}

bool Stm32SwdDebug::writeApRegister(uint8_t address, uint32_t value, String &error) {
  if (!selectApBank(address >> 4, error)) {
    return false;
  }
  return transport_.writeAp(address & 0x0C, value, error);
}
