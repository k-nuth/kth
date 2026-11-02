"""Real-browser persistence controls against an operator-selected BCH WS peer."""
import argparse
import asyncio
import json
import tempfile
from pathlib import Path
from urllib.parse import urlsplit
from playwright.async_api import async_playwright

HERE = Path(__file__).resolve().parent

async def install_routes(context, args, quota=False):
    options = json.dumps(dict(type='start', bridge=args.peer, network=args.network, capacity=args.capacity))
    html = '''<!doctype html><script>
window.control={height:0,saved:0,restored:0,caughtUp:false,reset:false,unavailable:false,closed:false};
window.worker=new Worker('/worker.js');worker.onmessage=({data})=>{
 if(data.type==='progress'){control.height=data.height;control.hash=data.tipHash;}
 if(data.type==='storageSaved')control.saved=data.height;
 if(data.type==='storageRestored')control.restored=data.height;
 if(data.type==='caughtUp')control.caughtUp=true;
 if(data.type==='storageReset')control.reset=true;
 if(data.type==='storageUnavailable')control.unavailable=true;
 if(data.type==='failure')control.failure=data.message;
 if(data.type==='closed')control.closed=true;
};worker.postMessage(OPTIONS);
</script>'''.replace('OPTIONS', options)
    async def serve(route):
        name = urlsplit(route.request.url).path
        files = {
            '/worker.js': HERE/'headers-persistent-worker.js',
            '/header-storage.js': HERE/'header-storage.js',
            '/browser_headers_persistent_v1.js': args.module_dir/'browser_headers_api_control.js',
            '/browser_headers_persistent_v1.wasm': args.module_dir/'browser_headers_api_control.wasm',
        }
        if name == '/':
            await route.fulfill(body=html, content_type='text/html')
        elif name in files:
            if name == '/header-storage.js' and quota:
                code = files[name].read_text().replace('    sync(populate) {',
                    "    sync(populate) {\n        if (!populate) return Promise.reject(new DOMException('Test quota', 'QuotaExceededError'));")
                await route.fulfill(body=code, content_type='application/javascript')
            else:
                await route.fulfill(path=str(files[name]), content_type='application/wasm' if name.endswith('.wasm') else 'application/javascript')
        else:
            await route.abort()
    await context.route(args.origin+'/**', serve)

async def wait(page, condition):
    await page.wait_for_function('window.control && (control.failure || ('+condition+'))', timeout=180000)
    state = await page.evaluate('window.control')
    if state.get('failure'):
        raise RuntimeError(state)
    return state

async def stop(page):
    await page.evaluate("worker.postMessage({type:'stop'})")
    await wait(page, 'control.closed')

async def corrupt_cache(page, network):
    directory = '/kth-header-cache/'+network.capitalize()
    await page.evaluate('''async directory=>{
        const db=await new Promise((resolve,reject)=>{
            const r=indexedDB.open(directory);r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error);
        });
        try{await new Promise((resolve,reject)=>{
            const t=db.transaction('FILE_DATA','readwrite'),s=t.objectStore('FILE_DATA');
            const key=directory+'/chain-v1.bin',r=s.get(key);
            r.onsuccess=()=>{const v=r.result;if(!v?.contents||v.contents.length<160){t.abort();return;}
                v.contents[84]^=1;s.put(v,key);};
            t.oncomplete=resolve;t.onerror=()=>reject(t.error);t.onabort=()=>reject(Error('Missing cache'));
        });}finally{db.close();}
    }''', directory)

async def main(args):
    results = {}
    async with async_playwright() as playwright:
        with tempfile.TemporaryDirectory(prefix='kth-header-persistence-') as profile:
            context = await playwright.chromium.launch_persistent_context(profile, headless=True)
            try:
                await install_routes(context, args)
                page = await context.new_page()
                await page.goto(args.origin+'/')
                state = await wait(page, 'control.saved>0')
                saved = state['saved'];results['first_saved_height']=saved
                await page.reload()
                state = await wait(page, f'control.restored>={saved}')
                results['partial_restored_height']=state['restored']
                state = await wait(page, 'control.caughtUp')
                tip = state['height'];results['tip']=tip
                await wait(page, f'control.saved>={tip}')
                traffic = []
                page.on('websocket', lambda ws: ws.on('framereceived', lambda data: traffic.append(len(data))))
                await page.reload()
                state = await wait(page, f'control.restored>={tip} && control.caughtUp')
                results['reload_received_bytes']=sum(traffic)
                assert sum(traffic)<500000, results
                await stop(page)
            finally:
                await context.close()
            # Closing and reopening Chromium preserves IndexedDB, not Worker memory.
            context = await playwright.chromium.launch_persistent_context(profile, headless=True)
            try:
                await install_routes(context, args)
                page = await context.new_page();await page.goto(args.origin+'/')
                state = await wait(page, f'control.restored>={tip}')
                results['browser_restart_restored_height']=state['restored']
                await wait(page, 'control.caughtUp')
                await stop(page)
                await corrupt_cache(page, args.network)
                await page.reload()
                await wait(page, 'control.reset && control.height>0')
                results['corrupt_cache_rejected']=True
                await stop(page)
            finally:
                await context.close()
        browser = await playwright.chromium.launch(headless=True)
        try:
            context = await browser.new_context()
            await install_routes(context, args, quota=True)
            page = await context.new_page();await page.goto(args.origin+'/')
            await wait(page, 'control.unavailable && control.height>0')
            results['quota_failure_continues_in_memory']=True
            await stop(page)
        finally:
            await browser.close()
    print(json.dumps(results, indent=2))

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module-dir', type=Path, required=True)
    parser.add_argument('--peer', required=True)
    parser.add_argument('--network', default='regtest', choices=['mainnet','testnet','regtest','testnet4','scalenet','chipnet','tempnet'])
    parser.add_argument('--origin', default='http://127.0.0.1:8766')
    parser.add_argument('--capacity', type=int, default=600000)
    args=parser.parse_args();args.origin=args.origin.rstrip('/')
    asyncio.run(main(args))
