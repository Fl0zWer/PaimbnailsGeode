# Guía para Actualizar el Mod en el Servidor

## Subir nueva versión del mod

### 1. Preparar el archivo .geode

Después de compilar el mod, tendrás el archivo en:
```
build/paimon.level_thumbnails.geode
```

### 2. Subir a Cloudflare R2

**Opción A: Usando la interfaz web de Cloudflare**

1. Ve a https://dash.cloudflare.com
2. Selecciona tu cuenta
3. Ve a "R2" en el menú lateral
4. Selecciona tu bucket: `paimon-thumbnails`
5. Navega a la carpeta `mod-releases/` (o créala si no existe)
6. Sube el archivo `paimon.level_thumbnails.geode`
7. Verifica que la ruta completa sea: `mod-releases/paimon.level_thumbnails.geode`

**Opción B: Usando Wrangler CLI**

```bash
cd cloudflare-server
wrangler r2 object put paimon-thumbnails/mod-releases/paimon.level_thumbnails.geode --file=../build/paimon.level_thumbnails.geode
```

### 3. Actualizar la versión en el servidor

Edita el archivo: `cloudflare-server/src/endpoints/modVersion.js`

```javascript
const CURRENT_VERSION = {
  version: '1.0.2',  // ⬅️ ACTUALIZAR AQUÍ
  downloadUrl: 'https://paimon-thumbnails-server.paimonalcuadrado.workers.dev/downloads/paimon.level_thumbnails.geode',
  changelog: '• Descripción de cambios\n• Más cambios\n• Etc'  // ⬅️ ACTUALIZAR AQUÍ
};
```

### 4. Desplegar el worker actualizado

```bash
cd cloudflare-server
npm run deploy
```

O con wrangler:
```bash
wrangler deploy
```

### 5. Verificar que funcione

**Probar endpoint de versión:**
```bash
curl https://paimon-thumbnails-server.paimonalcuadrado.workers.dev/api/mod/version
```

Debe retornar:
```json
{
  "version": "1.0.2",
  "downloadUrl": "https://...",
  "changelog": "..."
}
```

**Probar descarga:**
```bash
curl -o test.geode https://paimon-thumbnails-server.paimonalcuadrado.workers.dev/downloads/paimon.level_thumbnails.geode
```

## Flujo completo de actualización

1. ✅ Modificar código del mod
2. ✅ Actualizar versión en `mod.json`: `"version": "1.0.2"`
3. ✅ Compilar: `cmake --build build --config RelWithDebInfo`
4. ✅ Subir `.geode` a R2: `mod-releases/paimon.level_thumbnails.geode`
5. ✅ Actualizar `CURRENT_VERSION` en `modVersion.js`
6. ✅ Desplegar worker: `npm run deploy`
7. ✅ Probar en el juego: esperar 3 segundos después de iniciar

## Cómo funciona el sistema de actualización

1. **Al iniciar el juego:**
   - Después de 3 segundos, el mod hace una petición a `/api/mod/version`
   - Compara la versión actual con la versión del servidor
   - Si hay una versión más nueva, muestra un popup

2. **El usuario acepta actualizar:**
   - Se muestra un círculo de carga
   - Se descarga el archivo desde `/downloads/paimon.level_thumbnails.geode`
   - Se guarda como `paimon.level_thumbnails.geode.new`
   - Se renombra el archivo antiguo a `.geode.old`
   - Se instala el nuevo archivo
   - Se muestra popup para reiniciar el juego

3. **El usuario reinicia:**
   - El juego se cierra y se abre automáticamente
   - Se carga la nueva versión del mod

## Versioning

El sistema soporta versiones en formato:
- `1.0.0`
- `1.0.1`
- `2.0.0`
- `1.0.0-beta`
- etc.

La comparación se hace por partes numéricas, por ejemplo:
- `1.0.1` > `1.0.0` ✅
- `1.1.0` > `1.0.9` ✅
- `2.0.0` > `1.9.9` ✅

## Troubleshooting

**Error: "Mod file not found"**
- Verifica que el archivo esté en R2 en la ruta correcta: `mod-releases/paimon.level_thumbnails.geode`

**Error: "Failed to download update"**
- Verifica que el worker tenga permisos de lectura en el bucket R2
- Verifica que la URL de descarga sea correcta

**El popup no aparece:**
- Verifica que hayan pasado 3 segundos desde el inicio del juego
- Verifica que la versión en el servidor sea mayor que la local
- Revisa los logs del juego: `geode/logs/`

**El juego no se reinicia:**
- Esto solo funciona en Windows automáticamente
- En otras plataformas se mostrará un mensaje para reiniciar manualmente
