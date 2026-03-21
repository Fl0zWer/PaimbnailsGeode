/**
 * Authentication & authorization middleware
 */
import { corsHeaders } from './cors.js';
import { getR2Json, putR2Json } from '../services/storage.js';
import { getModerators } from '../services/moderation.js';

export const ADMIN_USERS = ['flozwer', 'gabriv4', 'alvaroeter'];

export function verifyApiKey(request, env) {
  const apiKey = request.headers.get('X-API-Key');
  return apiKey === env.API_KEY;
}

export async function verifyModAuth(request, env, username, accountID) {
  const sysBucket = env.SYSTEM_BUCKET;
  const rawModCode = request.headers.get('X-Mod-Code');
  const authKey = `data/auth/${username.toLowerCase()}.json`;

  if (!rawModCode) {
    console.log(`[Auth] No X-Mod-Code header for ${username}. Must generate in settings.`);
    return { authorized: false, needsModCode: true };
  }

  const modCode = rawModCode.trim();
  const storedData = await getR2Json(sysBucket, authKey);

  if (!storedData) {
    console.log(`[Auth] No stored auth data found for ${username} at ${authKey}. Code sent: ${modCode.substring(0, 8)}...`);
    return { authorized: false, invalidCode: true };
  }

  const storedCode = (storedData.code || '').trim();

  if (storedCode !== modCode) {
    console.log(`[Auth] Code mismatch for ${username}. Sent(len=${modCode.length}): "${modCode}" Stored(len=${storedCode.length}): "${storedCode}"`);
    return { authorized: false, invalidCode: true };
  }

  if (accountID > 0 && storedData.accountID && parseInt(storedData.accountID) !== parseInt(accountID)) {
    console.log(`[Auth] AccountID mismatch for ${username}: expected ${storedData.accountID}, got ${accountID}`);
    return { authorized: false };
  }

  console.log(`[Auth] Authorized ${username} with mod-code ${modCode.substring(0, 8)}...`);
  return { authorized: true };
}

export async function isModeratorOrAdmin(env, username) {
  const user = (username || '').toLowerCase().trim();
  if (!user) return false;
  if (ADMIN_USERS.includes(user)) return true;
  const moderators = await getModerators(env.SYSTEM_BUCKET);
  return moderators.includes(user);
}

export async function verifyModAuthFromBody(request, env, body) {
  const username = (body.username || body.adminUser || '').toString().trim();
  const accountID = parseInt(body.accountID || '0');
  if (!username) return { authorized: false };
  return await verifyModAuth(request, env, username, accountID);
}

export async function requireAdmin(request, env, body) {
  const auth = await verifyModAuthFromBody(request, env, body);
  if (!auth.authorized) {
    if (auth.needsModCode) return { authorized: false, error: 'Mod code required. Generate one in settings.' };
    if (auth.invalidCode) return { authorized: false, error: 'Invalid mod code. Refresh it in settings.' };
    return { authorized: false, error: 'Auth verification failed' };
  }
  const username = (body.username || body.adminUser || '').toString().trim().toLowerCase();
  if (!ADMIN_USERS.includes(username)) return { authorized: false, error: 'Admin privileges required' };
  return { authorized: true, newCode: auth.newCode };
}

export async function verifyAdminFromRequest(request, env) {
  const url = new URL(request.url);
  const username = (request.headers.get('X-Admin-User') || url.searchParams.get('username') || '').toLowerCase();
  if (!username || !ADMIN_USERS.includes(username)) return false;
  const accountID = parseInt(url.searchParams.get('accountID') || '0');
  const auth = await verifyModAuth(request, env, username, accountID);
  return auth.authorized;
}

export function forbiddenResponse(message) {
  return new Response(JSON.stringify({ error: message || 'Forbidden' }), {
    status: 403,
    headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

export function modAuthForbiddenResponse(auth) {
  if (auth.needsModCode) {
    return new Response(JSON.stringify({
      error: 'Mod code required',
      needsModCode: true,
      message: 'Generate a mod code in Paimbnails settings first'
    }), {
      status: 403,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
  if (auth.invalidCode) {
    return new Response(JSON.stringify({
      error: 'Invalid or expired mod code',
      invalidCode: true,
      message: 'Your mod code is invalid. Refresh it in Paimbnails settings.'
    }), {
      status: 403,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
  return forbiddenResponse('Moderator auth required');
}
