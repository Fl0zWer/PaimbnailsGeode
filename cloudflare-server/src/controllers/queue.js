/**
 * Queue controllers — get/accept/claim/reject verification queue
 */
import { corsHeaders, NO_STORE_CACHE_CONTROL } from '../middleware/cors.js';
import { verifyApiKey, verifyModAuth, verifyModAuthFromBody, modAuthForbiddenResponse, isModeratorOrAdmin } from '../middleware/auth.js';
import { rejectIfMalicious } from '../middleware/security.js';
import { detectMimeType } from '../image-security.js';
import { getR2Json, putR2Json, listR2Keys } from '../services/storage.js';
import { VersionManager } from '../services/versions.js';
import { MAX_THUMBNAILS_PER_LEVEL } from '../services/versions.js';
import { dispatchWebhook } from '../services/webhook.js';
import { logAudit } from '../services/moderation.js';
import { cfCacheDelete, memCache } from '../services/cache.js';

export async function handleGetQueue(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const url = new URL(request.url);
  const queueUsername = url.searchParams.get('username') || '';
  const queueAccountID = parseInt(url.searchParams.get('accountID') || '0');

  const auth = await verifyModAuth(request, env, queueUsername, queueAccountID);
  if (!auth.authorized) {
    console.log(`[Security] Get queue blocked: ${queueUsername || '(no username)'}`);
    return modAuthForbiddenResponse(auth);
  }
  if (!(await isModeratorOrAdmin(env, queueUsername))) {
    return new Response(JSON.stringify({ error: 'Moderator/Admin privileges required' }), {
      status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const category = url.pathname.split('/').pop();

  try {
    let prefix = '';
    if (category === 'verify') prefix = 'data/queue/suggestions/';
    else if (category === 'update') prefix = 'data/queue/updates/';
    else if (category === 'report') prefix = 'data/queue/reports/';
    else if (category === 'profileimgs') prefix = 'data/queue/profileimgs/';
    else if (category === 'profilebackground') prefix = 'data/queue/profilebackground/';
    else prefix = `data/queue/${category}/`;

    const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);
    const items = [];
    const dataResults = await Promise.all(keys.map(key => getR2Json(env.SYSTEM_BUCKET, key)));
    for (const data of dataResults) {
      if (data) {
        if (Array.isArray(data) && data.length > 0) {
          const first = data[0];
          items.push({
            levelId: first.levelId, category: first.category || category,
            submittedBy: first.submittedBy,
            timestamp: data[data.length - 1].timestamp || first.timestamp,
            status: first.status || 'pending', note: first.note,
            accountID: first.accountID, suggestions: data
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
      status: 200, headers: { 'Content-Type': 'application/json', 'Surrogate-Control': 'no-store', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Get queue error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get queue', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', 'Surrogate-Control': 'no-store', ...corsHeaders() }
    });
  }
}

export async function handleAcceptQueue(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, category, username } = body;

    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Accept queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }
    if (!(await isModeratorOrAdmin(env, username))) {
      return new Response(JSON.stringify({ error: 'Moderator/Admin privileges required' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (!levelId || !category) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    let queueFolder = category;
    let sourceFolder = '';
    if (category === 'verify') { queueFolder = 'suggestions'; sourceFolder = 'suggestions'; }
    else if (category === 'update') { queueFolder = 'updates'; sourceFolder = 'updates'; }
    else if (category === 'report') { queueFolder = 'reports'; }
    else if (category === 'profileimg') { queueFolder = 'profileimgs'; }
    else if (category === 'profilebackground') { queueFolder = 'profilebackground'; }

    // Handle user report acceptance (ban user)
    if (category === 'report' && body.type === 'user') {
      const userQueueKey = `data/queue/reports/user_${levelId}.json`;
      const userReportData = await getR2Json(env.SYSTEM_BUCKET, userQueueKey);
      if (!userReportData) {
        return new Response(JSON.stringify({ error: 'User report not found' }), {
          status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      const targetUsername = (userReportData.reportedUsername || '').toLowerCase();
      if (!targetUsername) {
        return new Response(JSON.stringify({ error: 'No reported username found' }), {
          status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json') || { banned: [], details: {} };
      const banned = Array.isArray(banData.banned) ? banData.banned : [];
      const details = banData.details || {};
      if (!banned.includes(targetUsername)) banned.push(targetUsername);
      details[targetUsername] = {
        reason: `Banned via user reports (${userReportData.reports?.length || 0} reports)`,
        bannedBy: username || 'Unknown',
        timestamp: Date.now(),
        date: new Date().toISOString()
      };
      await putR2Json(env.SYSTEM_BUCKET, 'data/banlist.json', { banned, details });
      await env.SYSTEM_BUCKET.delete(userQueueKey);
      logAudit(env.SYSTEM_BUCKET, 'user_ban_from_reports', {
        targetUsername, reportCount: userReportData.reports?.length, bannedBy: username
      }, ctx);
      return new Response(JSON.stringify({ success: true, message: `User ${targetUsername} has been banned` }), {
        status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const queueKey = `data/queue/${queueFolder}/${levelId}.json`;
    const queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);

    const queueSubmitter = Array.isArray(queueData) ? (queueData[0]?.submittedBy || 'unknown') : (queueData?.submittedBy || 'unknown');
    const queueAccountID = Array.isArray(queueData) ? (queueData[0]?.accountID || 0) : (queueData?.accountID || 0);

    // Handle profileimg acceptance
    if (category === 'profileimg') {
      const accountId = levelId;
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${accountId}_` });
      if (pendingList.objects.length === 0) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'No pending profile image found' }), {
          status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      const sorted = pendingList.objects.sort((a, b) => {
        const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
        return getTs(b) - getTs(a);
      });
      const pendingObj = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      if (!pendingObj) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'Failed to read pending image' }), {
          status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      const pendingBuffer = await pendingObj.arrayBuffer();
      const realMime = detectMimeType(new Uint8Array(pendingBuffer)) || pendingObj.httpMetadata?.contentType || 'image/png';
      const pendingSecReject = rejectIfMalicious(new Uint8Array(pendingBuffer), realMime);
      if (pendingSecReject) return pendingSecReject;

      const ext = sorted[0].key.split('.').pop() || 'png';
      const ts = Date.now().toString();
      const destKey = `profileimgs/${accountId}_${ts}.${ext}`;

      const existingPrefixes = [`profileimgs/${accountId}.`, `profileimgs/${accountId}_`];
      const existingKeys = [];
      for (const pfx of existingPrefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: pfx });
        existingKeys.push(...list.objects.map(o => o.key));
      }
      if (existingKeys.length > 0) await Promise.all(existingKeys.map(k => env.THUMBNAILS_BUCKET.delete(k)));

      await env.THUMBNAILS_BUCKET.put(destKey, pendingBuffer, {
        httpMetadata: { contentType: pendingObj.httpMetadata?.contentType || `image/${ext}`, cacheControl: NO_STORE_CACHE_CONTROL },
        customMetadata: {
          uploadedBy: queueData?.submittedBy || 'unknown', updated_by: queueData?.submittedBy || 'unknown',
          acceptedBy: username || 'unknown', acceptedAt: new Date().toISOString(),
          accountID: accountId.toString(), category: 'profileimg'
        }
      });

      await Promise.all(sorted.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      await env.SYSTEM_BUCKET.delete(queueKey);

      // Invalidate CF Cache for profileimgs serve endpoint
      const piOrigin = new URL(request.url).origin;
      for (const ext of ['', '.webp', '.png', '.gif', '.jpg']) {
        cfCacheDelete(`${piOrigin}/profileimgs/${accountId}${ext}`).catch(() => {});
      }

      return new Response(JSON.stringify({ success: true, message: 'Profile image approved and published' }), {
        status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Handle profilebackground acceptance
    if (category === 'profilebackground') {
      const accountId = levelId;
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${accountId}_` });
      if (pendingList.objects.length === 0) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'No pending profile background found' }), {
          status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      const sorted = pendingList.objects.sort((a, b) => {
        const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
        return getTs(b) - getTs(a);
      });
      const pendingObj = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      if (!pendingObj) {
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'Failed to read pending background' }), {
          status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
      const pendingBuffer = await pendingObj.arrayBuffer();
      const realBgMime = detectMimeType(new Uint8Array(pendingBuffer)) || pendingObj.httpMetadata?.contentType || 'image/png';
      const pendingBgSecReject = rejectIfMalicious(new Uint8Array(pendingBuffer), realBgMime);
      if (pendingBgSecReject) return pendingBgSecReject;

      const ext = sorted[0].key.split('.').pop() || 'png';
      const ts = Date.now().toString();
      const destKey = `profilebackground/${accountId}_${ts}.${ext}`;

      const existingPrefixes = [`profilebackground/${accountId}.`, `profilebackground/${accountId}_`];
      const existingKeys = [];
      for (const pfx of existingPrefixes) {
        const list = await env.THUMBNAILS_BUCKET.list({ prefix: pfx });
        existingKeys.push(...list.objects.map(o => o.key));
      }
      if (existingKeys.length > 0) await Promise.all(existingKeys.map(k => env.THUMBNAILS_BUCKET.delete(k)));

      await env.THUMBNAILS_BUCKET.put(destKey, pendingBuffer, {
        httpMetadata: { contentType: pendingObj.httpMetadata?.contentType || `image/${ext}`, cacheControl: NO_STORE_CACHE_CONTROL },
        customMetadata: {
          uploadedBy: queueData?.submittedBy || 'unknown', updated_by: queueData?.submittedBy || 'unknown',
          acceptedBy: username || 'unknown', acceptedAt: new Date().toISOString(),
          accountID: accountId.toString(), category: 'profilebackground', contentKind: 'profilebackground'
        }
      });

      await Promise.all(sorted.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      await env.SYSTEM_BUCKET.delete(queueKey);
      logAudit(env.SYSTEM_BUCKET, 'profilebackground_accept', { accountID: accountId, acceptedBy: username }, ctx);

      // Invalidate CF Cache for background serve endpoints
      const bgOrigin = new URL(request.url).origin;
      for (const ext of ['', '.webp', '.png', '.gif', '.jpg']) {
        cfCacheDelete(`${bgOrigin}/profilebackground/${accountId}${ext}`).catch(() => {});
        cfCacheDelete(`${bgOrigin}/backgrounds/${accountId}${ext}`).catch(() => {});
      }

      return new Response(JSON.stringify({ success: true, message: 'Background approved and published' }), {
        status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    // Thumbnail suggestions / updates acceptance
    if (sourceFolder) {
      let sourceKey = body.targetFilename;
      if (!sourceKey) sourceKey = `${sourceFolder}/${levelId}.webp`;

      let sourceObject = await env.THUMBNAILS_BUCKET.get(sourceKey);
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
        await env.SYSTEM_BUCKET.delete(queueKey);
        return new Response(JSON.stringify({ error: 'Source thumbnail not found', details: `Could not find file for level ${levelId} in ${sourceFolder}/` }), {
          status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }

      const thumbnailBuffer = await sourceObject.arrayBuffer();
      const realThumbMime = detectMimeType(new Uint8Array(thumbnailBuffer)) || sourceObject.httpMetadata?.contentType || 'image/webp';
      const approveSecReject = rejectIfMalicious(new Uint8Array(thumbnailBuffer), realThumbMime);
      if (approveSecReject) return approveSecReject;

      const versionManager = new VersionManager(env.SYSTEM_BUCKET);
      const version = Date.now().toString();
      const destKey = `thumbnails/${levelId}_${version}.webp`;

      await env.THUMBNAILS_BUCKET.put(destKey, thumbnailBuffer, {
        httpMetadata: { contentType: 'image/webp', cacheControl: NO_STORE_CACHE_CONTROL },
        customMetadata: {
          acceptedBy: username || 'unknown', acceptedAt: new Date().toISOString(),
          status: 'accepted', originalSubmitter: queueSubmitter,
          uploadedBy: queueSubmitter, updated_by: queueSubmitter, version
        }
      });

      const appendRes = await versionManager.appendVersion(levelId, version, 'webp', 'thumbnails', 'static', {
        uploadedBy: queueSubmitter, uploadedAt: new Date().toISOString()
      }, MAX_THUMBNAILS_PER_LEVEL);

      if (ctx) ctx.waitUntil(env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`));
      else await env.SYSTEM_BUCKET.delete(`ratings/${levelId}.json`);

      // Enforce max 10 by deleting only the trimmed oldest physical files.
      for (const removed of appendRes.removed) {
        const removedPath = (removed.path || 'thumbnails').replace(/^\//, '');
        const removedKey = `${removedPath}/${levelId}_${removed.version}.${removed.format || 'webp'}`;
        if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(removedKey));
        else await env.THUMBNAILS_BUCKET.delete(removedKey);
      }

      if (ctx) ctx.waitUntil(env.THUMBNAILS_BUCKET.delete(sourceKey));
      else await env.THUMBNAILS_BUCKET.delete(sourceKey);

      // Multi-suggestion cleanup
      if (category === 'verify' && queueData) {
        let suggestions = Array.isArray(queueData) ? queueData : [queueData];
        const deletionPromises = suggestions.map(s => {
          const fname = s.filename || `suggestions/${levelId}.webp`;
          if (fname !== sourceKey) return env.THUMBNAILS_BUCKET.delete(fname);
          return Promise.resolve();
        });
        if (ctx) ctx.waitUntil(Promise.all(deletionPromises));
        else await Promise.all(deletionPromises);
      }
    }

    // Update latest uploads for verify/update categories
    if (category === 'verify' || category === 'update') {
      const updateLatest = async () => {
        const latestKey = 'data/system/latest_uploads.json';
        let latest = await getR2Json(env.SYSTEM_BUCKET, latestKey) || [];
        latest = latest.filter(item => item.levelId !== parseInt(levelId));
        latest.unshift({
          levelId: parseInt(levelId), username: queueSubmitter,
          timestamp: Date.now(), accountID: queueAccountID, acceptedBy: username || 'unknown'
        });
        if (latest.length > 20) latest = latest.slice(0, 20);
        await putR2Json(env.SYSTEM_BUCKET, latestKey, latest);
        memCache.invalidate('latest_uploads');
      };
      if (ctx) ctx.waitUntil(updateLatest());
      else await updateLatest();

      const webhookPayload = {
        levelId: parseInt(levelId), username: queueSubmitter,
        timestamp: Date.now(), is_update: category === 'update',
      };
      if (ctx) ctx.waitUntil(dispatchWebhook(env, 'upload', webhookPayload));
      else await dispatchWebhook(env, 'upload', webhookPayload);
    }

    await env.SYSTEM_BUCKET.delete(queueKey);

    // Invalidate CF Cache for /t/{levelId} after thumbnail acceptance
    if (category === 'verify' || category === 'update') {
      const tOrigin = new URL(request.url).origin;
      for (const ext of ['', '.webp', '.png', '.gif', '.jpg']) {
        cfCacheDelete(`${tOrigin}/t/${levelId}${ext}`).catch(() => {});
      }
    }

    return new Response(JSON.stringify({ success: true, message: 'Item accepted and added to history' }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Accept queue error:', error);
    return new Response(JSON.stringify({ error: 'Failed to accept item', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleClaimQueue(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, category, username } = body;

    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Claim queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }
    if (!(await isModeratorOrAdmin(env, username))) {
      return new Response(JSON.stringify({ error: 'Moderator/Admin privileges required' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (!levelId || !category || !username) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    let queueFolder = category;
    if (category === 'verify') queueFolder = 'suggestions';
    else if (category === 'update') queueFolder = 'updates';
    else if (category === 'report') queueFolder = 'reports';
    else if (category === 'profileimg') queueFolder = 'profileimgs';
    else if (category === 'profilebackground') queueFolder = 'profilebackground';

    const isUserReport = category === 'report' && body.type === 'user';
    const queueKey = isUserReport
      ? `data/queue/reports/user_${levelId}.json`
      : `data/queue/${queueFolder}/${levelId}.json`;
    const queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);

    if (!queueData) {
      return new Response(JSON.stringify({ error: 'Queue item not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    queueData.claimedBy = username;
    queueData.claimedAt = new Date().toISOString();
    queueData.status = 'claimed';

    await putR2Json(env.SYSTEM_BUCKET, queueKey, queueData);

    const claimKey = `data/claims/${category}/${isUserReport ? 'user_' : ''}${levelId}.json`;
    await putR2Json(env.SYSTEM_BUCKET, claimKey, {
      levelId: parseInt(levelId), category,
      claimedBy: username, claimedAt: new Date().toISOString()
    });

    return new Response(JSON.stringify({ success: true, message: 'Level claimed successfully', claimedBy: username }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Claim queue error:', error);
    return new Response(JSON.stringify({ error: 'Failed to claim item', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleRejectQueue(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { levelId, category, username, reason } = body;

    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) {
      console.log(`[Security] Reject queue blocked: ${username}`);
      return modAuthForbiddenResponse(auth);
    }
    if (!(await isModeratorOrAdmin(env, username))) {
      return new Response(JSON.stringify({ error: 'Moderator/Admin privileges required' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (!levelId || !category) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    let queueFolder = category;
    let sourceFolder = '';
    if (category === 'verify') { queueFolder = 'suggestions'; sourceFolder = 'suggestions'; }
    else if (category === 'update') { queueFolder = 'updates'; sourceFolder = 'updates'; }
    else if (category === 'report') { queueFolder = 'reports'; }
    else if (category === 'profileimg') { queueFolder = 'profileimgs'; }
    else if (category === 'profilebackground') { queueFolder = 'profilebackground'; }

    const isUserReport = category === 'report' && body.type === 'user';
    const queueKey = isUserReport
      ? `data/queue/reports/user_${levelId}.json`
      : `data/queue/${queueFolder}/${levelId}.json`;
    const itemData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    await env.SYSTEM_BUCKET.delete(queueKey);

    if (category === 'profileimg') {
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profileimgs/${levelId}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else if (category === 'profilebackground') {
      const pendingList = await env.THUMBNAILS_BUCKET.list({ prefix: `pending_profilebackground/${levelId}_` });
      if (pendingList.objects.length > 0) {
        await Promise.all(pendingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    } else if (sourceFolder) {
      if (itemData) {
        const items = Array.isArray(itemData) ? itemData : [itemData];
        const deletePromises = items.map(item => {
          const fname = item.filename || `${sourceFolder}/${levelId}.webp`;
          return env.THUMBNAILS_BUCKET.delete(fname);
        });
        await Promise.all(deletePromises);
      }
      await env.THUMBNAILS_BUCKET.delete(`${sourceFolder}/${levelId}.webp`);

      const remainingList = await env.THUMBNAILS_BUCKET.list({ prefix: `${sourceFolder}/${levelId}_`, limit: 50 });
      if (remainingList.objects.length > 0) {
        await Promise.all(remainingList.objects.map(o => env.THUMBNAILS_BUCKET.delete(o.key)));
      }
    }

    const logKey = `data/history/rejected/${levelId}-${Date.now()}.json`;
    await putR2Json(env.SYSTEM_BUCKET, logKey, {
      levelId: parseInt(levelId), category,
      rejectedBy: username || 'unknown', rejectedAt: new Date().toISOString(),
      reason: reason || 'No reason provided', originalData: itemData
    });

    return new Response(JSON.stringify({ success: true, message: 'Item rejected and thumbnail deleted' }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Reject queue error:', error);
    return new Response(JSON.stringify({ error: 'Failed to reject item', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
