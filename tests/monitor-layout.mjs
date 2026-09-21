import { test } from 'node:test';
import assert from 'node:assert/strict';
import { validLayout,seedArrangement } from '../web/monitor-layout.mjs';
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
