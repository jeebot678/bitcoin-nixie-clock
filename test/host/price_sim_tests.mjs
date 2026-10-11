import assert from 'node:assert/strict';
import fs from 'node:fs';
import {parseRest, parseStream, freshTimestamp, apiRateLimited, PriceGuard, summarizeSamples} from '../../tools/simulate_prices.mjs';

const names=['Coinbase','Kraken','Bitstamp','Gemini','Bitfinex','Binance','OKX','KuCoin','Gate','Bitget','MEXC'];
const files=['coinbase','kraken','bitstamp','gemini','bitfinex','binance','okx','kucoin','gate','bitget','mexc'];
for(let i=0;i<names.length;i++){
  const fixture=JSON.parse(fs.readFileSync(`test/live_fixtures/${files[i]}.json`));
  assert.ok(parseRest(names[i],fixture).raw>0);
}
assert.throws(()=>parseRest('Binance',{symbol:'ETHUSDT',price:'80000'}));
for(const invalid of [undefined,null,true,'','NaN','Infinity','10junk','-2',0,Infinity])assert.throws(()=>parseRest('Binance',{symbol:'BTCUSDT',price:invalid}));
assert.throws(()=>parseRest('Coinbase',{price:'80000',time:'invalid'}));
const now=Date.now(),iso=new Date(now).toISOString();
const cases=[
  ['Coinbase',{type:'ticker',product_id:'BTC-USD',price:'80000',time:iso,trade_id:1}],
  ['Kraken',{channel:'trade',type:'update',data:[{symbol:'BTC/USD',price:80000,timestamp:iso,trade_id:1}]}],
  ['Bitstamp',{event:'trade',channel:'live_trades_btcusd',data:{price:'80000',microtimestamp:String(now*1000),id:1}}],
  ['Bitfinex',[12,'te',[1,now,.01,80000]]],
  ['Binance',{e:'aggTrade',s:'BTCUSDT',p:'80000',T:now,a:1}],
  ['OKX',{arg:{channel:'trades',instId:'BTC-USDT'},data:[{px:'80000',ts:String(now),tradeId:'1'}]}],
  ['Gate',{channel:'spot.trades',event:'update',result:{currency_pair:'BTC_USDT',price:'80000',create_time_ms:String(now),id:1}}],
  ['Bitget',{action:'update',arg:{channel:'trade',instId:'BTCUSDT',instType:'SPOT'},data:[{price:'80000',ts:String(now),tradeId:'1'}]}],
];
for(const [name,message] of cases){const quotes=parseStream(name,message);assert.equal(quotes.length,1);assert.equal(quotes[0].raw,80000);assert.equal(quotes[0].timestamp,now);assert.equal(quotes[0].id,'1');}
assert.deepEqual(parseStream('OKX',{event:'subscribe',arg:{channel:'trades',instId:'BTC-USDT'}}),[]);
assert.deepEqual(parseStream('Bitfinex',[12,'tu',[1,now,.01,80000]]),[]); // Don't count both execution and update.
assert.deepEqual(parseStream('Bitget',{action:'snapshot',arg:{channel:'trade',instId:'BTCUSDT',instType:'SPOT'},data:[]}),[]);
assert.deepEqual(parseStream('Coinbase',{type:'heartbeat',product_id:'BTC-USD',time:iso}),[]);
assert.equal(parseStream('Kraken',{channel:'trade',type:'update',data:[{symbol:'USDT/USD',price:.999,timestamp:iso,trade_id:1}]})[0].fxOnly,true);
assert.throws(()=>parseStream('Kraken',{channel:'trade',type:'update',data:[{symbol:'ETH/USD',price:4000,timestamp:iso,trade_id:1}]}));
assert.equal(freshTimestamp(now,now),true);assert.equal(freshTimestamp(0,now),false);assert.equal(freshTimestamp(now-120001,now),false);assert.equal(freshTimestamp(now+10001,now),false);
for(const error of [{code:'50011'},{code:'429000'},{retCode:10006},{error:['EAPI:Rate limit exceeded']}])assert.equal(apiRateLimited(error),true);
assert.equal(apiRateLimited({code:'0',data:[]}),false);
const guard=new PriceGuard();assert.equal(guard.accept(80000,'Coinbase',0),false);assert.equal(guard.accept(80010,'Coinbase',100),false);assert.equal(guard.accept(80020,'Kraken',200),true);
assert.equal(guard.accept(81000,'Bitstamp',300),true);assert.equal(guard.accept(150000,'Gate',400),false);assert.equal(guard.value,81000);assert.equal(guard.accept(90000,'Coinbase',500),false);assert.equal(guard.accept(90010,'Kraken',600),true);
const samples=Array.from({length:10},(_,i)=>({at_ms:i*400,receipt_age_ms:100,new_quotes:1,new_trades:0,digits_changed:false}));
assert.equal(summarizeSamples(samples,500).target_met,true); // Fresh real responses can carry an unchanged price.
samples[3].receipt_age_ms=550;assert.equal(summarizeSamples(samples,500).target_met,false);samples[3].receipt_age_ms=100;
samples[5].new_quotes=0;assert.equal(summarizeSamples(samples,500).target_met,false);samples[5].new_quotes=1;
samples[9].at_ms+=101;assert.equal(summarizeSamples(samples,500).target_met,false);
console.log('PASS: standalone simulator parses all 11 REST fixtures and 8 stream schemas, validates freshness/identifiers, skips acknowledgments/snapshots, requires independent confirmation, and detects missed 500ms samples.');
