/**
 * VersionManager — tracks which thumbnail version is active per level.
 * Now uses MemCache (60 s TTL) to avoid reading versions.json on every request.
 */
import { getR2Json, putR2Json } from './storage.js';
import { memCache } from './cache.js';

const VERSIONS_TTL = 300_000; // 5 min in-memory
const MEM_KEY = 'versions.json';

export class VersionManager {
  constructor(bucket) {
    this.bucket = bucket;
    this.cacheKey = 'data/system/versions.json';
  }

  async getMap() {
    const cached = memCache.get(MEM_KEY);
    if (cached) return cached;

    const data = await getR2Json(this.bucket, this.cacheKey);
    const map = data || {};
    memCache.set(MEM_KEY, map, VERSIONS_TTL);
    return map;
  }

  async getVersion(id) {
    const map = await this.getMap();
    const entry = map[id];
    if (!entry) return undefined;

    if (Array.isArray(entry)) return entry[0];
    if (typeof entry === 'string') return { version: entry, format: 'webp' };
    return entry;
  }

  async getAllVersions(id) {
    const map = await this.getMap();
    const entry = map[id];
    if (!entry) return [];

    if (Array.isArray(entry)) return entry;

    if (typeof entry === 'string') {
      return [{
        version: entry,
        format: 'webp',
        id: 'legacy',
        path: 'thumbnails',
        type: 'static'
      }];
    }
    return [{
      ...entry,
      id: entry.id || 'legacy',
      path: entry.path || 'thumbnails',
      type: entry.type || (entry.format === 'gif' ? 'gif' : 'static')
    }];
  }

  async update(id, version, format = 'webp', path = 'thumbnails', type = 'static', metadata = {}) {
    let map = await this.getMap();

    const cleanMeta = {};
    if (metadata.uploadedBy) cleanMeta.uploadedBy = metadata.uploadedBy;
    if (metadata.uploadedAt) cleanMeta.uploadedAt = metadata.uploadedAt;

    const finalId = "1";

    const newVersion = {
      id: finalId,
      version,
      format,
      path: path.replace(/^\//, ''),
      type,
      ...cleanMeta
    };

    map[id] = [newVersion];
    await putR2Json(this.bucket, this.cacheKey, map);
    memCache.invalidate(MEM_KEY); // bust cache after write
    return newVersion;
  }

  async set(id, versions) {
    let map = await this.getMap();
    map[id] = versions;
    await putR2Json(this.bucket, this.cacheKey, map);
    memCache.invalidate(MEM_KEY);
  }

  async delete(id) {
    let map = await this.getMap();
    if (map[id]) {
      delete map[id];
      await putR2Json(this.bucket, this.cacheKey, map);
      memCache.invalidate(MEM_KEY);
    }
  }
}
