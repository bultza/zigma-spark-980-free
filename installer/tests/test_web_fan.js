const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../../src/web/app.js'),'utf8');
let handler;const calls=[];
const select={value:'2',addEventListener:(event,cb)=>{assert.equal(event,'change');handler=cb;}};
vm.runInNewContext(source.split('\n').find(line=>line.startsWith("$('clean-fan-power').addEventListener")),{$:()=>select,sendCommand:(...args)=>calls.push(args)});
for(const level of [1,2,3]){select.value=String(level);handler();}
assert.deepEqual(calls.map(x=>[x[0],x[1].fan_power]),[['setfan',1],['setfan',2],['setfan',3]]);
console.log('web fan selector PASS: only setfan sent');
