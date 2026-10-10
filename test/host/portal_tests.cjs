const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(process.argv[2], 'utf8');

function page(initial) {
  class Element {
    constructor(id='') { this.id=id; this.value=''; this.textContent=''; this.disabled=false; this.required=false; this.hidden=false; this.options=[]; this.listeners={}; }
    addEventListener(type, callback) { this.listeners[type]=callback; }
    append(option) { this.options.push(option); }
    replaceChildren() { this.options=[]; this.value=''; }
    async emit(type) { return this.listeners[type]({preventDefault(){}}); }
    set innerHTML(value) { throw new Error('Network names must be rendered as text'); }
  }
  const ids=['wifi','message','connect','network','ssid','password','manual','manual-ssid','password-field','rescan','scan-message','token'];
  const nodes=Object.fromEntries(ids.map(id=>[id,new Element(id)]));
  const placeholder=new Element();placeholder.textContent='Searching for networks…';nodes.network.append(placeholder);
  nodes.password.disabled=true;nodes.token.value='test-csrf-token';
  nodes.wifi.reportValidity=()=>!!nodes.network.value&&(!nodes['manual-ssid'].required||!!nodes['manual-ssid'].value)&&(!nodes.password.required||nodes.password.disabled||nodes.password.value.length>=8);
  class FormData {
    constructor() { this.entries=[['token',nodes.token.value],['ssid',nodes.ssid.value]];if(!nodes.password.disabled)this.entries.push(['password',nodes.password.value]); }
    [Symbol.iterator]() { return this.entries[Symbol.iterator](); }
  }
  const replies=[initial],calls=[],timers=new Map();let timer=0,interval;
  const context={
    document:{getElementById:id=>nodes[id],createElement:()=>new Element()},FormData,URLSearchParams,
    fetch:async(url,options={})=>{calls.push({url,...options});assert.ok(replies.length,'Unexpected fetch: '+url);const r=replies.shift();if(r instanceof Error)throw r;return {ok:r.status===undefined||r.status<400,json:async()=>r.body||r};},
    setTimeout:callback=>{timers.set(++timer,callback);return timer;},clearTimeout:id=>timers.delete(id),setInterval:callback=>{interval=callback;},
  };
  vm.runInNewContext(source,context);
  return {nodes,calls,replies,timers,poll:()=>interval(),scanTick:()=>{const [id,callback]=timers.entries().next().value;timers.delete(id);return callback();}};
}
const settle=()=>new Promise(resolve=>setImmediate(resolve));
const home={ssid:'Home WiFi',rssi:-42,secured:true,channel:11};
const cafe={ssid:'Cafe <img onerror=alert(1)> & WiFi',rssi:-67,secured:false,channel:6};

(async()=>{
  const p=page({scanning:true});await settle();assert.equal(p.nodes.network.disabled,true);assert.equal(p.timers.size,1);
  p.replies.push({networks:[home,cafe]});await p.scanTick();await settle();
  assert.equal(p.nodes.network.options.length,4);assert.match(p.nodes.network.options[1].textContent,/Home WiFi.*Strong signal/);
  assert.equal(p.nodes.network.options[2].textContent,cafe.ssid+' · Good signal · open');assert.equal(p.calls[0].cache,'no-store');
  p.nodes.network.value='n0';await p.nodes.network.emit('change');assert.equal(p.nodes.ssid.value,home.ssid);assert.equal(p.nodes.password.disabled,false);assert.equal(p.nodes['password-field'].hidden,false);
  await p.nodes.wifi.emit('submit');assert.equal(p.calls.length,2); // empty required password never submits
  p.nodes.password.value='sample-pass';p.replies.push({status:202,body:{message:'Connecting…'}});await p.nodes.wifi.emit('submit');
  const payload=p.calls.at(-1).body;assert.equal(payload.get('ssid'),home.ssid);assert.equal(payload.get('password'),'sample-pass');assert.equal(payload.get('token'),'test-csrf-token');
  assert.equal(p.nodes.network.disabled,true);assert.equal(p.nodes.rescan.disabled,true);assert.equal(p.nodes.password.disabled,true);
  p.replies.push({connected:false,connecting:false,message:'Could not connect'});await p.poll();assert.equal(p.nodes.connect.disabled,false);assert.equal(p.nodes.password.disabled,false);assert.equal(p.nodes.password.value,'sample-pass');
  // Refresh replaces old options and retains the selected SSID/password by name.
  p.replies.push({networks:[cafe,home]});await p.nodes.rescan.emit('click');await settle();assert.equal(p.calls.at(-1).url,'/networks?refresh=1');assert.equal(p.nodes.network.options.length,4);assert.equal(p.nodes.network.value,'n1');assert.equal(p.nodes.password.value,'sample-pass');
  p.replies.push({status:503,body:{message:'Scan failed. Try again.'}});await p.nodes.rescan.emit('click');await settle();assert.equal(p.nodes.rescan.disabled,false);assert.equal(p.nodes.ssid.value,home.ssid);assert.match(p.nodes['scan-message'].textContent,/Scan failed/);
  p.nodes.network.value='n0';await p.nodes.network.emit('change');assert.equal(p.nodes.ssid.value,cafe.ssid);assert.equal(p.nodes.password.value,'');assert.equal(p.nodes.password.disabled,true);assert.equal(p.nodes['password-field'].hidden,true);
  p.replies.push({status:202,body:{message:'Connecting…'}});await p.nodes.wifi.emit('submit');assert.equal(p.calls.at(-1).body.get('ssid'),cafe.ssid);assert.equal(p.calls.at(-1).body.get('password'),null);
  p.replies.push({connected:true,connecting:false,message:'Connected!'});await p.poll();assert.equal(p.nodes.connect.disabled,true);assert.equal(p.nodes.rescan.disabled,true);assert.match(p.nodes.message.textContent,/Connected!/);
  const finishedCalls=p.calls.length;await p.nodes.wifi.emit('submit');await p.poll();assert.equal(p.calls.length,finishedCalls);

  const hidden=page({networks:[]});await settle();assert.equal(hidden.nodes.network.options.length,2);assert.match(hidden.nodes['scan-message'].textContent,/No networks found/);
  hidden.nodes.network.value='manual';await hidden.nodes.network.emit('change');assert.equal(hidden.nodes.manual.hidden,false);assert.equal(hidden.nodes['manual-ssid'].required,true);
  hidden.nodes['manual-ssid'].value='Hidden network';await hidden.nodes['manual-ssid'].emit('input');hidden.nodes.password.value='hidden-pass';
  hidden.replies.push({status:400,body:{message:'Check the password'}});await hidden.nodes.wifi.emit('submit');assert.equal(hidden.calls.at(-1).body.get('ssid'),'Hidden network');assert.equal(hidden.nodes.password.disabled,false);assert.equal(hidden.nodes.message.textContent,'Check the password');

  const busy=page({busy:true,networks:[]});await settle();assert.equal(busy.timers.size,1);busy.replies.push({networks:[home]});await busy.scanTick();await settle();assert.equal(busy.nodes.network.disabled,false);
  const failed=page(new Error('Network unavailable'));await settle();assert.equal(failed.nodes.rescan.disabled,false);assert.equal(failed.nodes.network.options.at(-1).value,'manual');
  console.log('PASS: actual portal JavaScript; network picker, safe SSIDs, async scan/refresh/retry, open and secured networks, hidden SSIDs, encoded credentials, connection errors and success');
})().catch(error=>{console.error(error);process.exitCode=1;});
