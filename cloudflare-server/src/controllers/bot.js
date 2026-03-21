/**
 * Bot controllers — bot config, latest uploads, gallery
 */
import { corsHeaders } from '../middleware/cors.js';
import { verifyApiKey } from '../middleware/auth.js';
import { getR2Json, putR2Json } from '../services/storage.js';
import { memCache } from '../services/cache.js';

export async function handleGetBotConfig(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const key = 'data/bot/config.json';
  const data = await getR2Json(env.SYSTEM_BUCKET, key);

  return new Response(JSON.stringify(data || {}), {
    headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

export async function handleSetBotConfig(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleGetLatestUploads(request, env) {
  try {
    const memKey = 'latest_uploads';
    let data = memCache.get(memKey);
    if (data === undefined) {
      data = await getR2Json(env.SYSTEM_BUCKET, 'data/system/latest_uploads.json');
      memCache.set(memKey, data, 30_000); // 30 s
    }
    return new Response(JSON.stringify({ uploads: data || [] }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleGalleryList(request, env) {
  return new Response(JSON.stringify({
    error: 'Gallery is under maintenance',
    maintenance: true, thumbnails: [], total: 0
  }), {
    status: 503, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}
