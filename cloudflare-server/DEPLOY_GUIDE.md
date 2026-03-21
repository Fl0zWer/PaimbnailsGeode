# Guía de Despliegue en Cloudflare

Esta guía te llevará paso a paso para desplegar el servidor de Paimon Thumbnails en Cloudflare Workers.

## Requisitos Previos

1. **Cuenta de Cloudflare** (plan Free es suficiente)
   - Registrarse en: https://dash.cloudflare.com/sign-up

2. **Node.js 18 o superior**
   - Descargar desde: https://nodejs.org/

3. **Git** (opcional, para clonar el repositorio)

## Paso 1: Preparación

### En Windows (PowerShell):

```powershell
# Navegar a la carpeta del servidor
cd "c:\Users\fg906\OneDrive\Imágenes\Paimon-Thumbnails-geode-main\cloudflare-server"

# Instalar dependencias
npm install

# Instalar Wrangler globalmente
npm install -g wrangler
```

### En Linux/Mac:

```bash
# Navegar a la carpeta del servidor
cd cloudflare-server

# Instalar dependencias
npm install

# Instalar Wrangler globalmente
npm install -g wrangler
```

## Paso 2: Autenticación en Cloudflare

```bash
wrangler login
```

Esto abrirá tu navegador para autorizar Wrangler con tu cuenta de Cloudflare.

## Paso 3: Crear Recursos en Cloudflare

### 3.1 Crear Bucket R2

```bash
wrangler r2 bucket create paimon-thumbnails
```

### 3.2 Crear KV Namespace (Producción)

```bash
wrangler kv:namespace create "QUEUE_KV"
```

Output esperado:
```
🌀 Creating namespace with title "paimon-thumbnails-server-QUEUE_KV"
✨ Success!
Add the following to your configuration file in your kv_namespaces array:
{ binding = "QUEUE_KV", id = "abc123def456..." }
```

**IMPORTANTE:** Copia el ID generado.

### 3.3 Crear KV Namespace (Preview/Dev)

```bash
wrangler kv:namespace create "QUEUE_KV" --preview
```

**IMPORTANTE:** Copia también este ID.

## Paso 4: Configurar wrangler.toml

Abre `wrangler.toml` y actualiza los IDs de KV:

```toml
[[kv_namespaces]]
binding = "QUEUE_KV"
id = "TU-ID-DE-PRODUCCION-AQUI"  # Del paso 3.2

[env.dev]
# ...
[[env.dev.kv_namespaces]]
binding = "QUEUE_KV"
id = "TU-ID-DE-PREVIEW-AQUI"  # Del paso 3.3

[env.production]
# ...
[[env.production.kv_namespaces]]
binding = "QUEUE_KV"
id = "TU-ID-DE-PRODUCCION-AQUI"  # Del paso 3.2
```

## Paso 5: Configurar Variables de Entorno

### 5.1 Para desarrollo local

```bash
# Copiar ejemplo
cp .dev.vars.example .dev.vars

# Editar .dev.vars y cambiar tu API_KEY
# Usa un editor de texto o:
notepad .dev.vars  # Windows
nano .dev.vars     # Linux/Mac
```

### 5.2 Para producción

Las variables en `wrangler.toml` bajo `[vars]` se usarán automáticamente:

```toml
[vars]
API_KEY = "CAMBIA-ESTO-POR-UNA-KEY-SEGURA"
MAX_UPLOAD_SIZE = "10485760"
ALLOWED_ORIGINS = "*"
```

**IMPORTANTE:** Cambia el `API_KEY` por una clave única y segura.

## Paso 6: Configurar Moderadores

Agregar usuarios a la lista de moderadores:

```bash
wrangler kv:key put --binding=QUEUE_KV "moderators" '["usuario1","usuario2","usuario3"]'
```

Ejemplo:
```bash
wrangler kv:key put --binding=QUEUE_KV "moderators" '["admin","moderador1","paimon"]'
```

## Paso 7: Probar en Desarrollo

```bash
npm run dev
```

Esto iniciará el servidor en `http://localhost:8787`

### Probar los endpoints:

```bash
# Health check
curl http://localhost:8787/health

# Verificar moderador
curl "http://localhost:8787/api/moderator/check?username=admin"

# Ejecutar suite de tests completa
node test.js
```

## Paso 8: Desplegar a Producción

```bash
npm run deploy
```

Output esperado:
```
Total Upload: XX.XX KiB / gzip: XX.XX KiB
Uploaded paimon-thumbnails-server (X.XX sec)
Published paimon-thumbnails-server (X.XX sec)
  https://paimon-thumbnails-server.your-subdomain.workers.dev
```

**¡Tu servidor está en producción!** 🎉

## Paso 9: Actualizar el Mod

Edita el archivo del mod que contiene la URL del servidor:

**Archivo:** `src/utils/HttpClient.cpp`

```cpp
HttpClient::HttpClient() {
    // Cambiar la URL a tu worker de Cloudflare
    m_serverURL = "https://paimon-thumbnails-server.your-subdomain.workers.dev";
    m_apiKey = "TU-API-KEY-QUE-PUSISTE-EN-WRANGLER-TOML";
    
    log::info("[HttpClient] Initialized with server: {}", m_serverURL);
}
```

Recompila el mod:

```bash
cd build
cmake --build . --config RelWithDebInfo
```

## Paso 10: Verificar Funcionamiento

### En el Dashboard de Cloudflare:

1. Ve a **Workers & Pages** en el dashboard
2. Busca **paimon-thumbnails-server**
3. Click en "View" para ver métricas
4. En la pestaña "Logs" puedes ver requests en tiempo real

### Desde la terminal:

```bash
# Ver logs en tiempo real
npm run tail

# O con wrangler directamente
wrangler tail
```

### Probar endpoints:

```bash
# Cambiar URL por la de tu worker
export WORKER_URL="https://paimon-thumbnails-server.your-subdomain.workers.dev"

# Health check
curl $WORKER_URL/health

# Verificar moderador
curl "$WORKER_URL/api/moderator/check?username=admin"

# Obtener cola de verificación (requiere API key)
curl -H "X-API-Key: TU-API-KEY" "$WORKER_URL/api/queue/verify"
```

## Troubleshooting

### Error: "KV namespace not found"

```bash
# Verificar que los namespaces existen
wrangler kv:namespace list

# Si no existe, crear de nuevo
wrangler kv:namespace create "QUEUE_KV"
```

### Error: "R2 bucket not found"

```bash
# Verificar buckets
wrangler r2 bucket list

# Si no existe, crear de nuevo
wrangler r2 bucket create paimon-thumbnails
```

### Error: "Unauthorized" en las peticiones

- Verifica que el `API_KEY` en `wrangler.toml` coincida con el del mod
- Verifica que estás enviando el header `X-API-Key` correctamente

### Cambios no se reflejan

```bash
# Limpiar cache y redesplegar
wrangler deploy --force
```

### Ver logs de errores

```bash
# Logs en tiempo real
wrangler tail

# O en el dashboard de Cloudflare
# Workers & Pages > paimon-thumbnails-server > Logs
```

## Comandos Útiles

```bash
# Desarrollo local
npm run dev

# Desplegar a producción
npm run deploy

# Ver logs en tiempo real
npm run tail

# Listar KV namespaces
wrangler kv:namespace list

# Listar buckets R2
wrangler r2 bucket list

# Ver contenido de un bucket R2
wrangler r2 object list paimon-thumbnails

# Agregar moderador
wrangler kv:key put --binding=QUEUE_KV "moderators" '["user1","user2"]'

# Ver valor de una key en KV
wrangler kv:key get --binding=QUEUE_KV "moderators"

# Eliminar key de KV
wrangler kv:key delete --binding=QUEUE_KV "queue:verify:12345"
```

## Actualizaciones

Para actualizar el servidor después de cambios en el código:

```bash
# 1. Hacer cambios en src/index.js
# 2. Probar localmente
npm run dev

# 3. Desplegar
npm run deploy
```

## Monitoreo

### Dashboard de Cloudflare

- **Requests**: Ver cantidad de peticiones
- **CPU Time**: Tiempo de procesamiento
- **Errors**: Rate de errores
- **Success Rate**: Tasa de éxito

### Límites del Plan Free

- 100,000 requests/day
- 10ms CPU time per request
- 10 GB R2 storage
- 1M R2 writes/month
- 10M R2 reads/month

**Para uso del mod, estos límites son más que suficientes.**

## Backups

### Backup de KV data:

```bash
# Exportar moderadores
wrangler kv:key get --binding=QUEUE_KV "moderators" > moderators-backup.json

# Listar todas las keys
wrangler kv:key list --binding=QUEUE_KV > kv-keys-backup.json
```

### Backup de R2:

```bash
# Descargar todas las thumbnails
wrangler r2 object list paimon-thumbnails > r2-files.txt

# Descargar un archivo específico
wrangler r2 object get paimon-thumbnails/thumbnails/12345.webp --file=12345.webp
```

## Soporte

Si tienes problemas:

1. Revisa los logs: `wrangler tail`
2. Verifica el dashboard de Cloudflare
3. Ejecuta los tests: `node test.js`
4. Consulta la documentación de Cloudflare: https://developers.cloudflare.com/workers/

---

**¡Felicidades! Tu servidor está listo para usar con el mod Paimon Thumbnails.** 🎉

