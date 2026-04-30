#include "uart_boot_backend.h"

#include <HardwareSerial.h>
#include "stm32_flasher.h"

UartBootBackend::UartBootBackend(HardwareSerial &serialPort, Stm32Flasher &flasher)
    : serialPort_(serialPort), flasher_(flasher) {}

FlashTransport UartBootBackend::transport() const {
  return FlashTransport::Uart;
}

const char *UartBootBackend::transportName() const {
  return "uart";
}

bool UartBootBackend::flash(const FlashManifest &manifest,
                            fs::FS &fs,
                            const char *firmwarePath,
                            FlashProgressCallback progressCallback,
                            ChipDetectCallback chipDetectCallback,
                            void *context,
                            String &error) {
  return flasher_.flash(serialPort_, manifest, fs, firmwarePath, progressCallback, chipDetectCallback, context, error);
}
