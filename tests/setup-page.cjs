const { chromium } = require('@playwright/test');
const fs = require('fs');
const assert = require('assert');
(async()=>{
 const browser=await chromium.launch({headless:true});
 const page=await browser.newPage({viewport:{width:900,height:850}});
 let state={device_name:"HID SWITCHER BLE",selected:0,generation:1,slots:[{name:'Main Mac',assigned:true,connected:true},{name:'MacBook',assigned:true,connected:true},{name:'Computer 3',assigned:false,connected:false}]};
 state.usb={healthy:true,interfaces:[{address:2,vid:0x3297,pid:0x1969,kind:'Keyboard',active:true},{address:3,vid:0x046d,pid:0xc548,kind:'Mouse',active:true}],recoveries:1,transfer_errors:1,open_errors:2};
 state.bluetooth={traffic:{window_ms:5000,mouse_in:625,mouse_tx:625,mouse_cap_hz:125},links:[{slot:0,encrypted:true,keyboard:true,mouse:true,interval_ms:15}],recoveries:0,send_errors:0};
 state.bluetooth.mouse_timing={arrival_spacing:{samples:100,mean_ms:8,max_ms:12,stddev_ms:1,p95_upper_ms:12},oldest_to_submission:{samples:50,mean_ms:9,max_ms:18,stddev_ms:3,p95_upper_ms:24}};
 state.firmware={build:'12345678',slot:'app0',trial:false,rollback_available:true,uptime_seconds:120,max_bytes:3145728};
 let shortcutConfig={generation:0,bindings:[[0,1,9,[43],0],[0,2,0,[],0],[1,1,0,[],0],[1,2,0,[],0],[2,1,0,[],0],[2,2,0,[],0],[3,1,9,[],0]]};
 let recordingAction=0,shortcutSaves=0,shortcutCancels=0;
 let uploads=0,rollbacks=0;
 let saved=0,selected=0,wifi=0;const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.route('http://setup.test/**',async route=>{
  const req=route.request(),path=new URL(req.url()).pathname;
  if(path==='/')return route.fulfill({contentType:'text/html',body:fs.readFileSync('web/index.html','utf8').replace('__SETUP_TOKEN__','test-token')});
  if(path==='/api/shortcuts'&&req.method()==='GET')return route.fulfill({contentType:'application/json',body:JSON.stringify(shortcutConfig)});
  if(req.method()==='POST'){
   assert.equal(req.headers()['x-setup-token'],'test-token');
   if(path==='/api/firmware'){uploads++;state.firmware.restarting=true;return route.fulfill({status:202,contentType:'application/json',body:'{"restarting":true}'});}
   if(path==='/api/firmware/rollback'){rollbacks++;state.firmware.restarting=true;return route.fulfill({status:202,contentType:'application/json',body:'{"restarting":true}'});}
   const body=req.postDataJSON();
   if(path==='/api/shortcuts'){
    let response;
    if(body.op==='shortcut-record'){recordingAction=body.action;response={token:'test-capture-token',state:'waiting'};}
    else if(body.op==='shortcut-status'){assert.equal(body.token,'test-capture-token');response={state:'ready',label:'Ctrl + Cmd + Button 4'};}
    else if(body.op==='shortcut-save'){assert.equal(body.token,'test-capture-token');shortcutSaves++;shortcutConfig.bindings[2*recordingAction+1]=[recordingAction,2,9,[],8];response=shortcutConfig;}
    else if(body.op==='shortcut-clear'){shortcutConfig.bindings[2*body.action+(body.kind===2)]=[body.action,body.kind,0,[],0];response=shortcutConfig;}
    else if(body.op==='shortcut-cancel'){shortcutCancels++;response={state:'cancelled'};}
    else if(body.op==='shortcuts-reset'){shortcutConfig.bindings[0]=[0,1,9,[43],0];shortcutConfig.bindings[1]=[0,2,0,[],0];response=shortcutConfig;}
    else throw Error('Unknown shortcut operation');
    return route.fulfill({contentType:'application/json',body:JSON.stringify(response)});
   }
   if(path==='/api/device'){state.device_name=body.name;}else if(path==='/api/config'){
    assert.equal(body.generation,state.generation);assert.equal(new Set(body.order).size,3);
    const previous=state.slots;state.slots=body.order.map((o,i)=>({...previous[o],name:body.names[i]}));state.selected=body.order.indexOf(state.selected);state.generation++;saved++;
   }else if(path==='/api/select'){state.selected=body.slot;state.generation++;selected++;}else if(path==='/api/wifi'){assert.equal(body.ssid,'Home WiFi');assert.equal(body.password,'testpass123');wifi++;state.network={connected:true,connecting:false,ssid:body.ssid,ip:'192.168.1.50',portal:true,error:''};}
  }
  return route.fulfill({contentType:'application/json',body:JSON.stringify(state)});
 });
 await page.goto('http://setup.test/');await page.getByText('Ready',{exact:true}).waitFor();
 assert.equal(await page.locator('.card').count(),3);
 assert.equal(await page.locator('#shortcut-list .shortcut').count(),4);
 assert(!(await page.locator('#shortcut-list .shortcut').nth(3).textContent()).includes('Mouse:'));
 assert((await page.locator('#shortcut-list .shortcut').nth(3).textContent()).includes('1/2/3'));
 await page.getByRole('button',{name:'Record cycle',exact:true}).click();
 await page.getByText('Captured: Ctrl + Cmd + Button 4. Save to use this shortcut.',{exact:true}).waitFor();
 assert.equal(shortcutSaves,0);
 await page.locator('#shortcut-save').click();await page.getByText('Shortcut saved.',{exact:true}).waitFor();
 assert.equal(shortcutSaves,1);assert.deepEqual(shortcutConfig.bindings[0],[0,1,9,[43],0]);assert((await page.locator('#shortcut-list').textContent()).includes('Ctrl + Cmd + Button 4'));
 await page.getByRole('button',{name:'Record next',exact:true}).click();
 await page.locator('#shortcut-cancel').click();await page.getByText('Recording cancelled; shortcut unchanged.',{exact:true}).waitFor();
 assert.equal(shortcutCancels,1);assert.equal(shortcutSaves,1);
 await page.getByRole('button',{name:'Clear keyboard for cycle',exact:true}).click();await page.getByText('Binding cleared.',{exact:true}).waitFor();
 assert.deepEqual(shortcutConfig.bindings[0],[0,1,0,[],0]);assert.deepEqual(shortcutConfig.bindings[1],[0,2,9,[],8]);
 assert.equal(await page.getByRole('button',{name:'Clear mouse for slot',exact:true}).count(),0);
 assert.equal(await page.locator('#device-name').inputValue(),'HID SWITCHER BLE');
 await page.locator('#device-name').fill('Desk <script>');
 await page.locator('#device-save').click();
 await page.getByText('Bluetooth name saved. Computers may cache the old name.',{exact:true}).waitFor();
 assert.equal(state.device_name,'Desk <script>');
 assert.equal(await page.locator('#pairing-name').textContent(),'Desk <script>');
 await page.locator('#device-name').fill('🎹'.repeat(8));
 await page.locator('#device-save').click();await page.locator('#device-message.error').waitFor();
 assert.equal(state.device_name,'Desk <script>');
 await page.locator('#name0').fill('Desk <script>');
 await page.locator('.card').nth(0).locator('select').selectOption('2');
 await page.getByRole('button',{name:'Save changes'}).click();await page.getByText('Changes saved',{exact:true}).waitFor();
 assert.equal(saved,1);assert.equal(state.selected,2);assert.equal(state.slots[2].name,'Desk <script>');
 await page.locator('.card').nth(1).getByRole('button',{name:'Switch here'}).click();
 await page.getByText('Computer 2 selected',{exact:true}).waitFor();assert.equal(selected,1);assert.equal(state.selected,1);
 fs.mkdirSync('artifacts/hub-release',{recursive:true});
 await page.screenshot({path:'artifacts/hub-release/setup-desktop.png',fullPage:true});
 await page.setViewportSize({width:375,height:850});
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 await page.screenshot({path:'artifacts/hub-release/setup-mobile.png',fullPage:true});
 await page.locator('#name0').fill('');await page.getByRole('button',{name:'Save changes'}).click();
 await page.locator('#message.error').waitFor();assert.equal(saved,1);
 await page.locator('#wifi-ssid').fill('Home WiFi');await page.locator('#wifi-password').fill('short');
 await page.getByRole('button',{name:'Connect to Wi-Fi'}).click();await page.locator('#wifi-message.error').waitFor();assert.equal(wifi,0);
 await page.locator('#wifi-password').fill('testpass123');await page.getByRole('button',{name:'Connect to Wi-Fi'}).click();
 await page.getByText('Connected to Home WiFi',{exact:true}).waitFor();assert.equal(wifi,1);assert.equal(await page.locator('#wifi-password').inputValue(),'');
 assert.equal(await page.locator('#wifi-ip a').getAttribute('href'),'http://192.168.1.50');assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 assert.equal(await page.locator('#health-usb').textContent(),'2');
 assert((await page.locator('#usb-devices').textContent()).includes('3297:1969'));
 assert((await page.locator('#ble-links').textContent()).includes('15 ms'));
 assert((await page.locator('#traffic-status').textContent()).includes('125 Bluetooth submissions/s'));
 assert((await page.locator('#mouse-timing').textContent()).includes('USB mouse report spacing: 8.00 ms average'));
 assert((await page.locator('#mouse-timing').textContent()).includes('Oldest combined movement wait: 9.00 ms average'));
 assert((await page.locator('#mouse-timing').textContent()).includes('Bluetooth submission spacing: waiting for movement'));
 assert((await page.locator('#mouse-settings-status').textContent()).includes('support has not been established'));
 await page.locator('#firmware-file').setInputFiles({name:'bad.bin',mimeType:'application/octet-stream',buffer:Buffer.alloc(5)});
 await page.locator('#firmware-install').click();await page.locator('#firmware-message.error').waitFor();assert.equal(uploads,0);
 await page.locator('#firmware-file').setInputFiles({name:'firmware.bin',mimeType:'application/octet-stream',buffer:Buffer.alloc(1024)});
 await page.locator('#firmware-install').click();await page.getByText('Upload verified. Restarting with Wi-Fi off. Use the BLE CLI to enable Wi-Fi before reopening this page.',{exact:true}).waitFor();assert.equal(uploads,1);
 page.on('dialog',dialog=>dialog.accept());await page.locator('#firmware-rollback').click();
 await page.getByText('Restoring previous firmware and restarting…',{exact:true}).waitFor();assert.equal(rollbacks,1);
 assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 assert.deepEqual(errors,[]);await browser.close();console.log('Setup page: names, slot moves, selection, validation, 375px layout PASS');
})().catch(e=>{console.error(e);process.exit(1)});
