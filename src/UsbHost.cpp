#include "UsbHost.h"
#include "RuntimeHealth.h"
#include "UsbControlDiagnostics.h"

void UsbHost::begin(Sink sink) {
  assert(!_connections);
  _sink = sink;
  _connections = xQueueCreate(10, sizeof(hid_host_device_handle_t));
  assert(_connections);
  _starter = xTaskGetCurrentTaskHandle();
  const auto started = xTaskCreatePinnedToCore(hostTask, "usb_events", 4096, this, 2, nullptr, 0);
  assert(started == pdPASS);
  // Never install HID against a host that failed to start.
  if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000))) abort();
  hid_host_driver_config_t driver = {};
  driver.create_background_task = true;
  driver.task_priority = 5;
  driver.stack_size = 4096;
  driver.core_id = 0;
  driver.callback = connected;
  driver.callback_arg = this;
  ESP_ERROR_CHECK(hid_host_install(&driver));
  const auto worker = xTaskCreate(connectionTask, "usb_connect", 4096, this, 2, nullptr);
  assert(worker == pdPASS);
}

void UsbHost::hostTask(void *context) {
  auto &self = *static_cast<UsbHost *>(context);
  esp_task_wdt_add(nullptr);
  usb_host_config_t config = {};
  config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  config.enum_filter_cb = [](const usb_device_desc_t *device, uint8_t *configuration) {
    *configuration = 1;
    const auto kind = device->bDeviceClass;
    if (usbDiagnosticsEnabled()) Serial.printf("[USB enumerate] vid=%04x pid=%04x class=%02x configs=%u\n",
                  device->idVendor, device->idProduct, kind, device->bNumConfigurations);
    return kind == 0 || kind == 3 || kind == 9 || kind == 0xef;
  };
  ESP_ERROR_CHECK(usb_host_install(&config));
  xTaskNotifyGive(self._starter);
  for (;;) {
    uint32_t events = 0;
    const auto result = usb_host_lib_handle_events(pdMS_TO_TICKS(100), &events);
    if (result != ESP_OK && result != ESP_ERR_TIMEOUT) self.fault();
    feedRuntimeWatchdog();
    portENTER_CRITICAL(&self._lock);
    self._usbHeartbeat = millis();
    portEXIT_CRITICAL(&self._lock);
    if (events & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    self.servicePortRecovery();
  }
}

void UsbHost::connected(hid_host_device_handle_t handle, hid_host_driver_event_t event, void *context) {
  if (event != HID_HOST_DRIVER_EVENT_CONNECTED) return;
  auto &self = *static_cast<UsbHost *>(context);
  if (xQueueSend(self._connections, &handle, 0) == pdTRUE) return;
  portENTER_CRITICAL(&self._lock);
  ++self._eventDrops;
  self._fault = true;
  portEXIT_CRITICAL(&self._lock);
}

void UsbHost::connectionTask(void *context) {
  auto &self = *static_cast<UsbHost *>(context);
  esp_task_wdt_add(nullptr);
  for (;;) {
    feedRuntimeWatchdog();
    portENTER_CRITICAL(&self._lock);
    self._hidHeartbeat = millis();
    portEXIT_CRITICAL(&self._lock);
    hid_host_device_handle_t handle = nullptr;
    if (xQueueReceive(self._connections, &handle, pdMS_TO_TICKS(50)) == pdTRUE) self.open(handle);
  }
}

void UsbHost::track(hid_host_device_handle_t handle, const hid_host_dev_params_t &params, bool active) {
  portENTER_CRITICAL(&_lock);
  Interface *entry = nullptr;
  for (auto &item : _interfaces) if (item.handle == handle) { entry = &item; break; }
  if (!entry) for (auto &item : _interfaces) if (!item.handle) { entry = &item; break; }
  if (entry) { entry->handle = handle; entry->params = params; entry->active = active; }
  portEXIT_CRITICAL(&_lock);
}

void UsbHost::open(hid_host_device_handle_t handle) {
  hid_host_dev_params_t params = {};
  if (hid_host_device_get_params(handle, &params) != ESP_OK) return;
  // Track only started interfaces. Rejected/closed optional interfaces have no
  // disconnect callback to retire their handles, so retaining them leaks slots.
  hid_host_device_config_t config = {};
  config.callback = report;
  config.callback_arg = this;
  const esp_err_t opened = hid_host_device_open(handle, &config);
  if (opened != ESP_OK) {
    portENTER_CRITICAL(&_lock);
    ++_openErrors;
    portEXIT_CRITICAL(&_lock);
    // An optional interface can exceed the controller's channel capacity.
    // Resetting the bus cannot add channels and drops working input devices.
    Serial.printf("[USB] Interface address=%u interface=%u unavailable: %s; keeping active devices\n",
                  params.addr, params.iface_num, esp_err_to_name(opened));
    return;
  }
  const bool keyboard = params.proto == HID_PROTOCOL_KEYBOARD && params.sub_class == HID_SUBCLASS_BOOT_INTERFACE;
  bool mouse = false;
  if (!keyboard) {
    size_t length = 0;
    const auto *descriptor = hid_host_get_report_descriptor(handle, &length);
    HidMouseParser parser;
    const bool supported = descriptor && parser.parse(descriptor, length);
    if (supported) {
      portENTER_CRITICAL(&_lock);
      mouse = _mice.attach(handle, parser);
      portEXIT_CRITICAL(&_lock);
    }
    if (!mouse) { hid_host_device_close(handle); return; }
  }
  if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
    hid_class_request_set_protocol(handle, keyboard ? HID_REPORT_PROTOCOL_BOOT : HID_REPORT_PROTOCOL_REPORT);
    if (keyboard) hid_class_request_set_idle(handle, 0, 0);
  }
  if (hid_host_device_start(handle) == ESP_OK) {
    track(handle, params, true);
    Serial.printf("[USB] %s address=%u interface=%u ready\n", keyboard ? "Keyboard" : "Mouse", params.addr, params.iface_num);
    return;
  }
  portENTER_CRITICAL(&_lock);
  MouseReport released;
  if (mouse) _mice.detach(handle, released);
  portEXIT_CRITICAL(&_lock);
  hid_host_device_close(handle);
  fault();
}

void UsbHost::report(hid_host_device_handle_t handle, hid_host_interface_event_t event, void *context) {
  static_cast<UsbHost *>(context)->input(handle, event);
}

void UsbHost::input(hid_host_device_handle_t handle, hid_host_interface_event_t event) {
  if (event == HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR) { fault(); return; }
  hid_host_dev_params_t params = {};
  if (hid_host_device_get_params(handle, &params) != ESP_OK) return;
  const bool keyboard = params.proto == HID_PROTOCOL_KEYBOARD && params.sub_class == HID_SUBCLASS_BOOT_INTERFACE;
  MouseReport movement;
  bool sendMouse = false;
  if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
    // Close in the driver callback: after close the handle is no longer valid.
    portENTER_CRITICAL(&_lock);
    for (auto &entry : _interfaces) if (entry.handle == handle) entry = {};
    sendMouse = _mice.detach(handle, movement);
    portEXIT_CRITICAL(&_lock);
    const uint8_t released[8] = {};
    if (keyboard && _sink.keyboard) _sink.keyboard(_sink.context, released, sizeof(released));
    if (sendMouse && _sink.mouse) _sink.mouse(_sink.context, movement);
    hid_host_device_close(handle);
    return;
  }
  if (event != HID_HOST_INTERFACE_EVENT_INPUT_REPORT) return;
  uint8_t bytes[64];
  size_t length = 0;
  if (hid_host_device_get_raw_input_report_data(handle, bytes, sizeof(bytes), &length) != ESP_OK) { fault(); return; }
  portENTER_CRITICAL(&_lock);
  for (auto &entry : _interfaces) if (entry.handle == handle) ++entry.reports;
  sendMouse = _mice.decode(handle, bytes, length, movement);
  portEXIT_CRITICAL(&_lock);
  if (sendMouse && _sink.mouse) _sink.mouse(_sink.context, movement);
  if (keyboard && _sink.keyboard) _sink.keyboard(_sink.context, bytes, length);
}

void UsbHost::fault() {
  portENTER_CRITICAL(&_lock);
  ++_transferErrors;
  _fault = true;
  portEXIT_CRITICAL(&_lock);
}

void UsbHost::requestRecovery() {
  portENTER_CRITICAL(&_lock);
  _fault = true;
  portEXIT_CRITICAL(&_lock);
}

bool UsbHost::service() {
  portENTER_CRITICAL(&_lock);
  const bool pending = _fault;
  _fault = false;
  portEXIT_CRITICAL(&_lock);
  if (pending) _recovery.fault();
  if (!_recovery.poll(millis())) return false;
  portENTER_CRITICAL(&_lock);
  _resetRequested = true;
  portEXIT_CRITICAL(&_lock);
  return true;
}

// Only the USB event task touches port power. In particular, do not race the
// hub task's reset/debounce/control-transfer handling from the Arduino loop.
void UsbHost::servicePortRecovery() {
  const uint32_t now = millis();
  if (_portOff) {
    usb_host_lib_info_t info = {};
    if (now - _portOffAt >= 100 && usb_host_lib_info(&info) == ESP_OK && info.num_devices == 0) {
      if (usb_host_lib_set_root_port_power(true) == ESP_OK) {
        portENTER_CRITICAL(&_lock);
        _portOff = false;
        portEXIT_CRITICAL(&_lock);
      }
    }
    return;
  }
  portENTER_CRITICAL(&_lock);
  const bool requested = _resetRequested;
  portEXIT_CRITICAL(&_lock);
  if (requested && usb_host_lib_set_root_port_power(false) == ESP_OK) {
    _portOffAt = now;
    portENTER_CRITICAL(&_lock);
    _portOff = true;
    _resetRequested = false;
    portEXIT_CRITICAL(&_lock);
  }
}

bool UsbHost::healthy() {
  portENTER_CRITICAL(&_lock);
  const uint32_t usb = _usbHeartbeat, hid = _hidHeartbeat;
  const bool restarting = _portOff || _resetRequested;
  portEXIT_CRITICAL(&_lock);
  const uint32_t now = millis();
  return usb && hid && now - usb < 2000 && now - hid < 2000 && !restarting;
}

void UsbHost::appendStatus(JsonObject out) {
  usb_host_lib_info_t hostInfo = {};
  const auto infoResult = usb_host_lib_info(&hostInfo);
  Interface snapshot[16];
  uint32_t transfer, open, drops;
  bool restarting;
  portENTER_CRITICAL(&_lock);
  memcpy(snapshot, _interfaces, sizeof(snapshot));
  transfer = _transferErrors; open = _openErrors; drops = _eventDrops;
  restarting = _portOff || _resetRequested;
  portEXIT_CRITICAL(&_lock);
  out["healthy"] = healthy(); out["transfer_errors"] = transfer; out["open_errors"] = open;
  // 'healthy' is task liveness, not proof that a hub or HID device enumerated.
  out["tasks_running"] = healthy();
  if (infoResult == ESP_OK) out["host_devices"] = hostInfo.num_devices;
  out["event_drops"] = drops; out["recoveries"] = _recovery.attempts;
  out["recovering"] = restarting || _recovery.pending;
  auto list = out["interfaces"].to<JsonArray>();
  for (const auto &entry : snapshot) if (entry.handle) {
    auto item = list.add<JsonObject>();
    item["address"] = entry.params.addr; item["interface"] = entry.params.iface_num;
    item["vid"] = entry.params.vid; item["pid"] = entry.params.pid;
    item["kind"] = entry.params.proto == 1 ? "Keyboard" : entry.params.proto == 2 ? "Mouse" : "Other HID";
    item["active"] = entry.active; item["reports"] = entry.reports;
  }
}

void UsbHost::printDiagnostics() {
  JsonDocument status;
  appendStatus(status.to<JsonObject>());
  Serial.print("[USB] "); serializeJson(status, Serial); Serial.println();
}
