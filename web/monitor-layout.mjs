const clone = value => structuredClone(value);
export function validLayout(screens) {
  if (!screens.some(s=>s.enabled)) return false;
  return screens.every((a,i)=>Number.isInteger(a.sensitivity??100)&&(a.sensitivity??100)>=25&&(a.sensitivity??100)<=400&&Number.isInteger(a.x)&&Number.isInteger(a.y)&&Math.abs(a.x)<=65536&&Math.abs(a.y)<=65536&&Number.isInteger(a.width)&&Number.isInteger(a.height)&&a.width>=64&&a.width<=16384&&a.height>=64&&a.height<=16384&&!screens.some((b,j)=>j<i&&a.enabled&&b.enabled&&a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height));
}
// Calibration is raw travel, not a resolution measurement. A 1920 px anchor
// supplies a preview scale only; every seeded dimension remains estimated.
export function seedArrangement(layout,slots) {
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
    s.enabled=!!(slots[i].paired||slots[i].connected||s.span_x);
    s.x=x;s.y=0;x+=s.width;
  }
  return draft;
}
export class MonitorArrangement {
  constructor(root, save) {
    this.root=root;this.save=save;this.selected=-1;this.dirty=false;this.busy=false;
    root.innerHTML=`<div class="section-heading"><h3>Monitor arrangement</h3><button type="button" class="text-action" data-row>Arrange in a row</button></div>
      <p class="small">Drag screens to match your desk. Touching edges connect. Calibration supplies estimated starting sizes; use the pencil to enter an exact resolution. One screen per slot.</p>
      <div class="monitor-stage" aria-label="Monitor arrangement"></div><div class="monitor-excluded actions"></div>
      <form class="monitor-editor" hidden>
        <label class="monitor-included"><input name="enabled" type="checkbox"> Include this screen</label>
      </form>
      <p class="small" data-status role="status"></p><div class="actions"><button data-save hidden>Save arrangement</button><button data-discard class="quiet" hidden>Discard changes</button></div>`;
    this.stage=root.querySelector('.monitor-stage');this.editor=root.querySelector('form');
    this.editor.addEventListener('submit',e=>e.preventDefault());
    this.editor.addEventListener('input',e=>{
      if(this.selected<0||this.busy)return;
      
      this.draft.screens[this.selected].enabled=this.editor.elements.enabled.checked;
      this.dirty=true;this.draw();this.controls();
    });
    this.stage.addEventListener('pointerdown',e=>{
      const button=e.target.closest('[data-slot]');if(!button||this.busy||this.resolutionEditing!==undefined||this.speedEditing!==undefined||e.target.closest('button,input,form'))return;
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
      this.snap(screen,18/d.frame.scale);this.dirty=true;this.draw(d.frame);this.edit();this.controls();
    });
    const end=e=>{if(!this.drag||this.drag.id!==e.pointerId)return;this.drag=null;this.draw();this.controls();};
    this.stage.addEventListener('pointerup',end);this.stage.addEventListener('pointercancel',end);
    this.stage.addEventListener('click',e=>{if(this.resolutionEditing!==undefined||this.speedEditing!==undefined)return;if(e.target.closest('button,input,form'))return;const b=e.target.closest('[data-slot]');if(b){this.selected=Number(b.dataset.slot);this.edit();this.draw();}});
    this.stage.addEventListener('keydown',e=>{
      const b=e.target.closest('[data-slot]');if(!b||this.busy||this.resolutionEditing!==undefined||this.speedEditing!==undefined||e.target.closest('button,input,form'))return;
      const moves={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,-1],ArrowDown:[0,1]};if(!moves[e.key])return;
      e.preventDefault();this.selected=Number(b.dataset.slot);const s=this.draft.screens[this.selected],step=e.shiftKey?1:10;
      s.x+=moves[e.key][0]*step;s.y+=moves[e.key][1]*step;this.dirty=true;this.draw();this.edit();this.controls();this.stage.querySelector(`[data-slot="${this.selected}"]`).focus();
    });
    root.querySelector('[data-row]').onclick=()=>{let x=0;for(const s of this.draft.screens){s.x=x;s.y=0;if(s.enabled)x+=s.width;}this.dirty=true;this.draw();this.edit();this.controls();};
    root.querySelector('[data-discard]').onclick=()=>{this.draft=seedArrangement(this.stored,this.slots);this.dirty=false;this.draw();this.edit();this.controls();};
    root.querySelector('[data-save]').onclick=()=>this.commit(true);
    new ResizeObserver(()=>{if(this.draft&&!this.drag)this.draw();}).observe(this.stage);
  }
  update(status){
    this.root.hidden=!status.absolute||!status.monitor_layout||status.calibration_active;if(this.root.hidden)return;
    this.slots=status.slots;
    this.names=status.slots.map(s=>s.name);this.connected=status.slots.map(s=>s.connected);
    this.stored=clone(status.monitor_layout);
    if(!this.dirty&&!this.busy&&!this.drag&&this.resolutionEditing===undefined&&this.speedEditing===undefined){this.draft=seedArrangement(this.stored,this.slots);this.draw();this.edit();}
    this.controls();
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
    const screens=this.draft.screens;
    const safe=screens.map((s,i)=>{const available=!!(this.slots?.[i]?.paired||this.slots?.[i]?.connected||!this.stored?.active);return {...s,enabled:available,x:Number.isFinite(s.x)?s.x:0,y:Number.isFinite(s.y)?s.y:0,width:Number.isFinite(s.width)?Math.max(64,s.width):1920,height:Number.isFinite(s.height)?Math.max(64,s.height):1080};});
    const visible=safe.filter(s=>s.enabled);const bounds=visible.length?visible:safe;
    const minX=Math.min(...bounds.map(s=>s.x)),minY=Math.min(...bounds.map(s=>s.y));
    const width=Math.max(...bounds.map(s=>s.x+s.width))-minX,height=Math.max(...bounds.map(s=>s.y+s.height))-minY;
    const area=this.stage.clientWidth;
    this.frame=frame||{scale:Math.min(Math.max(1,area-24)/width,226/height),x:minX,y:minY};
    const f=this.frame;const oldFocus=this.stage.contains(document.activeElement)?document.activeElement.dataset.slot:null;
    this.stage.replaceChildren();
    const excluded=this.root.querySelector('.monitor-excluded');excluded.replaceChildren();
    safe.forEach((s,i)=>{
      if(!s.enabled){
        return;
      }
      const b=document.createElement('div');b.tabIndex=0;b.setAttribute('role','group');b.dataset.slot=i;b.className='monitor-screen';b.classList.toggle('selected',i===this.selected);b.classList.toggle('excluded',!s.enabled);
      b.setAttribute('aria-label',`Slot ${i+1}: ${this.names[i]}. Edit dimensions`);
      Object.assign(b.style,{left:`${12+(s.x-f.x)*f.scale}px`,top:`${12+(s.y-f.y)*f.scale}px`,width:`${s.width*f.scale}px`,height:`${s.height*f.scale}px`});
      const title=document.createElement('span');title.textContent=`${i+1} · ${this.names[i]}`;b.append(title);
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
        save.onclick=()=>{const value=Number(input.value);if(Number.isInteger(value)&&value>=25&&value<=400){screens[i].sensitivity=value;this.dirty=true;this.speedEditing=undefined;this.draw();this.edit();this.controls();}};
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
    if(oldFocus!==null&&!this.drag)this.stage.querySelector(`[data-slot="${oldFocus}"]`)?.focus({preventScroll:true});
  }
  openResolution(slot){
    if(this.busy||this.resolutionEditing!==undefined||this.speedEditing!==undefined)return;
    this.selected=slot;this.draw();this.edit();
    this.resolutionEditing=slot;
    const screen=this.draft.screens[slot],box=this.stage.querySelector(`[data-slot="${slot}"]`);
    const form=document.createElement('form');form.className='monitor-resolution-edit';
    form.setAttribute('aria-label',`Resolution for slot ${slot+1}`);
    form.innerHTML='<input name="width" type="text" inputmode="numeric" aria-label="Width in pixels" required><span aria-hidden="true"> × </span><input name="height" type="text" inputmode="numeric" aria-label="Height in pixels" required><button type="submit" class="monitor-pencil" aria-label="Save resolution"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m5 12 4 4L19 6" fill="none" stroke="currentColor" stroke-width="2.5"/></svg></button>';
    form.elements.width.value=screen.width;form.elements.height.value=screen.height;
    const finish=()=>{this.resolutionEditing=undefined;this.draw();this.edit();this.controls();this.stage.querySelector(`[data-slot="${slot}"] .monitor-pencil`)?.focus();};
    form.onsubmit=e=>{e.preventDefault();if(!form.reportValidity())return;
      const width=Number(form.elements.width.value),height=Number(form.elements.height.value);
      if(!Number.isInteger(width)||!Number.isInteger(height)||width<64||height<64||width>16384||height>16384)return;
      screen.width=width;screen.height=height;screen.estimated=false;this.dirty=true;finish();
    };
    form.onkeydown=e=>{if(e.key==='Escape'){e.preventDefault();finish();}};
    box.querySelector('.monitor-resolution-line').replaceChildren(form);this.controls();form.elements.width.focus();form.elements.width.select();
  }
  edit(){
    this.editor.hidden=this.selected<0;if(this.selected<0)return;
    const s=this.draft.screens[this.selected];
      this.editor.elements.enabled.checked=s.enabled;

  }
  controls(){
    if(!this.draft)return;
    const valid=validLayout(this.draft.screens)&&this.resolutionEditing===undefined&&this.speedEditing===undefined,changed=this.dirty;
    const estimated=this.draft.screens.some(s=>s.enabled&&s.estimated);
    this.root.querySelector('[data-save]').hidden=!changed&&!!this.stored.active;
    this.root.querySelector('[data-save]').textContent=this.stored.active?'Save arrangement':'Use this arrangement';
    this.root.querySelector('[data-save]').disabled=!valid||this.busy;
    this.root.querySelector('[data-discard]').hidden=!changed;
    for(const b of this.root.querySelectorAll('[data-row],[data-discard],input'))b.disabled=this.busy||((this.resolutionEditing!==undefined&&!b.closest('.monitor-resolution-edit'))||(this.speedEditing!==undefined&&!b.closest('.monitor-speed-line')));
    this.root.querySelector('[data-status]').textContent=this.busy?'Saving…':this.resolutionEditing!==undefined?'Finish editing the resolution.':this.speedEditing!==undefined?'Finish editing screen speed.':!valid?'Check for overlapping screens, invalid dimensions, or sensitivity outside 25–400%.':changed?'Preview — save to apply. Gaps and uncovered edges will not switch.':this.stored.active?'Saved on board. Shared edges connect available computers.':'Select a screen to set its resolution, then save the arrangement.';
    if(estimated)this.root.querySelector('[data-status]').textContent+=' ≈ marks an estimated resolution.';
  }
  async commit(active){
    if(this.busy)return;this.busy=true;this.controls();
    try{
      const screens=clone(this.draft.screens);
      if(!active){let x=0;for(const s of screens){s.x=x;s.y=0;x+=s.width;}}
      await this.save({generation:this.draft.generation,active,screens});
      this.dirty=false;this.draft=clone(this.stored);this.draw();this.edit();
    }catch(error){this.error=error.message;}
    finally{this.busy=false;this.controls();if(this.error){this.root.querySelector('[data-status]').textContent=this.error;this.error='';}}
  }
}
