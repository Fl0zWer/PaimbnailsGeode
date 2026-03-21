import https from 'https';

const WORKER_URL = 'https://paimon-thumbnails-server.paimonalcuadrado.workers.dev';
const API_KEY = '7a4b2c1d-9e8f-4g3h-i2j1-k0l9m8n7p6o5'; // Your Admin API Key

async function migrateBucket(target) {
    console.log(`\nStarting migration for target: ${target}`);
    let cursor = null;
    let total = 0;
    let done = false;

    while (!done) {
        const url = `${WORKER_URL}/api/admin/migrate-bunny?target=${target}${cursor ? `&cursor=${encodeURIComponent(cursor)}` : ''}`;
        
        try {
            const data = await new Promise((resolve, reject) => {
                const req = https.request(url, {
                    method: 'GET',
                    headers: {
                        'X-API-Key': API_KEY
                    }
                }, (res) => {
                    let body = '';
                    res.on('data', (chunk) => body += chunk);
                    res.on('end', () => {
                        if (res.statusCode !== 200) {
                            reject(new Error(`Status ${res.statusCode}: ${body}`));
                        } else {
                            try {
                                resolve(JSON.parse(body));
                            } catch (e) {
                                reject(e);
                            }
                        }
                    });
                });
                req.on('error', reject);
                req.end();
            });

            if (data.results) {
                data.results.forEach(r => {
                    if (r.success) console.log(`[OK] ${r.key}`);
                    else console.error(`[ERR] ${r.key}: ${r.error || r.status}`);
                });
                total += data.results.length;
            }

            if (data.done) {
                done = true;
                console.log(`Finished ${target}. Total processed: ${total}`);
            } else {
                cursor = data.cursor;
                console.log(`Batch done. Total so far: ${total}. Continuing...`);
            }

        } catch (error) {
            console.error('Migration error:', error.message);
            console.log('Retrying in 5 seconds...');
            await new Promise(r => setTimeout(r, 5000));
        }
    }
}

async function run() {
    await migrateBucket('system');
    await migrateBucket('thumbnails');
}

run();
