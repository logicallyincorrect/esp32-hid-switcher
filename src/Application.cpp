#include "Application.h"
#include "Board.h"
#include "BootHealth.h"
#include "DeviceShortcut.h"
#include "RuntimeHealth.h"
#include "UsbControlDiagnostics.h"
#include <esp_log.h>
#include <esp32-hal-rgb-led.h>

void Application::begin() {
  startRuntimeWatchdog();
  pinMode(Board::setupButton, INPUT_PULLUP);
  if (Board::slotLed >= 0) { pinMode(Board::slotLed, OUTPUT); digitalWrite(Board::slotLed, LOW); }
  esp_log_level_set("*", ESP_LOG_WARN);
  // Read the old selection only as a migration hint. BLE settings are authoritative.
  Preferences legacy;
  if (legacy.begin("multi-select", true)) { _slot = legacy.getUChar("slot", 0); legacy.end(); }
  if (_slot >= Board::slots) _slot = 0;
  _reports = xQueueCreate(64, sizeof(Report));
  assert(_reports);
  _ble.begin(_slot);
  _slot = _ble.selected();
  _usb.begin({this, keyboard, mouse});
  Serial.printf("[Ready] %s; hold BOOT for setup. UART: 1/2/3 select, ? status, u USB recovery.\n", _ble.deviceName().c_str());
}

void Application::discardInput(bool uncertain) {
  _ble.cancelCalibration(true);
  xQueueReset(_reports);
  _ble.releaseAll();
  _input.barrier(uncertain);
}

void Application::tick() {
  feedRuntimeWatchdog();
  const uint32_t started = micros();
  if (_previousLoop && started - _previousLoop > _maxLoopGap) _maxLoopGap = started - _previousLoop;
  _previousLoop = started;
  if (_usb.service()) discardInput(true);
  _ble.loop();
  serviceBootHealth(_usb.healthy());
  const bool wasMenu = _menu.active();
  const bool bootDown=digitalRead(Board::setupButton)==LOW;
  if(_ble.calibrationActive()&&bootDown){_ble.cancelCalibration();_calibrationBootRelease=true;}
  if(!bootDown)_calibrationBootRelease=false;
  if(!_ble.calibrationActive()&&!_calibrationBootRelease)_menu.button(bootDown,millis());
  const uint32_t menuStarted = micros();
  _menu.tick(millis());
  const uint32_t menuTime = micros() - menuStarted;
  if (menuTime > _maxMenu) _maxMenu = menuTime;
  if (wasMenu != _menu.active()) _input.barrier();
  if (_epoch != _ble.inputEpoch()) { _epoch = _ble.inputEpoch(); xQueueReset(_reports);if(!_ble.calibrationActive())_ble.releaseAll();_input.barrier(true); }
  if (_slot != _ble.selected()) {
    _slot = _ble.selected();
    xQueueReset(_reports);
    _input.barrier();
  }
  indicators();
  serialCommands();
  portENTER_CRITICAL(&_queueLock);
  const bool overflow = _overflow;
  _overflow = false;
  portEXIT_CRITICAL(&_queueLock);
  if (overflow) {
    _menu.cancel();
    discardInput(true);
    Serial.println("[Input] Queue full; waiting for release");
  }
  drainInput();
  // Preserve the established cadence: consume USB first, then submit BLE once.
  _ble.serviceEdges(!_menu.active()&&!_input.keyboardHeld&&!_input.buttons);
  _ble.flushInput();
  // Final calibration feedback has drained on its original connection. Resume
  // setup immediately so edge switching stays blocked between the two flows.
  if(_ble.takeCalibrationMenuReturn()){
    _menu.resume(millis(),_input.keys,_input.modifiers,_input.buttons);
    _input.barrier();
  }
  if (millis() - _lastStatus >= 5000) {
    _lastStatus = millis();
    _ble.printStatus();
    Serial.printf("[Loop timing] max_gap_us=%lu max_menu_us=%lu\n", (unsigned long)_maxLoopGap, (unsigned long)_maxMenu);
    _maxLoopGap = _maxMenu = 0;
  }
}

void Application::drainInput() {
  Report report;
  while (xQueueReceive(_reports, &report, 0) == pdTRUE) {
    if (report.mouse) {
      _input.buttons = report.movement.buttons;
      if(_ble.calibrationActive()){_ble.calibrationMouse(report.movement,report.received);_input.barrier();continue;}
      if (_menu.mouse(_input.buttons, millis())) { _input.barrier(); continue; }
      if (!_input.mouseBlocked && shortcut(true)) { _ble.releaseAll(); _input.barrier(); continue; }
      if(_ble.edgeMouse(report.movement,report.received,_input.keyboardHeld||_input.mouseBlocked))continue;
      report.movement.buttons = _input.forwardedButtons();
      _ble.sendMouseReport(report.movement, report.received);
    } else {
      _input.keyboard(report.bytes);
      if(_ble.calibrationActive()){
        for(auto key:_input.keys)if(key==41&&!_input.modifiers){_ble.cancelCalibration();break;}
        _input.barrier();continue;
      }
      if (_menu.keyboard(_input.keys, _input.modifiers, millis())) { _input.barrier(); continue; }
      if (_input.consumeKeyboard()) continue;
      if (shortcut(false)) { _input.keyboardBlocked = true; _ble.releaseAll(); continue; }
      _ble.sendKeyboardReport(_input.keys, _input.modifiers);
    }
  }
}

bool Application::shortcut(bool mouse) {
  const int action = _ble.shortcutAction(_input.keys, _input.modifiers,
      _input.mouseBlocked ? 0 : _input.buttons, mouse ? 2 : 0);
  if (action < 0) return false;
  uint8_t connected = 0;
  for (unsigned slot = 0; slot < Board::slots; ++slot) if (_ble.connected(slot)) connected |= 1u << slot;
  select(shortcutDestination(action, _ble.selected(), connected));
  return true;
}

void Application::select(uint8_t slot) {
  if (slot >= Board::slots) return;
  _ble.selectSlot(slot);
  _slot = _ble.selected();
  _input.mouseBlocked = _input.buttons != 0;
}

void Application::indicators() {
  const uint32_t now = millis(), phase = now % 2000;
  if (Board::slotLed >= 0) digitalWrite(Board::slotLed, phase < (_slot + 1) * 300u && phase % 300 < 120);
  const bool lit = _ble.isConnected() || now % 1000 < 350;
  const uint32_t colors[] = {0x000018, 0x001800, 0x140014};
  const uint32_t calibrationColor=_ble.calibrationColor(now);
  const uint32_t color = calibrationColor!=0xffffffff?calibrationColor:_menu.active() ? 0x101000 : lit ? colors[_slot] : 0;
  if (Board::rgbLed >= 0 && color != _previousColor) {
    rgbLedWrite(Board::rgbLed, (color >> 16) & 255, (color >> 8) & 255, color & 255);
    _previousColor = color;
  }
}

void Application::serialCommands() {
  while (Serial.available()) {
    const char command = Serial.read();
    if (command >= '1' && command <= '3') {
      _ble.cancelCalibration();_menu.cancel(); _input.barrier(); select(command - '1');
    } else if (command == 'u') _usb.requestRecovery();
    else if (command == 'v') toggleUsbDiagnostics();
    else if (command == '?') { _ble.printStatus(); _usb.printDiagnostics(); }
  }
}

void Application::enqueue(const Report &report) {
  if (xQueueSend(_reports, &report, 0) == pdTRUE) return;
  portENTER_CRITICAL(&_queueLock);
  _overflow = true;
  portEXIT_CRITICAL(&_queueLock);
}

void Application::keyboard(void *context, const uint8_t *data, size_t length) {
  if (length < 8) return;
  Report report;
  memcpy(report.bytes, data, sizeof(report.bytes));
  static_cast<Application *>(context)->enqueue(report);
}

void Application::mouse(void *context, const MouseReport &movement) {
  Report report;
  report.mouse = true; report.movement = movement; report.received = micros();
  static_cast<Application *>(context)->enqueue(report);
}
