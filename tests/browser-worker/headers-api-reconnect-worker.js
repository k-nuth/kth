// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

self.onmessage=async({data})=>{
 try {
  importScripts('browser_headers_api_control.js');const core=await createHeaders();
  const client=new core.HeaderSync(data.bridge,'regtest',1500000);
  const first=await client.asyncRun();
  client.reset();
  const retained=client.lastHeight();
  const second=await client.asyncRun();
  await new Promise(resolve=>setTimeout(resolve,10));
  if(first!==3005n||retained!==first||second!==first||!client.idle())throw Error('Reconnect/retention failed');
  client.dispose();postMessage({type:'done',first:Number(first),second:Number(second),retained:Number(retained)});
 }catch(e){postMessage({type:'failure',message:String(e)});}
};
