/**
 * Profile controllers — backgrounds, profile config, serve profile/profileimgs
 */
import { corsHeaders, NO_STORE_CACHE_CONTROL } from '../middleware/cors.js';
import { verifyApiKey, verifyModAuth, ADMIN_USERS } from '../middleware/auth.js';
import { rejectIfMalicious } from '../middleware/security.js';
import { getR2Json, putR2Json } from '../services/storage.js';
import { getWhitelist } from '../services/moderation.js';
import { logAudit } from '../services/moderation.js';
import { cfCacheMatch, cfCachePut, cfCacheDelete, cfCacheKey, makeCacheable } from '../services/cache.js';

// Upload profile config
export async function handleUploadProfileConfig(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
  try {
    const formData = await request.formData();
    const accountID = formData.get('accountID');
    const config = formData.get('config');
    if (!accountID || !config) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
    try { JSON.parse(config); } catch (e) {
      return new Response(JSON.stringify({ error: 'Invalid JSON' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Get profile config
export async function handleGetProfileConfig(request, env) {
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

// Upload background (profilebackground)
export async function handleUploadBackground(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const banData = await getR2Json(env.SYSTEM_BUCKET, `data/bans/${accountID}.json`);
    if (banData && banData.banned) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const fileType = file.type || 'image/webp';
    let extension = 'webp';
    if (fileType === 'image/png') extension = 'png';
    else if (fileType === 'image/jpeg') extension = 'jpg';
    else if (fileType === 'image/gif') extension = 'gif';

    const securityReject = rejectIfMalicious(buffer, fileType, file.name || `bg_${accountID}.${extension}`);
    if (securityReject) return securityReject;

    const usernameLower = username ? username.toLowerCase() : '';
    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    const isModerator = authResult.authorized;
    const isAdmin = authResult.authorized && ADMIN_USERS.includes(usernameLower);
    const newModCode = authResult.newCode;

    const whitelistUsers = await getWhitelist(env.SYSTEM_BUCKET, 'profilebackground');
    const isWhitelisted = whitelistUsers.includes(usernameLower);

    const ts = Date.now().toString();
    let uploadKey;
    let uploadCategory;

    if (!isModerator && !isAdmin && !isWhitelisted) {
      uploadKey = `pending_profilebackground/${accountID}_${ts}.${extension}`;
      uploadCategory = 'pending_profilebackground';
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountID}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else {
      uploadKey = `profilebackground/${accountID}_${ts}.${extension}`;
      uploadCategory = 'profilebackground';
      const prefixes = [`profilebackground/${accountID}.`, `profilebackground/${accountID}_`];
      const keysToDelete = [];
      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix });
        for (const obj of list.objects) keysToDelete.push(obj.key);
      }
      if (keysToDelete.length > 0) await Promise.all(keysToDelete.map(k => env.THUMBNAILS_BUCKET.delete(k)));
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountID}_` });
      if (pendingList.objects.length > 0) await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`data/queue/profilebackground/${accountID}.json`));
      else await env.SYSTEM_BUCKET.delete(`data/queue/profilebackground/${accountID}.json`);
    }

    await env.THUMBNAILS_BUCKET.put(uploadKey, buffer, {
      httpMetadata: { contentType: fileType, cacheControl: 'no-store, no-cache, must-revalidate, max-age=0' },
      customMetadata: {
        uploadedBy: username || 'unknown', updated_by: username || 'unknown',
        uploadedAt: new Date().toISOString(), originalFormat: extension,
        version: ts, accountID: accountID.toString(),
        moderatorUpload: isModerator ? 'true' : 'false',
        whitelistUpload: isWhitelisted ? 'true' : 'false',
        category: uploadCategory, contentKind: 'profilebackground'
      }
    });

    if (!isModerator && !isAdmin && !isWhitelisted) {
      const queueKey = `data/queue/profilebackground/${accountID}.json`;
      const queueItem = {
        levelId: parseInt(accountID), accountID, submittedBy: username || 'unknown',
        timestamp: Date.now(), status: 'pending', category: 'profilebackground',
        filename: uploadKey, format: extension
      };
      if (ctx) ctx.waitUntil(putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem));
      else await putR2Json(env.SYSTEM_BUCKET, queueKey, queueItem);
    }

    if (isModerator || isAdmin || isWhitelisted) {
      logAudit(env.SYSTEM_BUCKET, 'profilebackground_upload', {
        accountID, username: usernameLower, direct: true,
        reason: isWhitelisted ? 'whitelist' : 'moderator'
      }, ctx);
    }

    const isPending = uploadCategory === 'pending_profilebackground';

    // Invalidate CF Cache for serve endpoints if content went live
    if (!isPending) {
      const origin = new URL(request.url).origin;
      const purgeExts = ['', '.webp', '.png', '.gif', '.jpg'];
      for (const ext of purgeExts) {
        cfCacheDelete(`${origin}/profilebackground/${accountID}${ext}`).catch(() => {});
        cfCacheDelete(`${origin}/backgrounds/${accountID}${ext}`).catch(() => {});
      }
    }

    const responseData = {
      success: true,
      message: isPending ? 'Profile background submitted for verification' : 'Background uploaded successfully',
      key: uploadKey, moderatorUpload: isModerator,
      whitelistUpload: isWhitelisted, pendingVerification: isPending,
      contentKind: 'profilebackground'
    };
    if (newModCode) responseData.newModCode = newModCode;

    return new Response(JSON.stringify(responseData), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Background upload error:', error);
    return new Response(JSON.stringify({ error: 'Background upload failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Upload background GIF
export async function handleUploadBackgroundGIF(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const accountID = parseInt(formData.get('accountID') || '0');
    const username = formData.get('username') || '';

    if (!file || !accountID) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
    if (file.type !== 'image/gif') {
      return new Response(JSON.stringify({ error: 'Invalid file type. Only GIF allowed.' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const usernameLower = username ? username.toLowerCase() : '';
    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    const isModerator = authResult.authorized;
    const newModCode = authResult.newCode;
    if (!isModerator) {
      return new Response(JSON.stringify({ error: 'Background GIF uploads are restricted to moderators only' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);
    const securityReject = rejectIfMalicious(buffer, 'image/gif', file.name || `bg_${accountID}.gif`);
    if (securityReject) return securityReject;

    const ts = Date.now().toString();
    const key = `profilebackground/${accountID}_${ts}.gif`;

    const prefixes = [`profilebackground/${accountID}.`, `profilebackground/${accountID}_`];
    const keysToDelete = [];
    for (const prefix of prefixes) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix });
      for (const obj of list.objects) keysToDelete.push(obj.key);
    }
    if (keysToDelete.length > 0) await Promise.all(keysToDelete.map(k => env.THUMBNAILS_BUCKET.delete(k)));

    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: { contentType: 'image/gif', cacheControl: 'no-store, no-cache, must-revalidate, max-age=0' },
      customMetadata: {
        uploadedBy: username || 'unknown', updated_by: username || 'unknown',
        uploadedAt: new Date().toISOString(), originalFormat: 'gif',
        version: ts, accountID: accountID.toString(),
        moderatorUpload: 'true', category: 'profilebackground', contentKind: 'profilebackground'
      }
    });

    const responseData = { success: true, message: 'Background GIF uploaded successfully', key, contentKind: 'profilebackground' };
    if (newModCode) responseData.newModCode = newModCode;

    // Invalidate CF Cache for background serve endpoints
    const origin = new URL(request.url).origin;
    const purgeExts = ['', '.webp', '.png', '.gif', '.jpg'];
    for (const ext of purgeExts) {
      cfCacheDelete(`${origin}/profilebackground/${accountID}${ext}`).catch(() => {});
      cfCacheDelete(`${origin}/backgrounds/${accountID}${ext}`).catch(() => {});
    }

    return new Response(JSON.stringify(responseData), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Background GIF upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Serve background — with CF Cache API (10 min)
export async function handleServeBackground(request, env) {
  // Skip cache for self-view requests (need freshness)
  const url = new URL(request.url);
  const isSelfRequest = url.searchParams.get('self') === '1';
  const cacheReq = cfCacheKey(request);
  if (!isSelfRequest) {
    const cached = await cfCacheMatch(cacheReq);
    if (cached) return cached;
  }

  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  const accountId = filename.replace(/\.[^/.]+$/, "");
  if (!accountId) return new Response('Account ID required', { status: 400 });

  let foundObject = null;
  let isPending = false;

  const list = await env.THUMBNAILS_BUCKET.list({ prefix: `profilebackground/${accountId}_` });
  if (list.objects.length > 0) {
    const sorted = list.objects.sort((a, b) => {
      const getTs = (k) => { const m = k.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
      return getTs(b.key) - getTs(a.key);
    });
    foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key, { skipMeta: true, cfCacheTtl: 300 });
  }

  if (!foundObject) {
    const legacyList = await env.THUMBNAILS_BUCKET.list({ prefix: `backgrounds/${accountId}_` });
    if (legacyList.objects.length > 0) {
      const sorted = legacyList.objects.sort((a, b) => {
        const getTs = (k) => { const m = k.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
        return getTs(b.key) - getTs(a.key);
      });
      foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key, { skipMeta: true, cfCacheTtl: 300 });
    }
  }

  if (!foundObject && isSelfRequest && verifyApiKey(request, env)) {
    const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountId}_` });
    if (pendingList.objects.length > 0) {
      const sorted = pendingList.objects.sort((a, b) => {
        const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
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
    headers.set('X-Content-Kind', 'profilebackground');
    if (isPending) headers.set('X-Pending-Verification', 'true');
    if (!isPending && !isSelfRequest) {
      const body = await foundObject.arrayBuffer();
      const resp = new Response(body, { headers });
      const cacheable = makeCacheable(resp, 1209600);
      cfCachePut(cacheReq, cacheable).catch(() => {});
      return cacheable.clone();
    }
    return new Response(foundObject.body, { headers });
  }
  return new Response('Background not found', { status: 404 });
}

// Serve profile (profiles/<accountId>) — with CF Cache API (10 min)
export async function handleServeProfile(request, env) {
  const url = new URL(request.url);
  const isSelfRequest = url.searchParams.get('self') === '1';
  const cacheReq = cfCacheKey(request);
  if (!isSelfRequest) {
    const cached = await cfCacheMatch(cacheReq);
    if (cached) return cached;
  }

  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  const accountId = filename.replace(/\.[^/.]+$/, "");
  if (!accountId) return new Response('Account ID required', { status: 400 });

  let foundObject = null;
  let isPending = false;

  const list = await env.THUMBNAILS_BUCKET.list({ prefix: `profiles/${accountId}_` });
  if (list.objects.length > 0) {
    const sorted = list.objects.sort((a, b) => {
      const getTs = (k) => { const m = k.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
      return getTs(b.key) - getTs(a.key);
    });
    foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key, { skipMeta: true, cfCacheTtl: 300 });
  }

  if (!foundObject) {
    const extensions = ['gif', 'webp', 'png', 'jpg', 'jpeg'];
    for (const ext of extensions) {
      const key = `profiles/${accountId}.${ext}`;
      const object = await env.THUMBNAILS_BUCKET.get(key, { skipMeta: true, cfCacheTtl: 300 });
      if (object) { foundObject = object; break; }
    }
  }

  if (!foundObject && isSelfRequest && verifyApiKey(request, env)) {
    const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profiles/${accountId}_` });
    if (pendingList.objects.length > 0) {
      const sorted = pendingList.objects.sort((a, b) => {
        const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
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
    if (isPending) headers.set('X-Pending-Verification', 'true');
    if (!isPending && !isSelfRequest) {
      const body = await foundObject.arrayBuffer();
      const resp = new Response(body, { headers });
      const cacheable = makeCacheable(resp, 1209600);
      cfCachePut(cacheReq, cacheable).catch(() => {});
      return cacheable.clone();
    }
    return new Response(foundObject.body, { headers });
  }
  return new Response('Profile not found', { status: 404 });
}

// Serve profile image (profileimgs/<accountId>) — with CF Cache API (10 min)
export async function handleServeProfileImg(request, env) {
  const url = new URL(request.url);
  const isSelfRequest = url.searchParams.get('self') === '1';
  const isPendingOnly = url.searchParams.get('pending') === '1';
  const cacheReq = cfCacheKey(request);
  if (!isSelfRequest && !isPendingOnly) {
    const cached = await cfCacheMatch(cacheReq);
    if (cached) return cached;
  }

  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  const accountId = filename.replace(/\.[^/.]+$/, "");
  if (!accountId) return new Response('Account ID required', { status: 400 });

  let foundObject = null;
  let isPending = false;

  const list = await env.THUMBNAILS_BUCKET.list({ prefix: `profileimgs/${accountId}_` });
  if (list.objects.length > 0) {
    const sorted = list.objects.sort((a, b) => {
      const getTs = (k) => { const m = k.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
      return getTs(b.key) - getTs(a.key);
    });
    foundObject = await env.THUMBNAILS_BUCKET.get(sorted[0].key, { skipMeta: true, cfCacheTtl: 300 });
  }

  if (!foundObject) {
    const extensions = ['gif', 'webp', 'png', 'jpg', 'jpeg', 'bmp', 'tiff'];
    for (const ext of extensions) {
      const key = `profileimgs/${accountId}.${ext}`;
      const object = await env.THUMBNAILS_BUCKET.get(key, { skipMeta: true, cfCacheTtl: 300 });
      if (object) { foundObject = object; break; }
    }
  }

  if ((!foundObject && isSelfRequest && verifyApiKey(request, env)) ||
      (isPendingOnly && verifyApiKey(request, env))) {
    if (isPendingOnly) foundObject = null;
    if (!foundObject) {
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${accountId}_` });
      if (pendingList.objects.length > 0) {
        const sorted = pendingList.objects.sort((a, b) => {
          const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
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
    if (isPending) headers.set('X-Pending-Verification', 'true');
    if (!isPending && !isSelfRequest && !isPendingOnly) {
      const body = await foundObject.arrayBuffer();
      const resp = new Response(body, { headers });
      const cacheable = makeCacheable(resp, 1209600);
      cfCachePut(cacheReq, cacheable).catch(() => {});
      return cacheable.clone();
    }
    return new Response(foundObject.body, { headers });
  }
  return new Response('Profile image not found', { status: 404 });
}
