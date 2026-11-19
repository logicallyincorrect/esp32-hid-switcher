import { test } from 'node:test';
import assert from 'node:assert/strict';
import { validLayout,seedArrangement,presetCopyName,presetPayload,placeOnScreen,validPresetName } from '../web/monitor-layout.mjs';
const layout={active:false,generation:0,screens:[{x:0,y:0,width:1920,height:1080,enabled:true,estimated:true,span_x:3000,span_y:1500},{x:1920,y:0,width:1920,height:1080,enabled:true,estimated:true,span_x:1500,span_y:1000},{x:3840,y:0,width:1920,height:1080,enabled:true,estimated:true,span_x:0,span_y:0}]};
test('calibration seeds labelled estimates in slot order without mutating saved data',()=>{
 const draft=seedArrangement(layout,[{paired:true},{paired:true},{}]);
 assert.deepEqual(draft.screens.map(s=>[s.width,s.height,s.x,s.enabled]),[[1920,960,0,true],[960,640,1920,true],[1920,1080,2880,false]]);
 assert(draft.screens.every(s=>s.estimated));assert(validLayout(draft.screens));assert.equal(layout.screens[0].height,1080);
});
test('saved custom dimensions and positions survive recalibration',()=>{
 const saved=structuredClone(layout);saved.active=true;saved.generation=1;saved.screens[0].height=1440;saved.screens[0].estimated=false;
 assert.deepEqual(seedArrangement(saved,[]),{...saved,screens:saved.screens.map(s=>({...s,sensitivity:100}))});
});
test('reject overlapping, empty, noninteger and out-of-range rectangles',()=>{
 const screens=structuredClone(layout.screens);assert(validLayout(screens));screens[1].x=1900;assert(!validLayout(screens));screens[1].x=1920;
 screens[0].width=NaN;assert(!validLayout(screens));screens[0].width=1920;screens[0].x=0.5;assert(!validLayout(screens));screens[0].x=65537;assert(!validLayout(screens));
 screens.forEach(s=>s.enabled=false);assert(!validLayout(screens));
});

test('screen speed defaults and validation',()=>{
 const draft=seedArrangement(layout,[{}, {}, {}]);assert(draft.screens.every(s=>s.sensitivity===100));
 for(const value of [25,100,150,400]){draft.screens[0].sensitivity=value;assert(validLayout(draft.screens));}
 for(const value of [0,24,401,NaN,100.5]){draft.screens[0].sensitivity=value;assert(!validLayout(draft.screens));}
});

test('preset copy names are unique case-insensitively and fit 24 characters',()=>{
 assert.equal(presetCopyName(['Default'], 'Default'), 'Default 2');
 assert.equal(presetCopyName(['Default', 'default 2'], 'Default'), 'Default 3');
 assert.equal(presetCopyName([], 'Split'), 'Split');
 const long='A'.repeat(24);
 assert.equal(presetCopyName([long],long),'A'.repeat(22)+' 2');
 assert(presetCopyName([long,'A'.repeat(22)+' 2'],long).length<=24);
});
test('preset payload carries only the protocol fields and keeps screens off',()=>{
 const payload = presetPayload({index: 1, name: 'Home PC', screens: [{x:0,y:0,width:1920,height:1080,enabled:false,estimated:false,sensitivity:100}]}, 7);
 assert.deepEqual(Object.keys(payload).sort(), ['generation','index','name','screens']);
 assert.equal(payload.screens[0].enabled, false);
 assert.equal(payload.generation,7);
 const extra=presetPayload({index:0,name:'Default',screens:[{x:1,y:2,width:1920,height:1080,enabled:true,estimated:true,sensitivity:125,span_x:3000,span_y:1500}]},2);
 assert.deepEqual(extra.screens[0],{x:1,y:2,width:1920,height:1080,enabled:true,estimated:true,sensitivity:125});
});
test('showing a screen places it right of the rightmost screen when it would overlap',()=>{
 const screens=[{x:0,y:100,width:1920,height:1080,enabled:true},{x:1920,y:0,width:1920,height:1080,enabled:true},{x:0,y:0,width:1280,height:800,enabled:false,estimated:false}];
 placeOnScreen(screens,2);
 assert.deepEqual([screens[2].x,screens[2].y,screens[2].enabled,screens[2].estimated],[3840,0,true,true]);
 assert(validLayout(screens));
 const free=[{x:0,y:0,width:1920,height:1080,enabled:true},{x:5000,y:0,width:100,height:100,enabled:false,estimated:false},{x:0,y:0,width:64,height:64,enabled:false}];
 placeOnScreen(free,1);assert.deepEqual([free[1].x,free[1].enabled,free[1].estimated],[5000,true,false]);
});
test('preset names follow the firmware rules',()=>{
 for(const name of ['Default','Home PC','x'.repeat(24)])assert(validPresetName(name));
 for(const name of ['','   ','x'.repeat(25),'Caf\u00e9','Tab\there'])assert(!validPresetName(name));
});
