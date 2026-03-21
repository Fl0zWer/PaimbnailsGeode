/**
 * VersionManager — tracks which thumbnail version is active per level.
 * Now uses MemCache (60 s TTL) to avoid reading versions.json on every request.
 */
import { getR2Json, putR2Json } from './storage.js';
import { memCache } from './cache.js';

const VERSIONS_TTL = 300_000; // 5 min in-memory
const MEM_KEY = 'versions.json';
export const MAX_THUMBNAILS_PER_LEVEL = 10;

function normalizeVersionEntry(entry, index = 0) {
  if (!entry) return null;
  if (typeof entry === 'string') {
    return {
      id: 'legacy',
      position: 1,
      version: entry,
      format: 'webp',
      path: 'thumbnails',
      type: 'static'
    };
  }
  return {
    id: entry.id || `${index + 1}`,
    position: typeof entry.position === 'number' ? entry.position : (index + 1),
    version: entry.version,
    format: entry.format || 'webp',
    path: (entry.path || 'thumbnails').replace(/^\//, ''),
    type: entry.type || (entry.format === 'gif' ? 'gif' : 'static'),
    uploadedBy: entry.uploadedBy,
    uploadedAt: entry.uploadedAt
  };
}

function sortByPositionAsc(a, b) {
  return (a.position || 0) - (b.position || 0);
}

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
    const versions = await this.getAllVersions(id);
    if (versions.length === 0) return undefined;
    // "Current" thumbnail is the latest (last by position)
    return versions[versions.length - 1];
  }

  async getAllVersions(id) {
    const map = await this.getMap();
    const entry = map[id];
    if (!entry) return [];

    if (Array.isArray(entry)) {
      return entry
        .map((v, i) => normalizeVersionEntry(v, i))
        .filter(Boolean)
        .sort(sortByPositionAsc);
    }

    if (typeof entry === 'string') {
      return [{
        version: entry,
        format: 'webp',
        id: 'legacy',
        path: 'thumbnails',
        type: 'static'
      }];
    }
    return [normalizeVersionEntry(entry, 0)].filter(Boolean);
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
    map[id] = versions
      .map((v, i) => normalizeVersionEntry(v, i))
      .filter(Boolean)
      .sort(sortByPositionAsc)
      .map((v, i) => ({ ...v, position: i + 1 }));
    await putR2Json(this.bucket, this.cacheKey, map);
    memCache.invalidate(MEM_KEY);
  }

  async appendVersion(id, version, format = 'webp', path = 'thumbnails', type = 'static', metadata = {}, maxPerLevel = MAX_THUMBNAILS_PER_LEVEL) {
    const current = await this.getAllVersions(id);
    const cleanMeta = {};
    if (metadata.uploadedBy) cleanMeta.uploadedBy = metadata.uploadedBy;
    if (metadata.uploadedAt) cleanMeta.uploadedAt = metadata.uploadedAt;

    const nextPosition = current.length + 1;
    const appended = {
      id: String(Date.now()),
      position: nextPosition,
      version,
      format,
      path: path.replace(/^\//, ''),
      type,
      ...cleanMeta
    };

    let next = [...current, appended];
    const removed = [];

    if (next.length > maxPerLevel) {
      const toRemove = next.length - maxPerLevel;
      removed.push(...next.slice(0, toRemove));
      next = next.slice(toRemove);
    }

    next = next
      .sort(sortByPositionAsc)
      .map((v, i) => ({ ...v, position: i + 1 }));

    await this.set(id, next);
    return { appended: next[next.length - 1], removed, versions: next };
  }

  async deleteVersion(id, thumbnailId) {
    const current = await this.getAllVersions(id);
    if (current.length === 0) {
      return { removed: null, versions: [] };
    }

    const idx = current.findIndex(v => String(v.id) === String(thumbnailId));
    if (idx < 0) {
      return { removed: null, versions: current };
    }

    const removed = current[idx];
    const next = current
      .filter((_, i) => i !== idx)
      .map((v, i) => ({ ...v, position: i + 1 }));

    if (next.length === 0) {
      await this.delete(id);
      return { removed, versions: [] };
    }

    await this.set(id, next);
    return { removed, versions: next };
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
