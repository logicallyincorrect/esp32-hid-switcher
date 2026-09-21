#include "esp_idf_version.h"
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5)
#error "Review the root-hub correction when upgrading ESP-IDF"
#endif
// Generated from the pinned SDK by scripts/usb-diagnostics.py, including the
// upstream SPDX license header. No globally installed SDK files are changed.
#include "patched_usb_hub.c"
