"""Exercise actual host-task recovery across pending events and delayed cleanup."""
from pathlib import Path
import subprocess,tempfile
s=Path('src/UsbHost.cpp').read_text();start=s.index('void UsbHost::servicePortRecovery() {');function=s[start:s.index('\n}',start)+2]
preamble=r'''
#include <cassert>
#include <cstdint>
constexpr int ESP_OK=0;
struct usb_host_lib_info_t {int num_devices;};
static uint32_t now;
static int off_result,on_result,devices,off_calls,on_calls,info_calls;
uint32_t millis(){return now;}
int usb_host_lib_info(usb_host_lib_info_t *info){++info_calls;info->num_devices=devices;return ESP_OK;}
int usb_host_lib_set_root_port_power(bool on){if(on){++on_calls;return on_result;}++off_calls;return off_result;}
#define portENTER_CRITICAL(x) do {} while(0)
#define portEXIT_CRITICAL(x) do {} while(0)
struct UsbHost {
 bool _portOff=false,_resetRequested=false;
 uint32_t _portOffAt=0;
 void servicePortRecovery();
};
'''
main=r'''
int main(){
 UsbHost usb;now=100;usb.servicePortRecovery();assert(off_calls==0 && on_calls==0);
 usb._resetRequested=true;off_result=1;usb.servicePortRecovery();
 assert(usb._resetRequested && !usb._portOff && off_calls==1);
 off_result=ESP_OK;usb.servicePortRecovery();
 assert(!usb._resetRequested && usb._portOff && usb._portOffAt==100);
 now=199;usb.servicePortRecovery();assert(on_calls==0 && info_calls==0);
 now=200;devices=2;usb.servicePortRecovery();assert(on_calls==0 && usb._portOff);
 now=201;devices=0;on_result=1;usb.servicePortRecovery();assert(on_calls==1 && usb._portOff);
 now=202;on_result=ESP_OK;usb.servicePortRecovery();assert(on_calls==2 && !usb._portOff);
 usb.servicePortRecovery();assert(off_calls==2 && on_calls==2);
}
'''
with tempfile.TemporaryDirectory() as d:
 source=Path(d)/'test.cpp';binary=Path(d)/'test';source.write_text(preamble+function+main)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('USB event-task recovery dispatch passed')
