# Paimon Thumbnails - Cloudflare Server

Servidor para Cloudflare Workers con R2 storage que maneja las miniaturas del mod Paimon Thumbnails para Geometry Dash.

## Características

- ✅ **Conversión automática PNG → WebP** para optimizar almacenamiento
- ✅ **Conversión WebP → PNG** cuando se solicita formato PNG
- ✅ **Almacenamiento en R2** con metadatos
- ✅ **Sistema de cola de verificación** con KV storage
- ✅ **Sistema de moderación** (aceptar/rechazar miniaturas)
- ✅ **Sistema de reportes** de miniaturas problemáticas
- ✅ **Autenticación por API Key**
- ✅ **CORS configurado** para peticiones desde el mod
- ✅ **Cache optimizado** (31536000 segundos = 1 año)

## Requisitos

- Cuenta de Cloudflare (plan Free funciona)
- Node.js 18+ instalado
- Wrangler CLI instalado (`npm install -g wrangler`)

## Configuración

### 1. Instalar dependencias

```bash
cd cloudflare-server
npm install
```

### 2. Configurar Cloudflare

```bash
# Iniciar sesión en Cloudflare
wrangler login

# Crear bucket R2 para thumbnails
wrangler r2 bucket create paimon-thumbnails

# Crear KV namespace para la cola
wrangler kv:namespace create "QUEUE_KV"
wrangler kv:namespace create "QUEUE_KV" --preview

# Crear KV namespace para moderadores
wrangler kv:key put --binding=QUEUE_KV "moderators" '["username1","username2"]'
```

### 3. Actualizar wrangler.toml

Después de crear los namespaces KV, copia los IDs generados y actualiza `wrangler.toml`:

```toml
[[kv_namespaces]]
binding = "QUEUE_KV"
id = "tu-kv-namespace-id-aqui"
```

### 4. Configurar variables de entorno

Edita `wrangler.toml` y actualiza:

```toml
[vars]
API_KEY = "tu-api-key-segura-aqui"
MAX_UPLOAD_SIZE = "10485760"  # 10MB
ALLOWED_ORIGINS = "*"
```

## Desarrollo local

```bash
npm run dev
```

El servidor estará disponible en `http://localhost:8787`

## Deploy a producción

```bash
npm run deploy
```

## Endpoints

### Upload de miniatura
```
POST /mod/upload
Headers: X-API-Key: tu-api-key
Body: multipart/form-data
  - image: archivo PNG
  - levelId: ID del nivel
  - username: nombre del usuario
  - path: (opcional) ruta de almacenamiento
```

### Descargar miniatura
```
GET /api/download/{levelId}.webp?path=/thumbnails
GET /api/download/{levelId}.png?path=/thumbnails (convierte a PNG)
```

### Verificar existencia
```
GET /api/exists?levelId=12345&path=/thumbnails
```

### Verificar moderador
```
GET /api/moderator/check?username=nombre
```

### Obtener cola de verificación
```
GET /api/queue/verify
GET /api/queue/update
GET /api/queue/report
Headers: X-API-Key: tu-api-key
```

### Aceptar item de la cola
```
POST /api/queue/accept/{levelId}
Headers: X-API-Key: tu-api-key
Body: {
  "levelId": 12345,
  "category": "verify",
  "username": "moderador"
}
```

### Reclamar item de la cola (marcar como en revisión)
```
POST /api/queue/claim/{levelId}
Headers: X-API-Key: tu-api-key
Body: {
  "levelId": 12345,
  "category": "verify",
  "username": "moderador"
}
Response: {
  "success": true,
  "message": "Level claimed successfully",
  "claimedBy": "moderador"
}
```
**Nota:** Este endpoint marca un nivel como "siendo revisado" por un moderador específico. Actualiza el estado en tiempo real para que otros moderadores vean que el nivel está siendo atendido.

### Rechazar item de la cola
```
POST /api/queue/reject/{levelId}
Headers: X-API-Key: tu-api-key
Body: {
  "levelId": 12345,
  "category": "verify",
  "username": "moderador",
  "reason": "Contenido inapropiado"
}
```

### Reportar miniatura
```
POST /api/report/submit
Headers: X-API-Key: tu-api-key
Body: {
  "levelId": 12345,
  "username": "reportador",
  "note": "Razón del reporte"
}
```

### Health check
```
GET /health
```

## Estructura de datos

### Item en cola (KV)
```json
{
  "levelId": 12345,
  "category": "verify",
  "submittedBy": "username",
  "timestamp": 1699900000000,
  "status": "pending",
  "claimedBy": "moderador",
  "claimedAt": "2024-11-21T10:30:00.000Z",
  "note": "Detalles adicionales"
}
```
**Campos opcionales:**
- `claimedBy`: Usuario moderador que está revisando el nivel
- `claimedAt`: Timestamp de cuando fue reclamado
- `status`: `"pending"` (sin reclamar) o `"claimed"` (siendo revisado)

### Metadatos en R2
```json
{
  "uploadedBy": "username",
  "uploadedAt": "2024-11-13T10:30:00.000Z",
  "originalFormat": "png"
}
```

## Optimizaciones

### Conversión WebP
- Calidad: 85%
- Dimensiones: 480x270 (escala proporcional)
- Ahorro promedio: 60-80% vs PNG

### Cache
- Headers: `Cache-Control: public, max-age=31536000`
- Miniaturas cacheadas por 1 año
- Invalidación manual al rechazar

### Límites
- Tamaño máximo: 10MB por imagen
- Formato soportado: PNG (entrada)
- Formato almacenado: WebP
- Formato de salida: WebP o PNG (según petición)

## Monitoreo

Ver logs en tiempo real:
```bash
npm run tail
```

Ver logs en dashboard de Cloudflare:
https://dash.cloudflare.com → Workers & Pages → paimon-thumbnails-server → Logs

## Costos (Cloudflare Free tier)

- R2 Storage: 10 GB gratis
- R2 Operations: 1M writes/month, 10M reads/month gratis
- Workers: 100,000 requests/day gratis
- KV: 100,000 reads/day, 1,000 writes/day gratis

**Estimación:** Para uso moderado del mod, el plan Free es suficiente.

## Seguridad

- ✅ Autenticación por API Key
- ✅ Validación de tamaño de archivo
- ✅ CORS configurado
- ✅ Headers de seguridad
- ✅ Rate limiting (via Cloudflare)

## Troubleshooting

### Error: "Unauthorized"
- Verifica que el API Key en `wrangler.toml` coincida con el del mod
- Header requerido: `X-API-Key: tu-api-key`

### Error: "File too large"
- El límite por defecto es 10MB
- Ajusta `MAX_UPLOAD_SIZE` en `wrangler.toml`

### Conversión WebP no funciona
- Cloudflare Image Resizing requiere un plan de pago
- Alternativa: El servidor guarda el PNG tal cual y lo sirve como WebP en el header
- Para conversión real, considera usar un servicio externo (Cloudinary, imgix)

## Actualizar lista de moderadores

```bash
wrangler kv:key put --binding=QUEUE_KV "moderators" '["mod1","mod2","mod3"]'
```

## Migración desde servidor anterior

Si tenías thumbnails en otro servidor:

1. Exporta todos los archivos PNG
2. Conviértelos a WebP localmente (opcional)
3. Súbelos a R2 usando Wrangler:

```bash
wrangler r2 object put paimon-thumbnails/thumbnails/12345.webp --file=12345.webp
```

## Soporte

Para problemas o preguntas sobre el servidor:
1. Revisa los logs con `npm run tail`
2. Verifica el dashboard de Cloudflare
3. Consulta la documentación de Cloudflare Workers

## Licencia

MIT
