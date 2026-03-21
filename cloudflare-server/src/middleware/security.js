/**
 * Image security validation middleware
 */
import { validateImageSecurity } from '../image-security.js';
import { corsHeaders } from './cors.js';

/**
 * Run full security validation on an uploaded image buffer.
 * Returns a Response with 400 status if malicious content is detected, or null if safe.
 */
export function rejectIfMalicious(buffer, declaredMimeType, filename = '') {
  const result = validateImageSecurity(buffer, declaredMimeType, filename);

  if (!result.safe) {
    console.error(`[Security] Image REJECTED - Errors: ${result.errors.join('; ')}`);
    if (result.warnings.length > 0) {
      console.warn(`[Security] Warnings: ${result.warnings.join('; ')}`);
    }
    return new Response(JSON.stringify({
      error: 'Image rejected by security scan',
      details: result.errors[0],
      code: 'SECURITY_VIOLATION'
    }), {
      status: 400,
      headers: { 'Content-Type': 'application/json', ...corsHeaders() }
    });
  }

  if (result.warnings.length > 0) {
    console.warn(`[Security] Image accepted with warnings: ${result.warnings.join('; ')}`);
  }

  return null;
}
