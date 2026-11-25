const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const repo=path.resolve(__dirname,'../..');
const source=fs.readFileSync(path.join(repo,'src/web/app.js'),'utf8');
const fn=source.slice(source.indexOf('async function loadSoftware(){'),source.indexOf('// Authenticated download:',source.indexOf('async function loadSoftware(){')));
(async()=>{
 for(const value of ['v1.1-rc20','manually edited <img src=x onerror=alert(1)>',null]){
  const elements={};const $=id=>elements[id]??=(id==='software-list'?{replaceChildren(){}}:{textContent:''});
  const ctx={$,api:async()=>({installer_version:value,items:[],available:true}),robotTimezone:'UTC'};
  vm.createContext(ctx);vm.runInContext(fn,ctx);await ctx.loadSoftware();
  assert.equal($('installer-version').textContent,value||'Not recorded');
  assert.equal($('software-status').textContent,'Installed files checked against the release manifest. Dates use UTC.');
 }
 const html=fs.readFileSync(path.join(repo,'src/web/index.html'),'utf8');
 const about=html.slice(html.indexOf('id="page-about"'));
 assert(about.indexOf('id="installer-version"')<about.indexOf('Made by bultza'));
 console.log('PASS: About shows editable installer label as text first, with independent component verification and missing-label fallback');
})().catch(e=>{console.error(e);process.exit(1)});
