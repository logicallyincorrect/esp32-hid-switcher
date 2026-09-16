#pragma once
#include "BleHid.h"
#include "UsbHost.h"
#include "MenuBackend.h"
#include "DeviceMenu.h"
#include "InputState.h"

class Application {
public:
  Application() : _backend(_ble, _usb), _menu(_backend) {}
  void begin();
  void tick();
  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;
private:
  struct Report {
    bool mouse = false;
    uint8_t bytes[8] = {};
    MouseReport movement;
    uint32_t received = 0;
  };
  BleHid _ble;
  UsbHost _usb;
  MenuBackend _backend;
  DeviceMenu<MenuBackend> _menu;
  InputState _input;
  QueueHandle_t _reports = nullptr;
  portMUX_TYPE _queueLock = portMUX_INITIALIZER_UNLOCKED;
  bool _overflow = false;
  uint8_t _slot = 0;
  uint32_t _epoch = 0, _previousLoop = 0, _maxLoopGap = 0, _maxMenu = 0, _lastStatus = 0;
  uint32_t _previousColor = 0xffffffff;
  void enqueue(const Report &report);
  void drainInput();
  void select(uint8_t slot);
  bool shortcut(bool mouse);
  void indicators();
  void serialCommands();
  void discardInput(bool uncertain);
  static void keyboard(void *context, const uint8_t *data, size_t length);
  static void mouse(void *context, const MouseReport &movement);
};
