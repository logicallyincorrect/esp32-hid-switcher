#include "USBManager.h"
#include <hid_usage_keyboard.h>
#include "HidMouseParser.h"
#include "RecoveryPolicy.h"
#include "RuntimeHealth.h"

KeyboardReportCallback USBManager::_keyboardCb = nullptr;
MouseReportCallback USBManager::_mouseCb = nullptr;
struct MouseDevice { hid_host_device_handle_t handle=nullptr; HidMouseParser parser; uint8_t buttons=0; };
static MouseDevice mice[4];
static uint8_t mouseButtons() { uint8_t b=0;for(auto &m:mice)b|=m.buttons;return b; }


struct InterfaceStatus {hid_host_device_handle_t handle=nullptr;hid_host_dev_params_t params={};bool active=false;uint32_t reports=0;};
static InterfaceStatus interfaces[16];
static portMUX_TYPE usbStatusMux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t transferErrors=0,openErrors=0,eventDrops=0,usbHeartbeat=0,hidHeartbeat=0;
static bool needsRecovery=false,portOff=false;
static uint32_t portOffAt=0;
static RecoveryPolicy recovery;
static void faultUSB(){portENTER_CRITICAL(&usbStatusMux);++transferErrors;needsRecovery=true;portEXIT_CRITICAL(&usbStatusMux);}
static void setInterface(hid_host_device_handle_t handle,const hid_host_dev_params_t &params,bool active,bool removed=false){
  portENTER_CRITICAL(&usbStatusMux);
  if(removed){for(auto &i:interfaces)if(i.params.addr==params.addr)i={};portEXIT_CRITICAL(&usbStatusMux);return;}
  InterfaceStatus *entry=nullptr;
  for(auto &i:interfaces)if(i.handle==handle){entry=&i;break;}
  if(!entry&&!removed)for(auto &i:interfaces)if(!i.handle){entry=&i;break;}
  if(entry){if(removed)*entry={};else{entry->handle=handle;entry->params=params;entry->active=active;}}
  portEXIT_CRITICAL(&usbStatusMux);
}
bool USBManager::healthy(){
  portENTER_CRITICAL(&usbStatusMux);const uint32_t a=usbHeartbeat,b=hidHeartbeat;portEXIT_CRITICAL(&usbStatusMux);
  return a&&b&&millis()-a<2000&&millis()-b<2000&&!portOff;
}
void USBManager::requestRecovery(){portENTER_CRITICAL(&usbStatusMux);needsRecovery=true;portEXIT_CRITICAL(&usbStatusMux);}
bool USBManager::service(){
  portENTER_CRITICAL(&usbStatusMux);bool pending=needsRecovery;needsRecovery=false;portEXIT_CRITICAL(&usbStatusMux);
  if(pending)recovery.fault();
  if(portOff){if(millis()-portOffAt>=100){usb_host_lib_set_root_port_power(true);portOff=false;}return false;}
  if(recovery.poll(millis())){
    Serial.println("[Recovery] Restarting USB host after transfer errors");
    if(usb_host_lib_set_root_port_power(false)==ESP_OK){portOff=true;portOffAt=millis();}
    return true;
  }
  return false;
}
void USBManager::appendStatus(JsonObject out){
  InterfaceStatus copy[16];uint32_t errors,failed,dropped;
  portENTER_CRITICAL(&usbStatusMux);memcpy(copy,interfaces,sizeof(copy));errors=transferErrors;failed=openErrors;dropped=eventDrops;portEXIT_CRITICAL(&usbStatusMux);
  out["healthy"]=healthy();out["transfer_errors"]=errors;out["open_errors"]=failed;out["event_drops"]=dropped;out["recoveries"]=recovery.attempts;out["recovering"]=portOff||recovery.pending;
  auto list=out["interfaces"].to<JsonArray>();
  for(const auto &i:copy)if(i.handle){auto item=list.add<JsonObject>();item["address"]=i.params.addr;item["interface"]=i.params.iface_num;item["vid"]=i.params.vid;item["pid"]=i.params.pid;item["kind"]=i.params.proto==1?"Keyboard":i.params.proto==2?"Mouse":"Other HID";item["active"]=i.active;item["reports"]=i.reports;}
}

static QueueHandle_t hid_host_event_queue;

typedef struct {
  hid_host_device_handle_t hid_device_handle;
  hid_host_driver_event_t event;
  void *arg;
} hid_host_event_queue_t;

static const char *hid_proto_name_str[] = {"NONE", "KEYBOARD", "MOUSE"};

void USBManager::printDiagnostics() {
  usb_host_lib_info_t info={};
  if(usb_host_lib_info(&info)!=ESP_OK)return;
  Serial.printf("[USB diagnostic] devices=%d clients=%d\n",info.num_devices,info.num_clients);
  usb_host_client_config_t config={};
  config.max_num_event_msg=8;
  config.async.client_event_callback=[](const usb_host_client_event_msg_t *,void *){};
  usb_host_client_handle_t client=nullptr;
  if(usb_host_client_register(&config,&client)!=ESP_OK)return;
  uint8_t addresses[8];int count=0;
  if(usb_host_device_addr_list_fill(8,addresses,&count)==ESP_OK)for(int i=0;i<count;++i) {
    usb_device_handle_t device=nullptr;
    if(usb_host_device_open(client,addresses[i],&device)!=ESP_OK)continue;
    const usb_device_desc_t *desc=nullptr;
    if(usb_host_get_device_descriptor(device,&desc)==ESP_OK)
      Serial.printf("[USB diagnostic] address=%u VID=%04x PID=%04x class=%u\n",addresses[i],desc->idVendor,desc->idProduct,desc->bDeviceClass);
    const usb_config_desc_t *cfg=nullptr;
    if(usb_host_get_active_config_descriptor(device,&cfg)==ESP_OK) {
      const uint8_t *bytes=reinterpret_cast<const uint8_t *>(cfg);
      for(size_t pos=0;pos+2<=cfg->wTotalLength;) {
        const size_t length=bytes[pos];
        if(length<2||pos+length>cfg->wTotalLength)break;
        if(bytes[pos+1]==4&&length>=9)
          Serial.printf("[USB diagnostic] interface=%u class=%u subclass=%u protocol=%u\n",bytes[pos+2],bytes[pos+5],bytes[pos+6],bytes[pos+7]);
        pos+=length;
      }
    }
    usb_host_device_close(client,device);
  }
  usb_host_client_handle_events(client,0);
  usb_host_client_deregister(client);
}

void USBManager::begin() {
  Serial.println("[USB] Installing USB Host library...");
  BaseType_t task_created =
      xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096,
                              xTaskGetCurrentTaskHandle(), 2, NULL, 0);
  assert(task_created == pdTRUE);

  ulTaskNotifyTake(false, 1000);
  Serial.println("[USB] USB Host library ready");

  // The driver can report an already-connected keyboard during installation.
  hid_host_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
  assert(hid_host_event_queue != nullptr);

  Serial.println("[USB] Installing HID driver...");
  const hid_host_driver_config_t hid_host_driver_config = {
      .create_background_task = true,
      .task_priority = 5,
      .stack_size = 4096,
      .core_id = 0,
      .callback = hid_host_device_callback,
      .callback_arg = NULL};
  ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));

  task_created = xTaskCreate(&hid_host_task, "hid_task", 4096, NULL, 2, NULL);
  assert(task_created == pdTRUE);
  Serial.println("[USB] HID driver ready");
}

void USBManager::usb_lib_task(void *arg) {
  esp_task_wdt_add(nullptr);
  const usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
      // With CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK enabled, IDF cancels
      // enumeration if this callback is absent. Accept the default config;
      // the HID driver decides which interfaces it supports afterwards.
      .enum_filter_cb = [](const usb_device_desc_t *device,uint8_t *configuration) {
        *configuration=1;
        Serial.printf("[USB] Enumerating VID=%04x PID=%04x class=%u\n",device->idVendor,device->idProduct,device->bDeviceClass);
        // Keep scarce host channels for HID and hubs. Composite devices expose
        // their classes per interface (0) or through an association (0xef).
        const uint8_t cls=device->bDeviceClass;
        const bool supported=cls==0 || cls==3 || cls==9 || cls==0xef;
        if(!supported)Serial.println("[USB] Skipping unsupported device class");
        return supported;
      },
  };

  ESP_ERROR_CHECK(usb_host_install(&host_config));
  xTaskNotifyGive((TaskHandle_t)arg);

  while (true) {
    uint32_t event_flags=0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);
    feedRuntimeWatchdog();
    portENTER_CRITICAL(&usbStatusMux);usbHeartbeat=millis();portEXIT_CRITICAL(&usbStatusMux);

    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      usb_host_device_free_all();
    }
  }
}

void USBManager::hid_host_task(void *pvParameters) {
  esp_task_wdt_add(nullptr);
  hid_host_event_queue_t evt_queue;
  while (true) {
    feedRuntimeWatchdog();
    portENTER_CRITICAL(&usbStatusMux);hidHeartbeat=millis();portEXIT_CRITICAL(&usbStatusMux);
    if (xQueueReceive(hid_host_event_queue, &evt_queue, pdMS_TO_TICKS(50))) {
      hid_host_device_event(evt_queue.hid_device_handle, evt_queue.event,
                            evt_queue.arg);
    }
  }
}

void USBManager::hid_host_device_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_driver_event_t event, void *arg) {
  const hid_host_event_queue_t evt_queue = {
      .hid_device_handle = hid_device_handle, .event = event, .arg = arg};
  if(xQueueSend(hid_host_event_queue, &evt_queue, 0)!=pdTRUE){
    portENTER_CRITICAL(&usbStatusMux);++eventDrops;needsRecovery=true;portEXIT_CRITICAL(&usbStatusMux);
  }
}

void USBManager::hid_host_device_event(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_driver_event_t event, void *arg) {
  hid_host_dev_params_t dev_params;

  if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK) {
    return;
  }

  const hid_host_device_config_t dev_config = {
      .callback = hid_host_interface_callback, .callback_arg = NULL};

  switch (event) {
  case HID_HOST_DRIVER_EVENT_CONNECTED: {
    setInterface(hid_device_handle,dev_params,false);
    Serial.printf("[USB] %s connected!\n",
                  hid_proto_name_str[dev_params.proto]);

    // Descriptor requests require an opened (READY) interface in this driver.
    if (hid_host_device_open(hid_device_handle, &dev_config) != ESP_OK) {
      portENTER_CRITICAL(&usbStatusMux);++openErrors;portEXIT_CRITICAL(&usbStatusMux);
      Serial.println("[USB] Failed to open HID device");
      break;
    }

    // Inspect report descriptors for mice, including non-boot HID interfaces.
    MouseDevice *mouse=nullptr;
    if(dev_params.proto!=HID_PROTOCOL_KEYBOARD) {
      size_t length=0;const uint8_t *descriptor=hid_host_get_report_descriptor(hid_device_handle,&length);
      for(auto &candidate:mice)if(!candidate.handle){mouse=&candidate;break;}
      if(!mouse||!descriptor||!mouse->parser.parse(descriptor,length)) {
        hid_host_device_close(hid_device_handle);
        Serial.println("[USB] Skipping unsupported non-keyboard interface");break;
      }
      mouse->handle=hid_device_handle;mouse->buttons=0;
      Serial.println("[USB] Relative mouse report descriptor accepted");
    }

    if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
      hid_class_request_set_protocol(hid_device_handle,
                                     mouse ? HID_REPORT_PROTOCOL_REPORT : HID_REPORT_PROTOCOL_BOOT);
      if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
        hid_class_request_set_idle(hid_device_handle, 0, 0);
      }
    }

    if (hid_host_device_start(hid_device_handle) != ESP_OK) {
      if(mouse)mouse->handle=nullptr;
      hid_host_device_close(hid_device_handle);
      Serial.println("[USB] Failed to start HID device");faultUSB();
    }else setInterface(hid_device_handle,dev_params,true);
    break;
  }
  default:
    break;
  }
}

void USBManager::hid_host_interface_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_interface_event_t event, void *arg) {
  uint8_t data[64] = {0};
  size_t data_length = 0;
  hid_host_dev_params_t dev_params;

  if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK) {
    return;
  }

  switch (event) {
  case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
    portENTER_CRITICAL(&usbStatusMux);for(auto &i:interfaces)if(i.handle==hid_device_handle)++i.reports;portEXIT_CRITICAL(&usbStatusMux);
    if (hid_host_device_get_raw_input_report_data(hid_device_handle, data, 64,
                                                  &data_length) == ESP_OK) {

      for(auto &mouse:mice)if(mouse.handle==hid_device_handle) {
        MouseReport report;bool hasButtons=false;
        if(mouse.parser.decode(data,data_length,report,hasButtons)) {
          if(hasButtons)mouse.buttons=report.buttons;
          report.buttons=mouseButtons();
          if(_mouseCb)_mouseCb(report);
        }
      }
      if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
        if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
          if (_keyboardCb) {
            _keyboardCb(data, data_length);
          }
        }
      }
    }
    break;

  case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
    setInterface(hid_device_handle,dev_params,false,true);
    Serial.printf("[USB] %s disconnected\n",
                  hid_proto_name_str[dev_params.proto]);
    if (_keyboardCb && dev_params.proto == HID_PROTOCOL_KEYBOARD) {
      const uint8_t released[8] = {0};
      _keyboardCb(released, sizeof(released));
    }
    for(auto &mouse:mice)if(mouse.handle==hid_device_handle) {
      mouse.handle=nullptr;mouse.buttons=0;
      MouseReport released;released.buttons=mouseButtons();if(_mouseCb)_mouseCb(released);
    }
    hid_host_device_close(hid_device_handle);
    break;

  case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
    faultUSB();
    Serial.printf("[USB] %s transfer error\n",
                  hid_proto_name_str[dev_params.proto]);
    break;

  default:
    break;
  }
}
