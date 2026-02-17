#include <HalGPIO.h>
#include <SPI.h>
#include <esp_sleep.h>

void HalGPIO::begin() {
  inputMgr.begin();
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);
  pinMode(BAT_GPIO0, INPUT);
  pinMode(UART0_RXD, INPUT);
}

void HalGPIO::update() {
  inputMgr.update();

  // Process BLE virtual button state with edge detection
  blePageTurner.update();
  const uint8_t bleState = blePageTurner.getVirtualButtonState();
  blePressedEvents = bleState & ~bleCurrentState;
  bleReleasedEvents = bleCurrentState & ~bleState;
  bleCurrentState = bleState;
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  return inputMgr.isPressed(buttonIndex) || (bleCurrentState & (1 << buttonIndex));
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  return inputMgr.wasPressed(buttonIndex) || (blePressedEvents & (1 << buttonIndex));
}

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed() || blePressedEvents > 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  return inputMgr.wasReleased(buttonIndex) || (bleReleasedEvents & (1 << buttonIndex));
}

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased() || bleReleasedEvents > 0; }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

void HalGPIO::startDeepSleep() {
  // Shut down BLE before sleeping to avoid power drain
  blePageTurner.end();

  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  while (inputMgr.isPressed(BTN_POWER)) {
    delay(50);
    inputMgr.update();
  }
  // Arm the wakeup trigger *after* the button is released
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

int HalGPIO::getBatteryPercentage() const {
  static const BatteryMonitor battery = BatteryMonitor(BAT_GPIO0);
  return battery.readPercentage();
}

bool HalGPIO::isUsbConnected() const {
  // U0RXD/GPIO20 reads HIGH when USB is connected
  return digitalRead(UART0_RXD) == HIGH;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const bool usbConnected = isUsbConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  if ((wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) ||
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP && usbConnected)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
