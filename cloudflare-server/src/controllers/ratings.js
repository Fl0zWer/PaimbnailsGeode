/**
 * Ratings controllers — V1, V2, profile ratings
 */
import { corsHeaders } from '../middleware/cors.js';
import { verifyApiKey } from '../middleware/auth.js';
import { getR2Json, putR2Json, listR2Keys } from '../services/storage.js';
import { censorText } from '../services/profanity.js';
import { VersionManager } from '../services/versions.js';
import { getWeekNumber, updateLeaderboard, updateTopThumbnailsCache, updateCreatorLeaderboardCache } from '../services/leaderboard.js';

/**
 * Resolve the original uploader for a level by checking accepted history
 * and queue entries. Fetches accepted keys in parallel for efficiency.
 */
async function resolveUploadedBy(env, levelID) {
  const acceptedKeys = await listR2Keys(env.SYSTEM_BUCKET, `data/history/accepted/${levelID}`);

  if (acceptedKeys.length > 0) {
    const results = await Promise.all(acceptedKeys.map(k => getR2Json(env.SYSTEM_BUCKET, k)));
    for (const aData of results) {
      if (aData) {
        const uploader = aData.originalSubmission?.submittedBy || aData.submittedBy || '';
        const accID = aData.originalSubmission?.accountID || aData.accountID || 0;
        if (uploader && uploader !== 'Unknown') {
          return { uploadedBy: uploader, accountID: accID ? parseInt(accID) : 0 };
        }
      }
    }
  }

  // Fallback: check queue entries in parallel
  const [suggestionsData, updatesData] = await Promise.all([
    getR2Json(env.SYSTEM_BUCKET, `data/queue/suggestions/${levelID}.json`),
    getR2Json(env.SYSTEM_BUCKET, `data/queue/updates/${levelID}.json`),
  ]);

  for (const qData of [suggestionsData, updatesData]) {
    if (qData) {
      const items = Array.isArray(qData) ? qData : [qData];
      for (const item of items) {
        const uploader = item.submittedBy || item.uploadedBy || '';
        const accID = item.accountID || 0;
        if (uploader && uploader !== 'Unknown' && uploader !== 'System') {
          return { uploadedBy: uploader, accountID: accID ? parseInt(accID) : 0 };
        }
      }
    }
  }

  return null;
}

export async function handleVote(request, env) {
  if (request.method !== 'POST') return new Response('Method not allowed', { status: 405 });
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }

  try {
    const { levelID, stars, username, thumbnailId } = await request.json();
    if (!levelID || !stars || !username) return new Response('Missing fields', { status: 400 });
    if (stars < 1 || stars > 5) return new Response('Invalid stars', { status: 400 });

    let key = `ratings/${levelID}.json`;
    if (thumbnailId) key = `ratings/${levelID}_${thumbnailId}.json`;
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };

    const userLower = String(username).toLowerCase();
    if (data.votes[userLower]) {
      return new Response(JSON.stringify({ success: false, message: 'Already voted' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }

    if (!data.uploadedBy) {
      try {
        const result = await resolveUploadedBy(env, levelID);
        if (result) {
          data.uploadedBy = result.uploadedBy;
          if (result.accountID) data.accountID = result.accountID;
        }
      } catch (e) { console.warn('Failed to fetch uploadedBy', e); }
    }

    data.votes[userLower] = stars;
    data.total += stars;
    data.count += 1;

    const currentAverage = data.total / data.count;
    if (currentAverage <= 3) {
      const queueKey = `data/queue/updates/${levelID}.json`;
      await putR2Json(env.SYSTEM_BUCKET, queueKey, {
        levelId: parseInt(levelID), category: 'verify', submittedBy: 'System',
        timestamp: Date.now(), status: 'pending',
        note: `Low rating detected: ${currentAverage.toFixed(2)} stars`, average: currentAverage
      });
    }

    const today = new Date().toISOString().split('T')[0];
    if (!data.daily || data.daily.date !== today) data.daily = { date: today, total: 0, count: 0 };
    data.daily.total += stars;
    data.daily.count += 1;

    const thisWeek = getWeekNumber(new Date());
    if (!data.weekly || data.weekly.week !== thisWeek) data.weekly = { week: thisWeek, total: 0, count: 0 };
    data.weekly.total += stars;
    data.weekly.count += 1;

    await putR2Json(env.SYSTEM_BUCKET, key, data);

    const uploaderAccountID = data.accountID || 0;
    await Promise.all([
      updateLeaderboard(env, 'daily', levelID, data.daily, data.uploadedBy, uploaderAccountID),
      updateLeaderboard(env, 'weekly', levelID, data.weekly, data.uploadedBy, uploaderAccountID)
    ]);

    const cachePromises = [
      updateTopThumbnailsCache(env, levelID, { total: data.total, count: data.count }, data.uploadedBy, data.accountID),
    ];
    if (data.uploadedBy && data.uploadedBy !== 'Unknown') {
      cachePromises.push(updateCreatorLeaderboardCache(env, data.uploadedBy, { addRating: stars, accountID: data.accountID }));
    }
    await Promise.all(cachePromises);

    return new Response(JSON.stringify({ success: true, average: data.total / data.count }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response('Error processing vote: ' + e.message, { status: 500 });
  }
}

export async function handleGetRating(request, env) {
  const url = new URL(request.url);
  const levelID = url.pathname.split('/').pop();
  const username = url.searchParams.get('username');
  const thumbnailId = url.searchParams.get('thumbnailId');

  const vm = new VersionManager(env.SYSTEM_BUCKET);
  let primaryId = null;
  try {
    const version = await vm.getVersion(levelID);
    if (version) primaryId = version.id || version.version || null;
  } catch (e) { console.warn('handleGetRating: failed to read version map', e); }

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
      'Content-Type': 'application/json', 'Cache-Control': 'no-store',
      ...corsHeaders(request.headers.get('Origin'))
    }
  });
}

export async function handleProfileRatingVote(request, env) {
  if (request.method !== 'POST') return new Response('Method not allowed', { status: 405 });
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }

  try {
    const { accountID, stars, username, message } = await request.json();
    if (!accountID || !stars || !username) {
      return new Response(JSON.stringify({ error: 'Missing fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }
    if (stars < 1 || stars > 5) {
      return new Response(JSON.stringify({ error: 'Stars must be 1-5' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }

    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    const banned = Array.isArray(banData?.banned) ? banData.banned : [];
    if (banned.includes(username.toLowerCase())) {
      return new Response(JSON.stringify({ error: 'User is banned' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }

    const key = `profile-ratings/${accountID.toString()}.json`;
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };

    const userLower = username.toLowerCase();
    const previousVote = data.votes?.[userLower];
    if (previousVote) {
      data.total = (data.total || 0) - previousVote.stars + stars;
    } else {
      data.total = (data.total || 0) + stars;
      data.count = (data.count || 0) + 1;
    }

    data.votes = data.votes || {};
    data.votes[userLower] = { stars, message: censorText((message || '').substring(0, 150)), timestamp: Date.now() };

    await putR2Json(env.SYSTEM_BUCKET, key, data);

    const average = data.count > 0 ? data.total / data.count : 0;

    return new Response(JSON.stringify({
      success: true, average: Math.round(average * 100) / 100,
      count: data.count, updated: !!previousVote
    }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: 'Error processing vote: ' + e.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }
}

export async function handleGetProfileRating(request, env) {
  const url = new URL(request.url);
  const accountID = url.pathname.split('/').pop();
  const username = url.searchParams.get('username');

  const key = `profile-ratings/${accountID}.json`;
  const data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };

  const average = data.count > 0 ? data.total / data.count : 0;
  const userVote = username && data.votes?.[username.toLowerCase()] ? data.votes[username.toLowerCase()] : null;

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
    average: Math.round(average * 100) / 100, count: data.count,
    userVote: userVote ? { stars: userVote.stars, message: userVote.message || '' } : null,
    reviews: reviews.slice(0, 20)
  }), {
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...corsHeaders(request.headers.get('Origin')) }
  });
}

export async function handleVoteV2(request, env) {
  if (request.method !== 'POST') return new Response('Method not allowed', { status: 405 });
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  }

  try {
    const { levelID, stars, username, thumbnailId } = await request.json();
    if (!levelID || !stars || !username) return new Response('Missing fields', { status: 400 });
    if (stars < 1 || stars > 5) return new Response('Invalid stars', { status: 400 });

    const levelStr = levelID.toString();
    let key = `ratings-v2/${levelStr}.json`;
    if (thumbnailId) key = `ratings-v2/${levelStr}_${thumbnailId}.json`;

    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { total: 0, count: 0, votes: {} };
    const userLower = String(username).toLowerCase();

    if (data.votes && data.votes[userLower]) {
      return new Response(JSON.stringify({ success: false, message: 'Already voted' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
      });
    }

    if (!data.uploadedBy) {
      try {
        const result = await resolveUploadedBy(env, levelStr);
        if (result) {
          data.uploadedBy = result.uploadedBy;
          if (result.accountID) data.accountID = result.accountID;
        }
      } catch (e) { console.warn('Failed to fetch uploadedBy (v2)', e); }
    }

    data.votes = data.votes || {};
    data.votes[userLower] = stars;
    data.total = (data.total || 0) + stars;
    data.count = (data.count || 0) + 1;

    const currentAverage = data.total / data.count;
    if (currentAverage <= 3) {
      await putR2Json(env.SYSTEM_BUCKET, `data/queue/updates/${levelStr}.json`, {
        levelId: parseInt(levelStr), category: 'verify', submittedBy: 'System',
        timestamp: Date.now(), status: 'pending',
        note: `Low rating detected: ${currentAverage.toFixed(2)} stars`, average: currentAverage
      });
    }

    const today = new Date().toISOString().split('T')[0];
    if (!data.daily || data.daily.date !== today) data.daily = { date: today, total: 0, count: 0 };
    data.daily.total += stars;
    data.daily.count += 1;

    const thisWeek = getWeekNumber(new Date());
    if (!data.weekly || data.weekly.week !== thisWeek) data.weekly = { week: thisWeek, total: 0, count: 0 };
    data.weekly.total += stars;
    data.weekly.count += 1;

    await putR2Json(env.SYSTEM_BUCKET, key, data);

    const uploaderAccountID = data.accountID || 0;
    await Promise.all([
      updateLeaderboard(env, 'daily', levelStr, data.daily, data.uploadedBy, uploaderAccountID),
      updateLeaderboard(env, 'weekly', levelStr, data.weekly, data.uploadedBy, uploaderAccountID)
    ]);

    const v2CachePromises = [
      updateTopThumbnailsCache(env, levelStr, { total: data.total, count: data.count }, data.uploadedBy, data.accountID),
    ];
    if (data.uploadedBy && data.uploadedBy !== 'Unknown') {
      v2CachePromises.push(updateCreatorLeaderboardCache(env, data.uploadedBy, { addRating: stars, accountID: data.accountID }));
    }
    await Promise.all(v2CachePromises);

    return new Response(JSON.stringify({ success: true, average: data.total / data.count, count: data.count }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders(request.headers.get('Origin')) }
    });
  } catch (e) {
    return new Response('Error processing vote: ' + e.message, { status: 500 });
  }
}

export async function handleGetRatingV2(request, env) {
  const url = new URL(request.url);
  const levelID = url.pathname.split('/').pop();
  const username = url.searchParams.get('username');
  const thumbnailId = url.searchParams.get('thumbnailId');

  const candidates = [];
  if (thumbnailId) candidates.push(`ratings-v2/${levelID}_${thumbnailId}.json`);
  candidates.push(`ratings-v2/${levelID}.json`);
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
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...corsHeaders(request.headers.get('Origin')) }
  });
}
