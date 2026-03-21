// A class that simulates an R2 object, backed by Bunny.
class BunnyR2Object {
    constructor(data) {
        this.key = data.key;
        this.version = data.etag;
        this.size = data.size;
        this.etag = data.etag;
        this.httpEtag = data.etag;
        this.uploaded = new Date(data.lastModified);
        this.httpMetadata = data.httpMetadata || {};
        this.customMetadata = data.customMetadata || {};
    }

    writeHttpMetadata(headers) {
        if (this.httpMetadata.contentType) headers.set('Content-Type', this.httpMetadata.contentType);
        if (this.httpMetadata.contentLanguage) headers.set('Content-Language', this.httpMetadata.contentLanguage);
        if (this.httpMetadata.contentDisposition) headers.set('Content-Disposition', this.httpMetadata.contentDisposition);
        if (this.httpMetadata.contentEncoding) headers.set('Content-Encoding', this.httpMetadata.contentEncoding);
        if (this.httpMetadata.cacheControl) headers.set('Cache-Control', this.httpMetadata.cacheControl);
    }
}

// A class that simulates an R2 object with a body.
class BunnyR2ObjectBody extends BunnyR2Object {
    constructor(data, response) {
        super(data);
        this.body = response.body;
        this.bodyUsed = false;
        this.response = response;
    }

    async arrayBuffer() { return this.response.arrayBuffer(); }
    async text() { return this.response.text(); }
    async json() { return this.response.json(); }
    async blob() { return this.response.blob(); }
}

// Main class that replaces an R2 Bucket using Bunny's HTTP API.
export class BunnyBucket {
    constructor(accessKeyId, secretAccessKey, endpoint, zone, subfolder) {
        // accessKeyId = Zone Name (paimbnails)
        // secretAccessKey = API Key / Password
        this.apiKey = secretAccessKey;
        this.zone = zone;
        this.subfolder = subfolder; // 'thumbnails' o 'system'
        
        // Endpoint base: https://storage.bunnycdn.com
        // If the user passed s3.storage.bunnycdn.com, normalize to storage.bunnycdn.com.
        let cleanEndpoint = endpoint.replace(/\/$/, '');
        if (cleanEndpoint.includes('s3.storage.bunnycdn.com')) {
            cleanEndpoint = cleanEndpoint.replace('s3.storage.bunnycdn.com', 'storage.bunnycdn.com');
        }
        this.baseUrl = `${cleanEndpoint}/${this.zone}`;
    }

    _getFullKey(key) {
        const cleanKey = key.startsWith('/') ? key.substring(1) : key;
        return this.subfolder ? `${this.subfolder}/${cleanKey}` : cleanKey;
    }

    _stripPrefix(fullKey) {
        if (this.subfolder && fullKey.startsWith(this.subfolder + '/')) {
            return fullKey.substring(this.subfolder.length + 1);
        }
        return fullKey;
    }

    /**
     * @param {string} key
     * @param {object} [opts]
     * @param {boolean} [opts.skipMeta] - skip .__meta__ fetch (saves 1 HTTP request)
     * @param {number}  [opts.cfCacheTtl] - seconds to let CF edge cache the subrequest (0 = no cache)
     */
    async get(key, opts = {}) {
        const fullKey = this._getFullKey(key);
        const url = `${this.baseUrl}/${fullKey}`; 
        const cacheTtl = opts.cfCacheTtl ?? 0;
        
        try {
            const res = await fetch(url, {
                method: 'GET',
                headers: {
                    'AccessKey': this.apiKey
                },
                cf: cacheTtl > 0 ? { cacheTtl } : { cacheTtl: 0 },
            });

            if (res.status === 404) return null;
            if (!res.ok) throw new Error(`Bunny GET ${res.status}: ${url}`);

            let customMeta = {};
            if (!opts.skipMeta) {
                try {
                    const metaUrl = `${this.baseUrl}/${fullKey}.__meta__`;
                    const metaRes = await fetch(metaUrl, {
                        method: 'GET',
                        headers: { 'AccessKey': this.apiKey },
                        cf: { cacheTtl: 0 },
                    });
                    if (metaRes.ok) {
                        customMeta = await metaRes.json();
                    }
                } catch (_) { /* meta file may not exist for older uploads */ }
            }

            const obj = new BunnyR2ObjectBody({
                key: key,
                etag: res.headers.get('ETag'),
                size: parseInt(res.headers.get('Content-Length') || '0'),
                lastModified: res.headers.get('Last-Modified'),
                httpMetadata: {
                    contentType: res.headers.get('Content-Type'),
                    cacheControl: res.headers.get('Cache-Control'),
                    contentDisposition: res.headers.get('Content-Disposition'),
                    contentEncoding: res.headers.get('Content-Encoding'),
                    contentLanguage: res.headers.get('Content-Language'),
                },
                customMetadata: customMeta,
            }, res);

            return obj;
        } catch (e) {
            console.error(`Error getting ${key} from Bunny:`, e);
            return null;
        }
    }

    /**
     * @param {string} key
     * @param {object} [opts]
     * @param {boolean} [opts.skipMeta]
     * @param {number}  [opts.cfCacheTtl]
     */
    async head(key, opts = {}) {
        const fullKey = this._getFullKey(key);
        const url = `${this.baseUrl}/${fullKey}`;
        const cacheTtl = opts.cfCacheTtl ?? 0;
        
        try {
            const res = await fetch(url, {
                method: 'HEAD',
                headers: {
                    'AccessKey': this.apiKey
                },
                cf: cacheTtl > 0 ? { cacheTtl } : { cacheTtl: 0 },
            });
            if (res.status === 404) return null;

            let customMeta = {};
            if (!opts.skipMeta) {
                try {
                    const metaUrl = `${this.baseUrl}/${fullKey}.__meta__`;
                    const metaRes = await fetch(metaUrl, {
                        method: 'GET',
                        headers: { 'AccessKey': this.apiKey },
                        cf: { cacheTtl: 0 },
                    });
                    if (metaRes.ok) {
                        customMeta = await metaRes.json();
                    }
                } catch (_) { /* meta file may not exist */ }
            }

            return new BunnyR2Object({
                key: key,
                etag: res.headers.get('ETag'),
                size: parseInt(res.headers.get('Content-Length') || '0'),
                lastModified: res.headers.get('Last-Modified'),
                httpMetadata: {
                    contentType: res.headers.get('Content-Type'),
                    cacheControl: res.headers.get('Cache-Control')
                },
                customMetadata: customMeta,
            });
        } catch (e) {
            return null;
        }
    }

    async put(key, body, options = {}) {
        const fullKey = this._getFullKey(key);
        const url = `${this.baseUrl}/${fullKey}`;
        const uploadedAt = new Date().toISOString();

        const headers = {
            'AccessKey': this.apiKey
        };
        
        if (options.httpMetadata) {
            if (options.httpMetadata.contentType) headers['Content-Type'] = options.httpMetadata.contentType;
        }

        const res = await fetch(url, {
            method: 'PUT',
            body: body,
            headers: headers
        });

        if (!res.ok) throw new Error(`Bunny PUT failed: ${res.status}`);

        // Build metadata object: always include uploadedAt, merge any customMetadata
        // passed in options so callers can persist arbitrary key-value pairs.
        const metaData = {
            uploadedAt,
            ...(options.customMetadata || {}),
        };

        // After uploading, store a companion .meta file with the timestamp
        // and custom metadata so we can retrieve it later on get()/head()
        // since Bunny Storage doesn't support custom metadata headers.
        const metaUrl = `${this.baseUrl}/${fullKey}.__meta__`;
        try {
            await fetch(metaUrl, {
                method: 'PUT',
                body: JSON.stringify(metaData),
                headers: {
                    'AccessKey': this.apiKey,
                    'Content-Type': 'application/json'
                }
            });
        } catch (e) {
            // Non-critical — the file was already uploaded successfully
            console.warn(`[Bunny] Failed to write meta for ${key}:`, e);
        }

        return {
            key: key,
            etag: 'uploaded',
            uploadedAt: uploadedAt
        };
    }

    async delete(key) {
        const fullKey = this._getFullKey(key);
        const url = `${this.baseUrl}/${fullKey}`;
        await fetch(url, { 
            method: 'DELETE',
            headers: { 'AccessKey': this.apiKey }
        });
        // Also remove companion meta file if it exists
        try {
            await fetch(`${url}.__meta__`, {
                method: 'DELETE',
                headers: { 'AccessKey': this.apiKey }
            });
        } catch (_) { /* ignore */ }
    }

    async list(options = {}) {
        const prefix = options.prefix || '';
        const fullPrefix = this.subfolder ? `${this.subfolder}/${prefix}` : prefix;
        
        let listPath = fullPrefix;
        if (listPath && !listPath.endsWith('/')) {
            const lastSlash = listPath.lastIndexOf('/');
            if (lastSlash !== -1) {
                listPath = listPath.substring(0, lastSlash + 1);
            } else {
                listPath = '';
            }
        }
        
        const url = `${this.baseUrl}/${listPath}`;
        
        const res = await fetch(url, {
            method: 'GET',
            headers: { 'AccessKey': this.apiKey },
            cf: { cacheTtl: 0 },
        });

        if (res.status === 404) {
            return { objects: [], truncated: false, cursor: null, delimitedPrefixes: [] };
        }
        
        if (!res.ok) throw new Error(`Bunny LIST failed: ${res.status}`);
        
        const items = await res.json();
        
        const objects = [];

        // Optimization: Calculate filter prefix relative to the directory we are listing
        // This avoids string concatenation and manipulation for every file in the directory
        let filterPrefix = '';
        if (fullPrefix && fullPrefix.startsWith(listPath)) {
            filterPrefix = fullPrefix.substring(listPath.length);
        }

        const zonePrefix = `/${this.zone}/`;

        for (const item of items) {
            if (item.IsDirectory) continue; 

            // Skip companion .__meta__ files (internal timestamp storage)
            if (item.ObjectName.endsWith('.__meta__')) continue;

            // Optimized check: Check ObjectName directly against the remaining prefix
            if (filterPrefix && !item.ObjectName.startsWith(filterPrefix)) continue;
            
            const itemFullPath = `${item.Path}${item.ObjectName}`; 
            
            // Strip Zone Name prefix from the path returned by Bunny
            // Path is usually /ZoneName/Path/To/File
            let relativePath = itemFullPath;
            if (relativePath.startsWith(zonePrefix)) {
                relativePath = relativePath.substring(zonePrefix.length);
            } else if (relativePath.startsWith('/')) {
                 relativePath = relativePath.substring(1);
            }
            
            if (fullPrefix && !relativePath.startsWith(fullPrefix)) continue;

            objects.push(new BunnyR2Object({
                key: this._stripPrefix(relativePath),
                etag: '', 
                size: item.Length,
                lastModified: item.LastChanged,
                httpMetadata: {} 
            }));
        }

        return {
            objects: objects,
            truncated: false,
            cursor: null,
            delimitedPrefixes: []
        };
    }
}
