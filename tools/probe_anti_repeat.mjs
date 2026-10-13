// Run from repository root. No device access: two verified public TLS feeds
// drive the actual compiled firmware parsers, PriceGuard and QuoteBook policy.
import tls from 'node:tls';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {performance} from 'node:perf_hooks';
import {spawn,execFileSync} from 'node:child_process';
import readline from 'node:readline';

const argument=(name,fallback)=>{const i=process.argv.indexOf(name);return i<0?fallback:process.argv[i+1];};
const durationMs=Number(argument('--duration-ms','90000'));
const warmupMs=Number(argument('--warmup-ms','10000'));
if(!Number.isInteger(durationMs)||durationMs<1000||durationMs>300000||!Number.isInteger(warmupMs)||warmupMs<0||warmupMs>=durationMs)throw Error('Invalid duration/warmup');
const directory=path.resolve(argument('--output','test/.build/anti-repeat-probe'));
const reportPath=path.resolve(argument('--report','test/anti_repeat_quote_probe_report.json'));
const hashes=Object.fromEntries(['include/QuoteBook.h','include/StreamParsers.h','include/ClockCore.h','tools/anti_repeat_quote_probe.cpp'].map(file=>[file,crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')]));
fs.mkdirSync(directory,{recursive:true});
const executable=path.join(directory,'selector');
execFileSync(process.env.CXX??'c++',['-std=c++11','-Wall','-Wextra','-Werror','-Iinclude','-I.pio/libdeps/esp32-devkitc-32e/ArduinoJson/src','tools/anti_repeat_quote_probe.cpp','-o',executable]);
const child=spawn(executable),frames=[],quotes=[],events=[],commands=[];
let childError='';child.stderr.on('data',data=>{childError+=data.toString();});
readline.createInterface({input:child.stdout}).on('line',line=>{const item=JSON.parse(line);(item.type==='frame'?frames:quotes).push(item);});
const start=performance.now(),checkedAt=new Date().toISOString();
const at=()=>Math.floor(performance.now()-start),epoch=()=>Math.floor(Date.now()/1000);
const send=command=>{commands.push(command);child.stdin.write(command+'\n');};
const roots=fs.readFileSync('data/cert/mozilla.pem');
const providers=[
  {name:'Coinbase',source:0,host:'ws-feed.exchange.coinbase.com',path:'/',subscriptions:[{type:'subscribe',product_ids:['BTC-USD'],channels:['ticker','heartbeat']}]},
  {name:'Bitfinex',source:4,host:'api-pub.bitfinex.com',path:'/ws/2',subscriptions:[{event:'conf',flags:32768},{event:'subscribe',channel:'book',symbol:'tBTCUSD',prec:'P0',freq:'F0',len:'1'}]}
];
if(process.argv.includes('--coinbase-cross'))providers[0].subscriptions[0].product_ids=['BTC-USD','BTC-USDT','USDT-USD'];
if(process.argv.includes('--coinbase-only'))providers.splice(1);
const crossBooks=process.argv.includes('--bitfinex-cross');
if(crossBooks){
  const bitfinex=providers.find(provider=>provider.source===4);if(!bitfinex)throw Error('Cross books need Bitfinex');
  for(const symbol of ['tBTCUST','tUSTUSD'])bitfinex.subscriptions.push({event:'subscribe',channel:'book',symbol,prec:'P0',freq:'F0',len:'1'});
}
const productTicks={};
const books={},crossFrames=[];let timestampsEnabled=false;
const currentBook=(book,now)=>book&&book.ready&&!book.corrupted&&now-book.messageAt<=35000&&
  Number.isFinite(book.bid)&&book.bid>0&&Number.isFinite(book.ask)&&book.ask>=book.bid&&
  (book.ask-book.bid)/((book.ask+book.bid)/2)<=.01&&
  (book.symbol!=='tUSTUSD'||book.bid>=.5&&book.ask<=1.5);
function crossBookMessage(data,now){
  if(data?.event==='conf'){timestampsEnabled=data.status==='OK'&&(data.flags&32768)!==0;return;}
  if(data?.event==='subscribed'&&data.channel==='book'){
    if(!timestampsEnabled||!['tBTCUSD','tBTCUST','tUSTUSD'].includes(data.symbol)||data.prec!=='P0'||data.freq!=='F0'||String(data.len)!=='1'||!Number.isInteger(data.chanId)||data.chanId<=0)throw Error('Invalid cross-book acknowledgement');
    books[data.symbol]={symbol:data.symbol,channel:data.chanId,ready:false,corrupted:false,bid:0,ask:0,publishedAt:0,receiptAt:0,messageAt:now,updates:0,heartbeats:0,quotes:0,distinct:new Set(),maxIncomingPublicationAgeMs:0};return;
  }
  if(!Array.isArray(data))return;
  const book=Object.values(books).find(book=>book.channel===data[0]);if(!book)return;
  book.messageAt=now;if(data[1]==='hb'){++book.heartbeats;return;}
  const stamp=data[2],pubAge=Date.now()-stamp;
  if(!timestampsEnabled||!Number.isSafeInteger(stamp)||stamp<=0||pubAge>5000||pubAge< -10000||stamp<book.publishedAt)throw Error('Invalid cross-book publication timestamp');
  const body=data[1];if(!Array.isArray(body))throw Error('Missing cross-book levels');
  const snapshot=Array.isArray(body[0]);if(snapshot){if(body.length!==2)throw Error('Partial cross-book snapshot');book.bid=book.ask=0;book.ready=true;}
  else if(!book.ready)throw Error('Book update before snapshot');
  for(const row of snapshot?body:[body]){
    if(!Array.isArray(row)||row.length<3)throw Error('Malformed cross-book row');
    const [price,count,amount]=row;
    if(!Number.isFinite(price)||price<=0||price>1e9||!Number.isInteger(count)||count<0||count>0xffffffff||!Number.isFinite(amount)||amount===0||count===0&&Math.abs(amount)!==1)throw Error('Invalid cross-book level');
    const side=amount>0?'bid':'ask';if(snapshot&&(!count||book[side]))throw Error('Duplicate/missing snapshot side');
    if(count)book[side]=price;else if(book[side]===price)book[side]=0;
  }
  book.publishedAt=stamp;book.receiptAt=now;++book.updates;book.maxIncomingPublicationAgeMs=Math.max(book.maxIncomingPublicationAgeMs,pubAge);
  if(currentBook(book,now)){++book.quotes;book.distinct.add((book.bid+book.ask)/2);}
}
function crossFrame(now){
  if(!crossBooks)return;
  const usd=books.tBTCUSD,ust=books.tBTCUST,fx=books.tUSTUSD,coinbase=productTicks['BTC-USD']?.at(-1);
  const directValid=!!currentBook(usd,now),convertedValid=!!currentBook(ust,now)&&!!currentBook(fx,now),coinbaseValid=!!coinbase&&now-coinbase.at_ms<=5000;
  const direct=directValid?(usd.bid+usd.ask)/2:null,rate=convertedValid?(fx.bid+fx.ask)/2:null,converted=convertedValid?(ust.bid+ust.ask)/2*rate:null;
  const values=[coinbaseValid?coinbase.price:null,direct,converted].filter(value=>value!==null);
  crossFrames.push({at_ms:now,coinbase:coinbaseValid?coinbase.price:null,direct_midpoint:direct,converted_midpoint:converted,fx_midpoint:rate,distinct_rounded_prices:new Set(values.map(value=>Math.floor(value+.5))).size,leg_receipt_age_ms:Object.fromEntries(Object.entries(books).map(([symbol,book])=>[symbol,now-book.receiptAt])),leg_publication_age_ms:Object.fromEntries(Object.entries(books).map(([symbol,book])=>[symbol,Date.now()-book.publishedAt]))});
}
function framed(opcode,value=Buffer.alloc(0)){
  const payload=Buffer.isBuffer(value)?value:Buffer.from(value),mask=crypto.randomBytes(4);
  let header;if(payload.length<126)header=Buffer.from([0x80|opcode,0x80|payload.length]);
  else if(payload.length<=65535){header=Buffer.alloc(4);header[0]=0x80|opcode;header[1]=0xfe;header.writeUInt16BE(payload.length,2);}
  else throw Error('Oversized client frame');
  const data=Buffer.from(payload);for(let i=0;i<data.length;++i)data[i]^=mask[i%4];return Buffer.concat([header,mask,data]);
}
function run(provider){return new Promise(resolve=>{
  const key=crypto.randomBytes(16).toString('base64');let buffer=Buffer.alloc(0),upgraded=false,settled=false,fragments=[],fragmentSize=0,fragmentOpcode=0,pingTimer=null,pongDeadline=null,pingAt=0;
  const result={provider:provider.name,source:provider.source,endpoint:`wss://${provider.host}${provider.path}`,request_headers:{Host:provider.host+':443',Connection:'Upgrade',Upgrade:'websocket','Sec-WebSocket-Version':'13',Origin:'file://','User-Agent':'arduino-WebSocket-Client'},certificate_validation:'embedded Mozilla PEM with hostname validation and rejectUnauthorized=true',subscriptions:provider.subscriptions,upgrade_status:null,tls_version:null,tls_authorized:false,open_at_ms:null,messages:0,pings_sent:0,pongs_received:0,server_pings:0,pong_rtt_ms:[],errors:[],unexpected_disconnect:false};
  const socket=tls.connect({host:provider.host,port:443,servername:provider.host,ca:roots,rejectUnauthorized:true,minVersion:'TLSv1.2',maxVersion:'TLSv1.2',ALPNProtocols:['http/1.1']});
  const deadline=setTimeout(()=>finish('duration-complete'),durationMs);
  function finish(reason){
    if(settled)return;settled=true;clearTimeout(deadline);clearTimeout(pongDeadline);clearInterval(pingTimer);
    result.finished_reason=reason;result.elapsed_ms=at();result.connected_at_end=upgraded&&!socket.destroyed;
    // Leave the final frame state intact when the observation ends normally.
    if(reason!=='duration-complete')send(`D ${at()} ${provider.source} ${epoch()}`);
    if(upgraded&&!socket.destroyed){const close=Buffer.alloc(2);close.writeUInt16BE(1000);socket.write(framed(8,close));}
    socket.destroy();resolve(result);
  }
  function message(payload){
    ++result.messages;let data;try{data=JSON.parse(payload.toString());}catch{result.errors.push('Malformed JSON');finish('malformed-json');return;}
    const now=at();events.push({provider:provider.name,at_ms:now,data});
    if(crossBooks&&provider.source===4){try{crossBookMessage(data,now);}catch(error){result.errors.push(error.message);finish('cross-book-invalid');return;}}
    if(provider.source===0&&data.type==='ticker'&&data.product_id){
      const list=productTicks[data.product_id]??=[];
      list.push({at_ms:now,price:Number(data.price),timestamp_ms:Date.parse(data.time),trade_id:data.trade_id,execution_age_ms:Date.now()-Date.parse(data.time)});
    }
    send(`M ${now} ${provider.source} ${epoch()} ${payload.toString()}`);
    if(data.type==='error'||data.event==='error'||data.success===false){result.errors.push(JSON.stringify(data).slice(0,300));finish('subscription-error');}
  }
  function consume(){
    while(buffer.length>=2){
      const fin=!!(buffer[0]&0x80),opcode=buffer[0]&15,masked=!!(buffer[1]&0x80);let length=buffer[1]&127,offset=2;
      if(masked||buffer[0]&0x70){result.errors.push('Invalid server frame flags');finish('frame-flags');return;}
      if(length===126){if(buffer.length<4)return;length=buffer.readUInt16BE(2);offset=4;}
      else if(length===127){if(buffer.length<10)return;const large=buffer.readBigUInt64BE(2);if(large>8192n){result.errors.push('Oversized server frame');finish('frame-limit');return;}length=Number(large);offset=10;}
      if(length>8192||(opcode>=8&&(!fin||length>125))){result.errors.push('Invalid frame bounds');finish('frame-limit');return;}
      if(buffer.length<offset+length)return;const payload=Buffer.from(buffer.subarray(offset,offset+length));buffer=buffer.subarray(offset+length);
      if(opcode===9){++result.server_pings;socket.write(framed(10,payload));}
      else if(opcode===10){++result.pongs_received;result.pong_rtt_ms.push(at()-pingAt);clearTimeout(pongDeadline);send(`H ${at()} ${provider.source} ${epoch()}`);}
      else if(opcode===8){result.close_code=length>=2?payload.readUInt16BE():null;result.unexpected_disconnect=true;finish('server-close');return;}
      else if(opcode===1||opcode===2){
        if(fin){if(opcode===1)message(payload);}
        else{fragmentOpcode=opcode;fragments=[payload];fragmentSize=payload.length;}
      }else if(opcode===0){
        if(!fragmentOpcode){result.errors.push('Unexpected continuation');finish('fragment-error');return;}
        fragments.push(payload);fragmentSize+=payload.length;
        if(fragmentSize>8192){result.errors.push('Oversized fragmented message');finish('frame-limit');return;}
        if(fin){if(fragmentOpcode===1)message(Buffer.concat(fragments));fragments=[];fragmentSize=fragmentOpcode=0;}
      }
      if(settled)return;
    }
  }
  socket.on('secureConnect',()=>{
    result.tls_authorized=socket.authorized;result.tls_version=socket.getProtocol();result.cipher=socket.getCipher().standardName;
    socket.write(`GET ${provider.path} HTTP/1.1\r\nHost: ${result.request_headers.Host}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${key}\r\nOrigin: file://\r\nUser-Agent: arduino-WebSocket-Client\r\n\r\n`);
  });
  socket.on('data',data=>{
    buffer=Buffer.concat([buffer,data]);if(buffer.length>32768){result.errors.push('Receive buffer exceeded limit');finish('buffer-limit');return;}
    if(!upgraded){
      const end=buffer.indexOf('\r\n\r\n');if(end<0)return;
      const lines=buffer.subarray(0,end).toString().split('\r\n');buffer=buffer.subarray(end+4);result.upgrade_status=Number(lines[0].split(' ')[1]);
      const headers=Object.fromEntries(lines.slice(1).map(line=>{const i=line.indexOf(':');return[line.slice(0,i).toLowerCase(),line.slice(i+1).trim()];}));
      const accept=crypto.createHash('sha1').update(key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
      if(result.upgrade_status!==101||headers['sec-websocket-accept']!==accept){result.errors.push('Upgrade/accept validation failed');finish('upgrade-error');return;}
      upgraded=true;result.open_at_ms=at();send(`C ${at()} ${provider.source} ${epoch()}`);
      for(const message of provider.subscriptions)socket.write(framed(1,JSON.stringify(message)));
      pingTimer=setInterval(()=>{pingAt=at();++result.pings_sent;socket.write(framed(9));pongDeadline=setTimeout(()=>{result.errors.push('PONG timeout');finish('pong-timeout');},5000);},20000);
    }
    consume();
  });
  socket.on('error',error=>{result.errors.push(error.message);finish('socket-error');});
  socket.on('end',()=>{result.unexpected_disconnect=true;finish('server-eof');});
  socket.on('close',()=>{if(!settled){result.unexpected_disconnect=true;finish('socket-close');}});
});}
let timer,nextFrame=100;
function frame(){
  const now=at();
  if(now>=nextFrame){crossFrame(now);send(`F ${now} ${epoch()} ${crypto.randomBytes(4).readUInt32LE()}`);nextFrame+=(Math.floor((now-nextFrame)/100)+1)*100;}
  timer=setTimeout(frame,Math.max(1,nextFrame-at()));
}
timer=setTimeout(frame,100);
const results=await Promise.all(providers.map(run));clearTimeout(timer);
const exit=new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',code=>code===0?resolve():reject(Error(`Selector exit ${code}: ${childError}`)));});
child.stdin.end();await exit;
const measured=frames.filter(frame=>frame.at>=warmupMs);
const changedDigits=(a,b)=>{let changed=0;for(let i=0;i<6;++i){changed+=a%10!==b%10;a=Math.floor(a/10);b=Math.floor(b/10);}return changed;};
let changes=0,multi=0,holds=0,independentAba=0;const distinct=[];
let allAba=0;
for(const frame of frames){
  if(frame.blank||frame.digits===distinct.at(-1))continue;
  if(distinct.length>1&&frame.digits===distinct.at(-2)){++allAba;if(frame.at>=warmupMs)++independentAba;}
  distinct.push(frame.digits);
}
for(let i=1;i<measured.length;++i){
  const a=measured[i-1],b=measured[i];if(a.blank||b.blank)continue;
  if(a.digits===b.digits)++holds;
  else{++changes;multi+=changedDigits(a.digits,b.digits)>=2;}
}
const transitions=Math.max(0,measured.length-1),nonblank=measured.filter(frame=>!frame.blank),gaps=measured.slice(1).map((frame,i)=>frame.at-measured[i].at);
const rejected=quotes.filter(quote=>!quote.accepted);
const observed=Object.fromEntries(providers.map(p=>{const q=quotes.filter(q=>q.source===p.source);return[p.name,{validated_quotes:q.length,accepted_quotes:q.filter(q=>q.accepted).length,distinct_rounded_values:new Set(q.map(q=>Math.floor(q.price+.5))).size}];}));
const productSummaries=Object.fromEntries(Object.entries(productTicks).map(([name,list])=>[name,{ticker_receipts:list.length,distinct_trade_ids:new Set(list.map(tick=>tick.trade_id)).size,first_trade_age_ms:list[0].execution_age_ms,minimum_trade_age_ms:Math.min(...list.map(tick=>tick.execution_age_ms)),maximum_trade_age_ms:Math.max(...list.map(tick=>tick.execution_age_ms)),receipts_with_trade_age_at_most_5s:list.filter(tick=>tick.execution_age_ms<=5000).length,first_trade:list[0],last_trade:list.at(-1)}]));
const realQuotes=new Set(quotes.filter(quote=>quote.accepted).map(quote=>JSON.stringify([quote.source,quote.price,quote.at,Math.floor(quote.timestamp_ms/1000),quote.kind])));
const validation={all_frames:frames.length,all_nonblank_frames:frames.filter(frame=>!frame.blank).length,frames_not_traceable_to_real_accepted_quote:frames.filter(frame=>!frame.blank&&!realQuotes.has(JSON.stringify([frame.source,frame.price,frame.at-frame.quote_age_ms,frame.timestamp,frame.kind]))).length,incorrect_rounding_frames:frames.filter(frame=>!frame.blank&&frame.digits!==Math.floor(frame.price+.5)).length,all_observed_distinct_sequence_ABA:allAba,startup_blank_frames:frames.filter(frame=>frame.blank&&frame.at<warmupMs).length};
const report={checked_at_utc:checkedAt,duration_ms:durationMs,warmup_ms:warmupMs,measurement:'computer_verified_TLS_actual_compiled_firmware_headers',scope:'Two computer TLS connections with embedded roots and exact firmware upgrade headers. Actual C++ StreamParsers, PriceGuard, QuoteSequence and QuoteBook; modeled frame fallback/liveness, no ESP32/device/SPI acceptance.',target_frame_period_ms:100,source_hashes:hashes,results,quote_counts:observed,selector:{frames:measured.length,transitions,whole_dollar_changes:changes,multi_digit_changes:multi,holds,changed_fraction:transitions?changes/transitions:0,multi_digit_fraction:transitions?multi/transitions:0,independent_distinct_sequence_ABA:independentAba,firmware_policy_reversals:measured.filter(frame=>frame.reversal).length,blank_frames:measured.filter(frame=>frame.blank).length,selected_sources:[...new Set(nonblank.map(frame=>frame.source))],distinct_displayed_values:new Set(nonblank.map(frame=>frame.digits)).size,max_selected_receipt_age_ms:Math.max(0,...nonblank.map(frame=>frame.quote_age_ms)),expired_receipt_current_book_frames:nonblank.filter(frame=>frame.kind==='bbo-midpoint'&&frame.quote_age_ms>5000&&frame.current_books_mask&16).length,frame_gap_ms:gaps.length?{min:Math.min(...gaps),max:Math.max(...gaps),mean:gaps.reduce((a,b)=>a+b,0)/gaps.length}:null},guard_rejected_quotes:rejected.length,raw_artifacts:'Ignored test/.build/anti-repeat-probe retains every input event/command and every output quote/frame. Re-run this script to regenerate.',visible_output_verified:false};
report.pass=results.every(result=>result.tls_authorized&&result.upgrade_status===101&&!result.errors.length&&!result.unexpected_disconnect)&&!independentAba&&!report.selector.firmware_policy_reversals&&!report.selector.blank_frames&&report.selector.frames>=Math.floor((durationMs-warmupMs)/100)-20;
report.coinbase_product_tickers=productSummaries;
report.validation=validation;
report.selector.expired_receipt_current_converted_book_frames=nonblank.filter(frame=>frame.kind==='converted-bbo-midpoint'&&frame.quote_age_ms>5000&&(frame.current_converted_books_mask&16)).length;
report.selector.displayed_quote_kinds=[...new Set(nonblank.map(frame=>frame.kind))];
report.raw_artifacts=`Ignored ${path.relative(process.cwd(),directory)} retains every input event/command and every output quote/frame. Re-run this script to regenerate.`;
report.scope=`${providers.length} computer TLS connection(s) with embedded roots and exact firmware upgrade headers. Actual C++ StreamParsers, PriceGuard, QuoteSequence and QuoteBook; modeled frame fallback/liveness, no ESP32/device/SPI acceptance.`;
report.pass=report.pass&&!validation.frames_not_traceable_to_real_accepted_quote&&!validation.incorrect_rounding_frames&&!validation.all_observed_distinct_sequence_ABA;
if(crossBooks){
  const measuredCross=crossFrames.filter(frame=>frame.at_ms>=warmupMs);
  report.scope='Computer probe: three bound timestamped Bitfinex depth-one books on one socket plus Coinbase on one socket. Actual C++ firmware parsers, guard and sequence selector consume direct and converted BBO ticks; independent JS level reconstruction measures leg validity/diversity. Frame fallback/liveness orchestration is modeled; no ESP32/device/SPI acceptance.';
  report.quote_kinds=Object.fromEntries([...new Set(quotes.map(quote=>quote.kind))].map(kind=>[kind,quotes.filter(quote=>quote.kind===kind).length]));
  report.cross_book_feasibility={frames:measuredCross.length,all_three_current_distinct_rounded_frames:measuredCross.filter(frame=>frame.distinct_rounded_prices===3).length,minimum_distinct_rounded_prices:Math.min(...measuredCross.map(frame=>frame.distinct_rounded_prices)),distinct_converted_rounded_values:new Set(measuredCross.filter(frame=>frame.converted_midpoint!==null).map(frame=>Math.floor(frame.converted_midpoint+.5))).size,book_results:Object.fromEntries(Object.entries(books).map(([symbol,book])=>[symbol,{channel:book.channel,updates:book.updates,valid_bbo_quotes:book.quotes,distinct_midpoints:book.distinct.size,heartbeats:book.heartbeats,final_bid:book.bid,final_ask:book.ask,max_incoming_publication_age_ms:book.maxIncomingPublicationAgeMs,max_observed_receipt_age_ms:Math.max(...measuredCross.map(frame=>frame.leg_receipt_age_ms[symbol]??0)),max_observed_publication_age_ms:Math.max(...measuredCross.map(frame=>frame.leg_publication_age_ms[symbol]??0))}]))};
  report.pass=report.pass&&Object.keys(books).length===3&&report.cross_book_feasibility.all_three_current_distinct_rounded_frames>=.95*measuredCross.length;
  fs.writeFileSync(path.join(directory,'cross_frames.jsonl'),crossFrames.map(frame=>JSON.stringify(frame)).join('\n')+'\n');
}
fs.writeFileSync(path.join(directory,'events.jsonl'),events.map(item=>JSON.stringify(item)).join('\n')+'\n');
fs.writeFileSync(path.join(directory,'commands.txt'),commands.join('\n')+'\n');
fs.writeFileSync(path.join(directory,'frames.jsonl'),frames.map(item=>JSON.stringify(item)).join('\n')+'\n');
fs.writeFileSync(path.join(directory,'quotes.jsonl'),quotes.map(item=>JSON.stringify(item)).join('\n')+'\n');
fs.writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report,null,2));if(!report.pass)process.exitCode=1;
