/**
 * Pet Shop controllers
 */
import { corsHeaders, NO_STORE_CACHE_CONTROL } from '../middleware/cors.js';
import { verifyApiKey } from '../middleware/auth.js';
import { rejectIfMalicious } from '../middleware/security.js';
import { getR2Json, putR2Json, expandKeyVariants } from '../services/storage.js';
import { getModerators } from '../services/moderation.js';
import { ADMIN_USERS } from '../middleware/auth.js';

const PET_SHOP_CATALOG_KEY = 'data/pet-shop/catalog.json';
const PET_SHOP_IMAGE_PREFIX = 'pet-shop/';

export async function handlePetShopList(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const catalog = await getR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY);
    const items = catalog?.items || [];
    return new Response(JSON.stringify({ items }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('[PetShop] List error:', error);
    return new Response(JSON.stringify({ error: 'Failed to load pet shop' }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handlePetShopDownload(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const url = new URL(request.url);
    const filename = url.pathname.split('/').pop();
    if (!filename) {
      return new Response(JSON.stringify({ error: 'Missing filename' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const key = PET_SHOP_IMAGE_PREFIX + filename;
    const candidates = expandKeyVariants(key);
    let object = null;
    for (const k of candidates) {
      object = await env.THUMBNAILS_BUCKET.get(k);
      if (object) break;
    }

    if (!object) {
      return new Response(JSON.stringify({ error: 'Pet not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const ext = filename.split('.').pop()?.toLowerCase();
    let contentType = 'application/octet-stream';
    if (ext === 'png') contentType = 'image/png';
    else if (ext === 'gif') contentType = 'image/gif';
    else if (ext === 'jpg' || ext === 'jpeg') contentType = 'image/jpeg';
    else if (ext === 'webp') contentType = 'image/webp';

    return new Response(object.body, {
      status: 200,
      headers: {
        'Content-Type': contentType,
        'Content-Disposition': `attachment; filename="${filename}"`,
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error('[PetShop] Download error:', error);
    return new Response(JSON.stringify({ error: 'Download failed' }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handlePetShopUpload(request, env) {
  if (!verifyApiKey(request, env)) {
    return new Response(JSON.stringify({ error: 'Unauthorized' }), {
      status: 401, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  try {
    const formData = await request.formData();
    const file = formData.get('image');
    const name = formData.get('name') || 'Unknown Pet';
    const creator = formData.get('creator') || 'Unknown';

    if (!file) {
      return new Response(JSON.stringify({ error: 'Missing image file' }), {
        status: 400, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const creatorLower = creator.toLowerCase();
    const moderators = await getModerators(env.SYSTEM_BUCKET);
    const isAdmin = ADMIN_USERS.includes(creatorLower);
    const isMod = moderators.includes(creatorLower);

    if (!isAdmin && !isMod) {
      return new Response(JSON.stringify({ error: 'Only moderators can upload pets' }), {
        status: 403, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const fileType = file.type || 'image/png';
    let format = 'png';
    if (fileType === 'image/gif') format = 'gif';
    else if (fileType === 'image/jpeg') format = 'jpg';
    else if (fileType === 'image/webp') format = 'webp';

    if (file.size > 5 * 1024 * 1024) {
      return new Response(JSON.stringify({ error: 'File too large (max 5MB)' }), {
        status: 413, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    const id = `pet_${Date.now()}_${Math.random().toString(36).substring(2, 8)}`;
    const storageKey = PET_SHOP_IMAGE_PREFIX + id + '.' + format;

    const arrayBuffer = await file.arrayBuffer();
    const securityReject = rejectIfMalicious(new Uint8Array(arrayBuffer), fileType, file.name || `pet.${format}`);
    if (securityReject) return securityReject;

    await env.THUMBNAILS_BUCKET.put(storageKey, arrayBuffer, {
      httpMetadata: { contentType: fileType, cacheControl: NO_STORE_CACHE_CONTROL }
    });

    const catalog = await getR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY) || { items: [] };
    if (!Array.isArray(catalog.items)) catalog.items = [];

    const newItem = {
      id, name: name.substring(0, 50), creator, format,
      fileSize: file.size, uploadedAt: new Date().toISOString()
    };

    catalog.items.unshift(newItem);
    await putR2Json(env.SYSTEM_BUCKET, PET_SHOP_CATALOG_KEY, catalog);

    console.log(`[PetShop] ${creator} uploaded pet "${name}" (${id}.${format}, ${file.size} bytes)`);

    return new Response(JSON.stringify({ success: true, message: 'Pet uploaded successfully', item: newItem }), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('[PetShop] Upload error:', error);
    return new Response(JSON.stringify({ error: 'Upload failed: ' + error.message }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
