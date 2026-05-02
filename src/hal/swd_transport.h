#pragma once

#include <Arduino.h>

class SwdTransport {
public:
  SwdTransport(int ioPin, int clockPin);

  void begin();
  void setFastMode(bool enabled);
  void lineReset();
  bool switchToSwd();
  bool readDp(uint8_t address, uint32_t &value, String &error);
  bool writeDp(uint8_t address, uint32_t value, String &error);
  bool readAp(uint8_t address, uint32_t &value, String &error);
  bool readApBlock(uint8_t address, uint32_t *values, size_t count, String &error);
  bool writeAp(uint8_t address, uint32_t value, String &error);
  bool writeApBlock(uint8_t address, const uint32_t *values, size_t count, String &error);
  bool powerUpDebug(String &error);
  bool clearStickyErrors(String &error);
  void sampleLineLevels(String &message);

private:
  int ioPin_;
  int clockPin_;
  uint32_t ioMask_;
  uint32_t clockMask_;
  bool fastMode_;

  inline void clockHi() { GPIO.out_w1ts = clockMask_; }
  inline void clockLo() { GPIO.out_w1tc = clockMask_; }
  inline void ioHi() { GPIO.out_w1ts = ioMask_; }
  inline void ioLo() { GPIO.out_w1tc = ioMask_; }
  inline bool ioLevel() { return (GPIO.in & ioMask_) != 0; }
  inline void ioOutputFast() { GPIO.enable_w1ts = ioMask_; }
  inline void ioInputFast() { GPIO.enable_w1tc = ioMask_; }

  void ioOutput();
  void ioInput();
  void clockCycle();
  void writeBit(bool level);
  bool readBit();
  void writeTurnaround();
  void readTurnaround();
  void recoverAfterFailedTransfer(uint8_t ack);
  uint8_t transfer(uint8_t request, uint32_t *data, bool read, String &error);
};
