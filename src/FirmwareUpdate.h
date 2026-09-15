#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_app_desc.h>
class FirmwareUpdate {
public:
  void begin();
  void loop(bool healthy);
  bool start();
  bool write(const uint8_t *data,size_t length);
  bool finish();
  bool commit();
  void abort(const char *reason);
  bool rollback();
  void status(JsonObject out) const;
  bool busy()const{return _busy;}
  bool rebootPending()const{return _rebootAt!=0;}
  const String &error()const{return _error;}
private:
  static constexpr size_t PREFIX=sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+sizeof(esp_app_desc_t);
  uint8_t _prefix[PREFIX]={};
  size_t _prefixSize=0,_bytes=0;
  bool _busy=false,_writing=false,_ready=false,_trial=false,_rollbackAvailable=false;
  uint32_t _healthySince=0,_rebootAt=0;
  esp_ota_handle_t _handle=0;
  const esp_partition_t *_target=nullptr;
  String _error;
};
