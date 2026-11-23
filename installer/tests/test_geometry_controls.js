// Production geometry transforms and gating; no browser or robot commands.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const text=fs.readFileSync(path.join(__dirname,'../../src/web/app.js'),'utf8');
const ctx=vm.createContext({});
vm.runInContext('var app={};'+text.slice(text.indexOf('function mapDisplay('),text.indexOf('async function loadDetailedMap('))+text.slice(text.indexOf('function zoneEditingReady('),text.indexOf('function zoneSignature(')),ctx);
const map={available:true,map_id:'11',width:205,height:249,map_origin:[144,59],zones_available:true,zones_read_only:false};
const detail={available:true,map_id:'11',width:320,height:320,grid_origin:[240,80],preview_step:1,resolution_m:0.05};
const status={app_server_profile:'robojet-issue08',working_status:'Hibernating',capabilities:{map_edit:true},cache_available:true,local_link_active:true,error:'NoError'};
ctx.app={map,detailMap:detail,status,mapView:'detailed',connected:true};
assert(ctx.editableMapFrame());assert(ctx.zoneEditingReady());assert(ctx.geometryIdle(status,true));
assert(!ctx.geometryIdle({...status,app_server_profile:'zigma'},true));
assert(!ctx.geometryIdle({...status,working_status:'AutoClean'},true));
// Click on detailed (120,100) -> native (-120,20) -> live (24,79).
vm.runInContext('function geometry(){return {scale:2,left:10,top:20,rect:{left:5,top:7}}}',ctx);
let p=ctx.toEditGrid({clientX:255,clientY:227});assert.equal(p.x,24);assert.equal(p.y,79);
ctx.app.detailMap={...detail,preview_step:2};p=ctx.toEditGrid({clientX:135,clientY:127});assert.equal(p.x,24);assert.equal(p.y,79);
ctx.app.detailMap={...detail,map_id:'12'};assert(!ctx.editableMapFrame());assert(!ctx.zoneEditingReady());
ctx.app.detailMap=detail;ctx.app.status={...status,working_status:'Relocation'};assert(!ctx.editableMapFrame());
ctx.app.status={...status,map_reset_pending:true};assert(!ctx.editableMapFrame());
ctx.app.status=status;ctx.app.mapView='laser';assert(!ctx.editableMapFrame());
ctx.app.mapView='geometry';assert(!ctx.editableMapFrame());
ctx.app.mapView='detailed';ctx.app.map={...map,zones_read_only:true};assert(!ctx.zoneEditingReady());
console.log('Detailed/live coordinate transforms, frame mismatch, reset, relocation, hibernation and read-only guards: PASS');
