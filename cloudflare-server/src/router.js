/**
 * Router — maps path + method to controller handlers.
 * Replaces the ~600-line if-else chain from the original index.js.
 */

// ── Controllers ──
import { handleUpload, handleUploadGIF, handleDownload, handleDirectThumbnail, handleExists, handleDeleteThumbnail, handleListThumbnails, handleGetThumbnailInfo } from './controllers/thumbnails.js';
import { handleUploadSuggestion, handleUploadUpdate, handleDownloadSuggestion, handleDownloadUpdate } from './controllers/suggestions.js';
import { handleUploadProfileConfig, handleGetProfileConfig, handleUploadBackground, handleUploadBackgroundGIF, handleServeBackground, handleServeProfile, handleServeProfileImg } from './controllers/profiles.js';
import { handleGetProfileMusic, handleUploadProfileMusic, handleDeleteProfileMusic, handleServeProfileMusic } from './controllers/music.js';
import { handleVote, handleGetRating, handleProfileRatingVote, handleGetProfileRating, handleVoteV2, handleGetRatingV2 } from './controllers/ratings.js';
import { handleSetDailyLevel, handleGetDailyLevel, handleSetWeeklyLevel, handleGetWeeklyLevel, handleGetDailyWeeklyHistory, handleGetLeaderboard, handleGetTopCreators, handleGetTopThumbnails } from './controllers/featured.js';
import { handleGetQueue, handleAcceptQueue, handleClaimQueue, handleRejectQueue } from './controllers/queue.js';
import { handleModeratorCheck, handleSetDaily, handleGetBanList, handleBanUser, handleUnbanUser, handleAddModerator, handleRemoveModerator, handleAddVip, handleRemoveVip, handleListModerators, handleGetWhitelist, handleAddWhitelist, handleRemoveWhitelist } from './controllers/admin.js';
import { handleSubmitReport, handleSubmitUserReport, handleFeedbackSubmit, handleFeedbackList, handleGetHistory } from './controllers/reports.js';
import { handlePetShopList, handlePetShopDownload, handlePetShopUpload } from './controllers/petshop.js';
import { handleGDLevelProxy, handleGDProfileProxy } from './controllers/proxy.js';
import { handleVersionCheck, handleModDownload } from './controllers/mod-system.js';
import { handleGetBotConfig, handleSetBotConfig, handleGetLatestUploads, handleGalleryList } from './controllers/bot.js';
import { handleBackfillContributors, handleMigrateLegacy } from './controllers/migration.js';

// ── Middleware / services ──
import { corsHeaders } from './middleware/cors.js';
import { verifyApiKey, verifyAdminFromRequest, ADMIN_USERS } from './middleware/auth.js';
import { getModerators } from './services/moderation.js';

// ── Static HTML pages ──
import { homeHtml } from './pages/home.js';
import { guidelinesHtml } from './pages/guidelines.js';
import { donateHtml } from './pages/donate.js';

/**
 * Main route dispatcher.
 * @param {Request} request
 * @param {object} env  — with THUMBNAILS_BUCKET, SYSTEM_BUCKET already initialised
 * @param {object} ctx
 * @returns {Promise<Response>}
 */
export async function routeRequest(request, env, ctx) {
  const url = new URL(request.url);
  const path = url.pathname;
  const method = request.method;

  // ── Assets ──
  if (path.startsWith('/assets/')) {
    const key = path.substring(1);
    const object = await env.SYSTEM_BUCKET.get(key);
    if (!object) return new Response('Not found', { status: 404 });
    return new Response(object.body, {
      headers: {
        'Content-Type': object.httpMetadata?.contentType || 'application/octet-stream',
        ...corsHeaders()
      }
    });
  }

  // ── Thumbnails ──
  if (path === '/mod/upload' && method === 'POST') return handleUpload(request, env, ctx);
  if (path === '/mod/upload-gif' && method === 'POST') return handleUploadGIF(request, env, ctx);
  if (path === '/api/thumbnails/list' && method === 'GET') return handleListThumbnails(request, env);
  if (path === '/api/thumbnails/info' && method === 'GET') return handleGetThumbnailInfo(request, env);
  if (path === '/api/profiles/upload' && method === 'POST') return handleUpload(request, env, ctx);
  if (path === '/api/profileimgs/upload' && method === 'POST') return handleUpload(request, env, ctx);
  if (path.startsWith('/api/download/')) return handleDownload(request, env, ctx);
  if (path.startsWith('/t/')) return handleDirectThumbnail(request, env, ctx);
  if (path === '/api/exists' && method === 'GET') return handleExists(request, env, ctx);
  if (path.startsWith('/api/thumbnails/delete/') && method === 'POST') return handleDeleteThumbnail(request, env, ctx);

  // ── Profiles ──
  if (path === '/api/profiles/config/upload' && method === 'POST') return handleUploadProfileConfig(request, env);
  if (path.startsWith('/api/profiles/config/') && method === 'GET') return handleGetProfileConfig(request, env);
  if (path === '/api/backgrounds/upload' && method === 'POST') return handleUploadBackground(request, env, ctx);
  if (path === '/api/backgrounds/upload-gif' && method === 'POST') return handleUploadBackgroundGIF(request, env, ctx);
  if (path.startsWith('/backgrounds/') && method === 'GET') return handleServeBackground(request, env);
  if (path.startsWith('/profilebackground/') && method === 'GET') return handleServeBackground(request, env);
  if (path.startsWith('/profiles/')) return handleServeProfile(request, env);
  if (path.startsWith('/profileimgs/')) return handleServeProfileImg(request, env);

  // Serve pending profilebackgrounds / backgrounds for moderator preview
  if ((path.startsWith('/pending_profilebackground/') || path.startsWith('/pending_backgrounds/')) && method === 'GET') {
    if (!verifyApiKey(request, env)) return new Response('Unauthorized', { status: 401, headers: corsHeaders() });
    const pendingFilename = decodeURIComponent(path.slice(1));
    if (pendingFilename.includes('..') || pendingFilename.includes('\\')) {
      return new Response('Invalid path', { status: 400, headers: corsHeaders() });
    }
    const obj = await env.THUMBNAILS_BUCKET.get(pendingFilename);
    if (!obj) return new Response('Not found', { status: 404, headers: corsHeaders() });
    const headers = new Headers();
    obj.writeHttpMetadata(headers);
    headers.set('Access-Control-Allow-Origin', '*');
    headers.set('Cache-Control', 'no-store, no-cache, must-revalidate');
    return new Response(obj.body, { headers });
  }

  // ── Whitelist ──
  if (path === '/api/whitelist' && method === 'GET') return handleGetWhitelist(request, env);
  if (path === '/api/whitelist/add' && method === 'POST') return handleAddWhitelist(request, env);
  if (path === '/api/whitelist/remove' && method === 'POST') return handleRemoveWhitelist(request, env);

  // ── Profile Music ──
  if (path === '/api/profile-music/upload' && method === 'POST') return handleUploadProfileMusic(request, env, ctx);
  if (path === '/api/profile-music/delete' && method === 'POST') return handleDeleteProfileMusic(request, env);
  if (path.startsWith('/api/profile-music/') && method === 'GET') return handleGetProfileMusic(request, env);
  if (path.startsWith('/profile-music/') && path.endsWith('.mp3') && method === 'GET') return handleServeProfileMusic(request, env);

  // ── Suggestions / Updates ──
  if (path === '/api/suggestions/upload' && method === 'POST') return handleUploadSuggestion(request, env);
  if (path === '/api/updates/upload' && method === 'POST') return handleUploadUpdate(request, env);
  if (path.startsWith('/suggestions/')) return handleDownloadSuggestion(request, env);
  if (path.startsWith('/updates/')) return handleDownloadUpdate(request, env);

  // ── Mod system ──
  if (path === '/api/mod/version' && method === 'GET') return handleVersionCheck(request);
  if (path === '/downloads/paimon.level_thumbnails.geode' && method === 'GET') return handleModDownload(request, env);

  // ── Featured (daily/weekly) ──
  if (path === '/api/daily/set' && method === 'POST') return handleSetDailyLevel(request, env);
  if (path === '/api/daily/current' && method === 'GET') return handleGetDailyLevel(request, env);
  if (path === '/api/weekly/set' && method === 'POST') return handleSetWeeklyLevel(request, env);
  if (path === '/api/weekly/current' && method === 'GET') return handleGetWeeklyLevel(request, env);
  if (path === '/api/leaderboard' && method === 'GET') return handleGetLeaderboard(request, env);
  if (path === '/api/featured/history' && method === 'GET') return handleGetDailyWeeklyHistory(request, env);
  if (path === '/api/top-creators' && method === 'GET') return handleGetTopCreators(request, env);
  if (path === '/api/top-thumbnails' && method === 'GET') return handleGetTopThumbnails(request, env);

  // ── Ratings ──
  if (path === '/api/profile-ratings/vote' && method === 'POST') return handleProfileRatingVote(request, env);
  if (path.startsWith('/api/profile-ratings/') && method === 'GET') return handleGetProfileRating(request, env);
  if (path === '/api/v2/ratings/vote' && method === 'POST') return handleVoteV2(request, env);
  if (path.startsWith('/api/v2/ratings/') && method === 'GET') return handleGetRatingV2(request, env);
  if (path === '/api/ratings/vote' && method === 'POST') return handleVote(request, env);
  if (path.startsWith('/api/ratings/') && method === 'GET') return handleGetRating(request, env);

  // ── Queue ──
  if (path.startsWith('/api/queue/accept/') && method === 'POST') return handleAcceptQueue(request, env, ctx);
  if (path.startsWith('/api/queue/claim/') && method === 'POST') return handleClaimQueue(request, env);
  if (path.startsWith('/api/queue/reject/') && method === 'POST') return handleRejectQueue(request, env);
  if (path.startsWith('/api/queue/') && method === 'GET') return handleGetQueue(request, env);

  // ── Reports / Feedback ──
  if (path === '/api/report/submit' && method === 'POST') return handleSubmitReport(request, env);
  if (path === '/api/report/user' && method === 'POST') return handleSubmitUserReport(request, env);
  if (path === '/api/feedback/submit' && method === 'POST') return handleFeedbackSubmit(request, env);
  if (path === '/api/feedback/list' && method === 'GET') return handleFeedbackList(request, env);
  if (path === '/api/history/uploads' && method === 'GET') return handleGetHistory(request, env, 'uploads');
  if (path === '/api/history/accepted' && method === 'GET') return handleGetHistory(request, env, 'accepted');
  if (path === '/api/history/rejected' && method === 'GET') return handleGetHistory(request, env, 'rejected');

  // ── Admin ──
  if (path === '/api/moderator/check' && method === 'GET') return handleModeratorCheck(request, env);
  if (path === '/api/admin/set-daily' && method === 'POST') return handleSetDaily(request, env);
  if (path === '/api/admin/banlist' && method === 'GET') return handleGetBanList(request, env);
  if (path === '/api/admin/ban' && method === 'POST') return handleBanUser(request, env);
  if (path === '/api/admin/unban' && method === 'POST') return handleUnbanUser(request, env);
  if (path === '/api/admin/add-moderator' && method === 'POST') return handleAddModerator(request, env);
  if (path === '/api/admin/remove-moderator' && method === 'POST') return handleRemoveModerator(request, env);
  if (path === '/api/admin/add-vip' && method === 'POST') return handleAddVip(request, env);
  if (path === '/api/admin/remove-vip' && method === 'POST') return handleRemoveVip(request, env);
  if (path === '/api/admin/moderators' && method === 'GET') return handleListModerators(request, env);
  if (path === '/api/admin/backfill-contributors' && method === 'POST') return handleBackfillContributors(request, env);
  if (path === '/api/admin/migrate-legacy' && method === 'POST') return handleMigrateLegacy(request, env);

  // ── Public moderators list (inline, no named handler) ──
  if (path === '/api/moderators' && method === 'GET') {
    const moderators = await getModerators(env.SYSTEM_BUCKET);
    let allMods = [...new Set([...ADMIN_USERS, ...moderators])];
    const hiddenUsers = ['viprin', 'robtop', 'kamisatonightor'];
    allMods = allMods.filter(user => !hiddenUsers.includes(user.toLowerCase()));
    const detailedMods = allMods.map((username) => ({
      username,
      role: ADMIN_USERS.includes(username.toLowerCase()) ? 'admin' : 'mod'
    }));
    return new Response(JSON.stringify({ moderators: detailedMods }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  // ── Bot ──
  if (path === '/api/bot/config' && method === 'GET') return handleGetBotConfig(request, env);
  if (path === '/api/bot/config' && method === 'POST') return handleSetBotConfig(request, env);
  if (path === '/api/latest-uploads' && method === 'GET') return handleGetLatestUploads(request, env);
  if (path === '/api/gallery/list' && method === 'GET') return handleGalleryList(request, env);

  // ── Pet Shop ──
  if (path === '/api/pet-shop/list' && method === 'GET') return handlePetShopList(request, env);
  if (path.startsWith('/api/pet-shop/download/') && method === 'GET') return handlePetShopDownload(request, env);
  if (path === '/api/pet-shop/upload' && method === 'POST') return handlePetShopUpload(request, env);

  // ── GD Proxy ──
  if (path.startsWith('/api/level/') && method === 'GET') return handleGDLevelProxy(request, env);
  if (path.startsWith('/api/gd/profile/') && method === 'GET') return handleGDProfileProxy(request, env);

  // ── Debug ──
  if (path === '/api/debug/bunny-raw' && method === 'GET') {
    if (!verifyApiKey(request, env)) return new Response('Unauthorized', { status: 401 });
    if (!(await verifyAdminFromRequest(request, env))) return new Response('Admin required', { status: 403 });
    const prefix = url.searchParams.get('prefix') || '';
    const bunnyUrl = `${env.THUMBNAILS_BUCKET.baseUrl}/${prefix}`;
    const res = await fetch(bunnyUrl, { method: 'GET', headers: { 'AccessKey': env.THUMBNAILS_BUCKET.apiKey } });
    return new Response(await res.text(), { status: res.status, headers: { 'Content-Type': 'application/json' } });
  }

  if (path === '/api/debug/r2-list' && method === 'GET') {
    if (!verifyApiKey(request, env)) return new Response('Unauthorized', { status: 401 });
    if (!(await verifyAdminFromRequest(request, env))) return new Response('Admin required', { status: 403 });
    try {
      const prefix = url.searchParams.get('prefix') || '';
      const listed = await env.THUMBNAILS_BUCKET.list({ limit: 100, prefix });
      return new Response(JSON.stringify({
        objects: listed.objects.map(obj => ({ key: obj.key, size: obj.size, uploaded: obj.uploaded }))
      }, null, 2), { status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() } });
    } catch (error) {
      return new Response(JSON.stringify({ error: error.message }), {
        status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
  }

  // ── Static Pages ──
  if (path === '/' || path === '/index.html') {
    return new Response(homeHtml, { status: 200, headers: { 'Content-Type': 'text/html; charset=utf-8', ...corsHeaders() } });
  }

  if (path === '/donate' || path === '/donate.html') {
    return new Response(donateHtml, { status: 200, headers: { 'Content-Type': 'text/html; charset=utf-8', ...corsHeaders() } });
  }

  if (path === '/download' || path === '/download.html') {
    const downloadHtml = await env.THUMBNAILS_BUCKET.get('public/download.html');
    if (downloadHtml) {
      return new Response(await downloadHtml.text(), { status: 200, headers: { 'Content-Type': 'text/html; charset=utf-8', ...corsHeaders() } });
    }
    return new Response('Download page not found', { status: 404 });
  }

  if (path === '/guidelines' || path === '/guidelines.html') {
    return new Response(guidelinesHtml, { status: 200, headers: { 'Content-Type': 'text/html; charset=utf-8', ...corsHeaders() } });
  }

  if (path === '/feedback-admin' || path === '/feedback-admin.html') {
    const adminHtml = await env.THUMBNAILS_BUCKET.get('public/feedback-admin.html');
    if (adminHtml) {
      return new Response(await adminHtml.text(), { status: 200, headers: { 'Content-Type': 'text/html; charset=utf-8', ...corsHeaders() } });
    }
    return new Response('Admin page not found', { status: 404 });
  }

  if (path === '/download/paimon.level_thumbnails.geode') {
    const geodeFile = await env.THUMBNAILS_BUCKET.get('public/paimon.level_thumbnails.geode');
    if (geodeFile) {
      return new Response(geodeFile.body, {
        status: 200,
        headers: { 'Content-Type': 'application/octet-stream', 'Content-Disposition': 'attachment; filename="paimon.level_thumbnails.geode"', ...corsHeaders() }
      });
    }
    return new Response('File not found', { status: 404 });
  }

  // ── Health ──
  if (path === '/health') {
    return new Response(JSON.stringify({ status: 'ok', timestamp: new Date().toISOString() }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  // ── Bunny migration ──
  if (path === '/api/admin/migrate-bunny') {
    if (!verifyApiKey(request, env)) return new Response('Unauthorized', { status: 401 });
    const cursor = url.searchParams.get('cursor');
    const target = url.searchParams.get('target');
    const BUNNY_ZONE = env.BUNNY_ZONE_NAME || 'paimbnails';
    const BUNNY_KEY = env.BUNNY_SECRET_KEY;

    let bucket, bunnyFolder;
    if (target === 'thumbnails') { bucket = env.THUMBNAILS_BUCKET; bunnyFolder = 'thumbnails'; }
    else if (target === 'system') { bucket = env.SYSTEM_BUCKET; bunnyFolder = 'system'; }
    else return new Response('Invalid target', { status: 400 });

    const list = await bucket.list({ limit: 10, cursor: cursor || undefined });
    const results = [];

    for (const obj of list.objects) {
      try {
        const r2Obj = await bucket.get(obj.key);
        if (r2Obj) {
          const cleanKey = obj.key.startsWith('/') ? obj.key.substring(1) : obj.key;
          const bunnyPath = `${bunnyFolder}/${cleanKey}`;
          const bunnyUrl = `https://storage.bunnycdn.com/${BUNNY_ZONE}/${bunnyPath}`;
          const uploadResp = await fetch(bunnyUrl, {
            method: 'PUT',
            headers: { 'AccessKey': BUNNY_KEY, 'Content-Type': r2Obj.httpMetadata?.contentType || 'application/octet-stream' },
            body: r2Obj.body
          });
          results.push({ key: obj.key, success: uploadResp.ok, status: uploadResp.status });
        }
      } catch (e) {
        results.push({ key: obj.key, error: e.message });
      }
    }

    return new Response(JSON.stringify({ cursor: list.truncated ? list.cursor : null, results, done: !list.truncated }), {
      headers: { 'Content-Type': 'application/json' }
    });
  }

  // ── 404 ──
  return new Response(JSON.stringify({ error: 'Not found' }), {
    status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}
