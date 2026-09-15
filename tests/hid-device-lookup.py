"""Exercise the vendored C lookup itself with two devices sharing endpoints."""
from pathlib import Path
import subprocess, tempfile
s=Path('lib/ESP32_USB_Host_HID/hid_host.c').read_text()
start=s.index('static hid_iface_t *get_interface_by_ep(')
end=s.index('\n}',start)+2
function=s[start:end]
preamble='''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/queue.h>
typedef struct {int id;} hid_device_t;
typedef struct hid_iface {hid_device_t *parent;uint8_t ep_in;STAILQ_ENTRY(hid_iface) tailq_entry;} hid_iface_t;
STAILQ_HEAD(ifaces,hid_iface);
struct driver {struct ifaces hid_ifaces_tailq;} driver;
static struct driver *s_hid_driver=&driver;
#define HID_ENTER_CRITICAL() do {} while(0)
#define HID_EXIT_CRITICAL() do {} while(0)
'''
main='''
int main(void) {
 hid_device_t receiver={1},keyboard={2},unknown={3};
 hid_iface_t a={.parent=&receiver,.ep_in=0x81},b={.parent=&keyboard,.ep_in=0x81},c={.parent=&keyboard,.ep_in=0x82};
 STAILQ_INIT(&driver.hid_ifaces_tailq);
 STAILQ_INSERT_TAIL(&driver.hid_ifaces_tailq,&a,tailq_entry);
 STAILQ_INSERT_TAIL(&driver.hid_ifaces_tailq,&b,tailq_entry);
 STAILQ_INSERT_TAIL(&driver.hid_ifaces_tailq,&c,tailq_entry);
 assert(get_interface_by_ep(&receiver,0x81)==&a);
 assert(get_interface_by_ep(&keyboard,0x81)==&b);
 assert(get_interface_by_ep(&keyboard,0x82)==&c);
 assert(get_interface_by_ep(&receiver,0x82)==NULL);
 assert(get_interface_by_ep(&unknown,0x81)==NULL);
 STAILQ_REMOVE(&driver.hid_ifaces_tailq,&a,hid_iface,tailq_entry);
 assert(get_interface_by_ep(&keyboard,0x81)==&b);
 assert(get_interface_by_ep(&receiver,0x81)==NULL);
}
'''
with tempfile.TemporaryDirectory() as d:
 source=Path(d)/'test.c';binary=Path(d)/'test';source.write_text(preamble+function+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('HID multi-device endpoint lookup passed')
