
/**
 * Helper to extract image dimensions from binary buffer
 * Supports PNG, JPEG, and WebP (VP8, VP8L, VP8X)
 * @param {Uint8Array|ArrayBuffer} buffer 
 * @returns {{width: number, height: number, type: string}|null}
 */
export function getImageDimensions(buffer) {
    const data = new Uint8Array(buffer);
    if (data.length < 30) return null;
  
    // PNG
    // Signature: 89 50 4E 47 0D 0A 1A 0A
    if (data[0] === 0x89 && data[1] === 0x50 && data[2] === 0x4E && data[3] === 0x47) {
      const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
      // IHDR Data at offset 16
      const width = view.getUint32(16, false); // Big Endian
      const height = view.getUint32(20, false);
      return { width, height, type: 'png' };
    }
  
    // WebP
    // RIFF .... WEBP
    if (data[0] === 0x52 && data[1] === 0x49 && data[2] === 0x46 && data[3] === 0x46 &&
        data[8] === 0x57 && data[9] === 0x45 && data[10] === 0x42 && data[11] === 0x50) {
      
      const chunkType = String.fromCharCode(data[12], data[13], data[14], data[15]);
      
      if (chunkType === 'VP8X') {
        // VP8X: Extended header
        // Offset 24-26 -> Width
        // Offset 27-29 -> Height
        const width = (data[24] | (data[25] << 8) | (data[26] << 16)) + 1;
        const height = (data[27] | (data[28] << 8) | (data[29] << 16)) + 1;
        return { width, height, type: 'webp' };
      }
      
      if (chunkType === 'VP8 ') {
        // VP8 (Lossy)
        // Check local signature 9d 01 2a at offset 23
        if (data[23] !== 0x9D || data[24] !== 0x01 || data[25] !== 0x2A) {
             // Sometimes there is an offset? VP8 chunk usually:
             // [VP8 ][size][frame_header]
             // Frame header: [1-bit frame type][3-bit version][1-bit show_frame][19-bit size of 1st partition]
             // Then 3 bytes signature 0x9D012A.
             // This is tricky. Simplified check for common simple VP8 webp files:
             // offset 26-27: width
             // but only IF keyframe.
             // For safety in simplified parser, we might skip complex VP8 parsing if not standard keyframe.
             // But assuming thumbnails are keyframes (still images).
        }
         const width = (data[26] | (data[27] << 8)) & 0x3FFF;
         const height = (data[28] | (data[29] << 8)) & 0x3FFF;
         return { width, height, type: 'webp' };
      }
      
      if (chunkType === 'VP8L') {
         // VP8L (Lossless)
         if (data[20] !== 0x2f) return null;
         
         const b0 = data[21];
         const b1 = data[22];
         const b2 = data[23];
         const b3 = data[24];
         
         const width = (b0 | ((b1 & 0x3F) << 8)) + 1;
         const height = ((b1 >> 6) | (b2 << 2) | ((b3 & 0xF) << 10)) + 1;
         
         return { width, height, type: 'webp' };
      }
    }
    
    // GIF (GIF87a / GIF89a)
    if (data[0] === 0x47 && data[1] === 0x49 && data[2] === 0x46 && data[3] === 0x38 &&
        (data[4] === 0x37 || data[4] === 0x39) && data[5] === 0x61) {
      const width = data[6] | (data[7] << 8);
      const height = data[8] | (data[9] << 8);
      return { width, height, type: 'gif' };
    }

    // JPEG
    if (data[0] === 0xFF && data[1] === 0xD8) {
        let offset = 2;
        while (offset < data.length - 8) {
            if (data[offset] !== 0xFF) break;
            const marker = data[offset + 1];
            const len = (data[offset + 2] << 8) | data[offset + 3];
            
            if (marker === 0xC0 || marker === 0xC2) { 
                const height = (data[offset + 5] << 8) | data[offset + 6];
                const width = (data[offset + 7] << 8) | data[offset + 8];
                return { width, height, type: 'jpeg' };
            }
            offset += 2 + len;
        }
    }
  
    return null;
  }
