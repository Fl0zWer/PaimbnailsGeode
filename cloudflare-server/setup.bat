@echo off
REM Script para configurar el servidor de Cloudflare Workers en Windows

echo ==========================================
echo Configuracion de Paimon Thumbnails Server
echo ==========================================
echo.

REM Verificar que Node.js este instalado
where node >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Node.js no esta instalado
    echo Por favor instala Node.js desde https://nodejs.org/
    pause
    exit /b 1
)
echo [OK] Node.js instalado
echo.

REM Verificar que Wrangler este instalado
where wrangler >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [INFO] Wrangler no esta instalado
    echo Instalando Wrangler globalmente...
    call npm install -g wrangler
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Fallo la instalacion de Wrangler
        pause
        exit /b 1
    )
)
echo [OK] Wrangler instalado
echo.

REM Instalar dependencias del proyecto
echo [INFO] Instalando dependencias del proyecto...
call npm install
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Fallo la instalacion de dependencias
    pause
    exit /b 1
)
echo [OK] Dependencias instaladas
echo.

REM Login en Cloudflare
echo [INFO] Iniciando sesion en Cloudflare...
echo Se abrira tu navegador para autenticarte
call wrangler login
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Fallo el login en Cloudflare
    pause
    exit /b 1
)
echo [OK] Sesion iniciada en Cloudflare
echo.

REM Crear bucket R2
echo [INFO] Creando bucket R2 para thumbnails...
call wrangler r2 bucket create paimon-thumbnails
echo [OK] Bucket R2 creado: paimon-thumbnails
echo.

REM Crear KV namespace para produccion
echo [INFO] Creando KV namespace (produccion)...
call wrangler kv:namespace create "QUEUE_KV" > kv_temp.txt
for /f "tokens=3 delims==" %%a in ('findstr "id" kv_temp.txt') do set QUEUE_KV_ID=%%a
set QUEUE_KV_ID=%QUEUE_KV_ID:"=%
set QUEUE_KV_ID=%QUEUE_KV_ID: =%
echo [OK] KV namespace creado: %QUEUE_KV_ID%
del kv_temp.txt
echo.

REM Crear KV namespace para preview (desarrollo)
echo [INFO] Creando KV namespace (preview)...
call wrangler kv:namespace create "QUEUE_KV" --preview > kv_temp.txt
for /f "tokens=3 delims==" %%a in ('findstr "id" kv_temp.txt') do set QUEUE_KV_PREVIEW_ID=%%a
set QUEUE_KV_PREVIEW_ID=%QUEUE_KV_PREVIEW_ID:"=%
set QUEUE_KV_PREVIEW_ID=%QUEUE_KV_PREVIEW_ID: =%
echo [OK] KV namespace preview creado: %QUEUE_KV_PREVIEW_ID%
del kv_temp.txt
echo.

REM Actualizar wrangler.toml
echo [INFO] Actualizando wrangler.toml con los IDs de KV...
powershell -Command "(gc wrangler.toml) -replace 'id = \"your-kv-namespace-id\"', 'id = \"%QUEUE_KV_ID%\"' | Out-File -encoding ASCII wrangler.toml"
powershell -Command "(gc wrangler.toml) -replace 'id = \"your-dev-kv-namespace-id\"', 'id = \"%QUEUE_KV_PREVIEW_ID%\"' | Out-File -encoding ASCII wrangler.toml"
powershell -Command "(gc wrangler.toml) -replace 'id = \"your-prod-kv-namespace-id\"', 'id = \"%QUEUE_KV_ID%\"' | Out-File -encoding ASCII wrangler.toml"
echo [OK] wrangler.toml actualizado
echo.

REM Configurar moderadores
echo [INFO] Configurando lista inicial de moderadores...
set /p MODERATORS="Ingresa nombres de moderadores separados por comas (ej: user1,user2): "
if "%MODERATORS%"=="" set MODERATORS=admin

REM Convertir a formato JSON (simplificado)
set MODS_JSON=["%MODERATORS:,=","%"]
call wrangler kv:key put --binding=QUEUE_KV "moderators" "%MODS_JSON%"
echo [OK] Moderadores configurados: %MODS_JSON%
echo.

REM Configurar variables de entorno
echo [INFO] Configurando variables de entorno...
if not exist .dev.vars (
    copy .dev.vars.example .dev.vars
    echo [OK] Archivo .dev.vars creado
    echo [IMPORTANTE] Edita .dev.vars y actualiza tu API_KEY
) else (
    echo [INFO] El archivo .dev.vars ya existe
)
echo.

REM Instrucciones finales
echo ==========================================
echo [OK] Configuracion completada
echo ==========================================
echo.
echo Proximos pasos:
echo.
echo 1. Edita wrangler.toml si necesitas cambiar configuraciones
echo 2. Edita .dev.vars y actualiza tu API_KEY
echo 3. Ejecuta 'npm run dev' para desarrollo local
echo 4. Ejecuta 'npm run deploy' para desplegar a produccion
echo.
echo Tu worker estara disponible en:
echo https://paimon-thumbnails-server.^<tu-subdomain^>.workers.dev
echo.
echo Endpoints principales:
echo - POST /mod/upload - Subir miniaturas
echo - GET /api/download/{levelId}.webp - Descargar miniaturas
echo - GET /api/queue/verify - Ver cola de verificacion
echo - POST /api/queue/accept/{levelId} - Aceptar miniatura
echo - POST /api/queue/reject/{levelId} - Rechazar miniatura
echo - POST /api/report/submit - Reportar miniatura
echo.
echo Para mas informacion, consulta README.md
echo.
pause
