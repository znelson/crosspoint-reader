#include "BlePageTurner.h"

#include <Logging.h>
#include <NimBLEDevice.h>

static constexpr char HID_SERVICE_UUID[] = "1812";
static constexpr char HID_REPORT_DATA_UUID[] = "2A4D";
static constexpr char HID_REPORT_MAP_UUID[] = "2A4B";

// HID keyboard keycodes (USB HID Usage Table, Section 10)
static constexpr uint8_t KEY_RIGHT_ARROW = 0x4F;
static constexpr uint8_t KEY_LEFT_ARROW = 0x50;
static constexpr uint8_t KEY_DOWN_ARROW = 0x51;
static constexpr uint8_t KEY_UP_ARROW = 0x52;
static constexpr uint8_t KEY_PAGE_DOWN = 0x4E;
static constexpr uint8_t KEY_PAGE_UP = 0x4B;
static constexpr uint8_t KEY_ENTER = 0x28;
static constexpr uint8_t KEY_SPACE = 0x2C;

// Button bit positions matching InputManager/HalGPIO
static constexpr uint8_t BTN_UP_BIT = (1 << 4);
static constexpr uint8_t BTN_DOWN_BIT = (1 << 5);

// BLE appearance category for Human Interface Devices (0x03C0-0x03FF)
static constexpr uint16_t BLE_APPEARANCE_HID_MIN = 0x03C0;
static constexpr uint16_t BLE_APPEARANCE_HID_MAX = 0x03FF;

// Singleton pointer for static NimBLE callbacks
static BlePageTurner* g_instance = nullptr;

uint8_t BlePageTurner::mapKeycode(const uint8_t keycode) {
  switch (keycode) {
    case KEY_RIGHT_ARROW:
    case KEY_DOWN_ARROW:
    case KEY_PAGE_DOWN:
    case KEY_SPACE:
    case KEY_ENTER:
      return BTN_DOWN_BIT;  // Maps to BTN_DOWN -> PageForward via MappedInputManager
    case KEY_LEFT_ARROW:
    case KEY_UP_ARROW:
    case KEY_PAGE_UP:
      return BTN_UP_BIT;  // Maps to BTN_UP -> PageBack via MappedInputManager
    default:
      return 0;
  }
}

// ---- NimBLE callbacks ----

static void onHidReport(NimBLERemoteCharacteristic*, uint8_t* pData, size_t length, bool) {
  if (!g_instance || length == 0) return;

  uint8_t buttons = 0;

  if (length >= 3) {
    // Standard HID keyboard report: [modifier, reserved, key1..key6]
    for (size_t i = 2; i < length && i < 8; i++) {
      if (pData[i] != 0) {
        buttons |= BlePageTurner::mapKeycode(pData[i]);
      }
    }
  } else {
    // Short reports from simple page turners
    for (size_t i = 0; i < length; i++) {
      if (pData[i] != 0) {
        buttons |= BlePageTurner::mapKeycode(pData[i]);
      }
    }
  }

  g_instance->virtualButtons.store(buttons, std::memory_order_relaxed);
}

class PageTurnerClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient*) override { LOG_DBG("BLE", "Connected to HID device"); }

  void onDisconnect(NimBLEClient*, int reason) override {
    LOG_INF("BLE", "Disconnected, reason=%d", reason);
    if (!g_instance) return;

    g_instance->virtualButtons.store(0, std::memory_order_relaxed);
    g_instance->state.store(BlePageTurner::State::Disconnected, std::memory_order_relaxed);

    if (!g_instance->targetAddress.empty()) {
      NimBLEDevice::getScan()->start(0, false);
    }
  }

  void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t pin) override {
    LOG_DBG("BLE", "Confirm passkey: %lu", static_cast<unsigned long>(pin));
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    if (!connInfo.isEncrypted()) {
      LOG_ERR("BLE", "Encryption failed, disconnecting");
      NimBLEDevice::getClientByHandle(connInfo.getConnHandle())->disconnect();
    } else {
      LOG_DBG("BLE", "Authentication complete");
    }
  }
};

static PageTurnerClientCallbacks clientCB;

class PageTurnerScanCallbacks : public NimBLEScanCallbacks {
  static bool looksLikeHidDevice(const NimBLEAdvertisedDevice* pDevice) {
    if (pDevice->isAdvertisingService(NimBLEUUID(HID_SERVICE_UUID))) return true;
    if (pDevice->haveAppearance()) {
      const uint16_t appearance = pDevice->getAppearance();
      if (appearance >= BLE_APPEARANCE_HID_MIN && appearance <= BLE_APPEARANCE_HID_MAX) return true;
    }
    return false;
  }

  void onResult(const NimBLEAdvertisedDevice* pDevice) override {
    if (!g_instance) return;
    if (!looksLikeHidDevice(pDevice)) return;

    const auto addr = pDevice->getAddress().toString();
    const auto name = pDevice->haveName() ? pDevice->getName() : std::string("HID Device");
    LOG_DBG("BLE", "Found HID device: %s [%s]", name.c_str(), addr.c_str());

    if (g_instance->pairingMode) {
      std::lock_guard<std::mutex> lock(g_instance->devicesMutex);
      for (const auto& d : g_instance->discoveredDevices) {
        if (d.address == addr) return;
      }
      g_instance->discoveredDevices.push_back({name, addr, pDevice->getAddress().getType()});
    } else if (addr == g_instance->targetAddress) {
      LOG_INF("BLE", "Found target device, connecting...");
      NimBLEDevice::getScan()->stop();
      g_instance->advDevice = pDevice;
      g_instance->doConnect.store(true, std::memory_order_relaxed);
    }
  }
};

static PageTurnerScanCallbacks scanCB;

// ---- Internal helpers ----

bool BlePageTurner::subscribeToHidReports(void* clientPtr) {
  auto* pClient = static_cast<NimBLEClient*>(clientPtr);
  auto* pSvc = pClient->getService(HID_SERVICE_UUID);
  if (!pSvc) {
    LOG_ERR("BLE", "HID service not found");
    return false;
  }

  auto* pReportMap = pSvc->getCharacteristic(HID_REPORT_MAP_UUID);
  if (pReportMap && pReportMap->canRead()) {
    auto value = pReportMap->readValue();
    LOG_DBG("BLE", "HID Report Map: %d bytes", static_cast<int>(value.length()));
  }

  // Subscribe to all HID Report Data characteristics.
  bool subscribed = false;
  const auto& chars = pSvc->getCharacteristics(true);
  for (auto* chr : chars) {
    if (chr->getUUID() == NimBLEUUID(HID_REPORT_DATA_UUID) && chr->canNotify()) {
      if (chr->subscribe(true, onHidReport)) {
        LOG_DBG("BLE", "Subscribed to HID report handle=0x%04x", chr->getHandle());
        subscribed = true;
      } else {
        LOG_ERR("BLE", "Subscribe failed for handle=0x%04x", chr->getHandle());
      }
    }
  }

  return subscribed;
}

bool BlePageTurner::connectToServer() {
  auto* pAdv = static_cast<const NimBLEAdvertisedDevice*>(advDevice);
  if (!pAdv) return false;

  state.store(State::Connecting, std::memory_order_relaxed);

  NimBLEClient* pClient = nullptr;

  if (NimBLEDevice::getCreatedClientCount() > 0) {
    pClient = NimBLEDevice::getClientByPeerAddress(pAdv->getAddress());
    if (pClient) {
      if (!pClient->connect(pAdv, false)) {
        LOG_ERR("BLE", "Reconnect failed");
        return false;
      }
      LOG_DBG("BLE", "Reconnected existing client");
    } else {
      pClient = NimBLEDevice::getDisconnectedClient();
    }
  }

  if (!pClient) {
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(&clientCB, false);
    pClient->setConnectionParams(24, 48, 0, 200);
    pClient->setConnectTimeout(10);

    if (!pClient->connect(pAdv)) {
      NimBLEDevice::deleteClient(pClient);
      LOG_ERR("BLE", "Connect failed");
      return false;
    }
  }

  if (!pClient->isConnected()) {
    if (!pClient->connect(pAdv)) {
      LOG_ERR("BLE", "Connect retry failed");
      return false;
    }
  }

  LOG_INF("BLE", "Connected to %s RSSI=%d", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());

  if (!subscribeToHidReports(pClient)) {
    pClient->disconnect();
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(devicesMutex);
    connectedName = pAdv->haveName() ? pAdv->getName() : pAdv->getAddress().toString();
  }

  state.store(State::Connected, std::memory_order_relaxed);
  return true;
}

// ---- Public API ----

void BlePageTurner::begin() {
  if (initialized) return;

  g_instance = this;
  NimBLEDevice::init("CrossPoint");

  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setPower(3);

  auto* pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCB, false);
  pScan->setInterval(100);
  pScan->setWindow(50);
  pScan->setActiveScan(true);

  initialized = true;
  state.store(State::Idle, std::memory_order_relaxed);
  LOG_INF("BLE", "Initialized");
}

void BlePageTurner::end() {
  if (!initialized) return;

  virtualButtons.store(0, std::memory_order_relaxed);
  NimBLEDevice::getScan()->stop();

  for (auto* c : NimBLEDevice::getConnectedClients()) {
    c->disconnect();
  }

  NimBLEDevice::deinit(true);
  initialized = false;
  g_instance = nullptr;
  state.store(State::Disabled, std::memory_order_relaxed);
  LOG_INF("BLE", "Deinitialized");
}

void BlePageTurner::update() {
  if (!initialized) return;

  if (doConnect.exchange(false, std::memory_order_relaxed)) {
    if (!connectToServer()) {
      LOG_ERR("BLE", "Connection failed, restarting scan");
      state.store(State::Disconnected, std::memory_order_relaxed);
      startTargetedScan();
    }
  }
}

void BlePageTurner::startPairingScan() {
  if (!initialized) return;

  pairingMode = true;
  {
    std::lock_guard<std::mutex> lock(devicesMutex);
    discoveredDevices.clear();
  }

  state.store(State::Scanning, std::memory_order_relaxed);
  NimBLEDevice::getScan()->start(0, false);
  LOG_DBG("BLE", "Pairing scan started");
}

void BlePageTurner::stopScan() {
  if (!initialized) return;

  NimBLEDevice::getScan()->stop();
  pairingMode = false;
  if (state.load() == State::Scanning) {
    state.store(State::Idle, std::memory_order_relaxed);
  }
}

void BlePageTurner::startTargetedScan() {
  if (!initialized || targetAddress.empty()) return;

  pairingMode = false;
  state.store(State::Scanning, std::memory_order_relaxed);
  NimBLEDevice::getScan()->start(0, false);
  LOG_DBG("BLE", "Targeted scan for %s", targetAddress.c_str());
}

void BlePageTurner::connectToAddress(const std::string& address, uint8_t addressType) {
  if (!initialized) return;

  stopScan();
  targetAddress = address;
  targetAddressType = addressType;
  pairingMode = false;

  state.store(State::Connecting, std::memory_order_relaxed);

  NimBLEClient* pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(&clientCB, false);
  pClient->setConnectionParams(24, 48, 0, 200);
  pClient->setConnectTimeout(10);

  NimBLEAddress addr(address, addressType);
  if (!pClient->connect(addr)) {
    LOG_ERR("BLE", "Direct connect failed, falling back to scan");
    NimBLEDevice::deleteClient(pClient);
    startTargetedScan();
    return;
  }

  if (!subscribeToHidReports(pClient)) {
    LOG_ERR("BLE", "Subscribe failed after direct connect");
    pClient->disconnect();
    state.store(State::Disconnected, std::memory_order_relaxed);
    startTargetedScan();
    return;
  }

  {
    std::lock_guard<std::mutex> lock(devicesMutex);
    connectedName = pClient->getPeerAddress().toString();
  }
  state.store(State::Connected, std::memory_order_relaxed);
  LOG_INF("BLE", "Connected and subscribed");
}

void BlePageTurner::autoConnect(const std::string& address) {
  if (!initialized || address.empty()) return;

  targetAddress = address;
  pairingMode = false;
  startTargetedScan();
}

void BlePageTurner::disconnect() {
  if (!initialized) return;

  targetAddress.clear();
  NimBLEDevice::getScan()->stop();

  for (auto* c : NimBLEDevice::getConnectedClients()) {
    c->disconnect();
  }

  virtualButtons.store(0, std::memory_order_relaxed);
  state.store(State::Idle, std::memory_order_relaxed);
}

void BlePageTurner::forgetDevice() {
  disconnect();
  NimBLEDevice::deleteAllBonds();
  targetAddress.clear();
  LOG_INF("BLE", "Forgot all paired devices");
}

std::vector<BlePageTurner::DiscoveredDevice> BlePageTurner::getDiscoveredDevices() const {
  std::lock_guard<std::mutex> lock(devicesMutex);
  return discoveredDevices;
}

std::string BlePageTurner::getConnectedDeviceName() const {
  std::lock_guard<std::mutex> lock(devicesMutex);
  return connectedName;
}
