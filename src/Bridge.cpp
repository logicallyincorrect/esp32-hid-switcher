#include "Bridge.h"
#include "DeviceShortcut.h"
#include <hid_usage_keyboard.h>
#include <esp32-hal-rgb-led.h>
#include "DeviceMenu.h"
#include "MenuBackend.h"
#include "BootHealth.h"
#include <esp_log.h>
#include "RuntimeHealth.h"

uint8_t Bridge::_currentSlot = 0;
BLEManager Bridge::_bleManager;
Preferences Bridge::_preferences;
static QueueHandle_t reports;
static portMUX_TYPE reportMux = portMUX_INITIALIZER_UNLOCKED;
static bool reportOverflow = false;
static bool shortcutHeld = false;
static bool keyboardHeld = false, mouseBlocked = false;
static uint8_t mouseButtonsHeld = 0;
static uint8_t keyboardModifiers=0,keyboardKeys[6]={};
struct Report { bool mouse=false; uint8_t bytes[8]={}; MouseReport movement; uint32_t received=0; };

void Bridge::begin() {
  startRuntimeWatchdog();
  pinMode(0,INPUT_PULLUP);
  // Per-report stack logging adds latency. Keep debug code available in the
  // SDK, but only emit warnings/errors during normal keyboard/mouse use.
  esp_log_level_set("*",ESP_LOG_WARN);
  _preferences.begin("multi-select", false);
  _currentSlot = _preferences.getUChar("slot", 0);
  if (_currentSlot >= NUM_DEVICE_SLOTS) _currentSlot = 0;
  reports = xQueueCreate(64, sizeof(Report));
  assert(reports);
  _bleManager.begin(_currentSlot);
  _currentSlot=_bleManager.selected();
  USBManager::setKeyboardCallback(onKeyboardReport);
  USBManager::setMouseCallback(onMouseReport);
  USBManager::begin();
  Serial.printf("[Setup] Pair %s on computer 1, select slot 2 and pair computer 2, then slot 3.\n",_bleManager.deviceName().c_str());
  Serial.println("[Setup] Use configured switch shortcuts. Defaults: Ctrl+Cmd+1/2/3 or Tab. UART digits select; ? shows status.");
}

void Bridge::loop() {
  feedRuntimeWatchdog();
  if(USBManager::service()){_bleManager.releaseAll();xQueueReset(reports);shortcutHeld=keyboardHeld;mouseBlocked=mouseButtonsHeld!=0;}
  static uint32_t previousLoop=0,maxLoopGap=0,maxMenu=0;
  const uint32_t loopStart=micros();
  if(previousLoop && loopStart-previousLoop>maxLoopGap)maxLoopGap=loopStart-previousLoop;
  previousLoop=loopStart;

  static MenuBackend backend(_bleManager);
  static DeviceMenu<MenuBackend> menu(backend);
  _bleManager.loop();
  serviceBootHealth(USBManager::healthy());
  const bool wasMenu=menu.active();
  menu.button(digitalRead(0)==LOW,millis());
  const uint32_t menuStart=micros();
  menu.tick(millis());
  const uint32_t menuTime=micros()-menuStart;if(menuTime>maxMenu)maxMenu=menuTime;
  if(wasMenu!=menu.active()){shortcutHeld=keyboardHeld;mouseBlocked=mouseButtonsHeld!=0;}
  static uint32_t inputEpoch=0;
  if(inputEpoch!=_bleManager.inputEpoch()){
    inputEpoch=_bleManager.inputEpoch();xQueueReset(reports);
    _bleManager.releaseAll();shortcutHeld=true;mouseBlocked=true;
  }
  if(_currentSlot!=_bleManager.selected()) {
    _currentSlot=_bleManager.selected();shortcutHeld=keyboardHeld;mouseBlocked=mouseButtonsHeld!=0;
    xQueueReset(reports);
  }
  // Freenove GPIO2 LED pulses the slot number; RGB48 gives ready/offline feedback.
  const uint32_t phase = millis() % 2000;
  if (LED_FEEDBACK_PIN >= 0)
    digitalWrite(LED_FEEDBACK_PIN, phase < (_currentSlot + 1) * 300 && phase % 300 < 120);
  const bool lit = _bleManager.isConnected() || (millis() % 1000 < 350);
  const uint32_t color = menu.active()?0x101000:lit ? (_currentSlot == 0 ? 0x000018 : (_currentSlot == 1 ? 0x001800 : 0x140014)) : 0;
  static uint32_t previousColor = 0xffffffff;
  if (LED_RGB_PIN >= 0 && color != previousColor) {
    rgbLedWrite(LED_RGB_PIN, (color >> 16) & 255, (color >> 8) & 255, color & 255);
    previousColor = color;
  }
  while (Serial.available()) {
    const char command = Serial.read();
    if (command >= '1' && command <= '3') {
      menu.cancel();shortcutHeld = keyboardHeld;
      switchToSlot(command - '1');
    }
    else if(command=='u')USBManager::requestRecovery();
    else if (command == '?') {
      _bleManager.printStatus();
      USBManager::printDiagnostics();
    }
  }
  portENTER_CRITICAL(&reportMux); bool overflow = reportOverflow; reportOverflow = false; portEXIT_CRITICAL(&reportMux);
  if (overflow) {
    menu.cancel();xQueueReset(reports);
    _bleManager.releaseAll();
    shortcutHeld = keyboardHeld;mouseBlocked=true;
    Serial.println("[USB] Input queue overflow; released keys, waiting for physical release");
  }
  Report report;
  while (xQueueReceive(reports, &report, 0) == pdTRUE) {
    if(report.mouse) {
      mouseButtonsHeld=report.movement.buttons;
      if(menu.mouse(mouseButtonsHeld,millis())){
        shortcutHeld=keyboardHeld;mouseBlocked=mouseButtonsHeld!=0;continue;
      }
      if(!mouseBlocked&&checkDeviceSwitchCombo(keyboardKeys,keyboardModifiers,true)){
        _bleManager.releaseAll();shortcutHeld=keyboardHeld;mouseBlocked=mouseButtonsHeld!=0;continue;
      }
      if(mouseBlocked){if(report.movement.buttons==0)mouseBlocked=false;else report.movement.buttons=0;}
      _bleManager.sendMouseReport(report.movement,report.received);continue;
    }
    bool released = report.bytes[0] == 0;
    for (int i = 2; i < 8; ++i) released &= report.bytes[i] == 0;
    keyboardHeld=!released;
    keyboardModifiers=report.bytes[0];memcpy(keyboardKeys,report.bytes+2,6);
    if(menu.keyboard(keyboardKeys,keyboardModifiers,millis())){
      shortcutHeld=keyboardHeld;mouseBlocked=mouseButtonsHeld!=0;continue;
    }
    if (shortcutHeld) {
      if (released) shortcutHeld = false;
      continue;
    }
    if (checkDeviceSwitchCombo(report.bytes + 2, report.bytes[0])) {
      shortcutHeld = true;
      _bleManager.releaseAll();
      continue;
    }
    _bleManager.sendKeyboardReport(report.bytes + 2, report.bytes[0]);
  }
  // Drain new USB input before submitting to BLE in this same loop iteration.
  _bleManager.flushInput();
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus >= 5000) {
    lastStatus = millis(); _bleManager.printStatus();
    Serial.printf("[Loop timing] max_gap_us=%lu max_menu_us=%lu\n",(unsigned long)maxLoopGap,(unsigned long)maxMenu);
    maxLoopGap=maxMenu=0;
  }
}

void Bridge::switchToSlot(uint8_t slot) {
  if (slot >= NUM_DEVICE_SLOTS) return;
  _bleManager.selectSlot(slot);
  mouseBlocked=mouseButtonsHeld!=0;
  _currentSlot=_bleManager.selected();
}

void Bridge::onKeyboardReport(const uint8_t *data, size_t length) {
  if (length < 8) return;
  Report report; memcpy(report.bytes, data, 8);
  if (xQueueSend(reports, &report, 0) != pdTRUE) {
    portENTER_CRITICAL(&reportMux); reportOverflow = true; portEXIT_CRITICAL(&reportMux);
  }
}

bool Bridge::checkDeviceSwitchCombo(const uint8_t *keys, uint8_t modifiers, bool mouseEvent) {
  if (!ENABLE_DEVICE_SWITCHING) return false;
  const int action=_bleManager.shortcutAction(keys,modifiers,mouseBlocked?0:mouseButtonsHeld,mouseEvent?2:0);
  if(action<0)return false;
  uint8_t connectedMask=0;
  for(unsigned i=0;i<NUM_DEVICE_SLOTS;++i)if(_bleManager.connected(i))connectedMask|=1u<<i;
  const unsigned slot=shortcutDestination(action,_bleManager.selected(),connectedMask);
  Serial.printf("[Shortcut] Action %d -> slot %u\n",action,slot+1);
  switchToSlot(slot);
  return true;
}

void Bridge::onMouseReport(const MouseReport &mouse) {
  Report report;report.mouse=true;report.movement=mouse;report.received=micros();
  if(xQueueSend(reports,&report,0)!=pdTRUE){portENTER_CRITICAL(&reportMux);reportOverflow=true;portEXIT_CRITICAL(&reportMux);}
}
