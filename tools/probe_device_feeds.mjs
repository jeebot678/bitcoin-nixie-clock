import fs from 'node:fs';
import {performance} from 'node:perf_hooks';

const directory='test/.build/device-feeds';fs.mkdirSync(directory,{recursive:true});
const feeds=[
  {name:'kraken',url:'wss://ws.kraken.com/v2',subscription:{method:'subscribe',params:{channel:'ticker',symbol:['BTC/USD'],event_trigger:'bbo',snapshot:true}}},
  {name:'coinbase',url:'wss://ws-feed.exchange.coinbase.com',subscription:{type:'subscribe',product_ids:['BTC-USD'],channels:['ticker','heartbeat']}},
];
const started=performance.now(),sockets=[],reports={};
for(const feed of feeds){
  const stats=reports[feed.name]={quotes:0,distinct_prices:0,max_gap_ms:0,last_at:null,timestamps:0,errors:[]};
  const prices=new Set(),socket=new WebSocket(feed.url);sockets.push(socket);
  socket.addEventListener('open',()=>socket.send(JSON.stringify(feed.subscription)));
  socket.addEventListener('message',event=>{
    const message=JSON.parse(event.data);
    const quote=feed.name==='kraken'?message.channel==='ticker'?message.data?.[0]:null:message.type==='ticker'?message:null;
    if(!quote)return;
    const value=Number(feed.name==='kraken'?quote.last:quote.price),timestamp=feed.name==='kraken'?quote.timestamp:quote.time;
    if(!(value>0))return;
    if(stats.quotes===0){fs.writeFileSync(`${directory}/${feed.name}.json`,JSON.stringify(message,null,2)+'\n');console.log(feed.name,'first quote',value,'timestamp',timestamp??'absent');}
    const at=performance.now()-started;if(stats.last_at!==null)stats.max_gap_ms=Math.max(stats.max_gap_ms,at-stats.last_at);
    stats.last_at=at;stats.quotes++;if(timestamp)stats.timestamps++;prices.add(value);stats.distinct_prices=prices.size;
  });
  socket.addEventListener('error',event=>stats.errors.push(event.message??'WebSocket failure'));
}
const progress=setInterval(()=>console.log(JSON.stringify(reports)),10000);
await new Promise(resolve=>setTimeout(resolve,60000));clearInterval(progress);for(const socket of sockets)socket.close();
fs.writeFileSync(`${directory}/report.json`,JSON.stringify({checked_at_utc:new Date().toISOString(),duration_ms:performance.now()-started,feeds:reports},null,2)+'\n');
console.log(JSON.stringify(reports,null,2));
if(!Object.values(reports).every(s=>s.quotes>0&&s.timestamps===s.quotes&&!s.errors.length))process.exitCode=1;
