import fs from 'node:fs';
import {performance} from 'node:perf_hooks';
import {spawn,execFileSync} from 'node:child_process';
import readline from 'node:readline';
const directory='test/.build/device-feeds';fs.mkdirSync(directory,{recursive:true});
execFileSync(process.env.CXX??'c++',['-std=c++11','-Wall','-Wextra','-Werror','-Iinclude','tools/quote_book_probe.cpp','-o',`${directory}/quote-book-probe`]);
const selector=spawn(`${directory}/quote-book-probe`),frames=[];
readline.createInterface({input:selector.stdout}).on('line',line=>frames.push(JSON.parse(line)));
const feeds=[
  {source:1,name:'kraken',url:'wss://ws.kraken.com/v2',subscription:{method:'subscribe',params:{channel:'ticker',symbol:['BTC/USD'],event_trigger:'bbo',snapshot:true}}},
  {source:0,name:'coinbase',url:'wss://ws-feed.exchange.coinbase.com',subscription:{type:'subscribe',product_ids:['BTC-USD'],channels:['ticker','heartbeat']}},
  {source:2,name:'bitstamp',url:'wss://ws.bitstamp.net',subscription:{event:'bts:subscribe',data:{channel:'live_trades_btcusd'}}},
  {source:4,name:'bitfinex',url:'wss://api-pub.bitfinex.com/ws/2',subscription:{event:'subscribe',channel:'trades',symbol:'tBTCUSD'}},
];
const started=performance.now(),sockets=[],reports={};
for(const feed of feeds){
  const stats=reports[feed.name]={quotes:0,distinct_prices:0,max_gap_ms:0,last_at:null,timestamps:0,errors:[]};
  const prices=new Set(),socket=new WebSocket(feed.url);sockets.push(socket);let channel=0,lastTrade=0;
  socket.addEventListener('open',()=>socket.send(JSON.stringify(feed.subscription)));
  socket.addEventListener('message',event=>{
    let message;try{message=JSON.parse(event.data);}catch{return;}
    if(feed.source===4&&message.event==='subscribed'&&message.symbol==='tBTCUSD'&&message.channel==='trades'){
      channel=message.chanId;fs.writeFileSync(`${directory}/bitfinex_subscription.json`,JSON.stringify(message,null,2)+'\n');return;
    }
    let value,timestamp,trade=0;
    if(feed.source===1&&message.channel==='ticker'&&message.data?.[0]?.symbol==='BTC/USD'){
      value=Number(message.data[0].last);timestamp=Date.parse(message.data[0].timestamp);
    }else if(feed.source===0&&message.type==='ticker'&&message.product_id==='BTC-USD'){
      value=Number(message.price);timestamp=Date.parse(message.time);
    }else if(feed.source===2&&message.event==='trade'&&message.channel==='live_trades_btcusd'){
      value=Number(message.data.price);timestamp=Number(message.data.microtimestamp)/1000;trade=message.data.id;
    }else if(feed.source===4&&channel&&message[0]===channel&&message[1]==='te'){
      value=Number(message[2][3]);timestamp=Number(message[2][1]);trade=message[2][0];
    }else return;
    if(!(value>0)||!Number.isFinite(timestamp)||Date.now()-timestamp>120000||timestamp-Date.now()>10000)return;
    if(trade&&trade<=lastTrade)return;lastTrade=trade;
    if(!stats.quotes){fs.writeFileSync(`${directory}/${feed.name}.json`,JSON.stringify(message,null,2)+'\n');console.log(feed.name,'first quote',value);}
    const at=Math.floor(performance.now()-started);if(stats.last_at!==null)stats.max_gap_ms=Math.max(stats.max_gap_ms,at-stats.last_at);
    stats.last_at=at;stats.quotes++;stats.timestamps++;prices.add(value);stats.distinct_prices=prices.size;
    selector.stdin.write(`Q ${at} ${feed.source} ${value} ${Math.floor(timestamp/1000)}\n`);
  });
  socket.addEventListener('error',event=>stats.errors.push(event.message??'WebSocket failure'));
}
const sample=setInterval(()=>selector.stdin.write(`F ${Math.floor(performance.now()-started)}\n`),500);
const progress=setInterval(()=>console.log(JSON.stringify(reports)),10000);
await new Promise(resolve=>setTimeout(resolve,60000));clearInterval(progress);clearInterval(sample);
for(const socket of sockets)socket.close();selector.stdin.end();await new Promise(resolve=>selector.on('exit',resolve));
const measured=frames.filter(frame=>frame.at>=10000),changes=measured.slice(1).filter((f,i)=>f.digits!==measured[i].digits).length;
const report={checked_at_utc:new Date().toISOString(),duration_ms:performance.now()-started,feeds:reports,
  selector:'actual firmware QuoteBook and PriceGuard, compiled C++',warmup_ms:10000,interval_ms:500,frames:measured.length,
  displayed_sources:[...new Set(measured.map(f=>f.source))],whole_dollar_changes:changes,
  whole_dollar_motion_ratio:changes/Math.max(1,measured.length-1),max_selected_quote_age_ms:Math.max(0,...measured.map(f=>f.quote_age_ms))};
fs.writeFileSync(`${directory}/frames.json`,JSON.stringify(frames,null,2)+'\n');fs.writeFileSync(`${directory}/report.json`,JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
if(!Object.values(reports).every(s=>s.quotes>0&&s.timestamps===s.quotes&&!s.errors.length)||measured.length<90||report.displayed_sources.length<2||report.max_selected_quote_age_ms>5000)process.exitCode=1;
