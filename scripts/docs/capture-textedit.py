"""Capture simulated menu transcripts in an already-open TextEdit demo window."""
import argparse,json,subprocess,shutil
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
parser.add_argument('--window-id', required=True)
parser.add_argument('--orca', default='orca')
args=parser.parse_args()
root=args.directory;frames=root/'frames';frames.mkdir(exist_ok=True)
base=[args.orca,'computer'];target=['--app','com.apple.TextEdit','--window-id',args.window_id]
def call(action,*args,text=None):
 p=subprocess.run(base+[action]+target+list(args)+['--json'],input=text,text=True,capture_output=True)
 r=json.loads(p.stdout);assert r.get('ok'),r
 return r['result']
state=call('get-app-state','--restore-window')
manifest={}
labels={
'name':['Hold BOOT for 3 seconds','Press 1: Computers','Press 1, then 2: rename Work Mac','Type the new name','Press Enter: review the change','Press Y to save; press 1 to check','Press Esc: return to the main menu'],
}
for kind in ['name']:
 out=[]
 call('hotkey','--key','CmdOrCtrl+A','--no-screenshot')
 state=call('press-key','--key','Backspace')
 def capture(state,duration,caption):
  path=frames/f'{kind}-{len(out):04}.png'
  shutil.copyfile(state['screenshot']['path'],path)
  out.append({'file':str(path),'duration_ms':duration,'caption':caption})
 capture(state,3000,labels[kind][0])
 previous=''
 for step in range(1,8):
  text=(root/'transcripts'/f'{kind}-{step:02}.txt').read_text()
  assert text.startswith(previous), 'Demo output must append, never replace a page'
  added=text[len(previous):]
  # Two paced HID reports per character (press + release), at 16 ms each.
  # Capture four characters per frame: approximately eight frames per second.
  chunk_size=1 if kind=='name' and step==4 else 4
  for offset in range(0,len(added),chunk_size):
   chunk=added[offset:offset+chunk_size]
   state=call('paste-text','--text-stdin',text=chunk)
   capture(state,len(chunk)*(100 if chunk_size==1 else 32),labels[kind][step-1])
  out[-1]['duration_ms']+=1400
  previous=text
  print(f'{kind}: step {step}/7, {len(out)} frames',flush=True)
 capture(state,3000,'Press Esc again: exit setup; normal input resumes')
 manifest[kind]=out
 (root/'manifest.json').write_text(json.dumps(manifest,indent=2))
