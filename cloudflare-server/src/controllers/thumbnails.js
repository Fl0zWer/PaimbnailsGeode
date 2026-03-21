/**
 * Thumbnail controllers — upload, upload-gif, download, direct, delete, exists, list, info
 */
import { corsHeaders, redirectNoStore, NO_STORE_CACHE_CONTROL, noStoreHeaders } from '../middleware/cors.js';
import { verifyApiKey, verifyModAuth, ADMIN_USERS } from '../middleware/auth.js';
import { rejectIfMalicious } from '../middleware/security.js';
import { getR2Json, putR2Json, expandCandidates, expandKeyVariants, listR2Keys } from '../services/storage.js';
import { VersionManager } from '../services/versions.js';
import { updateCreatorLeaderboardCache } from '../services/leaderboard.js';
import { dispatchWebhook } from '../services/webhook.js';
import { getImageDimensions } from '../image-utils.js';
import { getWhitelist } from '../services/moderation.js';
import { cfCacheMatch, cfCachePut, cfCacheDelete, cfCacheKey, makeCacheable, memCache } from '../services/cache.js';

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

// ===== Upload thumbnail (PNG/WebP) =====
export async function handleUpload(request, env, ctx) {
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

    const usernameLower = username ? username.toLowerCase() : '';

    const authResult = await verifyModAuth(request, env, usernameLower, accountID);
    let isModerator = authResult.authorized;
    let newModCode = authResult.newCode;
    const isAdmin = authResult.authorized && ADMIN_USERS.includes(usernameLower);

    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (banned.includes(usernameLower)) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    console.log(`[Upload] user="${username}" accountID=${accountID} isAdmin=${isAdmin} isMod=${isModerator}`);

    if (!isModerator) {
      if (authResult.needsModCode) {
        return new Response(JSON.stringify({
          error: 'Mod code required', needsModCode: true,
          message: 'Generate a mod code in Paimbnails settings first'
        }), { status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() } });
      }
      return new Response(JSON.stringify({
        error: 'Invalid or expired mod code', invalidCode: true,
        message: 'Your mod code is invalid. Refresh it in Paimbnails settings.'
      }), { status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() } });
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

    const securityReject = rejectIfMalicious(buffer, fileType, file.name || `${levelId}.${extension}`);
    if (securityReject) return securityReject;

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
      if (checks.some(o => o)) isUpdate = true;
    }

    let uploadKey;
    let uploadCategory;
    const version = Date.now().toString();
    const versionManager = new VersionManager(env.SYSTEM_BUCKET);

    if (path === 'profileimgs') {
      const ts = Date.now().toString();
      const whitelistProfileImg = await getWhitelist(env.SYSTEM_BUCKET, 'profileimgs');
      const isWhitelistedPI = whitelistProfileImg.includes(usernameLower);

      if (!isModerator && !isAdmin && !isWhitelistedPI) {
        uploadKey = `pending_profileimgs/${levelId}_${ts}.${extension}`;
        uploadCategory = 'pending_profileimg';
        const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${levelId}_` });
        if (pendingList.objects.length > 0) {
          await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
        }
      } else {
        uploadKey = `${path}/${levelId}_${ts}.${extension}`;
        uploadCategory = 'profileimg';
        const prefixes = [`${path}/${levelId}.`, `${path}/${levelId}_`];
        const keysToDelete = [];
        for (const prefix of prefixes) {
          const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
          for (const obj of list.objects) keysToDelete.push(obj.key);
        }
        if (keysToDelete.length > 0) {
          await Promise.all(keysToDelete.map(k => env.THUMBNAILS_BUCKET.delete(k)));
        }
        const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${levelId}_` });
        if (pendingList.objects.length > 0) {
          await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
        }
        if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`data/queue/profileimgs/${levelId}.json`));
        else await env.SYSTEM_BUCKET.delete(`data/queue/profileimgs/${levelId}.json`);
      }

    } else if (isModerator) {
      const type = extension === 'gif' ? 'gif' : 'static';
      const thisId = version;
      uploadKey = `${path}/${levelId}_${thisId}.${extension}`;
      uploadCategory = 'live';

      const prefixes = [`${path}/${levelId}`, `${path}/gif/${levelId}`];
      const cleanupKeys = [];
      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
        for (const obj of list.objects) {
          const k = obj.key;
          const cleanKey = k.replace(/^\//, '');
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
      const legacyPaths = [`${path}/${levelId}.webp`, `${path}/${levelId}.png`];
      for (const lp of legacyPaths) {
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(lp));
        else await env.THUMBNAILS_BUCKET.delete(lp);
      }

      await versionManager.update(levelId, version, extension, path, type, {
        uploadedBy: username, uploadedAt: new Date().toISOString()
      });

      if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`));
      else await env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`);
    } else {
      return new Response(JSON.stringify({ error: 'Moderator auth required' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    await env.THUMBNAILS_BUCKET.put(uploadKey, buffer, {
      httpMetadata: { contentType: fileType, cacheControl: 'no-store, no-cache, must-revalidate, max-age=0' },
      customMetadata: {
        uploadedBy: username || 'unknown', updated_by: username || 'unknown',
        uploadedAt: new Date().toISOString(), originalFormat: extension,
        isUpdate: isUpdate ? 'true' : 'false', version: version,
        accountID: accountID.toString(), moderatorUpload: isModerator ? 'true' : 'false',
        category: uploadCategory
      }
    });

    if (username && username !== 'Unknown' && path !== 'profileimgs') {
      const updateCreatorCache = () => updateCreatorLeaderboardCache(env, username, {
        incrementUpload: true, accountID: accountID
      });
      if (ctx) ctx.waitUntil(updateCreatorCache());
      else await updateCreatorCache();
    }

    if (isModerator && path !== 'profileimgs') {
      const updateLatest = async () => {
        const latestKey = 'data/system/latest_uploads.json';
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];
        latest = latest.filter(item => item.levelId !== parseInt(levelId));
        latest.unshift({ levelId: parseInt(levelId), username: username || 'unknown', timestamp: Date.now(), accountID: accountID });
        if (latest.length > 20) latest = latest.slice(0, 20);
        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
        memCache.invalidate('latest_uploads');
      };
      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();

      const webhookPayload = { levelId: parseInt(levelId), username: username || 'unknown', timestamp: Date.now(), is_update: isUpdate || false };
      if (ctx) ctx.waitUntil(dispatchWebhook(env, 'upload', webhookPayload));
      else await dispatchWebhook(env, 'upload', webhookPayload);
    }

    const isPendingProfileImg = uploadCategory === 'pending_profileimg';

    // Invalidate CF Cache for /t/{levelId} serve endpoint
    if (uploadCategory === 'live' || uploadCategory === 'profileimg') {
      const origin = new URL(request.url).origin;
      const purgeExts = ['', '.webp', '.png', '.gif', '.jpg'];
      for (const ext of purgeExts) {
        cfCacheDelete(`${origin}/t/${levelId}${ext}`).catch(() => {});
      }
      if (uploadCategory === 'profileimg') {
        for (const ext of purgeExts) {
          cfCacheDelete(`${origin}/profileimgs/${levelId}${ext}`).catch(() => {});
        }
      }
    }

    const responseData = {
      success: true,
      message: isPendingProfileImg ? 'Profile image submitted for verification' : 'Moderator upload: Thumbnail published directly to global',
      key: uploadKey, isUpdate: isUpdate, moderatorUpload: true, inQueue: false, pendingVerification: isPendingProfileImg
    };
    if (newModCode) responseData.newModCode = newModCode;

    return new Response(JSON.stringify(responseData), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// ===== Upload GIF =====
export async function handleUploadGIF(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
      if (authResult.needsModCode) {
        return new Response(JSON.stringify({ error: 'Mod code required', needsModCode: true, message: 'Generate a mod code in Paimbnails settings first' }), {
          status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      return new Response(JSON.stringify({ error: 'Invalid or expired mod code', invalidCode: true, message: 'Your mod code is invalid. Refresh it in Paimbnails settings.' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);

    const securityReject = rejectIfMalicious(buffer, 'image/gif', file.name || `${levelId}.gif`);
    if (securityReject) return securityReject;

    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const version = Date.now().toString();
    const thisId = version;

    let key;
    let isProfile = path === 'profiles';

    if (isProfile) {
      const ts = Date.now().toString();
      key = `profiles/${levelId}_${ts}.gif`;
      const prefixes = [`profiles/${levelId}.`, `profiles/${levelId}_`];
      const keysToDelete = [];
      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
        for (const obj of list.objects) keysToDelete.push(obj.key);
      }
      if (keysToDelete.length > 0) {
        await Promise.all(keysToDelete.map(k => env.THUMBNAILS_BUCKET.delete(k)));
      }
    } else {
      key = `${path}/${levelId}_${thisId}.gif`;
      const prefixes = [`${path}/${levelId}`, `${path}/gif/${levelId}`];
      const cleanupKeys = [];
      for (const prefix of prefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
        for (const obj of list.objects) {
          if (obj.key !== key) cleanupKeys.push(obj.key);
        }
      }
      cleanupKeys.push(`${path}/${levelId}.webp`, `${path}/${levelId}.png`, `${path}/${levelId}.gif`);
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
      httpMetadata: { contentType: 'image/gif', cacheControl: 'no-store, no-cache, must-revalidate, max-age=0' },
      customMetadata: {
        uploadedBy: username || 'unknown', uploadedAt: new Date().toISOString(),
        originalFormat: 'gif', isUpdate: isUpdate ? 'true' : 'false',
        version: thisId, accountID: accountID.toString(),
        moderatorUpload: 'true', category: isProfile ? 'profile' : 'live'
      }
    });

    if (!isProfile) {
      const updateVersion = async () => {
        await versionManager.update(levelId, thisId, 'gif', path, 'gif', {
          uploadedBy: username || 'unknown', uploadedAt: new Date().toISOString()
        });
      };
      if (ctx) ctx.waitUntil(updateVersion());
      else await updateVersion();

      const updateLatest = async () => {
        const latestKey = 'data/system/latest_uploads.json';
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];
        latest = latest.filter(item => item.levelId !== parseInt(levelId));
        latest.unshift({ levelId: parseInt(levelId), username: username || 'unknown', timestamp: Date.now(), accountID: accountID, isGif: true });
        if (latest.length > 20) latest = latest.slice(0, 20);
        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
        memCache.invalidate('latest_uploads');
      };
      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();
    }

    const responseData = { success: true, message: 'GIF uploaded successfully (Moderator)', key, isUpdate };
    if (newModCode) responseData.newModCode = newModCode;

    // Invalidate CF Cache for /t/{levelId}
    const origin = new URL(request.url).origin;
    const purgeExts = ['', '.webp', '.png', '.gif', '.jpg'];
    for (const ext of purgeExts) {
      cfCacheDelete(`${origin}/t/${levelId}${ext}`).catch(() => {});
    }
    if (isProfile) {
      for (const ext of purgeExts) {
        cfCacheDelete(`${origin}/profiles/${levelId}${ext}`).catch(() => {});
      }
    }

    return new Response(JSON.stringify(responseData), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Upload GIF error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// ===== Download =====
export async function handleDownload(request, env, ctx) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  const levelId = filename.replace(/\.(webp|png|gif)$/, '');
  const requestedFormat = filename.endsWith('.png') ? 'png' : (filename.endsWith('.gif') ? 'gif' : 'webp');
  const rawPath = url.searchParams.get('path') || '/thumbnails';
  const path = rawPath.replace(/^\//, '');

  let foundKey = null;

  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versionData = await vm.getVersion(levelId);
    if (versionData) {
      const storedFormat = versionData.format || 'webp';
      const storedPath = versionData.path || 'thumbnails';
      if (!(requestedFormat === 'gif' && storedFormat !== 'gif') &&
          !(storedFormat === 'gif' && requestedFormat !== 'gif')) {
        let vStr = versionData.version;
        foundKey = vStr === 'legacy' ? `${storedPath}/${levelId}.${storedFormat}` : `${storedPath}/${levelId}_${vStr}.${storedFormat}`;
        console.log(`[Download] Found via VersionManager (Lookup): ${foundKey}`);
      }
    }
  } catch (e) { console.warn('VersionManager lookup failed:', e); }

  if (!foundKey && levelId.includes('_')) foundKey = `${path}/${filename}`;

  if (!foundKey) {
    console.log(`[Download] Search for ${levelId} in ${path}`);
    const prefixes = [`${path}/${levelId}`, `/${path}/${levelId}`];
    for (const listPrefix of prefixes) {
      if (foundKey) break;
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: listPrefix, limit: 20 });
      const sortedObjects = list.objects.sort((a, b) => b.uploaded.getTime() - a.uploaded.getTime());
      for (const obj of sortedObjects) {
        const key = obj.key;
        const keyFilename = key.split('/').pop();
        const keyBase = keyFilename.replace(/\.(webp|png|gif)$/, '');
        if (keyBase === levelId || keyBase.startsWith(`${levelId}_`)) {
          if (requestedFormat === 'gif' && !key.endsWith('.gif')) continue;
          if ((requestedFormat === 'webp' || requestedFormat === 'png') && key.endsWith('.gif')) continue;
          foundKey = key;
          console.log(`[Download] Found via Search: ${key}`);
          break;
        }
      }
    }
  }

  if (!foundKey) {
    return new Response('Thumbnail not found', { status: 404, headers: corsHeaders() });
  }

  const bunnyUrl = `${env.R2_PUBLIC_URL}/${foundKey.replace(/^\//, '')}`;

  if (requestedFormat === 'png' && !foundKey.endsWith('.png')) {
    try {
      const imageRes = await fetch(bunnyUrl, {
        headers: { 'Accept': 'image/png', 'Cache-Control': 'no-cache' },
        cf: { image: { format: 'png' } }
      });
      if (imageRes.ok) {
        const newHeaders = new Headers(imageRes.headers);
        newHeaders.set('Content-Type', 'image/png');
        const cors = corsHeaders();
        for (const [k, v] of Object.entries(cors)) newHeaders.set(k, v);
        return new Response(imageRes.body, { status: 200, headers: newHeaders });
      }
    } catch (err) { console.error(`[Download] Conversion error: ${err}`); }
  }

  const ts = url.searchParams.get('t') || url.searchParams.get('_ts');
  const finalUrl = ts ? `${bunnyUrl}?t=${ts}` : bunnyUrl;
  return redirectNoStore(finalUrl, 302, corsHeaders());
}

// ===== Direct thumbnail (/t/:id) with CF Cache API =====
export async function handleDirectThumbnail(request, env, ctx) {
  // Layer 1: CF Cache API — return cached response if available
  const cacheReq = cfCacheKey(request);
  const cached = await cfCacheMatch(cacheReq);
  if (cached) return cached;

  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[2];
  const levelId = filename.replace(/\.(webp|gif|png)$/, '');
  const requestedFormat = filename.endsWith('.png') ? 'png' : (filename.endsWith('.gif') ? 'gif' : 'webp');

  let bestMatch = null;
  let maxVersion = -1;

  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versionData = await vm.getVersion(levelId);
    if (versionData) {
      const storedFormat = versionData.format || 'webp';
      const storedPath = versionData.path || 'thumbnails';
      const vStr = versionData.version;
      if (!(requestedFormat === 'gif' && storedFormat !== 'gif') &&
          !(storedFormat === 'gif' && requestedFormat !== 'gif')) {
        bestMatch = vStr === 'legacy' ? `${storedPath}/${levelId}.${storedFormat}` : `${storedPath}/${levelId}_${vStr}.${storedFormat}`;
        console.log(`[Direct] Found via VersionManager: ${bestMatch}`);
      }
    }
  } catch (e) { console.warn('[Direct] VersionManager error:', e); }

  if (!bestMatch) {
    console.log(`[Direct] Search for ${levelId} (Fallback)`);
    try {
      const checks = [
        env.THUMBNAILS_BUCKET.list({ prefix: `thumbnails/${levelId}.`, limit: 5 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `thumbnails/${levelId}_`, limit: 20 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `thumbnails/gif/${levelId}.`, limit: 5 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `/thumbnails/${levelId}.`, limit: 5 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `/thumbnails/${levelId}_`, limit: 20 }),
        env.THUMBNAILS_BUCKET.list({ prefix: `/thumbnails/gif/${levelId}.`, limit: 5 }),
      ];
      const results = await Promise.all(checks);
      let allObjects = [];
      for (let i = 0; i < 6; i++) {
        if (results[i] && results[i].objects) allObjects.push(...results[i].objects);
      }
      allObjects = [...new Map(allObjects.map(item => [item.key, item])).values()];

      for (const obj of allObjects) {
        const key = obj.key;
        const cleanKey = key.replace(/^\//, '');
        const isExact = cleanKey.includes(`/${levelId}.`) || cleanKey.includes(`/${levelId}_`);
        if (!isExact) continue;
        const versionMatch = key.match(/_(\d+)\.(webp|png|gif)$/);
        let version = -1;
        if (versionMatch) version = parseInt(versionMatch[1]);
        if (version > maxVersion) { maxVersion = version; bestMatch = key; }
        else if (version === maxVersion && !bestMatch) bestMatch = key;
      }
    } catch (e) { console.error('Class B error', e); }
  }

  if (bestMatch) {
    const bunnyUrl = `${env.R2_PUBLIC_URL}/${bestMatch.replace(/^\//, '')}`;

    if (requestedFormat === 'png' && !bestMatch.endsWith('.png') && !bestMatch.endsWith('.gif')) {
      try {
        const imageRes = await fetch(bunnyUrl, {
          headers: { 'Accept': 'image/png', 'Cache-Control': 'no-cache' },
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

    const object = await env.THUMBNAILS_BUCKET.get(bestMatch, { skipMeta: true, cfCacheTtl: 300 });
    if (object) {
      const headers = new Headers();
      object.writeHttpMetadata(headers);
      headers.set('etag', object.httpEtag);
      headers.set('Access-Control-Allow-Origin', '*');

      // Build response and store in CF Cache (2 weeks)
      const body = await object.arrayBuffer();
      const response = new Response(body, { headers });
      const cacheableResp = makeCacheable(response, 1209600);
      if (ctx) ctx.waitUntil(cfCachePut(cacheReq, cacheableResp));
      else cfCachePut(cacheReq, cacheableResp).catch(() => {});

      // Return a fresh clone to the client
      return cacheableResp.clone();
    }
  }

  return new Response(JSON.stringify({ error: 'Thumbnail not found' }), {
    status: 404, headers: { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' }
  });
}

// ===== Exists =====
export async function handleExists(request, env, ctx) {
  const url = new URL(request.url);
  const levelId = url.searchParams.get('levelId');
  const path = (url.searchParams.get('path') || 'thumbnails').replace(/^\//, '');

  if (!levelId) {
    return new Response(JSON.stringify({ error: 'Missing levelId' }), {
      status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const versionManager = new VersionManager(env.SYSTEM_BUCKET);
  const versionData = await versionManager.getVersion(levelId);
  if (versionData) {
    return new Response(JSON.stringify({ exists: true }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const baseKeys = [`${path}/${levelId}.webp`, `${path}/gif/${levelId}.gif`, `${path}/${levelId}.png`];
  const keys = expandCandidates(baseKeys);

  let exists = false;
  for (const key of keys) {
    const object = await env.THUMBNAILS_BUCKET.head(key);
    if (object) { exists = true; break; }
  }

  return new Response(JSON.stringify({ exists }), {
    status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// ===== Delete =====
export async function handleDeleteThumbnail(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, username } = body;
    const accountID = parseInt(body.accountID || '0');

    if (!levelId || !username) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const authResult = await verifyModAuth(request, env, username, accountID);
    if (!authResult.authorized) {
      return new Response(JSON.stringify({ error: 'Not authorized - moderator only' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const versionManager = new VersionManager(env.SYSTEM_BUCKET);
    const prefixes = [`thumbnails/${levelId}`, `thumbnails/gif/${levelId}`];
    let keysToDelete = [];

    for (const prefix of prefixes) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: prefix });
      for (const obj of list.objects) {
        const key = obj.key;
        const cleanKey = key.replace(/^\//, '');
        if (cleanKey.match(new RegExp(`^thumbnails/${levelId}(\\.|_)`)) ||
          cleanKey.match(new RegExp(`^thumbnails/gif/${levelId}\\.`))) {
          keysToDelete.push(key);
        }
      }
    }
    keysToDelete.push(`thumbnails/${levelId}.webp`, `thumbnails/${levelId}.png`, `thumbnails/gif/${levelId}.gif`);
    keysToDelete = [...new Set(keysToDelete)];

    console.log(`[Delete] Deleting ${keysToDelete.length} files for level ${levelId}:`, keysToDelete);
    for (const key of keysToDelete) await env.THUMBNAILS_BUCKET.delete(key);

    await versionManager.delete(levelId);

    // Invalidate CF Cache for /t/{levelId}
    const origin = new URL(request.url).origin;
    const purgeExts = ['', '.webp', '.png', '.gif', '.jpg'];
    for (const ext of purgeExts) {
      cfCacheDelete(`${origin}/t/${levelId}${ext}`).catch(() => {});
    }

    const logKey = `data/logs/deleted/${levelId}-${Date.now()}.json`;
    await putR2Json(env.SYSTEM_BUCKET, logKey, {
      levelId: parseInt(levelId), deletedBy: username, deletedAt: new Date().toISOString(), timestamp: Date.now()
    });

    return new Response(JSON.stringify({ success: true, message: 'Thumbnail deleted successfully' }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Delete thumbnail error:', error);
    return new Response(JSON.stringify({ error: 'Failed to delete thumbnail', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// ===== List thumbnails =====
export async function handleListThumbnails(request, env) {
  const url = new URL(request.url);
  const levelId = url.searchParams.get('levelId');

  if (!levelId) {
    return new Response(JSON.stringify({ error: 'Level ID required' }), {
      status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const vm = new VersionManager(env.SYSTEM_BUCKET);
  let versions = await vm.getAllVersions(levelId);

  if (versions.length === 0) {
    versions = await getLegacyVersions(env, levelId);
  }

  const results = versions.map(v => {
    const path = v.path || 'thumbnails';
    const format = v.format || 'webp';
    const version = v.version;

    if (v.isLegacy) {
      let filename = `${levelId}.${format}`;
      if (v.id !== 'legacy_file') filename = `${levelId}_${v.id}.${format}`;
      return { id: v.id, url: `${env.R2_PUBLIC_URL}/${path}/${filename}`, type: v.type, format };
    }

    return {
      id: v.id,
      url: `${env.R2_PUBLIC_URL}/${path}/${levelId}_${version}.${format}`,
      type: v.type || (format === 'gif' ? 'gif' : 'static'),
      format
    };
  });

  return new Response(JSON.stringify({ thumbnails: results }), {
    headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// ===== Get thumbnail info =====
export async function handleGetThumbnailInfo(request, env) {
  const url = new URL(request.url);
  const levelId = url.searchParams.get('levelId');

  if (!levelId) {
    return new Response(JSON.stringify({ error: 'Level ID required' }), {
      status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const vm = new VersionManager(env.SYSTEM_BUCKET);
    const versionData = await vm.getVersion(levelId);

    if (!versionData) {
      return new Response(JSON.stringify({ error: 'Thumbnail not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const storedFormat = versionData.format || 'webp';
    const storedPath = versionData.path || 'thumbnails';
    let key = versionData.version === 'legacy'
      ? `${storedPath}/${levelId}.${storedFormat}`
      : `${storedPath}/${levelId}_${versionData.version}.${storedFormat}`;

    const head = await env.THUMBNAILS_BUCKET.head(key);
    let metadata = head ? (head.customMetadata || {}) : {};

    if (versionData.uploadedBy) metadata.uploadedBy = versionData.uploadedBy;
    if (versionData.uploadedAt && !metadata.uploadedAt) metadata.uploadedAt = versionData.uploadedAt;
    if (!metadata.uploadedBy && metadata.originalSubmitter) metadata.uploadedBy = metadata.originalSubmitter;

    if (!metadata.uploadedBy || metadata.uploadedBy === 'unknown' || metadata.uploadedBy === 'Unknown') {
      try {
        const ratingData = await getR2Json(env.SYSTEM_BUCKET, `ratings/${levelId}.json`);
        if (ratingData && ratingData.uploadedBy && ratingData.uploadedBy !== 'Unknown') {
          metadata.uploadedBy = ratingData.uploadedBy;
          metadata.source = 'ratings_fallback';
        }
      } catch (e) { console.warn('Metadata fallback failed (ratings):', e); }
    }

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
      } catch (e) { console.warn('Metadata fallback failed (latest):', e); }
    }

    if (!metadata.uploadedAt && head && head.uploaded) {
      try { metadata.uploadedAt = head.uploaded.toISOString(); } catch (e) { }
    }

    if (!head) {
      return new Response(JSON.stringify({ error: 'File metadata not found', versionData, recoveredMetadata: metadata }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    return new Response(JSON.stringify({
      success: true, levelId,
      url: `${env.R2_PUBLIC_URL}/${key}`,
      version: versionData, metadata,
      fileInfo: { size: head.size, uploadedAt: head.uploaded, contentType: head.httpMetadata?.contentType }
    }), { status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() } });
  } catch (error) {
    console.error('Get info error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get info', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
