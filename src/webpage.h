// webpage.h
// The whole settings page, kept in flash memory rather than RAM.
//
// The page does not have any values filled in by the clock before it is sent.
// Instead the page asks the clock for its settings and its status after it
// loads, using the small JSON endpoints in webui.cpp. That keeps this file
// plain text and avoids the percent sign substitution the web server library
// would otherwise try to do inside the stylesheet.
//
// The sections are ordinary HTML detail blocks, so the opening and closing is
// the browser's own behaviour and needs no script to work.

#pragma once
#include <Arduino.h>

static const char SETTINGS_PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>My CYD Clock</title>
<style>
:root{--bg:#16181d;--card:#22252c;--line:#343842;--ink:#e8eaee;--dim:#9aa0ab;--accent:#4CAF50}
*{box-sizing:border-box}
body{margin:0;padding:0 16px 28px;font-family:system-ui,Segoe UI,Arial,sans-serif;background:var(--bg);color:var(--ink)}
.wrap{max-width:640px;margin:0 auto}
h1{font-size:19px;margin:0}
.sub{color:var(--dim);font-size:12px;margin:2px 0 0}
/* The title and the buttons stay put while the rest of the page scrolls. */
.topbar{position:sticky;top:0;z-index:30;background:var(--bg);
border-bottom:1px solid var(--line);margin:0 -16px 16px;padding:10px 16px;
box-shadow:0 3px 10px rgba(0,0,0,.5)}
.topbar .inner{max-width:640px;margin:0 auto;display:flex;align-items:center;gap:10px}
.topbar .grow{flex:1;min-width:0}
.topbar button{padding:10px 18px;white-space:nowrap}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:12px}
.card h2{font-size:15px;margin:0 0 12px;color:var(--accent);text-transform:uppercase;letter-spacing:.06em}
details.card{padding:0}
details.card>summary{list-style:none;cursor:pointer;padding:15px 16px;font-size:15px;
color:var(--accent);text-transform:uppercase;letter-spacing:.06em;font-weight:600;
display:flex;align-items:center}
details.card>summary::-webkit-details-marker{display:none}
details.card>summary::after{content:'';margin-left:auto;width:8px;height:8px;
border-right:2px solid var(--dim);border-bottom:2px solid var(--dim);
transform:rotate(45deg) translate(-2px,-2px);transition:transform .15s}
details.card[open]>summary::after{transform:rotate(-135deg) translate(-2px,-2px)}
details.card>.body{padding:0 16px 16px}
.row{display:flex;flex-wrap:wrap;gap:12px}
.row>div{flex:1 1 180px;min-width:0}
label{display:block;font-size:13px;color:var(--dim);margin:10px 0 5px}
input[type=text],input[type=password],input[type=number],input[type=time],select{
width:100%;padding:9px 10px;background:#1a1c21;border:1px solid var(--line);
border-radius:6px;color:var(--ink);font-size:14px}
input[type=color]{width:52px;height:36px;padding:2px;background:#1a1c21;border:1px solid var(--line);border-radius:6px}
input[type=range]{width:100%;accent-color:var(--accent)}
.rangewrap{display:flex;align-items:center;gap:12px}
.rangeval{min-width:64px;text-align:right;font-size:14px}
.colorrow{display:flex;gap:8px;align-items:center}
.colorrow input[type=text]{flex:1}
.swatches{display:flex;gap:6px;margin-top:8px;flex-wrap:wrap}
.sw{width:24px;height:24px;border-radius:5px;border:1px solid #000;cursor:pointer}
.check{display:flex;align-items:center;gap:8px;margin:9px 0;font-size:14px;color:var(--ink)}
.check input{width:17px;height:17px;accent-color:var(--accent)}
.days{display:flex;gap:4px;margin-top:6px}
.days label{display:flex;flex-direction:column;align-items:center;font-size:11px;margin:0;gap:3px}
button{background:var(--accent);color:#fff;border:0;border-radius:7px;padding:11px 16px;
font-size:14px;font-weight:600;cursor:pointer}
button.alt{background:#3a3f4a}
button.warn{background:#a93226}
.bar{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}
.status{display:grid;grid-template-columns:120px 1fr;gap:6px 10px;font-size:14px}
.status .k{color:var(--dim)}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:7px;background:#777}
.ok{background:#4CAF50}.bad{background:#e74c3c}.warnd{background:#e67e22}
.alarm{border:1px solid var(--line);border-radius:8px;padding:12px;margin-bottom:12px}
.note{font-size:12px;color:var(--dim);margin-top:8px;line-height:1.5}
pre#log{background:#12141a;border:1px solid var(--line);border-radius:7px;padding:10px;
margin:0;max-height:260px;overflow:auto;font-family:Consolas,Menlo,monospace;
font-size:12px;line-height:1.45;color:#c8ccd4;white-space:pre-wrap;word-break:break-word}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:#2f3541;
border:1px solid var(--line);padding:12px 20px;border-radius:8px;display:none;font-size:14px}
</style></head><body>

<div class="topbar"><div class="inner">
  <div class="grow">
    <h1>My CYD Clock</h1>
    <div class="sub" id="version">settings</div>
  </div>
  <button onclick="save()">Save</button>
  <button class="warn" onclick="resetDefaults()">Reset</button>
</div></div>

<div class="wrap">

<div class="card">
  <h2>Status</h2>
  <div class="status">
    <div class="k">WiFi</div><div id="s_wifi">-</div>
    <div class="k">IP address</div><div id="s_ip">-</div>
    <div class="k">Name</div><div id="s_host">-</div>
    <div class="k">Time</div><div id="s_time">-</div>
    <div class="k">Time server</div><div id="s_ntp">-</div>
    <div class="k">Weather</div><div id="s_wx">-</div>
    <div class="k">Running for</div><div id="s_up">-</div>
    <div class="k">Free memory</div><div id="s_heap">-</div>
    <div class="k">Restarts</div><div id="s_restart">-</div>
  </div>
  <div class="bar"><button class="alt" onclick="act('restart')">Restart the clock</button></div>
</div>

<details class="card"><summary>WiFi</summary><div class="body">
  <div class="row">
    <div><label>Network name</label><input type="text" id="ssid" placeholder="Your WiFi name"></div>
    <div><label>Password (leave blank to keep the old one)</label>
         <input type="password" id="pass" placeholder="unchanged"></div>
  </div>
  <div class="note">Changing the network here restarts the clock. If it cannot join,
  it creates its own open network called My_CYD_Clock so you can try again.</div>
  <div class="bar"><button class="warn" onclick="act('wifi_forget')">Forget WiFi and restart</button></div>
</div></details>

<details class="card"><summary>Network Time</summary><div class="body">
  <div class="row">
    <div><label>Time server</label><input type="text" id="ntp"></div>
    <div><label>Time zone</label>
      <select id="tz">
        <option value="US Eastern|EST5EDT,M3.2.0,M11.1.0">US Eastern</option>
        <option value="US Central|CST6CDT,M3.2.0,M11.1.0">US Central</option>
        <option value="US Mountain|MST7MDT,M3.2.0,M11.1.0">US Mountain</option>
        <option value="US Arizona|MST7">US Arizona (no daylight saving)</option>
        <option value="US Pacific|PST8PDT,M3.2.0,M11.1.0">US Pacific</option>
        <option value="US Alaska|AKST9AKDT,M3.2.0,M11.1.0">US Alaska</option>
        <option value="Hawaii|HST10">Hawaii</option>
        <option value="UTC|UTC0">UTC</option>
        <option value="UK|GMT0BST,M3.5.0/1,M10.5.0">United Kingdom</option>
        <option value="Central Europe|CET-1CEST,M3.5.0,M10.5.0/3">Central Europe</option>
        <option value="Eastern Europe|EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe</option>
        <option value="India|IST-5:30">India</option>
        <option value="Japan|JST-9">Japan</option>
        <option value="Sydney|AEST-10AEDT,M10.1.0,M4.1.0/3">Sydney</option>
        <option value="custom|custom">Other (type it below)</option>
      </select>
    </div>
  </div>
  <div id="tzcustomwrap" style="display:none">
    <label>Time zone rule</label><input type="text" id="tzcustom" placeholder="EST5EDT,M3.2.0,M11.1.0">
  </div>
</div></details>

<details class="card"><summary>Time Display</summary><div class="body">
  <div class="row">
    <div>
      <div class="check"><input type="checkbox" id="h24"><span>24 hour clock</span></div>
      <div class="check"><input type="checkbox" id="secs"><span>Show seconds</span></div>
    </div>
    <div>
      <div class="check"><input type="checkbox" id="ampm"><span>Show AM and PM</span></div>
      <div class="check"><input type="checkbox" id="blink"><span>Blink the colon</span></div>
    </div>
    <div>
      <div class="check"><input type="checkbox" id="date"><span>Show the date</span></div>
    </div>
  </div>
  <div class="row">
    <div>
      <label>Clock face</label>
      <select id="face">
        <option value="1">Seven segment LED</option>
        <option value="2">Bold sans</option>
        <option value="3">Bold serif</option>
        <option value="4">Typewriter</option>
        <option value="5">Italic sans</option>
      </select>
      <div class="check"><input type="checkbox" id="ghost">
        <span>Show unlit segments</span></div>
      <div class="check"><input type="checkbox" id="invert">
        <span>Invert colours</span></div>
      <label>Line between segments</label>
      <select id="stroke">
        <option value="0">None, segments run together</option>
        <option value="1">1 pixel</option>
        <option value="2">2 pixels</option>
        <option value="3">3 pixels</option>
      </select>
    </div>
    <div>
      <label>Text colour</label>
      <div class="colorrow"><input type="color" id="fgpick"><input type="text" id="fg" placeholder="#FF0000"></div>
      <div class="swatches" id="fgsw"></div>
    </div>
    <div>
      <label>Background colour</label>
      <div class="colorrow"><input type="color" id="bgpick"><input type="text" id="bg" placeholder="#000000"></div>
      <div class="swatches" id="bgsw"></div>
    </div>
  </div>
  <div class="row">
    <div><label>Brightness (0 to 255)</label><input type="number" id="bri" min="5" max="255"></div>
    <div><label>Night brightness</label><input type="number" id="nbri" min="5" max="255"></div>
    <div><div class="check" style="margin-top:32px"><input type="checkbox" id="autodim">
      <span>Dim after sunset</span></div></div>
  </div>
  <div class="note">Some panels show every colour as its opposite, so a red clock
  on a black background comes out cyan on white. <b>Invert colours</b> turns that
  round. It takes effect the moment you save, with no restart, so if the screen
  looks like a photographic negative just tick it and save.</div>
  <div class="note">Dimming after sunset uses the sunrise and sunset times that come
  with the forecast, so it needs a working weather location. With it switched on the
  clock fetches the forecast twice a day on its own to keep those times current.</div>
</div></details>

<details class="card"><summary>Weather</summary><div class="body">
  <div class="row">
    <div><label>ZIP or postal code</label><input type="text" id="zip"></div>
    <div><label>Country code</label><input type="text" id="country" placeholder="us"></div>
    <div><label>Units</label>
      <select id="metric">
        <option value="0">Fahrenheit and mph</option>
        <option value="1">Celsius and km/h</option>
      </select>
    </div>
  </div>
  <label>Forecast length</label>
  <div class="rangewrap">
    <input type="range" id="wxdays" min="3" max="10" step="1">
    <div class="rangeval" id="wxdaysval">7 days</div>
  </div>
  <label>Screens that tapping cycles through</label>
  <div class="row">
    <div><div class="check"><input type="checkbox" id="wxcur"><span>Current Weather</span></div></div>
    <div><div class="check"><input type="checkbox" id="wxhr"><span>4 Hour Forecast</span></div></div>
    <div><div class="check"><input type="checkbox" id="wxday"><span>Daily Forecast</span></div></div>
  </div>
  <div class="note">The weather is fetched when it is going to be used and not
  otherwise: once when the clock starts so the screens have something on them, and
  again whenever you tap through to a weather screen. A reading less than half a
  minute old is reused rather than fetched again.</div>
  <div class="bar"><button class="alt" onclick="act('wx_refresh')">Fetch the weather now</button></div>
</div></details>

<details class="card"><summary>Alarms</summary><div class="body">
  <div id="alarms"></div>
  <div class="row">
    <div><label>Sound</label><select id="alsound"></select></div>
    <div><label>Volume <span id="alvollabel"></span></label>
      <input type="range" id="alvol" min="0" max="100" step="5"
             oninput="g('alvollabel').textContent=this.value+'%'"></div>
    <div><label>&nbsp;</label>
      <button class="alt" type="button" onclick="testSound()">Hear it</button></div>
  </div>
  <div class="note">Touch the clock screen to stop an alarm. It stops on its own after
  five minutes. The tone needs a speaker plugged into the header marked SPEAK.</div>
  <div id="customrow" style="display:none">
    <label>Your own ringtone</label>
    <input type="text" id="alcustom" spellcheck="false" maxlength="1199"
           placeholder="MyTune:d=4,o=6,b=125:8e6,8d6,f#,8p,g,8p">
  </div>
  <div class="note"><b>Hear it</b> plays the sound straight away, at the volume and
  tone shown here, without saving anything or setting an alarm off.</div>
  <div class="note">Picking <b>Your own ringtone</b> plays whatever you paste in the
  box. It takes <b>RTTTL</b>, the Ring Tone Text Transfer Language that Nokia phones
  used: a name, then <code>d</code> for the default note length, <code>o</code> for
  the default octave and <code>b</code> for the speed in beats a minute, then the
  notes themselves.
  <br><br>
  <code>Mozart3:d=4,o=5,b=125:16d#,16c#,16c,16c#,8e,8p,16f#,16e,...</code>
  <br><br>
  Thousands of these have been written out over the last twenty five years and any
  of them will play here. <a href="https://rtttl-hub.io" target="_blank"
  rel="noopener">rtttl-hub.io</a> is a good place to find them. If a tune comes out
  too low to hear properly on a small speaker, raising the <code>o</code> by one
  lifts the whole thing an octave.
  <br><br>
  Songs and film themes belong to whoever wrote them, so none are shipped with the
  clock, but nothing stops you pasting one into your own.</div>
  <div class="note">100% is as loud as this board goes. The speaker is fed a square
  wave from a single pin, and half of each cycle is as hard as a square wave can push,
  which is where 100% already sits. The volume setting works downwards from there.
  <br><br>If 100% is still not loud enough, the speaker itself is the thing to change
  rather than any setting. These boards take an 8 ohm speaker on the header marked
  SPEAK, and a larger one, or the one you have pressed against a flat surface or a
  small box instead of hanging loose on its wires, is worth a great deal more than
  anything the firmware can do.</div>
</div></details>

<details class="card" id="logsection"><summary>Activity log</summary><div class="body">
  <pre id="log">loading...</pre>
  <div class="bar">
    <button class="alt" onclick="loadLog()">Refresh</button>
    <button class="alt" onclick="downloadLog()">Download</button>
    <button class="alt" onclick="clearLog()">Clear</button>
  </div>
  <div class="check"><input type="checkbox" id="autolog" checked><span>Keep it updating</span></div>
  <div class="note">The last 50 things the clock did, plus whatever it was doing
  before the last restart. It is kept in memory only, so pulling the power empties
  it.</div>
</div></details>

</div><div id="toast"></div>
<script>
const g=id=>document.getElementById(id);
const DAYS=['S','M','T','W','T','F','S'];
const SWATCH=['#FF0000','#FF6A00','#FFD400','#00FF66','#00E5FF','#3366FF','#CC33FF','#FFFFFF','#000000'];
let S={};

function toast(msg){const t=g('toast');t.textContent=msg;t.style.display='block';
  setTimeout(()=>t.style.display='none',2600);}

function buildSwatches(boxId,textId,pickId){
  const box=g(boxId);
  SWATCH.forEach(c=>{const d=document.createElement('div');d.className='sw';d.style.background=c;
    d.onclick=()=>{g(textId).value=c;g(pickId).value=c;};box.appendChild(d);});
}

function buildAlarms(){
  let html='';
  for(let i=0;i<3;i++){
    html+='<div class="alarm"><div class="row">'+
      '<div><div class="check"><input type="checkbox" id="a'+i+'en"><span>Alarm '+(i+1)+' on</span></div></div>'+
      '<div><label>Time</label><input type="time" id="a'+i+'time"></div>'+
      '</div><label>Days</label><div class="days">';
    for(let d=0;d<7;d++){
      html+='<label>'+DAYS[d]+'<input type="checkbox" id="a'+i+'d'+d+'"></label>';
    }
    html+='</div><div class="row" style="margin-top:8px">'+
      '<div><div class="check"><input type="checkbox" id="a'+i+'t"><span>Tone</span></div></div>'+
      '<div><div class="check"><input type="checkbox" id="a'+i+'l"><span>Flash the LED</span></div></div>'+
      '<div><div class="check"><input type="checkbox" id="a'+i+'s"><span>Flash the screen</span></div></div>'+
      '</div><div class="bar"><button class="alt" onclick="act(\'alarm_test\','+i+')">Test</button></div></div>';
  }
  g('alarms').innerHTML=html;
}

function two(n){return (n<10?'0':'')+n;}
function showDays(){g('wxdaysval').textContent=g('wxdays').value+' days';}

function fill(){
  g('ssid').value=S.ssid||'';
  g('ntp').value=S.ntp||'';
  let found=false;
  for(const o of g('tz').options){
    if(o.value.split('|')[1]===S.tz){g('tz').value=o.value;found=true;break;}
  }
  if(!found){g('tz').value='custom|custom';g('tzcustom').value=S.tz;}
  tzChanged();
  g('h24').checked=S.h24; g('secs').checked=S.secs; g('ampm').checked=S.ampm;
  g('blink').checked=S.blink; g('date').checked=S.date;
  g('face').value=S.face;
  g('ghost').checked=S.ghost;
  g('invert').checked=S.invert;
  g('stroke').value=String(S.stroke);
  g('fg').value=S.fg; g('fgpick').value=S.fg;
  g('bg').value=S.bg; g('bgpick').value=S.bg;
  g('bri').value=S.bri; g('nbri').value=S.nbri; g('autodim').checked=S.autodim;
  g('zip').value=S.zip; g('country').value=S.country;
  g('metric').value=S.metric?'1':'0';
  g('wxdays').value=S.wxdays; showDays();
  g('wxcur').checked=S.wxcur; g('wxhr').checked=S.wxhr; g('wxday').checked=S.wxday;
  buildTones(S.tones||[]);
  g('alsound').value=String(S.alsound);
  g('alcustom').value=S.alcustom||'';
  toneChanged();
  g('alvol').value=S.alvol; g('alvollabel').textContent=S.alvol+'%';
  S.alarms.forEach((a,i)=>{
    g('a'+i+'en').checked=a.on;
    g('a'+i+'time').value=two(a.h)+':'+two(a.m);
    for(let d=0;d<7;d++) g('a'+i+'d'+d).checked=((a.days>>d)&1)===1;
    g('a'+i+'t').checked=a.tone; g('a'+i+'l').checked=a.led; g('a'+i+'s').checked=a.screen;
  });
  g('version').textContent='firmware '+S.version;
}

function tzChanged(){
  const custom=g('tz').value.split('|')[1]==='custom';
  g('tzcustomwrap').style.display=custom?'block':'none';
}

function collect(){
  const p=new URLSearchParams();
  const b=(id)=>g(id).checked?'1':'0';
  p.set('ssid',g('ssid').value.trim());
  p.set('pass',g('pass').value);
  p.set('ntp',g('ntp').value.trim());
  const parts=g('tz').value.split('|');
  if(parts[1]==='custom'){p.set('tzname','Custom');p.set('tz',g('tzcustom').value.trim());}
  else {p.set('tzname',parts[0]);p.set('tz',parts[1]);}
  p.set('h24',b('h24'));p.set('secs',b('secs'));p.set('ampm',b('ampm'));
  p.set('blink',b('blink'));p.set('date',b('date'));
  p.set('face',g('face').value);
  p.set('ghost',b('ghost'));
  p.set('invert',b('invert'));
  p.set('stroke',g('stroke').value);
  p.set('fg',g('fg').value.trim());p.set('bg',g('bg').value.trim());
  p.set('bri',g('bri').value);p.set('nbri',g('nbri').value);p.set('autodim',b('autodim'));
  p.set('zip',g('zip').value.trim());p.set('country',g('country').value.trim().toLowerCase());
  p.set('metric',g('metric').value);
  p.set('wxdays',g('wxdays').value);
  p.set('wxcur',b('wxcur'));p.set('wxhr',b('wxhr'));p.set('wxday',b('wxday'));
  p.set('alsound',g('alsound').value);p.set('alvol',g('alvol').value);
  p.set('alcustom',g('alcustom').value.trim());
  for(let i=0;i<3;i++){
    p.set('a'+i+'en',b('a'+i+'en'));
    p.set('a'+i+'time',g('a'+i+'time').value||'07:00');
    let mask=0;
    for(let d=0;d<7;d++) if(g('a'+i+'d'+d).checked) mask|=(1<<d);
    p.set('a'+i+'d',mask);
    p.set('a'+i+'t',b('a'+i+'t'));p.set('a'+i+'l',b('a'+i+'l'));p.set('a'+i+'s',b('a'+i+'s'));
  }
  return p;
}

async function save(){
  try{
    const r=await fetch('/api/settings',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:collect().toString()});
    const j=await r.json();
    if(j.restarting){toast('Saved, restarting to join the new network');}
    else {toast('Saved'); g('pass').value='';}
  }catch(e){toast('Could not save');}
}

async function resetDefaults(){
  if(!confirm('Erase every setting and forget the WiFi network?\n\nThe clock restarts and has to be set up again from scratch.')) return;
  await act('factory_reset');
  toast('Erasing everything and restarting');
}

function buildTones(list){
  const sel=g('alsound');
  sel.innerHTML='';
  list.forEach((label,i)=>{
    const o=document.createElement('option');
    o.value=String(i+1); o.textContent=label; sel.appendChild(o);
  });
  const o=document.createElement('option');
  o.value='99'; o.textContent='Your own ringtone...'; sel.appendChild(o);
  sel.onchange=toneChanged;
}

function toneChanged(){
  g('customrow').style.display = g('alsound').value==='99' ? '' : 'none';
}

async function testSound(){
  try{
    const p=new URLSearchParams();
    p.set('do','sound_test');
    p.set('sound',g('alsound').value);
    p.set('vol',g('alvol').value);
    if(g('alsound').value==='99') p.set('custom',g('alcustom').value.trim());
    await fetch('/api/action',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});
    toast('Playing it now');
  }catch(e){toast('Could not reach the clock');}
}

async function act(what,idx){
  try{
    const p=new URLSearchParams();p.set('do',what);
    if(idx!==undefined)p.set('idx',idx);
    await fetch('/api/action',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});
    toast('Done');
  }catch(e){toast('Could not do that');}
}

async function loadLog(){
  try{
    const t=await (await fetch('/api/log')).text();
    const box=g('log');
    const atBottom=box.scrollTop+box.clientHeight>=box.scrollHeight-24;
    box.textContent=t;
    if(atBottom) box.scrollTop=box.scrollHeight;
  }catch(e){}
}

function downloadLog(){
  fetch('/api/log').then(r=>r.text()).then(t=>{
    const a=document.createElement('a');
    a.href=URL.createObjectURL(new Blob([t],{type:'text/plain'}));
    a.download='cyd-clock-log.txt';
    a.click();
    setTimeout(()=>URL.revokeObjectURL(a.href),2000);
  }).catch(()=>toast('Could not download the log'));
}

async function clearLog(){ await act('log_clear'); setTimeout(loadLog,300); }

async function tickStatus(){
  try{
    const s=await (await fetch('/api/status')).json();
    const dot=k=>'<span class="dot '+k+'"></span>';
    g('s_wifi').innerHTML=(s.online?dot('ok'):dot('bad'))+
      (s.online?(s.ssid+'  '+s.rssi+' dBm'):'not connected');
    g('s_ip').textContent=s.ip;
    g('s_host').textContent=s.host;
    g('s_time').textContent=s.time;
    g('s_ntp').innerHTML=(s.synced?dot('ok'):dot('warnd'))+s.ntp+(s.synced?' synced':' waiting');
    g('s_wx').innerHTML=(s.wxok?dot('ok'):dot('warnd'))+s.wx;
    g('s_up').textContent=s.uptime;
    g('s_heap').textContent=s.heap+' bytes';
    g('s_restart').innerHTML=(s.restarts>0?dot('warnd'):dot('ok'))+s.restart;
  }catch(e){}
}

window.addEventListener('load',async()=>{
  buildSwatches('fgsw','fg','fgpick');
  buildSwatches('bgsw','bg','bgpick');
  buildAlarms();
  g('fgpick').oninput=()=>g('fg').value=g('fgpick').value.toUpperCase();
  g('bgpick').oninput=()=>g('bg').value=g('bgpick').value.toUpperCase();
  g('fg').oninput=()=>{try{g('fgpick').value=g('fg').value;}catch(e){}};
  g('bg').oninput=()=>{try{g('bgpick').value=g('bg').value;}catch(e){}};
  g('tz').onchange=tzChanged;
  g('wxdays').oninput=showDays;
  g('logsection').addEventListener('toggle',()=>{if(g('logsection').open) loadLog();});
  S=await (await fetch('/api/settings')).json();
  fill();
  tickStatus();
  setInterval(()=>{
    tickStatus();
    if(g('logsection').open && g('autolog').checked) loadLog();
  },3000);
});
</script></body></html>
)HTMLPAGE";
