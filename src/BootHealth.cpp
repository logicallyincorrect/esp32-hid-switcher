#include "BootHealth.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
extern "C" bool verifyRollbackLater(){return true;}
void serviceBootHealth(bool healthy){
  static bool started=false,trial=false;
  static uint32_t since=0;
  if(!started){
    started=true;esp_ota_img_states_t state;
    trial=esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
    Serial.printf("[Firmware] Running %s; trial=%u\n",esp_ota_get_running_partition()->label,trial);
  }
  if(!trial)return;
  if(!healthy){since=0;return;}
  if(!since)since=millis();
  if(millis()-since>=30000&&esp_ota_mark_app_valid_cancel_rollback()==ESP_OK){trial=false;Serial.println("[Firmware] Trial boot healthy; firmware accepted");}
}
