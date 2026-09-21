"""Packaging rejects missing/mismatched images and never spans the NVS region."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import zipfile
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);build=root/'build';build.mkdir();out=root/'out'
    for name in ('bootloader.bin','firmware.bin','boot_app0.bin'):(build/name).write_bytes(b'test-image')
    def table(nvs=0x9000):
        rows=[('nvs',nvs,0x5000),('otadata',0xe000,0x2000),('app0',0x10000,0x300000)]
        return b''.join(struct.pack('<HBBII16sI',0x50aa,1,2,offset,size,name.encode(),0) for name,offset,size in rows)
    (build/'partitions.bin').write_bytes(table())
    command=[sys.executable,'scripts/package-web.py','--build',str(build),'--boot-app',str(build/'boot_app0.bin'),'--output',str(out),'--version','test']
    subprocess.run(command,check=True,capture_output=True)
    manifest=json.loads((out/'firmware/manifest.json').read_text())
    assert manifest['new_install_prompt_erase'] is True
    old_catalog=json.loads((out/'firmware/versions.json').read_text())
    old_manifest=old_catalog['current']
    (build/'firmware.bin').write_bytes(b'new-test-image')
    subprocess.run(command[:-1]+['test-new'],check=True,capture_output=True)
    catalog=json.loads((out/'firmware/versions.json').read_text())
    assert len(catalog['versions'])==2 and catalog['current']!=old_manifest
    assert (out/old_manifest).exists()
    assert (out/old_manifest).parent.joinpath('firmware.bin').read_bytes()==b'test-image'
    subprocess.run(command[:-1]+['test-new'],check=True,capture_output=True)
    assert len(json.loads((out/'firmware/versions.json').read_text())['versions'])==2
    for part in manifest['builds'][0]['parts']:
        start=part['offset'];end=start+(out/'firmware'/part['path']).stat().st_size
        assert end<=0x9000 or start>=0xe000, 'image overlaps NVS'
    (build/'partitions.bin').write_bytes(table(0xa000))
    assert subprocess.run(command,capture_output=True).returncode!=0
    archive=root/'history.zip'
    with zipfile.ZipFile(archive,'w') as bundle:
        bundle.writestr('firmware/versions/test/firmware.bin',b'old-build')
        bundle.writestr('index.html',b'old-page')
    restored=root/'restored'
    restore=[sys.executable,'scripts/restore-web-history.py',str(archive),str(restored)]
    subprocess.run(restore,check=True,capture_output=True)
    assert (restored/'firmware/versions/test/firmware.bin').read_bytes()==b'old-build'
    assert not (restored/'index.html').exists()
    with zipfile.ZipFile(archive,'w') as bundle:
        bundle.writestr('firmware/../../escape.bin',b'bad')
    assert subprocess.run(restore,capture_output=True).returncode!=0
    assert not (root/'escape.bin').exists()
    (build/'partitions.bin').write_bytes(table());(build/'firmware.bin').unlink()
    assert subprocess.run(command,capture_output=True).returncode!=0
print('Installer layout, preserved NVS range, and missing-image rejection PASS')
