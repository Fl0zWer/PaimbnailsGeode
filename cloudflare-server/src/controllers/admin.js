/**
 * Admin controllers — moderator management, bans, whitelist, set daily/weekly (admin version)
 */
import { corsHeaders } from '../middleware/cors.js';
import { verifyApiKey, verifyModAuth, requireAdmin, forbiddenResponse, modAuthForbiddenResponse, verifyModAuthFromBody, ADMIN_USERS } from '../middleware/auth.js';
import { getR2Json, putR2Json } from '../services/storage.js';
import { getModerators, getVips, getWhitelist, addToWhitelist, removeFromWhitelist, logAudit } from '../services/moderation.js';
import { dispatchWebhook } from '../services/webhook.js';

// Check moderator status
export async function handleModeratorCheck(request, env) {
  if (!verifyApiKey(request, env)) {
    const receivedKey = request.headers.get('X-API-Key') || '(none)';
    const expectedPrefix = (env.API_KEY || '').substring(0, 8);
    const receivedPrefix = receivedKey.substring(0, 8);
    console.error(`[ModCheck] API key mismatch. Received prefix: "${receivedPrefix}", Expected prefix: "${expectedPrefix}"`);
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const url = new URL(request.url);
  const username = url.searchParams.get('username');
  const accountID = parseInt(url.searchParams.get('accountID') || '0');

  if (!username) {
    return new Response(JSON.stringify({ error: 'Missing username' }), {
      status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const usernameLower = username.toLowerCase();
  const isAdmin = ADMIN_USERS.includes(usernameLower);

  let isModerator = isAdmin;
  let isVip = isAdmin;
  if (!isAdmin) {
    try {
      const moderators = await getModerators(env.SYSTEM_BUCKET);
      isModerator = moderators.includes(usernameLower);
      if (isModerator) isVip = true;
    } catch (e) { console.error('Error fetching moderators list:', e); }
  }

  if (!isVip) {
    try {
      const vips = await getVips(env.SYSTEM_BUCKET);
      isVip = vips.includes(usernameLower);
    } catch (e) { console.error('Error fetching VIP list:', e); }
  }

  console.log(`[ModCheck] username="${username}" lowercase="${usernameLower}" isAdmin=${isAdmin} isMod=${isModerator} isVip=${isVip} accountID=${accountID}`);

  let newModCode = undefined;
  let gdVerified = false;
  if ((isModerator || isAdmin) && accountID > 0) {
    try {
      const authKey = `data/auth/${usernameLower}.json`;
      const existingAuth = await getR2Json(env.SYSTEM_BUCKET, authKey);

      if (existingAuth && existingAuth.code && existingAuth.accountID == accountID) {
        const created = new Date(existingAuth.created || 0).getTime();
        const thirtyDays = 30 * 24 * 60 * 60 * 1000;
        if (Date.now() - created < thirtyDays) {
          newModCode = existingAuth.code;
          gdVerified = true;
          console.log(`[ModCheck] Reusing existing mod code for ${username} (prefix: ${existingAuth.code.substring(0, 8)}... age: ${Math.round((Date.now() - created) / 86400000)}d)`);
        } else {
          console.log(`[ModCheck] Existing mod code for ${username} expired (age: ${Math.round((Date.now() - created) / 86400000)}d). Regenerating.`);
        }
      } else if (existingAuth) {
        console.log(`[ModCheck] Existing auth for ${username} has accountID mismatch or missing code. stored=${existingAuth.accountID} vs request=${accountID}`);
      } else {
        console.log(`[ModCheck] No existing auth found for ${username} at ${authKey}`);
      }

      if (!newModCode) {
        let identityOk = false;
        try {
          const gdRes = await fetch(`https://gdbrowser.com/api/profile/${encodeURIComponent(username)}`);
          if (gdRes.ok) {
            const gdData = await gdRes.json();
            if (parseInt(gdData.accountID) === accountID) {
              identityOk = true;
              gdVerified = true;
              console.log(`[ModCheck] GDBrowser verified ${username} owns accountID ${accountID}`);
            } else {
              console.warn(`[ModCheck] GDBrowser accountID mismatch for ${username}: GD=${gdData.accountID} vs request=${accountID}`);
            }
          } else {
            console.error(`[ModCheck] GDBrowser request failed for ${username}: HTTP ${gdRes.status}`);
          }
        } catch (e) { console.error(`[ModCheck] GDBrowser error for ${username}:`, e); }

        if (identityOk) {
          newModCode = crypto.randomUUID();
          const writeOk = await putR2Json(env.SYSTEM_BUCKET, authKey, {
            code: newModCode, created: new Date().toISOString(), accountID
          });
          if (!writeOk) {
            console.error(`[ModCheck] CRITICAL: Failed to write mod code to storage for ${username}`);
            newModCode = undefined;
          } else {
            const verification = await getR2Json(env.SYSTEM_BUCKET, authKey);
            if (!verification || verification.code !== newModCode) {
              console.error(`[ModCheck] CRITICAL: Write verification failed for ${username}.`);
              newModCode = undefined;
            } else {
              console.log(`[ModCheck] Generated and verified new mod code for ${username}`);
            }
          }
        }
      }
    } catch (e) { console.error(`[ModCheck] Error generating mod code for ${username}:`, e); }
  }

  const responseData = { isModerator, isAdmin, isVip, accountID, accountRequiredForGlobalUploads: true };
  if (newModCode) {
    responseData.newModCode = newModCode;
  } else if ((isModerator || isAdmin) && !gdVerified) {
    responseData.gdVerificationFailed = true;
  }

  return new Response(JSON.stringify(responseData), {
    status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// Admin: set daily/weekly (legacy admin endpoint)
export async function handleSetDaily(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response('Unauthorized', { status: 401 });
  }

  try {
    const body = await request.json();
    const { levelID, type, username } = body;
    if (!levelID) return new Response('Missing levelID', { status: 400 });
    if (!username) return new Response('Missing username', { status: 400 });

    const admin = await requireAdmin(request, env, { username, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Set daily/weekly blocked: ${username} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const key = 'data/daily_weekly.json';
    let data = await getR2Json(env.SYSTEM_BUCKET, key) || { daily: null, weekly: null };

    if (type === 'weekly') data.weekly = levelID;
    else if (type === 'daily') data.daily = levelID;
    else if (type === 'unset') {
      if (data.daily == levelID) data.daily = null;
      if (data.weekly == levelID) data.weekly = null;
    }

    await putR2Json(env.SYSTEM_BUCKET, key, data);

    if (type === 'daily' || type === 'weekly') {
      dispatchWebhook(env, type, { levelID: parseInt(levelID), setBy: username, setAt: Date.now() }).catch(() => {});
    }

    return new Response(JSON.stringify({ success: true }), {
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (e) {
    return new Response('Error: ' + e.message, { status: 500 });
  }
}

// Ban list
export async function handleGetBanList(request, env) {
  if (!verifyApiKey(request, env)) return new Response('Unauthorized', { status: 401 });

  const url = new URL(request.url);
  const blUsername = url.searchParams.get('username') || '';
  const blAccountID = parseInt(url.searchParams.get('accountID') || '0');
  if (blUsername) {
    const auth = await verifyModAuth(request, env, blUsername, blAccountID);
    if (!auth.authorized) {
      console.log(`[Security] Get banlist blocked: ${blUsername}`);
      return modAuthForbiddenResponse(auth);
    }
  }

  try {
    const data = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json');
    return new Response(JSON.stringify({ banned: data?.banned || [], details: data?.details || {} }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleBanUser(request, env) {
  if (!verifyApiKey(request, env)) return new Response('Unauthorized', { status: 401 });

  try {
    const data = await request.json();
    const adminName = (data.admin || data.adminUser || '').toString().trim();
    const admin = await requireAdmin(request, env, { username: adminName, accountID: data.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Ban user blocked: ${adminName} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const username = (data?.username || '').toString().trim().toLowerCase();
    if (!username) {
      return new Response(JSON.stringify({ error: 'username required' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const isAdminTarget = ADMIN_USERS.includes(username);
    const isModTarget = moderators.includes(username) || isAdminTarget;
    if (isModTarget) {
      return new Response(JSON.stringify({ error: 'cannot ban moderator/admin' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json') || { banned: [], details: {} };
    const banned = Array.isArray(banData.banned) ? banData.banned : [];
    const details = banData.details || {};

    if (!banned.includes(username)) banned.push(username);
    details[username] = {
      reason: data.reason || 'No reason provided',
      bannedBy: data.admin || 'Unknown', timestamp: Date.now(), date: new Date().toISOString()
    };

    const ok = await putR2Json(env.SYSTEM_BUCKET, 'data/banlist.json', { banned, details });
    if (!ok) {
      return new Response(JSON.stringify({ error: 'failed to write banlist' }), {
        status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    return new Response(JSON.stringify({ success: true, banned }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleUnbanUser(request, env) {
  if (!verifyApiKey(request, env)) return new Response('Unauthorized', { status: 401 });

  try {
    const data = await request.json();
    const adminName = (data.admin || data.adminUser || '').toString().trim();
    const admin = await requireAdmin(request, env, { username: adminName, accountID: data.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Unban user blocked: ${adminName} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const username = (data?.username || '').toString().trim().toLowerCase();
    if (!username) {
      return new Response(JSON.stringify({ error: 'username required' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const banData = await getR2Json(env.SYSTEM_BUCKET, 'data/banlist.json') || { banned: [], details: {} };
    let banned = Array.isArray(banData.banned) ? banData.banned : [];
    let details = banData.details || {};
    banned = banned.filter(u => u !== username);
    if (details[username]) delete details[username];

    await putR2Json(env.SYSTEM_BUCKET, 'data/banlist.json', { banned, details });

    return new Response(JSON.stringify({ success: true }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (e) {
    return new Response(JSON.stringify({ error: e.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleAddModerator(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, adminUser } = body;
    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Add moderator blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const usernameLower = username.toLowerCase();
    try {
      const profileRes = await fetch(`https://gdbrowser.com/api/profile/${encodeURIComponent(username)}`);
      if (!profileRes.ok) {
        return new Response(JSON.stringify({ success: false, message: `User "${username}" not found on GDBrowser` }), {
          status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
    } catch (e) { console.warn('GDBrowser validation failed (allowing anyway):', e); }

    const moderators = await getModerators(env.SYSTEM_BUCKET);
    if (moderators.includes(usernameLower)) {
      return new Response(JSON.stringify({ success: false, message: 'User is already a moderator' }), {
        status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    moderators.push(usernameLower);
    await putR2Json(env.SYSTEM_BUCKET, 'data/moderators.json', { moderators });
    console.log(`[ModChange] ${adminUser} added ${usernameLower} as moderator`);

    return new Response(JSON.stringify({ success: true, message: `${username} added as moderator`, moderators }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Add moderator error:', error);
    return new Response(JSON.stringify({ error: 'Failed to add moderator', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleRemoveModerator(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, adminUser } = body;
    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Remove moderator blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    const newModerators = moderators.filter(mod => mod !== usernameLower);

    if (newModerators.length === moderators.length) {
      return new Response(JSON.stringify({ success: false, message: 'User was not a moderator' }), {
        status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    await putR2Json(env.SYSTEM_BUCKET, 'data/moderators.json', { moderators: newModerators });
    try { await env.SYSTEM_BUCKET.delete(`data/auth/${usernameLower}.json`); } catch (e) { console.warn(`Failed to delete auth code for ${usernameLower}:`, e); }
    console.log(`[ModChange] ${adminUser} removed ${usernameLower} from moderators`);

    return new Response(JSON.stringify({ success: true, message: `${username} removed from moderators`, moderators: newModerators }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Remove moderator error:', error);
    return new Response(JSON.stringify({ error: 'Failed to remove moderator', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleAddVip(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, adminUser } = body;
    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Add VIP blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const vips = await getVips(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    if (vips.includes(usernameLower)) {
      return new Response(JSON.stringify({ success: false, message: 'User is already a VIP' }), {
        status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    vips.push(usernameLower);
    await putR2Json(env.SYSTEM_BUCKET, 'data/vips.json', { vips });
    console.log(`[VipChange] ${adminUser} added ${usernameLower} as VIP`);

    return new Response(JSON.stringify({ success: true, message: `${username} added as VIP`, vips }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Add VIP error:', error);
    return new Response(JSON.stringify({ error: 'Failed to add VIP', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleRemoveVip(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, adminUser } = body;
    if (!username || !adminUser) {
      return new Response(JSON.stringify({ error: 'Missing required fields' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const admin = await requireAdmin(request, env, { username: adminUser, accountID: body.accountID });
    if (!admin.authorized) {
      console.log(`[Security] Remove VIP blocked: ${adminUser} - ${admin.error}`);
      return forbiddenResponse(admin.error);
    }

    const vips = await getVips(env.SYSTEM_BUCKET);
    const usernameLower = username.toLowerCase();
    const newVips = vips.filter(v => v !== usernameLower);

    if (newVips.length === vips.length) {
      return new Response(JSON.stringify({ success: false, message: 'User was not a VIP' }), {
        status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    await putR2Json(env.SYSTEM_BUCKET, 'data/vips.json', { vips: newVips });
    console.log(`[VipChange] ${adminUser} removed ${usernameLower} from VIPs`);

    return new Response(JSON.stringify({ success: true, message: `${username} removed from VIPs`, vips: newVips }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Remove VIP error:', error);
    return new Response(JSON.stringify({ error: 'Failed to remove VIP', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleListModerators(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const url = new URL(request.url);
  const adminUser = request.headers.get('X-Admin-User') || url.searchParams.get('username') || '';
  const adminAccountID = parseInt(url.searchParams.get('accountID') || '0');

  if (!adminUser || !ADMIN_USERS.includes(adminUser.toLowerCase())) {
    return new Response(JSON.stringify({ error: 'Admin privileges required' }), {
      status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const admin = await requireAdmin(request, env, { username: adminUser, accountID: adminAccountID });
  if (!admin.authorized) return forbiddenResponse(admin.error);

  const moderators = await getModerators(env.SYSTEM_BUCKET);

  return new Response(JSON.stringify({ success: true, count: moderators.length, moderators }), {
    status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

// Whitelist endpoints
export async function handleGetWhitelist(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  const url = new URL(request.url);
  const wlUsername = url.searchParams.get('username') || '';
  const wlAccountID = parseInt(url.searchParams.get('accountID') || '0');
  const auth = await verifyModAuth(request, env, wlUsername, wlAccountID);
  if (!auth.authorized) return modAuthForbiddenResponse(auth);

  const type = url.searchParams.get('type') || 'profilebackground';
  const users = await getWhitelist(env.SYSTEM_BUCKET, type);

  return new Response(JSON.stringify({ success: true, type, users }), {
    status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
  });
}

export async function handleAddWhitelist(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, type } = body;
    const wlType = type || 'profilebackground';

    if (!username) {
      return new Response(JSON.stringify({ error: 'Missing username' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) return modAuthForbiddenResponse(auth);

    const adminUser = (body.adminUser || body.moderator || '').toString().trim();
    const users = await addToWhitelist(env.SYSTEM_BUCKET, username, adminUser, wlType);

    await logAudit(env.SYSTEM_BUCKET, 'whitelist_add', {
      target: username.toLowerCase(), addedBy: adminUser, type: wlType
    });

    return new Response(JSON.stringify({ success: true, users }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Add whitelist error:', error);
    return new Response(JSON.stringify({ error: 'Failed to add to whitelist', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleRemoveWhitelist(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const body = await request.json();
    const { username, type } = body;
    const wlType = type || 'profilebackground';

    if (!username) {
      return new Response(JSON.stringify({ error: 'Missing username' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const auth = await verifyModAuthFromBody(request, env, body);
    if (!auth.authorized) return modAuthForbiddenResponse(auth);

    const adminUser = (body.adminUser || body.moderator || '').toString().trim();
    const users = await removeFromWhitelist(env.SYSTEM_BUCKET, username, adminUser, wlType);

    await logAudit(env.SYSTEM_BUCKET, 'whitelist_remove', {
      target: username.toLowerCase(), removedBy: adminUser, type: wlType
    });

    return new Response(JSON.stringify({ success: true, users }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Remove whitelist error:', error);
    return new Response(JSON.stringify({ error: 'Failed to remove from whitelist', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
