/**
 * Discord bot webhook dispatch
 */

export async function dispatchWebhook(env, eventType, payload) {
  const url = env.DISCORD_BOT_WEBHOOK_URL;
  const secret = env.DISCORD_BOT_WEBHOOK_SECRET;
  if (!url || !secret) return;

  const body = JSON.stringify({ event: eventType, data: payload });

  for (let attempt = 0; attempt < 2; attempt++) {
    try {
      const resp = await fetch(url, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${secret}`,
        },
        body,
      });
      if (resp.ok) {
        console.log(`[Webhook] Dispatched '${eventType}' event successfully`);
        return;
      }
      console.warn(`[Webhook] '${eventType}' attempt ${attempt + 1} got status ${resp.status}`);
      if (resp.status < 500) return;
    } catch (err) {
      console.error(`[Webhook] '${eventType}' attempt ${attempt + 1} failed:`, err.message);
    }
    if (attempt === 0) await new Promise(r => setTimeout(r, 2000));
  }
}
