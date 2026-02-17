#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class BlePageTurner {
 public:
  enum class State : uint8_t {
    Disabled,      // BLE stack not initialized
    Idle,          // Initialized but not scanning or connected
    Scanning,      // Scanning for HID peripherals
    Connecting,    // Connection in progress
    Connected,     // Receiving HID reports
    Disconnected,  // Lost connection, will auto-reconnect if target set
  };

  struct DiscoveredDevice {
    std::string name;
    std::string address;
    uint8_t addressType;
  };

  BlePageTurner() = default;

  // Lifecycle
  void begin();
  void end();

  // Call from main loop to process deferred connections from scan callbacks
  void update();

  // Scanning / pairing
  void startPairingScan();
  void stopScan();
  void connectToAddress(const std::string& address, uint8_t addressType);
  void disconnect();
  void forgetDevice();

  // Auto-connect to a previously paired device address.
  void autoConnect(const std::string& address);

  // Virtual button state (bitmask compatible with InputManager button indices).
  // Thread-safe; updated from BLE callback, read from main loop.
  uint8_t getVirtualButtonState() const { return virtualButtons.load(std::memory_order_relaxed); }

  State getState() const { return state.load(std::memory_order_relaxed); }
  bool isConnected() const { return getState() == State::Connected; }

  // Access discovered devices (protected by mutex, copy out)
  std::vector<DiscoveredDevice> getDiscoveredDevices() const;
  std::string getConnectedDeviceName() const;

  // Maps a HID keyboard keycode to a virtual button bitmask bit.
  // Returns 0 if the keycode is not mapped. Public for use by notification callback.
  static uint8_t mapKeycode(uint8_t keycode);

  // Fields accessed by NimBLE callbacks (must be public for static callback access)
  std::atomic<State> state{State::Disabled};
  std::atomic<uint8_t> virtualButtons{0};
  std::atomic<bool> doConnect{false};
  const void* advDevice = nullptr;

  mutable std::mutex devicesMutex;
  std::vector<DiscoveredDevice> discoveredDevices;

  std::string targetAddress;
  bool pairingMode = false;

 private:
  std::string connectedName;
  uint8_t targetAddressType = 0;
  bool initialized = false;

  void startTargetedScan();
  bool connectToServer();
  bool subscribeToHidReports(void* client);
};
