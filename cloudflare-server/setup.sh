#!/bin/bash

# Script para configurar el servidor de Cloudflare Workers

echo "=========================================="
echo "Configuración de Paimon Thumbnails Server"
echo "=========================================="
echo ""

# Verificar que Wrangler esté instalado
if ! command -v wrangler &> /dev/null; then
    echo "❌ Wrangler no está instalado"
    echo "Instalando Wrangler globalmente..."
    npm install -g wrangler
fi

echo "✅ Wrangler instalado"
echo ""

# Instalar dependencias del proyecto
echo "📦 Instalando dependencias..."
npm install
echo "✅ Dependencias instaladas"
echo ""

# Login en Cloudflare
echo "🔐 Iniciando sesión en Cloudflare..."
wrangler login
echo ""

# Crear bucket R2
echo "🪣 Creando bucket R2 para thumbnails..."
wrangler r2 bucket create paimon-thumbnails
echo "✅ Bucket R2 creado: paimon-thumbnails"
echo ""

# Crear KV namespace para producción
echo "💾 Creando KV namespace (producción)..."
QUEUE_KV_ID=$(wrangler kv:namespace create "QUEUE_KV" | grep -o 'id = "[^"]*"' | cut -d'"' -f2)
echo "✅ KV namespace creado: $QUEUE_KV_ID"
echo ""

# Crear KV namespace para preview (desarrollo)
echo "💾 Creando KV namespace (preview)..."
QUEUE_KV_PREVIEW_ID=$(wrangler kv:namespace create "QUEUE_KV" --preview | grep -o 'id = "[^"]*"' | cut -d'"' -f2)
echo "✅ KV namespace preview creado: $QUEUE_KV_PREVIEW_ID"
echo ""

# Actualizar wrangler.toml con los IDs
echo "📝 Actualizando wrangler.toml con los IDs de KV..."
sed -i.bak "s/id = \"your-kv-namespace-id\"/id = \"$QUEUE_KV_ID\"/g" wrangler.toml
sed -i.bak "s/id = \"your-dev-kv-namespace-id\"/id = \"$QUEUE_KV_PREVIEW_ID\"/g" wrangler.toml
sed -i.bak "s/id = \"your-prod-kv-namespace-id\"/id = \"$QUEUE_KV_ID\"/g" wrangler.toml
rm wrangler.toml.bak
echo "✅ wrangler.toml actualizado"
echo ""

# Crear lista inicial de moderadores
echo "👮 Configurando lista inicial de moderadores..."
read -p "Ingresa nombres de moderadores separados por comas (ej: user1,user2): " MODERATORS

if [ -z "$MODERATORS" ]; then
    MODERATORS="admin"
fi

# Convertir a formato JSON array
MODS_ARRAY="["
IFS=',' read -ra ADDR <<< "$MODERATORS"
for i in "${ADDR[@]}"; do
    i=$(echo "$i" | xargs) # trim whitespace
    MODS_ARRAY+="\"$i\","
done
MODS_ARRAY="${MODS_ARRAY%,}]" # remove trailing comma and close array

wrangler kv:key put --binding=QUEUE_KV "moderators" "$MODS_ARRAY"
echo "✅ Moderadores configurados: $MODS_ARRAY"
echo ""

# Configurar variables de entorno para desarrollo
echo "🔧 Configurando variables de entorno..."
if [ ! -f .dev.vars ]; then
    cp .dev.vars.example .dev.vars
    echo "✅ Archivo .dev.vars creado"
    echo "⚠️  IMPORTANTE: Edita .dev.vars y actualiza tu API_KEY"
else
    echo "ℹ️  El archivo .dev.vars ya existe"
fi
echo ""

# Instrucciones finales
echo "=========================================="
echo "✅ Configuración completada"
echo "=========================================="
echo ""
echo "Próximos pasos:"
echo ""
echo "1. Edita wrangler.toml si necesitas cambiar configuraciones"
echo "2. Edita .dev.vars y actualiza tu API_KEY"
echo "3. Ejecuta 'npm run dev' para desarrollo local"
echo "4. Ejecuta 'npm run deploy' para desplegar a producción"
echo ""
echo "Tu worker estará disponible en:"
echo "https://paimon-thumbnails-server.<tu-subdomain>.workers.dev"
echo ""
echo "Endpoints principales:"
echo "- POST /mod/upload - Subir miniaturas"
echo "- GET /api/download/{levelId}.webp - Descargar miniaturas"
echo "- GET /api/queue/verify - Ver cola de verificación"
echo "- POST /api/queue/accept/{levelId} - Aceptar miniatura"
echo "- POST /api/queue/reject/{levelId} - Rechazar miniatura"
echo "- POST /api/report/submit - Reportar miniatura"
echo ""
echo "Para más información, consulta README.md"
echo ""
