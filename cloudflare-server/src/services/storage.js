/**
 * R2/Bunny storage helpers
 */
import { NO_STORE_CACHE_CONTROL } from '../middleware/cors.js';

export async function getR2Json(bucket, key) {
  const object = await bucket.get(key);
  if (!object) return null;
  const text = await object.text();
  try {
    const clean = text.replace(/^\uFEFF/, '').trim();
    return JSON.parse(clean);
  } catch (e) {
    console.error(`Error parsing JSON for ${key}:`, e);
    return null;
  }
}

export async function putR2Json(bucket, key, data) {
  try {
    const json = JSON.stringify(data, null, 2);
    await bucket.put(key, json, {
      httpMetadata: { contentType: 'application/json', cacheControl: NO_STORE_CACHE_CONTROL }
    });
    return true;
  } catch (error) {
    console.error(`Error writing ${key}:`, error);
    return false;
  }
}

export function expandKeyVariants(baseKey) {
  const clean = baseKey.replace(/^\//, '');
  return [clean, '/' + clean];
}

export function expandCandidates(keys) {
  const out = [];
  for (const k of keys) {
    for (const v of expandKeyVariants(k)) {
      out.push(v);
    }
  }
  return [...new Set(out)];
}

export async function listR2Keys(bucket, prefix) {
  try {
    let keys = [];
    let cursor = undefined;

    do {
      const list = await bucket.list({ prefix, cursor });
      keys = keys.concat(list.objects.map(obj => obj.key));
      cursor = list.truncated ? list.cursor : undefined;
    } while (cursor);

    return keys;
  } catch (error) {
    console.error(`Error listing ${prefix}:`, error);
    return [];
  }
}
