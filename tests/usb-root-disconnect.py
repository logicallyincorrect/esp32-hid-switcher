"""Execute the actual SDK patch helper for stale and live root-device nodes."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from usb_stack_patch import ROOT_DISCONNECT, ROOT_STOP, patch_hub_source
preamble = r'''
#include <assert.h>
#include <stddef.h>
typedef int esp_err_t;
typedef int root_port_state_t;
typedef void *hcd_port_handle_t;
enum {ESP_OK=0, ESP_ERR_NOT_FOUND=1, OTHER_ERROR=2, HCD_PORT_STATE_RECOVERY=3,
      PORT_REQ_RECOVER=4, HUB_DRIVER_ACTION_ROOT_REQ=8, ESP_ERR_INVALID_STATE=9,
      ROOT_PORT_STATE_NOT_POWERED=0, ROOT_PORT_STATE_ENABLED=1, HCD_PORT_CMD_POWER_OFF=2};
static struct {struct {int port_reqs, root_port_state; struct {int actions;} flags;} dynamic; struct {void *root_port_hdl;} constant;} hub;
static __typeof__(hub) *p_hub_driver_obj=&hub;
static int result, state, checked, notified, command_result, commands;
static int hcd_port_command(void *port, int command) {
 assert(port==(void*)1 && command==HCD_PORT_CMD_POWER_OFF);++commands;return command_result;
}
static int dev_tree_node_dev_gone(void *parent, int port) {
 assert(parent==NULL && port==0); ++notified; return result;
}
static int hcd_port_get_state(void *port) {assert(port==(void*)1);return state;}
#define HUB_DRIVER_ENTER_CRITICAL() do {} while(0)
#define HUB_DRIVER_EXIT_CRITICAL() do {} while(0)
#define HUB_DRIVER_CHECK_FROM_CRIT(cond,err) do {if(!(cond))return err;} while(0)
#define ESP_ERROR_CHECK(ret) do {checked=(ret);} while(0)
'''
main = r'''
int main(void) {
 result=ESP_ERR_NOT_FOUND; state=HCD_PORT_STATE_RECOVERY;
 root_port_notify_device_gone((void*)1);
 assert(notified==1 && checked==0);
 assert(hub.dynamic.port_reqs==PORT_REQ_RECOVER);
 assert(hub.dynamic.flags.actions==HUB_DRIVER_ACTION_ROOT_REQ);
 hub.dynamic.port_reqs=0;hub.dynamic.flags.actions=0;state=0;
 root_port_notify_device_gone((void*)1);
 assert(hub.dynamic.port_reqs==0 && hub.dynamic.flags.actions==0);
 result=ESP_OK;state=HCD_PORT_STATE_RECOVERY;
 root_port_notify_device_gone((void*)1);
 assert(checked==0 && hub.dynamic.port_reqs==0);
 result=OTHER_ERROR;root_port_notify_device_gone((void*)1);
 assert(checked==OTHER_ERROR);
 hub.constant.root_port_hdl=(void*)1;
 hub.dynamic.root_port_state=ROOT_PORT_STATE_ENABLED;
 command_result=ESP_ERR_INVALID_STATE;
 assert(hub_root_stop()==ESP_ERR_INVALID_STATE);
 assert(hub.dynamic.root_port_state==ROOT_PORT_STATE_ENABLED && commands==1);
 command_result=ESP_OK;
 assert(hub_root_stop()==ESP_OK);
 assert(hub.dynamic.root_port_state==ROOT_PORT_STATE_NOT_POWERED && commands==2);
 assert(hub_root_stop()==ESP_OK && commands==2);
}
'''
with tempfile.TemporaryDirectory() as d:
 source=Path(d)/'test.c';binary=Path(d)/'test';source.write_text(preamble+ROOT_DISCONNECT+ROOT_STOP+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
try:
 patch_hub_source('unexpected SDK version')
 raise AssertionError('patch must fail closed if the upstream source changes')
except RuntimeError:
 pass
print('USB root disconnect recovery passed')
