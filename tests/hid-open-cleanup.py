"""Fault-inject the actual HID open helper and verify resources are balanced."""
from pathlib import Path
import subprocess, tempfile
s=Path('lib/ESP32_USB_Host_HID/hid_host.c').read_text()
start=s.index('static esp_err_t\nhid_host_interface_claim_and_prepare_transfer(')
function=s[start:s.index('\n}',start)+2]
preamble=r'''
#include <assert.h>
#include <stddef.h>
typedef int esp_err_t;
enum {ESP_OK=0, NO_MEMORY=1, NO_CHANNEL=2, HID_INTERFACE_STATE_READY=3};
static int alloc_result,claim_result,allocations,claims,release_count;
static struct {int client_handle;} driver;
static __typeof__(driver) *s_hid_driver=&driver;
typedef struct {int dev_hdl;} parent_t;
typedef struct {parent_t *parent;struct {int iface_num;} dev_params;int ep_in_mps;void *in_xfer;int state;} hid_iface_t;
static int usb_host_transfer_alloc(int size,int isoc,void **out){
 assert(size==8 && isoc==0);
 if(alloc_result)return alloc_result;
 ++allocations;*out=(void*)1;return ESP_OK;
}
static int usb_host_interface_claim(int client,int dev,int iface,int alt){
 (void)client;(void)dev;(void)iface;assert(alt==0);
 if(claim_result)return claim_result;
 ++claims;return ESP_OK;
}
static int usb_host_transfer_free(void *x){assert(x==(void*)1);--allocations;++release_count;return ESP_OK;}
#define HID_RETURN_ON_ERROR(call,msg) do {int r=(call);if(r!=ESP_OK)return r;} while(0)
#define ESP_ERROR_CHECK(call) assert((call)==ESP_OK)
'''
main=r'''
int main(void){
 parent_t parent={1};hid_iface_t iface={.parent=&parent,.ep_in_mps=8};
 alloc_result=NO_MEMORY;
 assert(hid_host_interface_claim_and_prepare_transfer(&iface)==NO_MEMORY);
 assert(allocations==0 && claims==0 && iface.state==0);
 alloc_result=0;claim_result=NO_CHANNEL;
 for(int i=0;i<100;i++){
  assert(hid_host_interface_claim_and_prepare_transfer(&iface)==NO_CHANNEL);
  assert(allocations==0 && claims==0 && iface.in_xfer==NULL && iface.state==0);
 }
 assert(release_count==100);claim_result=0;
 assert(hid_host_interface_claim_and_prepare_transfer(&iface)==ESP_OK);
 assert(allocations==1 && claims==1 && iface.state==HID_INTERFACE_STATE_READY);
}
'''
with tempfile.TemporaryDirectory() as d:
 source=Path(d)/'test.c';binary=Path(d)/'test';source.write_text(preamble+function+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('HID failed-open resource cleanup passed')
