// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

self.onmessage=async({data})=>{
 const checks={};let client;
 try {
  importScripts('browser_headers_api_control.js');const core=await createHeaders();
  client=new core.HeaderSync(data.bridge,'regtest',1500000);
  const run=client.asyncRun();
  for(const [name,call] of [['dispose',()=>client.dispose()],['reset',()=>client.reset()]]){
   try{call();throw Error(name+' admitted during run');}catch(e){if(!String(e).includes('pending async'))throw e;checks[name]=true;}
  }
  try{await client.asyncRun();throw Error('second run admitted');}catch(e){if(!String(e).includes('pending async'))throw e;checks.secondRun=true;}
  checks.query=client.lastHeight()===0n;
  await client.asyncRequestStop();await run;checks.stopped=client.status()==='stopped';
  // A new event-loop turn checks executor cleanup after terminal delivery.
  await new Promise(resolve=>setTimeout(resolve,10));checks.idle=client.idle();
  client.reset();checks.resetAfterCompletion=true;client.dispose();checks.disposed=true;
  if(Object.values(checks).some(v=>v!==true))throw Error(JSON.stringify(checks));
  postMessage({type:'done',checks});
 }catch(e){postMessage({type:'failure',message:String(e),checks});}
};
