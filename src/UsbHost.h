#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "hid_host.h"
#include "usb/usb_host.h"
#include "UsbMice.h"
#include "RecoveryPolicy.h"

// One instance owns the board's native USB controller for the firmware lifetime.
// The sink must copy reports and return without waiting for another task.
class UsbHost {
public:
  struct Sink {
    void *context;
    void (*keyboard)(void *, const uint8_t *, size_t);
    void (*mouse)(void *, const MouseReport &);
  };
  void begin(Sink sink);
  bool service();
  bool healthy();
  void requestRecovery();
  void appendStatus(JsonObject out);
  void printDiagnostics();
  UsbHost() = default;
  UsbHost(const UsbHost &) = delete;
  UsbHost &operator=(const UsbHost &) = delete;
private:
  struct Interface {
    hid_host_device_handle_t handle = nullptr;
    hid_host_dev_params_t params = {};
    bool active = false;
    uint32_t reports = 0;
  };
  Sink _sink = {};
  Interface _interfaces[16];
  UsbMice<hid_host_device_handle_t> _mice;
  portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
  QueueHandle_t _connections = nullptr;
  TaskHandle_t _starter = nullptr;
  uint32_t _usbHeartbeat = 0, _hidHeartbeat = 0;
  uint32_t _transferErrors = 0, _openErrors = 0, _eventDrops = 0;
  bool _fault = false, _portOff = false;
  uint32_t _portOffAt = 0;
  RecoveryPolicy _recovery;
  void fault();
  void track(hid_host_device_handle_t handle, const hid_host_dev_params_t &params, bool active);
  void open(hid_host_device_handle_t handle);
  void input(hid_host_device_handle_t handle, hid_host_interface_event_t event);
  static void hostTask(void *context);
  static void connectionTask(void *context);
  static void connected(hid_host_device_handle_t handle, hid_host_driver_event_t event, void *context);
  static void report(hid_host_device_handle_t handle, hid_host_interface_event_t event, void *context);
};
