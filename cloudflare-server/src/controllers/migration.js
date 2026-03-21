/**
 * Migration controllers — backfill contributors, migrate legacy thumbnails
 */
import { corsHeaders } from '../middleware/cors.js';
import { NO_STORE_CACHE_CONTROL } from '../middleware/cors.js';
import { verifyApiKey, requireAdmin, forbiddenResponse } from '../middleware/auth.js';
import { getR2Json, putR2Json, listR2Keys } from '../services/storage.js';
import { VersionManager } from '../services/versions.js';

export async function handleBackfillContributors(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
      try { head = await env.THUMBNAILS_BUCKET.head(key); } catch (_) { }
      const meta = head?.customMetadata || {};

      const alreadyHas = meta.uploadedBy && (meta.acceptedBy || !info.acceptedBy);
      if (alreadyHas) { summary.skipped++; continue; }

      if (dryRun) { summary.updated++; continue; }

      try {
        const object = await env.THUMBNAILS_BUCKET.get(key);
        if (!object) { summary.skipped++; continue; }
        const buf = await object.arrayBuffer();

        await env.THUMBNAILS_BUCKET.put(key, buf, {
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
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Backfill contributors error:', error);
    return new Response(JSON.stringify({ error: 'Backfill failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleMigrateLegacy(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

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
      success: true, scanned: scannedCount, migrated: updatedCount,
      message: `Migrated ${updatedCount} legacy thumbnails to VersionManager`
    }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Migration error:', error);
    return new Response(JSON.stringify({ error: 'Migration failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
