/**
 * Paimon Thumbnails - Cloudflare Worker
 * Slim entry point. Initialises Bunny buckets, dispatches to router.
 */
import { BunnyBucket } from './bunny-wrapper.js';
import { handleOptions } from './middleware/cors.js';
import { corsHeaders } from './middleware/cors.js';
import { routeRequest } from './router.js';

export default {
  async fetch(request, env, ctx) {
    // Initialise BunnyBucket adapters on every request (Workers are stateless per invocation)
    env.THUMBNAILS_BUCKET = new BunnyBucket(
      env.BUNNY_ACCESS_KEY,
      env.BUNNY_SECRET_KEY,
      env.BUNNY_ENDPOINT || 'https://storage.bunnycdn.com',
      env.BUNNY_ZONE_NAME || 'paimbnails',
      'thumbnails'
    );
    env.SYSTEM_BUCKET = new BunnyBucket(
      env.BUNNY_ACCESS_KEY,
      env.BUNNY_SECRET_KEY,
      env.BUNNY_ENDPOINT || 'https://storage.bunnycdn.com',
      env.BUNNY_ZONE_NAME || 'paimbnails',
      'system'
    );

    // CORS preflight
    if (request.method === 'OPTIONS') {
      return handleOptions(request);
    }

    try {
      return await routeRequest(request, env, ctx);
    } catch (error) {
      console.error('Request error:', error);
      return new Response(JSON.stringify({ error: 'Internal server error', details: error.message }), {
        status: 500,
        headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
  },

  async scheduled(event, env, ctx) {
    // Cloudflare Cron - configure triggers in wrangler.toml
  }
};
