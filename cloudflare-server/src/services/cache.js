/**
 * Cache service — two layers:
 * 1. MemCache: per-isolate in-memory Map with TTL (survives across requests within the same isolate)
 * 2. CF Cache API helpers: `caches.default` edge cache (survives across isolates, reduces Worker invocations)
 */

// ────────────────────────────────────────────────
// Layer 1 — In-memory cache (per-isolate)
// ────────────────────────────────────────────────
const _store = new Map(); // { key → { data, expiresAt } }

export const memCache = {
  get(key) {
    const entry = _store.get(key);
    if (!entry) return undefined;
    if (Date.now() > entry.expiresAt) {
      _store.delete(key);
      return undefined;
    }
    return entry.data;
  },

  set(key, data, ttlMs) {
    _store.set(key, { data, expiresAt: Date.now() + ttlMs });
  },

  invalidate(key) {
    _store.delete(key);
  },

  invalidatePrefix(prefix) {
    for (const k of _store.keys()) {
      if (k.startsWith(prefix)) _store.delete(k);
    }
  },
};

// ────────────────────────────────────────────────
// Layer 2 — Cloudflare Cache API helpers
// ────────────────────────────────────────────────

const STRIP_PARAMS = ['_ts', 't', 'self', 'pending'];

/**
 * Normalize a Request into a stable CF Cache key by stripping
 * ephemeral query params (_ts, t, self, pending).
 */
export function cfCacheKey(request) {
  const url = new URL(request.url);
  for (const p of STRIP_PARAMS) url.searchParams.delete(p);
  url.searchParams.sort();
  return new Request(url.toString(), { method: request.method, headers: request.headers });
}

/**
 * Try to serve a response from the CF edge cache.
 * Returns the cached Response or null.
 */
export async function cfCacheMatch(request) {
  try {
    const cache = caches.default;
    return await cache.match(request);
  } catch {
    return null;
  }
}

/**
 * Store a response in the CF edge cache.
 * The response MUST have a Cache-Control header with max-age.
 * `request` is the original Request object (used as the cache key).
 */
export async function cfCachePut(request, response) {
  try {
    const cache = caches.default;
    await cache.put(request, response.clone());
  } catch (e) {
    console.error('[CfCache] put failed:', e);
  }
}

/**
 * Purge a single URL from the CF edge cache.
 */
export async function cfCacheDelete(url) {
  try {
    const cache = caches.default;
    await cache.delete(url);
  } catch {
    // ignore — key might not exist
  }
}

/**
 * Build a cacheable Response wrapper.
 * Clones the body/headers and sets a public Cache-Control.
 * @param {Response} original - the origin response
 * @param {number} maxAgeSec - max-age in seconds (default 1209600 = 2 weeks)
 */
export function makeCacheable(original, maxAgeSec = 1209600) {
  const headers = new Headers(original.headers);
  headers.set('Cache-Control', `public, max-age=${maxAgeSec}, stale-while-revalidate=86400`);
  headers.delete('Pragma');
  headers.delete('Expires');
  return new Response(original.body, {
    status: original.status,
    headers,
  });
}
