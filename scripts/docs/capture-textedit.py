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
'name':['Hold BOOT for 3 seconds','Press 1: Computers','Press 1, then 2: rename Work Mac','Type the new name','Press Enter: review the change','Press Y to save; press 1 to check'],
'mouse':['Hold BOOT for 3 seconds','Press 2: Shortcuts','Press 1: Cycle','Press 1: Record','Press and release mouse button 4','Press Y: shortcut saved']}
for kind in ['name','mouse']:
 out=[]
 for step in range(1,7):
  text=(root/'transcripts'/f'{kind}-{step:02}.txt').read_text()
  variants=[(text,3000)]
  if kind=='name' and step==4:
   prefix=text[:-len('Studio Mac')]
   variants=[(prefix+'Studio Mac'[:i],180) for i in [1,3,5,7]]+[(text,1700)]
  for content,duration in variants:
   call('hotkey','--key','CmdOrCtrl+A','--no-screenshot')
   state=call('paste-text','--text-stdin',text=content)
   state=call('hotkey','--key','CmdOrCtrl+Down')
   path=frames/f'{kind}-{len(out):02}.png';shutil.copyfile(state['screenshot']['path'],path)
   (frames/f'{kind}-{len(out):02}.json').write_text(json.dumps(state,indent=2))
   out.append({'file':str(path),'duration_ms':duration,'caption':labels[kind][step-1]})
   print(path.name,flush=True)
 manifest[kind]=out
(root/'manifest.json').write_text(json.dumps(manifest,indent=2))
