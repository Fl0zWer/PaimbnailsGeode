/**
 * Moderation helpers — moderators, VIPs, whitelist, audit log, bans
 */
import { getR2Json, putR2Json } from './storage.js';

export async function getModerators(bucket) {
  const data = await getR2Json(bucket, 'data/moderators.json');
  return data?.moderators || [];
}

export async function getVips(bucket) {
  const data = await getR2Json(bucket, 'data/vips.json');
  return data?.vips || [];
}

export async function addVip(bucket, username) {
  const vips = await getVips(bucket);
  if (!vips.includes(username.toLowerCase())) {
    vips.push(username.toLowerCase());
    await putR2Json(bucket, 'data/vips.json', { vips });
  }
  return true;
}

export async function removeVip(bucket, username) {
  const vips = await getVips(bucket);
  const filtered = vips.filter(v => v !== username.toLowerCase());
  await putR2Json(bucket, 'data/vips.json', { vips: filtered });
  return true;
}

export async function addModerator(bucket, username) {
  const moderators = await getModerators(bucket);
  if (!moderators.includes(username.toLowerCase())) {
    moderators.push(username.toLowerCase());
    await putR2Json(bucket, 'data/moderators.json', { moderators });
  }
  return true;
}

// ===== WHITELIST =====
export async function getWhitelist(bucket, type = 'profilebackground') {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`);
  return data?.users || [];
}

export async function addToWhitelist(bucket, username, addedBy, type = 'profilebackground') {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`) || { users: [], log: [] };
  const users = data.users || [];
  const log = data.log || [];
  const lower = username.toLowerCase();
  if (!users.includes(lower)) {
    users.push(lower);
    log.push({ action: 'add', username: lower, by: addedBy, at: new Date().toISOString() });
    await putR2Json(bucket, `data/whitelist/${type}.json`, { users, log });
  }
  return users;
}

export async function removeFromWhitelist(bucket, username, removedBy, type = 'profilebackground') {
  const data = await getR2Json(bucket, `data/whitelist/${type}.json`) || { users: [], log: [] };
  let users = data.users || [];
  const log = data.log || [];
  const lower = username.toLowerCase();
  const prev = users.length;
  users = users.filter(u => u !== lower);
  if (users.length < prev) {
    log.push({ action: 'remove', username: lower, by: removedBy, at: new Date().toISOString() });
    await putR2Json(bucket, `data/whitelist/${type}.json`, { users, log });
  }
  return users;
}

// ===== AUDIT =====
export async function logAudit(bucket, action, details, ctx) {
  const ts = Date.now();
  const key = `data/audit/${action}/${ts}-${Math.random().toString(36).slice(2, 8)}.json`;
  const entry = { action, timestamp: ts, date: new Date().toISOString(), ...details };
  if (ctx) ctx.waitUntil(putR2Json(bucket, key, entry));
  else await putR2Json(bucket, key, entry);
}
