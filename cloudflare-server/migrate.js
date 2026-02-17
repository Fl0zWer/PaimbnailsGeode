const fetch = require('node-fetch');

const API_KEY = "7a4b2c1d-9e8f-4g3h-i2j1-k0l9m8n7p6o5";
const WORKER_URL = "https://paimon-thumbnails-server.paimonalcuadrado.workers.dev";

async function migrate() {
  console.log('Starting migration...');
  let cursor = null;
  let done = false;
  let totalMigrated = 0;

  while (!done) {
    try {
      console.log(`Migrating batch... (Cursor: ${cursor})`);
      const response = await fetch(`${WORKER_URL}/api/system/migrate`, {
        method: 'POST',
        headers: {
          'X-API-Key': API_KEY,
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({ cursor })
      });

      if (!response.ok) {
        const text = await response.text();
        throw new Error(`HTTP ${response.status}: ${text}`);
      }

      const data = await response.json();
      
      if (data.errors && data.errors.length > 0) {
        console.error('Errors in batch:', data.errors);
      }

      totalMigrated += data.migrated;
      console.log(`Batch migrated: ${data.migrated}. Total: ${totalMigrated}`);

      if (data.done) {
        done = true;
        console.log('Migration complete!');
      } else {
        cursor = data.cursor;
      }

    } catch (error) {
      console.error('Migration failed:', error);
      break;
    }
  }
}

migrate();