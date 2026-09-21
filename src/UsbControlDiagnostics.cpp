#include <Arduino.h>
#include "usb_private.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "UsbControlDiagnostics.h"
#include <atomic>

static std::atomic<bool> verbose{false};
bool usbDiagnosticsEnabled() { return verbose.load(); }
void toggleUsbDiagnostics() {
  const bool enabled = !verbose.load();
  verbose.store(enabled);
  for (const char *tag : {"HUB", "EXT_HUB", "EXT_PORT"})
    esp_log_level_set(tag, enabled ? ESP_LOG_DEBUG : ESP_LOG_WARN);
  Serial.printf("[USB] Verbose diagnostics %s\n", enabled ? "on" : "off");
}

// This private hook is deliberately tied to the SDK pinned in platformio.ini.
static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(5, 5, 5),
              "Recheck the USB control diagnostic hook when upgrading ESP-IDF");

extern "C" esp_err_t __real_usbh_dev_submit_ctrl_urb(usb_device_handle_t, urb_t *);

extern "C" esp_err_t __wrap_usbh_dev_submit_ctrl_urb(usb_device_handle_t device, urb_t *urb) {
  const auto &transfer = urb->transfer;
  const auto *setup = reinterpret_cast<const usb_setup_packet_t *>(transfer.data_buffer);
  // Hub port class requests: identify the exact feature immediately before
  // the driver's asynchronous completion/error log. Do not alter the request,
  // callback, transfer lifetime, or normal HID report traffic.
  const bool portRequest = transfer.num_bytes >= sizeof(usb_setup_packet_t) &&
                           setup->bmRequestType == 0x23;
  if (portRequest && usbDiagnosticsEnabled()) {
    Serial.printf("[USB control] device=%p request=%u feature=%u port=%u length=%u\n",
                  device, setup->bRequest, setup->wValue, setup->wIndex, setup->wLength);
  }
  const esp_err_t result = __real_usbh_dev_submit_ctrl_urb(device, urb);
  if (portRequest && result != ESP_OK)
    Serial.printf("[USB control] submit failed: %s\n", esp_err_to_name(result));
  return result;
}
