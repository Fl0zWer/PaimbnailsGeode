/**
 * Leaderboard & cache update helpers
 */
import { getR2Json, putR2Json } from './storage.js';
import { memCache } from './cache.js';

export function getWeekNumber(d) {
  d = new Date(Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()));
  d.setUTCDate(d.getUTCDate() + 4 - (d.getUTCDay() || 7));
  var yearStart = new Date(Date.UTC(d.getUTCFullYear(), 0, 1));
  var weekNo = Math.ceil((((d - yearStart) / 86400000) + 1) / 7);
  return `${d.getUTCFullYear()}-W${weekNo}`;
}

export async function updateLeaderboard(env, type, levelId, stats, uploadedBy, accountID) {
  const date = new Date();
  let key;
  if (type === 'daily') key = `data/leaderboards/daily/${date.toISOString().split('T')[0]}.json`;
  else if (type === 'weekly') key = `data/leaderboards/weekly/${getWeekNumber(date)}.json`;
  else return;

  try {
    let leaderboard = await getR2Json(env.SYSTEM_BUCKET, key) || [];

    leaderboard = leaderboard.filter(item => item.levelId !== parseInt(levelId));

    const average = stats.count > 0 ? stats.total / stats.count : 0;

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

    leaderboard.sort((a, b) => {
      if (b.rating !== a.rating) return b.rating - a.rating;
      return b.count - a.count;
    });

    if (leaderboard.length > 100) leaderboard = leaderboard.slice(0, 100);

    await putR2Json(env.SYSTEM_BUCKET, key, leaderboard);
    memCache.invalidate(`leaderboard:${key}`);
  } catch (e) {
    console.error(`Failed to update ${type} leaderboard:`, e);
  }
}

export async function updateTopThumbnailsCache(env, levelId, stats, uploadedBy, accountID) {
  try {
    let cache = await getR2Json(env.SYSTEM_BUCKET, 'data/system/top_thumbnails.json') || [];
    const lid = parseInt(levelId);
    const average = stats.count > 0 ? stats.total / stats.count : 0;

    cache = cache.filter(item => item.levelId !== lid);

    if (stats.count >= 3) {
      cache.push({
        levelId: lid,
        rating: Math.round(average * 100) / 100,
        count: stats.count,
        uploadedBy: uploadedBy || 'Unknown',
        accountID: accountID ? parseInt(accountID) : 0
      });
    }

    cache.sort((a, b) => {
      if (b.rating !== a.rating) return b.rating - a.rating;
      return b.count - a.count;
    });

    if (cache.length > 100) cache = cache.slice(0, 100);

    await putR2Json(env.SYSTEM_BUCKET, 'data/system/top_thumbnails.json', cache);
    memCache.invalidate('top_thumbnails');
  } catch (e) {
    console.error('Failed to update top thumbnails cache:', e);
  }
}

export async function updateCreatorLeaderboardCache(env, username, opts = {}) {
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

    cache.sort((a, b) => {
      if (b.uploadCount !== a.uploadCount) return b.uploadCount - a.uploadCount;
      return (b.avgRating || 0) - (a.avgRating || 0);
    });

    if (cache.length > 100) cache = cache.slice(0, 100);

    await putR2Json(env.SYSTEM_BUCKET, 'data/system/creator_leaderboard.json', cache);
    memCache.invalidate('top_creators');
  } catch (e) {
    console.error('Failed to update creator leaderboard cache:', e);
  }
}
