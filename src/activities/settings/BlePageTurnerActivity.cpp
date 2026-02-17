#include "BlePageTurnerActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BlePageTurnerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  screen = Screen::Main;
  selectedIndex = 0;
  buildMenuItems();
  requestUpdate();
}

void BlePageTurnerActivity::onExit() { ActivityWithSubactivity::onExit(); }

void BlePageTurnerActivity::buildMenuItems() {
  menuItems.clear();

  const auto bleState = blePageTurner.getState();
  const bool hasPairedDevice = strlen(SETTINGS.bleDeviceAddress) > 0;

  // Status line
  std::string statusValue;
  if (bleState == BlePageTurner::State::Connected) {
    statusValue = std::string(tr(STR_BLE_CONNECTED)) + ": " + blePageTurner.getConnectedDeviceName();
  } else if (bleState == BlePageTurner::State::Scanning || bleState == BlePageTurner::State::Connecting) {
    statusValue = tr(STR_BLE_CONNECTING);
  } else if (hasPairedDevice) {
    statusValue = std::string(tr(STR_BLE_DISCONNECTED)) + ": " + std::string(SETTINGS.bleDeviceAddress);
  } else {
    statusValue = tr(STR_BLE_NOT_PAIRED);
  }
  menuItems.push_back({"Status", statusValue});

  // Pair new device
  menuItems.push_back({tr(STR_BLE_PAIR_NEW), ""});

  // Forget device (only if paired)
  if (hasPairedDevice) {
    menuItems.push_back({tr(STR_BLE_FORGET), ""});
  }
}

void BlePageTurnerActivity::handleMainAction() {
  if (selectedIndex == 0) {
    // Status line: no action
    return;
  }

  if (selectedIndex == 1) {
    // Pair new device: start scanning
    screen = Screen::Scanning;
    selectedIndex = 0;
    blePageTurner.startPairingScan();
    requestUpdate();
    return;
  }

  if (selectedIndex == 2) {
    // Forget device
    blePageTurner.forgetDevice();
    SETTINGS.bleDeviceAddress[0] = '\0';
    SETTINGS.saveToFile();
    selectedIndex = 0;
    buildMenuItems();
    requestUpdate();
  }
}

void BlePageTurnerActivity::loop() {
  if (screen == Screen::Scanning) {
    // In scanning mode
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      blePageTurner.stopScan();
      screen = Screen::Main;
      selectedIndex = 0;
      buildMenuItems();
      requestUpdate();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      // Select the highlighted device
      auto devices = blePageTurner.getDiscoveredDevices();
      if (selectedIndex >= 0 && selectedIndex < static_cast<int>(devices.size())) {
        const auto& dev = devices[selectedIndex];
        blePageTurner.stopScan();

        // Store the address and connect
        strncpy(SETTINGS.bleDeviceAddress, dev.address.c_str(), sizeof(SETTINGS.bleDeviceAddress) - 1);
        SETTINGS.bleDeviceAddress[sizeof(SETTINGS.bleDeviceAddress) - 1] = '\0';
        SETTINGS.saveToFile();

        blePageTurner.connectToAddress(dev.address, dev.addressType);

        screen = Screen::Main;
        selectedIndex = 0;
        buildMenuItems();
        requestUpdate();
        return;
      }
    }

    // Navigation in scan results
    auto devices = blePageTurner.getDiscoveredDevices();
    const int deviceCount = static_cast<int>(devices.size());

    buttonNavigator.onNextRelease([this, deviceCount] {
      if (deviceCount > 0) {
        selectedIndex = ButtonNavigator::nextIndex(selectedIndex, deviceCount);
      }
      requestUpdate();
    });

    buttonNavigator.onPreviousRelease([this, deviceCount] {
      if (deviceCount > 0) {
        selectedIndex = ButtonNavigator::previousIndex(selectedIndex, deviceCount);
      }
      requestUpdate();
    });

    // Periodically refresh to show newly discovered devices
    static unsigned long lastRefresh = 0;
    if (millis() - lastRefresh > 1000) {
      lastRefresh = millis();
      requestUpdate();
    }

    return;
  }

  // Main screen
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleMainAction();
    return;
  }

  const int itemCount = static_cast<int>(menuItems.size());

  buttonNavigator.onNextRelease([this, itemCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, itemCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount);
    requestUpdate();
  });
}

void BlePageTurnerActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  auto metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_BLE_PAGE_TURNER_SETTINGS));

  const int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (screen == Screen::Scanning) {
    auto devices = blePageTurner.getDiscoveredDevices();
    const int deviceCount = static_cast<int>(devices.size());

    if (deviceCount == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, contentY + contentHeight / 2 - 20, tr(STR_BLE_SCANNING), true);
      renderer.drawCenteredText(UI_10_FONT_ID, contentY + contentHeight / 2 + 10, tr(STR_BLE_NO_DEVICES), true);
    } else {
      GUI.drawList(
          renderer, Rect{0, contentY, pageWidth, contentHeight}, deviceCount, selectedIndex,
          [&devices](int i) { return devices[i].name; },
          [&devices](int i) { return devices[i].address; }, nullptr, nullptr);
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), deviceCount > 0 ? tr(STR_SELECT) : "", tr(STR_DIR_UP),
                                              tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    // Main screen
    const int itemCount = static_cast<int>(menuItems.size());

    GUI.drawList(
        renderer, Rect{0, contentY, pageWidth, contentHeight}, itemCount, selectedIndex,
        [this](int i) { return menuItems[i].label; }, nullptr, nullptr,
        [this](int i) { return menuItems[i].value; });

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
