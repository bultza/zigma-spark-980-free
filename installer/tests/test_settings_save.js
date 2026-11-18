const fs=require('fs'),vm=require('vm'),assert=require('assert');
const path=require('path');
const repo=path.resolve(__dirname,'../..');
const source=fs.readFileSync(path.join(repo,'src/web/app.js'),'utf8');
const fn=source.slice(source.indexOf('async function saveSettingsGroup('),source.indexOf("$('general-form').addEventListener"));
async function test(values,expected){
 const calls=[],result={},control={disabled:false};
 const settings={robot_name:'Ciccia_Poland_Test69',timezone:'Asia/Shanghai',ntp_server:'time.google.com',area_unit:'m2',over_ridge:false,continue_clean:true};
 const ctx={app:{settings},$:id=>id==='form'?{querySelectorAll:()=>[control]}:result,refresh:async()=>{},api:async(url,opt)=>{const v=JSON.parse(opt.body);calls.push(v);assert.notEqual(v.action,'name','Unchanged installer name must not be sent');return {...settings,[v.action]:v.value}},setTimeout};
 vm.createContext(ctx);vm.runInContext(fn,ctx);await ctx.saveSettingsGroup('form','result',values);assert.deepEqual(calls,expected);assert.equal(control.disabled,false);assert.equal(result.textContent,'Saved.');
}
(async()=>{
 await test([['name','Ciccia_Poland_Test69'],['timezone','Europe/Warsaw'],['ntp_server','time.google.com'],['area_unit','m2'],['over_ridge','false'],['continue_clean','true']],[{action:'timezone',value:'Europe/Warsaw'}]);
 await test([['name','Ciccia_Poland_Test69'],['timezone','Asia/Shanghai']],[]);
 const html=fs.readFileSync(path.join(repo,'src/web/index.html'),'utf8');assert(!html.match(/<input[^>]*id="settings-name"[^>]*pattern=/));
 console.log('PASS: timezone-only save preserves installer name and options; unchanged form sends nothing; existing name passes HTML validation');
})().catch(e=>{console.error(e);process.exit(1)});
