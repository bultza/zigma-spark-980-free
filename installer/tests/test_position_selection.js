// Exercise production selectors without a browser or robot.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const text=fs.readFileSync(path.join(__dirname,'../../src/web/app.js'),'utf8');
const ctx=vm.createContext({});vm.runInContext(text.slice(text.indexOf('function navigationPose('),text.indexOf('function drawMap()')),ctx);
const m={available:true,map_id:'a',width:100,height:100,map_origin:[40,50],path_endpoint:{x:60,y:80,kind:'recorded_path_endpoint'}};
const pick=(map=m,status={})=>ctx.selectedRobotPosition(map,status,1000);
assert.equal(pick().quality,'recorded');assert.equal(pick().heading,null);
assert.match(ctx.robotPoseText(pick()),/Heading unavailable.*recorded/);
m.position_estimate={x:65,y:80,kind:'recent_path_estimate',received_at:999};assert.equal(pick().quality,'estimated');
m.position_estimate.received_at=1001;assert.equal(pick().quality,'recorded');
const nav={map_id:'a',width:100,height:100,map_origin:[40,50],x:60,y:80,heading:0.5,resolution_m:0.05,frame:'map_grid',kind:'navigation_pose_cache',cache_read_at:999};
assert.equal(pick(m,{navigation_pose:nav}).source,'navigation_memory');
assert.equal(pick(m,{navigation_pose:{...nav,map_id:'b'}}).quality,'recorded');
assert.equal(pick(m,{navigation_pose:{...nav,cache_read_at:980}}).quality,'recorded');
assert.equal(pick(m,{map_reset_pending:true}),null);assert.equal(pick(m,{working_status:'Relocation'}),null);
assert.equal(pick({...m,path_endpoint:{...m.path_endpoint,x:100}}),null);
assert.equal(pick({...m,map_origin:[]}),null);assert.equal(pick({...m,available:false}),null);
assert.equal(pick({...m,position:{x:20,y:30}}).quality,'reported');
console.log('Position selection, source priority, bounds, reset, stale/future samples and missing heading: PASS');

vm.runInContext("function toScreen(p,g){return p;}"+text.slice(text.indexOf('function drawZone('),text.indexOf('function drawRect(')),ctx);
const strokes=[],paint={beginPath(){},moveTo(x,y){strokes.push([x,y]);},lineTo(x,y){strokes.push([x,y]);},stroke(){strokes.push('stroke');}};
ctx.drawZone(paint,{kind:'virtual_wall',points:[{x:1,y:2},{x:3,y:4}]},{});
assert.deepEqual(strokes,[[1,2],[3,4],'stroke']);
console.log('Read-only virtual wall renders as a line: PASS');

vm.runInContext("var app={map:{zones_read_only:true},status:{}};"+text.slice(text.indexOf('function zoneEditingReady('),text.indexOf('function zoneSignature(')),ctx);
assert.equal(ctx.zoneEditingReady(),false);
console.log('Read-only geometry disables editing before any command: PASS');
