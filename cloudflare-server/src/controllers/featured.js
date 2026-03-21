/**
 * Featured controllers — daily/weekly levels, leaderboard, top creators/thumbnails, featured history
 */
import { corsHeaders } from '../middleware/cors.js';
import { verifyApiKey, verifyModAuth, ADMIN_USERS } from '../middleware/auth.js';
import { getR2Json, putR2Json, listR2Keys } from '../services/storage.js';
import { getWeekNumber } from '../services/leaderboard.js';
import { dispatchWebhook } from '../services/webhook.js';
import { memCache } from '../services/cache.js';

export async function handleSetDailyLevel(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const { levelID, username, accountID } = await request.json();
    if (!levelID || !username) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const authResult = await verifyModAuth(request, env, username, accountID || 0);
    if (!authResult.authorized) {
      return new Response(JSON.stringify({ error: 'Not authorized' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const now = Date.now();
    const expiresAt = now + (24 * 60 * 60 * 1000);
    const data = { levelID: parseInt(levelID), setAt: now, expiresAt, setBy: username };

    await putR2Json(env.SYSTEM_BUCKET, 'data/daily/current.json', data);
    dispatchWebhook(env, 'daily', data).catch(() => {});

    const historyKey = `data/daily/history/${new Date().toISOString().split('T')[0]}.json`;
    await putR2Json(env.SYSTEM_BUCKET, historyKey, data);

    return new Response(JSON.stringify({ success: true, data }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleGetDailyLevel(request, env) {
  try {
    const data = await getR2Json(env.SYSTEM_BUCKET, 'data/daily/current.json');
    if (!data) {
      return new Response(JSON.stringify({ error: 'No daily level set' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
    if (Date.now() > data.expiresAt) {
      return new Response(JSON.stringify({ error: 'Daily level expired', expired: true }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
    return new Response(JSON.stringify({ success: true, data }), {
      status: 200, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...corsHeaders() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleSetWeeklyLevel(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const { levelID, username, accountID } = await request.json();
    if (!levelID || !username) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const authResult = await verifyModAuth(request, env, username, accountID || 0);
    if (!authResult.authorized) {
      return new Response(JSON.stringify({ error: 'Not authorized' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const now = Date.now();
    const expiresAt = now + (7 * 24 * 60 * 60 * 1000);
    const data = { levelID: parseInt(levelID), setAt: now, expiresAt, setBy: username };

    await putR2Json(env.SYSTEM_BUCKET, 'data/weekly/current.json', data);
    dispatchWebhook(env, 'weekly', data).catch(() => {});

    const historyKey = `data/weekly/history/${getWeekNumber(new Date())}.json`;
    await putR2Json(env.SYSTEM_BUCKET, historyKey, data);

    return new Response(JSON.stringify({ success: true, data }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleGetWeeklyLevel(request, env) {
  try {
    const data = await getR2Json(env.SYSTEM_BUCKET, 'data/weekly/current.json');
    if (!data) {
      return new Response(JSON.stringify({ error: 'No weekly level set' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
    if (Date.now() > data.expiresAt) {
      return new Response(JSON.stringify({ error: 'Weekly level expired', expired: true }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
    return new Response(JSON.stringify({ success: true, data }), {
      status: 200, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...corsHeaders() }
    });
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleGetDailyWeeklyHistory(request, env) {
  try {
    const url = new URL(request.url);
    const type = url.searchParams.get('type') || 'daily';
    const limit = parseInt(url.searchParams.get('limit')) || 50;

    if (type !== 'daily' && type !== 'weekly') {
      return new Response(JSON.stringify({ error: 'Invalid type, must be daily or weekly' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const prefix = `data/${type}/history/`;
    const keys = await listR2Keys(env.SYSTEM_BUCKET, prefix);

    const items = [];
    const slice = keys.slice(-limit).reverse();
    const dataResults = await Promise.all(slice.map(key => getR2Json(env.SYSTEM_BUCKET, key)));
    for (const data of dataResults) {
      if (data) items.push(data);
    }

    items.sort((a, b) => (b.setAt || 0) - (a.setAt || 0));

    return new Response(JSON.stringify({ success: true, type, count: items.length, items }), {
      status: 200, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=1209600', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Get daily/weekly history error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get history', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleGetLeaderboard(request, env) {
  const url = new URL(request.url);
  const type = url.searchParams.get('type') || 'daily';
  const date = new Date();

  let key;
  if (type === 'daily') key = `data/leaderboards/daily/${date.toISOString().split('T')[0]}.json`;
  else if (type === 'weekly') key = `data/leaderboards/weekly/${getWeekNumber(date)}.json`;
  else return new Response(JSON.stringify({ error: 'Invalid type' }), { status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() } });

  const memKey = `leaderboard:${key}`;
  let data = memCache.get(memKey);
  if (data === undefined) {
    data = await getR2Json(env.SYSTEM_BUCKET, key) || [];
    memCache.set(memKey, data, 120_000); // 2 min
  }

  return new Response(JSON.stringify({ success: true, type, data }), {
    status: 200, headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...corsHeaders() }
  });
}

export async function handleGetTopCreators(request, env) {
  try {
    const url = new URL(request.url);
    const page = parseInt(url.searchParams.get('page') || '0');
    const limit = parseInt(url.searchParams.get('limit') || '20');

    const cacheKey = 'top_creators';
    let cache = memCache.get(cacheKey);
    if (cache === undefined) {
      cache = await getR2Json(env.SYSTEM_BUCKET, 'data/system/creator_leaderboard.json') || [];
      memCache.set(cacheKey, cache, 300_000); // 5 min
    }
    const start = page * limit;
    const slice = cache.slice(start, start + limit);

    return new Response(JSON.stringify({ success: true, total: cache.length, page, creators: slice }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response(JSON.stringify({ success: false, message: e.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }
}

export async function handleGetTopThumbnails(request, env) {
  try {
    const url = new URL(request.url);
    const page = parseInt(url.searchParams.get('page') || '0');
    const limit = parseInt(url.searchParams.get('limit') || '20');

    const cacheKey = 'top_thumbnails';
    let cache = memCache.get(cacheKey);
    if (cache === undefined) {
      cache = await getR2Json(env.SYSTEM_BUCKET, 'data/system/top_thumbnails.json') || [];
      memCache.set(cacheKey, cache, 300_000); // 5 min
    }
    const start = page * limit;
    const slice = cache.slice(start, start + limit);

    return new Response(JSON.stringify({ success: true, total: cache.length, page, thumbnails: slice }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response(JSON.stringify({ success: false, message: e.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }
}
