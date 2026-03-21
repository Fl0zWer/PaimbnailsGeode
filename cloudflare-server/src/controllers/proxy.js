/**
 * GD Proxy controllers — level and profile proxy via GDBrowser
 */
import { corsHeaders } from '../middleware/cors.js';

export async function handleGDLevelProxy(request, env) {
  const url = new URL(request.url);
  const id = url.pathname.replace('/api/level/', '').split('/')[0].trim();

  if (!id || !/^\d+$/.test(id)) {
    return new Response(JSON.stringify({ error: 'Invalid level ID' }), {
      status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const cacheKey = `cache/level/${id}.json`;
  try {
    const cached = await env.SYSTEM_BUCKET.get(cacheKey);
    if (cached) {
      const meta = cached.customMetadata || {};
      const cachedAt = parseInt(meta.cachedAt || '0');
      if (Date.now() - cachedAt < 3600_000) {
        const text = await cached.text();
        return new Response(text, {
          status: 200, headers: { 'Content-Type': 'application/json', 'X-Cache': 'HIT', ...corsHeaders() }
        });
      }
    }
  } catch (_) { }

  try {
    const gdRes = await fetch(`https://gdbrowser.com/api/level/${id}`, {
      headers: { 'User-Agent': 'PaimonThumbnails/1.0' },
      cf: { cacheTtl: 3600 }
    });

    if (!gdRes.ok) {
      return new Response(JSON.stringify({ error: 'Level not found', status: gdRes.status }), {
        status: gdRes.status, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const data = await gdRes.json();
    if (!data || typeof data !== 'object' || !data.name) {
      return new Response(JSON.stringify({ error: 'Level not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const jsonText = JSON.stringify(data);
    try {
      await env.SYSTEM_BUCKET.put(cacheKey, jsonText, {
        httpMetadata: { contentType: 'application/json' },
        customMetadata: { cachedAt: String(Date.now()) }
      });
    } catch (_) { }

    return new Response(jsonText, {
      status: 200, headers: { 'Content-Type': 'application/json', 'X-Cache': 'MISS', ...corsHeaders() }
    });
  } catch (e) {
    console.error('[GDProxy] Error fetching level', id, e);
    return new Response(JSON.stringify({ error: 'Proxy error', detail: e.message }), {
      status: 502, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleGDProfileProxy(request, env) {
  const url = new URL(request.url);
  const username = url.pathname.replace('/api/gd/profile/', '').split('/')[0].trim();

  if (!username) {
    return new Response(JSON.stringify({ error: 'Invalid username' }), {
      status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
          status: 200, headers: { 'Content-Type': 'application/json', 'X-Cache': 'HIT', ...corsHeaders() }
        });
      }
    }
  } catch (_) { }

  try {
    const gdRes = await fetch(`https://gdbrowser.com/api/profile/${username}`, {
      headers: { 'User-Agent': 'PaimonThumbnails/1.0' },
      cf: { cacheTtl: 3600 }
    });

    if (!gdRes.ok) {
      return new Response(JSON.stringify({ error: 'Profile not found' }), {
        status: gdRes.status, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
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
      status: 200, headers: { 'Content-Type': 'application/json', 'X-Cache': 'MISS', ...corsHeaders() }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: 'Proxy error', detail: e.message }), {
      status: 502, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
