/**
 * CORS & cache-control headers middleware
 */

export const NO_STORE_CACHE_CONTROL = 'no-store, no-cache, must-revalidate, max-age=0';

export function noStoreHeaders(extra = {}) {
  return {
    'Cache-Control': NO_STORE_CACHE_CONTROL,
    Pragma: 'no-cache',
    Expires: '0',
    ...extra,
  };
}

export function redirectNoStore(url, status = 302, extraHeaders = {}) {
  return new Response(null, {
    status,
    headers: noStoreHeaders({
      Location: url,
      ...extraHeaders,
    }),
  });
}

export function corsHeaders(origin) {
  return {
    'Access-Control-Allow-Origin': origin || '*',
    'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type, X-API-Key, X-Mod-Code, Authorization',
    'Access-Control-Max-Age': '0',
    ...noStoreHeaders(),
  };
}

export function handleOptions(request) {
  const origin = request.headers.get('Origin');
  return new Response(null, {
    status: 204,
    headers: { ...corsHeaders(origin), ...noStoreHeaders() }
  });
}
