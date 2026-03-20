# QA + Logging Checklist

## Logging (Geode)

- `error`: fallo funcional sin recuperacion.
- `warn`: fallo recuperable o degradacion temporal.
- `info`: hitos de flujo (inicio/cierre/migraciones).
- `debug`: diagnostico detallado, solo bajo gate de debug.

## Gate de debug

- Requiere launch arg de mod `debug-token`.
- Requiere matching con setting `debug-password`.
- Si falla validacion, se desactiva automaticamente `enable-debug-logs`.

## Regresion manual minima

- `MenuLayer`: carga inicial sin freeze, botones funcionales.
- `LevelInfoLayer`: thumbnails/fondo y transiciones correctas.
- `PlayLayer` + `PauseLayer`: captura de thumbnail por boton y keybind.
- `ProfilePage`: carga de imagen/fondo sin leaks evidentes.
- Cierre del juego: sin crashes en salida y limpieza de cache estable.

## Red y async

- Verificar timeout y manejo de HTTP != 2xx.
- Confirmar callbacks de UI solo en main thread.
- Confirmar que requests no bloquean thread principal.

## Ejecucion (pasada profunda)

- Checklist estatico completado sobre hooks, lifecycle y rutas async/web.
- Se verifico:
  - Sin `detach()` en `src/features/thumbnails/services`.
  - Requests GDBrowser de UI con `TaskHolder` owner-aware.
  - Timeouts explicitos en capas de community/moderation.
  - Fallbacks de `getChildByID` agregados en hooks criticos.
  - Callbacks de dialogo tardio migrados a `WeakRef`.
- Nota: validacion jugable/manual en cliente GD queda pendiente por entorno headless.

## Captura / Edit Layers (robustez)

- Abrir `CapturePreviewPopup` y pulsar `edit_layers`; cerrar con boton X, `Esc` y back: el preview debe seguir aceptando toques.
- Desde `CaptureEditPopup`, abrir `CaptureLayerEditorPopup` y cerrarlo con `Done`, con X y con `Esc`: no debe quedar bloqueado `m_childPopupOpen`.
- Ejecutar recapture repetida (3-5 veces) mientras se alterna ocultar P1/P2: la UI debe restaurarse siempre.
- Forzar fallo/cancelacion de file dialog: no debe quedar `m_fileDialogOpen` atascado.
- Iniciar captura y esperar watchdog (sin callback): overlay/menu deben recuperarse automaticamente.

## Multimedia / GIF (pasada optimizacion)

- `LevelCell`: validar scroll rapido de lista (100+ celdas) sin stutter notable ni congelamientos al hover.
- `LevelCell`: verificar que chequeo hover GIF siga estable en Windows (toggle static/GIF) y no genere spam de `setTexture` por frame.
- `ProfilePage`: abrir/cerrar perfiles de usuarios distintos (50+) y confirmar que cache de `profileimg` se mantiene acotada.
- `CapturePreviewPopup`: descargar varias capturas seguidas y cerrar popup durante guardado; no debe haber crash ni callbacks sobre UI destruida.
- `Exit`: cerrar juego durante descargas/decodificaciones GIF y confirmar salida limpia sin hilos detached vivos.
