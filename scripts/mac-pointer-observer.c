// Read cursor position, or temporarily set the HID mouse acceleration parameter.
// The experiment runner restores the exact original value in its finally block.
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int main(int argc,char **argv){
  CGEventRef event=CGEventCreate(NULL);if(!event){fprintf(stderr,"Cannot read cursor\n");return 1;}
  CGPoint p=CGEventGetLocation(event);CFRelease(event);
  io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("IOHIDSystem"));io_connect_t connection=0;
  kern_return_t opened=service?IOServiceOpen(service,mach_task_self(),kIOHIDParamConnectType,&connection):KERN_FAILURE;
  if(service)IOObjectRelease(service);
  double acceleration=0; kern_return_t read=KERN_FAILURE,write=KERN_SUCCESS;
  if(opened==KERN_SUCCESS){
    if(argc==2){char *end;double value=strtod(argv[1],&end);if(*end||!isfinite(value)||value< -1||value>3){IOServiceClose(connection);return 2;}
      write=IOHIDSetAccelerationWithKey(connection,CFSTR("HIDMouseAcceleration"),value);
    }
    read=IOHIDGetAccelerationWithKey(connection,CFSTR("HIDMouseAcceleration"),&acceleration);IOServiceClose(connection);
  }
  printf("{\"x\":%.4f,\"y\":%.4f,\"open_status\":%d,\"read_status\":%d,\"write_status\":%d,\"acceleration\":%.8f}\n",p.x,p.y,opened,read,write,acceleration);
  return argc==2&&(opened||read||write)?1:0;
}
