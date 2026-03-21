/**
 * Suggestions controllers — upload, download, upload-update, download-update
 */
import { corsHeaders, NO_STORE_CACHE_CONTROL } from '../middleware/cors.js';
import { verifyApiKey } from '../middleware/auth.js';
import { rejectIfMalicious } from '../middleware/security.js';
import { getR2Json, putR2Json } from '../services/storage.js';
import { getImageDimensions } from '../image-utils.js';

// Upload suggestion (non-moderators)
export async function handleUploadSuggestion(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (username !== 'Unknown' && banned.includes(username.toLowerCase())) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    if (accountID === 0) {
      console.log(`Unauthenticated suggestion upload (accountID=0) by '${username}' for level ${levelId}`);
    }

    if (file.size > parseInt(env.MAX_UPLOAD_SIZE)) {
      return new Response(JSON.stringify({ error: 'File too large' }), {
        status: 413, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const arrayBuffer = await file.arrayBuffer();
    const buffer = new Uint8Array(arrayBuffer);

    const securityReject = rejectIfMalicious(buffer, file.type || 'image/webp', file.name || `suggestion_${levelId}.webp`);
    if (securityReject) return securityReject;

    const dims = getImageDimensions(buffer);
    if (!dims) {
      return new Response(JSON.stringify({ error: 'Invalid or unsupported image format' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const queueKey = `data/queue/suggestions/${levelId}.json`;
    let queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);

    let suggestions = [];
    if (queueData) {
      if (Array.isArray(queueData)) {
        suggestions = queueData;
      } else {
        if (!queueData.filename) queueData.filename = `suggestions/${levelId}.webp`;
        suggestions = [queueData];
      }
    }

    if (suggestions.length >= 10) {
      return new Response(JSON.stringify({ error: 'Suggestion limit reached for this level (max 10)' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const uniqueId = Math.random().toString(36).substring(7);
    const timestamp = Date.now();
    const filename = `suggestions/${levelId}_${timestamp}_${uniqueId}.webp`;

    await env.THUMBNAILS_BUCKET.put(filename, buffer, {
      httpMetadata: { contentType: 'image/webp', cacheControl: NO_STORE_CACHE_CONTROL },
      customMetadata: { uploadedBy: username, uploadedAt: new Date().toISOString(), category: 'suggestion' }
    });

    suggestions.push({
      levelId: parseInt(levelId), category: 'verify', submittedBy: username,
      timestamp, status: 'pending', note: 'User suggestion',
      accountID, unauthenticated: accountID === 0, filename
    });

    await putR2Json(env.SYSTEM_BUCKET, queueKey, suggestions);

    return new Response(JSON.stringify({ success: true, message: 'Suggestion uploaded successfully' }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Suggestion upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Upload update (non-moderators)
export async function handleUploadUpdate(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (username !== 'Unknown' && banned.includes(username.toLowerCase())) {
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

    const dims = getImageDimensions(buffer);
    if (!dims) {
      return new Response(JSON.stringify({ error: 'Invalid or unsupported image format' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const detectedType = dims.type;
    const extMap = { png: 'png', webp: 'webp', jpeg: 'jpg', gif: 'gif' };
    const mimeMap = { png: 'image/png', webp: 'image/webp', jpeg: 'image/jpeg', gif: 'image/gif' };
    const ext = extMap[detectedType] || 'webp';
    const mime = mimeMap[detectedType] || 'image/webp';

    const securityReject = rejectIfMalicious(buffer, mime, file.name || `update_${levelId}.${ext}`);
    if (securityReject) return securityReject;

    const queueKey = `data/queue/updates/${levelId}.json`;
    let queueData = await getR2Json(env.SYSTEM_BUCKET, queueKey);
    let updates = [];
    if (queueData) {
      if (Array.isArray(queueData)) {
        updates = queueData;
      } else {
        updates = [queueData];
      }
    }

    if (updates.length >= 10) {
      return new Response(JSON.stringify({ error: 'Update limit reached for this level (max 10)' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const uniqueId = Math.random().toString(36).substring(7);
    const timestamp = Date.now();
    const key = `updates/${levelId}_${timestamp}_${uniqueId}.${ext}`;
    await env.THUMBNAILS_BUCKET.put(key, buffer, {
      httpMetadata: { contentType: mime, cacheControl: NO_STORE_CACHE_CONTROL },
      customMetadata: {
        uploadedBy: username, uploadedAt: new Date().toISOString(),
        category: 'update', accountID: accountID.toString(),
        unauthenticated: accountID === 0 ? 'true' : 'false'
      }
    });

    updates.push({
      levelId: parseInt(levelId), category: 'update', submittedBy: username,
      timestamp,
      status: 'pending',
      note: 'Update proposal',
      accountID,
      unauthenticated: accountID === 0,
      filename: key
    });
    await putR2Json(env.SYSTEM_BUCKET, queueKey, updates);

    return new Response(JSON.stringify({ success: true, message: 'Update uploaded successfully' }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Update upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Download suggestion
export async function handleDownloadSuggestion(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[2];
  const levelId = filename.replace(/\.webp$/, '');

  try {
    let object = await env.THUMBNAILS_BUCKET.get(`suggestions/${filename}`);
    if (!object) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: `suggestions/${levelId}`, limit: 20 });
      if (list.objects.length > 0) {
        const sorted = list.objects.sort((a, b) => {
          const getTs = (k) => { const m = k.key.match(/_(\d+)_/); return m ? parseInt(m[1]) : 0; };
          return getTs(b) - getTs(a);
        });
        object = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      }
    }
    if (!object) object = await env.THUMBNAILS_BUCKET.get(`suggestions/${levelId}.webp`);

    if (!object) {
      return new Response(JSON.stringify({ error: 'Suggestion not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

// Download update
export async function handleDownloadUpdate(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[2];
  const levelId = filename.replace(/\.webp$/, '');

  try {
    let object = await env.THUMBNAILS_BUCKET.get(`updates/${filename}`);
    if (!object) {
      const list = await env.THUMBNAILS_BUCKET.list({ prefix: `updates/${levelId}`, limit: 20 });
      if (list.objects.length > 0) {
        const sorted = list.objects.sort((a, b) => {
          const getTs = (k) => { const m = k.key.match(/_(\d+)\./); return m ? parseInt(m[1]) : 0; };
          return getTs(b) - getTs(a);
        });
        object = await env.THUMBNAILS_BUCKET.get(sorted[0].key);
      }
    }
    if (!object) object = await env.THUMBNAILS_BUCKET.get(`updates/${levelId}.webp`);

    if (!object) {
      return new Response(JSON.stringify({ error: 'Update not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
    console.error('Download update error:', error);
    return new Response(JSON.stringify({ error: 'Download failed', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
