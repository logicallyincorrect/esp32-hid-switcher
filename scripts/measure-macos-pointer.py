#!/usr/bin/env python3
"""Explicit hardware test: replay absolute positions at several macOS HID tracking values.
Run only with the user ready, the current Mac selected, and the mouse left still.
No keyboard events, clicks, preference-file writes, or persistent board changes.
"""
import argparse
import importlib.util
import json
import math
import subprocess
import time
from pathlib import Path
import serial

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--observer',required=True)
parser.add_argument('--output',required=True)
parser.add_argument('--slot',type=int,default=0)
args=parser.parse_args()
spec=importlib.util.spec_from_file_location('serial_cli',Path(__file__).resolve().parents[1]/'cli/hid-switcher.py')
cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)
def observe(speed=None):
    run=subprocess.run([args.observer]+([] if speed is None else [str(speed)]),capture_output=True,text=True,check=True)
    return json.loads(run.stdout)
original=observe()
if original['open_status'] or original['read_status']:
    raise SystemExit('Cannot read original HID tracking value; no changes made')
result={'original':original,'runs':[],'restored':False,'note':'Tests HIDMouseAcceleration with a repeated absolute-HID sequence. Does not establish behavior of every macOS per-device preference.'}
port=serial.Serial(port=None,baudrate=115200,timeout=.2,write_timeout=2)
port.dtr=False;port.rts=False;port.port=args.port
sequence=0
with port:
    time.sleep(2)
    def request(op,**fields):
        global sequence
        sequence+=1
        reply=cli.request(port,sequence,dict(op=op,**fields))
        if not reply.get('ok'):raise RuntimeError(reply.get('error','Unknown failure'))
        return reply['result']
    status=request('status')
    deadline=time.monotonic()+30
    while status['selected']==args.slot and status['seamless'] and not status['slots'][args.slot]['connected'] and time.monotonic()<deadline:
        time.sleep(1);status=request('status')
    if status['selected']!=args.slot or not status['seamless'] or not status['slots'][args.slot]['connected']:
        raise SystemExit('Expected current Mac selected, connected, and seamless enabled; no changes made')
    position=status['pointer_position']
    try:
        for speed in [0.0,3.0,original['acceleration']]:
            current=observe(speed)
            if current['read_status'] or current['write_status'] or abs(current['acceleration']-speed)>0.0001:
                raise RuntimeError('HID tracking change did not read back correctly')
            time.sleep(.3)
            trial={'acceleration':speed,'samples':[]}
            for x,y in [(8000,12000),(10000,12000),(12000,12000),(16000,12000),(24000,12000),(24000,16000),(24000,24000)]:
                request('pointer-probe',x=x,y=y)
                time.sleep(.12)
                point=observe()
                trial['samples'].append({'sent':[x,y],'observed':[point['x'],point['y']]})
            result['runs'].append(trial)
        deviations=[]
        for a,b in zip(result['runs'][0]['samples'],result['runs'][1]['samples']):
            deviations.append(math.dist(a['observed'],b['observed']))
        result['max_low_high_difference_points']=max(deviations)
        result['endpoint_variation_detected']=max(deviations)>2
    except BaseException as error:
        result['error']=str(error)
        raise
    finally:
        try:
            restored=observe(original['acceleration'])
            result['restored']=not restored['write_status'] and abs(restored['acceleration']-original['acceleration'])<.0001
            result['restore_readback']=restored
        finally:
            try: request('pointer-probe',**position)
            except Exception as error: result['cursor_restore_error']=str(error)
            Path(args.output).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
