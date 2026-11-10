'use strict';
const $ = id => document.getElementById(id);
const icons = {
 wifi:'<path d="M2 8a16 16 0 0 1 20 0M5 12a11 11 0 0 1 14 0M8 16a6 6 0 0 1 8 0"/><circle cx="12" cy="20" r=".5"/>',
 settings:'<path d="M12 3v3m0 12v3M3 12h3m12 0h3M5.6 5.6l2.1 2.1m8.6 8.6 2.1 2.1m0-12.8-2.1 2.1m-8.6 8.6-2.1 2.1"/><circle cx="12" cy="12" r="5"/>',
 robot:'<circle cx="12" cy="13" r="9"/><circle cx="12" cy="9" r="3"/><path d="M8 17h8"/>',
 home:'<path d="m3 10 9-7 9 7v10H3Z"/><path d="M9 20v-7h6v7"/>',
 calendar:'<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M7 3v4m10-4v4M3 11h18m-14 4h2m4 0h2m-8 3h2"/>',
 activity:'<path d="M3 12h4l3-8 4 16 3-8h4"/>', battery:'<rect x="2" y="7" width="17" height="10" rx="2"/><path d="M22 10v4M6 10v4m4-4v4"/>',
 area:'<path d="M9 3H3v6m12-6h6v6M3 15v6h6m12-6v6h-6M8 8h8v8H8Z"/>',
 clock:'<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
 key:'<circle cx="8" cy="9" r="5"/><path d="m12 13 8 8m-4-4 3-3m-1 5 3-3"/>',
 shield:'<path d="M12 3 4 6v6c0 5 8 9 8 9s8-4 8-9V6Z"/><path d="m8 12 3 3 5-6"/>',
 play:'<path d="m8 4 12 8-12 8Z"/>',pause:'<path d="M8 4v16m8-16v16"/>',
 dock:'<path d="m5 11 7-6 7 6v9H5Z"/><path d="M12 9v7m-3-3 3 3 3-3"/>',
 plus:'<path d="M12 4v16M4 12h16"/>',ban:'<rect x="4" y="4" width="16" height="16" rx="3"/><path d="m5 19 14-14"/>',
 hand:'<path d="M8 13V6a2 2 0 0 1 4 0v6-8a2 2 0 0 1 4 0v8-6a2 2 0 0 1 4 0v9c0 4-3 7-7 7-3 0-4-1-6-3l-4-5a2 2 0 0 1 3-2l2 2"/>',
 expand:'<path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5"/>',
 bolt:'<path d="m13 2-8 12h6l-1 8 9-13h-6Z"/>',
 info:'<circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10v.1"/>',
 edit:'<path d="m4 16 12-12 4 4L8 20H4Zm10-10 4 4"/>',
 trash:'<path d="M3 6h18M9 6V3h6v3M6 6l1 15h10l1-15M10 10v7m4-7v7"/>',
 check:'<path d="m5 12 4 4L19 6"/>'
};
const icon = name => `<svg viewBox="0 0 24 24" aria-hidden="true">${icons[name] || icons.info}</svg>`;
document.querySelectorAll('[data-icon]').forEach(el => el.innerHTML = icon(el.dataset.icon));
const app = { connected:false, page:'control', status:null, map:null, schedules:[], events:[], tool:'pan', selection:null,
  editingId:null, days:new Set([1,2,3,4,5]), busy:false, apiBusy:false, key:sessionStorage.getItem('zigma-key') || '',
  view:{zoom:1, panX:0, panY:0}, drag:null, lastEvents:0, activeArea:null, editingZone:null, zoneBase:null, pendingZones:null };
try {const saved=JSON.parse(sessionStorage.getItem('zigma-area'));if(saved&&saved.rect&&saved.context&&Number.isFinite(saved.at))app.activeArea=saved;}catch{}
const dayNames = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
const dayFull = ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
let robotTimezone='Europe/Madrid';
let dateFormat = new Intl.DateTimeFormat('en-GB',{timeZone:'Europe/Madrid',weekday:'short',day:'numeric',month:'short',hour:'2-digit',minute:'2-digit'});
let timeFormat = new Intl.DateTimeFormat('en-GB',{timeZone:'Europe/Madrid',hour:'2-digit',minute:'2-digit'});
const states = {charging:['At the dock','Ready for the next clean'],PileCharging:['At the dock','Charging for the next clean'],
  cleaning:['Cleaning','Cleaning in progress'],AutoClean:['Cleaning','Cleaning in progress'],PlanningRect:['Cleaning an area','Selected area cleaning in progress'],
  paused:['Paused','Resume when you are ready'],Pause:['Paused','Resume when you are ready'],returning:['Returning to dock','Looking for the charging dock'],BackCharging:['Returning to dock','Looking for the charging dock'],
  idle:['Ready','Ready to start'],Standby:['Standby','Waiting for a command'],unknown:['Unconfirmed state','Waiting for recent robot data']};
function toast(message) { $('toast').textContent=message; $('toast').classList.remove('hidden'); clearTimeout(toast.timer); toast.timer=setTimeout(()=>$('toast').classList.add('hidden'),4200); }
const laserView={frame:null,received:0,error:'',timer:null,busy:false,generation:0,mode:'fixed',overlay:true,disconnected:false};
function laserVisible(){return app.page==='control'&&app.mapView==='laser'&&!document.hidden&&Boolean(app.key);}
function laserState(frame,elapsed=0){
 if(!frame?.available||!Array.isArray(frame.ranges_mm)||frame.ranges_mm.length!==360||!frame.ranges_mm.every(v=>Number.isInteger(v)&&v>=0&&v<=100000)||!Number.isFinite(frame.read_age_ms)||frame.read_age_ms<0||elapsed<0||frame.read_age_ms+elapsed>3000)return 'unavailable';
 return Number.isFinite(frame.change_age_ms)&&frame.change_age_ms>=0&&frame.change_age_ms+elapsed<=3000?'changing':'cached';
}
// Fixed axes follow the house map. Robot axes rotate the entire scene so
// the same physical forward vector points up; no sensor calibration changes.
function laserHeading(heading){return heading;}
function laserViewRotation(heading,mode){return mode==='fixed'?0:-heading-Math.PI/2;}
function laserProjection(index,heading,mode){
 const a=index*Math.PI/180+(mode==='fixed'&&Number.isFinite(heading)?heading:-Math.PI/2);
 return {x:Math.cos(a),y:Math.sin(a)};
}
function laserArrowRotation(heading,mode){return mode==='fixed'?heading+Math.PI/2:0;}
function laserOverlayReady(map,pose,status){
 return Boolean(map?.available&&pose&&status?.working_status!=='Relocation'&&!status?.map_reset_pending&&!map.reset_pending&&
  String(map.map_id)===String(pose.map_id)&&map.map_id!=null&&map.width===pose.width&&map.height===pose.height&&
  Array.isArray(map.map_origin)&&Array.isArray(pose.map_origin)&&map.map_origin.length===2&&pose.map_origin.length===2&&map.map_origin.every((v,i)=>Number.isFinite(v)&&v===pose.map_origin[i])&&
  Number.isFinite(pose.x)&&Number.isFinite(pose.y)&&Number.isFinite(pose.heading)&&Number.isFinite(pose.resolution_m)&&pose.resolution_m>=.001&&pose.resolution_m<=1&&
  Array.isArray(map.cells)&&map.cells.length===map.width*map.height);
}
function drawLaserOverlay(ctx,map,pose,mode,cx,cy,scale){
 if(laserView.mapCells!==map.cells){
  const raster=document.createElement('canvas');raster.width=map.width;raster.height=map.height;
  const paint=raster.getContext('2d'),palette=['#fafaf5','#78916b','#bc995f',null];
  map.cells.forEach((v,i)=>{if(palette[v]){paint.fillStyle=palette[v];paint.fillRect(i%map.width,Math.floor(i/map.width),1,1);}});
  laserView.mapCells=map.cells;laserView.mapRaster=raster;
 }
 ctx.save();ctx.translate(cx,cy);ctx.rotate(laserViewRotation(pose.heading,mode));ctx.globalAlpha=.38;ctx.imageSmoothingEnabled=false;
 const cellScale=pose.resolution_m*scale;
 ctx.drawImage(laserView.mapRaster,-pose.x*cellScale,-pose.y*cellScale,map.width*cellScale,map.height*cellScale);
 if(map.dock){ctx.globalAlpha=.8;ctx.fillStyle='#477950';ctx.fillRect((map.dock.x-pose.x)*cellScale-5,(map.dock.y-pose.y)*cellScale-4,10,8);}
 ctx.restore();
}
function laserAccept(frame,now){
 if(laserState(frame)==='unavailable'){laserView.disconnected=true;return;}
 laserView.frame=frame;laserView.received=now;laserView.disconnected=false;laserView.error='';
}
function laserSchedule(){
 clearTimeout(laserView.timer);clearInterval(laserView.watchdog);laserView.generation++;laserView.error='';if(!app.key)laserView.frame=null;
 if(laserVisible()){laserView.watchdog=setInterval(()=>{drawMap();},500);laserPoll();}
}
async function laserPoll(){
 if(!laserVisible())return;
 if(laserView.busy){laserView.timer=setTimeout(laserPoll,200);return;}
 const generation=laserView.generation,started=performance.now();laserView.busy=true;
 try{const frame=await api('/api/laser');if(generation!==laserView.generation||!laserVisible())return;laserAccept(frame,performance.now());}
 catch(error){if(generation===laserView.generation){laserView.disconnected=true;laserView.error=error.message;}}
 finally{laserView.busy=false;if(generation===laserView.generation&&laserVisible()){drawMap();laserView.timer=setTimeout(laserPoll,laserView.error?2000:Math.max(100,1000-(performance.now()-started)));}}
}
function drawLaser(ctx,bounds){
 const frame=laserView.frame,elapsed=performance.now()-laserView.received;
 const state=laserState(frame,elapsed);
 const pose=frame?.navigation_pose,heading=pose?.heading;
 const fixed=laserView.mode==='fixed'&&Number.isFinite(heading);
 const overlay=laserView.overlay&&laserOverlayReady(app.map,pose,app.status);
 $('map-description').textContent=overlay?'Laser + simplified map · alignment preview':'Laser returns';$('position-legend').textContent='Robot · arrow points forward';
 $('map-source').textContent=state==='unavailable'?(frame?'OFFLINE':'NO DATA'):state==='changing'?'UPDATING':'RETAINED';
 $('map-empty').classList.add('hidden');
 $('laser-rotate').textContent=laserView.mode==='fixed'?'Fixed frame':'Robot frame';
 $('laser-rotate').setAttribute('aria-pressed',String(laserView.mode==='fixed'));
 $('laser-rotate').title='Switch between fixed map axes and robot facing up';
 $('laser-overlay').textContent=laserView.overlay?'Map overlay: on':'Map overlay: off';
 $('laser-overlay').setAttribute('aria-pressed',String(laserView.overlay));
 $('map-freshness').textContent=(state==='unavailable'?(frame?'Offline · scan older than 3 s':'No scan received yet'):state==='changing'?'Readings changing':'Last scan · no recent changes')+(laserView.mode==='fixed'&&!fixed?' · heading unavailable: robot frame':fixed?' · fixed map axes':' · robot facing up')+(laserView.overlay&&!overlay?' · map overlay unavailable':'');
 $('map-instruction').textContent='Drag to explore · Scroll to zoom';
 const scale=Math.min(bounds.width,bounds.height)/10*app.view.zoom,cx=bounds.width/2+app.view.panX,cy=bounds.height/2+app.view.panY;
 if(overlay)drawLaserOverlay(ctx,app.map,pose,fixed?'fixed':'robot',cx,cy,scale);
 ctx.strokeStyle='#b8c3af66';ctx.lineWidth=1;ctx.font='11px sans-serif';ctx.fillStyle='#536351';
 for(let r=1;r<=10;r++){ctx.beginPath();ctx.arc(cx,cy,r*scale,0,Math.PI*2);ctx.stroke();ctx.fillText(r+' m',cx+4,cy-r*scale-3);}
 if(frame){ctx.fillStyle=state==='changing'?'#127c81':'#888888';frame.ranges_mm.forEach((mm,i)=>{if(!mm)return;const p=laserProjection(i,heading,fixed?'fixed':'robot');ctx.beginPath();ctx.arc(cx+mm/1000*p.x*scale,cy+mm/1000*p.y*scale,2.2,0,Math.PI*2);ctx.fill();});}
 ctx.save();ctx.translate(cx,cy);ctx.rotate(laserArrowRotation(heading,fixed?'fixed':'robot'));
 ctx.fillStyle=state==='changing'?'#df8e25':'#888888';ctx.beginPath();ctx.moveTo(0,-10);ctx.lineTo(5.5,5.5);ctx.lineTo(0,3);ctx.lineTo(-5.5,5.5);ctx.closePath();ctx.fill();ctx.restore();
}

async function api(url, options={}) {
  const headers={'Content-Type':'application/json','X-Zigma-Request':'1',...options.headers};
  if(app.key) headers.Authorization=`Bearer ${app.key}`;
  if(url==='/api/map'&&app.mapTag)headers['If-None-Match']=app.mapTag;
  const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),6500);
  let response;try { response=await fetch(url,{...options,headers,signal:controller.signal}); } finally { clearTimeout(timer); }
  if(response.status===304&&url==='/api/map'&&app.map)return app.map;
  const value=await response.json();
  if(response.ok&&url==='/api/map')app.mapTag=response.headers.get('ETag');
  if(!response.ok) { if(response.status===401 && !$('access-dialog').open) $('access-dialog').showModal(); throw new Error(value.error || 'The request could not be completed'); }
  return value;
}
function changePage(page) {
 app.page=page;laserSchedule();document.querySelectorAll('.page').forEach(el=>el.classList.toggle('hidden',el.id!==`page-${page}`));
 document.querySelectorAll('.nav-item').forEach(el=>{const active=el.dataset.page===page;el.classList.toggle('active',active);el.setAttribute('aria-current',active?'page':'false');});
 if(page==='control') requestAnimationFrame(drawMap);
 if(page==='maps') loadSavedMaps().catch(showError);
 if(page==='schedules') loadSchedules().catch(showError);
 if(page==='activity') loadEvents().catch(showError);
 if(page==='settings') loadSettings().catch(showError);
 if(page==='about') loadSoftware().catch(showError);
 window.scrollTo({top:0,behavior:'smooth'});
}
function showError(error) { toast(error.name==='AbortError'?'The connection is taking too long. No command was repeated.':error.message); }
function renderStatus() {
 const s=app.status;if(!s)return;
 if(s.map_reset_pending){if(app.activeArea){app.activeArea=null;saveArea();}app.selection=null;app.pendingZones=null;app.mapTag=null;app.map={available:false,reset_pending:true,warning:s.map_restore?'Master restoration pending. Waiting for native confirmation and a new map.':'Clear request recorded. Waiting for a new robot map.'};if(app.tool!=='pan')setTool('pan');}
 const area=nextAreaState(app.activeArea,s);if(area!==app.activeArea){app.activeArea=area;saveArea();drawMap();}
 if(s.timezone&&s.timezone!==robotTimezone){robotTimezone=s.timezone;dateFormat=new Intl.DateTimeFormat('en-GB',{timeZone:robotTimezone,weekday:'short',day:'numeric',month:'short',hour:'2-digit',minute:'2-digit'});timeFormat=new Intl.DateTimeFormat('en-GB',{timeZone:robotTimezone,hour:'2-digit',minute:'2-digit'});}
 if($('settings-current-time'))$('settings-current-time').textContent=`${dateFormat.format(new Date(s.server_time*1000))} · ${robotTimezone}`;
 const expected={area_clean:['PlanningRect'],clean:['AutoClean','PlanningRect','cleaning'],resume:['AutoClean','PlanningRect','cleaning'],pause:['Pause','paused','Standby','idle'],dock:['BackCharging','PileCharging','returning','charging']};
 if(app.pending&&s.command_feedback?.action===app.pending.action&&s.command_feedback?.dispatched_at>=app.pending.at&&s.command_feedback?.phase==='expected_state_reported'){$('command-result').textContent='Robot reported the requested state after dispatch.';app.pending=null;}
 if(app.pending&&s.command_feedback?.action===app.pending.action&&s.command_feedback?.dispatched_at>=app.pending.at&&s.command_feedback?.phase==='error_reported'){$('command-result').textContent='Robot reported an error after dispatch. Check its status.';app.pending=null;}
 if(app.pending&&s.server_time-app.pending.at>30){$('command-result').textContent='No confirming report yet. The command was not retried.';app.pending=null;}
 if(app.pending&&s.command_feedback?.action!==app.pending.action&&s.fresh&&s.observed_at>=app.pending.at&&expected[app.pending.action]?.includes(s.working_status)){$('command-result').textContent=`New robot report: ${(states[s.working_status]||[s.working_status])[0]}`;app.pending=null;}
 const entry=states[s.working_status] || [s.working_status,'State reported by the robot'];
 document.title=`Zigma Free - ${s.robot}`;
 $('robot-name').textContent=s.robot;$('breadcrumb-name').textContent=s.robot;
 $('summary-state').textContent=s.fresh?entry[0]:'Stale data';$('robot-state-label').textContent=s.fresh?entry[0]:'';
 $('robot-state-detail').textContent=s.fresh?entry[1]:'State is old. Manual commands remain available while connected.';
 if(Number.isInteger(s.vendor_state_code)) $('robot-state-detail').textContent=s.fresh?entry[1]:s.observed_at>0?`Last report: ${entry[0]} · ${dateFormat.format(new Date(s.observed_at*1000))}`:'No timestamped robot report available';
 if(s.telemetry_state==='cached'){ $('robot-state-label').textContent=`${entry[0]} · cached`; $('summary-state').textContent='Cached robot state'; $('robot-state-detail').textContent=s.local_link_active?'Robot cache read successfully · local link active':'Robot cache read successfully · local link activity unconfirmed'; }
 if(s.telemetry_state==='missing')$('robot-state-detail').textContent='No timestamped robot report available';
 if(s.telemetry_state==='clock_mismatch')$('robot-state-detail').textContent='Robot report time is in the future; state unconfirmed';
 $('battery').replaceChildren(document.createTextNode(Number.isFinite(s.battery_level)?String(s.battery_level):'—'),unit('%'));
 const wifi=s.wifi;const wifiAvailable=wifi?.available&&Number.isFinite(wifi.signal_dbm);
 $('wifi-signal').replaceChildren(document.createTextNode(wifiAvailable?String(wifi.signal_dbm):'—'),unit('dBm'));
 $('wifi-signal').title=wifiAvailable?'Current Wi-Fi signal · closer to 0 is stronger':'Wi-Fi signal unavailable';
 $('battery').title=s.fresh?'Recent robot report':'Last reported battery; current value unconfirmed';
 $('clean-area').title=$('clean-time').title=s.statistics_available===false?'Statistics units have not been verified':s.fresh?'Recent robot report':'Last reported statistics';
 $('battery-fill').style.height=`${Math.max(0,Math.min(100,s.battery_level || 0))}%`;
 $('clean-area').replaceChildren(document.createTextNode(Number.isFinite(s.clean_area_m2)?(s.clean_area_m2*(s.area_unit==='ft2'?10.7639104167:1)).toLocaleString('en-GB',{maximumFractionDigits:1}):'—'),unit(s.area_unit==='ft2'?'ft²':'m²'));
 $('clean-time').replaceChildren(document.createTextNode(Number.isFinite(s.clean_time_s)?String(Math.floor(s.clean_time_s/60)):'—'),unit('min'));
 const demo=s.mode==='demo';$('mode-badge').textContent=demo?'LOCAL SIMULATION':'LOCAL AGENT';
 $('footer-note').textContent=demo?'Prototype · simulated controls and data':'Local control · check the state after sending a command';
 $('local-note-text').textContent=demo?'Demo with simulated data. No robot is connected.':s.fresh?'Local control.':'Last reported state.';
 $('connection-label').textContent=demo?'Prototype connected':'Agent connected';
 $('clock').textContent=timeFormat.format(new Date(s.server_time*1000));
 document.querySelector('.schedule-banner strong').textContent=`Robot local time · ${robotTimezone}`;document.querySelector('.schedule-time-row strong').textContent=robotTimezone;
 const paused=['paused','Pause'].includes(s.working_status),cleaning=['cleaning','AutoClean','PlanningRect'].includes(s.working_status),docked=['charging','PileCharging'].includes(s.working_status);
 $('primary-command-label').textContent=paused?'Resume cleaning':'Start cleaning';
 $('primary-command').disabled=app.busy || !app.connected || !s.capabilities.basic_control || (s.fresh&&cleaning) || (s.fresh&&['returning','BackCharging'].includes(s.working_status));
 $('pause-command').disabled=app.busy || !app.connected || !s.capabilities.basic_control || (s.fresh&&!(cleaning || ['returning','BackCharging'].includes(s.working_status)));
 $('dock-command').disabled=app.busy || !app.connected || !s.capabilities.basic_control || (s.fresh&&docked);
 document.querySelector('[data-tool="area"]').disabled=displayedMap().geometry||!app.map?.available||app.busy||Boolean(app.activeArea?.locked)||!app.connected||!s.capabilities.area_clean||!['charging','PileCharging','DirCharging','Standby','idle','Pause','paused'].includes(s.working_status);
 const zoneButton=document.querySelector('[data-tool="zone"]');zoneButton.disabled=!zoneEditingReady();zoneButton.title=zoneEditingReady()?'Create a no-go zone':!app.connected?'Connect to the robot first':!s.capabilities.map_edit?'Reload the page to load the zone editor':!(s.fresh||(s.cache_available&&s.local_link_active))?'Waiting for recent robot status and local communication':app.activeArea?.locked?'Waiting for area cleaning to finish':app.pendingZones?'Waiting for the robot to confirm the previous zone update':'Zone editing requires an idle robot with no error';
}
function sTime(){return app.status?.server_time || Math.floor(Date.now()/1000);}
function unit(text) { const el=document.createElement('small');el.textContent=text;return el; }
async function sendCommand(action,extra={}) {
 if(app.busy)return;app.busy=true;renderStatus();
 try { const result=await api('/api/commands',{method:'POST',body:JSON.stringify({action,...extra})});
  app.pending=result.result==='simulated'||action==='set_forbidden'?null:{action,at:result.dispatched_at || sTime()};
  if(action==='area_clean'){app.activeArea={rect:{...extra.rect},context:extra.map_context,at:result.dispatched_at||sTime(),started:result.result==='simulated',locked:true};saveArea();setTool('pan');}
  const message=result.result==='simulated'?'Done · simulated action':'Command dispatched. Waiting for robot feedback.';
  $('command-result').textContent=message;$('command-result').classList.remove('hidden');toast(message);
  await refresh();await loadEvents();return true;
 } catch(error) { $('command-result').textContent=error.message;$('command-result').classList.remove('hidden');showError(error);return false; } finally { app.busy=false;renderStatus(); }
}
async function refresh() {
 if(app.apiBusy)return;app.apiBusy=true;
 try { const previousDisplay=displayedMap();const previous=app.map;const previousPose=app.status?.navigation_pose;const status=await api('/api/status');const map=app.page==='control'&&(app.mapView!=='laser'||laserView.overlay)?await api('/api/map'):app.map;app.status=status;app.map=map;app.connected=true;if(app.mapView==='detailed')await loadDetailedMap();const nextDisplay=displayedMap();if(previousDisplay.geometry!==nextDisplay.geometry||JSON.stringify(mapContext(previousDisplay.map))!==JSON.stringify(mapContext(nextDisplay.map)))app.view={zoom:1,panX:0,panY:0};if(displayedMap().geometry&&app.tool!=='pan')setTool('pan');
  if(app.selection&&JSON.stringify(app.selectionContext)!==JSON.stringify(mapContext(map))){app.selection=null;app.drag=null;app.selectionContext=null;renderSelection();toast('The map changed. Draw the area again.');}
  updateZoneFeedback();
  if(app.tool==='zone'&&app.selection&&app.zoneBase&&zoneSignature(app.zoneBase)!==zoneSignature(map?.zones||[])){setTool('pan');toast('Zones changed on the robot. Select again.');}
  $('connection-error').classList.add('hidden');renderStatus();renderSelection();renderZones();if(map!==previous||displayedMap().geometry||app.mapView==='geometry'){drawMap();renderZones();}else if(map?.position_estimate||status.navigation_pose||previousPose){drawMap();}
 } catch(error) { app.connected=false;$('connection-label').textContent='Disconnected';$('connection-error').textContent='No recent agent data. Commands are not retried automatically.';$('connection-error').classList.remove('hidden');
  if(app.status) {app.status.fresh=false;renderStatus();$('connection-label').textContent='Disconnected';}
  ['primary-command','pause-command','dock-command','apply-selection'].forEach(id=>$(id).disabled=true);
  if(error.name!=='AbortError' && !error.message.includes('clave')) console.info('Agent unavailable');
 } finally {app.apiBusy=false;}
}
function eventElement(event,compact=false) {
 const row=document.createElement('div');row.className=compact?'recent-event':'activity-row';
 const badge=document.createElement('span');badge.className='event-icon';badge.innerHTML=icon(event.kind==='schedule'?'calendar':event.kind==='map'?'area':event.kind==='error'?'info':'check');
 const content=document.createElement('div'),text=document.createElement('strong'),time=document.createElement('time');text.textContent=event.message;time.dateTime=new Date(event.timestamp*1000).toISOString();time.textContent=dateFormat.format(new Date(event.timestamp*1000));content.append(text,time);row.append(badge,content);return row;
}
async function loadCleaningReports() {
 const host=$('cleaning-reports');if(!host)return;
 const data=await api('/api/sessions'),signature=JSON.stringify([data,app.status?.area_unit]);if(host.dataset.catalog===signature)return;host.dataset.catalog=signature;host.replaceChildren();
 if(!data.available)return;
 if(!data.items.length){if(data.integrity_errors||data.unassociated_records){const note=document.createElement('p');note.textContent='Records are present, but none can be associated reliably with a cleaning report.';host.append(note);}return;}
 const title=document.createElement('h2');title.textContent='Cleaning reports';host.append(title);
 for(const report of [...data.items].sort((a,b)=>String(b.started).localeCompare(String(a.started)))){
  const row=document.createElement('article');row.className='activity-row cleaning-report';
  const body=document.createElement('div');body.className='report-content';const heading=document.createElement('strong'),details=document.createElement('p');
  const outcome={finished_reported:'Finish reported by robot',partial_reported:'Partial cleaning report',recorded_completion_unknown:'Cleaning record · completion unknown',conflicting_reports:'Conflicting cleaning records'}[report.outcome]||'Cleaning record';
  heading.textContent=outcome;
  const units=app.status?.area_unit==='ft2';const stats=Number.isFinite(report.clean_area_m2)&&Number.isFinite(report.clean_time_s)?`${(report.clean_area_m2*(units?10.7639104:1)).toFixed(1)} ${units?'ft²':'m²'} · ${(report.clean_time_s/60).toFixed(0)} min`:'Statistics unavailable';
  details.textContent=`${report.started} → ${report.ended} · ${stats}${report.time_basis==='native_local_time_timezone_unknown'?' · Robot local time; timezone unknown':''}`;
  body.append(heading,details);row.append(body);host.append(row);
  if(report.map_available){
   const button=document.createElement('button');button.className='secondary-button';button.textContent='View cleaning map';body.append(button);
   button.addEventListener('click',async()=>{button.disabled=true;try{
    const map=await api(`/api/sessions/${report.id}/map`),canvas=document.createElement('canvas');canvas.width=map.width;canvas.height=map.height;canvas.className='cleaning-report-map';canvas.setAttribute('aria-label','Historical cleaning map');canvas.setAttribute('role','img');
    const ctx=canvas.getContext('2d'),image=ctx.createImageData(map.width,map.height),colors=[[237,243,240],[64,79,74],[220,228,224],[255,255,255]];
    map.cells.forEach((v,i)=>{image.data.set([...colors[v],255],i*4);});ctx.putImageData(image,0,0);
    ctx.strokeStyle='#087f71';ctx.lineWidth=1.5;ctx.beginPath();(map.path||[]).forEach((p,i)=>i?ctx.lineTo(p.x,p.y):ctx.moveTo(p.x,p.y));ctx.stroke();body.append(canvas);button.remove();
   }catch(error){button.disabled=false;showError(error);}});
  }
 }
 if(data.integrity_errors||data.unassociated_records){const note=document.createElement('p');note.textContent='Some records could not be associated reliably; they are not shown as completed reports.';host.append(note);}
}
async function loadEvents() {
 const data=await api('/api/events');app.events=data.items;$('recent-events').replaceChildren(...data.items.slice(0,2).map(e=>eventElement(e,true)));
 $('activity-list').replaceChildren(...data.items.map(e=>eventElement(e)));app.lastEvents=Date.now();if(app.page==='activity')await loadCleaningReports();
}
function daySummary(days) {
 if(days.length===7)return 'Every day';if([1,2,3,4,5].every(d=>days.includes(d))&&days.length===5)return 'Monday to Friday';
 return [1,2,3,4,5,6,0].filter(d=>days.includes(d)).map(d=>dayNames[d]).join(' · ');
}
async function loadSchedules() { const data=await api('/api/schedules');app.schedules=data.items;app.scheduleSongs=data.songs||[];renderSchedules(data); }
function scheduleActionLabel(job){return job.action==='random_song'?'Random song':job.action==='play_song'?`Play song · ${job.song}`:job.action==='custom_command'?`Custom command · ${job.command}`:job.action==='dock'?'Return to dock':job.action==='reboot'?'Reboot robot':job.action==='restore_master'?`Restore master · ${job.master_name||'Unavailable copy'}`:`Full clean · Fan ${job.fan_power??3}`;}
function renderExternalCron(data){
 const list=$('external-cron-list');list.replaceChildren();
 $('external-cron-status').textContent=data.external_available?`${data.external_entries.length} entries outside Zigma Free · Read only${data.external_truncated?' · More entries are available in the raw table':''}`:(data.external_error||'Robot crontab is unavailable.');
 for(const entry of data.external_entries||[]){const card=document.createElement('article');card.className='external-cron-entry';const label=document.createElement('strong'),raw=document.createElement('pre');label.textContent=entry.label||'Custom command';raw.textContent=entry.raw;card.append(label,raw);list.append(card);}
 if(data.external_available&&!data.external_entries.length)list.textContent='No additional entries.';
}
function renderSchedules(data) {
 $('schedule-count').textContent=String(app.schedules.filter(j=>j.enabled).length);
 $('cron-preview').textContent=data.crontab_raw??data.cron_preview;$('cron-table-title').textContent=data.crontab_raw!==undefined?'Robot crontab (raw)':'Generated crontab block';renderExternalCron(data);
 $('schedule-installation').textContent=data.installed?'Cron is running · Schedules installed on the robot.':data.cron?.available&&!data.cron?.running?'Cron is installed but not running. Saving schedules will try to start it.':app.status?.mode==='demo'?'Schedules are saved in this prototype. No robot crontab is installed yet.':'Schedules saved; crontab installation pending.';
 const list=$('schedule-list');list.replaceChildren();
 if(!app.schedules.length) {
  const empty=document.createElement('div');empty.className='empty-schedules';empty.innerHTML=`<span>${icon('calendar')}</span><h2>No schedules</h2><button class="primary-button fit">Create a schedule</button>`;empty.querySelector('button').addEventListener('click',()=>openSchedule());list.append(empty);
 }
 for(const job of app.schedules) {
  const row=document.createElement('article');row.className=`schedule-row${job.enabled?'':' off'}`;
  const hour=document.createElement('div');hour.className='schedule-hour';hour.textContent=job.time;
  const info=document.createElement('div'),title=document.createElement('h3'),days=document.createElement('p');title.textContent=job.label;days.textContent=`${daySummary(job.days)} · ${scheduleActionLabel(job)}${job.execution_method==='direct_control'?' · Direct cron command':job.last_result?` · Last: ${job.last_result.replaceAll('_',' ')}`:''}`;info.append(title,days);
  const next=document.createElement('div');next.className='schedule-next';const small=document.createElement('small');small.textContent='NEXT RUN';next.append(small,document.createTextNode(job.next_run?dateFormat.format(new Date(job.next_run*1000)):'Disabled'));
  const actions=document.createElement('div');actions.className='schedule-row-actions';
  const toggle=document.createElement('button');toggle.className=`switch${job.enabled?' on':''}`;toggle.role='switch';toggle.setAttribute('aria-checked',String(job.enabled));toggle.setAttribute('aria-label',`${job.enabled?'Desactivar':'Activar'} ${job.label}`);
  toggle.addEventListener('click',()=>saveSchedules(app.schedules.map(x=>x.id===job.id?{...x,enabled:!x.enabled}:x)));
  const edit=document.createElement('button');edit.className='icon-button';edit.innerHTML=icon('edit');edit.setAttribute('aria-label',`Edit ${job.label}`);edit.addEventListener('click',()=>openSchedule(job));
  const remove=document.createElement('button');remove.className='icon-button';remove.innerHTML=icon('trash');remove.setAttribute('aria-label',`Delete ${job.label}`);remove.addEventListener('click',()=>{if(window.confirm(`Delete schedule “${job.label}”?`))saveSchedules(app.schedules.filter(x=>x.id!==job.id));});
  actions.append(toggle,edit,remove);row.append(hour,info,next,actions);list.append(row);
 }
 const next=app.schedules.filter(j=>j.enabled&&j.next_run).sort((a,b)=>a.next_run-b.next_run)[0];
 $('next-routine-time').textContent='At your own pace';
 // Use the server epoch with an explicit timezone, not the browser weekday.
 if(next)$('next-routine-time').textContent=new Intl.DateTimeFormat('en-GB',{timeZone:robotTimezone,weekday:'long',hour:'2-digit',minute:'2-digit'}).format(new Date(next.next_run*1000));
 $('next-routine-detail').textContent=next?next.label:'No schedules yet.';
}
function schedulePayload(job){const {id,label,time,days,enabled,action,confirm_action,master_uid,master_revision,master_name,fan_power,song,command}=job;return {id,label,time,days,enabled,action:action||'clean',confirm_action:Boolean(confirm_action),master_uid,master_revision,master_name,...((action||'clean')==='clean'&&fan_power!==undefined?{fan_power}:{}),...(action==='play_song'?{song}:{}),...(action==='custom_command'?{command}:{})};}
async function saveSchedules(items) {
 if(app.busy)return false;app.busy=true;
 const clean=items.map(schedulePayload);
 try { const data=await api('/api/schedules',{method:'PUT',body:JSON.stringify({items:clean})});app.schedules=data.items;renderSchedules(data);await loadEvents();toast(data.installed?'Schedules installed in crontab':'Schedules saved; check cron status');return true;
 } catch(error) {showError(error);await loadSchedules().catch(()=>{});return false;}finally{app.busy=false;}
}
function renderDays() { const picker=$('day-picker');picker.replaceChildren();for(const day of [1,2,3,4,5,6,0]) {const b=document.createElement('button');b.type='button';b.textContent=dayNames[day];b.className=app.days.has(day)?'selected':'';b.setAttribute('aria-label',dayFull[day]);b.setAttribute('aria-pressed',String(app.days.has(day)));b.addEventListener('click',()=>{app.days.has(day)?app.days.delete(day):app.days.add(day);renderDays();});picker.append(b);} }
function updateScheduleAction(){
 const action=$('schedule-action').value,restore=action==='restore_master',special=action==='reboot'||restore;
 $('schedule-song-field').classList.toggle('hidden',action!=='play_song');$('schedule-song').required=action==='play_song';
 $('schedule-command-field').classList.toggle('hidden',action!=='custom_command');$('schedule-command').required=action==='custom_command';
 $('schedule-fan-field').classList.toggle('hidden',action!=='clean');
 $('schedule-master-field').classList.toggle('hidden',!restore);$('schedule-master').required=restore;
 $('schedule-confirm-field').classList.toggle('hidden',!special);$('schedule-action-confirm').required=special;
 $('schedule-action-note').textContent=action==='random_song'?'Plays a random MP3 using play_random_song.sh at the selected time.':action==='play_song'?'Plays the selected file from the songs folder using audio_player.':action==='custom_command'?'Runs your shell command as root on the robot at the selected time. Use absolute paths.':action==='dock'?'Stops cleaning and returns to the base at the selected time. Requires recent, error-free status.':action==='reboot'?'Reboots both controllers directly at the selected time, including during cleaning. The web disconnects and restarts.':restore?'Restores this protected copy without starting a clean. Working maps are backed up first. A deleted or unavailable master is skipped.':'Sends zigmactl clean directly at the selected time, without waiting for a recent status report.';
}
$('schedule-action').addEventListener('change',()=>{$('schedule-action-confirm').checked=false;updateScheduleAction();});
async function openSchedule(job=null) {
 let copies=[];try{copies=(await api('/api/map-library')).items.filter(copy=>!copy.deleted&&copy.verified!==false);}catch(error){/* Full clean and reboot remain available if the library is unavailable. */}
 const select=$('schedule-master');select.replaceChildren();for(const copy of copies){const option=document.createElement('option');option.value=copy.uid;option.textContent=`${copy.name} · Map ${copy.id}`;option.dataset.revision=copy.revision;select.append(option);}
 if(!copies.length||job?.master_uid&&!copies.some(copy=>copy.uid===job.master_uid)){const option=document.createElement('option');option.value=job?.master_uid||'';option.textContent=job?.master_uid?'Saved copy unavailable — choose another':'No verified master copies';option.dataset.revision=job?.master_revision||'';select.prepend(option);}
 if(job?.master_uid)select.value=job.master_uid;
 $('schedule-song').value=job?.song||'';$('schedule-command').value=job?.command||'';const songs=$('schedule-song-list');songs.replaceChildren();for(const name of [...(app.scheduleSongs||[])].sort()){const option=document.createElement('option');option.value=name;songs.append(option);}
 $('schedule-fan-power').value=String(job?.fan_power??3);$('schedule-action').value=job?.action||'clean';$('schedule-action-confirm').checked=Boolean(job?.confirm_action);updateScheduleAction();
 app.editingId=job?.id??null;app.days=new Set(job?.days || [1,2,3,4,5]);$('schedule-dialog-title').textContent=job?'Edit schedule':'New schedule';$('schedule-label').value=job?.label || '';$('schedule-time').value=job?.time || '09:30';$('schedule-error').classList.add('hidden');renderDays();$('schedule-dialog').showModal();
}
$('schedule-form').addEventListener('submit',async event=>{event.preventDefault();const label=$('schedule-label').value.trim();if(!app.days.size||!label){$('schedule-error').textContent='Choose at least one day and enter a name.';$('schedule-error').classList.remove('hidden');return;}
 const old=app.schedules.find(x=>x.id===app.editingId);const item={id:app.editingId,label,time:$('schedule-time').value,days:[...app.days].sort(),enabled:old?.enabled??true,action:$('schedule-action').value,song:$('schedule-song').value.trim(),command:$('schedule-command').value,...($('schedule-action').value==='clean'?{fan_power:Number($('schedule-fan-power').value)}:{}),confirm_action:$('schedule-action-confirm').checked,master_uid:$('schedule-master').value,master_revision:$('schedule-master').selectedOptions[0]?.dataset.revision||''};
 const items=app.editingId?app.schedules.map(x=>x.id===app.editingId?item:x):[...app.schedules,item];
 const button=event.submitter;button.disabled=true;try{if(await saveSchedules(items))$('schedule-dialog').close();}finally{button.disabled=false;}
});
function mapDisplay(map,status,mode='map',now=Date.now()/1000) {if(map?.reset_pending||status?.map_reset_pending)return {map:null,geometry:false};const g=status?.map_geometry;const age=now-g?.cache_read_at;const valid=g?.available&&age>=0&&age<=15&&Number.isInteger(g.width)&&Number.isInteger(g.height)&&g.width>0&&g.height>0;const raster=map?.available&&Array.isArray(map.cells)&&map.cells.length===map.width*map.height;const relocating=status?.working_status==='Relocation';const changed=valid&&g.map_id&&map?.map_id&&g.map_id!==map.map_id;if(relocating||changed)return {map:valid?g:null,geometry:true};if(mode==='geometry')return {map:valid?g:raster?map:null,geometry:true};return raster?{map,geometry:false}:{map:valid?g:null,geometry:true};}
function detailedDisplay(detail,map,status) {const blocked=map?.reset_pending||status?.map_reset_pending;const mismatch=map?.map_id&&detail?.map_id!==String(map.map_id);return {map:!blocked&&!mismatch?detail:null,geometry:true,detailed:true};}
function detailedOverlayGeometry(detail,map,status,g) {if(!detail?.available||!map?.available||status?.working_status==='Relocation'||status?.map_reset_pending||map.reset_pending||String(map.map_id)!==detail.map_id||!Array.isArray(detail.grid_origin)||detail.grid_origin.length!==2||!Array.isArray(map.map_origin)||map.map_origin.length!==2||![...detail.grid_origin,...map.map_origin].every(Number.isFinite)||!Number.isInteger(detail.preview_step)||detail.preview_step<1||!Number.isFinite(detail.resolution_m)||Math.abs(detail.resolution_m-.05)>1e-7)return null;const scale=g.scale/detail.preview_step;return {...g,scale,left:g.left+(detail.grid_origin[0]-map.map_origin[0])*scale,top:g.top+(detail.grid_origin[1]-map.map_origin[1])*scale};}
function displayedMap() {if(app.mapView==='laser')return {geometry:true,map:{available:true,width:10,height:10,zones:[]}};return app.mapView==='detailed'?detailedDisplay(app.detailMap,app.map,app.status):mapDisplay(app.map,app.status,app.mapView);}
async function loadDetailedMap() {
 if(app.detailBusy||app.mapView!=='detailed'||app.page!=='control'||document.hidden)return;
 if(app.map?.reset_pending||app.status?.map_reset_pending){app.detailMap=null;return;}
 app.detailBusy=true;app.detailError='Reading saved occupancy…';
 try {
  let id=app.map?.map_id;
  if(!id||!/^\d+$/.test(String(id))){const catalog=await api('/api/maps');if(catalog.reset_pending)throw new Error('Map change pending');id=catalog.native_active_id;}
  if(!id||!/^\d+$/.test(String(id)))throw new Error('No saved active map is available');
  id=String(id);const preview=await api(`/api/maps/${encodeURIComponent(id)}/preview`);
  if(preview.id!==id)throw new Error('Saved map identity changed');
  if(app.mapView!=='detailed'||app.map?.reset_pending||app.status?.map_reset_pending||(app.map?.map_id&&String(app.map.map_id)!==id))return;
  if(app.detailMap?.revision!==preview.revision||app.detailMap?.map_id!==id){const pixels=savedPreviewPixels(preview),raster=document.createElement('canvas');raster.width=preview.preview_width;raster.height=preview.preview_height;raster.getContext('2d').putImageData(new ImageData(pixels,raster.width,raster.height),0,0);app.detailMap={available:true,map_id:id,revision:preview.revision,width:raster.width,height:raster.height,raster,grid_origin:preview.grid_origin,preview_step:preview.preview_step,resolution_m:preview.resolution_m};}
  app.detailError='';
 }catch(error){app.detailMap=null;app.detailError=error.name==='AbortError'?'Saved map read timed out':error.message;}
 finally{app.detailBusy=false;if(app.mapView==='detailed')drawMap();}
}
function geometry() {const map=displayedMap().map;if(!map?.available)return null;const canvas=$('map'),rect=canvas.getBoundingClientRect(),pad=24;const scale=Math.min((rect.width-pad*2)/map.width,(rect.height-pad*2)/map.height)*app.view.zoom;return {scale,left:rect.width/2-map.width*scale/2+app.view.panX,top:rect.height/2-map.height*scale/2+app.view.panY,rect};}
function toScreen(point,g) {return {x:g.left+point.x*g.scale,y:g.top+point.y*g.scale};}
function toGrid(event) {const g=geometry();return g?{x:(event.clientX-g.rect.left-g.left)/g.scale,y:(event.clientY-g.rect.top-g.top)/g.scale}:null;}
function navigationPose(map,status,now=Date.now()/1000) {const p=status?.navigation_pose;if(!p||p.kind!=='navigation_pose_cache'||p.frame!=='map_grid'||p.map_id!==map?.map_id||p.width!==map.width||p.height!==map.height||!Array.isArray(p.map_origin)||!Array.isArray(map.map_origin)||p.map_origin.length!==2||map.map_origin.length!==2||p.map_origin.some((v,i)=>!Number.isFinite(v)||v!==map.map_origin[i])||![p.x,p.y,p.heading,p.cache_read_at,now].every(Number.isFinite)||p.x<0||p.y<0||p.x>=map.width||p.y>=map.height||now-p.cache_read_at<0||now-p.cache_read_at>15)return null;return p;}
function robotPoseText(pose){
 if(!pose||!Array.isArray(pose.map_origin)||pose.map_origin.length!==2||![pose.x,pose.y,pose.heading,...pose.map_origin].every(Number.isFinite))return 'Robot · position unavailable';
 const heading=((pose.heading*180/Math.PI+90)%360+360)%360;
 return `Robot X: ${(pose.x-pose.map_origin[0]).toFixed(1)}  Y: ${(pose.y-pose.map_origin[1]).toFixed(1)} · Heading: ${(Math.round(heading*10)/10%360).toFixed(1)}° · cached`;
}
function renderRobotPose(){
 const blocked=app.map?.reset_pending||app.status?.map_reset_pending||app.status?.working_status==='Relocation';
 const pose=blocked?null:navigationPose(app.map,app.status);
 $('robot-coordinates').textContent=robotPoseText(pose);
}
function drawMap() {
 renderRobotPose();
 if(app.hoverScreen)showMapCoordinates(app.hoverScreen);else $('map-coordinates').textContent='X: —  Y: —';
 const canvas=$('map'),bounds=canvas.getBoundingClientRect();if(!bounds.width||!bounds.height)return;const ratio=Math.min(devicePixelRatio || 1,2);canvas.width=Math.round(bounds.width*ratio);canvas.height=Math.round(bounds.height*ratio);const ctx=canvas.getContext('2d');ctx.scale(ratio,ratio);ctx.clearRect(0,0,bounds.width,bounds.height);
 document.querySelector('.map-panel').classList.toggle('detailed-view',app.mapView==='detailed');document.querySelector('.map-panel').classList.toggle('laser-view',app.mapView==='laser');$('laser-rotate').classList.toggle('hidden',app.mapView!=='laser');$('laser-overlay').classList.toggle('hidden',app.mapView!=='laser');document.querySelector('.map-panel h2').textContent=app.mapView==='laser'?'Live LIDAR':'Your map';if(app.mapView==='laser'){drawLaser(ctx,bounds);return;}
 const display=displayedMap();let map=display.map;const geometryOnly=display.geometry;document.querySelector('.map-panel').classList.toggle('detailed-view',Boolean(display.detailed));$('map-empty').classList.toggle('hidden',Boolean(map?.available));if(!map?.available){$('map-empty').textContent=app.map?.reset_pending?app.map.warning:app.mapView==='detailed'?(app.detailError||'Saved occupancy is not available yet.'):app.mapView==='geometry'?'Usable path data is not available yet.':'The map is not available yet.';$('map-description').textContent=app.map?.reset_pending?'Waiting for a new robot map':'Map unavailable';$('position-legend').textContent='Position unavailable';$('map-source').textContent='UNAVAILABLE';$('map-freshness').textContent=app.map?.reset_pending?'Old map hidden during map change':'';return;}
 let g=geometry();if(display.detailed){ctx.imageSmoothingEnabled=false;ctx.drawImage(map.raster,g.left,g.top,map.width*g.scale,map.height*g.scale);$('map-description').textContent='Detailed occupancy · saved map · read only';$('map-source').textContent='SAVED GRID';$('map-freshness').textContent='Saved occupancy · overlays unavailable for this frame';$('position-legend').textContent='Position unavailable';const overlay=detailedOverlayGeometry(map,app.map,app.status,g);if(!overlay)return;g=overlay;map=app.map;ctx.save();const frame=geometry();ctx.beginPath();ctx.rect(frame.left,frame.top,display.map.width*frame.scale,display.map.height*frame.scale);ctx.clip();}
 if(!geometryOnly){
 if(app.gridCache?.cells!==map.cells){const raster=document.createElement('canvas');raster.width=map.width;raster.height=map.height;const paint=raster.getContext('2d'),palette=['#fafaf5','#b8c3af','#d9bd8b',null];for(let y=0;y<map.height;y++)for(let x=0;x<map.width;x++){const color=palette[map.cells[y*map.width+x]];if(color){paint.fillStyle=color;paint.fillRect(x,y,1,1);}}app.gridCache={cells:map.cells,raster};}
 ctx.imageSmoothingEnabled=false;ctx.drawImage(app.gridCache.raster,g.left,g.top,map.width*g.scale,map.height*g.scale);
 } else if(!display.detailed){ctx.strokeStyle="#b8c3af";ctx.setLineDash([4,4]);ctx.strokeRect(g.left,g.top,map.width*g.scale,map.height*g.scale);ctx.setLineDash([]);}
 if(map.path?.length){ctx.beginPath();map.path.forEach((p,i)=>{const q=toScreen(p,g);i?ctx.lineTo(q.x,q.y):ctx.moveTo(q.x,q.y);});ctx.strokeStyle='#b4a181';ctx.lineWidth=1.7;ctx.setLineDash([3,4]);ctx.stroke();ctx.setLineDash([]);}
 for(const zone of visibleMapZones(map))drawZone(ctx,zone,g);
 if(!geometryOnly&&app.activeArea?.locked&&JSON.stringify(app.activeArea.context)===JSON.stringify(mapContext(map)))drawRect(ctx,app.activeArea.rect,g,'#398bdf',true);
 if(!geometryOnly&&app.selection)drawRect(ctx,app.selection,g,app.tool==='area'?'#398bdf':'#e89435',false);
 if(map.dock){const p=toScreen(map.dock,g);ctx.fillStyle='#819572';ctx.beginPath();ctx.roundRect(p.x-6,p.y-4,12,9,2);ctx.fill();ctx.strokeStyle='#f7f8f2';ctx.lineWidth=1.5;ctx.beginPath();ctx.moveTo(p.x-3,p.y);ctx.lineTo(p.x+3,p.y);ctx.stroke();}
 const candidate=map.position_estimate;const age=Date.now()/1000-(candidate?.received_at || 0);const estimate=(!geometryOnly||display.detailed)&&age>=0&&age<=6?candidate:null;const nav=geometryOnly&&!display.detailed?null:navigationPose(map,app.status);const marker=nav || map.position || estimate || map.path_endpoint;
 if(marker){const p=toScreen(marker,g);ctx.fillStyle=nav?'#487b98':map.position||estimate?'#436b53':'#b4a181';ctx.shadowBlur=10;ctx.shadowColor='#456a4433';ctx.beginPath();ctx.arc(p.x,p.y,6,0,Math.PI*2);ctx.fill();ctx.shadowBlur=0;ctx.strokeStyle='#fff';ctx.lineWidth=2;ctx.stroke();ctx.beginPath();ctx.arc(p.x,p.y,10,0,Math.PI*2);ctx.strokeStyle='#456a4425';ctx.lineWidth=2;ctx.stroke();}
 if(nav){const p=toScreen(nav,g);ctx.strokeStyle='#487b98';ctx.lineWidth=2.5;ctx.beginPath();ctx.moveTo(p.x,p.y);ctx.lineTo(p.x+18*Math.cos(nav.heading),p.y+18*Math.sin(nav.heading));ctx.stroke();}
 document.querySelector('.legend-robot').style.background=nav?'#487b98':map.position||estimate?'#436b53':'#b4a181';
 $('position-legend').textContent=nav?'Navigation position · cached':map.position?'Reported position':estimate?'Position estimate · updating':map.path_endpoint?'Last recorded point · not live':'Position unavailable';
 $('map-description').textContent=map.source==='synthetic'?'An example home, without private data':'Map received from the agent';$('map-source').textContent=map.source==='synthetic'?'SIMULATED':'LOCAL MAP';$('map-freshness').textContent=map.source==='synthetic'?'Simulated data':(nav?'Navigation pose and heading from cache · physical sampling time unknown':(map.warning || 'Last received path · freshness unknown'));
 if(geometryOnly){$('map-description').textContent=app.status?.working_status==='Relocation'?'Relocating · path view':app.mapView==='geometry'?'Path · read only':'Current map unavailable · showing path';$('map-source').textContent=map.source==='local_core_geometry'?'RECEIVED PATH':'CACHED PATH';$('map-freshness').textContent=map.frame==='native_geometry_bounds'?'Native zone geometry · units and measurement time unknown · border is view extent':'Path and available zones · measurement time unknown · border is map extent';}
 if(display.detailed){ctx.restore();$('map-description').textContent='Detailed occupancy · saved map · read only';$('map-source').textContent='SAVED GRID + OVERLAYS';$('map-freshness').textContent='Saved occupancy · cached overlays · alignment preview';}

}
function drawZone(ctx,zone,g){if(!Array.isArray(zone.points)){drawRect(ctx,zone,g,'#c47e6c',true);return;}if(zone.points.length!==4)return;const color=zone.can_clean?'#c79a51':'#ba6d5d';ctx.beginPath();zone.points.forEach((p,i)=>{const q=toScreen(p,g);i?ctx.lineTo(q.x,q.y):ctx.moveTo(q.x,q.y);});ctx.closePath();ctx.fillStyle=`${color}33`;ctx.strokeStyle=color;ctx.lineWidth=1.6;ctx.fill();ctx.stroke();}
function drawRect(ctx,r,g,color,saved) {const a=toScreen({x:r.x,y:r.y},g);ctx.fillStyle=`${color}1c`;ctx.fillRect(a.x,a.y,r.width*g.scale,r.height*g.scale);ctx.strokeStyle=color;ctx.lineWidth=1.4;ctx.setLineDash(saved?[]:[5,3]);ctx.strokeRect(a.x,a.y,r.width*g.scale,r.height*g.scale);ctx.setLineDash([]);}
function setTool(tool) {
 if(tool!=='pan'&&displayedMap().geometry)return;
 if(tool==='area'&&(app.busy||app.activeArea?.locked))return;
 if(tool==='zone'&&!zoneEditingReady())return;
 app.editingZone=null;app.zoneBase=tool==='zone'?structuredClone(app.map.zones||[]):null;if(tool==='zone')$('zone-restriction').value='all';
 app.tool=tool;app.selection=null;app.drag=null;canvas.style.cursor='';renderSelection();document.querySelectorAll('.map-tool').forEach(el=>el.classList.toggle('selected',el.dataset.tool===tool));document.querySelector('.map-stage').classList.toggle('drawing',tool!=='pan');$('map-instruction').textContent=tool==='pan'?'Drag to explore · Scroll to zoom':tool==='area'?'Draw the rectangle you want to clean':'Draw a no-go zone';
 app.selectionContext=null;drawMap();
}
function saveArea() {try {sessionStorage.setItem('zigma-area',JSON.stringify(app.activeArea));}catch{}}
function nextAreaState(area,s) {if(!area?.locked)return null;const recent=(s.fresh&&s.observed_at>=area.at)||(s.cache_available&&s.local_link_active&&s.cache_read_at>=area.at);if(!recent)return area;const work=s.working_status;if(['PlanningRect','Relocation','AutoClean','cleaning'].includes(work)&&!area.started)return {...area,started:true};if(['charging','PileCharging','DirCharging','Standby','idle'].includes(work)&&(area.started||s.server_time-area.at>30))return null;return area;}
function mapContext(map) {return {map_id:map?.map_id,width:map?.width,height:map?.height,map_origin:map?.map_origin?.slice()};}
function renderSelection() {
 $('zone-restriction-field').classList.toggle('hidden',app.tool!=='zone');
 $('selection-panel').classList.toggle('hidden',!app.selection);if(!app.selection)return;const r=app.selection;
 $('map-instruction').textContent=`Drag inside the ${app.tool==='area'?'blue':'orange'} rectangle to move it · Drag outside to draw again`;
 $('selection-title').textContent=app.tool==='zone'?(app.editingZone===null?'New no-go zone':`Edit zone ${app.editingZone+1}`):'Selected area';$('selection-size').textContent=`${r.width} × ${r.height} map cells`;
 for(const key of ['x','y','width','height'])$(`rect-${key}`).value=String(Math.round(r[key]));
 $('apply-selection').textContent=app.tool==='zone'?'Save zone':'Clean this area';$('apply-selection').disabled=app.busy||!app.connected||(app.tool==='zone'&&!zoneEditingReady())||r.width<2||r.height<2||(app.tool==='area'&&(app.activeArea?.locked||!app.status?.capabilities.area_clean||!['charging','PileCharging','DirCharging','Standby','idle','Pause','paused'].includes(app.status?.working_status)));
}
function zoneEditingReady() {const s=app.status;return Boolean(!displayedMap().geometry&&app.connected&&!app.busy&&!app.pendingZones&&!app.activeArea?.locked&&s?.capabilities.map_edit&&(s.mode==='demo'||app.map?.zones_available)&&(s.fresh||(s.cache_available&&s.local_link_active))&&s.error==='NoError'&&['charging','PileCharging','DirCharging','Standby','idle'].includes(s.working_status));}
function zoneSignature(zones) {return JSON.stringify((zones||[]).map(z=>{const points=z.points||rectangleZone(z,{}).points,xy=points.map(p=>[snapGridCell(p.x),snapGridCell(p.y)]),variants=[];for(const order of [xy,[...xy].reverse()])for(let i=0;i<order.length;i++)variants.push(JSON.stringify([...order.slice(i),...order.slice(0,i)]));return {polygon:variants.sort()[0],can_clean:z.can_clean,can_mop:z.can_mop};}));}
function rectangleZone(rect,flags) {return {points:[{x:rect.x,y:rect.y},{x:rect.x,y:rect.y+rect.height},{x:rect.x+rect.width,y:rect.y+rect.height},{x:rect.x+rect.width,y:rect.y}],...flags};}
function snapGridCell(value) {return Number.isFinite(value)&&Math.abs(value-Math.round(value))<=0.00001?Math.round(value):value;}
function zoneRectangle(z) {const p=z.points?.map(v=>({x:snapGridCell(v.x),y:snapGridCell(v.y)}));if(!p)return {x:z.x,y:z.y,width:z.width,height:z.height};if(p.length!==4||p.some(v=>!Number.isInteger(v.x)||!Number.isInteger(v.y))||new Set(p.map(v=>v.x)).size!==2||new Set(p.map(v=>v.y)).size!==2||new Set(p.map(v=>`${v.x},${v.y}`)).size!==4)return null;const x=Math.min(...p.map(v=>v.x)),y=Math.min(...p.map(v=>v.y));return {x,y,width:Math.max(...p.map(v=>v.x))-x,height:Math.max(...p.map(v=>v.y))-y};}
function editZone(index) {if(!zoneEditingReady())return;const z=app.map.zones[index],r=zoneRectangle(z);if(!r){toast('This polygon can be preserved or deleted; rectangle editing is unavailable.');return;}setTool('zone');app.editingZone=index;app.selection=r;app.selectionContext=mapContext(app.map);$('zone-restriction').value=z.can_clean?'mop':z.can_mop?'vacuum':'all';renderSelection();drawMap();}
async function changeZone(operation,index=null) {
 if(!zoneEditingReady())return false;
 const flags={can_clean:$('zone-restriction').value==='mop',can_mop:$('zone-restriction').value==='vacuum'};
 const base=operation==='delete'?structuredClone(app.map.zones||[]):app.zoneBase,context=operation==='delete'?mapContext(app.map):app.selectionContext,rect=app.selection?{...app.selection}:null;
 if(!base)return false;const target=structuredClone(base);if(operation==='delete')target.splice(index,1);else {const z=rectangleZone(rect,flags);if(operation==='add')target.push(z);else target[index]=z;}
 const ok=await sendCommand('set_forbidden',{operation,index,rect,flags,base_zones:base,map_context:context});
 if(ok){app.pendingZones={target,base,context,at:Date.now()};setTool('pan');$('command-result').textContent='Zone update sent. Waiting for the robot map.';renderStatus();renderZones();updateZoneFeedback();}return ok;
}
function updateZoneFeedback() {const p=app.pendingZones;if(!p||!app.map?.available)return;const sameMap=JSON.stringify(p.context)===JSON.stringify(mapContext(app.map));if(sameMap&&zoneSignature(p.target)===zoneSignature(app.map.zones)){$('command-result').textContent='Zones confirmed in the robot map.';toast('Zones confirmed by the robot');app.pendingZones=null;}else if(!sameMap||zoneSignature(p.base)!==zoneSignature(app.map.zones)||Date.now()-p.at>30000){$('command-result').textContent='Zone update not confirmed exactly. Review the robot map before retrying.';toast('Review the zones returned by the robot');app.pendingZones=null;}}
function renderZones() {
 const zones=app.map?.zones || [];const list=$('zone-list');list.classList.toggle('hidden',!zones.length);list.replaceChildren();
 zones.forEach((zone,index)=>{const chip=document.createElement('span');chip.className='zone-chip';const edit=document.createElement('button');edit.textContent=`Zone ${index+1}`;edit.setAttribute('aria-label',`Edit zone ${index+1}`);edit.disabled=!zoneEditingReady();edit.addEventListener('click',()=>editZone(index));chip.append(edit);const remove=document.createElement('button');remove.textContent='×';remove.setAttribute('aria-label',`Remove zone ${index+1}`);remove.disabled=!zoneEditingReady();remove.addEventListener('click',()=>changeZone('delete',index));chip.append(remove);chip.title=zone.can_clean?'No mopping':zone.can_mop?'No vacuuming':'No cleaning or mopping';list.append(chip);});
}
function visibleMapZones(map) {return (map.zones||[]).filter((zone,index)=>!(app.tool==='zone'&&app.selection&&app.editingZone===index));}
function zoneAtPoint(point,map) {for(let index=(map.zones||[]).length-1;index>=0;index--){const rect=zoneRectangle(map.zones[index]);if(rect&&insideSelection(point,rect))return index;}return null;}
function insideSelection(point,rect) {return Boolean(point&&rect&&point.x>=rect.x&&point.x<=rect.x+rect.width&&point.y>=rect.y&&point.y<=rect.y+rect.height);}
function movedSelection(rect,from,to,map) {return {...rect,x:Math.max(0,Math.min(Math.max(0,map.width-rect.width),Math.round(rect.x+to.x-from.x))),y:Math.max(0,Math.min(Math.max(0,map.height-rect.height),Math.round(rect.y+to.y-from.y)))};}
function gotoCoordinates(point,map,view) {
 if(!point||!map?.available||(view==='detailed'||view==='laser')||!Array.isArray(map.map_origin)||map.map_origin.length!==2||!map.map_origin.every(Number.isInteger))return null;
 const x=Math.round(point.x),y=Math.round(point.y);
 if(x<0||y<0||x>=map.width||y>=map.height)return null;
 return {x:x-map.map_origin[0],y:y-map.map_origin[1]};
}
function detailedCursorCoordinates(point,detail){
 if(!point||!detail?.available||!Array.isArray(detail.grid_origin)||detail.grid_origin.length!==2||!detail.grid_origin.every(Number.isFinite)||!Number.isInteger(detail.preview_step)||detail.preview_step<1||![point.x,point.y].every(Number.isFinite)||point.x<0||point.y<0||point.x>=detail.width||point.y>=detail.height)return null;
 return {x:Math.round(point.x*detail.preview_step-detail.grid_origin[0]),y:Math.round(point.y*detail.preview_step-detail.grid_origin[1])};
}
function showMapCoordinates(event) {
 const d=displayedMap(),blocked=app.status?.map_reset_pending||app.map?.reset_pending;
 const p=blocked?null:d.detailed?detailedCursorCoordinates(toGrid(event),d.map):d.geometry?null:gotoCoordinates(toGrid(event),app.map,app.mapView);
 $('map-coordinates').textContent=p?`X: ${p.x}  Y: ${p.y}`:'X: —  Y: —';
}
const canvas=$('map');canvas.addEventListener('pointerdown',event=>{
 app.hoverScreen={clientX:event.clientX,clientY:event.clientY};showMapCoordinates(event);
 const p=toGrid(event);if(!p||app.busy)return;
 if(app.tool==='zone'&&!insideSelection(p,app.selection)&&zoneEditingReady()){const index=zoneAtPoint(p,app.map);if(index!==null)editZone(index);}
 const mode=app.tool==='pan'?'pan':insideSelection(p,app.selection)?'move':'draw';
 if(mode==='draw'){app.selectionContext=mapContext(app.map);if(app.tool==='zone')app.zoneBase=structuredClone(app.map.zones||[]);}
 canvas.setPointerCapture(event.pointerId);app.drag={mode,rect:app.selection?{...app.selection}:null,point:p,screen:{x:event.clientX,y:event.clientY},pan:{x:app.view.panX,y:app.view.panY}};
});
canvas.addEventListener('pointermove',event=>{
 app.hoverScreen={clientX:event.clientX,clientY:event.clientY};showMapCoordinates(event);
 const p=toGrid(event);canvas.style.cursor=app.tool!=='pan'&&insideSelection(p,app.selection)?'move':'';if(!app.drag||!p)return;
 if(app.drag.mode==='pan'){app.view.panX=app.drag.pan.x+event.clientX-app.drag.screen.x;app.view.panY=app.drag.pan.y+event.clientY-app.drag.screen.y;}
 else if(app.drag.mode==='move'){app.selection=movedSelection(app.drag.rect,app.drag.point,p,app.map);canvas.style.cursor='move';renderSelection();}
 else {const a=app.drag.point,w=app.map.width,h=app.map.height;const x=Math.max(0,Math.min(w,Math.min(a.x,p.x))),y=Math.max(0,Math.min(h,Math.min(a.y,p.y)));app.selection={x:Math.round(x),y:Math.round(y),width:Math.round(Math.max(0,Math.min(w,Math.max(a.x,p.x))-x)),height:Math.round(Math.max(0,Math.min(h,Math.max(a.y,p.y))-y))};renderSelection();}drawMap();
});
for(const type of ['pointerup','pointercancel','lostpointercapture'])canvas.addEventListener(type,()=>app.drag=null);
canvas.addEventListener('pointerleave',()=>{app.hoverScreen=null;$('map-coordinates').textContent='X: —  Y: —';if(!app.drag)canvas.style.cursor='';});
canvas.addEventListener('wheel',event=>{event.preventDefault();app.view.zoom=Math.max(.65,Math.min(4,app.view.zoom*(event.deltaY<0?1.12:.9)));drawMap();},{passive:false});
canvas.addEventListener('keydown',event=>{if(event.key==='Escape'){app.selection=null;renderSelection();drawMap();}if(event.key==='+'||event.key==='='){app.view.zoom=Math.min(4,app.view.zoom*1.2);drawMap();}if(event.key==='-'){app.view.zoom=Math.max(.65,app.view.zoom/1.2);drawMap();}});
new ResizeObserver(drawMap).observe(canvas);
for(const key of ['x','y','width','height'])$(`rect-${key}`).addEventListener('input',()=>{if(!app.selection)return;const value=Number($(`rect-${key}`).value);if(!Number.isFinite(value))return;app.selection[key]=value;drawMap();});
$('apply-selection').addEventListener('click',async()=>{if(!app.selection)return;const rect={...app.selection};const ok=app.tool==='zone'?await changeZone(app.editingZone===null?'add':'update',app.editingZone):await sendCommand('area_clean',{rect,map_context:app.selectionContext});if(ok){app.selection=null;renderSelection();drawMap();}});
$('cancel-selection').addEventListener('click',()=>{app.selection=null;renderSelection();drawMap();});
$('map-view').addEventListener('change',()=>{$('map-coordinates').textContent='X: —  Y: —';app.mapView=$('map-view').value;laserSchedule();app.detailMap=null;app.detailError='';app.view={zoom:1,panX:0,panY:0};setTool('pan');renderStatus();renderZones();drawMap();if(app.mapView==='detailed')loadDetailedMap();});
$('reset-map').addEventListener('click',()=>{app.view={zoom:1,panX:0,panY:0};drawMap();});
$('zoom-in').addEventListener('click',()=>{app.view.zoom=Math.min(4,app.view.zoom*1.2);drawMap();});$('zoom-out').addEventListener('click',()=>{app.view.zoom=Math.max(.65,app.view.zoom/1.2);drawMap();});
$('primary-command').addEventListener('click',()=>sendCommand(['paused','Pause'].includes(app.status?.working_status)?'resume':'clean',{fan_power:Number($('clean-fan-power').value)}));
$('pause-command').addEventListener('click',()=>sendCommand('pause'));$('dock-command').addEventListener('click',()=>sendCommand('dock'));
$('go-schedules').addEventListener('click',()=>changePage('schedules'));$('go-activity').addEventListener('click',()=>changePage('activity'));
document.querySelectorAll('[data-page]').forEach(el=>el.addEventListener('click',()=>changePage(el.dataset.page)));
document.querySelectorAll('[data-tool]').forEach(el=>el.addEventListener('click',()=>setTool(el.dataset.tool)));
$('add-schedule').addEventListener('click',()=>openSchedule());for(const id of ['close-schedule','cancel-schedule'])$(id).addEventListener('click',()=>$('schedule-dialog').close());
$('weekdays-preset').addEventListener('click',()=>{app.days=new Set([1,2,3,4,5]);renderDays();});$('everyday-preset').addEventListener('click',()=>{app.days=new Set([0,1,2,3,4,5,6]);renderDays();});
$('copy-cron').addEventListener('click',async()=>{try{await navigator.clipboard.writeText($('cron-preview').textContent);toast('Crontab copied');}catch{toast('Select the crontab text to copy it.');}});
$('access-button').addEventListener('click',()=>$('access-dialog').showModal());$('close-access').addEventListener('click',()=>$('access-dialog').close());
$('clear-access').addEventListener('click',()=>{app.key='';laserSchedule();sessionStorage.removeItem('zigma-key');$('access-key').value='';$('access-dialog').close();refresh();});
$('access-form').addEventListener('submit',async event=>{event.preventDefault();app.key=$('access-key').value.trim();try{await api('/api/status');sessionStorage.setItem('zigma-key',app.key);laserSchedule();$('access-error').classList.add('hidden');$('access-dialog').close();await refresh();await loadSchedules();await loadEvents();}catch(error){$('access-error').textContent=error.message;$('access-error').classList.remove('hidden');}});
async function poll() {const started=Date.now();if(!document.hidden){await refresh();if(Date.now()-app.lastEvents>7000)await loadEvents().catch(()=>{});}setTimeout(poll,Math.max(250,2500-(Date.now()-started)));}
document.addEventListener('visibilitychange',()=>{laserSchedule();if(!document.hidden)refresh();});
$('laser-overlay').addEventListener('click',()=>{laserView.overlay=!laserView.overlay;drawMap();if(laserView.overlay)refresh();});
$('laser-rotate').addEventListener('click',()=>{laserView.mode=laserView.mode==='fixed'?'robot':'fixed';drawMap();});
(async()=>{await refresh();await loadSchedules().catch(()=>{});await loadEvents().catch(()=>{});setTimeout(poll,2500);})();

function renderTimezones(){
 const select=$('settings-timezone'),selected=select.value||app.settings?.timezone;select.replaceChildren();
 const zones=[...new Set([app.settings.timezone,...(app.settings.timezones||[])])].sort();let group=null,region='';
 for(const zone of zones){const next=zone.includes('/')?zone.split('/')[0]:'Other';if(next!==region){group=document.createElement('optgroup');group.label=next;select.append(group);region=next;}const option=document.createElement('option');option.value=zone;option.textContent=zone.replaceAll('_',' ');group.append(option);}
 select.value=selected;
}
function renderOptionValues(s){
 for(const [key,id] of [['over_ridge','settings-over-ridge'],['continue_clean','settings-continue-clean']]){const input=$(id);input.disabled=!s.options_editable||typeof s[key]!=='boolean';input.checked=s[key]===true;}
 $('settings-volume').disabled=!s.options_editable||!Number.isFinite(s.volume);$('audio-save').disabled=$('settings-volume').disabled;
 if(Number.isFinite(s.volume))$('settings-volume').value=s.volume;$('volume-value').textContent=Number.isFinite(s.volume)?`${s.volume}%`:'Unavailable';
 $('options-status').textContent=typeof s.over_ridge==='boolean'&&typeof s.continue_clean==='boolean'?'':'Waiting for robot settings.';
}
function renderNtp(s){$('ntp-status').textContent=`${s.ntp_enabled?'Automatic time':'Automatic sync not confirmed'} · ${{requested:'Sync requested…',running:'Syncing…',success:'Synced',failed:'Sync failed',idle:'Ready'}[s.ntp_sync_status]||'Ready'}`;$('ntp-sync').disabled=!s.editable||['requested','running'].includes(s.ntp_sync_status);}
async function loadSettings(){
 const s=await api('/api/settings');app.settings=s;$('settings-name').value=s.robot_name;$('settings-timezone').replaceChildren();renderTimezones();$('settings-timezone').value=s.timezone;$('settings-ntp-server').value=s.ntp_server||'';$('settings-area-unit').value=s.area_unit||'m2';
 $('settings-mode').textContent=s.simulated?'SIMULATED':'LOCAL';$('settings-current-time').textContent=`${s.local_datetime.replace('T',' ')} · ${s.timezone}`;
 $('general-save').disabled=!s.editable||!s.options_editable||typeof s.over_ridge!=='boolean'||typeof s.continue_clean!=='boolean';
 $('ssh-submit').disabled=!s.ssh_available;renderOptionValues(s);renderNtp(s);await Promise.all([loadVoices(),loadTelegram()]);
}
async function saveSetting(action,value,button){
 button.disabled=true;const result=$('settings-result');result.classList.add('hidden');
 try{
  const s=await api('/api/settings',{method:'PUT',body:JSON.stringify({action,value})});app.settings=s;
  const option=['volume','continue_clean','over_ridge'].includes(action);
  result.textContent=s.simulated?'Saved in demo.':option?'Sent. Waiting for the robot to confirm.':action==='ntp_sync'?'Sync requested.':'Saved.';result.classList.remove('hidden');
  await refresh();renderNtp(s);
  if(option&&!s.simulated){let confirmed=false;for(let i=0;i<5;i++){await new Promise(resolve=>setTimeout(resolve,1300));const actual=await api('/api/settings');if(String(actual[action])===String(value)){confirmed=true;app.settings=actual;renderOptionValues(actual);break;}}result.textContent=confirmed?'Robot setting confirmed.':'Sent, but not confirmed. Refresh before retrying.';}
  if(action==='ntp_sync'&&!s.simulated){for(let i=0;i<15;i++){await new Promise(resolve=>setTimeout(resolve,2000));const actual=await api('/api/settings');renderNtp(actual);if(['success','failed'].includes(actual.ntp_sync_status)){result.textContent=actual.ntp_sync_status==='success'?'Clock synchronized.':'Synchronization failed.';break;}}}
 }catch(error){result.textContent=error.message;result.classList.remove('hidden');}finally{button.disabled=false;}
}
// Each card submits its complete settings snapshot; checkboxes are drafts until Save.
async function saveSettingsGroup(formId,resultId,values,afterSave){
 const form=$(formId),result=$(resultId),controls=[...form.querySelectorAll('input,select,button')],disabled=controls.map(c=>c.disabled);
 const completed=[];controls.forEach(c=>c.disabled=true);result.textContent='Saving…';
 try{
  let latest;
  for(const [action,value] of values){latest=await api('/api/settings',{method:'PUT',body:JSON.stringify({action,value})});completed.push(action);}
  if(afterSave)await afterSave();
  app.settings=latest;await refresh();
  const options=values.filter(([key])=>['volume','over_ridge','continue_clean'].includes(key));
  let confirmed=latest.simulated||!options.length;
  for(let i=0;!confirmed&&i<5;i++){await new Promise(resolve=>setTimeout(resolve,1300));const actual=await api('/api/settings');confirmed=options.every(([key,value])=>String(actual[key])===value);}
  result.textContent=confirmed?'Saved.':'Saved; robot options await confirmation.';
 }catch(error){result.textContent=(completed.length?'Some settings were saved. ':'')+error.message+' Check before retrying.';}
 finally{controls.forEach((c,i)=>c.disabled=disabled[i]);}
}
$('general-form').addEventListener('submit',e=>{
 e.preventDefault();
 const values=[['name',$('settings-name').value.trim()],['timezone',$('settings-timezone').value],['ntp_server',$('settings-ntp-server').value.trim()],['area_unit',$('settings-area-unit').value],['over_ridge',String($('settings-over-ridge').checked)],['continue_clean',String($('settings-continue-clean').checked)]];
 saveSettingsGroup('general-form','general-result',values);
});
$('settings-volume').addEventListener('input',()=>{$('volume-value').textContent=`${$('settings-volume').value}%`;});
const pendingVoices=new Map();let audioSaving=false;
$('audio-form').addEventListener('submit',async e=>{
 e.preventDefault();if(audioSaving)return;audioSaving=true;
 const pending=[...pendingVoices.entries()];
 await saveSettingsGroup('audio-form','audio-result',[['volume',$('settings-volume').value]],async()=>{
  for(const [name,change] of pending){
   if(change.restore)await api('/api/voices/restore',{method:'POST',body:JSON.stringify({name})});
   else {const data=await new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>resolve(String(r.result).split(',')[1]);r.onerror=()=>reject(new Error('Could not read '+change.file.name));r.readAsDataURL(change.file);});await api('/api/voices',{method:'PUT',body:JSON.stringify({name,data})});}
   pendingVoices.delete(name);
  }
 });
 audioSaving=false;await loadVoices().catch(showError);
});
$('ntp-sync').addEventListener('click',e=>saveSetting('ntp_sync','',e.currentTarget));
let voiceObjectURL=null;
async function loadVoices(){app.voices=await api('/api/voices');renderVoices();}
function renderVoices(){
 const list=$('voices-list');list.replaceChildren();if(!app.voices?.available){list.textContent='Voice files unavailable.';return;}
 const query=$('voice-search').value.toLowerCase();
 for(const item of [...app.voices.items].sort((a,b)=>a.name.localeCompare(b.name))){if(!item.name.toLowerCase().includes(query))continue;
  const row=document.createElement('div');row.className='voice-row';row.setAttribute('role','listitem');const title=document.createElement('strong');title.textContent=item.name;row.append(title);const actions=document.createElement('div');actions.className='voice-actions';
  const add=(text,fn)=>{const b=document.createElement('button');b.type='button';b.className='quiet-button';b.textContent=text;b.addEventListener('click',async()=>{b.disabled=true;try{await fn();}catch(error){showError(error);}finally{b.disabled=false;}});actions.append(b);return b;};
  add('Play',async()=>{const r=await fetch(`/api/voices/${encodeURIComponent(item.name)}`,{headers:{Authorization:`Bearer ${app.key}`}});if(!r.ok)throw new Error('Voice could not be loaded.');if(voiceObjectURL)URL.revokeObjectURL(voiceObjectURL);voiceObjectURL=URL.createObjectURL(await r.blob());const player=$('voice-player');player.src=voiceObjectURL;player.classList.remove('hidden');await player.play();});
  const upload=document.createElement('input');upload.type='file';upload.accept='.mp3,audio/mpeg';upload.hidden=true;row.append(upload);
  add('Replace',()=>upload.click());
  upload.addEventListener('change',()=>{const file=upload.files[0];if(!file)return;if(file.size>app.voices.max_bytes){showError(new Error('Maximum MP3 size: 512 KB.'));return;}pendingVoices.set(item.name,{file});renderVoices();});
  const restore=add('Restore original',()=>{pendingVoices.set(item.name,{restore:true});renderVoices();});restore.disabled=!item.original_available;
  const pending=pendingVoices.get(item.name);
  if(pending){const note=document.createElement('small');note.className='voice-pending';note.textContent=pending.restore?'Restore original on Save':`On Save: ${pending.file.name}`;row.append(note);add('Undo',()=>{pendingVoices.delete(item.name);renderVoices();});}
  if(audioSaving)actions.querySelectorAll('button').forEach(b=>b.disabled=true);
  row.append(actions);list.append(row);
 }
}
$('voice-search').addEventListener('input',renderVoices);
$('ssh-form').addEventListener('submit',async e=>{e.preventDefault();const file=$('ssh-public-key').files[0];if(!file)return;if(file.size>8192){$('ssh-result').textContent='Choose a public key file up to 8 KB.';return;}e.submitter.disabled=true;try{const result=await api('/api/ssh-keys',{method:'POST',body:JSON.stringify({public_key:await file.text()})});$('ssh-result').textContent=result.added?'Public key added. Existing keys kept.':'This key is already installed.';$('ssh-form').reset();}catch(error){$('ssh-result').textContent=error.message;}finally{e.submitter.disabled=false;}});

$('password-form').addEventListener('submit',async event=>{event.preventDefault();const next=$('password-new').value;if(next!==$('password-repeat').value){toast('New passwords do not match');return;}const button=event.submitter;button.disabled=true;try{await api('/api/access-key',{method:'PUT',body:JSON.stringify({current_key:$('password-current').value,new_key:next})});app.key=next;sessionStorage.setItem('zigma-key',next);$('password-form').reset();toast('Access password changed. Other tabs must sign in again.');await loadEvents();}catch(error){showError(error);}finally{button.disabled=false;}});

// Saved maps and master copies never replace the live control canvas.
const savedPreviewCache=new Map();
let savedCatalogGeneration=0,savedMapMutation=false;
function savedPreviewPixels(preview) {
 const w=preview.preview_width,h=preview.preview_height,hex=preview.pixels;
 if(preview.orientation!=='row0_top_v2'||preview.encoding!=='occupancy-u8-hex'||!Number.isInteger(w)||!Number.isInteger(h)||w<1||h<1||w>400||h>400||typeof hex!=='string'||hex.length!==w*h*2||!/^[0-9a-f]+$/.test(hex))throw new Error('Invalid or outdated saved-map preview. Reload the page.');
 const rgba=new Uint8ClampedArray(w*h*4);
 for(let i=0;i<w*h;i++){const value=parseInt(hex.slice(i*2,i*2+2),16),offset=i*4;const colour=value===0?[235,238,233]:[value,value,value];rgba.set([...colour,255],offset);}
 return rgba;
}
async function showSavedPreview(item,card,generation) {
 const button=card.querySelector('.saved-preview-button'),canvas=card.querySelector('canvas'),note=card.querySelector('.saved-preview-note');
 button.disabled=true;note.textContent='Reading saved map…';
 try {
  const key=previewKey(item);let preview=savedPreviewCache.get(key);
  if(!preview){preview=await api(item.uid?`/api/map-library/${item.uid}/preview`:`/api/maps/${item.id}/preview`);if(preview.id!==item.id||preview.revision!==item.revision||(item.uid&&preview.uid!==item.uid))throw new Error('The saved map changed. Refresh the catalog.');savedPreviewPixels(preview);savedPreviewCache.set(key,preview);}
  if(generation!==savedCatalogGeneration)return;
  canvas.width=preview.preview_width;canvas.height=preview.preview_height;canvas.getContext('2d').putImageData(new ImageData(savedPreviewPixels(preview),canvas.width,canvas.height),0,0);canvas.classList.remove('hidden');
  button.textContent='Preview loaded';note.textContent=item.uid?'Protected copy · unchanged by cleaning':'Saved floor geometry';
 }catch(error){if(generation===savedCatalogGeneration){note.textContent=error.message;button.textContent='Load preview';button.disabled=false;}}
}
function previewKey(item){return `${item.uid?'master:'+item.uid:'robot:'+item.id}:${item.revision}:row0_top_v2`;}
function newCopyId(){const bytes=new Uint8Array(16);crypto.getRandomValues(bytes);return Array.from(bytes,n=>n.toString(16).padStart(2,'0')).join('');}
function savedMapClearReady(item,catalog,status){return Boolean(catalog.clear_available&&item.id===catalog.native_active_id&&savedMapDeleteReady({...item,id:'other'},catalog,status));}
function savedMapDeleteReady(item,catalog,status){return Boolean(catalog.delete_available&&catalog.native_active_id&&item.id!==catalog.native_active_id&&status&&(status.fresh||(status.cache_available&&status.local_link_active))&&status.error==='NoError'&&['PileCharging','DirCharging','Standby','Hibernating','charging','idle'].includes(status.working_status));}
function confirmMapAction(title,detail,label,requiredText='',offerBackup=false){
 return new Promise(resolve=>{const dialog=$('map-action-dialog');$('map-action-title').textContent=title;$('map-action-detail').textContent=detail;$('map-action-confirm').textContent=label;$('map-action-backup-field').classList.toggle('hidden',!offerBackup);$('map-action-backup').checked=true;const typed=$('map-action-typed');typed.value='';typed.required=Boolean(requiredText);typed.pattern=requiredText;$('map-action-word').textContent=requiredText;$('map-action-typed-field').classList.toggle('hidden',!requiredText);dialog.returnValue='cancel';dialog.addEventListener('close',()=>resolve(dialog.returnValue==='confirm'),{once:true});dialog.showModal();});
}
async function mapMutation(button,work){
 if(savedMapMutation)return;savedMapMutation=true;button.disabled=true;const status=$('saved-maps-action-status');status.textContent='';
 try{status.textContent=await work();await loadSavedMaps();}catch(error){status.textContent=error.message;}finally{savedMapMutation=false;button.disabled=false;}
}
function mapCard(item,generation){
 const card=document.createElement('article');card.className='saved-map-card';
 card.innerHTML='<div class="saved-map-heading"><h2></h2><span class="tiny-badge hidden"></span></div><div class="saved-map-preview"><canvas class="hidden" role="img"></canvas><button class="quiet-button saved-preview-button">Load preview</button><p class="saved-preview-note">Preview loads on demand</p></div><p class="saved-map-details"></p><div class="saved-map-actions"></div>';
 card.querySelector('h2').textContent=item.name||`Map ${item.id}`;card.querySelector('canvas').setAttribute('aria-label',`Saved floor geometry for ${item.name||'map '+item.id}`);
 const at=item.uid?item.created_at:item.saved_at;card.querySelector('.saved-map-details').textContent=`${item.uid?'Copied':'Saved'} ${dateFormat.format(new Date(at*1000))} · Map ${item.id} · ${item.width} × ${item.height} cells`;
 card.querySelector('.saved-preview-button').addEventListener('click',()=>showSavedPreview(item,card,generation));return card;
}
function mapButton(parent,label,primary=false){const button=document.createElement('button');button.type='button';button.className=primary?'primary-button small':'quiet-button';button.textContent=label;parent.append(button);return button;}
function mapNameForm(card,item,save){
 const form=document.createElement('form');form.className='saved-map-name';form.innerHTML='<label class="field">Name in Zigma Free<input maxlength="48" required autocomplete="off"></label><button type="submit" class="quiet-button">Save name</button><p class="saved-name-result" role="status"></p>';
 const input=form.querySelector('input');input.value=item.name||'';input.placeholder=`Map ${item.id}`;input.setAttribute('aria-label',`Name for ${item.uid?'master copy':'map'} ${item.name||item.id}`);
 form.addEventListener('submit',event=>{event.preventDefault();mapMutation(form.querySelector('button'),()=>save(input.value.trim()));});card.querySelector('.saved-map-actions').append(form);return input;
}
function renderRobotMap(item,catalog,list,generation){
 const card=mapCard(item,generation),actions=card.querySelector('.saved-map-actions'),badge=card.querySelector('.tiny-badge');
 if(item.id===(catalog.native_active_id||catalog.last_observed_active_id)){badge.classList.remove('hidden');badge.textContent=catalog.native_active_id?'ACTIVE · ROBOT CACHE':'LAST OBSERVED ACTIVE';}
 const input=mapNameForm(card,item,async name=>{await api('/api/maps/name',{method:'PUT',body:JSON.stringify({id:item.id,revision:item.revision,previous_name:item.name,name})});return 'Name saved in Zigma Free.';});
 const save=mapButton(actions,'Save master copy',true);save.disabled=!catalog.library_available;
 save.addEventListener('click',()=>mapMutation(save,async()=>{item.saveUid ||= newCopyId();await api('/api/map-library/save',{method:'POST',body:JSON.stringify({id:item.id,revision:item.revision,uid:item.saveUid,name:input.value.trim()||item.name||`Map ${item.id}`})});return 'Master copy saved separately on the robot. Cleaning will not overwrite it.';}));
 if(item.id===catalog.native_active_id){
  const clear=mapButton(actions,'Clear current map · start fresh');clear.classList.add('destructive-map-button');clear.disabled=!savedMapClearReady(item,catalog,app.status);
  clear.addEventListener('click',async()=>{if(!await confirmMapAction('Clear the current robot map?',`${item.name||'Map '+item.id} will be discarded from navigation. Choose whether to save a protected backup first; existing master copies are kept. Plan to map your home again on the next full clean, and review no-go zones afterward. No cleaning starts automatically.`,'Clear map','CLEAR',true))return;
   const backup=$('map-action-backup').checked;
   mapMutation(clear,async()=>{item.clearUid ||= newCopyId();await api('/api/maps/clear',{method:'POST',body:JSON.stringify({id:item.id,revision:item.revision,active_id:catalog.native_active_id,uid:item.clearUid,name:`Before clear · ${item.name||'Map '+item.id}`.slice(0,48),confirm:true,backup,confirmation_text:'CLEAR'})});app.mapTag=null;app.map={available:false,reset_pending:true,warning:'Clear request sent. Waiting for a new map.'};app.activeArea=null;app.selection=null;app.pendingZones=null;saveArea();setTool('pan');renderZones();drawMap();return `Clear request dispatched. ${backup?'Backup kept.':'No new backup saved.'} The old map is hidden while waiting for a new robot map; start a full clean yourself when ready.`;});
  });
 }
 if(item.id!==catalog.native_active_id){
 const remove=mapButton(actions,'Delete from robot');remove.disabled=!savedMapDeleteReady(item,catalog,app.status);remove.title=item.id===catalog.native_active_id?'The active map cannot be deleted':'Requires an idle robot and confirmed map identity';
 remove.addEventListener('click',async()=>{if(!await confirmMapAction('Delete this robot map?',`${item.name||'Map '+item.id} will be removed from the robot catalog. Choose whether to save a protected backup first. Existing protected copies are kept.`,'Delete map','',true))return;
  const backup=$('map-action-backup').checked;
  mapMutation(remove,async()=>{item.deleteUid ||= newCopyId();await api('/api/maps/delete',{method:'POST',body:JSON.stringify({id:item.id,revision:item.revision,active_id:catalog.native_active_id,uid:item.deleteUid,name:`Backup · ${item.name||'Map '+item.id}`.slice(0,48),confirm:true,backup})});return `Deletion dispatched. ${backup?'Backup kept in the master library.':'No new backup saved.'} Refresh to confirm removal; do not repeat the order automatically.`;});
 });
 }
 list.append(card);if(catalog.items.length===1||savedPreviewCache.has(previewKey(item)))showSavedPreview(item,card,generation);
}
function renderMasterMap(item,list,generation,catalog){
 const card=mapCard(item,generation),actions=card.querySelector('.saved-map-actions'),badge=card.querySelector('.tiny-badge');badge.classList.remove('hidden');badge.textContent=item.verified===false?'VERIFY COPY':item.deleted?'TRASH':item.kind==='reset_backup'?'BEFORE CLEAR':item.kind==='deletion_backup'?'DELETION BACKUP':'MASTER COPY';
 if(!item.deleted){
  const activate=mapButton(actions,'Restore master to robot',true);
  activate.disabled=item.verified===false||!catalog?.restore_available||!app.status||( !app.status.fresh&&!(app.status.cache_available&&app.status.local_link_active))||app.status.error!=='NoError';
  activate.title='Requires an idle robot. The master remains protected; working maps are backed up before replacement.';
  activate.addEventListener('click',async()=>{
   if(!await confirmMapAction('Restore this master to the robot?',`${item.name} (map ${item.id}) will become the robot’s working map, including the restrictions stored in that copy. Working maps that may be replaced will be backed up first. Use this copy only in its original floor and layout. The robot will not start cleaning. Type RESTORE to continue.`,'Restore master','RESTORE'))return;
   mapMutation(activate,async()=>{
    const result=await api('/api/map-library/activate',{method:'POST',body:JSON.stringify({uid:item.uid,version:item.version,active_id:catalog.restore_context.id,operation_uid:newCopyId(),active_backup_uid:newCopyId(),target_backup_uid:newCopyId(),confirm:true,confirmation_text:'RESTORE'})});
    if(result.accepted){app.mapTag=null;app.map={available:false,reset_pending:true,warning:'Master restore sent. Waiting for native confirmation and a new map.'};app.activeArea=null;app.selection=null;app.pendingZones=null;saveArea();setTool('pan');renderZones();drawMap();}
    return 'Restoration dispatched. No cleaning started. Refresh maps to check native confirmation; do not repeat the order automatically.';
   });
  });
  mapNameForm(card,item,async name=>{await api('/api/map-library/rename',{method:'POST',body:JSON.stringify({uid:item.uid,version:item.version,name})});return 'Master copy renamed.';});
  const remove=mapButton(actions,'Move to Trash');remove.addEventListener('click',async()=>{if(!await confirmMapAction('Move this copy to Trash?',`${item.name} will leave the master library. You can recover it from Trash. The robot map is not affected.`,'Move to Trash'))return;
   mapMutation(remove,async()=>{await api('/api/map-library/trash',{method:'POST',body:JSON.stringify({uid:item.uid,version:item.version,confirm:true})});return 'Master copy moved to Trash.';});
  });
 }else{
  const restore=mapButton(actions,'Recover copy',true);restore.addEventListener('click',()=>mapMutation(restore,async()=>{await api('/api/map-library/restore',{method:'POST',body:JSON.stringify({uid:item.uid,version:item.version})});return 'Copy recovered to the master library. The active robot map was not changed.';}));
  const purge=mapButton(actions,'Delete permanently');purge.addEventListener('click',async()=>{if(!await confirmMapAction('Permanently delete this copy?',`${item.name} will be permanently removed to free storage. This cannot be undone. Other master copies and the robot map are not affected.`,'Delete permanently'))return;
   mapMutation(purge,async()=>{await api('/api/map-library/purge',{method:'POST',body:JSON.stringify({uid:item.uid,version:item.version,confirm:true})});return 'Trashed copy permanently deleted.';});
  });
 }
 list.append(card);if(savedPreviewCache.has(previewKey(item)))showSavedPreview(item,card,generation);
}
async function loadSavedMaps() {
 const generation=++savedCatalogGeneration,status=$('saved-maps-status'),list=$('saved-maps-list'),masters=$('master-maps-list'),trash=$('master-trash-list'),libraryStatus=$('master-maps-status');
 status.textContent='Reading robot maps…';libraryStatus.textContent='Reading master copies…';$('refresh-saved-maps').disabled=true;list.replaceChildren();masters.replaceChildren();trash.replaceChildren();
 try{
  const [robotResult,libraryResult]=await Promise.allSettled([api('/api/maps'),api('/api/map-library')]);if(generation!==savedCatalogGeneration)return;
  const all=[];
  if(robotResult.status==='fulfilled'){const catalog=robotResult.value;all.push(...catalog.items);status.textContent=catalog.restoration?(catalog.restoration.phase==='new_map_observed'?'Master restored: native success and new map confirmed. No cleaning started.':catalog.restoration.phase==='restore_failed'?'Native restore failed. Old geometry remains hidden; inspect the robot before retrying.':'Master restoration pending. Old geometry is hidden. Refresh to check confirmation.') :catalog.reset_pending?'Clear request recorded. Waiting for a new robot map; old geometry is hidden.':`${catalog.items.length} saved robot map${catalog.items.length===1?'':'s'}${catalog.unreadable_count?` · ${catalog.unreadable_count} unreadable file(s)`:''}`;for(const item of catalog.items)renderRobotMap(item,catalog,list,generation);}
  else status.textContent=robotResult.reason.message;
  if(libraryResult.status==='fulfilled'){const library=libraryResult.value;all.push(...library.items);const kept=library.items.filter(item=>!item.deleted),removed=library.items.filter(item=>item.deleted);kept.sort((a,b)=>b.created_at-a.created_at);
   libraryStatus.textContent=`${kept.length} protected cop${kept.length===1?'y':'ies'} · ${Math.ceil(library.used_bytes/1024)} KB of ${library.quota_bytes/1024/1024} MB · Up to ${library.max_copies} copies including Trash${library.unreadable_count?' · Some copies could not be verified':''}${library.orphan_count?' · An incomplete copy needs inspection':''}`;
   for(const item of kept)renderMasterMap(item,masters,generation,robotResult.status==='fulfilled'?robotResult.value:null);for(const item of removed)renderMasterMap(item,trash,generation,null);
   if(!kept.length)masters.textContent='When the map looks right, choose Save master copy above. You can keep several versions or floors.';
   if(!removed.length)trash.textContent='Trash is empty.';$('master-trash-count').textContent=String(removed.length);
  }else libraryStatus.textContent=libraryResult.reason.message;
  const keys=new Set(all.map(previewKey));for(const key of savedPreviewCache.keys())if(!keys.has(key))savedPreviewCache.delete(key);
 }finally{if(generation===savedCatalogGeneration)$('refresh-saved-maps').disabled=false;}
}
$('refresh-saved-maps').addEventListener('click',()=>loadSavedMaps().catch(showError));

function renderTelegram(s, fields=false) {
 if(fields){for(const key of ['finished','errors','map','activity','battery','system','commands','docking'])$(`telegram-${key}`).checked=!!s[key];$('telegram-token').value='';}
 $('telegram-token').placeholder=s.token_configured?'Saved token · leave blank to keep':'Paste token from BotFather';
 $('telegram-receiver').textContent=s.commands?`Commands: ${s.receiver}`:'';
 $('telegram-chat').textContent=s.linked?`Chat: ${s.chat}`:'No chat linked';
 $('telegram-result').textContent=s.available?s.status:'Telegram transport unavailable';
 $('telegram-save').disabled=!s.available;
 $('telegram-link').disabled=!s.available||!s.token_configured||s.busy;
 $('telegram-test').disabled=!s.available||!s.linked||s.busy||s.pending>0||!!s.pairing_code;
 $('telegram-pairing').classList.toggle('hidden',!s.pairing_code);
 $('telegram-command').value=s.pairing_code?`/link ${s.pairing_code}`:'';
}
async function loadTelegram(){renderTelegram(await api('/api/telegram'),true);}
$('telegram-form').addEventListener('submit',async e=>{
 e.preventDefault();$('telegram-save').disabled=true;
 const values={token:$('telegram-token').value.trim()};for(const key of ['finished','errors','map','activity','battery','system','commands','docking'])values[key]=$(`telegram-${key}`).checked;
 try {renderTelegram(await api('/api/telegram',{method:'PUT',body:JSON.stringify(values)}),true);}catch(error){$('telegram-result').textContent=error.message;}finally{$('telegram-save').disabled=false;}
});
for(const action of ['link','test'])$(`telegram-${action}`).addEventListener('click',async()=>{
 $(`telegram-${action}`).disabled=true;
 try{renderTelegram(await api(`/api/telegram/${action}`,{method:'POST',body:'{}'}));}catch(error){$('telegram-result').textContent=error.message;}
});
$('telegram-command').addEventListener('click',e=>e.target.select());
let telegramPolling=false;
setInterval(async()=>{
 if(app.page!=='settings'||document.hidden||!app.connected||telegramPolling)return;
 telegramPolling=true;try{renderTelegram(await api('/api/telegram'));}catch{}finally{telegramPolling=false;}
},3000);

$('telegram-select-commands').addEventListener('click',()=>{const field=$('telegram-botfather');field.focus();field.select();});

async function loadSoftware(){
 const status=$('software-status');status.textContent='Checking installed software…';
 try{const data=await api('/api/about');const rows=[];
  for(const item of [...(data.items||[])].sort((a,b)=>a.name.localeCompare(b.name,'en',{sensitivity:'base',numeric:true}))){const row=document.createElement('tr'),name=document.createElement('td');
   const label=document.createElement('strong');label.textContent=item.name;const path=document.createElement('code');path.textContent=item.path;name.append(label,path);row.append(name);
   const date=item.compiled_at?new Date(item.compiled_at).toLocaleString('en-GB',{timeZone:robotTimezone}):item.prepared_at?'Packaged '+new Date(item.prepared_at).toLocaleString('en-GB',{timeZone:robotTimezone}):'Not recorded (legacy build)';
   for(const [heading,value] of [['Version',item.version||'Unknown'],['Build date',date],['Status',item.status]]){const cell=document.createElement('td');cell.dataset.label=heading;cell.textContent=value;row.append(cell);}rows.push(row);
  }$('software-list').replaceChildren(...rows);status.textContent=data.available?'Installed files checked against the release manifest. Dates use '+robotTimezone+'.':'No release manifest installed.';
 }catch(error){status.textContent='Software versions could not be checked.';throw error;}
}


// Authenticated download: access key is never placed in a URL.
let norController=null,norUrls=[];
$('nor-cancel').addEventListener('click',()=>norController?.abort());
$('nor-start').addEventListener('click',async()=>{
  norUrls.forEach(url=>URL.revokeObjectURL(url));norUrls=[];$('nor-files').replaceChildren();
  norController=new AbortController();const timer=setTimeout(()=>norController.abort(),600000);
  $('nor-start').disabled=true;$('nor-cancel').hidden=false;$('nor-status').textContent='Opening NOR read-only. Keep the robot idle…';
  try{
    const response=await fetch('/api/backup/nor',{headers:{Authorization:`Bearer ${app.key}`},signal:norController.signal});
    if(!response.ok){const error=await response.json();throw new Error(error.error||'Backup unavailable.');}
    if(!response.body)throw new Error('Streaming downloads are not supported by this browser.');
    const result=await readNorBackup(response.body.getReader(),(phase,done,total)=>{$('nor-status').textContent=`${phase}: ${Math.floor(done/1048576)} / ${total/1048576} MiB`;});
    const name='zigma-nor-'+new Date().toISOString().replace(/[:.]/g,'-');
    for(const [suffix,body,type] of [['.bin',result.data,'application/octet-stream'],['.json',JSON.stringify(result.manifest,null,2),'application/json']]){
      const url=URL.createObjectURL(new Blob([body],{type}));norUrls.push(url);const a=document.createElement('a');a.href=url;a.download=name+suffix;a.textContent=suffix==='.bin'?'Save verified NOR (32 MiB)':'Save verification manifest';a.className='primary-button fit';$('nor-files').append(a,document.createTextNode(' '));
    }
    $('nor-status').textContent='Verified: both complete reads match. Save the backup and its manifest below. SHA256: '+result.manifest.first_sha256;
  }catch(error){norController.abort();$('nor-status').textContent=error.name==='AbortError'?'Backup cancelled or timed out. Nothing was saved.':error.message;}
  finally{clearTimeout(timer);norController=null;$('nor-start').disabled=false;$('nor-cancel').hidden=true;}
});
