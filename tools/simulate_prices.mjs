#!/usr/bin/env node
// Standalone live-network experiment. No ESP32, API key, or npm dependency.
import https from 'node:https';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {performance} from 'node:perf_hooks';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sleep = ms => new Promise(resolve => setTimeout(resolve, Math.max(0, ms)));
const price = value => {
  if (typeof value !== 'number' && typeof value !== 'string') throw Error('missing price');
  if (typeof value === 'string' && !/^[0-9]+(?:\.[0-9]+)?$/.test(value)) throw Error('invalid price string');
  const n = Number(value);
  if (!Number.isFinite(n) || n <= 0 || n > 1e9) throw Error('invalid price');
  return n;
};
const requireValue = (condition, text) => { if (!condition) throw Error(text); };

export function parseRest(name, data) {
  let value, timestamp = 0, id, fx;
  switch (name) {
    case 'Coinbase': value=data.price; timestamp=Date.parse(data.time); id=data.trade_id; break;
    case 'Kraken':
      requireValue(Array.isArray(data.error) && !data.error.length, 'Kraken API error');
      value=data.result?.XXBTZUSD?.c?.[0]; fx=price(data.result?.USDTZUSD?.c?.[0]); break;
    case 'Bitstamp': value=data.last; timestamp=Number(data.timestamp)*1000; break;
    case 'Gemini': requireValue(data.symbol==='BTCUSD', 'wrong symbol'); value=data.close; break;
    case 'Bitfinex': requireValue(Array.isArray(data) && data.length>=10, 'wrong ticker'); value=data[6]; break;
    case 'Binance': case 'MEXC': requireValue(data.symbol==='BTCUSDT', 'wrong symbol'); value=data.price; break;
    case 'OKX':
      requireValue(data.code==='0' && data.data?.length===1 && data.data[0].instId==='BTC-USDT' && data.data[0].instType==='SPOT', 'wrong ticker');
      value=data.data[0].last; timestamp=Number(data.data[0].ts); break;
    case 'KuCoin': requireValue(data.code==='200000', 'KuCoin API error'); value=data.data?.price; timestamp=Number(data.data?.time); break;
    case 'Gate': requireValue(data.length===1 && data[0].currency_pair==='BTC_USDT', 'wrong ticker'); value=data[0].last; break;
    case 'Bitget':
      requireValue(data.code==='00000' && data.data?.length===1 && data.data[0].symbol==='BTCUSDT', 'wrong ticker');
      value=data.data[0].lastPr; timestamp=Number(data.data[0].ts); break;
    case 'Bybit':
      requireValue(data.retCode===0 && data.result?.list?.[0]?.symbol==='BTCUSDT', 'Bybit API error');
      value=data.result.list[0].lastPrice; timestamp=Number(data.time); break;
    default: throw Error('unknown provider');
  }
  if (['Coinbase','Bitstamp','OKX','KuCoin','Bitget','Bybit'].includes(name)) requireValue(Number.isFinite(timestamp) && timestamp>0, 'missing timestamp');
  return {raw:price(value), timestamp, id, fx};
}

export const streams = [
  {name:'Coinbase', url:'wss://ws-feed.exchange.coinbase.com', subscribe:{type:'subscribe', product_ids:['BTC-USD'], channels:['ticker','heartbeat']}},
  {name:'Kraken', url:'wss://ws.kraken.com/v2', subscribe:{method:'subscribe', params:{channel:'trade', symbol:['BTC/USD','USDT/USD'], snapshot:false}}},
  {name:'Bitstamp', url:'wss://ws.bitstamp.net', subscribe:{event:'bts:subscribe', data:{channel:'live_trades_btcusd'}}},
  {name:'Bitfinex', url:'wss://api-pub.bitfinex.com/ws/2', subscribe:{event:'subscribe', channel:'trades', symbol:'tBTCUSD'}},
  {name:'Binance', url:'wss://data-stream.binance.vision/ws/btcusdt@aggTrade'},
  {name:'OKX', url:'wss://ws.okx.com:8443/ws/v5/public', subscribe:{op:'subscribe', args:[{channel:'trades', instId:'BTC-USDT'}]}, ping:'ping'},
  {name:'Gate', url:'wss://api.gateio.ws/ws/v4/', subscribe:{channel:'spot.trades', event:'subscribe', payload:['BTC_USDT']}, ping:{channel:'spot.ping'}},
  {name:'Bitget', url:'wss://ws.bitget.com/v2/ws/public', subscribe:{op:'subscribe', args:[{instType:'SPOT', channel:'trade', instId:'BTCUSDT'}]}, ping:'ping'},
];

export function parseStream(name, data) {
  switch (name) {
    case 'Coinbase': return data.type==='ticker' && data.product_id==='BTC-USD' ? [{raw:price(data.price), timestamp:Date.parse(data.time), id:String(data.trade_id)}] : [];
    case 'Kraken': return data.channel==='trade' && data.type==='update' ? data.data.map(t => {
      requireValue(['BTC/USD','USDT/USD'].includes(t.symbol), 'wrong Kraken symbol');
      return {raw:price(t.price), timestamp:Date.parse(t.timestamp), id:String(t.trade_id), fxOnly:t.symbol==='USDT/USD'};
    }) : [];
    case 'Bitstamp': return data.event==='trade' && data.channel==='live_trades_btcusd' ? [{raw:price(data.data.price), timestamp:Number(data.data.microtimestamp)/1000, id:String(data.data.id)}] : [];
    case 'Bitfinex': return Array.isArray(data) && data[1]==='te' ? [{raw:price(data[2][3]), timestamp:Number(data[2][1]), id:String(data[2][0])}] : [];
    case 'Binance': return data.e==='aggTrade' && data.s==='BTCUSDT' ? [{raw:price(data.p), timestamp:Number(data.T), id:String(data.a)}] : [];
    case 'OKX': return data.arg?.channel==='trades' && data.arg.instId==='BTC-USDT' && Array.isArray(data.data) ? data.data.map(t => ({raw:price(t.px), timestamp:Number(t.ts), id:String(t.tradeId)})) : [];
    case 'Gate': return data.channel==='spot.trades' && data.event==='update' && data.result?.currency_pair==='BTC_USDT' ? [{raw:price(data.result.price), timestamp:Number(data.result.create_time_ms), id:String(data.result.id)}] : [];
    case 'Bitget': return data.action==='update' && data.arg?.channel==='trade' && data.arg.instId==='BTCUSDT' && data.arg.instType==='SPOT' ? data.data.map(t => ({raw:price(t.price), timestamp:Number(t.ts), id:String(t.tradeId)})) : [];
    default: return [];
  }
}

export function freshTimestamp(timestamp, now=Date.now(), maxAge=120000) {
  return Number.isFinite(timestamp) && timestamp>0 && timestamp<=now+10000 && timestamp>=now-maxAge;
}
export function apiRateLimited(data) {
  const code=String(data?.code??data?.retCode??'');
  return ['50011','429000','429','10006'].includes(code) || /rate.?limit|throttled|too many requests/i.test(JSON.stringify(data?.error??data?.msg??data?.retMsg??''));
}
export class PriceGuard {
  value=0; acceptedAt=0; pending=null;
  accept(value, source, now) {
    if (this.value && now-this.acceptedAt<600000 && Math.abs(value/this.value-1)<=0.05) {
      this.value=value; this.acceptedAt=now; this.pending=null; return true;
    }
    if (this.pending && this.pending.source!==source && now-this.pending.at<=60000 && Math.abs(value/this.pending.value-1)<=0.01) {
      this.value=value; this.acceptedAt=now; this.pending=null; return true;
    }
    this.pending={value,source,at:now}; return false;
  }
}
export function percentile(values, fraction) {
  if (!values.length) return null;
  const sorted=[...values].sort((a,b)=>a-b);
  return Math.round(sorted[Math.min(sorted.length-1, Math.ceil(fraction*sorted.length)-1)]*1000)/1000;
}
export function summarizeSamples(samples, targetMs) {
  const gaps=samples.slice(1).map((s,i)=>s.at_ms-samples[i].at_ms);
  return {
    samples:samples.length,
    samples_with_new_accepted_quote:samples.filter(s=>s.new_quotes>0).length,
    samples_with_receipt_age_within_target:samples.filter(s=>s.receipt_age_ms!==null && s.receipt_age_ms<=targetMs).length,
    samples_with_new_trade:samples.filter(s=>s.new_trades>0).length,
    whole_dollar_changes:samples.filter(s=>s.digits_changed).length,
    sample_gap_ms:{p50:percentile(gaps,.5), p95:percentile(gaps,.95), max:percentile(gaps,1)},
    quote_receipt_age_ms:{p50:percentile(samples.filter(s=>s.receipt_age_ms!==null).map(s=>s.receipt_age_ms),.5), p95:percentile(samples.filter(s=>s.receipt_age_ms!==null).map(s=>s.receipt_age_ms),.95), max:percentile(samples.filter(s=>s.receipt_age_ms!==null).map(s=>s.receipt_age_ms),1)},
    target_met:samples.length>1 && gaps.every(g=>g<=targetMs) && samples.every(s=>s.new_quotes>0 && s.receipt_age_ms!==null && s.receipt_age_ms<=targetMs),
  };
}

function sources() {
  // Read the firmware's real endpoints and cooldowns; don't invent a faster roster.
  const text=fs.readFileSync(path.join(root,'include/PriceSources.h'),'utf8');
  const entries=[...text.matchAll(/\{Provider::\w+,\s*"([^"]+)",\s*"([^"]+)",\s*(true|false),\s*(\d+)\}/g)];
  requireValue(entries.length>=10, 'could not load firmware source list');
  return entries.map(m=>({name:m[1],url:m[2],usdt:m[3]==='true',minimum_ms:Number(m[4])})).concat([
    {name:'Bybit',url:'https://api.bybit.com/v5/market/tickers?category=spot&symbol=BTCUSDT',usdt:true,minimum_ms:5000},
  ]);
}

function request(source, agent, timeoutMs) {
  return new Promise((resolve,reject)=>{
    const req=https.get(source.url,{agent, headers:{'User-Agent':'BitcoinClock-Benchmark/1.0','Accept':'application/json','Accept-Encoding':'identity','Cache-Control':'no-cache'}}, response=>{
      let size=0;const chunks=[];
      response.on('data',chunk=>{size+=chunk.length;if(size>16384)req.destroy(Error('oversized response'));else chunks.push(chunk);});
      response.on('error',reject);
      response.on('end',()=>resolve({status:response.statusCode,headers:response.headers,body:Buffer.concat(chunks).toString('utf8')}));
    });
    const timer=setTimeout(()=>req.destroy(Error('request deadline exceeded')),timeoutMs);
    req.once('error',reject);req.once('close',()=>clearTimeout(timer));
  });
}

export async function run(options) {
  const roster=sources(), started=performance.now(), startedUtc=new Date().toISOString();
  if(options.policy==='fast')for(const source of roster) {
    // <=1 request/s/provider: below all documented public limits. Keep the
    // ambiguous MEXC allowance conservative; don't use blocked Bybit as capacity.
    if(!['MEXC','Bybit'].includes(source.name))source.minimum_ms=1000;
  }
  const now=()=>performance.now()-started;
  const directory=path.resolve(options.output);fs.mkdirSync(directory,{recursive:true});
  const eventLog=fs.createWriteStream(path.join(directory,`${options.mode}.events.jsonl`));
  const sampleLog=fs.createWriteStream(path.join(directory,`${options.mode}.samples.jsonl`));
  const provider=Object.fromEntries(roster.map(s=>[s.name,{rest_attempts:0,rest_success:0,rest_errors:0,http_statuses:{},latencies:[],rest_distinct_prices:new Set(),ws_connections:0,ws_messages:0,ws_quotes:0,ws_errors:0,rejected:0,duplicates:0,available:0,busy:false,failures:0,min_request_gap_ms:null,last_request_at:null,lastError:null}]));
  const agents=Object.fromEntries(roster.map(s=>[s.name,new https.Agent({keepAlive:options.mode!=='baseline',maxSockets:1,ca:fs.readFileSync(path.join(root,'data/cert/mozilla.pem'))})]));
  const guard=new PriceGuard(), seen=new Map(), samples=[], accepted=[];
  let fx=0,fxAt=-Infinity,last=null,counter=0,trades=0,sampledCounter=0,sampledTrades=0,lastDigits=null,stopping=false,cursor=0,inflight=0;
  const write=(type,data)=>eventLog.write(JSON.stringify({type,at_ms:Math.round(now()*1000)/1000,...data})+'\n');
  const accept=(source,quote,transport)=>{
    const state=provider[source.name];
    if (quote.timestamp && !freshTimestamp(quote.timestamp)) {state.rejected++;write('rejected',{source:source.name,transport,reason:'stale/future trade timestamp',quote});return;}
    if (transport==='websocket' && !freshTimestamp(quote.timestamp)) {state.rejected++;return;}
    if (quote.fxOnly) {if(quote.raw>=.5&&quote.raw<=1.5){fx=quote.raw;fxAt=now();}return;}
    if (quote.fx>=.5 && quote.fx<=1.5) {fx=quote.fx;fxAt=now();}
    if (source.usdt && (!fx || now()-fxAt>120000)) {state.rejected++;write('rejected',{source:source.name,transport,reason:'USDT/USD conversion unavailable'});return;}
    let isNewTrade=false;
    if (quote.id!==undefined || quote.timestamp) {
      const identity=quote.id!==undefined?String(quote.id):String(quote.timestamp);
      const key=`${source.name}:${transport}`;
      if (seen.get(key)===identity) {state.duplicates++;if(transport==='websocket')return;}
      else {seen.set(key,identity);isNewTrade=transport==='websocket';}
    }
    const usd=quote.raw*(source.usdt?fx:1),at=now();
    if (!guard.accept(usd,source.name,at)) {state.rejected++;write('rejected',{source:source.name,transport,reason:'independent confirmation pending',usd});return;}
    counter++;if(isNewTrade)trades++;
    last={source:source.name,transport,price_usd:usd,raw:quote.raw,currency:source.usdt?'USDT':'USD',fx:source.usdt?fx:null,exchange_timestamp_ms:quote.timestamp||null,received_at_ms:at};
    accepted.push({at,transport});write('quote',{...last,new_trade:isNewTrade,id:quote.id});
  };
  const choose=()=>{
    for(let i=0;i<roster.length;i++){const s=roster[cursor++%roster.length],state=provider[s.name];if(!state.busy&&now()>=state.available)return s;}
    return null;
  };
  const fetch=async source=>{
    const state=provider[source.name],begin=now();inflight++;state.busy=true;
    if(state.last_request_at!==null){const gap=begin-state.last_request_at;state.min_request_gap_ms=state.min_request_gap_ms===null?gap:Math.min(state.min_request_gap_ms,gap);}
    state.last_request_at=begin;state.available=begin+source.minimum_ms;state.rest_attempts++;
    try {
      const reply=await request(source,agents[source.name],6000);
      state.http_statuses[reply.status]=(state.http_statuses[reply.status]||0)+1;
      write('http',{source:source.name,status:reply.status,elapsed_ms:now()-begin,headers:Object.fromEntries(Object.entries(reply.headers).filter(([key])=>['date','age','cache-control','retry-after'].includes(key)||/rate.?limit|used-weight/.test(key)))});
      if(reply.status!==200) {
        const error=Error(`HTTP ${reply.status}`);error.status=reply.status;
        const retry=reply.headers['retry-after'];error.retry_ms=/^\d+$/.test(retry||'')?Number(retry)*1000:Math.max(0,Date.parse(retry)-Date.now())||0;throw error;
      }
      requireValue(reply.headers['content-type']?.startsWith('application/json'),'unexpected content type');
      requireValue(freshTimestamp(Date.parse(reply.headers.date),Date.now(),20000),'stale/missing HTTP Date');
      requireValue(Number(reply.headers.age||0)<=15,'stale HTTP Age');
      const data=JSON.parse(reply.body);
      if(apiRateLimited(data)){const error=Error('API rate limit');error.status=429;throw error;}
      const quote=parseRest(source.name,data);
      state.rest_success++;state.failures=0;state.latencies.push(now()-begin);state.rest_distinct_prices.add(quote.raw);
      accept(source,quote,'rest');
    } catch(error) {
      state.rest_errors++;state.failures++;state.lastError=error.message;
      let backoff=Math.min(600000,5000*2**Math.min(7,state.failures-1));
      if([429,418].includes(error.status))backoff=Math.max(backoff,60000);
      if([401,403,404,451].includes(error.status))backoff=Math.max(backoff,3600000);
      state.available=Math.max(state.available,now()+backoff,now()+(error.retry_ms||0));
      write('error',{source:source.name,transport:'rest',error:error.message,next_try_ms:state.available});
    } finally {state.busy=false;inflight--;}
  };
  const sockets=new Set(),wsTasks=[];let signalStop;
  const stopSignal=new Promise(resolve=>{signalStop=resolve;});
  if(options.mode==='stream')for(const feed of streams){
    const source=roster.find(s=>s.name===feed.name);
    wsTasks.push((async()=>{
      let failures=0;
      while(!stopping){
        let received=false;
        await new Promise(resolve=>{
          const state=provider[feed.name],ws=new WebSocket(feed.url);sockets.add(ws);let finish=false;
          const close=()=>{if(finish)return;finish=true;clearTimeout(connectTimer);clearInterval(pingTimer);sockets.delete(ws);resolve();};
          const connectTimer=setTimeout(()=>{state.ws_errors++;state.lastError='WebSocket connection deadline exceeded';write('error',{source:feed.name,transport:'websocket',error:state.lastError});ws.close();close();},10000);
          const pingTimer=setInterval(()=>{if(ws.readyState===WebSocket.OPEN&&feed.ping){const body=typeof feed.ping==='string'?feed.ping:JSON.stringify({...feed.ping,time:Math.floor(Date.now()/1000)});ws.send(body);}},20000);
          ws.addEventListener('open',()=>{clearTimeout(connectTimer);state.ws_connections++;write('connected',{source:feed.name,url:feed.url});if(feed.subscribe)ws.send(JSON.stringify({...feed.subscribe,...(feed.name==='Gate'?{time:Math.floor(Date.now()/1000)}:{})}));});
          ws.addEventListener('message',event=>{
            state.ws_messages++;if(event.data==='pong')return;
            try {
              const data=JSON.parse(event.data);
              if(data.event==='error'||data.success===false||data.error){state.ws_errors++;write('error',{source:feed.name,transport:'websocket',error:JSON.stringify(data)});ws.close();return;}
              for(const quote of parseStream(feed.name,data)){received=true;if(!quote.fxOnly)state.ws_quotes++;accept(source,quote,'websocket');}
            }catch(error){state.ws_errors++;write('error',{source:feed.name,transport:'websocket',error:error.message});}
          });
          ws.addEventListener('error',event=>{state.ws_errors++;state.lastError=event.error?.message||event.message||'WebSocket connection failed';write('error',{source:feed.name,transport:'websocket',error:state.lastError});close();});
          ws.addEventListener('close',event=>{if(!stopping)write('disconnected',{source:feed.name,code:event.code,reason:event.reason});close();});
        });
        if(stopping)break;
        failures=received?0:Math.min(6,failures+1);await Promise.race([sleep(Math.min(60000,5000*2**failures)),stopSignal]);
      }
    })());
  }
  // A 400 ms sample cadence gives timer jitter room below the requested 500 ms.
  const sampleMs=Math.min(options.targetMs*.8,400),warmupMs=Math.min(10000,options.seconds*1000/4);
  let nextSample=sampleMs,nextRequest=0,nextPrint=10000;
  console.log(`${options.mode}: ${roster.length} REST candidates, ${options.mode==='stream'?streams.length:0} stream candidates; ${options.seconds}s, target ${options.targetMs}ms; TLS verified.`);
  while(now()<options.seconds*1000){
    if(now()>=nextSample){
      const at=now(),digits=last?Math.round(last.price_usd):null;
      const sample={at_ms:at,price_usd:last?.price_usd??null,source:last?.source??null,transport:last?.transport??null,receipt_age_ms:last?at-last.received_at_ms:null,exchange_age_ms:last?.exchange_timestamp_ms?Date.now()-last.exchange_timestamp_ms:null,new_quotes:counter-sampledCounter,new_trades:trades-sampledTrades,digits_changed:digits!==null&&lastDigits!==null&&digits!==lastDigits,warmup:at<warmupMs};
      sampleLog.write(JSON.stringify(sample)+'\n');if(!sample.warmup)samples.push(sample);
      sampledCounter=counter;sampledTrades=trades;lastDigits=digits;nextSample+=sampleMs;
      if(now()>nextSample+sampleMs)nextSample=now()+sampleMs; // Never invent catch-up samples.
    }
    if(now()>=nextPrint){console.log(`  ${Math.floor(now()/1000)}s: ${counter} accepted quotes, ${trades} new streamed trades; latest ${last?.source??'waiting'} ${last?.price_usd.toFixed(2)??''}, age ${last?Math.round(now()-last.received_at_ms):'-'}ms`);nextPrint+=10000;}
    if(now()>=nextRequest && inflight<(options.mode==='baseline'?1:4)){
      const source=choose();
      if(source){nextRequest=now()+(options.mode==='baseline'?options.targetMs:options.policy==='fast'?100:250);void fetch(source);}
    }
    await sleep(Math.min(5,Math.max(1,nextSample-now())));
  }
  stopping=true;signalStop();for(const ws of sockets)ws.close();
  // Do not count replies received after the measurement window in its results.
  const measuredEnd=now(),lastMeasuredQuote=last;while(inflight)await sleep(10);
  await Promise.all(wsTasks);for(const agent of Object.values(agents))agent.destroy();
  eventLog.end();sampleLog.end();await Promise.all([new Promise(r=>eventLog.on('finish',r)),new Promise(r=>sampleLog.on('finish',r))]);
  const measuredQuotes=accepted.filter(e=>e.at>=warmupMs&&e.at<measuredEnd),quoteGaps=measuredQuotes.slice(1).map((q,i)=>q.at-measuredQuotes[i].at);
  const report={mode:options.mode,policy:options.policy,started_at_utc:startedUtc,duration_ms:measuredEnd,warmup_ms:warmupMs,target_ms:options.targetMs,sample_period_ms:sampleMs,
    firmware_transport_difference:'Node REST uses HTTP/1.1; baseline disables connection reuse and serializes requests. This measures a computer, not ESP32 TLS/CPU/Wi-Fi.',
    rest_policy:`${options.policy==='fast'?'At most 1 request/s/provider (MEXC/Bybit 5s)':'Firmware per-provider cooldowns (5 seconds)'}; max ${options.mode==='baseline'?1:4} in flight; one request starts per ${options.mode==='baseline'?options.targetMs:options.policy==='fast'?100:250}ms. Retry-After/backoff honored.`,
    successful_rest_sources:roster.filter(s=>provider[s.name].rest_success).map(s=>s.name),successful_stream_sources:streams.filter(s=>provider[s.name].ws_quotes).map(s=>s.name),
    accepted_quotes:measuredQuotes.length,accepted_quotes_per_second:measuredQuotes.length/((measuredEnd-warmupMs)/1000),quote_gap_ms:{p50:percentile(quoteGaps,.5),p95:percentile(quoteGaps,.95),max:percentile(quoteGaps,1)},
    display:summarizeSamples(samples,options.targetMs),last_quote:lastMeasuredQuote,
    providers:Object.fromEntries(Object.entries(provider).map(([name,s])=>[name,{...s,minimum_interval_ms:roster.find(p=>p.name===name).minimum_ms,rest_distinct_prices:s.rest_distinct_prices.size,latencies:undefined,rest_latency_ms:{p50:percentile(s.latencies,.5),p95:percentile(s.latencies,.95),max:percentile(s.latencies,1)}}])),
  };
  fs.writeFileSync(path.join(directory,`${options.mode}.report.json`),JSON.stringify(report,null,2)+'\n');
  for(const [name,s] of Object.entries(report.providers))console.log(`${name.padEnd(9)} REST ${s.rest_success}/${s.rest_attempts}, p95 ${s.rest_latency_ms.p95??'-'}ms; WS ${s.ws_quotes} ticks; errors ${s.rest_errors+s.ws_errors}${s.lastError?`: ${s.lastError}`:''}`);
  console.log(JSON.stringify({mode:report.mode,quotes_per_second:report.accepted_quotes_per_second,quote_gap_ms:report.quote_gap_ms,display:report.display},null,2));
  return report;
}

if(process.argv[1] && path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const options={mode:'stream',policy:'fast',seconds:120,targetMs:500,output:path.join(root,'test/.build/price-benchmark')};
  for(let i=2;i<process.argv.length;i+=2){const key=process.argv[i],value=process.argv[i+1];if(key==='--mode')options.mode=value;else if(key==='--policy')options.policy=value;else if(key==='--seconds')options.seconds=Number(value);else if(key==='--interval-ms')options.targetMs=Number(value);else if(key==='--output')options.output=value;else throw Error(`unknown option ${key}`);}
  requireValue(['baseline','rest','stream'].includes(options.mode),'mode must be baseline, rest or stream');
  requireValue(['firmware','fast'].includes(options.policy),'policy must be firmware or fast');
  requireValue(options.seconds>=10&&options.seconds<=3600&&options.targetMs>=100&&options.targetMs<=3600000,'invalid duration/interval');
  run(options).catch(error=>{console.error(error);process.exitCode=1;});
}
