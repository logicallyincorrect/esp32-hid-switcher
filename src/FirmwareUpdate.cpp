#include "FirmwareUpdate.h"
#include "RuntimeHealth.h"
#include <cstring>

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error Firmware updates require bootloader rollback support
#endif

// Arduino must defer its default early acceptance to our health check.
extern "C" bool verifyRollbackLater(){return true;}

static bool previousFirmwareAvailable(){
  const auto *other=esp_ota_get_next_update_partition(nullptr);
  esp_ota_img_states_t state;
  return other&&other!=esp_ota_get_running_partition()&&
    esp_ota_get_state_partition(other,&state)==ESP_OK&&state==ESP_OTA_IMG_VALID&&esp_ota_check_rollback_is_possible();
}
void FirmwareUpdate::begin(){
  esp_ota_img_states_t state;
  _trial=esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK&&state!=ESP_OTA_IMG_VALID;
  _rollbackAvailable=previousFirmwareAvailable();
  Serial.printf("[Firmware] Running %s; trial=%u\n",esp_ota_get_running_partition()->label,_trial);
}
void FirmwareUpdate::loop(bool healthy){
  if(_rebootAt&&int32_t(millis()-_rebootAt)>=0){ESP.restart();return;}
  if(!_trial)return;
  if(!healthy){_healthySince=0;return;}
  if(!_healthySince)_healthySince=millis();
  if(millis()-_healthySince>=30000){
    if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK){_trial=false;Serial.println("[Firmware] Trial boot healthy; firmware accepted");}
    else {_error="Could not confirm trial firmware";}
  }
}
void FirmwareUpdate::abort(const char *reason){
  if(_writing)esp_ota_abort(_handle);
  _writing=_ready=_busy=false;_error=reason;
}
bool FirmwareUpdate::start(){
  if(_busy||_rebootAt){_error="An update is already in progress";return false;}
  if(_trial){_error="Wait for this firmware's startup check to finish";return false;}
  _target=esp_ota_get_next_update_partition(nullptr);
  if(!_target||_target==esp_ota_get_running_partition()){_error="No inactive firmware slot is available";return false;}
  _bytes=_prefixSize=0;_ready=_writing=false;_busy=true;_error="";return true;
}
bool FirmwareUpdate::write(const uint8_t *data,size_t length){
  feedRuntimeWatchdog();
  if(!_busy)return false;
  if(length>_target->size-_bytes){abort("Firmware exceeds the available slot");return false;}
  _bytes+=length;
  if(_prefixSize<PREFIX){
    const size_t take=std::min(length,PREFIX-_prefixSize);
    memcpy(_prefix+_prefixSize,data,take);_prefixSize+=take;data+=take;length-=take;
    if(_prefixSize<PREFIX)return true;
    esp_image_header_t header;esp_app_desc_t app;
    memcpy(&header,_prefix,sizeof(header));
    memcpy(&app,_prefix+sizeof(header)+sizeof(esp_image_segment_header_t),sizeof(app));
    if(header.magic!=ESP_IMAGE_HEADER_MAGIC||header.chip_id!=ESP_CHIP_ID_ESP32S3||app.magic_word!=ESP_APP_DESC_MAGIC_WORD||
       memcmp(app.project_name,esp_app_get_description()->project_name,sizeof(app.project_name))!=0){
      abort("Choose a HID Switcher ESP32-S3 firmware.bin, not a factory image");return false;
    }
    if(esp_ota_begin(_target,OTA_WITH_SEQUENTIAL_WRITES,&_handle)!=ESP_OK){abort("Could not open inactive firmware slot");return false;}
    _writing=true;_rollbackAvailable=false;
    if(esp_ota_write(_handle,_prefix,PREFIX)!=ESP_OK){abort("Could not write firmware header");return false;}
  }
  if(length&&esp_ota_write(_handle,data,length)!=ESP_OK){abort("Firmware write failed");return false;}
  return true;
}
bool FirmwareUpdate::finish(){
  if(!_busy)return false;
  if(!_writing){abort("Firmware file is incomplete");return false;}
  const esp_err_t rc=esp_ota_end(_handle);_writing=false;
  if(rc!=ESP_OK){abort("Firmware verification failed; current firmware is unchanged");return false;}
  _ready=true;return true;
}
bool FirmwareUpdate::commit(){
  if(!_ready)return false;
  if(esp_ota_set_boot_partition(_target)!=ESP_OK){abort("Could not select the new firmware");return false;}
  _ready=_busy=false;_rebootAt=millis()+1500;return true;
}
bool FirmwareUpdate::rollback(){
  if(_busy||_rebootAt){_error="An update is already in progress";return false;}
  if(!previousFirmwareAvailable()){_error="No previous working firmware is available yet";return false;}
  if(esp_ota_mark_app_invalid_rollback()!=ESP_OK){_error="Could not select previous firmware";return false;}
  _trial=false;_rebootAt=millis()+1500;return true;
}
void FirmwareUpdate::status(JsonObject out)const{
  const auto *running=esp_ota_get_running_partition();const auto *app=esp_app_get_description();
  char build[17];for(unsigned i=0;i<8;++i)snprintf(build+i*2,3,"%02x",app->app_elf_sha256[i]);
  out["build"]=build;out["slot"]=running->label;out["trial"]= _trial;out["updating"]=_busy;out["restarting"]=bool(_rebootAt);out["bytes"]=_bytes;out["error"]=_error;
  out["rollback_available"]=!_busy&&!_rebootAt&&_rollbackAvailable;
  const auto *target=esp_ota_get_next_update_partition(nullptr);out["max_bytes"]=target?target->size:0;
  out["uptime_seconds"]=millis()/1000;
}
