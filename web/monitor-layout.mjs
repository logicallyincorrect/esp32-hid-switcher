const clone = value => structuredClone(value);
export function validLayout(screens) {
  if (!screens.some(s=>s.enabled)) return false;
  return screens.every((a,i)=>Number.isInteger(a.sensitivity??100)&&(a.sensitivity??100)>=25&&(a.sensitivity??100)<=400&&Number.isInteger(a.x)&&Number.isInteger(a.y)&&Math.abs(a.x)<=65536&&Math.abs(a.y)<=65536&&Number.isInteger(a.width)&&Number.isInteger(a.height)&&a.width>=64&&a.width<=16384&&a.height>=64&&a.height<=16384&&!screens.some((b,j)=>j<i&&a.enabled&&b.enabled&&a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height));
}
export const PRESET_COUNT=4,PRESET_NAME_LENGTH=24;
// Mirrors validPresetName in src/LayoutPresets.h: printable ASCII, not blank.
export function validPresetName(name){
  return typeof name==='string'&&name.length>=1&&name.length<=PRESET_NAME_LENGTH&&/^[\x20-\x7e]+$/.test(name)&&/[^ ]/.test(name);
}
export function presetCopyName(names,base){
  const taken=new Set(names.map(n=>n.toLowerCase()));
  const fit=(suffix='')=>base.slice(0,PRESET_NAME_LENGTH-suffix.length)+suffix;
  if(!taken.has(fit().toLowerCase()))return fit();
  for(let n=2;;++n){const name=fit(` ${n}`);if(!taken.has(name.toLowerCase()))return name;}
}
export function presetPayload(preset,generation){
  return {index:preset.index,name:preset.name,generation,screens:preset.screens.map(({x,y,width,height,enabled,estimated,sensitivity})=>({x,y,width,height,enabled,estimated,sensitivity}))};
}
const overlaps=(a,b)=>a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;
// Mirrors placeOnScreen in src/LayoutPresets.h: keep the stored rectangle when it
// is free, otherwise top-align it right of the rightmost on-screen rectangle.
export function placeOnScreen(screens,slot){
  const screen=screens[slot];screen.enabled=true;
  const others=screens.filter((s,i)=>i!==slot&&s.enabled);
  if(!others.some(s=>overlaps(screen,s)))return screens;
  const right=others.reduce((a,b)=>b.x+b.width>a.x+a.width?b:a);
  screen.x=right.x+right.width;screen.y=right.y;screen.estimated=true;
  return screens;
}
// Calibration is raw travel, not a resolution measurement. A 1920 px anchor
// supplies a preview scale only; every seeded dimension remains estimated.
// keepEnabled: preset firmware stores which screens are on screen, so the page
// must not derive it from pairing.
export function seedArrangement(layout,slots,keepEnabled=false) {
  const draft=clone(layout);
  for(const screen of draft.screens)screen.sensitivity ??= 100;
  if(layout.active||layout.generation!==0)return draft;
  const anchor=draft.screens.find(s=>s.span_x&&s.span_y);
  let x=0;
  for(let i=0;i<draft.screens.length;i++){
    const s=draft.screens[i];
    if(s.estimated&&anchor&&s.span_x&&s.span_y){
      s.width=Math.max(64,Math.min(16384,Math.round(s.span_x*1920/anchor.span_x)));
      s.height=Math.max(64,Math.min(16384,Math.round(s.span_y*1920/anchor.span_x)));
    }
    if(!keepEnabled)s.enabled=!!(slots[i].paired||slots[i].connected||s.span_x);
    s.x=x;s.y=0;x+=s.width;
  }
  return draft;
}
const eyeOff='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 3l18 18M10.6 6.1A9.6 9.6 0 0 1 12 6c5 0 9 6 9 6a15.6 15.6 0 0 1-2.6 3.2M6.6 6.6C4.3 8.2 3 12 3 12s4 6 9 6a8.7 8.7 0 0 0 4.4-1.2M9.9 9.9a3 3 0 0 0 4.2 4.2" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
export class MonitorArrangement {
  // run(op,args) sends one request and refreshes status before resolving.
  constructor(root, run) {
    this.root=root;this.run=run;this.selected=-1;this.dirty=false;this.busy=false;this.presets=null;this.viewing=undefined;this.message='';this.renaming=false;this.presetSignature='';
    root.innerHTML=`<div class="section-heading"><h3>Monitor arrangement</h3><button type="button" class="text-action" data-row>Arrange in a row</button></div>
      <div class="preset-bar" hidden><div class="preset-tabs" role="group" aria-label="Layout presets"></div>
        <div class="preset-actions"><button type="button" class="text-action" data-preset-new>New preset</button><button type="button" class="text-action" data-preset-rename>Rename</button><button type="button" class="text-action" data-preset-delete>Delete</button><button type="button" class="text-action" data-preset-activate>Activate</button></div>
        <form class="preset-rename" hidden><label>Preset name<input name="preset" maxlength="24" autocomplete="off" required></label><div class="actions"><button type="submit">Save name</button><button type="button" class="quiet" data-rename-cancel>Cancel</button></div></form></div>
      <p class="small">Drag screens to match your desk. Touching edges connect. Calibration supplies estimated starting sizes; use the pencil to enter an exact resolution. One screen per slot.</p>
      <div class="monitor-stage" aria-label="Monitor arrangement"></div><div class="monitor-excluded" hidden></div>
      <form class="monitor-editor" hidden>
        <label class="monitor-included"><input name="enabled" type="checkbox"> Include this screen</label>
        <div class="monitor-selected" hidden><span data-selected-name></span><button type="button" class="quiet" data-selected-hide>Not on screen</button></div>
      </form>
      <p class="small" data-status role="status"></p><div class="actions"><button data-save hidden>Save arrangement</button><button data-discard class="quiet" hidden>Discard changes</button></div>`;
    this.stage=root.querySelector('.monitor-stage');this.editor=root.querySelector('.monitor-editor');this.excluded=root.querySelector('.monitor-excluded');
    this.tabs=root.querySelector('.preset-tabs');this.renameForm=root.querySelector('.preset-rename');
    this.editor.addEventListener('submit',e=>e.preventDefault());
    this.editor.addEventListener('input',e=>{
      if(this.selected<0||this.busy)return;
      this.draft.screens[this.selected].enabled=this.editor.elements.enabled.checked;
      this.changed();
    });
    this.stage.addEventListener('pointerdown',e=>{
      const button=e.target.closest('[data-slot]');if(!button||this.busy||this.renaming||this.resolutionEditing!==undefined||this.speedEditing!==undefined||e.target.closest('button,input,form'))return;
      this.selected=Number(button.dataset.slot);this.edit();button.focus();
      this.drag={id:e.pointerId,x:e.clientX,y:e.clientY,screen:clone(this.draft.screens[this.selected]),frame:this.frame};
      this.stage.setPointerCapture(e.pointerId);
    });
    this.stage.addEventListener('pointermove',e=>{
      if(!this.drag||this.drag.id!==e.pointerId)return;
      const d=this.drag,screen=this.draft.screens[this.selected];
      if(!d.moved&&Math.hypot(e.clientX-d.x,e.clientY-d.y)<3)return;
      d.moved=true;
      screen.x=Math.round(d.screen.x+(e.clientX-d.x)/d.frame.scale);screen.y=Math.round(d.screen.y+(e.clientY-d.y)/d.frame.scale);
      this.snap(screen,18/d.frame.scale);this.dirty=true;this.message='';this.draw(d.frame);this.edit();this.controls();
    });
    const end=e=>{if(!this.drag||this.drag.id!==e.pointerId)return;this.drag=null;this.draw();this.controls();};
    this.stage.addEventListener('pointerup',end);this.stage.addEventListener('pointercancel',end);
    this.stage.addEventListener('click',e=>{if(this.resolutionEditing!==undefined||this.speedEditing!==undefined)return;if(e.target.closest('button,input,form'))return;const b=e.target.closest('[data-slot]');if(b){this.selected=Number(b.dataset.slot);this.edit();this.draw();this.controls();}});
    this.stage.addEventListener('keydown',e=>{
      const b=e.target.closest('[data-slot]');if(!b||this.busy||this.renaming||this.resolutionEditing!==undefined||this.speedEditing!==undefined||e.target.closest('button,input,form'))return;
      const moves={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,-1],ArrowDown:[0,1]};if(!moves[e.key])return;
      e.preventDefault();this.selected=Number(b.dataset.slot);const s=this.draft.screens[this.selected],step=e.shiftKey?1:10;
      s.x+=moves[e.key][0]*step;s.y+=moves[e.key][1]*step;this.changed();this.stage.querySelector(`[data-slot="${this.selected}"]`).focus();
    });
    root.querySelector('[data-selected-hide]').onclick=()=>this.hideScreen(this.selected);
    root.querySelector('[data-row]').onclick=()=>{let x=0;for(const s of this.draft.screens){s.x=x;s.y=0;if(s.enabled)x+=s.width;}this.changed();};
    root.querySelector('[data-discard]').onclick=()=>{this.dirty=false;this.message='';this.reseed();this.controls();};
    root.querySelector('[data-save]').onclick=()=>this.commit();
    this.tabs.addEventListener('keydown',e=>{
      const step={ArrowLeft:-1,ArrowRight:1}[e.key];const tabs=[...this.tabs.querySelectorAll('button:not(:disabled)')];
      const at=tabs.indexOf(document.activeElement);if(!step||at<0)return;
      e.preventDefault();tabs[(at+step+tabs.length)%tabs.length].focus();
    });
    root.querySelector('[data-preset-new]').onclick=()=>this.newPreset();
    root.querySelector('[data-preset-rename]').onclick=()=>{this.renaming=true;this.message='';this.renameForm.elements.preset.value=this.viewedPreset().name;this.renderPresets();this.controls();this.renameForm.elements.preset.focus();this.renameForm.elements.preset.select();};
    root.querySelector('[data-rename-cancel]').onclick=()=>this.closeRename();
    this.renameForm.addEventListener('keydown',e=>{if(e.key==='Escape'){e.preventDefault();this.closeRename();}});
    this.renameForm.onsubmit=e=>{e.preventDefault();this.rename(this.renameForm.elements.preset.value.trim());};
    root.querySelector('[data-preset-delete]').onclick=()=>this.deletePreset();
    root.querySelector('[data-preset-activate]').onclick=()=>this.act(()=>this.run('preset-activate',{index:this.viewing}));
    new ResizeObserver(()=>{if(this.draft&&!this.drag){this.draw();this.controls();}}).observe(this.stage);
  }
  get editing(){return this.resolutionEditing!==undefined||this.speedEditing!==undefined;}
  viewedPreset(){return this.presets?.list.find(p=>p.index===this.viewing);}
  // The layout being viewed: the viewed preset when the firmware has presets,
  // otherwise the single stored arrangement.
  storedLayout(){
    const layout=this.status.monitor_layout;
    if(!this.presets)return clone(layout);
    return {active:this.presets.arrangement,generation:this.presets.generation,
      screens:this.viewedPreset().screens.map((s,i)=>({...s,span_x:layout.screens[i]?.span_x,span_y:layout.screens[i]?.span_y}))};
  }
  update(status){
    this.root.hidden=!status.absolute||!status.monitor_layout||status.calibration_active;if(this.root.hidden)return;
    this.status=status;this.slots=status.slots;
    this.names=status.slots.map(s=>s.name);this.connected=status.slots.map(s=>s.connected);
    const presets=status.presets?clone(status.presets):null;
    const baseGeneration=this.presets?.generation,baseViewed=this.dirty&&this.presets?clone(this.viewedPreset()):null;
    if(!!presets!==!!this.presets){this.dirty=false;this.renaming=false;this.viewing=undefined;}
    this.presets=presets;
    if(presets&&!presets.list.some(p=>p.index===this.viewing)){this.viewing=presets.active;this.dirty=false;this.renaming=false;}
    // A preset activation bumps the generation without touching the viewed preset:
    // keep an unsaved draft saveable when its stored base is unchanged. A draft
    // already behind the previous generation was built from an older preset: never rebase it.
    if(this.dirty&&baseViewed&&this.draft&&this.draft.generation===baseGeneration&&presets.generation!==baseGeneration){
      const now=presets.list.find(p=>p.index===baseViewed.index);
      if(now&&now.name===baseViewed.name&&JSON.stringify(now.screens)===JSON.stringify(baseViewed.screens))this.draft.generation=presets.generation;
    }
    this.stored=this.storedLayout();
    if(!this.dirty&&!this.busy&&!this.drag&&!this.editing)this.reseed();
    this.renderPresets();this.controls();
  }
  reseed(){this.draft=seedArrangement(this.stored,this.slots,!!this.presets);this.draw();this.edit();}
  changed(){this.dirty=true;this.message='';this.draw();this.edit();this.controls();}
  view(index){
    if(this.dirty||this.busy||this.editing)return;
    this.viewing=index;this.renaming=false;this.selected=-1;this.message='';this.stored=this.storedLayout();this.reseed();this.renderPresets();this.controls();
  }
  renderPresets(){
    const bar=this.root.querySelector('.preset-bar');bar.hidden=!this.presets;if(!this.presets)return;
    const {list,active}=this.presets;
    const signature=JSON.stringify([list.map(p=>[p.index,p.name]),active,this.viewing]);
    if(signature!==this.presetSignature){
      this.presetSignature=signature;const focused=this.tabs.contains(document.activeElement)?document.activeElement.dataset.preset:null;
      this.tabs.replaceChildren(...[...list].sort((a,b)=>a.index-b.index).map(p=>{
        const tab=document.createElement('button');tab.type='button';tab.className='preset-tab';tab.dataset.preset=p.index;
        tab.setAttribute('aria-pressed',String(p.index===this.viewing));tab.setAttribute('aria-label',p.index===active?`${p.name}, active`:p.name);
        const name=document.createElement('span');name.textContent=p.name;tab.append(name);
        if(p.index===active){const badge=document.createElement('span');badge.className='preset-badge';badge.textContent='Active';tab.append(badge);}
        tab.onclick=()=>this.view(p.index);return tab;
      }));
      if(focused!==null)this.tabs.querySelector(`[data-preset="${focused}"]`)?.focus({preventScroll:true});
    }
    const name=this.viewedPreset()?.name||'';
    this.root.querySelector('.preset-actions').hidden=this.renaming;this.renameForm.hidden=!this.renaming;
    this.root.querySelector('[data-preset-rename]').setAttribute('aria-label',`Rename preset ${name}`);
    this.root.querySelector('[data-preset-delete]').setAttribute('aria-label',`Delete preset ${name}`);
    this.root.querySelector('[data-preset-activate]').setAttribute('aria-label',`Activate preset ${name}`);
    this.root.querySelector('[data-preset-delete]').hidden=list.length<=1;
    this.root.querySelector('[data-preset-activate]').hidden=this.viewing===active;
  }
  closeRename(){this.renaming=false;this.renderPresets();this.controls();this.root.querySelector('[data-preset-rename]').focus();}
  async act(task,next){
    if(this.busy)return false;this.busy=true;this.message='';this.controls();
    let ok=false;
    try{await task();ok=true;}catch(error){this.message=error.message;}
    finally{
      this.busy=false;
      if(ok){this.dirty=false;this.renaming=false;if(next!==undefined&&this.presets?.list.some(p=>p.index===next))this.viewing=next;}
      if(this.presets&&!this.presets.list.some(p=>p.index===this.viewing))this.viewing=this.presets.active;
      if(ok){this.selected=-1;this.stored=this.storedLayout();this.reseed();}
      this.renderPresets();this.controls();
    }
    return ok;
  }
  newPreset(){
    const {list,generation}=this.presets,source=this.viewedPreset();
    const index=[0,1,2,3].find(i=>!list.some(p=>p.index===i));if(index===undefined)return;
    const name=presetCopyName(list.map(p=>p.name),source.name);
    return this.act(()=>this.run('preset-save',presetPayload({index,name,screens:source.screens},generation)),index);
  }
  rename(name){
    const preset=this.viewedPreset();
    if(!validPresetName(name))this.message='Preset names are 1–24 letters, numbers, spaces or punctuation.';
    else if(this.presets.list.some(p=>p.index!==preset.index&&p.name.toLowerCase()===name.toLowerCase()))this.message='Another preset already uses that name.';
    else return this.act(()=>this.run('preset-save',presetPayload({...preset,name},this.presets.generation)));
    this.controls();
  }
  deletePreset(){
    const preset=this.viewedPreset();
    if(!confirm(`Delete preset “${preset.name}”? Its shortcuts are removed too.`))return;
    return this.act(()=>this.run('preset-delete',{index:preset.index,generation:this.presets.generation})).then(ok=>{
      if(ok)this.tabs.querySelector(`[data-preset="${this.presets.active}"]`)?.focus();
      return ok;
    });
  }
  hideScreen(i){
    if(i<0||this.busy)return;
    this.draft.screens[i].enabled=false;if(this.selected===i)this.selected=-1;this.changed();
    this.excluded.querySelector(`[data-excluded="${i}"] button`)?.focus();
  }
  snap(screen,tolerance){
    let bestX=tolerance,bestY=tolerance,dx=0,dy=0;
    for(const other of this.draft.screens){if(other===screen||!other.enabled)continue;
      for(const x of [other.x-screen.width,other.x+other.width,other.x,other.x+other.width-screen.width])if(Math.abs(x-screen.x)<bestX){dx=x-screen.x;bestX=Math.abs(dx);}
      for(const y of [other.y-screen.height,other.y+other.height,other.y,other.y+other.height-screen.height])if(Math.abs(y-screen.y)<bestY){dy=y-screen.y;bestY=Math.abs(dy);}
    }
    screen.x+=dx;screen.y+=dy;
  }
  draw(frame){
    if(!this.draft)return;
    const screens=this.draft.screens,presetMode=!!this.presets;
    const safe=screens.map((s,i)=>{const paired=!!(this.slots?.[i]?.paired||this.slots?.[i]?.connected);const available=presetMode?!!s.enabled&&paired:!!(paired||!this.stored?.active);return {...s,paired,enabled:available,x:Number.isFinite(s.x)?s.x:0,y:Number.isFinite(s.y)?s.y:0,width:Number.isFinite(s.width)?Math.max(64,s.width):1920,height:Number.isFinite(s.height)?Math.max(64,s.height):1080};});
    const visible=safe.filter(s=>s.enabled);const bounds=visible.length?visible:safe;
    const minX=Math.min(...bounds.map(s=>s.x)),minY=Math.min(...bounds.map(s=>s.y));
    const width=Math.max(...bounds.map(s=>s.x+s.width))-minX,height=Math.max(...bounds.map(s=>s.y+s.height))-minY;
    const area=this.stage.clientWidth;
    this.frame=frame||{scale:Math.min(Math.max(1,area-24)/width,226/height),x:minX,y:minY};
    const f=this.frame;const oldFocus=this.stage.contains(document.activeElement)?document.activeElement.dataset.slot:null;
    this.stage.replaceChildren();
    const excluded=this.excluded;excluded.replaceChildren();
    safe.forEach((s,i)=>{
      if(!s.enabled){
        // Paired computers left off this preset wait in the tray; unpaired slots stay hidden.
        if(presetMode&&s.paired){
          const chip=document.createElement('div');chip.className='monitor-chip';chip.dataset.excluded=i;
          const label=document.createElement('span');label.textContent=`${i+1} · ${this.names[i]}`;
          const show=document.createElement('button');show.type='button';show.className='quiet';show.textContent='Show on screen';show.setAttribute('aria-label',`Show slot ${i+1} on screen`);
          show.onclick=()=>{placeOnScreen(this.draft.screens,i);this.selected=i;this.changed();this.stage.querySelector(`[data-slot="${i}"]`)?.focus();};
          chip.append(label,show);excluded.append(chip);
        }
        return;
      }
      const b=document.createElement('div');b.tabIndex=0;b.setAttribute('role','group');b.dataset.slot=i;b.className='monitor-screen';b.classList.toggle('selected',i===this.selected);b.classList.toggle('excluded',!s.enabled);
      b.setAttribute('aria-label',`Slot ${i+1}: ${this.names[i]}. Edit dimensions`);
      Object.assign(b.style,{left:`${12+(s.x-f.x)*f.scale}px`,top:`${12+(s.y-f.y)*f.scale}px`,width:`${s.width*f.scale}px`,height:`${s.height*f.scale}px`});
      const titleLine=document.createElement('div');titleLine.className='monitor-title-line';
      const title=document.createElement('span');title.textContent=`${i+1} · ${this.names[i]}`;titleLine.append(title);
      if(presetMode){
        const hide=document.createElement('button');hide.type='button';hide.className='monitor-pencil monitor-hide';hide.dataset.hide=i;
        hide.setAttribute('aria-label',`Mark slot ${i+1} not on screen`);hide.title='Not on screen';hide.innerHTML=eyeOff;
        hide.onclick=()=>this.hideScreen(i);
        titleLine.append(hide);
      }
      b.append(titleLine);
      const resolution=document.createElement('div');resolution.className='monitor-resolution-line';
      const value=document.createElement('span');value.textContent=`${screens[i].estimated?'≈ ':''}${screens[i].width} × ${screens[i].height}`;
      const pencil=document.createElement('button');pencil.type='button';pencil.className='monitor-pencil';
      pencil.setAttribute('aria-label',`Edit resolution for slot ${i+1}`);
      pencil.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m16 3 5 5-12 12-6 1 1-6Z M14 5l5 5" fill="none" stroke="currentColor" stroke-width="2"/></svg>';
      pencil.onclick=()=>this.openResolution(i);
      resolution.append(value,pencil);b.append(resolution);
      const speed=document.createElement('div');speed.className='monitor-speed-line';
      if(this.speedEditing===i){
        const input=document.createElement('input');input.type='text';input.inputMode='numeric';input.value=screens[i].sensitivity??100;input.setAttribute('aria-label',`Speed for slot ${i+1} percent`);
        const percent=document.createElement('span');percent.textContent='%';
        const save=document.createElement('button');save.type='button';save.className='monitor-pencil';save.setAttribute('aria-label',`Save speed for slot ${i+1}`);save.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m5 12 4 4L19 6" fill="none" stroke="currentColor" stroke-width="2.5"/></svg>';
        save.onclick=()=>{const value=Number(input.value);if(Number.isInteger(value)&&value>=25&&value<=400){screens[i].sensitivity=value;this.speedEditing=undefined;this.changed();}};
        input.onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();save.click();}if(e.key==='Escape'){e.preventDefault();this.speedEditing=undefined;this.draw();this.controls();}};
        speed.append(input,percent,save);
      }else{
        const label=document.createElement('span');label.textContent=`Speed ${screens[i].sensitivity??100}%`;
        const speedPencil=document.createElement('button');speedPencil.type='button';speedPencil.className='monitor-pencil';speedPencil.setAttribute('aria-label',`Edit speed for slot ${i+1}`);speedPencil.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m16 3 5 5-12 12-6 1 1-6Z M14 5l5 5" fill="none" stroke="currentColor" stroke-width="2"/></svg>';speedPencil.onclick=()=>{this.speedEditing=i;this.selected=i;this.draw();this.controls();this.stage.querySelector(`[data-slot="${i}"] input`)?.focus();};
        speed.append(label,speedPencil);
      }
      b.append(speed);
      if(!this.connected[i]){const offline=document.createElement('span');offline.textContent='Offline';b.append(offline);}
      this.stage.append(b);
    });
    if(excluded.childElementCount){const heading=document.createElement('span');heading.className='small';heading.textContent='Not on screen';excluded.prepend(heading);}
    excluded.hidden=!excluded.childElementCount;
    if(oldFocus!==null&&!this.drag)this.stage.querySelector(`[data-slot="${oldFocus}"]`)?.focus({preventScroll:true});
  }
  openResolution(slot){
    if(this.busy||this.editing)return;
    this.selected=slot;this.draw();this.edit();
    this.resolutionEditing=slot;
    const screen=this.draft.screens[slot],box=this.stage.querySelector(`[data-slot="${slot}"]`);
    const form=document.createElement('form');form.className='monitor-resolution-edit';
    form.setAttribute('aria-label',`Resolution for slot ${slot+1}`);
    form.innerHTML='<input name="width" type="text" inputmode="numeric" aria-label="Width in pixels" required><span aria-hidden="true"> × </span><input name="height" type="text" inputmode="numeric" aria-label="Height in pixels" required><button type="submit" class="monitor-pencil" aria-label="Save resolution"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m5 12 4 4L19 6" fill="none" stroke="currentColor" stroke-width="2.5"/></svg></button>';
    form.elements.width.value=screen.width;form.elements.height.value=screen.height;
    const finish=()=>{this.resolutionEditing=undefined;this.draw();this.edit();this.controls();this.stage.querySelector(`[data-slot="${slot}"] .monitor-resolution-line .monitor-pencil`)?.focus();};
    form.onsubmit=e=>{e.preventDefault();if(!form.reportValidity())return;
      const width=Number(form.elements.width.value),height=Number(form.elements.height.value);
      if(!Number.isInteger(width)||!Number.isInteger(height)||width<64||height<64||width>16384||height>16384)return;
      screen.width=width;screen.height=height;screen.estimated=false;this.dirty=true;this.message='';finish();
    };
    form.onkeydown=e=>{if(e.key==='Escape'){e.preventDefault();finish();}};
    box.querySelector('.monitor-resolution-line').replaceChildren(form);this.controls();form.elements.width.focus();form.elements.width.select();
  }
  edit(){
    this.editor.hidden=this.selected<0;if(this.editor.hidden)return;
    // Preset firmware stores on-screen state; the button repeats the screen's own
    // action for screens drawn too small to show it.
    this.editor.querySelector('.monitor-included').hidden=!!this.presets;this.editor.querySelector('.monitor-selected').hidden=!this.presets;
    this.editor.querySelector('[data-selected-name]').textContent=`Slot ${this.selected+1} · ${this.names[this.selected]}`;
    this.editor.elements.enabled.checked=this.draft.screens[this.selected].enabled;
  }
  controls(){
    if(!this.draft)return;
    const editing=this.editing,valid=validLayout(this.draft.screens)&&!editing,changed=this.dirty;
    const estimated=this.draft.screens.some(s=>s.enabled&&s.estimated);
    // With the arrangement off, saving the active preset is what turns it on.
    const useArrangement=!this.stored.active&&(!this.presets||this.viewing===this.presets.active);
    const save=this.root.querySelector('[data-save]');
    save.hidden=!changed&&!useArrangement;
    save.textContent=useArrangement?'Use this arrangement':this.presets?'Save preset':'Save arrangement';
    save.disabled=!valid||this.busy;
    this.root.querySelector('[data-discard]').hidden=!changed;
    for(const b of this.root.querySelectorAll('[data-row],[data-discard],.monitor-excluded button,.monitor-stage input'))b.disabled=this.busy||this.renaming||((this.resolutionEditing!==undefined&&!b.closest('.monitor-resolution-edit'))||(this.speedEditing!==undefined&&!b.closest('.monitor-speed-line')));
    // Unsaved screen edits would be lost when the rename saves, so the stage is read-only meanwhile.
    for(const b of this.root.querySelectorAll('.monitor-stage .monitor-pencil'))b.disabled=this.renaming||this.busy;
    this.editor.elements.enabled.disabled=this.busy||editing||this.renaming;
    // Unpaired slots are never drawn in preset mode, so they do not count.
    const onScreen=this.draft.screens.filter((s,i)=>s.enabled&&(!this.presets||this.slots?.[i]?.paired||this.slots?.[i]?.connected)).length;
    for(const b of this.root.querySelectorAll('.monitor-hide,[data-selected-hide]')){b.disabled=this.busy||editing||this.renaming||onScreen<=1;b.title=onScreen<=1?'Keep at least one screen on screen':'Not on screen';}
    if(this.presets){
      // Preset actions work on saved presets only, so a draft is never lost.
      const locked=this.busy||editing||changed||this.renaming,hint=changed?'Save or discard changes first':'';
      for(const b of this.root.querySelectorAll('.preset-tabs button,.preset-actions button')){b.disabled=locked&&b.getAttribute('aria-pressed')!=='true';b.title=b.disabled?hint:'';}
      const full=this.presets.list.length>=PRESET_COUNT,add=this.root.querySelector('[data-preset-new]');
      if(full){add.disabled=true;add.title=`Up to ${PRESET_COUNT} presets`;}
      for(const b of this.renameForm.querySelectorAll('input,button'))b.disabled=this.busy;
    }
    const viewingActive=!this.presets||this.viewing===this.presets.active;
    const text=this.busy?'Saving…':this.message?this.message:this.resolutionEditing!==undefined?'Finish editing the resolution.':this.speedEditing!==undefined?'Finish editing screen speed.':!valid?'Check for overlapping screens, invalid dimensions, or sensitivity outside 25–400%. Keep at least one screen on screen.':changed?'Preview — save to apply. Gaps and uncovered edges will not switch.':!this.stored.active?(viewingActive?'Select a screen to set its resolution, then save the arrangement.':'Saved on board. Activate this preset and use its arrangement to switch at its edges.'):viewingActive?'Saved on board. Shared edges connect available computers.':'Saved on board. Activate this preset to use its edges.';
    this.root.querySelector('[data-status]').textContent=text+(estimated&&!this.message?' ≈ marks an estimated resolution.':'');
  }
  async commit(){
    if(this.busy)return;
    const screens=clone(this.draft.screens),generation=this.draft.generation,useArrangement=!this.stored.active&&(!this.presets||this.viewing===this.presets.active);
    await this.act(()=>this.presets&&!useArrangement
      ?this.run('preset-save',presetPayload({...this.viewedPreset(),screens},generation))
      :this.run('monitor-layout',{generation,active:true,screens}));
  }
}
