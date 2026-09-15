#pragma once
#include <esp_task_wdt.h>
inline void startRuntimeWatchdog(){
  const esp_task_wdt_config_t config={.timeout_ms=10000,.idle_core_mask=0,.trigger_panic=true};
  const auto state=esp_task_wdt_status(nullptr);
  if(state==ESP_ERR_INVALID_STATE)esp_task_wdt_init(&config);else esp_task_wdt_reconfigure(&config);
  if(state!=ESP_OK)esp_task_wdt_add(nullptr);
}
inline void feedRuntimeWatchdog(){esp_task_wdt_reset();}
