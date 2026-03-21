/**
 * Music controllers — profile music get/upload/delete/serve
 */
import { corsHeaders, NO_STORE_CACHE_CONTROL } from '../middleware/cors.js';
import { verifyApiKey } from '../middleware/auth.js';
import { getR2Json, putR2Json } from '../services/storage.js';
import { cfCacheMatch, cfCachePut, cfCacheDelete, cfCacheKey, makeCacheable } from '../services/cache.js';

export async function handleGetProfileMusic(request, env) {
  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const accountID = pathParts[pathParts.length - 1];

  if (!accountID || accountID === 'undefined') {
    return new Response(JSON.stringify({ error: 'Account ID required' }), {
      status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const configKey = `profile-music/${accountID}.json`;
    const config = await getR2Json(env.SYSTEM_BUCKET, configKey);

    if (!config) {
      return new Response(JSON.stringify({ error: 'No music configured' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    return new Response(JSON.stringify(config), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('Get profile music error:', error);
    return new Response(JSON.stringify({ error: 'Failed to get music config' }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleUploadProfileMusic(request, env, ctx) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const data = await request.json();
    const { accountID, username, songID, startMs, endMs, volume, songName, artistName } = data;

    if (!accountID || !songID) {
      return new Response(JSON.stringify({ error: 'Account ID and Song ID required' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const duration = endMs - startMs;
    if (duration > 20000) {
      return new Response(JSON.stringify({ error: 'Fragment cannot exceed 20 seconds' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }
    if (duration < 5000) {
      return new Response(JSON.stringify({ error: 'Fragment must be at least 5 seconds' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    let audioBuffer;

    if (data.audioData) {
      console.log(`[ProfileMusic] Receiving audio from client (base64 length: ${data.audioData.length})`);

      // Limit audio upload to 10 MB decoded
      const maxAudioSize = 10 * 1024 * 1024;
      if (data.audioData.length > maxAudioSize * 1.37) {
        return new Response(JSON.stringify({ error: 'Audio file too large (max 10 MB)' }), {
          status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }

      try {
        const binaryString = atob(data.audioData);
        audioBuffer = new Uint8Array(binaryString.length);
        for (let i = 0; i < binaryString.length; i++) {
          audioBuffer[i] = binaryString.charCodeAt(i);
        }

        console.log(`[ProfileMusic] Decoded audio: ${audioBuffer.length} bytes`);

        if (audioBuffer.length < 100) {
          return new Response(JSON.stringify({ error: 'Audio file too small' }), {
            status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
          });
        }

        const isMp3 = (audioBuffer[0] === 0xFF && (audioBuffer[1] & 0xE0) === 0xE0) ||
          (audioBuffer[0] === 0x49 && audioBuffer[1] === 0x44 && audioBuffer[2] === 0x33);
        const isWav = audioBuffer[0] === 0x52 && audioBuffer[1] === 0x49 &&
          audioBuffer[2] === 0x46 && audioBuffer[3] === 0x46;

        const extension = isWav ? 'wav' : 'mp3';
        const contentType = isWav ? 'audio/wav' : 'audio/mpeg';

        console.log(`[ProfileMusic] Detected format: ${extension}, size: ${audioBuffer.length} bytes`);

        const audioKey = `profile-music/${accountID}.mp3`;
        await env.THUMBNAILS_BUCKET.put(audioKey, audioBuffer, {
          httpMetadata: { contentType, cacheControl: NO_STORE_CACHE_CONTROL },
          customMetadata: {
            songID: songID.toString(), startMs: startMs.toString(), endMs: endMs.toString(),
            uploadedBy: username || 'unknown', uploadedAt: new Date().toISOString(), format: extension
          }
        });

        const config = {
          accountID, username, songID, startMs, endMs,
          volume: volume || 1.0, enabled: true,
          songName: songName || '', artistName: artistName || '',
          format: extension, updatedAt: new Date().toISOString()
        };

        const configKey = `profile-music/${accountID}.json`;
        await putR2Json(env.SYSTEM_BUCKET, configKey, config);

        console.log(`[ProfileMusic] Uploaded ${extension} for account ${accountID}: ${audioBuffer.length} bytes`);

        // Invalidate CF Cache for /profile-music/{accountID}.mp3
        const origin = new URL(request.url).origin;
        cfCacheDelete(`${origin}/profile-music/${accountID}.mp3`).catch(() => {});

        return new Response(JSON.stringify({ success: true, message: 'Profile music uploaded successfully', config }), {
          status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });

      } catch (e) {
        console.error(`[ProfileMusic] Failed to process audio: ${e.message}`);
        return new Response(JSON.stringify({ error: 'Invalid audio data', details: e.message }), {
          status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
        });
      }
    }

    return new Response(JSON.stringify({
      error: 'Audio data required',
      details: 'Please download the song in GD first using the Download button'
    }), {
      status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Upload profile music error:', error);
    return new Response(JSON.stringify({ error: 'Failed to upload music', details: error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleDeleteProfileMusic(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const data = await request.json();
    const { accountID } = data;

    if (!accountID) {
      return new Response(JSON.stringify({ error: 'Account ID required' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    try { await env.THUMBNAILS_BUCKET.delete(`profile-music/${accountID}.mp3`); } catch (e) { }
    try { await env.THUMBNAILS_BUCKET.delete(`profile-music/${accountID}.wav`); } catch (e) { }

    const configKey = `profile-music/${accountID}.json`;
    await env.SYSTEM_BUCKET.delete(configKey);

    console.log(`[ProfileMusic] Deleted music for account ${accountID}`);

    // Invalidate CF Cache for /profile-music/{accountID}.mp3
    const origin = new URL(request.url).origin;
    cfCacheDelete(`${origin}/profile-music/${accountID}.mp3`).catch(() => {});

    return new Response(JSON.stringify({ success: true, message: 'Profile music deleted' }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });

  } catch (error) {
    console.error('Delete profile music error:', error);
    return new Response(JSON.stringify({ error: 'Failed to delete music' }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleServeProfileMusic(request, env) {
  // CF Cache API — serve cached audio if available (10 min)
  const cacheReq = cfCacheKey(request);
  const cached = await cfCacheMatch(cacheReq);
  if (cached) return cached;

  const url = new URL(request.url);
  const pathParts = url.pathname.split('/');
  const filename = pathParts[pathParts.length - 1];
  const accountID = filename.replace(/\.(mp3|wav)$/, '');

  if (!accountID) {
    return new Response('Not found', { status: 404 });
  }

  try {
    const configKey = `profile-music/${accountID}.json`;
    const config = await getR2Json(env.SYSTEM_BUCKET, configKey);

    const extension = config?.format || 'mp3';
    let audioKey = `profile-music/${accountID}.${extension}`;
    let audioObj = await env.THUMBNAILS_BUCKET.get(audioKey, { skipMeta: true, cfCacheTtl: 300 });

    if (!audioObj) {
      const altExtension = extension === 'mp3' ? 'wav' : 'mp3';
      audioKey = `profile-music/${accountID}.${altExtension}`;
      audioObj = await env.THUMBNAILS_BUCKET.get(audioKey, { skipMeta: true, cfCacheTtl: 300 });
    }

    if (!audioObj) {
      return new Response('Music not found', { status: 404, headers: corsHeaders() });
    }

    const contentType = audioKey.endsWith('.wav') ? 'audio/wav' : 'audio/mpeg';

    const headers = {
      'Content-Type': contentType,
      'Cache-Control': NO_STORE_CACHE_CONTROL,
      ...corsHeaders()
    };

    if (config) {
      headers['X-Start-Ms'] = config.startMs?.toString() || '0';
      headers['X-End-Ms'] = config.endMs?.toString() || '20000';
      headers['X-Volume'] = config.volume?.toString() || '1.0';
    }

    // Cache the audio response at the edge (2 weeks)
    const body = await audioObj.arrayBuffer();
    const resp = new Response(body, { status: 200, headers });
    const cacheable = makeCacheable(resp, 1209600);
    cfCachePut(cacheReq, cacheable).catch(() => {});
    return cacheable.clone();

  } catch (error) {
    console.error('Serve profile music error:', error);
    return new Response('Error serving music', { status: 500, headers: corsHeaders() });
  }
}
