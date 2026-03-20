# Hook Priority Matrix

## Regla general

- Preferir `setHookPriorityAfterPost` / `BeforePost` / `AfterPre` / `BeforePre`.
- Evitar aritmetica con prioridades numericas para mejorar compatibilidad entre mods.

## Hooks clave auditados

- `CCMenuItemSpriteExtra::activate` (`src/hooks/DynamicPopupHook.cpp`)
  - `Pre + First`
  - Motivo: capturar origen del boton antes de cualquier mutacion de otros hooks.

- `FLAlertLayer::show` (`src/hooks/DynamicPopupHook.cpp`)
  - `Post + Late`
  - Motivo: aplicar animacion despues de que la capa base y hooks tempranos inicialicen nodos.

- `FLAlertLayer::keyBackClicked` (`src/hooks/DynamicPopupHook.cpp`)
  - `Post + Late`
  - Motivo: salida custom sin romper handlers previos.

- `LevelCell::loadFromLevel` / `LevelCell::loadCustomLevelCell` (`src/hooks/LevelCell.cpp`)
  - `AfterPost geode.node-ids`
  - Motivo: la personalizacion de nodos multimedia depende de IDs ya asignados por `geode.node-ids`.

- `LevelInfoLayer::init` (`src/hooks/LevelInfoLayer.cpp`)
  - `AfterPost geode.node-ids`
  - Motivo: fondos de thumbnail/GIF y menus extra usan Node IDs con fallback.

## Checklist por hook nuevo

- Declara prioridad explicitamente en `onModify`.
- Documenta por que es `Pre` o `Post`.
- Indica si llama a original siempre, condicional o nunca.
- Agrega guards nulos para nodos (`m_mainLayer`, `getParent`, `getChildByID`).
