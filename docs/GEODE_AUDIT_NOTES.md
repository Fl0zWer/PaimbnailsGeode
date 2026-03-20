# Geode Audit Notes (Paimbnails)

## Alcance de esta iteracion

- Basado en tutoriales/clases de Geode: `modify`, `hookpriority`, `memory`, `async`, `fetch`, `logging`, `fields`.
- Se fortalecio seguridad de debug sin remover la API key embebida, por requerimiento explicito del proyecto.

## Cambios aplicados

- Lifecycle de workers:
  - `src/features/thumbnails/services/ThumbnailLoader.cpp/.hpp`
  - `src/features/thumbnails/services/LocalThumbs.cpp/.hpp`
  - Se removio el uso de `detach()` en loaders criticos y se agrego espera ordenada de workers en shutdown.
- Debug gating sin hash hardcodeado:
  - `src/core/DebugSettings.cpp`
  - `enable-debug-logs` ahora valida un token via launch arg (`debug-token`) y el valor ingresado en setting.
- Hook defensivo:
  - `src/hooks/DynamicPopupHook.cpp`
  - `worldToMLParent` ahora valida `m_mainLayer` nulo antes de usarlo.
- Node IDs con fallback:
  - `src/hooks/LevelSearchLayer.cpp`, `src/hooks/PauseLayer.cpp`, `src/hooks/LeaderboardsLayer.cpp`
  - Se agregaron rutas fallback cuando no existe el nodo por ID.
- Ownership de requests por UI:
  - `src/utils/WebHelper.hpp`, `src/features/community/ui/CommunityHubLayer.*`, `src/features/moderation/ui/ModeratorsLayer.*`
  - Se agrego `dispatchOwned` con `TaskHolder` para cancelar requests al destruir la capa.
- Hardening de callbacks UI:
  - `src/features/capture/ui/CaptureEditPopup.cpp`
  - `src/features/moderation/ui/SetDailyWeeklyPopup.cpp`
  - `src/features/transitions/ui/CustomTransitionEditorPopup.cpp`
  - Migracion de callbacks tardios a `WeakRef`.
- Robustez de captura / edit_layers:
  - `src/features/capture/ui/CapturePreviewPopup.*`
  - `src/features/capture/ui/CaptureEditPopup.*`
  - `src/features/capture/ui/CaptureLayerEditorPopup.*`
  - `src/hooks/PauseLayer.cpp`
  - `src/utils/FileDialog.cpp`
  - `src/hooks/PlayLayer.cpp`
  - Se cerro el gap de cierres por `back/escape`, se protegieron callbacks de selector de archivos en main thread, y se agrego watchdog de UI para evitar overlays/menu atascados.
- Interoperabilidad de hook de musica:
  - `src/hooks/LevelSelectLayer.cpp`
  - Se agrego `music-hook-passthrough` (saved value) para no bloquear flujo vanilla cuando se requiera compatibilidad.
- Red estandarizada:
  - `src/utils/WebHelper.hpp`
  - `dispatch` ahora usa `send(method, url)` (GET/POST/PUT/PATCH), normaliza metodo y nombra tareas async.
- Transporte HTTP:
  - `src/framework/net/HttpTransport.hpp`
  - Se agrego `userAgent`, `acceptEncoding`, y se corrigio parseo de headers con trim en request binario.
- Versionado:
  - `mod.json` y `CMakeLists.txt` alineados a `2.3.5`.
- Optimizacion multimedia (pasada profunda):
  - `src/features/pet/services/PetManager.*`
  - `src/utils/AnimatedGIFSprite.*`
  - `src/features/profiles/services/ProfileThumbs.*`
  - `src/features/capture/ui/CapturePreviewPopup.cpp`
  - `src/features/thumbnails/services/ThumbnailLoader.*`
  - `src/hooks/LevelCell.cpp`
  - `src/hooks/ProfilePage.cpp`
  - Se agrego cleanup explicito de textura compartida del pet en shutdown, worker GIF con ciclo de vida controlado (sin `detach`), encapsulacion de escrituras async para perfil/captura, limite LRU por bytes para cache de thumbnails, y throttling de hover GIF en `LevelCell` para reducir jank.
  - Se reforzo fallback de localizacion de popup/menu en rutas de UI de imagen critica.

## Riesgos abiertos / pendientes

- Metadata de distribucion: falta `logo.png` en raiz para release.
- Existen dos capas HTTP (`HttpClient` y `HttpTransport`) que aun requieren consolidacion completa.
- No hay adopcion amplia de `async::TaskHolder` para cancelacion ligada a ciclo de vida de UI.
