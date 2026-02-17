/**
 * Script de pruebas para el servidor de Cloudflare
 * Ejecutar con: node test.js
 */

const BASE_URL = 'http://localhost:8787'; // Cambiar a tu URL de producción si es necesario
const API_KEY = '7a4b2c1d-9e8f-4g3h-i2j1-k0l9m8n7p6o5'; // Cambiar por tu API key

// Colores para la consola
const colors = {
  reset: '\x1b[0m',
  green: '\x1b[32m',
  red: '\x1b[31m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m'
};

function log(message, color = colors.reset) {
  console.log(color + message + colors.reset);
}

async function testHealthCheck() {
  log('\n🏥 Testing Health Check...', colors.blue);
  try {
    const response = await fetch(`${BASE_URL}/health`);
    const data = await response.json();
    
    if (response.ok && data.status === 'ok') {
      log('✅ Health check passed', colors.green);
      return true;
    } else {
      log('❌ Health check failed', colors.red);
      return false;
    }
  } catch (error) {
    log(`❌ Health check error: ${error.message}`, colors.red);
    return false;
  }
}

async function testModeratorCheck() {
  log('\n👮 Testing Moderator Check...', colors.blue);
  try {
    const response = await fetch(`${BASE_URL}/api/moderator/check?username=admin`);
    const data = await response.json();
    
    if (response.ok) {
      log(`✅ Moderator check passed: admin is ${data.isModerator ? 'a moderator' : 'not a moderator'}`, colors.green);
      return true;
    } else {
      log('❌ Moderator check failed', colors.red);
      return false;
    }
  } catch (error) {
    log(`❌ Moderator check error: ${error.message}`, colors.red);
    return false;
  }
}

async function testUploadThumbnail() {
  log('\n📤 Testing Thumbnail Upload...', colors.blue);
  try {
    // Create a simple 1x1 PNG image (smallest valid PNG)
    const pngData = Buffer.from([
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
      0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
      0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
      0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
      0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
      0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
      0x42, 0x60, 0x82
    ]);

    const formData = new FormData();
    const blob = new Blob([pngData], { type: 'image/png' });
    formData.append('image', blob, 'test.png');
    formData.append('levelId', '999999');
    formData.append('username', 'testuser');
    formData.append('path', '/thumbnails');

    const response = await fetch(`${BASE_URL}/mod/upload`, {
      method: 'POST',
      headers: {
        'X-API-Key': API_KEY
      },
      body: formData
    });

    const data = await response.json();
    
    if (response.ok && data.success) {
      log('✅ Upload successful', colors.green);
      return true;
    } else {
      log(`❌ Upload failed: ${data.error || 'Unknown error'}`, colors.red);
      return false;
    }
  } catch (error) {
    log(`❌ Upload error: ${error.message}`, colors.red);
    return false;
  }
}

async function testThumbnailExists() {
  log('\n🔍 Testing Thumbnail Existence Check...', colors.blue);
  try {
    const response = await fetch(`${BASE_URL}/api/exists?levelId=999999&path=/thumbnails`);
    const data = await response.json();
    
    if (response.ok) {
      log(`✅ Existence check passed: thumbnail ${data.exists ? 'exists' : 'does not exist'}`, colors.green);
      return true;
    } else {
      log('❌ Existence check failed', colors.red);
      return false;
    }
  } catch (error) {
    log(`❌ Existence check error: ${error.message}`, colors.red);
    return false;
  }
}

async function testGetQueue() {
  log('\n📋 Testing Get Verification Queue...', colors.blue);
  try {
    const response = await fetch(`${BASE_URL}/api/queue/verify`, {
      headers: {
        'X-API-Key': API_KEY
      }
    });
    const data = await response.json();
    
    if (response.ok && data.success) {
      log(`✅ Queue retrieved: ${data.items.length} items`, colors.green);
      return true;
    } else {
      log(`❌ Queue retrieval failed: ${data.error || 'Unknown error'}`, colors.red);
      return false;
    }
  } catch (error) {
    log(`❌ Queue retrieval error: ${error.message}`, colors.red);
    return false;
  }
}

async function testSubmitReport() {
  log('\n📝 Testing Submit Report...', colors.blue);
  try {
    const response = await fetch(`${BASE_URL}/api/report/submit`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'X-API-Key': API_KEY
      },
      body: JSON.stringify({
        levelId: 999999,
        username: 'testuser',
        note: 'Test report - inappropriate content'
      })
    });

    const data = await response.json();
    
    if (response.ok && data.success) {
      log('✅ Report submitted successfully', colors.green);
      return true;
    } else {
      log(`❌ Report submission failed: ${data.error || 'Unknown error'}`, colors.red);
      return false;
    }
  } catch (error) {
    log(`❌ Report submission error: ${error.message}`, colors.red);
    return false;
  }
}

async function testAcceptQueueItem() {
  log('\n✅ Testing Accept Queue Item...', colors.blue);
  try {
    const response = await fetch(`${BASE_URL}/api/queue/accept/999999`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'X-API-Key': API_KEY
      },
      body: JSON.stringify({
        levelId: 999999,
        category: 'verify',
        username: 'testmoderator'
      })
    });

    const data = await response.json();
    
    if (response.ok && data.success) {
      log('✅ Queue item accepted', colors.green);
      return true;
    } else {
      log(`❌ Accept failed: ${data.error || 'Unknown error'}`, colors.red);
      return false;
    }
  } catch (error) {
    log(`❌ Accept error: ${error.message}`, colors.red);
    return false;
  }
}

async function testRejectQueueItem() {
  log('\n❌ Testing Reject Queue Item...', colors.blue);
  try {
    const response = await fetch(`${BASE_URL}/api/queue/reject/999999`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'X-API-Key': API_KEY
      },
      body: JSON.stringify({
        levelId: 999999,
        category: 'verify',
        username: 'testmoderator',
        reason: 'Test rejection'
      })
    });

    const data = await response.json();
    
    if (response.ok && data.success) {
      log('✅ Queue item rejected', colors.green);
      return true;
    } else {
      log(`❌ Reject failed: ${data.error || 'Unknown error'}`, colors.red);
      return false;
    }
  } catch (error) {
    log(`❌ Reject error: ${error.message}`, colors.red);
    return false;
  }
}

async function runAllTests() {
  log('\n' + '='.repeat(50), colors.yellow);
  log('🚀 Starting Paimon Thumbnails Server Tests', colors.yellow);
  log('='.repeat(50), colors.yellow);
  log(`\nTesting server at: ${BASE_URL}`, colors.blue);

  const results = [];

  results.push(await testHealthCheck());
  results.push(await testModeratorCheck());
  results.push(await testUploadThumbnail());
  results.push(await testThumbnailExists());
  results.push(await testGetQueue());
  results.push(await testSubmitReport());
  results.push(await testGetQueue()); // Check queue after report
  results.push(await testAcceptQueueItem());
  // Note: testRejectQueueItem would delete the thumbnail, so it's commented out
  // results.push(await testRejectQueueItem());

  const passed = results.filter(r => r).length;
  const total = results.length;

  log('\n' + '='.repeat(50), colors.yellow);
  log(`📊 Test Results: ${passed}/${total} tests passed`, colors.yellow);
  log('='.repeat(50), colors.yellow);

  if (passed === total) {
    log('✅ All tests passed!', colors.green);
  } else {
    log(`⚠️  ${total - passed} test(s) failed`, colors.red);
  }
}

// Run tests
runAllTests().catch(error => {
  log(`\n💥 Fatal error: ${error.message}`, colors.red);
  process.exit(1);
});
