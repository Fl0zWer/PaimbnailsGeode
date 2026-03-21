/**
 * Mod system controllers — version check, mod download
 */
import { corsHeaders } from '../middleware/cors.js';

const CURRENT_MOD_VERSION = {
  version: '1.1.6',
  downloadUrl: 'https://paimon-thumbnails-server.paimonalcuadrado.workers.dev/downloads/paimon.level_thumbnails.geode',
  changelog: '• Nuevo: Animación de flash al cargar miniaturas\n• Corrección: Mejoras en la carga de imágenes'
};

export async function handleVersionCheck(request) {
  try {
    console.log('[VersionCheck] Request from:', request.headers.get('user-agent'));
    return new Response(JSON.stringify(CURRENT_MOD_VERSION), {
      status: 200, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  } catch (error) {
    console.error('[VersionCheck] Error:', error);
    return new Response(JSON.stringify({ error: 'Internal server error', message: error.message || 'Unknown error' }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}

export async function handleModDownload(request, env) {
  try {
    const object = await env.SYSTEM_BUCKET.get('mod-releases/paimon.level_thumbnails.geode');
    if (!object) {
      console.error('[ModDownload] File not found in R2');
      return new Response(JSON.stringify({ error: 'Mod file not found' }), {
        status: 404, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
      });
    }

    console.log('[ModDownload] Serving mod file from R2');
    return new Response(object.body, {
      status: 200,
      headers: {
        'Content-Type': 'application/octet-stream',
        'Content-Disposition': 'attachment; filename="paimon.level_thumbnails.geode"',
        ...corsHeaders()
      }
    });
  } catch (error) {
    console.error('[ModDownload] Error:', error);
    return new Response(JSON.stringify({ error: 'Internal server error', message: error.message || 'Unknown error' }), {
      status: 500, headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }
}
