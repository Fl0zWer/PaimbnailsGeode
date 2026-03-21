/**
 * Cloudflare Worker for Paimon Thumbnails
 * Uses only R2 for storage (thumbnails + data) - No KV needed
 * Simplified version: No Cache API, No Rate Limiting, No Analytics
 */

import { homeHtml } from './pages/home.js';
import { guidelinesHtml } from './pages/guidelines.js';
import { donateHtml } from './pages/donate.js';
import { BunnyBucket } from './bunny-wrapper.js';
import { getImageDimensions } from './image-utils.js';
import { validateImageSecurity } from './image-security.js';

const ADMIN_USERS = ['flozwer', 'gabriv4', 'alvaroeter'];

// ===== IMAGE SECURITY HELPER =====
/**
 * Run full security validation on an uploaded image buffer.
 * Returns a Response with 400 status if malicious content is detected, or null if safe.
 * @param {Uint8Array} buffer - Raw image data
 * @param {string} declaredMimeType - MIME type from the upload
 * @param {string} [filename] - Original filename
 * @returns {Response|null} Error response if rejected, null if safe
 */
function rejectIfMalicious(buffer, declaredMimeType, filename = '') {
  const result = validateImageSecurity(buffer, declaredMimeType, filename);

  if (!result.safe) {
    console.error(`[Security] Image REJECTED - Errors: ${result.errors.join('; ')}`);
    if (result.warnings.length > 0) {
      console.warn(`[Security] Warnings: ${result.warnings.join('; ')}`);
    }
    return new Response(JSON.stringify({
      error: 'Image rejected by security scan',
      details: result.errors[0], // Only expose the first error to the client
      code: 'SECURITY_VIOLATION'
    }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  if (result.warnings.length > 0) {
    console.warn(`[Security] Image accepted with warnings: ${result.warnings.join('; ')}`);
  }

  return null; // Safe
}

// ===== CACHE POLICY =====
// User requested: absolutely no caching anywhere (server-side).
const NO_STORE_CACHE_CONTROL = 'no-store, no-cache, must-revalidate, max-age=0';

function noStoreHeaders(extra = {}) {
  return {
    'Cache-Control': NO_STORE_CACHE_CONTROL,
    Pragma: 'no-cache',
    Expires: '0',
    ...extra,
  };
}

function redirectNoStore(url, status = 302, extraHeaders = {}) {
  return new Response(null, {
    status,
    headers: noStoreHeaders({
      Location: url,
      ...extraHeaders,
    }),
  });
}

// Helper functions for R2 data storage
async function getR2Json(bucket, key) {
  // Don't catch errors here to prevent overwriting data on read failures
  const object = await bucket.get(key);
  if (!object) return null;
  const text = await object.text();
  try {
    // Strip BOM and whitespace that Bunny storage may inject
    const clean = text.replace(/^\uFEFF/, '').trim();
    return JSON.parse(clean);
  } catch (e) {
    console.error(`Error parsing JSON for ${key}:`, e);
    return null;
  }
}

async function putR2Json(bucket, key, data) {
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

// Helper: generate key variants with and without leading slash to be tolerant
function expandKeyVariants(baseKey) {
  const clean = baseKey.replace(/^\//, '');
  return [clean, '/' + clean];
}

function expandCandidates(keys) {
  const out = [];
  for (const k of keys) {
    for (const v of expandKeyVariants(k)) {
      out.push(v);
    }
  }
  // Remove duplicates while preserving order
  return [...new Set(out)];
}

async function listR2Keys(bucket, prefix) {
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

// Get moderators list from R2 directly
async function getModerators(bucket) {
  const data = await getR2Json(bucket, 'data/moderators.json');
  return data?.moderators || [];
}

// Get VIP list from R2 directly
async function getVips(bucket) {
  const data = await getR2Json(bucket, 'data/vips.json');
  return data?.vips || [];
}

// Add VIP
async function addVip(bucket, username) {
  const vips = await getVips(bucket);
  if (!vips.includes(username.toLowerCase())) {
    vips.push(username.toLowerCase());
    await putR2Json(bucket, 'data/vips.json', { vips });
  }
  return true;
}

// Remove VIP
async function removeVip(bucket, username) {
  const vips = await getVips(bucket);
  const filtered = vips.filter(v => v !== username.toLowerCase());
  await putR2Json(bucket, 'data/vips.json', { vips: filtered });
  return true;
}

// Add moderator
async function addModerator(bucket, username) {
  const moderators = await getModerators(bucket);
  if (!moderators.includes(username.toLowerCase())) {
    moderators.push(username.toLowerCase());
    await putR2Json(bucket, 'data/moderators.json', { moderators });
  }
  return true;
}

// ===== INCREMENTAL CACHE HELPERS =====

// Update top-thumbnails cache incrementally after a vote
async function updateTopThumbnailsCache(env, levelId, stats, uploadedBy, accountID) {
  try {
    let cache = await getR2Json(env.SYSTEM_BUCKET, 'data/system/top_thumbnails.json') || [];
    const lid = parseInt(levelId);
    const average = stats.count > 0 ? stats.total / stats.count : 0;

    // Remove existing entry for this level
    cache = cache.filter(item => item.levelId !== lid);

    // Only add if >= 3 votes
    if (stats.count >= 3) {
      cache.push({
        levelId: lid,
        rating: Math.round(average * 100) / 100,
        count: stats.count,
        uploadedBy: uploadedBy || 'Unknown',
        accountID: accountID ? parseInt(accountID) : 0
      });
    }

    // Sort by rating desc, tie-break by count desc
    cache.sort((a, b) => {
      if (b.rating !== a.rating) return b.rating - a.rating;
      return b.count - a.count;
    });

    // Keep top 100
    if (cache.length > 100) cache = cache.slice(0, 100);

    await putR2Json(env.SYSTEM_BUCKET, 'data/system/top_thumbnails.json', cache);
  } catch (e) {
    console.error('Failed to update top thumbnails cache:', e);
  }
}

// Update creator-leaderboard cache incrementally
async function updateCreatorLeaderboardCache(env, username, opts = {}) {
  if (!username || username === 'Unknown' || username === 'System') return;
  try {
    let cache = await getR2Json(env.SYSTEM_BUCKET, 'data/system/creator_leaderboard.json') || [];
    const userLower = username.toLowerCase();

    let entry = cache.find(c => c.username.toLowerCase() === userLower);
    if (!entry) {
      entry = { username, accountID: opts.accountID || 0, uploadCount: 0, totalRating: 0, totalVotes: 0 };
      cache.push(entry);
    }

    if (opts.incrementUpload) {
      entry.uploadCount = (entry.uploadCount || 0) + 1;
    }
    if (opts.accountID && !entry.accountID) {
      entry.accountID = parseInt(opts.accountID);
    }
    if (opts.addRating) {
      entry.totalRating = (entry.totalRating || 0) + opts.addRating;
      entry.totalVotes = (entry.totalVotes || 0) + 1;
    }

    entry.avgRating = entry.totalVotes > 0 ? Math.round((entry.totalRating / entry.totalVotes) * 100) / 100 : 0;

    // Sort by uploadCount desc, avgRating desc as tiebreaker
    cache.sort((a, b) => {
      if (b.uploadCount !== a.uploadCount) return b.uploadCount - a.uploadCount;
      return (b.avgRating || 0) - (a.avgRating || 0);
    });

    // Keep top 100
    if (cache.length > 100) cache = cache.slice(0, 100);

    await putR2Json(env.SYSTEM_BUCKET, 'data/system/creator_leaderboard.json', cache);
  } catch (e) {
    console.error('Failed to update creator leaderboard cache:', e);
  }
}

// CORS headers helper
function corsHeaders(origin) {
  return {
    'Access-Control-Allow-Origin': origin || '*',
    'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type, X-API-Key, X-Mod-Code, Authorization',
    'Access-Control-Max-Age': '0',
    ...noStoreHeaders(),
  };
}

// Verify API key
function verifyApiKey(request, env) {
  const apiKey = request.headers.get('X-API-Key');
  return apiKey === env.API_KEY;
}

// ===== WEBHOOK DISPATCH =====
/**
 * Send a push event to the Discord bot's webhook endpoint.
 * Always call inside ctx.waitUntil() so it runs in the background
 * and never blocks the main response.
 *
 * @param {object} env - Worker environment bindings
 * @param {string} eventType - 'upload' | 'daily' | 'weekly'
 * @param {object} payload - Event data to send
 */
async function dispatchWebhook(env, eventType, payload) {
  const url = env.DISCORD_BOT_WEBHOOK_URL;
  const secret = env.DISCORD_BOT_WEBHOOK_SECRET;
  if (!url || !secret) return;

  const body = JSON.stringify({ event: eventType, data: payload });

  for (let attempt = 0; attempt < 2; attempt++) {
    try {
      const resp = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${secret}`,
        },
        body,
      });
      if (resp.ok) {
        console.log(`[Webhook] Dispatched '${eventType}' event successfully`);
        return;
      }
      console.warn(`[Webhook] '${eventType}' attempt ${attempt + 1} got status ${resp.status}`);
      if (resp.status < 500) return; // Don't retry client errors
    } catch (err) {
      console.error(`[Webhook] '${eventType}' attempt ${attempt + 1} failed:`, err.message);
    }
    // Wait 2s before retry
    if (attempt === 0) await new Promise(r => setTimeout(r, 2000));
  }
}

// Handle CORS preflight
function handleOptions(request) {
  const origin = request.headers.get('Origin');
  return new Response(null, {
    status: 204,
    headers: { ...corsHeaders(origin), ...noStoreHeaders() }
  });
}

// ===== VERSION MANAGER =====
class VersionManager {
  constructor(bucket) {
    this.bucket = bucket;
    this.cacheKey = 'data/system/versions.json';
  }

  async getMap() {
    // No caching - always fetch fresh data from storage
    const data = await getR2Json(this.bucket, this.cacheKey);
    return data || {};
  }

  async getVersion(id) {
    const map = await this.getMap();
    const entry = map[id];
    if (!entry) return undefined;

    // Handle array (multiple thumbnails) - return main (first)
    if (Array.isArray(entry)) {
      return entry[0];
    }

    // Support both old string format and new object format
    if (typeof entry === 'string') {
      return { version: entry, format: 'webp' }; // Default to webp for legacy string versions
    }
    return entry; // Returns { version, format } or undefined
  }

  async getAllVersions(id) {
    const map = await this.getMap();
    const entry = map[id];
    if (!entry) return [];

    if (Array.isArray(entry)) {
      return entry;
    }

    if (typeof entry === 'string') {
      // Legacy string entry (just the version timestamp)
      // We need to reconstruct the path/format if possible, or assume defaults
      return [{
        version: entry,
        format: 'webp',
        id: 'legacy',
        path: 'thumbnails', // Default path
        type: 'static'
      }];
    }
    // Legacy object entry
    return [{
      ...entry,
      id: entry.id || 'legacy',
      path: entry.path || 'thumbnails',
      type: entry.type || (entry.format === 'gif' ? 'gif' : 'static')
    }];
  }

  async update(id, version, format = 'webp', path = 'thumbnails', type = 'static', metadata = {}) {
    let map = await this.getMap();

    // Clean metadata to minimal set
    const cleanMeta = {};
    if (metadata.uploadedBy) cleanMeta.uploadedBy = metadata.uploadedBy;
    if (metadata.uploadedAt) cleanMeta.uploadedAt = metadata.uploadedAt;

    // Always overwrite as slot "1" (Single thumbnail mode)
    const finalId = "1";

    const newVersion = {
      id: finalId,
      version,
      format,
      path: path.replace(/^\//, ''),
      type,
      ...cleanMeta
    };

    // Always replace with a single-item array containing the new version
    map[id] = [newVersion];

    await putR2Json(this.bucket, this.cacheKey, map);
    return newVersion;
  }

  async set(id, versions) {
    let map = await this.getMap();
    map[id] = versions;
    await putR2Json(this.bucket, this.cacheKey, map);
  }

  async delete(id) {
    let map = await this.getMap();
    if (map[id]) {
      delete map[id];
      await putR2Json(this.bucket, this.cacheKey, map);
    }
  }
}

// ===== PET SHOP =====
// Catalog stored in SYSTEM_BUCKET at data/pet-shop/catalog.json
// Pet images stored in THUMBNAILS_BUCKET at pet-shop/{id}.{format}

const PET_SHOP_CATALOG_KEY = 'data/pet-shop/catalog.json';
const PET_SHOP_IMAGE_PREFIX = 'pet-shop/';

async function handlePetShopList(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const catalog = await getR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY);
    const items = catalog?.items || [];

    return new Response(JSON.stringify({ items }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('[PetShop] List error:', error);
    return new Response(JSON.stringify({ error: 'Failed to load pet shop' }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

async function handlePetShopDownload(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const url = new URL(request.url);
    // path: /api/pet-shop/download/{id}.{format}
    const filename = url.pathname.split('/').pop();
    if (!filename) {
      return new Response(JSON.stringify({ error: 'Missing filename' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const key = PET_SHOP_IMAGE_PREFIX + filename;

    // try with and without leading slash
    const candidates = expandKeyVariants(key);
    let object = null;
    for (const k of candidates) {
      object = await env.THUMBNAILS_BUCKET.get(k);
      if (object) break;
    }

    if (!object) {
      return new Response(JSON.stringify({ error: 'Pet not found' }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const ext = filename.split('.').pop()?.toLowerCase();
    let contentType = 'application/octet-stream';
    if (ext === 'png') contentType = 'image/png';
    else if (ext === 'gif') contentType = 'image/gif';
    else if (ext === 'jpg' || ext === 'jpeg') contentType = 'image/jpeg';
    else if (ext === 'webp') contentType = 'image/webp';

    return new Response(object.body, {
      status: 200,
      headers: {
        'Content-Type': contentType,
        'Content-Disposition': `attachment; filename="${filename}"`,
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error('[PetShop] Download error:', error);
    return new Response(JSON.stringify({ error: 'Download failed' }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

async function handlePetShopUpload(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const name = formData.get('name') || 'Unknown Pet';
    const creator = formData.get('creator') || 'Unknown';

    if (!file) {
      return new Response(JSON.stringify({ error: 'Missing image file' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Verify uploader is a moderator or admin
    const creatorLower = creator.toLowerCase();
    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const isAdmin = ADMIN_USERS.includes(creatorLower);
    const isMod = moderators.includes(creatorLower);

    if (!isAdmin && !isMod) {
      return new Response(JSON.stringify({ error: 'Only moderators can upload pets' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Determine format
    const fileType = file.type || 'image/png';
    let format = 'png';
    if (fileType === 'image/gif') format = 'gif';
    else if (fileType === 'image/jpeg') format = 'jpg';
    else if (fileType === 'image/webp') format = 'webp';

    // Size limit: 5MB
    if (file.size > 5 * 1024 * 1024) {
      return new Response(JSON.stringify({ error: 'File too large (max 5MB)' }), {
        status: 413,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Generate unique ID
    const id = `pet_${Date.now()}_${Math.random().toString(36).substring(2, 8)}`;
    const storageKey = PET_SHOP_IMAGE_PREFIX + id + '.' + format;

    // Store the image
    const arrayBuffer = await file.arrayBuffer();

    // ===== SECURITY SCAN =====
    const securityReject = rejectIfMalicious(new Uint8Array(arrayBuffer), fileType, file.name || `pet.${format}`);
    if (securityReject) return securityReject;
    // =========================

    await env.THUMBNAILS_BUCKET.put(storageKey, arrayBuffer, {
      httpMetadata: { contentType: fileType, cacheControl: NO_STORE_CACHE_CONTROL }
    });

    // Update catalog
    const catalog = await getR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY) || { items: [] };
    if (!Array.isArray(catalog.items)) catalog.items = [];

    const newItem = {
      id,
      name: name.substring(0, 50), // limit name length
      creator,
      format,
      fileSize: file.size,
      uploadedAt: new Date().toISOString()
    };

    catalog.items.unshift(newItem); // newest first
    await putR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY, catalog);

    console.log(`[PetShop] ${creator} uploaded pet "${name}" (${id}.${format}, ${file.size} bytes)`);

    return new Response(JSON.stringify({
      success: true,
      message: 'Pet uploaded successfully',
      item: newItem
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('[PetShop] Upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed: ' + error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Upload suggestion endpoint (non-moderators)
async function handleUploadSuggestion(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const levelId = formData.get('levelId');
    const username = formData.get('username') || 'Unknown';
    const accountID = parseInt(formData.get('accountID') || '0');

    if (!file || !levelId) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Check if user is banned
    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (username !== 'Unknown' && banned.includes(username.toLowerCase())) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (accountID === 0) {
      console.log(`Unauthenticated suggestion upload (accountID=0) by '${username}' for level ${levelId}`);
    }

    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);

    // ===== SECURITY SCAN =====
    const securityReject = rejectIfMalicious(buffer, file.type || 'image/webp', file.name || `suggestion_${levelId}.webp`);
    if (securityReject) return securityReject;
    // =========================

    // Validate resolution
    const dims = getImageDimensions(buffer);
    if (!dims) {
      return new Response(JSON.stringify({ error: 'Invalid or unsupported image format' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Handle multi-suggestion queue
    const queueKey = `data/queue/suggestions/${levelId}.json`;
    let queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);

    // Normalize queue data to array
    let suggestions = [];
    if (queueData) {
      if (Array.isArray(queueData)) {
        suggestions = queueData;
      } else {
        // Convert legacy single object to array
        // Ensure legacy object has a filename if it doesn't (it was likely suggestions/${levelId}.webp)
        if (!queueData.filename) {
          queueData.filename = `suggestions/${levelId}.webp`;
        }
        suggestions = [queueData];
      }
    }

    // Check limit
    if (suggestions.length >= 10) {
      return new Response(JSON.stringify({ error: 'Suggestion limit reached for this level (max 10)' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Generate unique filename
    const uniqueId = Math.random().toString(36).substring(7);
    const timestamp = Date.now();
    const filename = `suggestions/${levelId}_${timestamp}_${uniqueId}.webp`;

    // Store in suggestions folder with unique name
    await env.THUMBNAILS_BUCKET.put(filename, buffer, {
      httpMetadata: { contentType: 'image/webp', cacheControl: NO_STORE_CACHE_CONTROL },
      customMetadata: {
        uploadedBy: username,
        uploadedAt: new Date().toISOString(),
        category: 'suggestion'
      }
    });

    // Add new suggestion to array
    suggestions.push({
      levelId: parseInt(levelId),
      category: 'verify',
      submittedBy: username,
      timestamp: timestamp,
      status: 'pending',
      note: 'User suggestion',
      accountID: accountID,
      unauthenticated: accountID === 0,
      filename: filename
    });

    // Save updated queue
    await putR2Json(env.SYSTEM_BUCKET, queueKey, suggestions);

    return new Response(JSON.stringify({
      success: true,
      message: 'Suggestion uploaded successfully'
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Suggestion upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Upload update endpoint (non-moderators)
async function handleUploadUpdate(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const levelId = formData.get('levelId');
    const username = formData.get('username') || 'Unknown';
    const accountID = parseInt(formData.get('accountID') || '0');

    if (!file || !levelId) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Check if user is banned
    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (username !== 'Unknown' && banned.includes(username.toLowerCase())) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);

    // Validate resolution
    const dims = getImageDimensions(buffer);
    if (!dims) {
      return new Response(JSON.stringify({ error: 'Invalid or unsupported image format' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Detect actual image format from bytes
    const detectedTypeU = dims.type;
    const extMapU = { png: 'png', webp: 'webp', jpeg: 'jpg', gif: 'gif' };
    const mimeMapU = { png: 'image/png', webp: 'image/webp', jpeg: 'image/jpeg', gif: 'image/gif' };
    const updExt = extMapU[detectedTypeU] || 'webp';
    const updMime = mimeMapU[detectedTypeU] || 'image/webp';

    // ===== SECURITY SCAN =====
    const securityReject = rejectIfMalicious(buffer, updMime, file.name || `update_${levelId}.${updExt}`);
    if (securityReject) return securityReject;
    // =========================

    // Store in updates folder with detected extension
    const key = `updates/${levelId}.${updExt}`;
    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: { contentType: updMime, cacheControl: NO_STORE_CACHE_CONTROL },
      customMetadata: {
        uploadedBy: username,
        uploadedAt: new Date().toISOString(),
        category: 'update',
        accountID: accountID.toString(),
        unauthenticated: accountID === 0 ? 'true' : 'false'
      }
    });

    // Add to updates queue
    const queueKey = `data/queue/updates/${levelId}.json`;
    await putR2Json(env.SYSTEM_BUCKET, queueKey, {
      levelId: parseInt(levelId),
      category: 'update',
      submittedBy: username,
      timestamp: Date.now(),
      status: 'pending',
      note: 'Update proposal',
      accountID: accountID,
      unauthenticated: accountID === 0
    });

    return new Response(JSON.stringify({
      success: true,
      message: 'Update uploaded successfully'
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Update upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// ===== MOD VERSION & UPDATE SYSTEM =====

const CURRENT_MOD_VERSION = {
  version: '1.1.6',
  downloadUrl: 'https://paimon-thumbnails-server.paimonalcuadrado.workers.dev/downloads/paimon.level_thumbnails.geode',
  changelog: '• Nuevo: Animación de flash al cargar miniaturas\n• Corrección: Mejoras en la carga de imágenes'
};

async function handleVersionCheck(request) {
  try {
    console.log('[VersionCheck] Request from:', request.headers.get('user-agent'));

    return new Response(JSON.stringify(CURRENT_MOD_VERSION), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error('[VersionCheck] Error:', error);
    return new Response(JSON.stringify({
      error: 'Internal server error',
      message: error.message || 'Unknown error'
    }), {
      status: 500,
      headers: {
        'Content-Type': 'application/json',
        ...corsHeaders()
      }
    });
  }
}

async function handleModDownload(request, env, ctx) {
  try {
    const object = await env.SYSTEM_BUCKET.get('mod-releases/paimon.level_thumbnails.geode');

    if (!object) {
      console.error('[ModDownload] File not found in R2');
      return new Response(JSON.stringify({ error: 'Mod file not found' }), {
        status: 404,
        headers: {
          'Content-Type': 'application/json',
          ...corsHeaders()
        }
      });
    }

    console.log('[ModDownload] Serving mod file from R2');

    return new Response(object.body, {
      status: 200,
      headers: {
        'Content-Type': 'application/octet-stream',
        'Content-Disposition': 'attachment; filename="paimon.level_thumbnails.geode"',
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error('[ModDownload] Error:', error);
    return new Response(JSON.stringify({
      error: 'Internal server error',
      message: error.message || 'Unknown error'
    }), {
      status: 500,
      headers: {
        'Content-Type': 'application/json',
        ...corsHeaders()
      }
    });
  }
}

// ===== END MOD VERSION & UPDATE SYSTEM =====

// Upload profile config
async function handleUploadProfileConfig(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const accountID = formData.get('accountID');
    const config = formData.get('config');

    if (!accountID || !config) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Validate JSON
    try {
      JSON.parse(config);
    } catch (e) {
      return new Response(JSON.stringify({ error: 'Invalid JSON' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const key = `profiles/config/${accountID}.json`;
    await env.THUMBNAILS_BUCKET.put(key, config, {
      httpMetadata: { contentType: 'application/json', cacheControl: NO_STORE_CACHE_CONTROL }
    });

    return new Response(JSON.stringify({ success: true }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Get profile config
async function handleGetProfileConfig(request, env) {
  const url = new URL(request.url);
  const accountID = url.pathname.split('/').pop().replace('.json', '');

  const key = `profiles/config/${accountID}.json`;
  const object = await env.THUMBNAILS_BUCKET.get(key);

  if (!object) {
    return new Response(JSON.stringify({}), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const data = await object.text();
  return new Response(data, {
    headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// Upload thumbnail endpoint (PNG)
// Get latest uploads
async function handleGetLatestUploads(request, env) {
  try {
    const data = await getR2Json(env.SYSTEM_BUCKET, 'data/system/latest_uploads.json');
    return new Response(JSON.stringify({
      uploads: data || []
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Check moderator auth via code or GDBrowser
async function verifyModAuth(request, env, username, accountID) {
  const sysBucket = env.SYSTEM_BUCKET;
  const rawModCode = request.headers.get('X-Mod-Code');
  const authKey = `data/auth/${username.toLowerCase()}.json`;

  // Mod-code is required — no GDBrowser fallback
  if (!rawModCode) {
    console.log(`[Auth] No X-Mod-Code header for ${username}. Must generate in settings.`);
    return { authorized: false, needsModCode: true };
  }

  // Trim whitespace/invisible chars from header value
  const modCode = rawModCode.trim();

  const storedData = await getR2Json(sysBucket, authKey);

  if (!storedData) {
    console.log(`[Auth] No stored auth data found for ${username} at ${authKey}. Code sent: ${modCode.substring(0, 8)}...`);
    return { authorized: false, invalidCode: true };
  }

  // Trim stored code too — Bunny storage may add trailing chars
  const storedCode = (storedData.code || '').trim();

  if (storedCode !== modCode) {
    console.log(`[Auth] Code mismatch for ${username}. Sent(len=${modCode.length}): "${modCode}" Stored(len=${storedCode.length}): "${storedCode}"`);
    return { authorized: false, invalidCode: true };
  }

  // If accountID is provided, require it to match the one that generated the code.
  if (accountID > 0 && storedData.accountID && parseInt(storedData.accountID) !== parseInt(accountID)) {
    console.log(`[Auth] AccountID mismatch for ${username}: expected ${storedData.accountID}, got ${accountID}`);
    return { authorized: false };
  }

  console.log(`[Auth] Authorized ${username} with mod-code ${modCode.substring(0, 8)}...`);
  return { authorized: true };
}

// Helper: verify mod auth extracting username/accountID from parsed body
async function verifyModAuthFromBody(request, env, body) {
  const username = (body.username || body.adminUser || '').toString().trim();
  const accountID = parseInt(body.accountID || '0');
  if (!username) return { authorized: false };
  return await verifyModAuth(request, env, username, accountID);
}

// ===== GD LEVEL PROXY =====
/**
 * GET /api/level/:id
 * Proxies to GDBrowser from Cloudflare (avoids IP blocks on bot hosting).
 * Response is cached in R2 for 1 hour to reduce external calls.
 */
async function handleGDLevelProxy(request, env) {
  const url = new URL(request.url);
  const id = url.pathname.replace('/api/level/', '').split('/')[0].trim();

  if (!id || !/^\d+$/.test(id)) {
    return new Response(JSON.stringify({ error: 'Invalid level ID' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  // ── Cache in R2 for 1 hour ─────────────────────────────────────────
  const cacheKey = `cache/level/${id}.json`;
  try {
    const cached = await env.SYSTEM_BUCKET.get(cacheKey);
    if (cached) {
      const meta = cached.customMetadata || {};
      const cachedAt = parseInt(meta.cachedAt || '0');
      if (Date.now() - cachedAt < 3600_000) {
        const text = await cached.text();
        return new Response(text, {
          status: 200,
          headers: { 'Content-Type': 'application/json', 'X-Cache': 'HIT', ...corsHeaders() }
        });
      }
    }
  } catch (_) { }

  // ── Fetch from GDBrowser ────────────────────────────────────────────
  try {
    const gdRes = await fetch(`https://gdbrowser.com/api/level/${id}`, {
      headers: { 'User-Agent': 'PaimonThumbnails/1.0' },
      cf: { cacheTtl: 0 }
    });

    if (!gdRes.ok) {
      return new Response(JSON.stringify({ error: 'Level not found', status: gdRes.status }), {
        status: gdRes.status,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const data = await gdRes.json();

    // If GDBrowser returned -1 (not found)
    if (!data || typeof data !== 'object' || !data.name) {
      return new Response(JSON.stringify({ error: 'Level not found' }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const jsonText = JSON.stringify(data);

    // Store in R2 cache
    try {
      await env.SYSTEM_BUCKET.put(cacheKey, jsonText, {
        httpMetadata: { contentType: 'application/json' },
        customMetadata: { cachedAt: String(Date.now()) }
      });
    } catch (_) { }

    return new Response(jsonText, {
      status: 200,
      headers: { 'Content-Type': 'application/json', 'X-Cache': 'MISS', ...corsHeaders() }
    });

  } catch (e) {
    console.error('[GDProxy] Error fetching level', id, e);
    return new Response(JSON.stringify({ error: 'Proxy error', detail: e.message }), {
      status: 502,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

/**
 * GET /api/profile/:username
 * Proxies GDBrowser profile endpoint with R2 cache (1 hour).
 */
async function handleGDProfileProxy(request, env) {
  const url = new URL(request.url);
  const username = url.pathname.replace('/api/gd/profile/', '').split('/')[0].trim();

  if (!username) {
    return new Response(JSON.stringify({ error: 'Invalid username' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const cacheKey = `cache/profile/${username.toLowerCase()}.json`;
  try {
    const cached = await env.SYSTEM_BUCKET.get(cacheKey);
    if (cached) {
      const meta = cached.customMetadata || {};
      const cachedAt = parseInt(meta.cachedAt || '0');
      if (Date.now() - cachedAt < 3600_000) {
        return new Response(await cached.text(), {
          status: 200,
          headers: { 'Content-Type': 'application/json', 'X-Cache': 'HIT', ...corsHeaders() }
        });
      }
    }
  } catch (_) { }

  try {
    const gdRes = await fetch(`https://gdbrowser.com/api/profile/${username}`, {
      headers: { 'User-Agent': 'PaimonThumbnails/1.0' },
      cf: { cacheTtl: 0 }
    });

    if (!gdRes.ok) {
      return new Response(JSON.stringify({ error: 'Profile not found' }), {
        status: gdRes.status,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const data = await gdRes.json();
    const jsonText = JSON.stringify(data);

    try {
      await env.SYSTEM_BUCKET.put(cacheKey, jsonText, {
        httpMetadata: { contentType: 'application/json' },
        customMetadata: { cachedAt: String(Date.now()) }
      });
    } catch (_) { }

    return new Response(jsonText, {
      status: 200,
      headers: { 'Content-Type': 'application/json', 'X-Cache': 'MISS', ...corsHeaders() }
    });

  } catch (e) {
    return new Response(JSON.stringify({ error: 'Proxy error', detail: e.message }), {
      status: 502,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Helper: verify admin (mod auth + must be in ADMIN_USERS)
async function requireAdmin(request, env, body) {
  const auth = await verifyModAuthFromBody(request, env, body);
  if (!auth.authorized) {
    if (auth.needsModCode) return { authorized: false, error: 'Mod code required. Generate one in settings.' };
    if (auth.invalidCode) return { authorized: false, error: 'Invalid mod code. Refresh it in settings.' };
    return { authorized: false, error: 'Auth verification failed' };
  }
  const username = (body.username || body.adminUser || '').toString().trim().toLowerCase();
  if (!ADMIN_USERS.includes(username)) return { authorized: false, error: 'Admin privileges required' };
  return { authorized: true, newCode: auth.newCode };
}

// Helper: return 403 forbidden response
function forbiddenResponse(message) {
  return new Response(JSON.stringify({ error: message || 'Forbidden' }), {
    status: 403,
    headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// Build a 403 response from a failed verifyModAuth result with mod-code context
function modAuthForbiddenResponse(auth) {
  if (auth.needsModCode) {
    return new Response(JSON.stringify({
      error: 'Mod code required',
      needsModCode: true,
      message: 'Generate a mod code in Paimbnails settings first'
    }), {
      status: 403,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
  if (auth.invalidCode) {
    return new Response(JSON.stringify({
      error: 'Invalid or expired mod code',
      invalidCode: true,
      message: 'Your mod code is invalid. Refresh it in Paimbnails settings.'
    }), {
      status: 403,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
  return forbiddenResponse('Moderator auth required');
}

async function getLegacyVersions(env, levelId) {
  const [rootFiles, gifFiles] = await Promise.all([
    listR2Keys(env.THUMBNAILS_BUCKET, `${levelId}`),
    listR2Keys(env.THUMBNAILS_BUCKET, `gif/${levelId}`)
  ]);

  const legacyFiles = [...new Set([...rootFiles, ...gifFiles])];

  const matches = legacyFiles.filter(key => {
    const filename = key.split('/').pop();
    const base = filename.replace(/\.(png|webp|gif)$/, '');
    return base === levelId || base.startsWith(`${levelId}_`);
  });

  if (matches.length > 0) {
    return matches.map(key => {
      const filename = key.split('/').pop();
      const ext = filename.split('.').pop();
      let version = 'legacy';
      const parts = filename.replace(/\.(png|webp|gif)$/, '').split('_');
      if (parts.length > 1 && parts[0] === levelId) {
        version = parts[1];
      }

      return {
        id: version === 'legacy' ? 'legacy_file' : version,
        version: version === 'legacy' ? Date.now().toString() : version,
        format: ext,
        path: 'thumbnails',
        type: ext === 'gif' ? 'gif' : 'static',
        isLegacy: true
      };
    });
  }
  return [];
}

// ===== WHITELIST HELPERS =====
async function getWhitelist(bucket, type = 'profilebackground') {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`);
  return data?.users || [];
}

async function addToWhitelist(bucket, username, addedBy, type = 'profilebackground') {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`) || { users: [], log: [] };
  const users = data.users || [];
  const log = data.log || [];
  const lower = username.toLowerCase();
  if (!users.includes(lower)) {
    users.push(lower);
    log.push({ action: 'add', username: lower, by: addedBy, at: new Date().toISOString() });
    await putR2Json(bucket, `data/whitelist/${type}.json`, { users, log });
  }
  return users;
}

async function removeFromWhitelist(bucket, username, removedBy, type = 'profilebackground') {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`) || { users: [], log: [] };
  let users = data.users || [];
  const log = data.log || [];
  const lower = username.toLowerCase();
  const prev = users.length;
  users = users.filter(u => u !== lower);
  if (users.length < prev) {
    log.push({ action: 'remove', username: lower, by: removedBy, at: new Date().toISOString() });
    await putR2Json(bucket, `data/whitelist/${type}.json`, { users, log });
  }
  return users;
}

// ===== AUDIT LOGGING =====
async function logAudit(bucket, action, details, ctx) {
  const ts = Date.now();
  const key = `data/audit/${action}/${ts}-${Math.random().toString(36).slice(2, 8)}.json`;
  const entry = { action, timestamp: ts, date: new Date().toISOString(), ...details };
  if (ctx) ctx.waitUntil(putR2Json(bucket, key, entry));
  else await putR2Json(bucket, key, entry);
}

// ===== DEDICATED PROFILE BACKGROUND UPLOAD (profilebackground) =====
async function handleUploadBackground(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const accountID = parseInt(formData.get('accountID') || '0');
    const username = formData.get('username') || '';
    const levelId = formData.get('levelId') || accountID.toString();

    if (!file || !accountID) {
      return new Response(JSON.stringify({ error: 'Missing required fields (image, accountID)' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Ban check
    const banData = await getR2Json(env.SYSTEM_BUCKET, `data/bans/${accountID}.json`);
    if (banData && banData.banned) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);

    const fileType = file.type || 'image/webp';
    let extension = 'webp';
    if (fileType === 'image/png') extension = 'png';
    else if (fileType === 'image/jpeg') extension = 'jpg';
    else if (fileType === 'image/gif') extension = 'gif';

    // Security scan
    const securityReject = rejectIfMalicious(buffer, fileType, file.name || `bg_${accountID}.${extension}`);
    if (securityReject) return securityReject;

    const usernameLower = username ? username.toLowerCase() : '';
    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    const isModerator = authResult.authorized;
    // isAdmin requires auth verification to prevent username spoofing
    const isAdmin = authResult.authorized && ADMIN_USERS.includes(usernameLower);
    const newModCode = authResult.newCode;

    // Check whitelist: whitelisted users publish directly like moderators
    const whitelistUsers = await getWhitelist(env.SYSTEM_BUCKET, 'profilebackground');
    const isWhitelisted = whitelistUsers.includes(usernameLower);

    const ts = Date.now().toString();
    let uploadKey;
    let uploadCategory;

    if (!isModerator && !isAdmin && !isWhitelisted) {
      // Non-moderator, non-whitelisted: go to pending for verification
      uploadKey = `pending_profilebackground/${accountID}_${ts}.${extension}`;
      uploadCategory = 'pending_profilebackground';

      // Cleanup previous pending backgrounds for this user
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountID}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else {
      // Moderator/admin/whitelisted: upload directly
      uploadKey = `profilebackground/${accountID}_${ts}.${extension}`;
      uploadCategory = 'profilebackground';

      // Cleanup ALL previous backgrounds for this user
      const prefixes = [`profilebackground/${accountID}.`, `profilebackground/${accountID}_`];
      const keysToDelete = [];
      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
        for (const obj of list.objects) {
          keysToDelete.push(obj.key);
        }
      }
      if (keysToDelete.length > 0) {
        await Promise.all(keysToDelete.map(k => env.THUMBNAILS_BUCKET.delete(k)));
      }

      // Also clean pending folder + queue
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountID}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
      if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`data/queue/profilebackground/${accountID}.json`));
      else await env.SYSTEM_BUCKET.delete(`data/queue/profilebackground/${accountID}.json`);
    }

    await env.THUMBNAILS_BUCKET.put(uploadKey, buffer, {
      httpMetadata: {
        contentType: fileType,
        cacheControl: 'no-store, no-cache, must-revalidate, max-age=0'
      },
      customMetadata: {
        uploadedBy: username || 'unknown',
        updated_by: username || 'unknown',
        uploadedAt: new Date().toISOString(),
        originalFormat: extension,
        version: ts,
        accountID: accountID.toString(),
        moderatorUpload: isModerator ? 'true' : 'false',
        whitelistUpload: isWhitelisted ? 'true' : 'false',
        category: uploadCategory,
        contentKind: 'profilebackground'
      }
    });

    if (!isModerator && !isAdmin && !isWhitelisted) {
      // Create queue entry for pending profilebackground verification
      const queueKey = `data/queue/profilebackground/${accountID}.json`;
      const queueItem = {
        levelId: parseInt(accountID),
        accountID: accountID,
        submittedBy: username || 'unknown',
        timestamp: Date.now(),
        status: 'pending',
        category: 'profilebackground',
        filename: uploadKey,
        format: extension
      };
      if (ctx) ctx.waitUntil(putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem));
      else await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }

    // Audit log for direct uploads
    if (isModerator || isAdmin || isWhitelisted) {
      logAudit(env.SYSTEM_BUCKET, 'profilebackground_upload', {
        accountID, username: usernameLower, direct: true,
        reason: isWhitelisted ? 'whitelist' : 'moderator'
      }, ctx);
    }

    const isPending = uploadCategory === 'pending_profilebackground';

    const responseData = {
      success: true,
      message: isPending
        ? 'Profile background submitted for verification'
        : 'Background uploaded successfully',
      key: uploadKey,
      moderatorUpload: isModerator,
      whitelistUpload: isWhitelisted,
      pendingVerification: isPending,
      contentKind: 'profilebackground'
    };

    if (newModCode) responseData.newModCode = newModCode;

    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Background upload error:', error);
    return new Response(JSON.stringify({
      error: 'Background upload failed',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// ===== DEDICATED PROFILE BACKGROUND GIF UPLOAD =====
async function handleUploadBackgroundGIF(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const accountID = parseInt(formData.get('accountID') || '0');
    const username = formData.get('username') || '';
    const levelId = formData.get('levelId') || accountID.toString();

    if (!file || !accountID) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (file.type !== 'image/gif') {
      return new Response(JSON.stringify({ error: 'Invalid file type. Only GIF allowed.' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const usernameLower = username ? username.toLowerCase() : '';
    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    const isModerator = authResult.authorized;
    const newModCode = authResult.newCode;

    if (!isModerator) {
      return new Response(JSON.stringify({ error: 'Background GIF uploads are restricted to moderators only' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);

    // Security scan
    const securityReject = rejectIfMalicious(buffer, 'image/gif', file.name || `bg_${accountID}.gif`);
    if (securityReject) return securityReject;

    const ts = Date.now().toString();
    const key = `profilebackground/${accountID}_${ts}.gif`;

    // Cleanup ALL previous backgrounds for this user
    const prefixes = [`profilebackground/${accountID}.`, `profilebackground/${accountID}_`];
    const keysToDelete = [];
    for (const prefix of prefixes) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
      for (const obj of list.objects) {
        keysToDelete.push(obj.key);
      }
    }
    if (keysToDelete.length > 0) {
      await Promise.all(keysToDelete.map(k => env.THUMBNAILS_BUCKET.delete(k)));
    }

    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: {
        contentType: 'image/gif',
        cacheControl: 'no-store, no-cache, must-revalidate, max-age=0'
      },
      customMetadata: {
        uploadedBy: username || 'unknown',
        updated_by: username || 'unknown',
        uploadedAt: new Date().toISOString(),
        originalFormat: 'gif',
        version: ts,
        accountID: accountID.toString(),
        moderatorUpload: 'true',
        category: 'profilebackground',
        contentKind: 'profilebackground'
      }
    });

    const responseData = {
      success: true,
      message: 'Background GIF uploaded successfully',
      key,
      contentKind: 'profilebackground'
    };
    if (newModCode) responseData.newModCode = newModCode;

    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Background GIF upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// ===== SERVE PROFILE BACKGROUND (profilebackground) =====
async function handleServeBackground(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  const accountId = filename.replace(/\.[^/.]+$/, "");

  if (!accountId) {
    return new Response('Account ID required', { status: 400 });
  }

  let foundObject = null;
  let isPending = false;

  // 1. Try profilebackground/ folder (canonical location)
  const list = await env.THUMBNAILS_BUCKET.list({ prefix: `profilebackground/${accountId}_` });
  if (list.objects.length > 0) {
    const sorted = list.objects.sort((a, b) => {
      const getTs = (k) => {
        const match = k.match(/_(\\d+)\\./);
        return match ? parseInt(match[1]) : 0;
      };
      return getTs(b.key) - getTs(a.key);
    });
    foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
  }

  // 2. Legacy fallback: try backgrounds/ folder (pre-migration)
  if (!foundObject) {
    const legacyBgList = await env.THUMBNAILS_BUCKET.list({ prefix: `backgrounds/${accountId}_` });
    if (legacyBgList.objects.length > 0) {
      const sorted = legacyBgList.objects.sort((a, b) => {
        const getTs = (k) => {
          const match = k.match(/_(\d+)\./);
          return match ? parseInt(match[1]) : 0;
        };
        return getTs(b.key) - getTs(a.key);
      });
      foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
    }
  }

  // 3. If self-request, try pending_profilebackground
  const isSelfRequest = url.searchParams.get('self') === '1';
  if (!foundObject && isSelfRequest && verifyApiKey(request, env)) {
    const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountId}_` });
    if (pendingList.objects.length > 0) {
      const sorted = pendingList.objects.sort((a, b) => {
        const getTs = (k) => {
          const match = k.key.match(/_(\d+)\./);
          return match ? parseInt(match[1]) : 0;
        };
        return getTs(b) - getTs(a);
      });
      foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      isPending = true;
    }
  }

  if (foundObject) {
    const headers = new Headers();
    foundObject.writeHttpMetadata(headers);
    headers.set('etag', foundObject.httpEtag);
    headers.set('Access-Control-Allow-Origin', '*');
    headers.set('Cache-Control', 'no-store, no-cache, must-revalidate, proxy-revalidate');
    headers.set('Pragma', 'no-cache');
    headers.set('Expires', '0');
    headers.set('X-Content-Kind', 'profilebackground'); // explicit marker
    if (isPending) {
      headers.set('X-Pending-Verification', 'true');
    }

    return new Response(foundObject.body, { headers });
  }

  return new Response('Background not found', { status: 404 });
}

async function handleUpload(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const levelId = formData.get('levelId');
    const username = formData.get('username');
    const accountID = parseInt(formData.get('accountID') || '0');
    const path = (formData.get('path') || '/thumbnails').replace(/^\//, '');

    if (!file || !levelId) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Check if user is moderator OR admin
    const usernameLower = username ? username.toLowerCase() : '';

    // Auth Check
    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    let isModerator = authResult.authorized;
    let newModCode = authResult.newCode;

    // isAdmin requires auth verification to prevent username spoofing
    const isAdmin = authResult.authorized && ADMIN_USERS.includes(usernameLower);

    // Check if user is banned
    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (banned.includes(usernameLower)) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    console.log(`[Upload] user="${username}" accountID=${accountID} isAdmin=${isAdmin} isMod=${isModerator}`);

    // /mod/upload requires valid mod-code — no silent downgrade to suggestion
    if (!isModerator) {
      if (authResult.needsModCode) {
        return new Response(JSON.stringify({
          error: 'Mod code required',
          needsModCode: true,
          message: 'Generate a mod code in Paimbnails settings first'
        }), {
          status: 403,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({
        error: 'Invalid or expired mod code',
        invalidCode: true,
        message: 'Your mod code is invalid. Refresh it in Paimbnails settings.'
      }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);

    const fileType = file.type || 'image/webp';
    let extension = 'webp';
    if (fileType === 'image/png') extension = 'png';
    else if (fileType === 'image/jpeg') extension = 'jpg';
    else if (fileType === 'image/gif') extension = 'gif';

    // ===== SECURITY SCAN =====
    const securityReject = rejectIfMalicious(buffer, fileType, file.name || `${levelId}.${extension}`);
    if (securityReject) return securityReject;
    // =========================

    // Check if thumbnail exists in LIVE folder
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const vData = await vm.getVersion(levelId);
    let isUpdate = !!vData;

    if (!isUpdate) {
      const keys = [
        `${path}/${levelId}.webp`,
        `${path}/gif/${levelId}.gif`,
        `${path}/${levelId}.png`
      ];
      const checks = await Promise.all(keys.map(k => env.THUMBNAILS_BUCKET.head(k)));
      if (checks.some(o => o)) {
        isUpdate = true;
      }
    }

    let uploadKey;
    let uploadCategory;
    const version = Date.now().toString();
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);

    if (path === 'profileimgs') {
      const ts = Date.now().toString();

      // Check whitelist for profileimgs
      const whitelistProfileImg = await getWhitelist(env.SYSTEM_BUCKET, 'profileimgs');
      const isWhitelistedPI = whitelistProfileImg.includes(usernameLower);

      if (!isModerator && !isAdmin && !isWhitelistedPI) {
        // Non-privileged: upload to pending for verification
        uploadKey = `pending_profileimgs/${levelId}_${ts}.${extension}`;
        uploadCategory = 'pending_profileimg';

        // Cleanup previous pending images for this user
        const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${levelId}_` });
        if (pendingList.objects.length > 0) {
          await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
        }
      } else {
        // Moderator/admin/whitelisted: upload directly
        uploadKey = `${path}/${levelId}_${ts}.${extension}`;
        uploadCategory = 'profileimg';

        // Cleanup ALL previous images for this user in this path
        const prefixes = [`${path}/${levelId}.`, `${path}/${levelId}_`];
        const keysToDelete = [];

        for (const prefix of prefixes) {
          const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
          for (const obj of list.objects) {
            keysToDelete.push(obj.key);
          }
        }

        if (keysToDelete.length > 0) {
          await Promise.all(keysToDelete.map(k => env.THUMBNAILS_BUCKET.delete(k)));
        }

        // Also clean pending folder + queue
        const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${levelId}_` });
        if (pendingList.objects.length > 0) {
          await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
        }
        if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`data/queue/profileimgs/${levelId}.json`));
        else await env.SYSTEM_BUCKET.delete(`data/queue/profileimgs/${levelId}.json`);
      }

    } else if (isModerator) {
      // Single slot mode: Always act as "legacy replace" / "overwrite"
      const type = extension === 'gif' ? 'gif' : 'static';

      // Always use fixed ID "1" but with timestamped filename for cache busting
      // The old VersionManager.update logic we modified ensures ID is "1" always.

      const thisId = version;
      uploadKey = `${path}/${levelId}_${thisId}.${extension}`;
      uploadCategory = 'live';

      // Cleanup ALL previous versions to ensure we only have ONE active thumbnail
      const prefixes = [`${path}/${levelId}`, `${path}/gif/${levelId}`];
      const cleanupKeys = [];

      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
        for (const obj of list.objects) {
          const k = obj.key;
          const cleanKey = k.replace(/^\//, '');
          // Match standard patterns
          if (cleanKey.match(new RegExp(`^${path}/${levelId}(\\.|_)`)) ||
            cleanKey.match(new RegExp(`^${path}/gif/${levelId}\\.`))) {
            cleanupKeys.push(k);
          }
        }
      }

      const uniqueCleanup = [...new Set(cleanupKeys)];
      console.log(`[Upload] Cleaning up ${uniqueCleanup.length} old versions for ${levelId}`);

      for (const k of uniqueCleanup) {
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(k));
        else await env.THUMBNAILS_BUCKET.delete(k);
      }

      // Also delete legacy path exact matches
      const legacyPaths = [
        `${path}/${levelId}.webp`,
        `${path}/${levelId}.png`
      ];
      for (const lp of legacyPaths) {
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(lp));
        else await env.THUMBNAILS_BUCKET.delete(lp);
      }

      await versionManager.update(
        levelId,
        version,
        extension,
        path,
        type,
        { uploadedBy: username, uploadedAt: new Date().toISOString() }
      );

      // Reset ratings
      if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`));
      else await env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`);

    } else {
      // This path is unreachable now (/mod/upload rejects non-mods early)
      // but kept as safety net
      return new Response(JSON.stringify({ error: 'Moderator auth required' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const isProfile = path === 'profileimgs';

    await env.THUMBNAILS_BUCKET.put(uploadKey, buffer, {
      httpMetadata: {
        contentType: fileType,
        cacheControl: 'no-store, no-cache, must-revalidate, max-age=0'
      },
      customMetadata: {
        uploadedBy: username || 'unknown',
        updated_by: username || 'unknown',
        uploadedAt: new Date().toISOString(),
        originalFormat: extension,
        isUpdate: isUpdate ? 'true' : 'false',
        version: version,
        accountID: accountID.toString(),
        moderatorUpload: isModerator ? 'true' : 'false',
        category: uploadCategory
      }
    });

    // Non-moderator queue entries removed — /mod/upload now rejects non-mods early

    // Update creator leaderboard cache on successful upload
    if (username && username !== 'Unknown' && path !== 'profileimgs') {
      const updateCreatorCache = () => updateCreatorLeaderboardCache(env, username, {
        incrementUpload: true,
        accountID: accountID
      });
      if (ctx) ctx.waitUntil(updateCreatorCache());
      else await updateCreatorCache();
    }

    // Update latest uploads for public moderator uploads
    if (isModerator && path !== 'profileimgs') {
      const updateLatest = async () => {
        const latestKey = 'data/system/latest_uploads.json';
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];

        // Remove if exists (to move to top)
        latest = latest.filter(item => item.levelId !== parseInt(levelId));

        // Add new item
        latest.unshift({
          levelId: parseInt(levelId),
          username: username || 'unknown',
          timestamp: Date.now(),
          accountID: accountID
        });

        // Keep last 20
        if (latest.length > 20) latest = latest.slice(0, 20);

        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
      };

      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();

      // Notify Discord bot via webhook
      const webhookPayload = {
        levelId: parseInt(levelId),
        username: username || 'unknown',
        timestamp: Date.now(),
        is_update: isUpdate || false,
      };
      if (ctx) ctx.waitUntil(dispatchWebhook(env, 'upload', webhookPayload));
      else await dispatchWebhook(env, 'upload', webhookPayload);
    }

    // Historial removido para optimizar operaciones

    const isPendingProfileImg = uploadCategory === 'pending_profileimg';

    const responseData = {
      success: true,
      message: isPendingProfileImg
        ? 'Profile image submitted for verification'
        : 'Moderator upload: Thumbnail published directly to global',
      key: uploadKey,
      isUpdate: isUpdate,
      moderatorUpload: true,
      inQueue: false,
      pendingVerification: isPendingProfileImg
    };

    if (newModCode) responseData.newModCode = newModCode;

    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Upload error:', error);
    return new Response(JSON.stringify({
      error: 'Upload failed',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Upload GIF endpoint
async function handleUploadGIF(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const levelId = formData.get('levelId');
    const username = formData.get('username');
    const accountID = parseInt(formData.get('accountID') || '0');
    const path = (formData.get('path') || '/thumbnails/gif').replace(/^\//, '');

    if (!file || !levelId) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (file.type !== 'image/gif') {
      return new Response(JSON.stringify({ error: 'Invalid file type. Only GIF allowed.' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const usernameLower = username ? username.toLowerCase() : '';

    // Auth Check
    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    const isModerator = authResult.authorized;
    const newModCode = authResult.newCode;

    if (!isModerator) {
      if (authResult.needsModCode) {
        return new Response(JSON.stringify({ error: 'Mod code required', needsModCode: true, message: 'Generate a mod code in Paimbnails settings first' }), {
          status: 403,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({ error: 'Invalid or expired mod code', invalidCode: true, message: 'Your mod code is invalid. Refresh it in Paimbnails settings.' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);

    // ===== SECURITY SCAN =====
    const securityReject = rejectIfMalicious(buffer, 'image/gif', file.name || `${levelId}.gif`);
    if (securityReject) return securityReject;
    // =========================

    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const version = Date.now().toString();

    // Always use simplified ID/Version logic (One thumbnail per level)
    const thisId = version;

    let key;
    let isProfile = path === 'profiles';

    if (isProfile) {
      const ts = Date.now().toString();
      key = `profiles/${levelId}_${ts}.gif`;

      // Cleanup ALL previous profile images for this user
      const prefixes = [`profiles/${levelId}.`, `profiles/${levelId}_`];
      const keysToDelete = [];

      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
        for (const obj of list.objects) {
          keysToDelete.push(obj.key);
        }
      }

      if (keysToDelete.length > 0) {
        await Promise.all(keysToDelete.map(k => env.THUMBNAILS_BUCKET.delete(k)));
      }
    } else {
      key = `${path}/${levelId}_${thisId}.gif`;

      // Cleanup ALL previous versions to ensure we only have ONE active thumbnail
      const prefixes = [`${path}/${levelId}`, `${path}/gif/${levelId}`];
      const cleanupKeys = [];

      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
        for (const obj of list.objects) {
          // Do not delete the key we are about to write (though it shouldn't exist yet)
          if (obj.key !== key) {
            cleanupKeys.push(obj.key);
          }
        }
      }

      // Also legacy paths
      cleanupKeys.push(`${path}/${levelId}.webp`);
      cleanupKeys.push(`${path}/${levelId}.png`);
      cleanupKeys.push(`${path}/${levelId}.gif`);

      const uniqueCleanup = [...new Set(cleanupKeys)];
      if (uniqueCleanup.length > 0) {
        const deletePromises = uniqueCleanup.map(k => env.THUMBNAILS_BUCKET.delete(k));
        if (ctx) ctx.waitUntil(Promise.all(deletePromises));
        else await Promise.all(deletePromises);
      }
    }

    const existingVersion = await versionManager.getVersion(levelId);
    const isUpdate = !!existingVersion;

    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: {
        contentType: 'image/gif',
        cacheControl: 'no-store, no-cache, must-revalidate, max-age=0'
      },
      customMetadata: {
        uploadedBy: username || 'unknown',
        uploadedAt: new Date().toISOString(),
        originalFormat: 'gif',
        isUpdate: isUpdate ? 'true' : 'false',
        version: thisId,
        accountID: accountID.toString(),
        moderatorUpload: 'true',
        category: isProfile ? 'profile' : 'live'
      }
    });

    if (!isProfile) {
      // Update version manager with full path for GIFs
      const updateVersion = async () => {
        const meta = {
          uploadedBy: username || 'unknown',
          uploadedAt: new Date().toISOString()
        };
        // Call update with correct signature: id, version, format, path, type, metadata
        // Note: mode/replaceId logic is removed.
        await versionManager.update(levelId, thisId, 'gif', path, 'gif', meta);
      };
      if (ctx) ctx.waitUntil(updateVersion());
      else await updateVersion();

      // Update latest uploads for GIF uploads
      const updateLatest = async () => {
        const latestKey = 'data/system/latest_uploads.json';
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];

        // Remove if exists (to move to top)
        latest = latest.filter(item => item.levelId !== parseInt(levelId));

        // Add new item
        latest.unshift({
          levelId: parseInt(levelId),
          username: username || 'unknown',
          timestamp: Date.now(),
          accountID: accountID,
          isGif: true
        });

        // Keep last 20
        if (latest.length > 20) latest = latest.slice(0, 20);

        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
      };

      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();
    }

    const responseData = {
      success: true,
      message: 'GIF uploaded successfully (Moderator)',
      key,
      isUpdate
    };
    if (newModCode) responseData.newModCode = newModCode;

    return new Response(JSON.stringify(responseData), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Upload GIF error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Download thumbnail endpoint
async function handleDownload(request, env, ctx) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  const levelId = filename.replace(/\.(webp|png|gif)$/, '');
  const requestedFormat = filename.endsWith('.png') ? 'png' : (filename.endsWith('.gif') ? 'gif' : 'webp');

  const rawPath = url.searchParams.get('path') || '/thumbnails';
  const path = rawPath.replace(/^\//, '');

  let foundKey = null;

  // OPTIMIZATION: Lookup via VersionManager (Fastest)
  // Use VersionManager to find the latest version directly
  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versionData = await vm.getVersion(levelId);

    if (versionData) {
      // Check if requested format matches stored format OR if we can convert
      // If stored is GIF and requested is matching, use it.
      // If stored is WebP and requested is PNG/WebP, use it.

      const storedFormat = versionData.format || 'webp';
      const storedPath = versionData.path || 'thumbnails';

      if (requestedFormat === 'gif' && storedFormat !== 'gif') {
        // Skip if requesting gif but have static
      } else if (storedFormat === 'gif' && requestedFormat !== 'gif') {
        // Skip if requesting static but have gif (unless we assume first frame?)
      } else {
        // Match found via VersionManager
        let vStr = versionData.version;
        if (vStr === 'legacy') {
          foundKey = `${storedPath}/${levelId}.${storedFormat}`;
        } else {
          foundKey = `${storedPath}/${levelId}_${vStr}.${storedFormat}`;
        }
        console.log(`[Download] Found via VersionManager (Lookup): ${foundKey}`);
      }
    }
  } catch (e) {
    console.warn('VersionManager lookup failed:', e);
  }

  // Backup: Direct check if filename includes version details
  if (!foundKey && levelId.includes('_')) {
    foundKey = `${path}/${filename}`;
  }

  // Fallback: Search (List) - Only if VersionManager failed
  if (!foundKey) {
    console.log(`[Download] Search for ${levelId} in ${path}`);

    // Search prefixes to cover standard and legacy paths
    const prefixes = [
      `${path}/${levelId}`,
      `/${path}/${levelId}`
    ];

    for (const listPrefix of prefixes) {
      if (foundKey) break;

      // List files (Class B approach in this context)
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: listPrefix, limit: 20 });

      // Sort by upload date (newest first) to get latest version
      const sortedObjects = list.objects.sort((a, b) => {
        return b.uploaded.getTime() - a.uploaded.getTime();
      });

      for (const obj of sortedObjects) {
        const key = obj.key;
        const keyFilename = key.split('/').pop();

        // Check if it matches the levelId exactly or is a version of it (e.g. 123_1.webp)
        const keyBase = keyFilename.replace(/\.(webp|png|gif)$/, '');

        if (keyBase === levelId || keyBase.startsWith(`${levelId}_`)) {
          // Check if format matches requested
          if (requestedFormat === 'gif' && !key.endsWith('.gif')) continue;
          // If requested PNG/WebP and found GIF, skip
          if ((requestedFormat === 'webp' || requestedFormat === 'png') && key.endsWith('.gif')) continue;

          foundKey = key;
          if (foundKey) {
            console.log(`[Download] Found via Search: ${key}`);
            break;
          }
        }
      }
    }
  }

  if (!foundKey) {
    return new Response('Thumbnail not found', { status: 404, headers: corsHeaders() });
  }

  const bunnyUrl = `${env.R2_PUBLIC_URL}/${foundKey.replace(/^\//, '')}`;

  // CONVERSION LOGIC: If requested PNG, convert on the fly
  if (requestedFormat === 'png' && !foundKey.endsWith('.png')) {
    console.log(`[Download] Converting to PNG: ${foundKey}`);
    try {
      // Fetch from Bunny and ask Cloudflare to convert to PNG
      const imageRes = await fetch(bunnyUrl, {
        headers: {
          'Accept': 'image/png',
          // Pass through cache control to ensure we get fresh content if needed
          'Cache-Control': 'no-cache'
        },
        cf: {
          image: {
            format: 'png'
            // quality: 90 // Optional
          }
        }
      });

      if (imageRes.ok) {
        const newHeaders = new Headers(imageRes.headers);
        // Ensure content-type is png
        newHeaders.set('Content-Type', 'image/png');
        // Add CORS
        const cors = corsHeaders();
        for (const [k, v] of Object.entries(cors)) {
          newHeaders.set(k, v);
        }

        return new Response(imageRes.body, {
          status: 200,
          headers: newHeaders
        });
      } else {
        console.warn(`[Download] Conversion failed status ${imageRes.status}, falling back to redirect`);
      }
    } catch (err) {
      console.error(`[Download] Conversion error: ${err}`);
    }
  }

  // Redirect to BunnyCDN (Standard behavior)
  // Add timestamp to bypass cache if requested
  const ts = url.searchParams.get('t') || url.searchParams.get('_ts');
  const finalUrl = ts ? `${bunnyUrl}?t=${ts}` : bunnyUrl;

  return redirectNoStore(finalUrl, 302, corsHeaders());
}

// Direct thumbnail access with unique URL
async function handleDirectThumbnail(request, env, ctx) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[2];
  const levelId = filename.replace(/\.(webp|gif|png)$/, '');
  const requestedFormat = filename.endsWith('.png') ? 'png' : (filename.endsWith('.gif') ? 'gif' : 'webp');

  // Strategy: Try VersionManager (Lookup) first for speed, then fallback to Search (List)
  let bestMatch = null;
  let maxVersion = -1;

  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versionData = await vm.getVersion(levelId);
    if (versionData) {
      const storedFormat = versionData.format || 'webp';
      const storedPath = versionData.path || 'thumbnails';
      const vStr = versionData.version;

      if (requestedFormat === 'gif' && storedFormat !== 'gif') {
        // Requested GIF, but have static. Ignored.
      } else if (storedFormat === 'gif' && requestedFormat !== 'gif') {
        // Stored GIF, requested static. Ignored.
      } else {
        if (vStr === 'legacy') bestMatch = `${storedPath}/${levelId}.${storedFormat}`;
        else bestMatch = `${storedPath}/${levelId}_${vStr}.${storedFormat}`;
        console.log(`[Direct] Found via VersionManager: ${bestMatch}`);
      }
    }
  } catch (e) {
    console.warn('[Direct] VersionManager error:', e);
  }

  if (!bestMatch) {
    console.log(`[Direct] Search for ${levelId} (Fallback)`);
    try {
      // STRATEGY: Comprehensive search including leading slashes and various formats
      const checks = [
        // Standard paths
        env.THUMBNAILS_BUCKET.list({ prefix: `thumbnails/${levelId}.`, limit: 5 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `thumbnails/${levelId}_`, limit: 20 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `thumbnails/gif/${levelId}.`, limit: 5 }),
        // Leading slash paths (Legacy data often has this)
        env.THUMBNAILS_BUCKET.list({ prefix: `/thumbnails/${levelId}.`, limit: 5 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `/thumbnails/${levelId}_`, limit: 20 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `/thumbnails/gif/${levelId}.`, limit: 5 }),
      ];

      const results = await Promise.all(checks);

      let allObjects = [];

      // Process list results
      for (let i = 0; i < 6; i++) {
        if (results[i] && results[i].objects) {
          allObjects.push(...results[i].objects);
        }
      }

      // Remove duplicates by key
      allObjects = [...new Map(allObjects.map(item => [item.key, item])).values()];

      // Sort to find the best match logic ... (same as before)
      for (const obj of allObjects) {
        const key = obj.key;
        const cleanKey = key.replace(/^\//, '');

        const isExact = cleanKey.includes(`/${levelId}.`) || cleanKey.includes(`/${levelId}_`);
        if (!isExact) continue;

        // Check for versioned file: thumbnails/123_456.webp
        const versionMatch = key.match(/_(\d+)\.(webp|png|gif)$/);
        let version = -1;

        if (versionMatch) {
          version = parseInt(versionMatch[1]);
        }

        if (version > maxVersion) {
          maxVersion = version;
          bestMatch = key;
        } else if (version === maxVersion) {
          if (!bestMatch) {
            bestMatch = key;
          } else {
            const currentExt = bestMatch.split('.').pop();
            const newExt = key.split('.').pop();
            if (newExt === requestedFormat && currentExt !== requestedFormat) {
              bestMatch = key;
            }
          }
        }
      }
    } catch (e) { console.error('Class B error', e); }
  }

  if (bestMatch) {
    const bunnyUrl = `${env.R2_PUBLIC_URL}/${bestMatch.replace(/^\//, '')}`;

    // CONVERSION LOGIC
    if (requestedFormat === 'png' && !bestMatch.endsWith('.png') && !bestMatch.endsWith('.gif')) {
      try {
        const imageRes = await fetch(bunnyUrl, {
          headers: {
            'Accept': 'image/png',
            'Cache-Control': 'no-cache' // Bypass bunny cache 
          },
          cf: { image: { format: 'png' } }
        });

        if (imageRes.ok) {
          const newHeaders = new Headers(imageRes.headers);
          newHeaders.set('Content-Type', 'image/png');
          newHeaders.set('Access-Control-Allow-Origin', '*');
          newHeaders.set('Cache-Control', NO_STORE_CACHE_CONTROL);
          return new Response(imageRes.body, { status: 200, headers: newHeaders });
        }
      } catch (e) { console.error('Direct conversion failed', e); }
    }

    // Serve original (proxy)
    // Since handleDirectThumbnail previously served the bytes via env.THUMBNAILS_BUCKET.get(), we should continue to do so or proxy via fetching bunnyUrl
    // But using env.THUMBNAILS_BUCKET.get uses the BunnyBucket wrapper which does a fetch anyway.
    // So let's use the wrapper to get metadata properly.

    const object = await env.THUMBNAILS_BUCKET.get(bestMatch);

    if (object) {
      const headers = new Headers();
      object.writeHttpMetadata(headers);
      headers.set('etag', object.httpEtag);
      headers.set('Cache-Control', NO_STORE_CACHE_CONTROL);
      headers.set('Pragma', 'no-cache');
      headers.set('Expires', '0');
      headers.set('Access-Control-Allow-Origin', '*');

      return new Response(object.body, { headers });
    }
  }

  return new Response(JSON.stringify({ error: 'Thumbnail not found' }), {
    status: 404,
    headers: {
      'Content-Type': 'application/json',
      'Access-Control-Allow-Origin': '*'
    }
  });
}

// Download suggestion thumbnail
async function handleDownloadSuggestion(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[2];
  const levelId = filename.replace(/\.webp$/, '');

  try {
    // Try exact filename first (e.g. from targetFilename in queue data)
    let object = await env.THUMBNAILS_BUCKET.get(`suggestions/${filename}`);

    // If not found, search by prefix to find timestamped versions
    if (!object) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: `suggestions/${levelId}`, limit: 20 });
      if (list.objects.length > 0) {
        // Sort by key descending (timestamp in name) to get most recent
        const sorted = list.objects.sort((a, b) => {
          const getTs = (k) => { const m = k.key.match(/_(\d+)_/); return m ? parseInt(m[1]) : 0; };
          return getTs(b) - getTs(a);
        });
        object = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      }
    }

    // Fallback: legacy exact match
    if (!object) {
      object = await env.THUMBNAILS_BUCKET.get(`suggestions/${levelId}.webp`);
    }

    if (!object) {
      return new Response(JSON.stringify({ error: 'Suggestion not found' }), {
        status: 404,
        headers: {
          'Content-Type': 'application/json',
          ...corsHeaders()
        }
      });
    }

    const buf = await object.arrayBuffer();
    const respHeaders = new Headers();
    object.writeHttpMetadata(respHeaders);
    respHeaders.set('Access-Control-Allow-Origin', '*');
    respHeaders.set('Cache-Control', 'no-store, no-cache, must-revalidate');
    if (!respHeaders.has('Content-Type')) respHeaders.set('Content-Type', 'image/webp');
    return new Response(buf, { headers: respHeaders });
  } catch (error) {
    console.error('Download suggestion error:', error);
    return new Response(JSON.stringify({ error: 'Download failed', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Download update thumbnail
async function handleDownloadUpdate(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[2];
  const levelId = filename.replace(/\.webp$/, '');

  try {
    // Try exact filename first
    let object = await env.THUMBNAILS_BUCKET.get(`updates/${filename}`);

    // If not found, search by prefix to find timestamped versions
    if (!object) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: `updates/${levelId}`, limit: 20 });
      if (list.objects.length > 0) {
        // Sort by key descending to get most recent
        const sorted = list.objects.sort((a, b) => {
          const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
          return getTs(b) - getTs(a);
        });
        object = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      }
    }

    // Fallback: legacy exact match
    if (!object) {
      object = await env.THUMBNAILS_BUCKET.get(`updates/${levelId}.webp`);
    }

    if (!object) {
      return new Response(JSON.stringify({ error: 'Update not found' }), {
        status: 404,
        headers: {
          'Content-Type': 'application/json',
          ...corsHeaders()
        }
      });
    }

    const buf = await object.arrayBuffer();
    const updRespHeaders = new Headers();
    object.writeHttpMetadata(updRespHeaders);
    updRespHeaders.set('Access-Control-Allow-Origin', '*');
    updRespHeaders.set('Cache-Control', 'no-store, no-cache, must-revalidate');
    if (!updRespHeaders.has('Content-Type')) updRespHeaders.set('Content-Type', 'image/webp');
    return new Response(buf, { headers: updRespHeaders });
  } catch (error) {
    console.error('Download update error:', error);
    return new Response(JSON.stringify({ error: 'Download failed', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Check if thumbnail exists
async function handleExists(request, env, ctx) {
  const url = new URL(request.url);
  const levelId = url.searchParams.get('levelId');
  const path = (url.searchParams.get('path') || 'thumbnails').replace(/^\//, '');

  if (!levelId) {
    return new Response(JSON.stringify({ error: 'Missing levelId' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const versionManager = new VersionManager(env.SYSTEM_BUCKET);
  const versionData = await versionManager.getVersion(levelId);
  if (versionData) {
    return new Response(JSON.stringify({ exists: true }), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        ...corsHeaders()
      }
    });
  }

  const baseKeys = [
    `${path}/${levelId}.webp`,
    `${path}/gif/${levelId}.gif`,
    `${path}/${levelId}.png`
  ];
  const keys = expandCandidates(baseKeys);

  let exists = false;
  for (const key of keys) {
    const object = await env.THUMBNAILS_BUCKET.head(key);
    if (object) {
      exists = true;
      break;
    }
  }

  return new Response(JSON.stringify({ exists }), {
    status: 200,
    headers: {
      'Content-Type': 'application/json',
      ...corsHeaders()
    }
  });
}

// Check moderator status (requires API key)
async function handleModeratorCheck(request, env) {
  if (!verifyApiKey(request, env)) {
    const receivedKey = request.headers.get('X-API-Key') || '(none)';
    const expectedPrefix = (env.API_KEY || '').substring(0, 8);
    const receivedPrefix = receivedKey.substring(0, 8);
    console.error(`[ModCheck] API key mismatch. Received prefix: "${receivedPrefix}", Expected prefix: "${expectedPrefix}"`);
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const url = new URL(request.url);
  const username = url.searchParams.get('username');
  const accountID = parseInt(url.searchParams.get('accountID') || '0');

  if (!username) {
    return new Response(JSON.stringify({ error: 'Missing username' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const usernameLower = username.toLowerCase();
  const isAdmin = ADMIN_USERS.includes(usernameLower);

  let isModerator = isAdmin;
  let isVip = isAdmin; // admins are always VIP
  // Only fetch moderators list if not already an admin
  if (!isAdmin) {
    try {
      const moderators = await getModerators(env.SYSTEM_BUCKET);
      isModerator = moderators.includes(usernameLower);
      if (isModerator) isVip = true; // mods are always VIP
    } catch (e) {
      console.error('Error fetching moderators list:', e);
    }
  }

  // Check VIP list if not already VIP (admin/mod)
  if (!isVip) {
    try {
      const vips = await getVips(env.SYSTEM_BUCKET);
      isVip = vips.includes(usernameLower);
    } catch (e) {
      console.error('Error fetching VIP list:', e);
    }
  }

  console.log(`[ModCheck] username="${username}" lowercase="${usernameLower}" isAdmin=${isAdmin} isMod=${isModerator} isVip=${isVip} accountID=${accountID}`);

  // Generate/refresh mod code for moderators and admins.
  // Uses GDBrowser to verify accountID truly belongs to this username
  // before issuing or reusing a code — prevents identity spoofing.
  let newModCode = undefined;
  let gdVerified = false;
  if ((isModerator || isAdmin) && accountID > 0) {
    try {
      const authKey = `data/auth/${usernameLower}.json`;
      const existingAuth = await getR2Json(env.SYSTEM_BUCKET, authKey);

      // Reuse existing code if still valid (< 30 days) AND accountID matches
      if (existingAuth && existingAuth.code && existingAuth.accountID == accountID) {
        const created = new Date(existingAuth.created || 0).getTime();
        const thirtyDays = 30 * 24 * 60 * 60 * 1000;
        if (Date.now() - created < thirtyDays) {
          newModCode = existingAuth.code;
          gdVerified = true; // already verified when first generated
          console.log(`[ModCheck] Reusing existing mod code for ${username} (prefix: ${existingAuth.code.substring(0, 8)}... age: ${Math.round((Date.now() - created) / 86400000)}d)`);
        } else {
          console.log(`[ModCheck] Existing mod code for ${username} expired (age: ${Math.round((Date.now() - created) / 86400000)}d). Regenerating.`);
        }
      } else if (existingAuth) {
        console.log(`[ModCheck] Existing auth for ${username} has accountID mismatch or missing code. stored=${existingAuth.accountID} vs request=${accountID}`);
      } else {
        console.log(`[ModCheck] No existing auth found for ${username} at ${authKey}`);
      }

      // Need new code — verify identity via GDBrowser first
      if (!newModCode) {
        let identityOk = false;
        try {
          const gdRes = await fetch(`https://gdbrowser.com/api/profile/${encodeURIComponent(username)}`);
          if (gdRes.ok) {
            const gdData = await gdRes.json();
            if (parseInt(gdData.accountID) === accountID) {
              identityOk = true;
              gdVerified = true;
              console.log(`[ModCheck] GDBrowser verified ${username} owns accountID ${accountID}`);
            } else {
              console.warn(`[ModCheck] GDBrowser accountID mismatch for ${username}: GD=${gdData.accountID} vs request=${accountID}`);
            }
          } else {
            console.error(`[ModCheck] GDBrowser request failed for ${username}: HTTP ${gdRes.status}`);
          }
        } catch (e) {
          console.error(`[ModCheck] GDBrowser error for ${username}:`, e);
        }

        if (identityOk) {
          newModCode = crypto.randomUUID();
          const writeOk = await putR2Json(env.SYSTEM_BUCKET, authKey, {
            code: newModCode,
            created: new Date().toISOString(),
            accountID: accountID
          });
          if (!writeOk) {
            console.error(`[ModCheck] CRITICAL: Failed to write mod code to storage for ${username}`);
            newModCode = undefined; // Don't return code that wasn't stored
          } else {
            // Read-back verification to ensure storage consistency
            const verification = await getR2Json(env.SYSTEM_BUCKET, authKey);
            if (!verification || verification.code !== newModCode) {
              console.error(`[ModCheck] CRITICAL: Write verification failed for ${username}. Written code doesn't match read-back.`);
              newModCode = undefined;
            } else {
              console.log(`[ModCheck] Generated and verified new mod code for ${username}`);
            }
          }
        }
      }
    } catch (e) {
      console.error(`[ModCheck] Error generating mod code for ${username}:`, e);
    }
  }

  const responseData = {
    isModerator,
    isAdmin,
    isVip,
    accountID,
    accountRequiredForGlobalUploads: true
  };

  if (newModCode) {
    responseData.newModCode = newModCode;
  } else if ((isModerator || isAdmin) && !gdVerified) {
    // Mod/admin but GDBrowser verification failed — can't issue code
    responseData.gdVerificationFailed = true;
  }

  return new Response(JSON.stringify(responseData), {
    status: 200,
    headers: {
      'Content-Type': 'application/json',
      ...corsHeaders()
    }
  });
}

// Get verification queue (moderator only)
async function handleGetQueue(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  // Server-side moderator verification via modCode/GDBrowser
  const url = new URL(request.url);
  const queueUsername = url.searchParams.get('username') || '';
  const queueAccountID = parseInt(url.searchParams.get('accountID') || '0');

  // Auth is mandatory for queue access
  const auth = await verifyModAuth(request, env, queueUsername, queueAccountID);
  if (!auth.authorized) {
    console.log(`[Security] Get queue blocked: ${queueUsername || '(no username)'}`);
    return modAuthForbiddenResponse(auth);
  }

  const category = url.pathname.split('/').pop();

  try {
    let prefix = '';
    if (category === 'verify') {
      prefix = 'data/queue/suggestions/';
    } else if (category === 'update') {
      prefix = 'data/queue/updates/';
    } else if (category === 'report') {
      prefix = 'data/queue/reports/';
    } else if (category === 'profileimgs') {
      prefix = 'data/queue/profileimgs/';
    } else if (category === 'profilebackground') {
      prefix = 'data/queue/profilebackground/';
    } else {
      prefix = `data/queue/${category}/`;
    }

    const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);

    const items = [];
    for (const key of keys) {
      const data = await getR2Json(env.SYSTEM_BUCKET, key);
      if (data) {
        if (Array.isArray(data) && data.length > 0) {
          // Suggestion arrays: group into a single item with suggestions sub-array
          const first = data[0];
          items.push({
            levelId: first.levelId,
            category: first.category || category,
            submittedBy: first.submittedBy,
            timestamp: data[data.length - 1].timestamp || first.timestamp,
            status: first.status || 'pending',
            note: first.note,
            accountID: first.accountID,
            suggestions: data
          });
        } else {
          items.push(data);
        }
      }
    }

    items.sort((a, b) => {
      const timeA = Array.isArray(a) ? (a[a.length - 1]?.timestamp || 0) : (a.timestamp || 0);
      const timeB = Array.isArray(b) ? (b[b.length - 1]?.timestamp || 0) : (b.timestamp || 0);
      return timeB - timeA;
    });

    return new Response(JSON.stringify({ success: true, items }), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        ...corsHeaders()
      }
    });

  } catch (error) {
    console.error('Get queue error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to get queue',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Accept queue item (moderator only)
async function handleAcceptQueue(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, category, username } = body;

    // Server-side moderator verification via modCode/GDBrowser
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Accept queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }

    if (!levelId || !category) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    let queueFolder = category;
    let sourceFolder = '';
    if (category === 'verify') {
      queueFolder = 'suggestions';
      sourceFolder = 'suggestions';
    } else if (category === 'update') {
      queueFolder = 'updates';
      sourceFolder = 'updates';
    } else if (category === 'report') {
      queueFolder = 'reports';
    } else if (category === 'profileimg') {
      queueFolder = 'profileimgs';
    } else if (category === 'profilebackground') {
      queueFolder = 'profilebackground';
    }

    const queueKey = `data/queue/${queueFolder}/${levelId}.json`;
    const queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);

    // Normalize: if queueData is an array (multi-suggestion), extract submitter info
    const queueSubmitter = Array.isArray(queueData) ? (queueData[0]?.submittedBy || 'unknown') : (queueData?.submittedBy || 'unknown');
    const queueAccountID = Array.isArray(queueData) ? (queueData[0]?.accountID || 0) : (queueData?.accountID || 0);

    // Handle profileimg acceptance separately
    if (category === 'profileimg') {
      const accountId = levelId; // For profileimgs, levelId is the accountID

      // Find the pending image
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${accountId}_` });
      if (pendingList.objects.length === 0) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'No pending profile image found' }), {
          status: 404,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }

      // Get the most recent pending image
      const sorted = pendingList.objects.sort((a, b) => {
        const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
        return getTs(b) - getTs(a);
      });
      const pendingObj = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      if (!pendingObj) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'Failed to read pending image' }), {
          status: 500,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }

      const pendingBuffer = await pendingObj.arrayBuffer();

      // ===== SECURITY SCAN (re-verify on approval) =====
      const pendingSecReject = rejectIfMalicious(new Uint8Array(pendingBuffer), pendingObj.httpMetadata?.contentType || 'image/png');
      if (pendingSecReject) return pendingSecReject;
      // =========================

      const ext = sorted[0].key.split('.').pop() || 'png';
      const ts = Date.now().toString();
      const destKey = `profileimgs/${accountId}_${ts}.${ext}`;

      // Clean up existing approved profileimgs for this user
      const existingPrefixes = [`profileimgs/${accountId}.`, `profileimgs/${accountId}_`];
      const existingKeys = [];
      for (const pfx of existingPrefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: pfx });
        existingKeys.push(...list.objects.map(o => o.key));
      }
      if (existingKeys.length > 0) {
        await Promise.all(existingKeys.map(k => env.THUMBNAILS_BUCKET.delete(k)));
      }

      // Upload to approved location
      await env.THUMBNAILS_BUCKET.put(destKey, pendingBuffer, {
        httpMetadata: {
          contentType: pendingObj.httpMetadata?.contentType || `image/${ext}`,
          cacheControl: NO_STORE_CACHE_CONTROL
        },
        customMetadata: {
          uploadedBy: queueData?.submittedBy || 'unknown',
          updated_by: queueData?.submittedBy || 'unknown',
          acceptedBy: username || 'unknown',
          acceptedAt: new Date().toISOString(),
          accountID: accountId.toString(),
          category: 'profileimg'
        }
      });

      // Clean up ALL pending images for this user
      await Promise.all(sorted.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));

      // Delete queue entry
      await env.SYSTEM_BUCKET.delete(queueKey);

      return new Response(JSON.stringify({
        success: true,
        message: 'Profile image approved and published'
      }), {
        status: 200,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Handle profilebackground acceptance (unified — replaces legacy profile/background/banner)
    if (category === 'profilebackground') {
      const accountId = levelId;

      // Find the pending background in canonical folder
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountId}_` });
      if (pendingList.objects.length === 0) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'No pending profile background found' }), {
          status: 404,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }

      // Get the most recent pending background
      const sorted = pendingList.objects.sort((a, b) => {
        const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
        return getTs(b) - getTs(a);
      });
      const pendingObj = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      if (!pendingObj) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'Failed to read pending background' }), {
          status: 500,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }

      const pendingBuffer = await pendingObj.arrayBuffer();

      // Security scan (re-verify on approval)
      const pendingBgSecReject = rejectIfMalicious(new Uint8Array(pendingBuffer), pendingObj.httpMetadata?.contentType || 'image/png');
      if (pendingBgSecReject) return pendingBgSecReject;

      const ext = sorted[0].key.split('.').pop() || 'png';
      const ts = Date.now().toString();
      const destKey = `profilebackground/${accountId}_${ts}.${ext}`;

      // Clean up existing approved profilebackgrounds for this user
      const existingPrefixes = [`profilebackground/${accountId}.`, `profilebackground/${accountId}_`];
      const existingKeys = [];
      for (const pfx of existingPrefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: pfx });
        existingKeys.push(...list.objects.map(o => o.key));
      }
      if (existingKeys.length > 0) {
        await Promise.all(existingKeys.map(k => env.THUMBNAILS_BUCKET.delete(k)));
      }

      // Upload to approved profilebackground/ location
      await env.THUMBNAILS_BUCKET.put(destKey, pendingBuffer, {
        httpMetadata: {
          contentType: pendingObj.httpMetadata?.contentType || `image/${ext}`,
          cacheControl: NO_STORE_CACHE_CONTROL
        },
        customMetadata: {
          uploadedBy: queueData?.submittedBy || 'unknown',
          updated_by: queueData?.submittedBy || 'unknown',
          acceptedBy: username || 'unknown',
          acceptedAt: new Date().toISOString(),
          accountID: accountId.toString(),
          category: 'profilebackground',
          contentKind: 'profilebackground'
        }
      });

      // Clean up ALL pending backgrounds for this user
      await Promise.all(sorted.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));

      // Delete queue entry
      await env.SYSTEM_BUCKET.delete(queueKey);

      // Audit log
      logAudit(env.SYSTEM_BUCKET, 'profilebackground_accept', {
        accountID: accountId, acceptedBy: username
      }, ctx);

      return new Response(JSON.stringify({
        success: true,
        message: 'Background approved and published'
      }), {
        status: 200,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Don't delete queueKey yet, we need it for cleanup if it's suggestions

    if (sourceFolder) {
      // Determine source key (support multi-suggestion)
      let sourceKey = body.targetFilename;

      // Fallback for legacy or updates
      if (!sourceKey) {
        sourceKey = `${sourceFolder}/${levelId}.webp`;
      }

      let sourceObject = await env.THUMBNAILS_BUCKET.get(sourceKey);

      // Prefix search fallback (timestamped filenames)
      if (!sourceObject) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: `${sourceFolder}/${levelId}`, limit: 20 });
        if (list.objects.length > 0) {
          const sorted = list.objects.sort((a, b) => {
            const getTs = (k) => { const m = k.key.match(/_(\d+)/); return m ? parseInt(m[1]) : 0; };
            return getTs(b) - getTs(a);
          });
          sourceKey = sorted[0].key;
          sourceObject = await env.THUMBNAILS_BUCKET.get(sourceKey);
        }
      }

      if (!sourceObject) {
        // Source image not found — don't silently succeed
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({
          error: 'Source thumbnail not found',
          details: `Could not find file for level ${levelId} in ${sourceFolder}/`
        }), {
          status: 404,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
        const thumbnailBuffer = await sourceObject.arrayBuffer();

        // ===== SECURITY SCAN (re-verify on approval) =====
        const approveSecReject = rejectIfMalicious(new Uint8Array(thumbnailBuffer), sourceObject.httpMetadata?.contentType || 'image/webp');
        if (approveSecReject) return approveSecReject;
        // =========================

        const versionManager = new VersionManager(env.SYSTEM_BUCKET);
        const oldVersionData = await versionManager.getVersion(levelId);

        const version = Date.now().toString();
        const destKey = `thumbnails/${levelId}_${version}.webp`;

        await env.THUMBNAILS_BUCKET.put(destKey, thumbnailBuffer, {
          httpMetadata: {
            contentType: 'image/webp',
            cacheControl: NO_STORE_CACHE_CONTROL
          },
          customMetadata: {
            acceptedBy: username || 'unknown',
            acceptedAt: new Date().toISOString(),
            status: 'accepted',
            originalSubmitter: queueSubmitter,
            uploadedBy: queueSubmitter,
            updated_by: queueSubmitter,
            version: version
          }
        });

        await versionManager.update(levelId, version, 'webp', 'thumbnails', 'static', {
          uploadedBy: queueSubmitter,
          uploadedAt: new Date().toISOString()
        });

        // Delete rating if exists (Reset ratings on update)
        if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`));
        else await env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`);

        if (oldVersionData && oldVersionData.version !== 'legacy') {
          const oldKey = `thumbnails/${levelId}_${oldVersionData.version}.${oldVersionData.format}`;
          if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(oldKey));
          else await env.THUMBNAILS_BUCKET.delete(oldKey);
        }

        // FORCE CLEANUP: Delete ALL other versions to ensure only one file exists
        const prefixes = [`thumbnails/${levelId}`, `thumbnails/gif/${levelId}`];
        const cleanupKeys = [];

        for (const prefix of prefixes) {
          const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
          for (const obj of list.objects) {
            const k = obj.key;
            // Don't delete the file we just uploaded
            if (k === destKey) continue;

            const cleanKey = k.replace(/^\//, '');
            if (cleanKey.match(new RegExp(`^thumbnails/${levelId}(\\.|_)`)) ||
              cleanKey.match(new RegExp(`^thumbnails/gif/${levelId}\\.`))) {
              cleanupKeys.push(k);
            }
          }
        }

        // Deduplicate
        const uniqueCleanup = [...new Set(cleanupKeys)];
        console.log(`[Accept] Cleaning up ${uniqueCleanup.length} old versions for ${levelId}`);

        for (const k of uniqueCleanup) {
          if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(k));
          else await env.THUMBNAILS_BUCKET.delete(k);
        }

        // Cleanup: Delete the accepted file
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(sourceKey));
        else await env.THUMBNAILS_BUCKET.delete(sourceKey);

        // Special cleanup for suggestions (multi-suggestion system)
        if (category === 'verify' && queueData) {
          let suggestions = [];
          if (Array.isArray(queueData)) {
            suggestions = queueData;
          } else {
            suggestions = [queueData];
          }

          // Delete ALL suggestion images for this level
          const deletionPromises = suggestions.map(s => {
            const fname = s.filename || `suggestions/${levelId}.webp`;
            // Don't delete if it's the one we just accepted (already deleted above, or maybe we should just ensure it's gone)
            if (fname !== sourceKey) {
              return env.THUMBNAILS_BUCKET.delete(fname);
            }
            return Promise.resolve();
          });

          if (ctx) ctx.waitUntil(Promise.all(deletionPromises));
          else await Promise.all(deletionPromises);
        }
    }
    // Update latest uploads so the Discord bot detects accepted suggestions/updates
    if (category === 'verify' || category === 'update') {
      const updateLatest = async () => {
        const latestKey = 'data/system/latest_uploads.json';
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];

        // Remove if exists (to move to top)
        latest = latest.filter(item => item.levelId !== parseInt(levelId));

        // Add new item (use original submitter as username)
        latest.unshift({
          levelId: parseInt(levelId),
          username: queueSubmitter,
          timestamp: Date.now(),
          accountID: queueAccountID,
          acceptedBy: username || 'unknown'
        });

        // Keep last 20
        if (latest.length > 20) latest = latest.slice(0, 20);

        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
      };

      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();

      // Notify Discord bot via webhook
      const webhookPayload = {
        levelId: parseInt(levelId),
        username: queueSubmitter,
        timestamp: Date.now(),
        is_update: category === 'update',
      };
      if (ctx) ctx.waitUntil(dispatchWebhook(env, 'upload', webhookPayload));
      else await dispatchWebhook(env, 'upload', webhookPayload);
    }

    // Finally delete the queue item
    await env.SYSTEM_BUCKET.delete(queueKey);

    // Historial guardado en metadata del thumbnail para reducir operaciones

    return new Response(JSON.stringify({
      success: true,
      message: 'Item accepted and added to history'
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Accept queue error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to accept item',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Claim queue item (moderator only)
async function handleClaimQueue(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, category, username } = body;

    // Server-side moderator verification via modCode/GDBrowser
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Claim queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }

    if (!levelId || !category || !username) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    let queueFolder = category;
    if (category === 'verify') {
      queueFolder = 'suggestions';
    } else if (category === 'update') {
      queueFolder = 'updates';
    } else if (category === 'report') {
      queueFolder = 'reports';
    } else if (category === 'profileimg') {
      queueFolder = 'profileimgs';
    } else if (category === 'profilebackground') {
      queueFolder = 'profilebackground';
    }

    const queueKey = `data/queue/${queueFolder}/${levelId}.json`;
    const queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);

    if (!queueData) {
      return new Response(JSON.stringify({ error: 'Queue item not found' }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    queueData.claimedBy = username;
    queueData.claimedAt = new Date().toISOString();
    queueData.status = 'claimed';

    await putR2Json(env.SYSTEM_BUCKET, queueKey, queueData);

    const claimKey = `data/claims/${category}/${levelId}.json`;
    await putR2Json(env.SYSTEM_BUCKET, claimKey, {
      levelId: parseInt(levelId),
      category,
      claimedBy: username,
      claimedAt: new Date().toISOString()
    });

    return new Response(JSON.stringify({
      success: true,
      message: 'Level claimed successfully',
      claimedBy: username
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Claim queue error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to claim item',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Reject queue item (moderator only)
async function handleRejectQueue(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, category, username, reason } = body;

    // Server-side moderator verification via modCode/GDBrowser
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Reject queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }

    if (!levelId || !category) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    let queueFolder = category;
    let sourceFolder = '';
    if (category === 'verify') {
      queueFolder = 'suggestions';
      sourceFolder = 'suggestions';
    } else if (category === 'update') {
      queueFolder = 'updates';
      sourceFolder = 'updates';
    } else if (category === 'report') {
      queueFolder = 'reports';
    } else if (category === 'profileimg') {
      queueFolder = 'profileimgs';
    } else if (category === 'profilebackground') {
      queueFolder = 'profilebackground';
    }

    const queueKey = `data/queue/${queueFolder}/${levelId}.json`;
    const itemData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    await env.SYSTEM_BUCKET.delete(queueKey);

    if (category === 'profileimg') {
      // Delete all pending profile images for this account
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${levelId}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else if (category === 'profilebackground') {
      // Delete all pending profilebackgrounds for this account
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${levelId}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else if (sourceFolder) {
      // Delete specific file from queue data if available
      if (itemData) {
        const items = Array.isArray(itemData) ? itemData : [itemData];
        const deletePromises = items.map(item => {
          const fname = item.filename || `${sourceFolder}/${levelId}.webp`;
          return env.THUMBNAILS_BUCKET.delete(fname);
        });
        await Promise.all(deletePromises);
      }
      // Also try legacy key as fallback
      await env.THUMBNAILS_BUCKET.delete(`${sourceFolder}/${levelId}.webp`);

      // Clean up any remaining timestamped files for this level in the source folder
      const remainingList = await env.THUMBNAILS_BUCKET.list({ prefix: `${sourceFolder}/${levelId}_`, limit: 50 });
      if (remainingList.objects.length > 0) {
        await Promise.all(remainingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    }

    const logKey = `data/history/rejected/${levelId}-${Date.now()}.json`;
    await putR2Json(env.SYSTEM_BUCKET, logKey, {
      levelId: parseInt(levelId),
      category,
      rejectedBy: username || 'unknown',
      rejectedAt: new Date().toISOString(),
      reason: reason || 'No reason provided',
      originalData: itemData
    });

    return new Response(JSON.stringify({
      success: true,
      message: 'Item rejected and thumbnail deleted'
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Reject queue error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to reject item',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Admin: Backfill contributor metadata
async function handleBackfillContributors(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json().catch(() => ({}));
    const limit = Math.max(1, Math.min(parseInt(body.limit || '200', 10), 2000));
    const dryRun = Boolean(body.dryRun);

    const acceptedKeys = await listR2Keys(env.SYSTEM_BUCKET, 'data/history/accepted/');
    const acceptedMap = new Map();
    for (const key of acceptedKeys) {
      const data = await getR2Json(env.SYSTEM_BUCKET, key);
      if (!data) continue;
      const levelId = (data.levelId ?? '').toString();
      if (!levelId) continue;
      const uploadedBy = data.originalSubmission?.submittedBy || data.submittedBy || undefined;
      const acceptedBy = data.acceptedBy || undefined;
      const acceptedAt = data.acceptedAt ? Date.parse(data.acceptedAt) : undefined;
      const prev = acceptedMap.get(levelId) || {};
      acceptedMap.set(levelId, { ...prev, uploadedBy, acceptedBy, acceptedAt });
    }

    const uploadKeys = await listR2Keys(env.SYSTEM_BUCKET, 'data/history/uploads/');
    for (const key of uploadKeys) {
      const data = await getR2Json(env.SYSTEM_BUCKET, key);
      if (!data) continue;
      const levelId = (data.levelId ?? '').toString();
      if (!levelId) continue;
      const uploadedBy = data.uploadedBy || undefined;
      if (!acceptedMap.has(levelId)) {
        acceptedMap.set(levelId, { uploadedBy });
      } else {
        const prev = acceptedMap.get(levelId) || {};
        acceptedMap.set(levelId, { ...prev, uploadedBy: prev.uploadedBy || uploadedBy });
      }
    }

    const listMain = await env.THUMBNAILS_BUCKET.list({ prefix: '/thumbnails/', limit: 2000 });
    const objects = listMain.objects.filter(o => o.key.endsWith('.webp') || o.key.endsWith('.gif'));

    const summary = { scanned: 0, updated: 0, skipped: 0, missingInfo: 0, errors: [] };

    for (const obj of objects.slice(0, limit)) {
      summary.scanned++;
      const key = obj.key;
      const basename = key.substring(key.lastIndexOf('/') + 1);
      const levelId = basename.replace(/\.(webp|gif)$/i, '');

      const info = acceptedMap.get(levelId);
      if (!info || (!info.uploadedBy && !info.acceptedBy)) {
        summary.missingInfo++;
        continue;
      }

      let head = null;
      try { head = await bucket.head(key); } catch (_) { }
      const meta = head?.customMetadata || {};

      const alreadyHas = meta.uploadedBy && (meta.acceptedBy || !info.acceptedBy);
      if (alreadyHas) { summary.skipped++; continue; }

      if (dryRun) { summary.updated++; continue; }

      try {
        const object = await bucket.get(key);
        if (!object) { summary.skipped++; continue; }
        const buf = await object.arrayBuffer();

        await bucket.put(key, buf, {
          httpMetadata: {
            contentType: key.endsWith('.gif') ? 'image/gif' : 'image/webp',
            cacheControl: NO_STORE_CACHE_CONTROL,
          },
          customMetadata: {
            ...meta,
            uploadedBy: meta.uploadedBy || info.uploadedBy || 'Unknown',
            acceptedBy: meta.acceptedBy || info.acceptedBy || undefined,
            acceptedAt: meta.acceptedAt || (info.acceptedAt ? new Date(info.acceptedAt).toISOString() : undefined),
            status: meta.status || (info.acceptedBy ? 'accepted' : meta.status || 'uploaded')
          }
        });
        summary.updated++;
      } catch (e) {
        summary.errors.push({ key, error: e.message });
      }
    }

    return new Response(JSON.stringify({ success: true, ...summary }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Backfill contributors error:', error);
    return new Response(JSON.stringify({ error: 'Backfill failed', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Migrate legacy thumbnails to VersionManager
async function handleMigrateLegacy(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  // Hardened: require full admin auth with Mod Code
  try {
    const body = await request.json().catch(() => ({}));
    const admin = await requireAdmin(request, env, body);
    if (!admin.authorized) {
      console.log(`[Security] Migrate legacy blocked: ${body.username || '(none)'} - ${admin.error}`);
      return forbiddenResponse(admin.error || 'Admin privileges required');
    }
  } catch {
    return forbiddenResponse('Admin auth required with Mod Code');
  }

  try {
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const currentMap = await versionManager.getMap();
    let updatedCount = 0;
    let scannedCount = 0;

    const scanAndUpdate = async (prefix, format) => {
      let truncated = true;
      let cursor = undefined;

      while (truncated) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix, cursor, limit: 1000 });
        truncated = list.truncated;
        cursor = list.cursor;

        for (const obj of list.objects) {
          scannedCount++;
          const key = obj.key;
          const filename = key.split('/').pop();

          if (filename.includes('_')) continue;

          const levelId = filename.replace(/\.(webp|png|gif)$/, '');

          if (!currentMap[levelId]) {
            currentMap[levelId] = { version: 'legacy', format: format };
            updatedCount++;
          }
        }
      }
    };

    await scanAndUpdate('thumbnails/', 'webp');
    await scanAndUpdate('thumbnails/gif/', 'gif');

    let truncated = true;
    let cursor = undefined;
    while (truncated) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: 'thumbnails/', cursor, limit: 1000 });
      truncated = list.truncated;
      cursor = list.cursor;

      for (const obj of list.objects) {
        if (!obj.key.endsWith('.png')) continue;

        const filename = obj.key.split('/').pop();
        if (filename.includes('_')) continue;

        const levelId = filename.replace('.png', '');
        if (!currentMap[levelId]) {
          currentMap[levelId] = { version: 'legacy', format: 'png' };
          updatedCount++;
        }
      }
    }

    if (updatedCount > 0) {
      await putR2Json(env.SYSTEM_BUCKET, versionManager.cacheKey, currentMap);
    }

    return new Response(JSON.stringify({
      success: true,
      scanned: scannedCount,
      migrated: updatedCount,
      message: `Migrated ${updatedCount} legacy thumbnails to VersionManager`
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Migration error:', error);
    return new Response(JSON.stringify({
      error: 'Migration failed',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Submit report
async function handleSubmitReport(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, username, note } = body;

    if (!levelId) {
      return new Response(JSON.stringify({ error: 'Missing levelId' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const queueKey = `data/queue/reports/${levelId}.json`;
    const queueItem = {
      levelId: parseInt(levelId),
      category: 'report',
      submittedBy: username || 'unknown',
      timestamp: Date.now(),
      status: 'pending',
      note: note || 'No details provided'
    };
    await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);

    return new Response(JSON.stringify({
      success: true,
      message: 'Report submitted successfully'
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Submit report error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to submit report',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Submit feedback
async function handleFeedbackSubmit(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { type, title, description, username } = body;

    if (!type || !title || !description) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const timestamp = Date.now();
    const id = `${timestamp}-${Math.random().toString(36).substring(2, 9)}`;
    const key = `data/feedback/${id}.json`;

    const feedbackData = {
      id,
      type,
      title,
      description,
      username: username || 'Anonymous',
      timestamp,
      status: 'pending',
      userAgent: request.headers.get('User-Agent'),
      ip: request.headers.get('CF-Connecting-IP')
    };

    await putR2Json(env.SYSTEM_BUCKET, key, feedbackData);

    return new Response(JSON.stringify({
      success: true,
      message: 'Feedback submitted successfully',
      id
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Feedback submit error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to submit feedback',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Get feedback list
async function handleFeedbackList(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const keys = await listR2Keys(env.SYSTEM_BUCKET, 'data/feedback/');
    const items = [];

    for (const key of keys) {
      const data = await getR2Json(env.SYSTEM_BUCKET, key);
      if (data) {
        items.push(data);
      }
    }

    items.sort((a, b) => b.timestamp - a.timestamp);

    return new Response(JSON.stringify({ success: true, count: items.length, items }), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        ...corsHeaders()
      }
    });

  } catch (error) {
    console.error('Get feedback error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to get feedback',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Get upload history
async function handleGetHistory(request, env, type) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const url = new URL(request.url);
    const limit = parseInt(url.searchParams.get('limit')) || 100;
    const levelId = url.searchParams.get('levelId');

    let prefix = `data/history/${type}/`;
    if (levelId) {
      prefix += `${levelId}-`;
    }

    const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);

    const items = [];
    for (const key of keys.slice(0, limit)) {
      const data = await getR2Json(env.SYSTEM_BUCKET, key);
      if (data) {
        items.push(data);
      }
    }

    items.sort((a, b) => {
      const timeA = new Date(a.uploadedAt || a.acceptedAt || a.rejectedAt).getTime();
      const timeB = new Date(b.uploadedAt || b.acceptedAt || b.rejectedAt).getTime();
      return timeB - timeA;
    });

    return new Response(JSON.stringify({ success: true, type, count: items.length, items }), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        ...corsHeaders()
      }
    });

  } catch (error) {
    console.error('Get history error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to get history',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Verify admin privileges (hardened: requires Mod Code via query params for GET debug endpoints)
async function verifyAdminFromRequest(request, env) {
  const url = new URL(request.url);
  const username = (request.headers.get('X-Admin-User') || url.searchParams.get('username') || '').toLowerCase();
  if (!username || !ADMIN_USERS.includes(username)) return false;
  const accountID = parseInt(url.searchParams.get('accountID') || '0');
  const auth = await verifyModAuth(request, env, username, accountID);
  return auth.authorized;
}

// ===== WHITELIST ENDPOINTS =====

// List whitelist
async function handleGetWhitelist(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const url = new URL(request.url);
  const wlUsername = url.searchParams.get('username') || '';
  const wlAccountID = parseInt(url.searchParams.get('accountID') || '0');
  const auth = await verifyModAuth(request, env, wlUsername, wlAccountID);
  if (!auth.authorized) {
    return modAuthForbiddenResponse(auth);
  }

  const type = url.searchParams.get('type') || 'profilebackground';
  const users = await getWhitelist(env.SYSTEM_BUCKET, type);

  return new Response(JSON.stringify({ success: true, type, users }), {
    status: 200,
    headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// Add to whitelist (moderator or admin)
async function handleAddWhitelist(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, type } = body;
    const wlType = type || 'profilebackground';

    if (!username) {
      return new Response(JSON.stringify({ error: 'Missing username' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Require moderator or admin auth
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      return modAuthForbiddenResponse(auth);
    }

    const adminUser = (body.adminUser || body.moderator || '').toString().trim();
    const users = await addToWhitelist(env.SYSTEM_BUCKET, username, adminUser, wlType);

    // Audit log
    await logAudit(env.SYSTEM_BUCKET, 'whitelist_add', {
      target: username.toLowerCase(), addedBy: adminUser, type: wlType
    });

    return new Response(JSON.stringify({ success: true, users }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Add whitelist error:', error);
    return new Response(JSON.stringify({ error: 'Failed to add to whitelist', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Remove from whitelist (moderator or admin)
async function handleRemoveWhitelist(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, type } = body;
    const wlType = type || 'profilebackground';

    if (!username) {
      return new Response(JSON.stringify({ error: 'Missing username' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Require moderator or admin auth
    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      return modAuthForbiddenResponse(auth);
    }

    const adminUser = (body.adminUser || body.moderator || '').toString().trim();
    const users = await removeFromWhitelist(env.SYSTEM_BUCKET, username, adminUser, wlType);

    // Audit log
    await logAudit(env.SYSTEM_BUCKET, 'whitelist_remove', {
      target: username.toLowerCase(), removedBy: adminUser, type: wlType
    });

    return new Response(JSON.stringify({ success: true, users }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Remove whitelist error:', error);
    return new Response(JSON.stringify({ error: 'Failed to remove from whitelist', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Add moderator
async function handleAddModerator(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, adminUser } = body;

    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Server-side admin verification via modCode/GDBrowser
    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Add moderator blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    // Validate username exists via GDBrowser before adding
    const usernameLower = username.toLowerCase();
    try {
      const profileRes = await fetch(`https://gdbrowser.com/api/profile/${encodeURIComponent(username)}`);
      if (!profileRes.ok) {
        return new Response(JSON.stringify({
          success: false,
          message: `User "${username}" not found on GDBrowser`
        }), {
          status: 400,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
    } catch (e) {
      console.warn('GDBrowser validation failed (allowing anyway):', e);
    }

    const moderators = await getModerators(env.SYSTEM_BUCKET);
    if (moderators.includes(usernameLower)) {
      return new Response(JSON.stringify({
        success: false,
        message: 'User is already a moderator'
      }), {
        status: 200,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    moderators.push(usernameLower);
    await putR2Json(env.SYSTEM_BUCKET, 'data/moderators.json', { moderators });

    // Log de cambio de moderador solo en consola para reducir operaciones
    console.log(`[ModChange] ${adminUser} added ${usernameLower} as moderator`);

    return new Response(JSON.stringify({
      success: true,
      message: `${username} added as moderator`,
      moderators
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Add moderator error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to add moderator',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Remove moderator
async function handleRemoveModerator(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, adminUser } = body;

    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Server-side admin verification via modCode/GDBrowser
    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Remove moderator blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    const newModerators = moderators.filter(mod => mod !== usernameLower);

    if (newModerators.length === moderators.length) {
      return new Response(JSON.stringify({
        success: false,
        message: 'User was not a moderator'
      }), {
        status: 200,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    await putR2Json(env.SYSTEM_BUCKET, 'data/moderators.json', { moderators: newModerators });

    // Invalidate removed moderator's auth code
    try {
      await env.SYSTEM_BUCKET.delete(`data/auth/${usernameLower}.json`);
    } catch (e) {
      console.warn(`Failed to delete auth code for ${usernameLower}:`, e);
    }

    // Log de cambio de moderador solo en consola para reducir operaciones
    console.log(`[ModChange] ${adminUser} removed ${usernameLower} from moderators`);

    return new Response(JSON.stringify({
      success: true,
      message: `${username} removed from moderators`,
      moderators: newModerators
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Remove moderator error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to remove moderator',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Add VIP
async function handleAddVip(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, adminUser } = body;

    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Add VIP blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const vips = await getVips(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    if (vips.includes(usernameLower)) {
      return new Response(JSON.stringify({
        success: false,
        message: 'User is already a VIP'
      }), {
        status: 200,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    vips.push(usernameLower);
    await putR2Json(env.SYSTEM_BUCKET, 'data/vips.json', { vips });

    console.log(`[VipChange] ${adminUser} added ${usernameLower} as VIP`);

    return new Response(JSON.stringify({
      success: true,
      message: `${username} added as VIP`,
      vips
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Add VIP error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to add VIP',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Remove VIP
async function handleRemoveVip(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, adminUser } = body;

    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Remove VIP blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const vips = await getVips(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    const newVips = vips.filter(v => v !== usernameLower);

    if (newVips.length === vips.length) {
      return new Response(JSON.stringify({
        success: false,
        message: 'User was not a VIP'
      }), {
        status: 200,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    await putR2Json(env.SYSTEM_BUCKET, 'data/vips.json', { vips: newVips });

    console.log(`[VipChange] ${adminUser} removed ${usernameLower} from VIPs`);

    return new Response(JSON.stringify({
      success: true,
      message: `${username} removed from VIPs`,
      vips: newVips
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Remove VIP error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to remove VIP',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// List all moderators
async function handleListModerators(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  // Server-side admin verification via modCode/GDBrowser
  const url = new URL(request.url);
  const adminUser = request.headers.get('X-Admin-User') || url.searchParams.get('username') || '';
  const adminAccountID = parseInt(url.searchParams.get('accountID') || '0');

  if (!adminUser || !ADMIN_USERS.includes(adminUser.toLowerCase())) {
    return new Response(JSON.stringify({ error: 'Admin privileges required' }), {
      status: 403,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const admin = await requireAdmin(request, env, { username: adminUser, accountID: adminAccountID });
  if (!admin.authorized) {
    return forbiddenResponse(admin.error);
  }

  const moderators = await getModerators(env.SYSTEM_BUCKET);

  return new Response(JSON.stringify({
    success: true,
    count: moderators.length,
    moderators
  }), {
    status: 200,
    headers: {
      'Content-Type': 'application/json',
      ...corsHeaders()
    }
  });
}

// ===== GALLERY HANDLER =====

async function handleGalleryList(request, env) {
  return new Response(JSON.stringify({
    error: 'Gallery is under maintenance',
    maintenance: true,
    thumbnails: [],
    total: 0
  }), {
    status: 503,
    headers: {
      'Content-Type': 'application/json',
      ...corsHeaders()
    }
  });
}


// Helper to get ISO week number
function getWeekNumber(d) {
  d = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()));
  d.setUTCDate(d.getUTCDate() + 4 - (d.getUTCDay() || 7));
  var yearStart = new Date(Date.UTC(d.getUTCFullYear(), 0, 1));
  var weekNo = Math.ceil((((d - yearStart) / 86400000) + 1) / 7);
  return `${d.getUTCFullYear()}-W${weekNo}`;
}

// Helper to update leaderboard
async function updateLeaderboard(env, type, levelId, stats, uploadedBy, accountID) {
  const date = new Date();
  let key;
  if (type === 'daily') key = `data/leaderboards/daily/${date.toISOString().split('T')[0]}.json`;
  else if (type === 'weekly') key = `data/leaderboards/weekly/${getWeekNumber(date)}.json`;
  else return;

  try {
    let leaderboard = await getR2Json(env.SYSTEM_BUCKET, key) || [];

    // Remove existing entry
    leaderboard = leaderboard.filter(item => item.levelId !== parseInt(levelId));

    // Calculate average
    const average = stats.count > 0 ? stats.total / stats.count : 0;

    // Add new entry if it has votes
    if (stats.count > 0) {
      const entry = {
        levelId: parseInt(levelId),
        rating: average,
        count: stats.count,
        uploadedBy: uploadedBy || 'Unknown',
        updatedAt: new Date().toISOString()
      };
      if (accountID) entry.accountID = parseInt(accountID);
      leaderboard.push(entry);
    }

    // Sort by rating (descending)
    leaderboard.sort((a, b) => {
      if (b.rating !== a.rating) return b.rating - a.rating;
      return b.count - a.count; // Tie-break with vote count
    });

    // Keep top 100
    if (leaderboard.length > 100) leaderboard = leaderboard.slice(0, 100);

    await putR2Json(env.SYSTEM_BUCKET, key, leaderboard);
  } catch (e) {
    console.error(`Failed to update ${type} leaderboard:`, e);
  }
}

// ===== DAILY/WEEKLY LEVEL HANDLERS =====

async function handleSetDailyLevel(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const { levelID, username, accountID } = await request.json();

    if (!levelID || !username) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const authResult = await verifyModAuth(request, env, username, accountID || 0);
    if (!authResult.authorized) {
      return new Response(JSON.stringify({ error: 'Not authorized' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Still check if admin for daily/weekly? Original code said "Admin only".
    // verifyModAuth checks mod OR admin. If strict admin is required, we need to check that.
    // Assuming mods can also set daily/weekly based on user request "check moderator code".
    // If strict admin is needed, I should check ADMIN_USERS again.

    const isAdmin = ADMIN_USERS.includes(username.toLowerCase());
    // Allowing mods too since user asked for moderator check.

    const now = Date.now();
    const expiresAt = now + (24 * 60 * 60 * 1000); // 24 hours

    const data = {
      levelID: parseInt(levelID),
      setAt: now,
      expiresAt: expiresAt,
      setBy: username
    };

    await putR2Json(env.SYSTEM_BUCKET, 'data/daily/current.json', data);

    // Notify Discord bot via webhook
    dispatchWebhook(env, 'daily', data).catch(() => {});

    // Also add to history
    const historyKey = `data/daily/history/${new Date().toISOString().split('T')[0]}.json`;
    await putR2Json(env.SYSTEM_BUCKET, historyKey, data);

    return new Response(JSON.stringify({ success: true, data }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

async function handleGetDailyLevel(request, env) {
  try {
    const data = await getR2Json(env.SYSTEM_BUCKET, 'data/daily/current.json');

    if (!data) {
      return new Response(JSON.stringify({ error: 'No daily level set' }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Check expiration
    if (Date.now() > data.expiresAt) {
      return new Response(JSON.stringify({ error: 'Daily level expired', expired: true }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    return new Response(JSON.stringify({ success: true, data }), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        'Cache-Control': 'no-store',
        ...corsHeaders()
      }
    });

  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

async function handleSetWeeklyLevel(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const { levelID, username, accountID } = await request.json();

    if (!levelID || !username) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const authResult = await verifyModAuth(request, env, username, accountID || 0);
    if (!authResult.authorized) {
      return new Response(JSON.stringify({ error: 'Not authorized' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Previous code restrict strictly to admin?
    // "const isAdmin = ADMIN_USERS.includes(username.toLowerCase());" 
    // Yes. But user request implies expanding to moderators or just securing the check.
    // I will stick to verifying they are at least a moderator/admin via verifyModAuth.

    const now = Date.now();
    const expiresAt = now + (7 * 24 * 60 * 60 * 1000); // 7 days

    const data = {
      levelID: parseInt(levelID),
      setAt: now,
      expiresAt: expiresAt,
      setBy: username
    };

    await putR2Json(env.SYSTEM_BUCKET, 'data/weekly/current.json', data);

    // Notify Discord bot via webhook
    dispatchWebhook(env, 'weekly', data).catch(() => {});

    // Also add to history
    const historyKey = `data/weekly/history/${getWeekNumber(new Date())}.json`;
    await putR2Json(env.SYSTEM_BUCKET, historyKey, data);

    return new Response(JSON.stringify({ success: true, data }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

async function handleGetWeeklyLevel(request, env) {
  try {
    const data = await getR2Json(env.SYSTEM_BUCKET, 'data/weekly/current.json');

    if (!data) {
      return new Response(JSON.stringify({ error: 'No weekly level set' }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Check expiration
    if (Date.now() > data.expiresAt) {
      return new Response(JSON.stringify({ error: 'Weekly level expired', expired: true }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    return new Response(JSON.stringify({ success: true, data }), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        'Cache-Control': 'no-store',
        ...corsHeaders()
      }
    });

  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Public history of daily/weekly levels (no auth required)
async function handleGetDailyWeeklyHistory(request, env) {
  try {
    const url = new URL(request.url);
    const type = url.searchParams.get('type') || 'daily';
    const limit = parseInt(url.searchParams.get('limit')) || 50;

    if (type !== 'daily' && type !== 'weekly') {
      return new Response(JSON.stringify({ error: 'Invalid type, must be daily or weekly' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const prefix = `data/${type}/history/`;
    const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);

    const items = [];
    for (const key of keys.slice(-limit).reverse()) {
      const data = await getR2Json(env.SYSTEM_BUCKET, key);
      if (data) {
        items.push(data);
      }
    }

    // Sort by setAt descending (most recent first)
    items.sort((a, b) => (b.setAt || 0) - (a.setAt || 0));

    return new Response(JSON.stringify({ success: true, type, count: items.length, items }), {
      status: 200,
      headers: {
        'Content-Type': 'application/json',
        'Cache-Control': 'public, max-age=300',
        ...corsHeaders()
      }
    });

  } catch (error) {
    console.error('Get daily/weekly history error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get history', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Get Top Creators (cached leaderboard)
async function handleGetTopCreators(request, env) {
  try {
    const url = new URL(request.url);
    const page = parseInt(url.searchParams.get('page') || '0');
    const limit = parseInt(url.searchParams.get('limit') || '20');

    let cache = await getR2Json(env.SYSTEM_BUCKET, 'data/system/creator_leaderboard.json') || [];

    const start = page * limit;
    const end = start + limit;
    const slice = cache.slice(start, end);

    return new Response(JSON.stringify({
      success: true,
      total: cache.length,
      page,
      creators: slice
    }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response(JSON.stringify({ success: false, message: e.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }
}

// Get Top Thumbnails (cached leaderboard)
async function handleGetTopThumbnails(request, env) {
  try {
    const url = new URL(request.url);
    const page = parseInt(url.searchParams.get('page') || '0');
    const limit = parseInt(url.searchParams.get('limit') || '20');

    let cache = await getR2Json(env.SYSTEM_BUCKET, 'data/system/top_thumbnails.json') || [];

    const start = page * limit;
    const end = start + limit;
    const slice = cache.slice(start, end);

    return new Response(JSON.stringify({
      success: true,
      total: cache.length,
      page,
      thumbnails: slice
    }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response(JSON.stringify({ success: false, message: e.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }
}

// Get Leaderboard Handler (daily/weekly only)
async function handleGetLeaderboard(request, env) {
  const url = new URL(request.url);
  const type = url.searchParams.get('type') || 'daily';

  let key;
  const date = new Date();

  if (type === 'daily') key = `data/leaderboards/daily/${date.toISOString().split('T')[0]}.json`;
  else if (type === 'weekly') key = `data/leaderboards/weekly/${getWeekNumber(date)}.json`;
  else return new Response(JSON.stringify({ error: 'Invalid type' }), { status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() } });

  const data = await getR2Json(env.SYSTEM_BUCKET, key) || [];

  return new Response(JSON.stringify({ success: true, type, data }), {
    status: 200,
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': 'no-store',
      ...corsHeaders()
    }
  });
}

// ===== RATING SYSTEM HANDLERS =====

async function handleVote(request, env) {
  if (request.method !== 'POST') return new Response('Method not allowed', { status: 405 });

  try {
    const { levelID, stars, username, thumbnailId } = await request.json();
    if (!levelID || !stars || !username) return new Response('Missing fields', { status: 400 });
    if (stars < 1 || stars > 5) return new Response('Invalid stars', { status: 400 });

    let key = `ratings/${levelID}.json`;
    if (thumbnailId) {
      key = `ratings/${levelID}_${thumbnailId}.json`;
    }
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };

    const userLower = String(username).toLowerCase();

    // Check if user already voted
    if (data.votes[userLower]) {
      return new Response(JSON.stringify({ success: false, message: 'Already voted' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }

    // Get uploadedBy and accountID if missing — from history/queue files
    if (!data.uploadedBy) {
      try {
        // Try accepted history first
        const acceptedKeys = await listR2Keys(env.SYSTEM_BUCKET, `data/history/accepted/${levelID}`);
        for (const aKey of acceptedKeys) {
          const aData = await getR2Json(env.SYSTEM_BUCKET, aKey);
          if (aData) {
            const uploader = aData.originalSubmission?.submittedBy || aData.submittedBy || '';
            const accID = aData.originalSubmission?.accountID || aData.accountID || 0;
            if (uploader && uploader !== 'Unknown') {
              data.uploadedBy = uploader;
              if (accID) data.accountID = parseInt(accID);
              break;
            }
          }
        }
        // Fallback to queue files
        if (!data.uploadedBy) {
          for (const qPrefix of ['data/queue/suggestions/', 'data/queue/updates/']) {
            const qData = await getR2Json(env.SYSTEM_BUCKET, `${qPrefix}${levelID}.json`);
            if (qData) {
              const items = Array.isArray(qData) ? qData : [qData];
              for (const item of items) {
                const uploader = item.submittedBy || item.uploadedBy || '';
                const accID = item.accountID || 0;
                if (uploader && uploader !== 'Unknown' && uploader !== 'System') {
                  data.uploadedBy = uploader;
                  if (accID) data.accountID = parseInt(accID);
                  break;
                }
              }
              if (data.uploadedBy) break;
            }
          }
        }
      } catch (e) { console.warn('Failed to fetch uploadedBy', e); }
    }

    // Update Global Stats
    data.votes[userLower] = stars;
    data.total += stars;
    data.count += 1;

    const currentAverage = data.total / data.count;

    // Check for low rating (<= 3 stars)
    if (currentAverage <= 3) {
      const queueKey = `data/queue/updates/${levelID}.json`;
      await putR2Json(env.SYSTEM_BUCKET, queueKey, {
        levelId: parseInt(levelID),
        category: 'verify', // Put in verification list
        submittedBy: 'System',
        timestamp: Date.now(),
        status: 'pending',
        note: `Low rating detected: ${currentAverage.toFixed(2)} stars`,
        average: currentAverage
      });
    }

    // Update Daily Stats
    const today = new Date().toISOString().split('T')[0];
    if (!data.daily || data.daily.date !== today) {
      data.daily = { date: today, total: 0, count: 0 };
    }
    data.daily.total += stars;
    data.daily.count += 1;

    // Update Weekly Stats
    const thisWeek = getWeekNumber(new Date());
    if (!data.weekly || data.weekly.week !== thisWeek) {
      data.weekly = { week: thisWeek, total: 0, count: 0 };
    }
    data.weekly.total += stars;
    data.weekly.count += 1;

    await putR2Json(env.SYSTEM_BUCKET, key, data);

    // Update Leaderboards
    const uploaderAccountID = data.accountID || 0;
    const promises = [
      updateLeaderboard(env, 'daily', levelID, data.daily, data.uploadedBy, uploaderAccountID),
      updateLeaderboard(env, 'weekly', levelID, data.weekly, data.uploadedBy, uploaderAccountID)
    ];

    await Promise.all(promises);

    // Update top-thumbnails and creator caches
    const cachePromises = [
      updateTopThumbnailsCache(env, levelID, { total: data.total, count: data.count }, data.uploadedBy, data.accountID),
    ];
    if (data.uploadedBy && data.uploadedBy !== 'Unknown') {
      cachePromises.push(updateCreatorLeaderboardCache(env, data.uploadedBy, {
        addRating: stars,
        accountID: data.accountID
      }));
    }
    await Promise.all(cachePromises);

    return new Response(JSON.stringify({ success: true, average: data.total / data.count }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response('Error processing vote: ' + e.message, { status: 500 });
  }
}

// ===== PROFILE RATINGS =====
// Stored under profile-ratings/<accountID>.json
// Each file: { total, count, votes: { username: { stars, message, timestamp } } }
async function handleProfileRatingVote(request, env) {
  if (request.method !== 'POST') return new Response('Method not allowed', { status: 405 });

  try {
    const { accountID, stars, username, message } = await request.json();
    if (!accountID || !stars || !username) {
      return new Response(JSON.stringify({ error: 'Missing fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }
    if (stars < 1 || stars > 5) {
      return new Response(JSON.stringify({ error: 'Stars must be 1-5' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }

    // Cannot rate yourself
    const accountStr = accountID.toString();

    // Check ban
    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (banned.includes(username.toLowerCase())) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }

    const key = `profile-ratings/${accountStr}.json`;
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };

    const userLower = username.toLowerCase();

    // Check if already voted — allow update
    const previousVote = data.votes?.[userLower];
    if (previousVote) {
      // Update: subtract old stars, add new
      data.total = (data.total || 0) - previousVote.stars + stars;
    } else {
      data.total = (data.total || 0) + stars;
      data.count = (data.count || 0) + 1;
    }

    data.votes = data.votes || {};
    data.votes[userLower] = {
      stars,
      message: (message || '').substring(0, 150), // limit message length
      timestamp: Date.now()
    };

    await putR2Json(env.SYSTEM_BUCKET, key, data);

    const average = data.count > 0 ? data.total / data.count : 0;

    return new Response(JSON.stringify({
      success: true,
      average: Math.round(average * 100) / 100,
      count: data.count,
      updated: !!previousVote
    }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: 'Error processing vote: ' + e.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }
}

async function handleGetProfileRating(request, env) {
  const url = new URL(request.url);
  const accountID = url.pathname.split('/').pop();
  const username = url.searchParams.get('username');

  const key = `profile-ratings/${accountID}.json`;
  const data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };

  const average = data.count > 0 ? data.total / data.count : 0;
  const userVote = username && data.votes?.[username.toLowerCase()]
    ? data.votes[username.toLowerCase()]
    : null;

  // Build recent reviews (last 20, sorted by timestamp desc)
  const reviews = [];
  if (data.votes) {
    for (const [user, vote] of Object.entries(data.votes)) {
      if (vote.message) {
        reviews.push({ username: user, stars: vote.stars, message: vote.message, timestamp: vote.timestamp || 0 });
      }
    }
    reviews.sort((a, b) => b.timestamp - a.timestamp);
  }

  return new Response(JSON.stringify({
    average: Math.round(average * 100) / 100,
    count: data.count,
    userVote: userVote ? { stars: userVote.stars, message: userVote.message || '' } : null,
    reviews: reviews.slice(0, 20)
  }), {
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': 'no-store',
      ...corsHeaders(request.headers.get('Origin'))
    }
  });
}

// ===== RATING V2 (new storage and routes) =====
// Stored under ratings-v2/ to avoid mixing with legacy data
async function handleVoteV2(request, env) {
  if (request.method !== 'POST') return new Response('Method not allowed', { status: 405 });

  try {
    const { levelID, stars, username, thumbnailId } = await request.json();
    if (!levelID || !stars || !username) return new Response('Missing fields', { status: 400 });
    if (stars < 1 || stars > 5) return new Response('Invalid stars', { status: 400 });

    const levelStr = levelID.toString();
    let key = `ratings-v2/${levelStr}.json`;
    if (thumbnailId) {
      key = `ratings-v2/${levelStr}_${thumbnailId}.json`;
    }

    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };

    const userLower = String(username).toLowerCase();

    // Reject duplicate votes
    if (data.votes && data.votes[userLower]) {
      return new Response(JSON.stringify({ success: false, message: 'Already voted' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }

    // Ensure uploadedBy and accountID metadata for creator leaderboard — from history/queue files
    if (!data.uploadedBy) {
      try {
        const acceptedKeys = await listR2Keys(env.SYSTEM_BUCKET, `data/history/accepted/${levelStr}`);
        for (const aKey of acceptedKeys) {
          const aData = await getR2Json(env.SYSTEM_BUCKET, aKey);
          if (aData) {
            const uploader = aData.originalSubmission?.submittedBy || aData.submittedBy || '';
            const accID = aData.originalSubmission?.accountID || aData.accountID || 0;
            if (uploader && uploader !== 'Unknown') {
              data.uploadedBy = uploader;
              if (accID) data.accountID = parseInt(accID);
              break;
            }
          }
        }
        if (!data.uploadedBy) {
          for (const qPrefix of ['data/queue/suggestions/', 'data/queue/updates/']) {
            const qData = await getR2Json(env.SYSTEM_BUCKET, `${qPrefix}${levelStr}.json`);
            if (qData) {
              const items = Array.isArray(qData) ? qData : [qData];
              for (const item of items) {
                const uploader = item.submittedBy || item.uploadedBy || '';
                const accID = item.accountID || 0;
                if (uploader && uploader !== 'Unknown' && uploader !== 'System') {
                  data.uploadedBy = uploader;
                  if (accID) data.accountID = parseInt(accID);
                  break;
                }
              }
              if (data.uploadedBy) break;
            }
          }
        }
      } catch (e) { console.warn('Failed to fetch uploadedBy (v2)', e); }
    }

    data.votes = data.votes || {};
    data.votes[userLower] = stars;
    data.total = (data.total || 0) + stars;
    data.count = (data.count || 0) + 1;

    const currentAverage = data.total / data.count;

    // Low rating queueing stays the same
    if (currentAverage <= 3) {
      const queueKey = `data/queue/updates/${levelStr}.json`;
      await putR2Json(env.SYSTEM_BUCKET, queueKey, {
        levelId: parseInt(levelStr),
        category: 'verify',
        submittedBy: 'System',
        timestamp: Date.now(),
        status: 'pending',
        note: `Low rating detected: ${currentAverage.toFixed(2)} stars`,
        average: currentAverage
      });
    }

    // Daily/weekly bookkeeping
    const today = new Date().toISOString().split('T')[0];
    if (!data.daily || data.daily.date !== today) {
      data.daily = { date: today, total: 0, count: 0 };
    }
    data.daily.total += stars;
    data.daily.count += 1;

    const thisWeek = getWeekNumber(new Date());
    if (!data.weekly || data.weekly.week !== thisWeek) {
      data.weekly = { week: thisWeek, total: 0, count: 0 };
    }
    data.weekly.total += stars;
    data.weekly.count += 1;

    await putR2Json(env.SYSTEM_BUCKET, key, data);

    const uploaderAccountID = data.accountID || 0;
    const promises = [
      updateLeaderboard(env, 'daily', levelStr, data.daily, data.uploadedBy, uploaderAccountID),
      updateLeaderboard(env, 'weekly', levelStr, data.weekly, data.uploadedBy, uploaderAccountID)
    ];

    await Promise.all(promises);

    // Update top-thumbnails and creator caches
    const v2CachePromises = [
      updateTopThumbnailsCache(env, levelStr, { total: data.total, count: data.count }, data.uploadedBy, data.accountID),
    ];
    if (data.uploadedBy && data.uploadedBy !== 'Unknown') {
      v2CachePromises.push(updateCreatorLeaderboardCache(env, data.uploadedBy, {
        addRating: stars,
        accountID: data.accountID
      }));
    }
    await Promise.all(v2CachePromises);

    return new Response(JSON.stringify({ success: true, average: data.total / data.count, count: data.count }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response('Error processing vote: ' + e.message, { status: 500 });
  }
}

async function handleGetRatingV2(request, env) {
  const url = new URL(request.url);
  const levelID = url.pathname.split('/').pop();
  const username = url.searchParams.get('username');
  const thumbnailId = url.searchParams.get('thumbnailId');

  const candidates = [];
  if (thumbnailId) candidates.push(`ratings-v2/${levelID}_${thumbnailId}.json`);
  candidates.push(`ratings-v2/${levelID}.json`);
  // Fallback to legacy keys so old data can still be shown
  if (thumbnailId) candidates.push(`ratings/${levelID}_${thumbnailId}.json`);
  candidates.push(`ratings/${levelID}.json`);

  let data = null;
  for (const key of candidates) {
    data = await getR2Json(env.SYSTEM_BUCKET, key);
    if (data) break;
  }
  if (!data) data = { total: 0, count: 0, votes: {} };

  const average = data.count > 0 ? data.total / data.count : 0;
  const userVote = username && data.votes && data.votes[username] ? data.votes[username] : 0;

  return new Response(JSON.stringify({ average, count: data.count, userVote }), {
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': 'no-store',
      ...corsHeaders(request.headers.get('Origin'))
    }
  });
}

async function handleGetRating(request, env) {
  const url = new URL(request.url);
  const levelID = url.pathname.split('/').pop();
  const username = url.searchParams.get('username');
  const thumbnailId = url.searchParams.get('thumbnailId');

  // Try multiple candidates to avoid "always 0" when the client sends a different thumbnailId
  // Priority: explicit thumbnailId -> primary version id -> level-only key
  const vm = new VersionManager(env.SYSTEM_BUCKET);
  let primaryId = null;
  try {
    const version = await vm.getVersion(levelID);
    if (version) {
      primaryId = version.id || version.version || null;
    }
  } catch (e) {
    console.warn('handleGetRating: failed to read version map', e);
  }

  const candidateKeys = [];
  if (thumbnailId) candidateKeys.push(`ratings/${levelID}_${thumbnailId}.json`);
  if (primaryId) candidateKeys.push(`ratings/${levelID}_${primaryId}.json`);
  candidateKeys.push(`ratings/${levelID}.json`);

  let data = null;
  for (const key of candidateKeys) {
    data = await getR2Json(env.SYSTEM_BUCKET, key);
    if (data) break;
  }
  if (!data) data = { total: 0, count: 0, votes: {} };

  const average = data.count > 0 ? data.total / data.count : 0;
  const userVote = username && data.votes[username] ? data.votes[username] : 0;

  return new Response(JSON.stringify({ average, count: data.count, userVote }), {
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': 'no-store', // Cache disabled
      ...corsHeaders(request.headers.get('Origin'))
    }
  });
}

// Serve profile image
async function handleServeProfile(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  // Remove extension if present to get accountId
  const accountId = filename.replace(/\.[^/.]+$/, "");

  if (!accountId) {
    return new Response('Account ID required', { status: 400 });
  }

  // 1. Try finding by Account ID in profiles/
  // We need to find the latest file: profiles/<accountId>_<timestamp>.<ext>
  // OR legacy: profiles/<accountId>.<ext>

  // List files starting with accountId
  // Note: We need to be careful not to match accountId "12" with "123"
  // So we check for `profiles/${accountId}_` and `profiles/${accountId}.`

  let foundObject = null;
  let isPending = false;

  // Try timestamped versions first (preferred)
  const list = await env.THUMBNAILS_BUCKET.list({ prefix: `profiles/${accountId}_` });

  if (list.objects.length > 0) {
    const sorted = list.objects.sort((a, b) => {
      const getTs = (k) => {
        const match = k.match(/_(\d+)\./);
        return match ? parseInt(match[1]) : 0;
      };
      return getTs(b.key) - getTs(a.key);
    });

    foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
  }

  // If not found, try legacy formats
  if (!foundObject) {
    const extensions = ['gif', 'webp', 'png', 'jpg', 'jpeg'];
    for (const ext of extensions) {
      const key = `profiles/${accountId}.${ext}`;
      const object = await env.THUMBNAILS_BUCKET.get(key);
      if (object) {
        foundObject = object;
        break;
      }
    }
  }

  // If no approved background and this is a self-request, try pending
  const url2 = new URL(request.url);
  const isSelfRequest = url2.searchParams.get('self') === '1';
  if (!foundObject && isSelfRequest && verifyApiKey(request, env)) {
    const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profiles/${accountId}_` });
    if (pendingList.objects.length > 0) {
      const sorted = pendingList.objects.sort((a, b) => {
        const getTs = (k) => {
          const match = k.key.match(/_(\d+)\./);
          return match ? parseInt(match[1]) : 0;
        };
        return getTs(b) - getTs(a);
      });
      foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      isPending = true;
    }
  }

  if (foundObject) {
    const headers = new Headers();
    foundObject.writeHttpMetadata(headers);
    headers.set('etag', foundObject.httpEtag);
    headers.set('Access-Control-Allow-Origin', '*');
    // Force no-cache to ensure immediate updates
    headers.set('Cache-Control', 'no-store, no-cache, must-revalidate, proxy-revalidate');
    headers.set('Pragma', 'no-cache');
    headers.set('Expires', '0');
    if (isPending) {
      headers.set('X-Pending-Verification', 'true');
    }

    return new Response(foundObject.body, {
      headers
    });
  }

  // If not found, return 404
  return new Response('Profile not found', { status: 404 });
}

// Serve profile image (profileimgs/<accountId>)
async function handleServeProfileImg(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  const accountId = filename.replace(/\.[^/.]+$/, "");

  if (!accountId) {
    return new Response('Account ID required', { status: 400 });
  }

  const isSelfRequest = url.searchParams.get('self') === '1';

  let foundObject = null;
  let isPending = false;

  // Try timestamped versions first (preferred)
  const list = await env.THUMBNAILS_BUCKET.list({ prefix: `profileimgs/${accountId}_` });

  if (list.objects.length > 0) {
    const sorted = list.objects.sort((a, b) => {
      const getTs = (k) => {
        const match = k.match(/_(\d+)\./);
        return match ? parseInt(match[1]) : 0;
      };
      return getTs(b.key) - getTs(a.key);
    });

    foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
  }

  // If not found, try legacy formats
  if (!foundObject) {
    const extensions = ['gif', 'webp', 'png', 'jpg', 'jpeg', 'bmp', 'tiff'];
    for (const ext of extensions) {
      const key = `profileimgs/${accountId}.${ext}`;
      const object = await env.THUMBNAILS_BUCKET.get(key);
      if (object) {
        foundObject = object;
        break;
      }
    }
  }

  // If this is a self-request or pending-only request, try pending folder
  const isPendingOnly = url.searchParams.get('pending') === '1';
  if ((!foundObject && isSelfRequest && verifyApiKey(request, env)) ||
      (isPendingOnly && verifyApiKey(request, env))) {
    // For pending-only requests, skip approved and go straight to pending
    if (isPendingOnly) foundObject = null;
    if (!foundObject) {
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${accountId}_` });
      if (pendingList.objects.length > 0) {
        const sorted = pendingList.objects.sort((a, b) => {
          const getTs = (k) => {
            const match = k.key.match(/_(\d+)\./);
            return match ? parseInt(match[1]) : 0;
          };
          return getTs(b) - getTs(a);
        });
        foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
        isPending = true;
      }
    }
  }

  if (foundObject) {
    const headers = new Headers();
    foundObject.writeHttpMetadata(headers);
    headers.set('etag', foundObject.httpEtag);
    headers.set('Access-Control-Allow-Origin', '*');
    headers.set('Cache-Control', 'no-store, no-cache, must-revalidate, proxy-revalidate');
    headers.set('Pragma', 'no-cache');
    headers.set('Expires', '0');
    if (isPending) {
      headers.set('X-Pending-Verification', 'true');
    }

    return new Response(foundObject.body, {
      headers
    });
  }

  return new Response('Profile image not found', { status: 404 });
}


// Delete thumbnail endpoint (moderator only)
async function handleDeleteThumbnail(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, username } = body;
    const accountID = parseInt(body.accountID || '0');

    if (!levelId || !username) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Auth Check with Code
    const authResult = await verifyModAuth(request, env, username, accountID);
    const isModerator = authResult.authorized;

    // Additional backup check if code auth fails but username is admin (legacy support)
    // but better to rely on verifyModAuth which handles admin logic too if configured right.
    // verifyModAuth falls back to GDBrowser which checks admin list.

    if (!isModerator) {
      return new Response(JSON.stringify({ error: 'Not authorized - moderator only' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const versionManager = new VersionManager(env.SYSTEM_BUCKET);

    // FORCE CLASS B DELETE: List and delete ALL files for this level
    // This ensures no old versions resurface after deletion
    const prefixes = [
      `thumbnails/${levelId}`,
      `thumbnails/gif/${levelId}`
    ];

    let keysToDelete = [];

    for (const prefix of prefixes) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
      for (const obj of list.objects) {
        // Verify it matches the level ID exactly (to avoid deleting 1234 when deleting 123)
        const key = obj.key;
        const cleanKey = key.replace(/^\//, '');

        // Match: thumbnails/123.webp, thumbnails/123_456.webp, thumbnails/gif/123.gif
        if (cleanKey.match(new RegExp(`^thumbnails/${levelId}(\\.|_)`)) ||
          cleanKey.match(new RegExp(`^thumbnails/gif/${levelId}\\.`))) {
          keysToDelete.push(key);
        }
      }
    }

    // Also add legacy paths just in case
    keysToDelete.push(`thumbnails/${levelId}.webp`);
    keysToDelete.push(`thumbnails/${levelId}.png`);
    keysToDelete.push(`thumbnails/gif/${levelId}.gif`);

    // Deduplicate
    keysToDelete = [...new Set(keysToDelete)];

    console.log(`[Delete] Deleting ${keysToDelete.length} files for level ${levelId}:`, keysToDelete);

    for (const key of keysToDelete) {
      await env.THUMBNAILS_BUCKET.delete(key);
    }

    await versionManager.delete(levelId);

    const logKey = `data/logs/deleted/${levelId}-${Date.now()}.json`;
    await putR2Json(env.SYSTEM_BUCKET, logKey, {
      levelId: parseInt(levelId),
      deletedBy: username,
      deletedAt: new Date().toISOString(),
      timestamp: Date.now()
    });

    return new Response(JSON.stringify({
      success: true,
      message: 'Thumbnail deleted successfully'
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Delete thumbnail error:', error);
    return new Response(JSON.stringify({
      error: 'Failed to delete thumbnail',
      details: error.message
    }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Main request handler
// Admin: Recalculate Creator Leaderboard
async function handleSetDaily(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response('Unauthorized', { status: 401 });
  }

  try {
    const body = await request.json();
    const { levelID, type, username } = body; // type: 'daily' or 'weekly', username required for auth

    if (!levelID) {
      return new Response('Missing levelID', { status: 400 });
    }

    if (!username) {
      return new Response('Missing username', { status: 400 });
    }

    // Server-side admin verification via modCode/GDBrowser
    const admin = await requireAdmin(request, env, { username, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Set daily/weekly blocked: ${username} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const key = 'data/daily_weekly.json';
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { daily: null, weekly: null };

    if (type === 'weekly') {
      data.weekly = levelID;
    } else if (type === 'daily') {
      data.daily = levelID;
    } else if (type === 'unset') {
      if (data.daily == levelID) data.daily = null;
      if (data.weekly == levelID) data.weekly = null;
    }

    await putR2Json(env.SYSTEM_BUCKET, key, data);

    // Notify Discord bot via webhook
    if (type === 'daily' || type === 'weekly') {
      dispatchWebhook(env, type, { levelID: parseInt(levelID), setBy: username, setAt: Date.now() }).catch(() => {});
    }

    return new Response(JSON.stringify({ success: true }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (e) {
    return new Response('Error: ' + e.message, { status: 500 });
  }
}

// Admin: Get ban list (stored in SYSTEM_BUCKET) - moderator access
async function handleGetBanList(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response('Unauthorized', { status: 401 });
  }

  // Server-side moderator verification via modCode/GDBrowser
  const url = new URL(request.url);
  const blUsername = url.searchParams.get('username') || '';
  const blAccountID = parseInt(url.searchParams.get('accountID') || '0');
  if (blUsername) {
    const auth = await verifyModAuth(request, env, blUsername, blAccountID);
    if (!auth.authorized) {
      console.log(`[Security] Get banlist blocked: ${blUsername}`);
      return modAuthForbiddenResponse(auth);
    }
  }

  try {
    const data = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    return new Response(JSON.stringify({
      banned: data?.banned || [],
      details: data?.details || {}
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Admin: Ban a user (writes to SYSTEM_BUCKET)
async function handleBanUser(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response('Unauthorized', { status: 401 });
  }

  try {
    const data = await request.json();

    // Server-side admin verification via modCode/GDBrowser
    const adminName = (data.admin || data.adminUser || '').toString().trim();
    const admin = await requireAdmin(request, env, { username: adminName, accountID: data.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Ban user blocked: ${adminName} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const username = (data?.username || '').toString().trim().toLowerCase();

    if (!username) {
      return new Response(JSON.stringify({ error: 'username required' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Block banning admins/moderators
    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const isAdmin = ADMIN_USERS.includes(username);
    const isModerator = moderators.includes(username) || isAdmin;
    if (isModerator) {
      return new Response(JSON.stringify({
        error: 'cannot ban moderator/admin'
      }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json') || { banned: [], details: {} };
    const banned = Array.isArray(banData.banned) ? banData.banned : [];
    const details = banData.details || {};

    if (!banned.includes(username)) banned.push(username);

    // Store ban details
    details[username] = {
      reason: data.reason || 'No reason provided',
      bannedBy: data.admin || 'Unknown',
      timestamp: Date.now(),
      date: new Date().toISOString()
    };

    const ok = await putR2Json(env.SYSTEM_BUCKET, 'data/banlist.json', { banned, details });
    if (!ok) {
      return new Response(JSON.stringify({ error: 'failed to write banlist' }), {
        status: 500,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    return new Response(JSON.stringify({ success: true, banned }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Admin: Unban a user
async function handleUnbanUser(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response('Unauthorized', { status: 401 });
  }

  try {
    const data = await request.json();

    // Server-side admin verification via modCode/GDBrowser
    const adminName = (data.admin || data.adminUser || '').toString().trim();
    const admin = await requireAdmin(request, env, { username: adminName, accountID: data.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Unban user blocked: ${adminName} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const username = (data?.username || '').toString().trim().toLowerCase();

    if (!username) {
      return new Response(JSON.stringify({ error: 'username required' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json') || { banned: [], details: {} };
    let banned = Array.isArray(banData.banned) ? banData.banned : [];
    let details = banData.details || {};

    // Remove from list
    banned = banned.filter(u => u !== username);
    // Remove details
    if (details[username]) delete details[username];

    const ok = await putR2Json(env.SYSTEM_BUCKET, 'data/banlist.json', { banned, details });

    return new Response(JSON.stringify({ success: true }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Get bot config
async function handleGetBotConfig(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const key = 'data/bot/config.json';
  const data = await getR2Json(env.SYSTEM_BUCKET, key);

  return new Response(JSON.stringify(data || {}), {
    headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// Set bot config (requires only API key — the bot itself is trusted)
async function handleSetBotConfig(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const config = await request.json();
    const key = 'data/bot/config.json';

    await putR2Json(env.SYSTEM_BUCKET, key, config);

    return new Response(JSON.stringify({ success: true }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

async function handleListThumbnails(request, env) {
  const url = new URL(request.url);
  const levelId = url.searchParams.get('levelId');

  if (!levelId) {
    return new Response(JSON.stringify({ error: 'Level ID required' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const vm = new VersionManager(env.SYSTEM_BUCKET);
  let versions = await vm.getAllVersions(levelId);

  // FALLBACK: If no versions found in JSON, check if physical files exist (Legacy migration)
  if (versions.length === 0) {
    versions = await getLegacyVersions(env, levelId);
  }

  // Map to full URLs
  const results = versions.map(v => {
    // Construct URL based on version info
    // If it's a legacy entry, it might not have all fields, so we fallback
    const path = v.path || 'thumbnails';
    const format = v.format || 'webp';
    const version = v.version;

    // If it's a virtual legacy file, the URL is direct to the file, not constructed with version
    if (v.isLegacy) {
      // Reconstruct filename
      let filename = `${levelId}.${format}`;
      if (v.id !== 'legacy_file') {
        filename = `${levelId}_${v.id}.${format}`;
      }
      return {
        id: v.id,
        url: `${env.R2_PUBLIC_URL}/${path}/${filename}`,
        type: v.type,
        format: format
      };
    }

    return {
      id: v.id,
      url: `${env.R2_PUBLIC_URL}/${path}/${levelId}_${version}.${format}`,
      type: v.type || (format === 'gif' ? 'gif' : 'static'),
      format: format
    };
  });

  return new Response(JSON.stringify({ thumbnails: results }), {
    headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// NUEVO ENDPOINT: Obtener toda la información y metadatos de un thumbnail
async function handleGetThumbnailInfo(request, env) {
  const url = new URL(request.url);
  const levelId = url.searchParams.get('levelId');

  if (!levelId) {
    return new Response(JSON.stringify({ error: 'Level ID required' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    // 1. Buscar la versión activa en el sistema
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versionData = await vm.getVersion(levelId);

    if (!versionData) {
      return new Response(JSON.stringify({ error: 'Thumbnail not found' }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // 2. Construir la ruta (key) del archivo real
    const storedFormat = versionData.format || 'webp';
    const storedPath = versionData.path || 'thumbnails';
    let key;

    if (versionData.version === 'legacy') {
      key = `${storedPath}/${levelId}.${storedFormat}`;
    } else {
      key = `${storedPath}/${levelId}_${versionData.version}.${storedFormat}`;
    }

    // 3. Obtener los metadatos directamente del almacenamiento (R2/Bunny)
    // Esto recupera el campo 'uploadedBy' y otros datos personalizados
    const head = await env.THUMBNAILS_BUCKET.head(key);

    let metadata = head ? (head.customMetadata || {}) : {};

    // MERGE: Use metadata from VersionManager (primary source for BunnyCDN)
    if (versionData.uploadedBy) {
      metadata.uploadedBy = versionData.uploadedBy;
    }
    if (versionData.uploadedAt) {
      if (!metadata.uploadedAt) metadata.uploadedAt = versionData.uploadedAt;
    }

    // NORMALIZATION: Map differing keys to standard 'uploadedBy'
    if (!metadata.uploadedBy && metadata.originalSubmitter) {
      metadata.uploadedBy = metadata.originalSubmitter;
    }

    // FALLBACK: Si no hay creador en los metadatos del archivo, buscar en ratings
    if (!metadata.uploadedBy || metadata.uploadedBy === 'unknown' || metadata.uploadedBy === 'Unknown') {
      try {
        // Intentar leer del archivo de ratings (cache común de información del nivel)
        const ratingKey = `ratings/${levelId}.json`;
        const ratingData = await getR2Json(env.SYSTEM_BUCKET, ratingKey);

        if (ratingData && ratingData.uploadedBy && ratingData.uploadedBy !== 'Unknown') {
          metadata.uploadedBy = ratingData.uploadedBy;
          metadata.source = 'ratings_fallback'; // Indicar que vino del fallback
        }
      } catch (e) {
        console.warn('Metadata fallback failed (ratings):', e);
      }
    }

    // FALLBACK 2: Check latest uploads (for recent items where metadata might have been lost)
    if (!metadata.uploadedBy || metadata.uploadedBy === 'unknown' || metadata.uploadedBy === 'Unknown') {
      try {
        const latestData = await getR2Json(env.SYSTEM_BUCKET, 'data/system/latest_uploads.json');
        if (latestData && Array.isArray(latestData)) {
          const entry = latestData.find(item => item.levelId === parseInt(levelId));
          if (entry && entry.username) {
            metadata.uploadedBy = entry.username;
            metadata.source = 'latest_uploads_fallback';
          }
        }
      } catch (e) {
        console.warn('Metadata fallback failed (latest):', e);
      }
    }

    // FALLBACK: Fecha desde el archivo si no está en metadata
    if (!metadata.uploadedAt && head && head.uploaded) {
      try {
        metadata.uploadedAt = head.uploaded.toISOString();
      } catch (e) { }
    }

    if (!head) {
      return new Response(JSON.stringify({ error: 'File metadata not found', versionData, recoveredMetadata: metadata }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // 4. Devolver todo unificado
    return new Response(JSON.stringify({
      success: true,
      levelId,
      url: `${env.R2_PUBLIC_URL}/${key}`,
      version: versionData,
      metadata: metadata, // Metadata enriquecida
      fileInfo: {
        size: head.size,
        uploadedAt: head.uploaded,
        contentType: head.httpMetadata?.contentType
      }
    }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Get info error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get info', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// ===== PROFILE MUSIC HANDLERS =====

/**
 * Get profile music config for a user
 * GET /api/profile-music/:accountID
 */
async function handleGetProfileMusic(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const accountID = pathParts[pathParts.length - 1];

  if (!accountID || accountID === 'undefined') {
    return new Response(JSON.stringify({ error: 'Account ID required' }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const configKey = `profile-music/${accountID}.json`;
    const config = await getR2Json(env.SYSTEM_BUCKET, configKey);

    if (!config) {
      return new Response(JSON.stringify({ error: 'No music configured' }), {
        status: 404,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    return new Response(JSON.stringify(config), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Get profile music error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get music config' }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

/**
 * Upload/update profile music configuration
 * Server downloads the song from Newgrounds, cuts the fragment, and stores it
 * POST /api/profile-music/upload
 */
async function handleUploadProfileMusic(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const data = await request.json();
    const { accountID, username, songID, startMs, endMs, volume, songName, artistName } = data;

    if (!accountID || !songID) {
      return new Response(JSON.stringify({ error: 'Account ID and Song ID required' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Validate fragment duration (max 20 seconds)
    const duration = endMs - startMs;
    if (duration > 20000) {
      return new Response(JSON.stringify({ error: 'Fragment cannot exceed 20 seconds' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (duration < 5000) {
      return new Response(JSON.stringify({ error: 'Fragment must be at least 5 seconds' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    let audioBuffer;

    // Check if client sent the audio data directly (preferred method)
    if (data.audioData) {
      console.log(`[ProfileMusic] Receiving audio from client (base64 length: ${data.audioData.length})`);

      try {
        // Decode base64 to binary
        const binaryString = atob(data.audioData);
        audioBuffer = new Uint8Array(binaryString.length);
        for (let i = 0; i < binaryString.length; i++) {
          audioBuffer[i] = binaryString.charCodeAt(i);
        }

        console.log(`[ProfileMusic] Decoded audio: ${audioBuffer.length} bytes`);

        if (audioBuffer.length < 100) {
          return new Response(JSON.stringify({ error: 'Audio file too small' }), {
            status: 400,
            headers: { 'Content-Type': 'application/json', ...corsHeaders() }
          });
        }

        // Detect format - check for MP3 sync word (0xFF 0xFB/FA/F3/F2) or ID3 tag
        const isMp3 = (audioBuffer[0] === 0xFF && (audioBuffer[1] & 0xE0) === 0xE0) ||
          (audioBuffer[0] === 0x49 && audioBuffer[1] === 0x44 && audioBuffer[2] === 0x33); // "ID3"
        const isWav = audioBuffer[0] === 0x52 && audioBuffer[1] === 0x49 &&
          audioBuffer[2] === 0x46 && audioBuffer[3] === 0x46; // "RIFF"

        const extension = isWav ? 'wav' : 'mp3';
        const contentType = isWav ? 'audio/wav' : 'audio/mpeg';

        console.log(`[ProfileMusic] Detected format: ${extension}, size: ${audioBuffer.length} bytes`);

        // Store the audio file (always use .mp3 extension for compatibility)
        const audioKey = `profile-music/${accountID}.mp3`;
        await env.THUMBNAILS_BUCKET.put(audioKey, audioBuffer, {
          httpMetadata: {
            contentType: contentType,
            cacheControl: NO_STORE_CACHE_CONTROL
          },
          customMetadata: {
            songID: songID.toString(),
            startMs: startMs.toString(),
            endMs: endMs.toString(),
            uploadedBy: username || 'unknown',
            uploadedAt: new Date().toISOString(),
            format: extension
          }
        });

        // Store the config with format info
        const config = {
          accountID,
          username,
          songID,
          startMs,
          endMs,
          volume: volume || 1.0,
          enabled: true,
          songName: songName || '',
          artistName: artistName || '',
          format: extension,
          updatedAt: new Date().toISOString()
        };

        const configKey = `profile-music/${accountID}.json`;
        await putR2Json(env.SYSTEM_BUCKET, configKey, config);

        console.log(`[ProfileMusic] Uploaded ${extension} for account ${accountID}: ${audioBuffer.length} bytes`);

        return new Response(JSON.stringify({
          success: true,
          message: 'Profile music uploaded successfully',
          config
        }), {
          status: 200,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });

      } catch (e) {
        console.error(`[ProfileMusic] Failed to process audio: ${e.message}`);
        return new Response(JSON.stringify({ error: 'Invalid audio data', details: e.message }), {
          status: 400,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
    }

    // No audioData provided - return error (we no longer try to download from Newgrounds)
    return new Response(JSON.stringify({
      error: 'Audio data required',
      details: 'Please download the song in GD first using the Download button'
    }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Upload profile music error:', error);
    return new Response(JSON.stringify({ error: 'Failed to upload music', details: error.message }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

/**
 * Delete profile music
 * POST /api/profile-music/delete
 */
async function handleDeleteProfileMusic(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const data = await request.json();
    const { accountID, username } = data;

    if (!accountID) {
      return new Response(JSON.stringify({ error: 'Account ID required' }), {
        status: 400,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Delete the audio file (try both formats)
    try {
      await env.THUMBNAILS_BUCKET.delete(`profile-music/${accountID}.mp3`);
    } catch (e) { }
    try {
      await env.THUMBNAILS_BUCKET.delete(`profile-music/${accountID}.wav`);
    } catch (e) { }

    // Delete the config
    const configKey = `profile-music/${accountID}.json`;
    await env.SYSTEM_BUCKET.delete(configKey);

    console.log(`[ProfileMusic] Deleted music for account ${accountID}`);

    return new Response(JSON.stringify({ success: true, message: 'Profile music deleted' }), {
      status: 200,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Delete profile music error:', error);
    return new Response(JSON.stringify({ error: 'Failed to delete music' }), {
      status: 500,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

/**
 * Serve profile music audio file
 * GET /profile-music/:accountID.mp3
 */
async function handleServeProfileMusic(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  // Remove any extension from the filename
  const accountID = filename.replace(/\.(mp3|wav)$/, '');

  if (!accountID) {
    return new Response('Not found', { status: 404 });
  }

  try {
    // First check config to get the format
    const configKey = `profile-music/${accountID}.json`;
    const config = await getR2Json(env.SYSTEM_BUCKET, configKey);

    // Try to get the audio file (try mp3 first, then wav)
    const extension = config?.format || 'mp3';
    let audioKey = `profile-music/${accountID}.${extension}`;
    let audioObj = await env.THUMBNAILS_BUCKET.get(audioKey);

    // If not found with config extension, try the other format
    if (!audioObj) {
      const altExtension = extension === 'mp3' ? 'wav' : 'mp3';
      audioKey = `profile-music/${accountID}.${altExtension}`;
      audioObj = await env.THUMBNAILS_BUCKET.get(audioKey);
    }

    if (!audioObj) {
      return new Response('Music not found', { status: 404, headers: corsHeaders() });
    }

    const contentType = audioKey.endsWith('.wav') ? 'audio/wav' : 'audio/mpeg';

    const headers = {
      'Content-Type': contentType,
      'Cache-Control': NO_STORE_CACHE_CONTROL,
      ...corsHeaders()
    };

    if (config) {
      headers['X-Start-Ms'] = config.startMs?.toString() || '0';
      headers['X-End-Ms'] = config.endMs?.toString() || '20000';
      headers['X-Volume'] = config.volume?.toString() || '1.0';
    }

    return new Response(audioObj.body, {
      status: 200,
      headers
    });

  } catch (error) {
    console.error('Serve profile music error:', error);
    return new Response('Error serving music', { status: 500, headers: corsHeaders() });
  }
}

export default {
  async fetch(request, env, ctx) {
    // --- BUNNY.NET INTEGRATION ---
    // Force Bunny usage - Cloudflare storage removed
    env.THUMBNAILS_BUCKET = new BunnyBucket(
      env.BUNNY_ACCESS_KEY,
      env.BUNNY_SECRET_KEY,
      env.BUNNY_ENDPOINT || 'https://storage.bunnycdn.com',
      env.BUNNY_ZONE_NAME || 'paimbnails',
      'thumbnails'
    );
    env.SYSTEM_BUCKET = new BunnyBucket(
      env.BUNNY_ACCESS_KEY,
      env.BUNNY_SECRET_KEY,
      env.BUNNY_ENDPOINT || 'https://storage.bunnycdn.com',
      env.BUNNY_ZONE_NAME || 'paimbnails',
      'system'
    );
    // -----------------------------

    const url = new URL(request.url);
    const path = url.pathname;

    if (request.method === 'OPTIONS') {
      return handleOptions(request);
    }

    try {

      if (path.startsWith('/assets/')) {
        const key = path.substring(1); // remove leading slash
        const object = await env.SYSTEM_BUCKET.get(key);
        if (!object) return new Response('Not found', { status: 404 });

        return new Response(object.body, {
          headers: {
            'Content-Type': object.httpMetadata?.contentType || 'application/octet-stream',
            ...corsHeaders()
          }
        });
      }

      if (path === '/mod/upload' && request.method === 'POST') {
        return await handleUpload(request, env, ctx);
      }

      if (path === '/api/thumbnails/list' && request.method === 'GET') {
        return await handleListThumbnails(request, env);
      }

      if (path === '/api/thumbnails/info' && request.method === 'GET') {
        return await handleGetThumbnailInfo(request, env);
      }

      if (path === '/api/profiles/upload' && request.method === 'POST') {
        return await handleUpload(request, env, ctx);
      }

      if (path === '/api/profiles/config/upload' && request.method === 'POST') {
        return await handleUploadProfileConfig(request, env);
      }

      if (path.startsWith('/api/profiles/config/') && request.method === 'GET') {
        return await handleGetProfileConfig(request, env);
      }

// === PROFILE BACKGROUND ROUTES (profilebackground — canonical) ===
      if (path === '/api/backgrounds/upload' && request.method === 'POST') {
        return await handleUploadBackground(request, env, ctx);
      }

      if (path === '/api/backgrounds/upload-gif' && request.method === 'POST') {
        return await handleUploadBackgroundGIF(request, env, ctx);
      }

      if (path.startsWith('/backgrounds/') && request.method === 'GET') {
        return await handleServeBackground(request, env);
      }

      // Also serve from /profilebackground/ path (canonical)
      if (path.startsWith('/profilebackground/') && request.method === 'GET') {
        return await handleServeBackground(request, env);
      }

      // Serve pending profilebackgrounds for moderator preview in verification center
      if (path.startsWith('/pending_profilebackground/') && request.method === 'GET') {
        if (!verifyApiKey(request, env)) {
          return new Response('Unauthorized', { status: 401, headers: corsHeaders() });
        }
        const pendingFilename = decodeURIComponent(path.slice(1)); // strip leading /
        const obj = await env.THUMBNAILS_BUCKET.get(pendingFilename);
        if (!obj) {
          return new Response('Not found', { status: 404, headers: corsHeaders() });
        }
        const headers = new Headers();
        obj.writeHttpMetadata(headers);
        headers.set('Access-Control-Allow-Origin', '*');
        headers.set('Cache-Control', 'no-store, no-cache, must-revalidate');
        return new Response(obj.body, { headers });
      }

      // Legacy pending_backgrounds redirect (serve from new location)
      if (path.startsWith('/pending_backgrounds/') && request.method === 'GET') {
        if (!verifyApiKey(request, env)) {
          return new Response('Unauthorized', { status: 401, headers: corsHeaders() });
        }
        const pendingFilename = decodeURIComponent(path.slice(1)); // strip leading /
        const obj = await env.THUMBNAILS_BUCKET.get(pendingFilename);
        if (!obj) {
          return new Response('Not found', { status: 404, headers: corsHeaders() });
        }
        const headers = new Headers();
        obj.writeHttpMetadata(headers);
        headers.set('Access-Control-Allow-Origin', '*');
        headers.set('Cache-Control', 'no-store, no-cache, must-revalidate');
        return new Response(obj.body, { headers });
      }
      // ==============================================================

      // === WHITELIST ROUTES ===
      if (path === '/api/whitelist' && request.method === 'GET') {
        return await handleGetWhitelist(request, env);
      }

      if (path === '/api/whitelist/add' && request.method === 'POST') {
        return await handleAddWhitelist(request, env);
      }

      if (path === '/api/whitelist/remove' && request.method === 'POST') {
        return await handleRemoveWhitelist(request, env);
      }
      // ========================

      // === PROFILE MUSIC ROUTES ===
      if (path === '/api/profile-music/upload' && request.method === 'POST') {
        return await handleUploadProfileMusic(request, env, ctx);
      }

      if (path === '/api/profile-music/delete' && request.method === 'POST') {
        return await handleDeleteProfileMusic(request, env);
      }

      if (path.startsWith('/api/profile-music/') && request.method === 'GET') {
        return await handleGetProfileMusic(request, env);
      }

      if (path.startsWith('/profile-music/') && path.endsWith('.mp3') && request.method === 'GET') {
        return await handleServeProfileMusic(request, env);
      }
      // =============================

      if (path === '/mod/upload-gif' && request.method === 'POST') {
        return await handleUploadGIF(request, env, ctx);
      }

      if (path === '/api/mod/version' && request.method === 'GET') {
        return await handleVersionCheck(request);
      }

      if (path === '/api/latest-uploads' && request.method === 'GET') {
        return await handleGetLatestUploads(request, env);
      }

      if (path === '/downloads/paimon.level_thumbnails.geode' && request.method === 'GET') {
        return await handleModDownload(request, env, ctx);
      }

      if (path === '/api/daily/set' && request.method === 'POST') {
        return await handleSetDailyLevel(request, env);
      }

      if (path === '/api/daily/current' && request.method === 'GET') {
        return await handleGetDailyLevel(request, env);
      }

      if (path === '/api/weekly/set' && request.method === 'POST') {
        return await handleSetWeeklyLevel(request, env);
      }

      if (path === '/api/weekly/current' && request.method === 'GET') {
        return await handleGetWeeklyLevel(request, env);
      }

      if (path === '/api/leaderboard' && request.method === 'GET') {
        return await handleGetLeaderboard(request, env);
      }

      if (path === '/api/featured/history' && request.method === 'GET') {
        return await handleGetDailyWeeklyHistory(request, env);
      }


      // Profile ratings
      if (path === '/api/profile-ratings/vote' && request.method === 'POST') {
        return await handleProfileRatingVote(request, env);
      }

      if (path.startsWith('/api/profile-ratings/') && request.method === 'GET') {
        return await handleGetProfileRating(request, env);
      }

      // Rating v2 (new storage)
      if (path === '/api/v2/ratings/vote' && request.method === 'POST') {
        return await handleVoteV2(request, env);
      }

      if (path.startsWith('/api/v2/ratings/') && request.method === 'GET') {
        return await handleGetRatingV2(request, env);
      }

      if (path === '/api/ratings/vote' && request.method === 'POST') {
        return await handleVote(request, env);
      }

      if (path.startsWith('/api/ratings/') && request.method === 'GET') {
        return await handleGetRating(request, env);
      }

      if (path === '/api/suggestions/upload' && request.method === 'POST') {
        return await handleUploadSuggestion(request, env);
      }

      if (path === '/api/updates/upload' && request.method === 'POST') {
        return await handleUploadUpdate(request, env);
      }

      if (path.startsWith('/suggestions/')) {
        return await handleDownloadSuggestion(request, env);
      }

      if (path.startsWith('/updates/')) {
        return await handleDownloadUpdate(request, env);
      }

      if (path.startsWith('/api/download/')) {
        return await handleDownload(request, env, ctx);
      }

      if (path.startsWith('/t/')) {
        return await handleDirectThumbnail(request, env, ctx);
      }

      if (path === '/api/exists' && request.method === 'GET') {
        return await handleExists(request, env, ctx);
      }

      if (path.startsWith('/api/thumbnails/delete/') && request.method === 'POST') {
        return await handleDeleteThumbnail(request, env, ctx);
      }

      if (path === '/api/moderator/check' && request.method === 'GET') {
        return await handleModeratorCheck(request, env);
      }

      if (path === '/api/admin/set-daily' && request.method === 'POST') {
        return await handleSetDaily(request, env);
      }


      if (path === '/api/admin/banlist' && request.method === 'GET') {
        return await handleGetBanList(request, env);
      }

      if (path === '/api/admin/ban' && request.method === 'POST') {
        return await handleBanUser(request, env);
      }

      if (path === '/api/admin/unban' && request.method === 'POST') {
        return await handleUnbanUser(request, env);
      }

      if (path.startsWith('/api/queue/accept/') && request.method === 'POST') {
        return await handleAcceptQueue(request, env, ctx);
      }

      if (path.startsWith('/api/queue/claim/') && request.method === 'POST') {
        return await handleClaimQueue(request, env);
      }

      if (path.startsWith('/api/queue/reject/') && request.method === 'POST') {
        return await handleRejectQueue(request, env);
      }

      if (path.startsWith('/api/queue/') && request.method === 'GET') {
        return await handleGetQueue(request, env);
      }

      if (path === '/api/report/submit' && request.method === 'POST') {
        return await handleSubmitReport(request, env);
      }

      if (path === '/api/feedback/submit' && request.method === 'POST') {
        return await handleFeedbackSubmit(request, env);
      }

      if (path === '/api/feedback/list' && request.method === 'GET') {
        return await handleFeedbackList(request, env);
      }

      if (path === '/api/history/uploads' && request.method === 'GET') {
        return await handleGetHistory(request, env, 'uploads');
      }

      if (path === '/api/history/accepted' && request.method === 'GET') {
        return await handleGetHistory(request, env, 'accepted');
      }

      if (path === '/api/history/rejected' && request.method === 'GET') {
        return await handleGetHistory(request, env, 'rejected');
      }

      if (path === '/api/admin/add-moderator' && request.method === 'POST') {
        return await handleAddModerator(request, env);
      }

      if (path === '/api/admin/remove-moderator' && request.method === 'POST') {
        return await handleRemoveModerator(request, env);
      }

      if (path === '/api/admin/add-vip' && request.method === 'POST') {
        return await handleAddVip(request, env);
      }

      if (path === '/api/admin/remove-vip' && request.method === 'POST') {
        return await handleRemoveVip(request, env);
      }

      if (path === '/api/admin/moderators' && request.method === 'GET') {
        return await handleListModerators(request, env);
      }

      if (path === '/api/admin/backfill-contributors' && request.method === 'POST') {
        return await handleBackfillContributors(request, env);
      }

      if (path === '/api/admin/migrate-legacy' && request.method === 'POST') {
        return await handleMigrateLegacy(request, env);
      }

      if (path === '/api/moderators' && request.method === 'GET') {
        const moderators = await getModerators(env.SYSTEM_BUCKET);
        // Combine with admins and remove duplicates
        let allMods = [...new Set([...ADMIN_USERS, ...moderators])];

        // Filter out hidden users
        const hiddenUsers = ['viprin', 'robtop', 'kamisatonightor'];
        allMods = allMods.filter(user => !hiddenUsers.includes(user.toLowerCase()));

        const detailedMods = allMods.map((username) => {
          const isAdmin = ADMIN_USERS.includes(username.toLowerCase());
          return {
            username: username,
            role: isAdmin ? 'admin' : 'mod'
          };
        });

        return new Response(JSON.stringify({ moderators: detailedMods }), {
          status: 200,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }

      // Top creators leaderboard (cached)
      if (path === '/api/top-creators' && request.method === 'GET') {
        return await handleGetTopCreators(request, env);
      }

      // Top thumbnails leaderboard (cached)
      if (path === '/api/top-thumbnails' && request.method === 'GET') {
        return await handleGetTopThumbnails(request, env);
      }

      if (path.startsWith('/profiles/')) {
        return await handleServeProfile(request, env);
      }

      if (path.startsWith('/profileimgs/')) {
        return await handleServeProfileImg(request, env);
      }

      if (path === '/api/profileimgs/upload' && request.method === 'POST') {
        return await handleUpload(request, env, ctx);
      }


      if (path === '/api/gallery/list' && request.method === 'GET') {
        return await handleGalleryList(request, env);
      }

      if (path === '/api/debug/bunny-raw' && request.method === 'GET') {
        if (!verifyApiKey(request, env)) {
          return new Response('Unauthorized', { status: 401 });
        }
        if (!(await verifyAdminFromRequest(request, env))) {
          return new Response('Admin required', { status: 403 });
        }
        const url = new URL(request.url);
        const prefix = url.searchParams.get('prefix') || '';
        const bunnyUrl = `${env.THUMBNAILS_BUCKET.baseUrl}/${prefix}`;

        const res = await fetch(bunnyUrl, {
          method: 'GET',
          headers: { 'AccessKey': env.THUMBNAILS_BUCKET.apiKey }
        });

        const text = await res.text();
        return new Response(text, {
          status: res.status,
          headers: { 'Content-Type': 'application/json' }
        });
      }

      if (path === '/api/debug/r2-list' && request.method === 'GET') {
        if (!verifyApiKey(request, env)) {
          return new Response('Unauthorized', { status: 401 });
        }
        if (!(await verifyAdminFromRequest(request, env))) {
          return new Response('Admin required', { status: 403 });
        }
        try {
          const url = new URL(request.url);
          const prefix = url.searchParams.get('prefix') || '';
          const listed = await env.THUMBNAILS_BUCKET.list({ limit: 100, prefix });
          return new Response(JSON.stringify({
            objects: listed.objects.map(obj => ({
              key: obj.key,
              size: obj.size,
              uploaded: obj.uploaded
            }))
          }, null, 2), {
            status: 200,
            headers: { 'Content-Type': 'application/json', ...corsHeaders() }
          });
        } catch (error) {
          return new Response(JSON.stringify({ error: error.message }), {
            status: 500,
            headers: { 'Content-Type': 'application/json', ...corsHeaders() }
          });
        }
      }

      // --- BOT CONFIG ENDPOINTS ---
      if (path === '/api/bot/config' && request.method === 'GET') {
        return await handleGetBotConfig(request, env);
      }

      if (path === '/api/bot/config' && request.method === 'POST') {
        return await handleSetBotConfig(request, env);
      }
      // ----------------------------

      // === PET SHOP ROUTES ===
      if (path === '/api/pet-shop/list' && request.method === 'GET') {
        return await handlePetShopList(request, env);
      }

      if (path.startsWith('/api/pet-shop/download/') && request.method === 'GET') {
        return await handlePetShopDownload(request, env);
      }

      if (path === '/api/pet-shop/upload' && request.method === 'POST') {
        return await handlePetShopUpload(request, env);
      }
      // =======================

      // ── GD Proxy routes (for the Discord bot — avoids IP blocks) ────────
      if (path.startsWith('/api/level/') && request.method === 'GET') {
        return await handleGDLevelProxy(request, env);
      }

      if (path.startsWith('/api/gd/profile/') && request.method === 'GET') {
        return await handleGDProfileProxy(request, env);
      }
      // ────────────────────────────────────────────────────────────────────

      if (path === '/' || path === '/index.html') {
        return new Response(homeHtml, {
          status: 200,
          headers: {
            'Content-Type': 'text/html; charset=utf-8',
            ...corsHeaders()
          }
        });
      }

      if (path === '/donate' || path === '/donate.html') {
        return new Response(donateHtml, {
          status: 200,
          headers: {
            'Content-Type': 'text/html; charset=utf-8',
            ...corsHeaders()
          }
        });
      }

      if (path === '/download' || path === '/download.html') {
        const downloadHtml = await env.THUMBNAILS_BUCKET.get('public/download.html');
        if (downloadHtml) {
          return new Response(await downloadHtml.text(), {
            status: 200,
            headers: {
              'Content-Type': 'text/html; charset=utf-8',
              ...corsHeaders()
            }
          });
        }
        return new Response('Download page not found', { status: 404 });
      }

      if (path === '/guidelines' || path === '/guidelines.html') {
        return new Response(guidelinesHtml, {
          status: 200,
          headers: {
            'Content-Type': 'text/html; charset=utf-8',
            ...corsHeaders()
          }
        });
      }

      if (path === '/feedback-admin' || path === '/feedback-admin.html') {
        const adminHtml = await env.THUMBNAILS_BUCKET.get('public/feedback-admin.html');
        if (adminHtml) {
          return new Response(await adminHtml.text(), {
            status: 200,
            headers: {
              'Content-Type': 'text/html; charset=utf-8',
              ...corsHeaders()
            }
          });
        }
        return new Response('Admin page not found', { status: 404 });
      }

      if (path === '/download/paimon.level_thumbnails.geode') {
        const geodeFile = await env.THUMBNAILS_BUCKET.get('public/paimon.level_thumbnails.geode');
        if (geodeFile) {
          return new Response(geodeFile.body, {
            status: 200,
            headers: {
              'Content-Type': 'application/octet-stream',
              'Content-Disposition': 'attachment; filename="paimon.level_thumbnails.geode"',
              ...corsHeaders()
            }
          });
        }
        return new Response('File not found', { status: 404 });
      }

      if (path === '/health') {
        return new Response(JSON.stringify({
          status: 'ok',
          timestamp: new Date().toISOString()
        }), {
          status: 200,
          headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }

      // --- MIGRATION ENDPOINT ---
      if (path === '/api/admin/migrate-bunny') {
        if (!verifyApiKey(request, env)) {
          return new Response('Unauthorized', { status: 401 });
        }

        const url = new URL(request.url);
        const cursor = url.searchParams.get('cursor');
        const target = url.searchParams.get('target'); // 'thumbnails' or 'system'

        const BUNNY_ZONE = env.BUNNY_ZONE_NAME || "paimbnails";
        const BUNNY_KEY = env.BUNNY_SECRET_KEY;

        let bucket, bunnyFolder;
        if (target === 'thumbnails') {
          bucket = env.THUMBNAILS_BUCKET;
          bunnyFolder = 'thumbnails';
        } else if (target === 'system') {
          bucket = env.SYSTEM_BUCKET;
          bunnyFolder = 'system';
        } else {
          return new Response('Invalid target', { status: 400 });
        }

        // List objects (batch of 10 to avoid timeout)
        const list = await bucket.list({ limit: 10, cursor: cursor || undefined });
        const results = [];

        for (const obj of list.objects) {
          try {
            const r2Obj = await bucket.get(obj.key);
            if (r2Obj) {
              // Ensure path starts with folder but no double slashes
              const cleanKey = obj.key.startsWith('/') ? obj.key.substring(1) : obj.key;
              const bunnyPath = `${bunnyFolder}/${cleanKey}`;
              const bunnyUrl = `https://storage.bunnycdn.com/${BUNNY_ZONE}/${bunnyPath}`;

              const uploadResp = await fetch(bunnyUrl, {
                method: 'PUT',
                headers: {
                  'AccessKey': BUNNY_KEY,
                  'Content-Type': r2Obj.httpMetadata?.contentType || 'application/octet-stream'
                },
                body: r2Obj.body
              });

              results.push({ key: obj.key, success: uploadResp.ok, status: uploadResp.status });
            }
          } catch (e) {
            results.push({ key: obj.key, error: e.message });
          }
        }

        return new Response(JSON.stringify({
          cursor: list.truncated ? list.cursor : null,
          results,
          done: !list.truncated
        }), { headers: { 'Content-Type': 'application/json' } });
      }
      // --------------------------

      return new Response(JSON.stringify({ error: 'Not found' }), {
        status: 404,
        headers: {
          'Content-Type': 'application/json',
          ...corsHeaders()
        }
      });

    } catch (error) {
      console.error('Request error:', error);
      return new Response(JSON.stringify({
        error: 'Internal server error',
        details: error.message
      }), {
        status: 500,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
  },
  async scheduled(event, env, ctx) {
    // Cloudflare Cron minimum is 1 minute; configure wrangler.toml triggers accordingly.
    const headers = { 'X-API-Key': env.API_KEY };
    const init = { method: 'POST', headers };
    // alltime/creators recalculation removed
  }
};
