#pragma once

#include <Arduino.h>

class SwdTransport {
public:
  SwdTransport(int ioPin, int clockPin);

  void begin();
  void lineReset();
  bool switchToSwd();
  bool readDp(uint8_t address, uint32_t &value, String &error);
  bool writeDp(uint8_t address, uint32_t value, String &error);
  bool readAp(uint8_t address, uint32_t &value, String &error);
  bool writeAp(uint8_t address, uint32_t value, String &error);
  bool powerUpDebug(String &error);
  bool clearStickyErrors(String &error);
  void sampleLineLevels(String &message);

private:
  int ioPin_;
  int clockPin_;

  void clockCycle();
  void writeBit(bool level);
  bool readBit();
  void writeTurnaround();
  void readTurnaround();
  void recoverAfterFailedTransfer(uint8_t ack);
  uint8_t transfer(uint8_t request, uint32_t *data, bool read, String &error);
};
