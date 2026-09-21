"""Exercise the real CLI request framing without a serial device."""
import importlib.util
from pathlib import Path
import json
spec=importlib.util.spec_from_file_location('hid_cli',Path(__file__).resolve().parents[1]/'cli/hid-switcher.py')
cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)
class Port:
    def __init__(self,lines):self.lines=iter(lines);self.writes=[]
    def write(self,data):self.writes.append(data)
    def read_until(self,*args):return next(self.lines)
p=Port([b'[Ready] diagnostics\n',b'@HID1 nope\n',b'@HID1 {"id":9,"ok":true}\n',b'@HID1 {"id":3,',b'"ok":false,"error":"busy"}\n'])
r=cli.request(p,3,{'op':'input','command':'scan'})
assert not r['ok'] and r['error']=='busy' and len(p.writes)==1
assert json.loads(p.writes[0][6:])=={'id':3,'op':'input','command':'scan'}
try:cli.request(p,4,{'op':'name','name':'x'*3000})
except ValueError:pass
else:raise AssertionError('oversized request accepted')
assert len(p.writes)==1
print('CLI framing, response correlation, errors and request bounds PASS')
