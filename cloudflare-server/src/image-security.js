/**
 * Image Security Module for Paimon Thumbnails
 *
 * Provides robust validation against malicious payloads embedded in images/GIFs:
 * - Magic byte validation (real format detection, not trusting Content-Type)
 * - Polyglot file detection (files that are valid as multiple formats)
 * - Embedded script/code detection in metadata regions (PHP, JS, HTML, shell, etc.)
 * - Structural validation of PNG, JPEG, WebP, GIF internals
 * - Dangerous metadata/chunk scanning
 * - Null byte injection prevention
 * - Oversized metadata detection
 * - Double extension & path traversal prevention
 *
 * NOTE: Text/binary pattern scanning is ONLY done on metadata regions and
 * file boundaries, NOT on compressed pixel data, to avoid false positives.
 */

// ========== MAGIC BYTES SIGNATURES ==========

const MAGIC_SIGNATURES = {
  png:  { bytes: [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A], offset: 0 },
  jpeg: { bytes: [0xFF, 0xD8, 0xFF], offset: 0 },
  gif87a: { bytes: [0x47, 0x49, 0x46, 0x38, 0x37, 0x61], offset: 0 },  // GIF87a
  gif89a: { bytes: [0x47, 0x49, 0x46, 0x38, 0x39, 0x61], offset: 0 },  // GIF89a
  webp:  { bytes: [0x52, 0x49, 0x46, 0x46], offset: 0, secondary: { bytes: [0x57, 0x45, 0x42, 0x50], offset: 8 } },
};

// ========== DANGEROUS PATTERN SIGNATURES ==========

// Binary patterns that indicate embedded executables or archives
// IMPORTANT: Only signatures with 4+ bytes to avoid false positives in compressed pixel data
const DANGEROUS_BINARY_SIGNATURES = [
  { name: 'ELF executable',      bytes: [0x7F, 0x45, 0x4C, 0x46] },
  { name: 'PE/MZ executable',    bytes: [0x4D, 0x5A, 0x90, 0x00] },  // Full MZ+DOS stub, not just 0x4D 0x5A
  { name: 'Java class',          bytes: [0xCA, 0xFE, 0xBA, 0xBE] },
  { name: 'ZIP archive',         bytes: [0x50, 0x4B, 0x03, 0x04] },
  { name: 'RAR archive',         bytes: [0x52, 0x61, 0x72, 0x21, 0x1A, 0x07] }, // Full RAR signature
  { name: '7z archive',          bytes: [0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C] },
  { name: 'PDF document',        bytes: [0x25, 0x50, 0x44, 0x46, 0x2D] },  // %PDF-
  { name: 'WebAssembly',         bytes: [0x00, 0x61, 0x73, 0x6D] },
];

// Text patterns that indicate embedded scripts/code (case-insensitive search)
// These are ONLY scanned in metadata regions and file boundaries, NOT in pixel data
const DANGEROUS_TEXT_PATTERNS = [
  // Web scripting - high confidence patterns
  '<script',
  '</script>',
  'javascript:',
  'vbscript:',

  // PHP - high confidence
  '<?php',
  '<?=',

  // Server-side includes
  '<!--#exec',
  '<!--#include',

  // Shell shebangs
  '#!/bin/',
  '#!/usr/',

  // HTML injection - high confidence
  '<iframe',
  '<object data=',
  '<embed src=',
  '<applet',
  '<svg onload',
  '<svg/onload',
  '<body onload',
  '<meta http-equiv',

  // Data URI with executable content
  'data:text/html',
  'data:text/javascript',
  'data:application/x-javascript',
];

// ========== CORE VALIDATION FUNCTIONS ==========

/**
 * Detect the real image format based on magic bytes.
 * @param {Uint8Array} data
 * @returns {string|null} 'png' | 'jpeg' | 'gif' | 'webp' | null
 */
function detectRealFormat(data) {
  if (data.length < 12) return null;

  // PNG
  if (matchBytes(data, MAGIC_SIGNATURES.png.bytes, 0)) return 'png';

  // JPEG
  if (matchBytes(data, MAGIC_SIGNATURES.jpeg.bytes, 0)) return 'jpeg';

  // GIF (87a or 89a)
  if (matchBytes(data, MAGIC_SIGNATURES.gif87a.bytes, 0) ||
      matchBytes(data, MAGIC_SIGNATURES.gif89a.bytes, 0)) return 'gif';

  // WebP (RIFF....WEBP)
  if (matchBytes(data, MAGIC_SIGNATURES.webp.bytes, 0) &&
      matchBytes(data, MAGIC_SIGNATURES.webp.secondary.bytes, 8)) return 'webp';

  return null;
}

/**
 * Check if bytes at a given offset match expected values
 */
function matchBytes(data, expected, offset) {
  if (data.length < offset + expected.length) return false;
  for (let i = 0; i < expected.length; i++) {
    if (data[offset + i] !== expected[i]) return false;
  }
  return true;
}

/**
 * Scan the TAIL of the file for dangerous embedded binary signatures.
 * Polyglot attacks typically APPEND payloads after the image end marker.
 * We only scan the last portion of the file to avoid false positives in pixel data.
 * @param {Uint8Array} data
 * @param {number} tailOffset - Where the image data logically ends (after IEND/EOI/trailer)
 * @returns {string|null} Name of detected threat or null
 */
function scanTailForBinaries(data, tailOffset) {
  if (tailOffset >= data.length) return null;

  const tail = data.slice(tailOffset);
  for (const sig of DANGEROUS_BINARY_SIGNATURES) {
    for (let i = 0; i <= tail.length - sig.bytes.length; i++) {
      if (matchBytes(tail, sig.bytes, i)) {
        return sig.name;
      }
    }
  }
  return null;
}

/**
 * Extract metadata regions from a PNG file (tEXt, iTXt, zTXt, eXIf chunks).
 * Returns concatenated metadata bytes for text scanning.
 */
function extractPNGMetadata(data) {
  const metaChunks = [];
  let offset = 8;
  while (offset + 12 <= data.length) {
    const view = new DataView(data.buffer, data.byteOffset + offset, Math.min(8, data.length - offset));
    const chunkLength = view.getUint32(0, false);
    const chunkType = String.fromCharCode(data[offset + 4], data[offset + 5], data[offset + 6], data[offset + 7]);

    if (chunkLength > data.length - offset - 12) break;

    const metadataTypes = ['tEXt', 'iTXt', 'zTXt', 'eXIf', 'iCCP'];
    if (metadataTypes.includes(chunkType)) {
      const chunkData = data.slice(offset + 8, offset + 8 + chunkLength);
      metaChunks.push(chunkData);
    }

    if (chunkType === 'IEND') break;
    offset += 12 + chunkLength;
  }
  return metaChunks;
}

/**
 * Extract metadata regions from a JPEG file (COM and APP segments).
 */
function extractJPEGMetadata(data) {
  const metaSegments = [];
  let offset = 2;

  while (offset < data.length - 3) {
    if (data[offset] !== 0xFF) break;
    const marker = data[offset + 1];

    if (marker === 0xFF) { offset++; continue; }
    if (marker === 0xD8 || marker === 0xD9 || (marker >= 0xD0 && marker <= 0xD7) || marker === 0x01) {
      offset += 2; continue;
    }
    if (marker === 0xDA) break; // SOS - rest is compressed data

    const segLength = (data[offset + 2] << 8) | data[offset + 3];
    if (segLength < 2 || offset + 2 + segLength > data.length) break;

    // APP segments (0xE0-0xEF) and COM (0xFE)
    if ((marker >= 0xE0 && marker <= 0xEF) || marker === 0xFE) {
      metaSegments.push(data.slice(offset + 4, offset + 2 + segLength));
    }

    offset += 2 + segLength;
  }
  return metaSegments;
}

/**
 * Extract metadata from GIF (Comment and Application extensions).
 */
function extractGIFMetadata(data) {
  const metaBlocks = [];
  const packed = data[10];
  const hasGCT = (packed & 0x80) !== 0;
  const gctSize = hasGCT ? 3 * (1 << ((packed & 0x07) + 1)) : 0;
  let offset = 13 + gctSize;

  while (offset < data.length) {
    const introducer = data[offset];
    if (introducer === 0x3B) break; // Trailer

    if (introducer === 0x21 && offset + 2 < data.length) {
      const label = data[offset + 1];
      offset += 2;

      // Comment (0xFE) or Application Extension (0xFF)
      if (label === 0xFE || label === 0xFF) {
        const blockParts = [];
        while (offset < data.length) {
          const blockSize = data[offset]; offset++;
          if (blockSize === 0) break;
          if (offset + blockSize <= data.length) {
            blockParts.push(data.slice(offset, offset + blockSize));
          }
          offset += blockSize;
        }
        if (blockParts.length > 0) {
          const total = blockParts.reduce((s, b) => s + b.length, 0);
          const merged = new Uint8Array(total);
          let pos = 0;
          for (const part of blockParts) { merged.set(part, pos); pos += part.length; }
          metaBlocks.push(merged);
        }
      } else {
        // Skip other extensions
        while (offset < data.length) {
          const blockSize = data[offset]; offset++;
          if (blockSize === 0) break;
          offset += blockSize;
        }
      }
    } else if (introducer === 0x2C) {
      // Image descriptor - skip entirely
      if (offset + 10 > data.length) break;
      const imgPacked = data[offset + 9];
      const hasLCT = (imgPacked & 0x80) !== 0;
      const lctSize = hasLCT ? 3 * (1 << ((imgPacked & 0x07) + 1)) : 0;
      offset += 10 + lctSize;
      if (offset >= data.length) break;
      offset++; // LZW min code size
      while (offset < data.length) {
        const blockSize = data[offset]; offset++;
        if (blockSize === 0) break;
        offset += blockSize;
      }
    } else {
      break; // Unknown block
    }
  }
  return metaBlocks;
}

/**
 * Extract metadata from WebP (EXIF, XMP, ICCP chunks).
 */
function extractWebPMetadata(data) {
  const metaChunks = [];
  if (data.length < 20) return metaChunks;

  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const riffSize = view.getUint32(4, true);
  const expectedEnd = Math.min(riffSize + 8, data.length);
  let offset = 12;

  while (offset + 8 <= expectedEnd) {
    const chunkId = String.fromCharCode(data[offset], data[offset + 1], data[offset + 2], data[offset + 3]);
    const chunkSize = view.getUint32(offset + 4, true);

    if (offset + 8 + chunkSize > data.length) break;

    const metaTypes = ['EXIF', 'XMP ', 'ICCP'];
    if (metaTypes.includes(chunkId)) {
      metaChunks.push(data.slice(offset + 8, offset + 8 + chunkSize));
    }

    offset += 8 + chunkSize + (chunkSize % 2);
  }
  return metaChunks;
}

/**
 * Scan ONLY metadata regions for dangerous text patterns.
 * This avoids false positives from compressed pixel data.
 * @param {Uint8Array[]} metadataRegions - Array of metadata byte regions
 * @returns {{found: boolean, pattern: string}|null}
 */
function scanMetadataForDangerousText(metadataRegions) {
  for (const region of metadataRegions) {
    let text;
    try {
      text = new TextDecoder('utf-8', { fatal: false }).decode(region).toLowerCase();
    } catch {
      continue;
    }

    for (const pattern of DANGEROUS_TEXT_PATTERNS) {
      if (text.includes(pattern.toLowerCase())) {
        return { found: true, pattern };
      }
    }
  }
  return null;
}

/**
 * Scan the appended tail of a file (after image end marker) for dangerous text.
 * This catches polyglot attacks where scripts are appended after the image.
 * @param {Uint8Array} data
 * @param {number} tailOffset
 * @returns {{found: boolean, pattern: string}|null}
 */
function scanTailForDangerousText(data, tailOffset) {
  if (tailOffset >= data.length) return null;
  const tail = data.slice(tailOffset);
  return scanMetadataForDangerousText([tail]);
}

/**
 * Validate PNG structure for security issues
 * @param {Uint8Array} data
 * @returns {{valid: boolean, error?: string, endOffset?: number}}
 */
function validatePNGStructure(data) {
  if (data.length < 33) return { valid: false, error: 'PNG file too small' };

  if (!matchBytes(data, MAGIC_SIGNATURES.png.bytes, 0)) {
    return { valid: false, error: 'Invalid PNG signature' };
  }

  let offset = 8;
  let foundIHDR = false;
  let foundIEND = false;
  let endOffset = data.length;
  let chunkCount = 0;
  const MAX_CHUNKS = 10000;
  let totalMetadataSize = 0;
  const MAX_METADATA_SIZE = 1024 * 1024; // 1MB

  while (offset + 8 <= data.length && chunkCount < MAX_CHUNKS) {
    const view = new DataView(data.buffer, data.byteOffset + offset, Math.min(8, data.length - offset));
    const chunkLength = view.getUint32(0, false);
    const chunkType = String.fromCharCode(data[offset + 4], data[offset + 5], data[offset + 6], data[offset + 7]);

    if (chunkLength > data.length - offset) {
      return { valid: false, error: `PNG chunk '${chunkType}' has invalid length: ${chunkLength}` };
    }

    if (chunkCount === 0 && chunkType !== 'IHDR') {
      return { valid: false, error: 'First PNG chunk must be IHDR' };
    }

    if (chunkType === 'IHDR') {
      foundIHDR = true;
      if (chunkLength !== 13) {
        return { valid: false, error: 'Invalid IHDR chunk length' };
      }
    }

    if (chunkType === 'IEND') {
      foundIEND = true;
      endOffset = offset + 12 + chunkLength;
      const afterIEND = data.length - endOffset;
      if (afterIEND > 16) {
        return { valid: false, error: `Suspicious data after IEND chunk (${afterIEND} bytes appended)`, endOffset };
      }
      break;
    }

    const metadataChunks = ['tEXt', 'iTXt', 'zTXt', 'eXIf', 'iCCP', 'sPLT', 'hIST'];
    if (metadataChunks.includes(chunkType)) {
      totalMetadataSize += chunkLength;
      if (totalMetadataSize > MAX_METADATA_SIZE) {
        return { valid: false, error: 'Excessive metadata in PNG (possible payload hiding)' };
      }
    }

    offset += 12 + chunkLength;
    chunkCount++;
  }

  if (!foundIHDR) return { valid: false, error: 'Missing IHDR chunk' };
  if (!foundIEND) return { valid: false, error: 'Missing IEND chunk (truncated or malformed PNG)' };
  if (chunkCount >= MAX_CHUNKS) return { valid: false, error: 'Excessive number of PNG chunks (possible DoS)' };

  return { valid: true, endOffset };
}

/**
 * Validate JPEG structure for security issues
 * @param {Uint8Array} data
 * @returns {{valid: boolean, error?: string, endOffset?: number}}
 */
function validateJPEGStructure(data) {
  if (data.length < 20) return { valid: false, error: 'JPEG file too small' };

  if (data[0] !== 0xFF || data[1] !== 0xD8) {
    return { valid: false, error: 'Invalid JPEG SOI marker' };
  }

  let offset = 2;
  let segmentCount = 0;
  const MAX_SEGMENTS = 5000;
  let totalCommentSize = 0;
  let totalAPPSize = 0;
  const MAX_COMMENT_SIZE = 512 * 1024;
  const MAX_APP_SIZE = 2 * 1024 * 1024;
  let endOffset = data.length;

  while (offset < data.length - 1 && segmentCount < MAX_SEGMENTS) {
    if (data[offset] !== 0xFF) break;

    const marker = data[offset + 1];

    if (marker === 0xFF) { offset++; continue; }

    if (marker === 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker === 0x01) {
      offset += 2; continue;
    }

    if (marker === 0xD9) {
      // EOI
      endOffset = offset + 2;
      const remaining = data.length - endOffset;
      if (remaining > 16) {
        return { valid: false, error: `Suspicious data after JPEG EOI (${remaining} bytes appended)`, endOffset };
      }
      break;
    }

    if (marker === 0xDA) break; // SOS - rest is compressed

    if (offset + 4 > data.length) break;
    const segLength = (data[offset + 2] << 8) | data[offset + 3];
    if (segLength < 2) return { valid: false, error: 'Invalid JPEG segment length' };

    if (marker === 0xFE) {
      totalCommentSize += segLength;
      if (totalCommentSize > MAX_COMMENT_SIZE) {
        return { valid: false, error: 'Excessive JPEG comment data (possible payload hiding)' };
      }
    }

    if (marker >= 0xE0 && marker <= 0xEF) {
      totalAPPSize += segLength;
      if (totalAPPSize > MAX_APP_SIZE) {
        return { valid: false, error: 'Excessive JPEG APP metadata (possible payload hiding)' };
      }
    }

    offset += 2 + segLength;
    segmentCount++;
  }

  if (segmentCount >= MAX_SEGMENTS) {
    return { valid: false, error: 'Excessive number of JPEG segments (possible DoS)' };
  }

  return { valid: true, endOffset };
}

/**
 * Validate GIF structure for security issues
 * @param {Uint8Array} data
 * @returns {{valid: boolean, error?: string, endOffset?: number}}
 */
function validateGIFStructure(data) {
  if (data.length < 13) return { valid: false, error: 'GIF file too small' };

  const header = String.fromCharCode(...data.slice(0, 6));
  if (header !== 'GIF87a' && header !== 'GIF89a') {
    return { valid: false, error: 'Invalid GIF header' };
  }

  const width = data[6] | (data[7] << 8);
  const height = data[8] | (data[9] << 8);
  const packed = data[10];
  const hasGCT = (packed & 0x80) !== 0;
  const gctSize = hasGCT ? 3 * (1 << ((packed & 0x07) + 1)) : 0;

  if (width === 0 || height === 0) {
    return { valid: false, error: 'GIF has zero dimensions' };
  }

  if (width > 16384 || height > 16384) {
    return { valid: false, error: `GIF dimensions too large (${width}x${height})` };
  }

  let offset = 13 + gctSize;
  let blockCount = 0;
  const MAX_BLOCKS = 50000;
  let totalCommentSize = 0;
  let totalAppExtSize = 0;
  const MAX_COMMENT_SIZE = 256 * 1024;
  const MAX_APP_EXT_SIZE = 512 * 1024;
  let endOffset = data.length;

  while (offset < data.length && blockCount < MAX_BLOCKS) {
    const introducer = data[offset];

    if (introducer === 0x3B) {
      endOffset = offset + 1;
      const remaining = data.length - endOffset;
      if (remaining > 16) {
        return { valid: false, error: `Suspicious data after GIF trailer (${remaining} bytes appended)`, endOffset };
      }
      break;
    }

    if (introducer === 0x2C) {
      if (offset + 10 > data.length) {
        return { valid: false, error: 'Truncated GIF image descriptor' };
      }
      const imgPacked = data[offset + 9];
      const hasLCT = (imgPacked & 0x80) !== 0;
      const lctSize = hasLCT ? 3 * (1 << ((imgPacked & 0x07) + 1)) : 0;
      offset += 10 + lctSize;
      if (offset >= data.length) break;
      offset++;
      while (offset < data.length) {
        const blockSize = data[offset]; offset++;
        if (blockSize === 0) break;
        offset += blockSize;
      }
    } else if (introducer === 0x21) {
      if (offset + 2 > data.length) break;
      const label = data[offset + 1];
      offset += 2;

      if (label === 0xFE) {
        while (offset < data.length) {
          const blockSize = data[offset]; offset++;
          if (blockSize === 0) break;
          totalCommentSize += blockSize;
          offset += blockSize;
        }
        if (totalCommentSize > MAX_COMMENT_SIZE) {
          return { valid: false, error: 'Excessive GIF comment data (possible payload hiding)' };
        }
      } else if (label === 0xFF) {
        while (offset < data.length) {
          const blockSize = data[offset]; offset++;
          if (blockSize === 0) break;
          totalAppExtSize += blockSize;
          offset += blockSize;
        }
        if (totalAppExtSize > MAX_APP_EXT_SIZE) {
          return { valid: false, error: 'Excessive GIF application extension data (possible payload hiding)' };
        }
      } else {
        while (offset < data.length) {
          const blockSize = data[offset]; offset++;
          if (blockSize === 0) break;
          offset += blockSize;
        }
      }
    } else {
      // Unknown block - be lenient here, some encoders produce non-standard GIFs
      // Just stop parsing instead of rejecting outright
      break;
    }

    blockCount++;
  }

  if (blockCount >= MAX_BLOCKS) {
    return { valid: false, error: 'Excessive number of GIF blocks (possible DoS/decompression bomb)' };
  }

  return { valid: true, endOffset };
}

/**
 * Validate WebP structure for security issues
 * @param {Uint8Array} data
 * @returns {{valid: boolean, error?: string, endOffset?: number}}
 */
function validateWebPStructure(data) {
  if (data.length < 20) return { valid: false, error: 'WebP file too small' };

  if (!matchBytes(data, [0x52, 0x49, 0x46, 0x46], 0)) {
    return { valid: false, error: 'Invalid RIFF header' };
  }

  if (!matchBytes(data, [0x57, 0x45, 0x42, 0x50], 8)) {
    return { valid: false, error: 'Invalid WebP identifier' };
  }

  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const riffSize = view.getUint32(4, true);
  const expectedSize = riffSize + 8;
  const endOffset = expectedSize;

  if (data.length > expectedSize + 16) {
    return { valid: false, error: `Data appended after RIFF container (${data.length - expectedSize} extra bytes)`, endOffset };
  }

  let offset = 12;
  let chunkCount = 0;
  const MAX_CHUNKS = 1000;
  let totalMetadataSize = 0;
  const MAX_METADATA = 2 * 1024 * 1024;

  while (offset + 8 <= data.length && offset < expectedSize && chunkCount < MAX_CHUNKS) {
    const chunkId = String.fromCharCode(data[offset], data[offset + 1], data[offset + 2], data[offset + 3]);
    const chunkSize = view.getUint32(offset + 4, true);

    const metaChunks = ['EXIF', 'XMP ', 'ICCP'];
    if (metaChunks.includes(chunkId)) {
      totalMetadataSize += chunkSize;
      if (totalMetadataSize > MAX_METADATA) {
        return { valid: false, error: 'Excessive WebP metadata (possible payload hiding)' };
      }
    }

    offset += 8 + chunkSize + (chunkSize % 2);
    chunkCount++;
  }

  if (chunkCount >= MAX_CHUNKS) {
    return { valid: false, error: 'Excessive number of WebP chunks' };
  }

  return { valid: true, endOffset };
}

/**
 * Check for null byte injection in filenames
 */
function hasNullByteInjection(filename) {
  return filename.includes('\0') || filename.includes('%00');
}

/**
 * Check for path traversal attempts
 */
function hasPathTraversal(input) {
  const patterns = ['../', '..\\', '%2e%2e', '%252e'];
  const lower = input.toLowerCase();
  return patterns.some(p => lower.includes(p));
}

/**
 * Check for double/dangerous extensions
 */
function hasDangerousExtension(filename) {
  const dangerous = [
    '.php', '.php3', '.php4', '.php5', '.phtml', '.pht',
    '.jsp', '.jspx', '.asp', '.aspx', '.ashx',
    '.exe', '.dll', '.bat', '.cmd', '.com', '.scr', '.ps1',
    '.sh', '.cgi', '.pl', '.py', '.rb',
    '.html', '.htm', '.xhtml', '.shtml', '.svg',
    '.swf', '.jar', '.war',
    '.htaccess', '.htpasswd',
  ];

  const lower = filename.toLowerCase();
  const parts = lower.split('.');
  if (parts.length > 2) {
    for (let i = 1; i < parts.length - 1; i++) {
      if (dangerous.some(ext => ext === '.' + parts[i])) {
        return true;
      }
    }
  }

  return dangerous.some(ext => lower.endsWith(ext));
}

/**
 * Detect decompression bomb potential
 */
function checkDecompressionBomb(data, width, height) {
  const pixelCount = width * height;
  const estimatedUncompressed = pixelCount * 4;
  const ratio = estimatedUncompressed / data.length;

  if (ratio > 1500 && pixelCount > 4000000) {
    return { safe: false, error: `Suspected decompression bomb (ratio ${Math.round(ratio)}:1, ${width}x${height})` };
  }

  if (width > 32768 || height > 32768) {
    return { safe: false, error: `Image dimensions too large (${width}x${height})` };
  }

  if (pixelCount > 100000000) {
    return { safe: false, error: `Too many pixels (${pixelCount})` };
  }

  return { safe: true };
}

/**
 * Extract basic dimensions from image data for decompression bomb detection.
 */
function getBasicDimensions(data, format) {
  try {
    if (format === 'png' && data.length >= 24) {
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
      return { width: view.getUint32(16, false), height: view.getUint32(20, false) };
    }
    if (format === 'gif' && data.length >= 10) {
      return { width: data[6] | (data[7] << 8), height: data[8] | (data[9] << 8) };
    }
    if (format === 'webp' && data.length >= 30) {
      const chunkType = String.fromCharCode(data[12], data[13], data[14], data[15]);
      if (chunkType === 'VP8X') {
        return {
          width: (data[24] | (data[25] << 8) | (data[26] << 16)) + 1,
          height: (data[27] | (data[28] << 8) | (data[29] << 16)) + 1
        };
      }
      if (chunkType === 'VP8 ' && data.length >= 30) {
        return {
          width: (data[26] | (data[27] << 8)) & 0x3FFF,
          height: (data[28] | (data[29] << 8)) & 0x3FFF
        };
      }
    }
    if (format === 'jpeg') {
      let offset = 2;
      while (offset < data.length - 8) {
        if (data[offset] !== 0xFF) break;
        const marker = data[offset + 1];
        const len = (data[offset + 2] << 8) | data[offset + 3];
        if (marker === 0xC0 || marker === 0xC2) {
          return {
            width: (data[offset + 7] << 8) | data[offset + 8],
            height: (data[offset + 5] << 8) | data[offset + 6]
          };
        }
        offset += 2 + len;
      }
    }
  } catch { /* ignore parse errors */ }
  return null;
}

// ========== MAIN VALIDATION FUNCTION ==========

/**
 * Comprehensive image security validation.
 *
 * @param {Uint8Array} buffer - Raw image data
 * @param {string} declaredType - MIME type declared by the client (e.g., 'image/png')
 * @param {string} [filename] - Optional filename for extension validation
 * @returns {{
 *   safe: boolean,
 *   realFormat: string|null,
 *   errors: string[],
 *   warnings: string[]
 * }}
 */
export function validateImageSecurity(buffer, declaredType, filename = '') {
  const errors = [];
  const warnings = [];
  const data = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);

  // ===== 1. Basic sanity checks =====
  if (data.length < 12) {
    return { safe: false, realFormat: null, errors: ['File too small to be a valid image'], warnings };
  }

  if (data.length > 100 * 1024 * 1024) {
    return { safe: false, realFormat: null, errors: ['File exceeds maximum size limit'], warnings };
  }

  // ===== 2. Filename validation =====
  if (filename) {
    if (hasNullByteInjection(filename)) {
      errors.push('Null byte injection detected in filename');
    }
    if (hasPathTraversal(filename)) {
      errors.push('Path traversal detected in filename');
    }
    if (hasDangerousExtension(filename)) {
      errors.push('Dangerous file extension detected');
    }
  }

  // ===== 3. Magic byte validation =====
  const realFormat = detectRealFormat(data);
  if (!realFormat) {
    return { safe: false, realFormat: null, errors: ['Unable to determine image format from magic bytes - not a valid image'], warnings };
  }

  // ===== 4. MIME type mismatch detection =====
  const mimeToFormat = {
    'image/png': 'png',
    'image/jpeg': 'jpeg',
    'image/jpg': 'jpeg',
    'image/gif': 'gif',
    'image/webp': 'webp',
  };

  const expectedFormat = mimeToFormat[declaredType?.toLowerCase()];
  if (expectedFormat && expectedFormat !== realFormat) {
    errors.push(`MIME type mismatch: declared ${declaredType} but magic bytes indicate ${realFormat} (possible polyglot attack)`);
  }

  // ===== 5. Structural validation per format =====
  let structResult;
  switch (realFormat) {
    case 'png':
      structResult = validatePNGStructure(data);
      break;
    case 'jpeg':
      structResult = validateJPEGStructure(data);
      break;
    case 'gif':
      structResult = validateGIFStructure(data);
      break;
    case 'webp':
      structResult = validateWebPStructure(data);
      break;
  }

  if (structResult && !structResult.valid) {
    errors.push(`${realFormat.toUpperCase()} structure error: ${structResult.error}`);
  }

  // ===== 5.5. Decompression bomb check =====
  const dims = getBasicDimensions(data, realFormat);
  if (dims) {
    const bombCheck = checkDecompressionBomb(data, dims.width, dims.height);
    if (!bombCheck.safe) {
      errors.push(bombCheck.error);
    }
  }

  // ===== 6. Metadata text scanning (NOT pixel data) =====
  let metadataRegions = [];
  try {
    switch (realFormat) {
      case 'png':  metadataRegions = extractPNGMetadata(data); break;
      case 'jpeg': metadataRegions = extractJPEGMetadata(data); break;
      case 'gif':  metadataRegions = extractGIFMetadata(data); break;
      case 'webp': metadataRegions = extractWebPMetadata(data); break;
    }
  } catch {
    // If metadata extraction fails, skip text scanning - don't block the upload
  }

  if (metadataRegions.length > 0) {
    const metaTextResult = scanMetadataForDangerousText(metadataRegions);
    if (metaTextResult) {
      errors.push(`Dangerous code pattern in metadata: ${metaTextResult.pattern}`);
    }
  }

  // ===== 7. Tail scanning (data after image end marker) =====
  const tailOffset = structResult?.endOffset || data.length;
  if (tailOffset < data.length) {
    // Scan appended data for binaries
    const tailBinary = scanTailForBinaries(data, tailOffset);
    if (tailBinary) {
      errors.push(`Embedded binary after image end: ${tailBinary}`);
    }
    // Scan appended data for scripts
    const tailText = scanTailForDangerousText(data, tailOffset);
    if (tailText) {
      errors.push(`Dangerous code appended after image end: ${tailText.pattern}`);
    }
  }

  // ===== 8. Return results =====
  return {
    safe: errors.length === 0,
    realFormat,
    errors,
    warnings
  };
}

/**
 * Quick validation - just checks magic bytes and basic structure.
 * Use this for less critical paths where full scan is too expensive.
 * @param {Uint8Array} buffer
 * @param {string} declaredType
 * @returns {boolean}
 */
/**
 * Detect the real MIME type from buffer magic bytes.
 * Returns 'image/png', 'image/jpeg', 'image/gif', 'image/webp', or null.
 */
export function detectMimeType(buffer) {
  const data = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  const format = detectRealFormat(data);
  const map = { png: 'image/png', jpeg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp' };
  return map[format] || null;
}

export function quickValidateImage(buffer, declaredType) {
  const data = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
  if (data.length < 12) return false;

  const realFormat = detectRealFormat(data);
  if (!realFormat) return false;

  const mimeToFormat = {
    'image/png': 'png',
    'image/jpeg': 'jpeg',
    'image/jpg': 'jpeg',
    'image/gif': 'gif',
    'image/webp': 'webp',
  };

  const expected = mimeToFormat[declaredType?.toLowerCase()];
  return !(expected && expected !== realFormat);
}

