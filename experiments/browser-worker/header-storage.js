// Worker-owned IDBFS cache. Each network has its own database and one atomic header file.
class HeaderStorage {
    constructor(core, network) {
        const names = {mainnet:'Mainnet',testnet:'Testnet',regtest:'Regtest',testnet4:'Testnet4',scalenet:'Scalenet',chipnet:'Chipnet',tempnet:'Tempnet'};
        const name = names[network];
        if (!name) throw new Error('Red de almacenamiento desconocida');
        this.core = core;
        this.path = '/kth-header-cache/' + name;
        this.file = this.path + '/chain-v1.bin';
        this.available = false;
        this.inflight = undefined;
        this.stamp = undefined;
        this.lastSave = -Infinity;
    }
    sync(populate) {
        return new Promise((resolve, reject) => this.core.FS.syncfs(populate, error => error ? reject(error) : resolve()));
    }
    report(error) {
        this.available = false;
        postMessage({type:'storageUnavailable',message:'No se pudo guardar la cadena en este navegador: ' + String(error)});
    }
    async open() {
        try {
            postMessage({type:'storageLoading'});
            this.core.FS.mkdirTree(this.path);
            this.core.FS.mount(this.core.IDBFS, {}, this.path);
            await this.sync(true);
            this.available = true;
        } catch (error) { this.report(error); }
    }
    hasCache() {
        return this.available && this.core.FS.analyzePath(this.file).exists;
    }
    attach(client) {
        if (!this.available) return;
        const height = Number(client.lastHeight());
        if (this.hasCache()) this.stamp = height + ':' + client.headerHash(BigInt(height));
        postMessage({type:'storageRestored',height});
    }
    async discard() {
        this.core.FS.unlink(this.file);
        this.stamp = undefined;
        try { await this.sync(false); } catch (error) { this.report(error); }
        postMessage({type:'storageReset',message:'La copia guardada no pasó la validación. Se volverá a sincronizar desde génesis.'});
    }
    async save(client, force = false) {
        if (!this.available) return;
        if (this.inflight) {
            if (force) { await this.inflight; return this.save(client, true); }
            return;
        }
        if (!force && performance.now() - this.lastSave < 2000) return;
        try {
            const height = Number(client.lastHeight());
            const stamp = height + ':' + client.headerHash(BigInt(height));
            if (this.stamp === stamp || !this.hasCache()) return;
            if (this.core.FS.stat(this.file).size !== (height + 1) * 80) throw new Error('Copia local de headers incompleta');
            this.lastSave = performance.now();
            this.inflight = this.sync(false).then(() => {
                this.stamp = stamp;
                postMessage({type:'storageSaved',height});
            }).catch(error => this.report(error));
            await this.inflight;
        } catch (error) { this.report(error); }
        finally { this.inflight = undefined; }
    }
}
self.HeaderStorage = HeaderStorage;
