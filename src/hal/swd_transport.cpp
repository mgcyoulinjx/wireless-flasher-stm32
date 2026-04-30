#include "swd_transport.h"

namespace {
constexpr uint8_t kAckOk = 0b001;
constexpr uint8_t kAckWait = 0b010;
constexpr uint8_t kAckFault = 0b100;
constexpr uint8_t kDpAbort = 0x00;
constexpr uint8_t kDpSelect = 0x08;
constexpr uint8_t kDpCtrlStat = 0x04;
constexpr uint8_t kDpRdbuff = 0x0C;
constexpr uint32_t kDpPowerUpReq = 0x50000000UL;
constexpr uint32_t kDpPowerUpAck = 0xA0000000UL;
}

SwdTransport::SwdTransport(int ioPin, int clockPin) : ioPin_(ioPin), clockPin_(clockPin) {}

void SwdTransport::begin() {
  pinMode(clockPin_, OUTPUT);
  digitalWrite(clockPin_, HIGH);
  pinMode(ioPin_, INPUT);
}

void SwdTransport::lineReset() {
  pinMode(ioPin_, OUTPUT);
  digitalWrite(ioPin_, HIGH);
  for (int i = 0; i < 60; ++i) {
    clockCycle();
  }
}

bool SwdTransport::switchToSwd() {
  lineReset();
  static constexpr uint16_t sequence = 0xE79E;
  pinMode(ioPin_, OUTPUT);
  for (int i = 0; i < 16; ++i) {
    writeBit((sequence >> i) & 0x1);
  }
  lineReset();
  pinMode(ioPin_, OUTPUT);
  digitalWrite(ioPin_, LOW);
  for (int i = 0; i < 8; ++i) {
    clockCycle();
  }
  pinMode(ioPin_, INPUT);
  return true;
}

uint8_t makeRequest(bool ap, bool read, uint8_t address) {
  const uint8_t a2 = (address >> 2) & 0x1;
  const uint8_t a3 = (address >> 3) & 0x1;
  const uint8_t parity = (ap ? 1 : 0) ^ (read ? 1 : 0) ^ a2 ^ a3;
  return static_cast<uint8_t>(0x81 | (ap ? 0x02 : 0x00) | (read ? 0x04 : 0x00) | (a2 << 3) | (a3 << 4) | (parity << 5));
}

bool SwdTransport::readDp(uint8_t address, uint32_t &value, String &error) {
  for (int attempt = 0; attempt < 50; ++attempt) {
    uint32_t data = 0;
    uint8_t ack = transfer(makeRequest(false, true, address), &data, true, error);
    if (ack == kAckOk) {
      value = data;
      return true;
    }
    if (ack != kAckWait) {
      return false;
    }
    delayMicroseconds(50);
  }
  error = "SWD WAIT timeout";
  return false;
}

bool SwdTransport::writeDp(uint8_t address, uint32_t value, String &error) {
  for (int attempt = 0; attempt < 50; ++attempt) {
    uint32_t data = value;
    uint8_t ack = transfer(makeRequest(false, false, address), &data, false, error);
    if (ack == kAckOk) {
      return true;
    }
    if (ack != kAckWait) {
      return false;
    }
    delayMicroseconds(50);
  }
  error = "SWD WAIT timeout";
  return false;
}

bool SwdTransport::readAp(uint8_t address, uint32_t &value, String &error) {
  for (int attempt = 0; attempt < 50; ++attempt) {
    uint32_t data = 0;
    uint8_t ack = transfer(makeRequest(true, true, address), &data, true, error);
    if (ack == kAckOk) {
      return readDp(kDpRdbuff, value, error);
    }
    if (ack != kAckWait) {
      return false;
    }
    delayMicroseconds(50);
  }
  error = "SWD WAIT timeout";
  return false;
}

bool SwdTransport::writeAp(uint8_t address, uint32_t value, String &error) {
  for (int attempt = 0; attempt < 50; ++attempt) {
    uint32_t data = value;
    uint8_t ack = transfer(makeRequest(true, false, address), &data, false, error);
    if (ack == kAckOk) {
      return true;
    }
    if (ack != kAckWait) {
      return false;
    }
    delayMicroseconds(50);
  }
  error = "SWD WAIT timeout";
  return false;
}

bool SwdTransport::powerUpDebug(String &error) {
  String history;
  for (int attempt = 0; attempt < 30; ++attempt) {
    String abortError;
    writeDp(kDpAbort, 0x1E, abortError);
    if (!writeDp(kDpCtrlStat, kDpPowerUpReq, error)) {
      history += "write" + String(attempt + 1) + ": " + error + "; ";
      switchToSwd();
      delay(2);
      continue;
    }
    uint32_t status = 0;
    if (readDp(kDpCtrlStat, status, error)) {
      if ((status & kDpPowerUpAck) == kDpPowerUpAck) {
        return true;
      }
      history += "status" + String(attempt + 1) + ": 0x" + String(status, HEX) + "; ";
    } else {
      history += "read" + String(attempt + 1) + ": " + error + "; ";
      switchToSwd();
    }
    delay(2);
  }
  error = "SWD power-up timeout: " + history;
  return false;
}

bool SwdTransport::clearStickyErrors(String &error) {
  return writeDp(kDpAbort, 0x1E, error);
}

void SwdTransport::sampleLineLevels(String &message) {
  pinMode(clockPin_, OUTPUT);
  digitalWrite(clockPin_, LOW);
  pinMode(ioPin_, INPUT);
  delayMicroseconds(20);
  const int pullup = digitalRead(ioPin_);
  digitalWrite(ioPin_, LOW);
  pinMode(ioPin_, OUTPUT);
  delayMicroseconds(20);
  const int drivenLow = digitalRead(ioPin_);
  digitalWrite(ioPin_, HIGH);
  delayMicroseconds(20);
  const int drivenHigh = digitalRead(ioPin_);
  pinMode(ioPin_, INPUT);
  message = "SWD line sample: input_pullup=" + String(pullup) + ", drive_low=" + String(drivenLow) + ", drive_high=" + String(drivenHigh);
}

void SwdTransport::clockCycle() {
  digitalWrite(clockPin_, LOW);
  delayMicroseconds(0);
  digitalWrite(clockPin_, HIGH);
  delayMicroseconds(0);
}

void SwdTransport::writeBit(bool level) {
  digitalWrite(ioPin_, level ? HIGH : LOW);
  digitalWrite(clockPin_, LOW);
  delayMicroseconds(0);
  digitalWrite(clockPin_, HIGH);
  delayMicroseconds(0);
}

bool SwdTransport::readBit() {
  digitalWrite(clockPin_, LOW);
  delayMicroseconds(0);
  bool level = digitalRead(ioPin_);
  digitalWrite(clockPin_, HIGH);
  delayMicroseconds(0);
  return level;
}

void SwdTransport::writeTurnaround() {
  pinMode(ioPin_, INPUT);
  clockCycle();
  pinMode(ioPin_, OUTPUT);
}

void SwdTransport::readTurnaround() {
  pinMode(ioPin_, INPUT);
  clockCycle();
}

void SwdTransport::recoverAfterFailedTransfer(uint8_t ack) {
  if (ack == kAckWait || ack == kAckFault || ack == kAckOk) {
    clockCycle();
    pinMode(ioPin_, OUTPUT);
    digitalWrite(ioPin_, HIGH);
    return;
  }
  pinMode(ioPin_, OUTPUT);
  digitalWrite(ioPin_, HIGH);
  for (int i = 0; i < 8; ++i) {
    clockCycle();
  }
  pinMode(ioPin_, INPUT);
}

uint8_t SwdTransport::transfer(uint8_t request, uint32_t *data, bool read, String &error) {
  pinMode(ioPin_, OUTPUT);
  uint8_t parity = 0;
  writeBit(true);
  writeBit((request >> 1) & 0x1);
  writeBit((request >> 2) & 0x1);
  writeBit((request >> 3) & 0x1);
  writeBit((request >> 4) & 0x1);
  parity = ((request >> 1) & 0x1) ^ ((request >> 2) & 0x1) ^ ((request >> 3) & 0x1) ^ ((request >> 4) & 0x1);
  writeBit(parity & 0x1);
  writeBit(false);
  writeBit(true);

  readTurnaround();
  bool ack0 = readBit();
  bool ack1 = readBit();
  bool ack2 = readBit();
  uint8_t ack = 0;
  ack |= ack0 ? 0x1 : 0x0;
  ack |= ack1 ? 0x2 : 0x0;
  ack |= ack2 ? 0x4 : 0x0;

  if (ack == kAckWait) {
    error = "SWD WAIT response";
    recoverAfterFailedTransfer(ack);
    return ack;
  }
  if (ack == kAckFault) {
    error = "SWD FAULT response";
    recoverAfterFailedTransfer(ack);
    return ack;
  }
  if (ack != kAckOk) {
    error = "SWD invalid ACK 0b" + String((ack >> 2) & 0x1) + String((ack >> 1) & 0x1) + String(ack & 0x1);
    recoverAfterFailedTransfer(ack);
    return ack;
  }

  uint32_t value = 0;
  uint8_t dataParity = 0;
  if (read) {
    for (int bit = 0; bit < 32; ++bit) {
      bool level = readBit();
      if (level) {
        value |= (1UL << bit);
        dataParity ^= 1;
      }
    }
    bool parityBit = readBit();
    if (parityBit != (dataParity & 0x1)) {
      error = "SWD parity mismatch";
    }
    writeTurnaround();
    if (data) {
      *data = value;
    }
  } else {
    writeTurnaround();
    pinMode(ioPin_, OUTPUT);
    value = data ? *data : 0;
    for (int bit = 0; bit < 32; ++bit) {
      bool level = (value >> bit) & 0x1;
      if (level) {
        dataParity ^= 1;
      }
      writeBit(level);
    }
    writeBit(dataParity & 0x1);
  }

  pinMode(ioPin_, INPUT);
  return ack;
}
