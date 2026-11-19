// Run with NODE_PATH pointing to a Playwright installation and the packaged site served on localhost:8765.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true});
 const page=await browser.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.addInitScript(()=>{
   let controller;const enc=new TextEncoder();let decoder='';
   const state={protocol:1,firmware:{version:'test',date:'Sep 20 2026',time:'12:00:00',build_id:'test'},usb:{interfaces:[{address:1,interface:0,vid:123,pid:456,kind:'Keyboard',active:true},{address:1,interface:1,vid:123,pid:456,kind:'Other HID',active:true},{address:2,interface:0,vid:789,pid:12,kind:'Mouse',active:true}]},name:'HID SWITCHER BLE',selected:0,generation:0,ble_input:true,absolute:true,seamless:false,edge_distance:100,calibration_needed:1,calibration_ready:1,calibration_active:false,calibration_stage:0,calibration_result:'idle or cancelled',slots:[{name:'Computer 1',paired:0,connected:false},{name:'Computer 2',paired:0,connected:false},{name:'Computer 3',paired:0,connected:false}],shortcuts:{bindings:[[0,1,9,[43],0],[0,2,0,[],0],[3,1,9,[],0],...[4,5,6,7,8].flatMap(a=>[[a,1,0,[],0],[a,2,0,[],0]])]}};
   const input={available:true,busy:false,ready:false,saved:false,state:'not paired',device:'',saved_name:'',pairing:{active:false,kind:'none'},status:'Not connected',devices:'',count:0,revision:0};
   // Preset firmware: status.monitor_layout reports the active preset plus calibration spans.
   const spans=[{span_x:3000,span_y:1500},{span_x:1500,span_y:1000},{span_x:0,span_y:0}];
   state.presets={generation:0,active:0,arrangement:false,list:[{index:0,name:'Default',screens:[0,1920,3840].map(x=>({x,y:0,width:1920,height:1080,enabled:true,estimated:true,sensitivity:100}))}]};
   const strip=screens=>screens.map(({x,y,width,height,enabled,estimated,sensitivity=100})=>({x,y,width,height,enabled,estimated,sensitivity}));
   const sync=()=>{const p=state.presets,active=p.list.find(e=>e.index===p.active);state.monitor_layout={active:p.arrangement,generation:p.generation,screens:active.screens.map((s,i)=>({...s,...spans[i]}))};};
   sync();
   window.setupState=state;window.requests=[];
   const send=data=>controller.enqueue(enc.encode('@HID1 '+JSON.stringify(data)+'\n'));
   const port={
     async open(){if(this.readable?.locked||this.writable?.locked)throw new Error('Port still locked');window.portOpens=(window.portOpens||0)+1;this.readable=new ReadableStream({start(c){controller=c;}});this.writable=new WritableStream({write(bytes){
       decoder+=new TextDecoder().decode(bytes);
       while(decoder.includes('\n')){const cut=decoder.indexOf('\n');const line=decoder.slice(0,cut);decoder=decoder.slice(cut+1);const q=JSON.parse(line.slice(6));window.requests.push(q);if(window.noReply)continue;let result={};
        if(q.op==='status')result=state;
        else if(q.op==='input-status')result=input;
        else if(q.op==='input'){
         if(q.command==='scan'){input.devices='1 Combo keyboard [aa:bb]\n2 <img onerror=alert(1)> [cc:dd]\n';input.count=2;input.status='Scan complete';}
         if(q.command==='scan all'){input.devices='12 Phone simulator [ee:ff]\n';input.count=1;input.status='Scan complete';}
         if(q.command==='connect 12'){input.status='Compare 012345 with the code on your input device.';input.busy=true;input.pairing={active:true,kind:'compare',code:12345,id:8,seconds_left:25};}
         if(q.command==='connect 1'){input.status='Type 123456 on the WIRELESS keyboard, then press Enter.';input.busy=true;input.pairing={active:true,kind:'display',code:123456,id:7,seconds_left:25};}
         if(q.command==='confirm 012345'){input.status='Connected: 1 input reports ready.';input.busy=false;}
        }else if(q.op==='input-cancel'){input.busy=false;input.pairing.active=false;}
        else if(q.op==='input-confirm'){if(q.attempt!==input.pairing.id||q.code!==input.pairing.code){send({id:q.id,ok:false,error:'Pairing prompt expired'});continue;} input.pairing.active=false;input.busy=false;input.ready=true;input.saved=true;input.device=input.saved_name='Phone simulator';input.state='connected';input.status='Connected';}
        else if(q.op==='select'){state.selected=q.slot;controller.enqueue(enc.encode('[BLE computer pairing] Enter 654321 on the computer.\n'));}
        else if(q.op==='slots'){state.slots=q.order.map((i,n)=>({...state.slots[i],name:q.names[n]}));++state.generation;}
        else if(q.op==='monitor-layout'&&state.presets){if(q.generation!==state.presets.generation){send({id:q.id,ok:false,error:'Arrangement changed. Reload it before saving.'});continue;}const p=state.presets;p.list.find(e=>e.index===p.active).screens=strip(q.screens);p.arrangement=q.active;++p.generation;sync();}
        else if(q.op==='monitor-layout'){state.monitor_layout={active:q.active,generation:state.monitor_layout.generation+1,screens:q.screens};}
        else if(['preset-save','preset-delete'].includes(q.op)&&q.generation!==state.presets.generation){send({id:q.id,ok:false,error:'Presets changed. Reload them before saving.'});continue;}
        else if(q.op==='preset-save'){const p=state.presets,entry={index:q.index,name:q.name,screens:strip(q.screens)};p.list=[...p.list.filter(e=>e.index!==q.index),entry].sort((a,b)=>a.index-b.index);++p.generation;sync();result=p;}
        else if(q.op==='preset-activate'){const p=state.presets;p.active=q.index;++p.generation;sync();result=p;}
        else if(q.op==='preset-delete'){const p=state.presets;if(p.list.length<2){send({id:q.id,ok:false,error:'Choose a saved preset; the last preset cannot be deleted'});continue;}p.list=p.list.filter(e=>e.index!==q.index);if(p.active===q.index)p.active=p.list[0].index;++p.generation;sync();result=p;}
        else if(q.op==='pointer-speed'){state.pointer_speed=q.value;}
        else if(q.op==='seamless'){state.seamless=q.enabled;}
        else if(q.op==='calibration-start'){state.calibration_active=true;state.calibration_stage=1;}
        else if(q.op==='calibration-cancel'){state.calibration_active=false;}
        else if(q.op==='shortcut-record'){result={token:'test',state:'waiting'};}
        else if(q.op==='shortcut-status'){result={state:window.captureReady?'ready':'waiting',label:'Control + Tab'};}
        send({id:q.id,ok:true,result});
       }
     }});},async setSignals(){},async close(){window.portCloses=(window.portCloses||0)+1;}};
   Object.defineProperty(navigator,'serial',{value:{requestPort:async()=>port}});
 });
 await page.goto(process.env.SETUP_TEST_URL || 'http://127.0.0.1:8765');
 await page.waitForFunction(()=>customElements.get('esp-web-install-button'),{},{timeout:20000});
 await page.locator('#open-firmware').click();
 const versions=page.locator('#firmware-version option');
 assert(await versions.count()>=2);
 const oldVersion=await versions.nth(1).getAttribute('value');
 await page.locator('#firmware-version').selectOption(oldVersion);
 assert.equal(await page.locator('esp-web-install-button').getAttribute('manifest'),oldVersion);
 await page.locator('#close-firmware').click();
 await page.locator('#connect').click();await page.waitForFunction(()=>!document.getElementById('settings').disabled);
 assert.equal(await page.locator('#scan-all').isVisible(),false);
 assert.equal(await page.locator('#forget-input').isVisible(),false);
 assert.equal(await page.locator('#usb-devices .device-card').count(),2);
 await page.locator('#usb-devices').locator('..').screenshot({path:'artifacts/ble-add.png'});
 await page.locator('#add-ble').click();
 assert.equal(await page.locator('#ble-dialog').isVisible(),true);
 await page.locator('#scan').click();await page.getByRole('button',{name:'Combo keyboard [aa:bb]',exact:true}).waitFor();
 assert.equal(await page.locator('#devices img').count(),0);
 await page.getByRole('button',{name:'Combo keyboard [aa:bb]',exact:true}).click();
 await page.waitForFunction(()=>document.getElementById('input-status').textContent.includes('123456'));
 assert.equal(await page.locator('#pairing-code').textContent(),'123456');
 assert.equal(await page.locator('#input-confirm').isVisible(),false);
 assert.equal(await page.locator('#scan').isVisible(),false);
 await page.locator('#ble-dialog').screenshot({path:'artifacts/ble-passkey.png'});
 await page.keyboard.press('Escape');
 await page.waitForFunction(()=>!document.getElementById('ble-dialog').open);
 assert(await page.evaluate(()=>window.requests.some(r=>r.op==='input-cancel')));
 await page.locator('#add-ble').click();
 await page.waitForFunction(()=>document.getElementById('input-pairing').hidden);
 await page.locator('#input-help > summary').click();
 await page.locator('#scan-all').click();
 await page.getByRole('button',{name:'Phone simulator [ee:ff]',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='input'&&r.command==='connect 12'));
 await page.waitForFunction(()=>document.getElementById('pairing-code').textContent==='012345');
 await page.locator('#ble-dialog').screenshot({path:'artifacts/ble-compare.png'});
 await page.setViewportSize({width:320,height:800});
 assert(await page.locator('#ble-dialog').evaluate(el=>el.scrollWidth<=el.clientWidth));
 await page.locator('#ble-dialog').screenshot({path:'artifacts/ble-compare-mobile.png'});
 await page.setViewportSize({width:1280,height:900});
 await page.locator('#input-confirm').click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='input-confirm'&&r.attempt===8&&r.code===12345));
 await page.waitForFunction(()=>document.getElementById('input-pairing').hidden);
 assert.equal(await page.locator('#reconnect').isDisabled(),true);
 assert.equal(await page.locator('#scan').isDisabled(),true);
 assert.equal(await page.locator('#input-disconnect').isEnabled(),true);
 assert.equal(await page.locator('#forget-input').isEnabled(),true);
 assert.equal(await page.locator('#devices button').count(),0);
 assert.equal(await page.locator('#scan').isVisible(),false);
 assert.equal(await page.locator('#forget-input').isVisible(),true);
 await page.locator('#close-ble').click();
 assert.equal(await page.locator('#ble-dialog').isVisible(),false);
 await page.locator('#usb-devices').locator('..').screenshot({path:'artifacts/ble-connected.png'});
 await page.getByRole('button',{name:'Pair computer',exact:true}).first().click();
 await page.waitForFunction(()=>document.getElementById('computer-passkey').textContent.includes('654321'));
 const names=page.locator('#slots input');await names.nth(1).fill('Work');await page.locator('#slots .slot').nth(1).getByRole('button',{name:'Save name',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='slots'&&r.names[1]==='Work'));
 await page.evaluate(()=>window.setupState.slots.forEach(s=>s.paired=1));
 await page.locator('#seamless-toggle').click();await page.waitForFunction(()=>document.getElementById('seamless-toggle').getAttribute('aria-checked')==='true');
 assert.equal(await page.evaluate(()=>window.requests.some(r=>r.op==='calibration-start')),false);
 await page.locator('#pointer-speed').fill('125');await page.locator('#speed-save').click();await page.waitForFunction(()=>window.requests.some(r=>r.op==='pointer-speed'&&r.value===125));
 await page.locator('#calibrate').click();await page.waitForFunction(()=>document.getElementById('calibration').textContent.includes('TOP LEFT'));
 await page.locator('#calibration-cancel').click();
 await page.locator('#seamless-toggle').click();
 await page.waitForFunction(()=>document.getElementById('seamless-toggle').getAttribute('aria-checked')==='false');
 assert.equal(await page.locator('#seamless-options').isVisible(),false);
 assert.equal(await page.locator('#reset-shortcuts').isVisible(),true);
 assert.equal(await page.locator('#calibration-cancel').isVisible(),false);
 assert.equal(await page.getByRole('button',{name:'Forget pairing',exact:true}).count(),3);
 await page.getByRole('button',{name:'Edit cycle computers keyboard',exact:true}).click();
 assert.equal(await page.locator('dialog[open]').count(),0);
 await page.waitForFunction(()=>document.getElementById('bindings').textContent.includes('Listening'));
 await page.evaluate(()=>{window.captureReady=true;});
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='shortcut-save'));
 await page.waitForFunction(()=>document.getElementById('recording').textContent.startsWith('Saved:'));
 await page.evaluate(()=>{window.captureReady=false;});
 await page.getByRole('button',{name:'Edit cycle computers mouse',exact:true}).click();
 await page.getByRole('button',{name:'Cancel recording',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='shortcut-cancel'));
 assert(await page.evaluate(()=>window.requests.some(r=>r.op==='shortcut-record'&&r.kind===2)));
 await page.getByRole('button',{name:'Remove cycle computers keyboard',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='shortcut-clear'&&r.action===0&&r.kind===1));
 await page.evaluate(()=>{window.setupState.seamless=true;window.setupState.calibration_needed=0;});
 await page.waitForFunction(()=>document.getElementById('seamless-toggle').getAttribute('aria-checked')==='true');
 assert.equal(await page.locator('#calibrate').textContent(),'Calibrate sensitivity');
 assert.equal(await page.locator('#edge-save').isVisible(),false);
 await page.locator('#edge').fill('120');assert.equal(await page.locator('#edge-save').isVisible(),true);
 await page.locator('#edge').fill('100');assert.equal(await page.locator('#edge-save').isVisible(),false);
 // Calibration-fed arrangement, exact dimensions, shared-edge placement and save.
 const layout=page.locator('#monitor-arrangement');
 assert.equal(await layout.isVisible(),true);
 assert((await layout.locator('[data-slot="0"]').textContent()).includes('1920 × 960'));
 assert.equal(await layout.locator('details,[name="x"],[name="y"],[data-default]').count(),0);
 await layout.getByRole('button',{name:'Edit resolution for slot 1',exact:true}).click();
 await layout.screenshot({path:'artifacts/debug-speed.png'});
 await layout.locator('[name="width"]').fill('3440');await layout.locator('[name="height"]').fill('1440');
 // Polling must not replace the focused editor.
 await page.waitForTimeout(1200);assert.equal(await layout.locator('[name="width"]').inputValue(),'3440');
 await layout.getByRole('button',{name:'Save resolution',exact:true}).click();
 assert.equal(await layout.locator('[data-save]').isDisabled(),true); // overlap until arranged
 await layout.getByRole('button',{name:'Edit resolution for slot 2',exact:true}).click();
 await layout.locator('[name="width"]').fill('1920');await layout.locator('[name="height"]').fill('1080');
 await layout.getByRole('button',{name:'Save resolution',exact:true}).click();
 await layout.locator('[data-row]').click();
 await layout.getByRole('button',{name:'Edit speed for slot 1',exact:true}).click();
 await layout.locator('[aria-label="Speed for slot 1 percent"]').fill('125');
 await layout.getByRole('button',{name:'Save speed for slot 1',exact:true}).click();
 await layout.locator('[data-row]').click();
 await layout.locator('[data-slot="1"]').focus();
 for(let i=0;i<18;i++)await page.keyboard.press('ArrowDown');
 await layout.locator('[data-save]').click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='monitor-layout'&&r.active&&r.screens[1].y===180));
 await page.waitForFunction(()=>document.querySelector('#monitor-arrangement [data-save]').hidden);
 assert(!(await layout.locator('[data-slot="0"]').textContent()).includes('≈'));
 assert(await page.evaluate(()=>window.requests.some(r=>r.op==='monitor-layout'&&r.screens[0].sensitivity===125)));
 // Drag a screen away and discard: only Save applies a new arrangement.
 const screen=await layout.locator('[data-slot="1"]').boundingBox();
 await page.mouse.move(screen.x+screen.width/2,screen.y+screen.height/2);await page.mouse.down();await page.mouse.move(screen.x+screen.width/2,screen.y+screen.height/2+60,{steps:5});await page.mouse.up();
 assert.equal(await layout.locator('[data-discard]').isVisible(),true);
 await layout.locator('[data-discard]').click();
 assert(await page.evaluate(()=>window.setupState.monitor_layout.screens[1].y===180));
 await layout.getByRole('button',{name:'Edit resolution for slot 2',exact:true}).click();
 await layout.locator('[name="width"]').fill('999');await page.keyboard.press('Escape');
 assert((await layout.locator('[data-slot="1"]').textContent()).includes('1920 × 1080'));
 for(const width of [320,390,430,1440]){
   await page.setViewportSize({width,height:900});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`overflow at ${width}`);
   await layout.getByRole('button',{name:'Edit resolution for slot 2',exact:true}).click();
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
   assert(await layout.locator('.monitor-resolution-edit').evaluate(el=>el.getBoundingClientRect().width>0),'resolution editor visible');
   await layout.screenshot({path:`artifacts/monitor-editor-${width}.png`});
   await page.keyboard.press('Escape');
   await page.screenshot({path:`artifacts/web-setup-${width}.png`,fullPage:true});
 }
 // Presets: New preset copies the viewed preset into a new tab and views it.
 const bar=layout.locator('.preset-bar');
 assert.equal(await bar.isVisible(),true);
 assert.equal(await bar.getByRole('button',{name:'Default, active',exact:true}).getAttribute('aria-pressed'),'true');
 assert.equal(await layout.locator('[data-preset-delete]').isVisible(),false);
 assert.equal(await layout.locator('[data-preset-activate]').isVisible(),false);
 await layout.locator('[data-slot="0"]').focus();await page.keyboard.press('ArrowLeft');await page.keyboard.press('ArrowRight');
 assert.equal(await layout.locator('[name="enabled"]').isVisible(),false);
 assert.equal(await layout.locator('.monitor-selected').getByRole('button',{name:'Not on screen',exact:true}).isVisible(),true);
 await layout.locator('[data-discard]').click();
 await layout.getByRole('button',{name:'New preset',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='preset-save'&&r.index===1&&r.name==='Default 2'&&r.screens.length===3&&!('span_x' in r.screens[0])));
 await bar.getByRole('button',{name:'Default 2',exact:true}).waitFor();
 assert.equal(await bar.getByRole('button',{name:'Default 2',exact:true}).getAttribute('aria-pressed'),'true');
 assert.equal(await layout.locator('[data-preset-activate]').isVisible(),true);
 // Shortcut rows: Next preset plus one row per saved preset, by name.
 await page.waitForFunction(()=>document.getElementById('bindings').textContent.includes('Default 2'));
 assert.equal(await page.getByRole('button',{name:'Edit next preset keyboard',exact:true}).count(),1);
 assert.equal(await page.getByRole('button',{name:'Edit preset Default 2 mouse',exact:true}).count(),1);
 await page.getByRole('button',{name:'Edit preset Default 2 keyboard',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='shortcut-record'&&r.action===6&&r.kind===1));
 await page.getByRole('button',{name:'Cancel recording',exact:true}).click();
 await page.waitForFunction(()=>window.requests.filter(r=>r.op==='shortcut-cancel').length>=2);
 // Not on screen moves a screen to the tray; Save writes the viewed preset only.
 await layout.getByRole('button',{name:'Mark slot 2 not on screen',exact:true}).click();
 assert.equal(await layout.locator('.monitor-stage [data-slot="1"]').count(),0);
 assert.equal(await layout.locator('.monitor-excluded').getByRole('button',{name:'Show slot 2 on screen',exact:true}).isVisible(),true);
 assert.equal(await bar.getByRole('button',{name:'Default, active',exact:true}).isDisabled(),true);
 await layout.locator('[data-save]').click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='preset-save'&&r.index===1&&r.name==='Default 2'&&r.screens[1].enabled===false));
 await page.waitForFunction(()=>document.querySelector('#monitor-arrangement [data-save]').hidden);
 assert(await page.evaluate(()=>window.setupState.presets.list[0].screens[1].enabled));
 assert.equal(await layout.locator('.monitor-excluded .monitor-chip').count(),1);
 // Show on screen previews the screen right of the rightmost one.
 await layout.getByRole('button',{name:'Show slot 2 on screen',exact:true}).click();
 assert.equal(await layout.locator('.monitor-stage [data-slot="1"]').count(),1);
 assert.equal(await layout.locator('.monitor-excluded').isVisible(),false);
 await layout.locator('[data-discard]').click();
 assert.equal(await layout.locator('.monitor-stage [data-slot="1"]').count(),0);
 // Activate, rename and delete.
 await layout.getByRole('button',{name:'Activate preset Default 2',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='preset-activate'&&r.index===1));
 await bar.getByRole('button',{name:'Default 2, active',exact:true}).waitFor();
 await layout.getByRole('button',{name:'Rename preset Default 2',exact:true}).click();
 await layout.getByLabel('Preset name').fill('default');
 await layout.getByRole('button',{name:'Save name',exact:true}).click();
 assert((await layout.locator('[data-status]').textContent()).includes('already uses'));
 await layout.getByLabel('Preset name').fill('Home PC');
 await layout.getByRole('button',{name:'Save name',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='preset-save'&&r.index===1&&r.name==='Home PC'));
 await page.waitForFunction(()=>document.getElementById('bindings').textContent.includes('Home PC'));
 // Rename blocks stage edits so unsaved screen edits cannot be dropped.
 await layout.getByRole('button',{name:'Rename preset Home PC',exact:true}).click();
 await layout.locator('[data-slot="0"]').focus();await page.keyboard.press('ArrowRight');
 assert.equal(await layout.locator('[data-save]').isHidden(),true);
 assert.equal(await layout.getByRole('button',{name:'Mark slot 1 not on screen',exact:true}).isDisabled(),true);
 await layout.getByRole('button',{name:'Cancel',exact:true}).click();
 // A preset activation elsewhere keeps an unsaved draft of an unchanged preset saveable.
 await bar.getByRole('button',{name:'Default',exact:true}).click();
 await layout.locator('[data-slot="0"]').focus();await page.keyboard.press('ArrowDown');
 assert.equal(await layout.locator('[data-save]').isVisible(),true);
 await page.evaluate(()=>{const p=window.setupState.presets;p.active=0;++p.generation;const a=p.list.find(e=>e.index===0);window.setupState.monitor_layout={active:p.arrangement,generation:p.generation,screens:a.screens.map((s,i)=>({...s,...window.setupState.monitor_layout.screens[i]}))};});
 await bar.getByRole('button',{name:'Default, active',exact:true}).waitFor();
 assert.equal(await layout.locator('[data-save]').isVisible(),true);
 const priorSaves=await page.evaluate(()=>window.requests.filter(r=>r.op==='preset-save').length);
 await layout.locator('[data-save]').click();
 await page.waitForFunction(n=>window.requests.filter(r=>r.op==='preset-save').length>n,priorSaves);
 await page.waitForFunction(()=>document.querySelector('#monitor-arrangement [data-save]').hidden);
 assert.equal((await layout.locator('[data-status]').textContent()).includes('Reload'),false);
 // Stale draft: the preset changes elsewhere, then an activation; Save must still ask for a reload.
 await layout.locator('[data-slot="0"]').focus();await page.keyboard.press('ArrowDown');
 await page.evaluate(()=>{const p=window.setupState.presets;p.list.find(e=>e.index===0).screens[0].y+=500;++p.generation;});
 const polled=await page.evaluate(()=>window.requests.filter(r=>r.op==='status').length);
 await page.waitForFunction(n=>window.requests.filter(r=>r.op==='status').length>n+1,polled);
 await page.evaluate(()=>{const p=window.setupState.presets;p.active=1;++p.generation;});
 await bar.getByRole('button',{name:'Home PC, active',exact:true}).waitFor();
 await layout.locator('[data-save]').click();
 await page.waitForFunction(()=>document.querySelector('#monitor-arrangement [data-status]').textContent.includes('Reload'));
 await layout.locator('[data-discard]').click();
 await page.evaluate(()=>{const p=window.setupState.presets;p.active=0;++p.generation;});
 await bar.getByRole('button',{name:'Default, active',exact:true}).waitFor();
 for(const width of [320,390]){
   await page.setViewportSize({width,height:900});
   assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),`preset overflow at ${width}`);
   await layout.screenshot({path:`artifacts/monitor-presets-${width}.png`});
 }
 await page.setViewportSize({width:1280,height:900});
 await bar.getByRole('button',{name:'Default, active',exact:true}).click();
 page.once('dialog',dialog=>dialog.accept());
 await layout.getByRole('button',{name:'Delete preset Default',exact:true}).click();
 await page.waitForFunction(()=>window.requests.some(r=>r.op==='preset-delete'&&r.index===0));
 await page.waitForFunction(()=>document.querySelectorAll('#monitor-arrangement .preset-tab').length===1);
 assert.equal(await page.evaluate(()=>document.activeElement?.dataset?.preset),'1');
 assert.equal(await layout.locator('[data-preset-delete]').isVisible(),false);
 await page.waitForFunction(()=>!document.getElementById('bindings').textContent.includes('Default'));
 // Older firmware without presets keeps the single editor and monitor-layout.
 await page.evaluate(()=>{const s=window.setupState;delete s.presets;s.shortcuts.bindings=s.shortcuts.bindings.filter(b=>b[0]<4);});
 await page.waitForFunction(()=>document.querySelector('#monitor-arrangement .preset-bar').hidden);
 assert.equal(await layout.locator('.monitor-stage [data-slot]').count(),3);
 assert.equal(await layout.locator('.monitor-hide').count(),0);
 assert.equal(await page.getByRole('button',{name:'Edit next preset keyboard',exact:true}).count(),0);
 await layout.locator('[data-slot="2"]').focus();await page.keyboard.press('ArrowDown');
 assert.equal(await layout.locator('[name="enabled"]').isVisible(),true);
 assert.equal(await layout.locator('.monitor-selected').isVisible(),false);
 assert.equal(await layout.locator('[data-save]').textContent(),'Save arrangement');
 const before=await page.evaluate(()=>window.requests.length);
 await layout.locator('[data-save]').click();
 await page.waitForFunction(n=>window.requests.slice(n).some(r=>r.op==='monitor-layout'&&r.active),before);
 assert.equal(await page.evaluate(n=>window.requests.slice(n).some(r=>r.op.startsWith('preset-')),before),false);
 await page.locator('#seamless-toggle').click();
 await page.waitForFunction(()=>document.getElementById('seamless-options').hidden);
 // Exercise the real install button handoff, substituting only the flash dialog
 // lifecycle so this browser test never talks to hardware or installs firmware.
 assert.equal(await page.locator('#open-firmware').isVisible(),true);
 await page.locator('#open-firmware').click();
 assert.equal(await page.getByRole('button',{name:'Install selected version',exact:true}).isVisible(),true);
 const beforeInstall=await page.evaluate(()=>({opens:window.portOpens,closes:window.portCloses||0}));
 page.once('dialog',dialog=>dialog.dismiss());
 await page.getByRole('button',{name:'Install selected version',exact:true}).click();
 assert.equal(await page.locator('#settings').evaluate(el=>el.disabled),false);
 assert.deepEqual(await page.evaluate(()=>({opens:window.portOpens,closes:window.portCloses||0})),beforeInstall);
 await page.evaluate(async()=>{
   await import('https://unpkg.com/esp-web-tools@10.1.0/dist/web/install-dialog-CQzvAv3p.js?module');
   const create=document.createElement.bind(document);
   document.createElement=(name,...args)=>{if(name!=='ewt-install-dialog')return create(name,...args);const dialog=create('div');dialog.id='test-install-dialog';return dialog;};
   new MutationObserver(()=>{const dialog=document.getElementById('test-install-dialog');if(dialog)window.testInstallManifest=dialog.manifestPath;}).observe(document.body,{childList:true});
 });
 const chosenManifest=await page.locator('#firmware-version').inputValue();
 page.once('dialog',dialog=>dialog.accept());
 await page.getByRole('button',{name:'Install selected version',exact:true}).click();
 await page.waitForFunction(()=>window.testInstallManifest);
 assert.equal(await page.evaluate(()=>window.testInstallManifest),chosenManifest);
 assert.equal(await page.evaluate(()=>window.portOpens),beforeInstall.opens+1);
 assert.equal(await page.evaluate(()=>window.portCloses),beforeInstall.closes+1);
 assert.equal(await page.locator('#settings').evaluate(el=>el.disabled),true);
 assert.equal(await page.locator('#connect').isDisabled(),true);
 await page.evaluate(()=>{const dialog=document.getElementById('test-install-dialog');dialog.dispatchEvent(new Event('closed'));dialog.remove();});
 await page.waitForFunction(()=>!document.getElementById('connect').disabled);
 assert.equal(await page.locator('#connect').isEnabled(),true);
 await page.evaluate(()=>{window.noReply=true;});
 await page.locator('#connect').click();
 await page.waitForFunction(()=>document.getElementById('connection').textContent.includes('use Install firmware'),{},{timeout:25000});
 assert.equal(await page.locator('#connect').isEnabled(),true);
 assert.equal(await page.locator('#disconnect').isEnabled(),false);
 assert.equal(await page.locator('#settings').evaluate(el=>el.disabled),true);
 assert.equal(await page.locator('#scan').isDisabled(),true);
 assert.deepEqual(errors,[]);
 await browser.close();console.log('Browser setup flow and responsive layouts PASS (simulated USB, no hardware flash)');
})().catch(e=>{console.error(e);process.exit(1)});
