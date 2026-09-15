"""Check the built flash table, including preservation of deployed data offsets."""
from pathlib import Path
import struct
raw=Path('.pio/build/esp32s3_usb_ble/partitions.bin').read_bytes();parts={};regions=[]
for pos in range(0,len(raw),32):
 magic,kind,sub,offset,size,label,flags=struct.unpack('<HBBII16sI',raw[pos:pos+32])
 if magic!=0x50aa:break
 name=label.rstrip(b'\x00').decode();parts[name]=(kind,sub,offset,size);regions.append((offset,offset+size))
assert parts['nvs']==(1,2,0x9000,0x5000)
assert parts['otadata']==(1,0,0xe000,0x2000)
assert parts['app0']==(0,0x10,0x10000,0x300000)
assert parts['app1']==(0,0x11,0x410000,0x300000)
assert parts['spiffs'][2:]==(0x310000,0xe0000)
assert parts['coredump'][2:]==(0x3f0000,0x10000)
regions.sort();assert all(a[1]<=b[0] for a,b in zip(regions,regions[1:]));assert regions[-1][1]<=0x1000000
print('Built partition table: two OTA slots and preserved data offsets PASS')
