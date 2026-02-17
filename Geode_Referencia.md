# Geode SDK — Guía/Referencia Completa (ES)

Esta guía es una **referencia práctica** para desarrollar mods con Geode (C++). Está pensada para uso diario: patrones correctos, "pitfalls" típicos, y cómo organizar un mod para que sea **compatible** con otros mods.

La documentación oficial evoluciona: si algo aquí no coincide con tu SDK, prioriza lo que diga `docs.geode-sdk.org`.

> nota: esta guía está escrita para **Geode 4.10.2** (GD **2.2074**), que es lo que usa este repo.

---

## 0) Fuentes oficiales y normas (léelo primero)

### Referencias oficiales (las buenas)

- Tutorials (lista completa): https://docs.geode-sdk.org/tutorials
- Getting started / CLI: https://docs.geode-sdk.org/getting-started/geode-cli
- Config de mods (`mod.json`, recursos, settings, etc): https://docs.geode-sdk.org/mods/configuring
- Guidelines del Index (compatibilidad, seguridad, etc): https://docs.geode-sdk.org/mods/guidelines
- Publicación en el Index: https://docs.geode-sdk.org/mods/publishing

### Releases / changelog del SDK

- Releases oficiales: https://github.com/geode-sdk/geode/releases
    - tip: revisa siempre si tu release dice que es para **GD 2.2074** o si soporta **2.208**; varios releases recientes aclaran esto explícitamente.
    - tip: los `nightly` (pre-release) pueden no instalar resources automáticamente y suelen requerir usar el installer.

## Índice (TOC)

- 0. Fuentes oficiales y normas
- 1. Primeros pasos (CLI / template)
- 2. `mod.json` (configuración del mod)
- 3. Hooking con `$modify`
- 4. `Fields` (estado por instancia)
- 5. Prioridad de hooks
- 6. Manual hooks (cuando `$modify` no alcanza)
- 7. Casting (static vs `typeinfo_cast`)
- 8. Logging
- 9. Settings
- 10. Eventos
- 11. Tasks (async bien hecho)
- 12. Web requests (`web::WebRequest`)
- 13. Memoria (Ref / WeakRef)
- 14. Node IDs / NodeTree
- 15. Layouts
- 16. Positioning (posición, content size, anchor)
- 17. Touch Priority
- 18. Buttons
- 19. Popups (`createQuickPopup` y `geode::Popup`)
- 20. LazySprite (image downloading/loading)
- 21. Recursos (Sprites, Fonts, Audio)
- 22. Dependencias e incompatibilidades
- 23. Save data (no editable por el usuario)
- 24. Utils útiles
- 25. Publicación y guidelines
- 26. Checklist de compatibilidad y pitfalls
- 27. Patrones de UI (recetas)
- 28. Input Handling (Teclado/Mouse)
- 29. Game Managers (Singletons esenciales)
- 30. Audio (SFX y Música)
- 31. Cocos2d Basics (Acciones, Scheduler, Director)
- 32. Math & Colors
- 33. Trabajar con Strings (Textos)
- 34. Debugging (VS Code & launch.json)
- 35. Spritesheets & FrameCache (.plist)
- 36. Notificaciones (geode::Notification)
- 37. JSON Avanzado (matjson)
- 38. Mod Interop (API & Export)
- 39. Errores Comunes (Troubleshooting)
- 40. Custom Events & EventFilter (Avanzado)
- 41. Platform & Utils (Addresser, Web, Clip, File)
- 42. C++ Tips oficiales para Geode
- 43. Corrutinas y $async (C++20)
- 44. Diccionario de Modding (Vocabulario oficial)
- 45. Modificando Geode UI (Logos y botones)
- 46. Colecciones Cocos vs C++ (CCArrayExt)
- 47. Helpers del Juego (Restart, Exit)
- 48. Manipulación de Strings y Rangos
- 49. Manejo de Errores (Result/Err)
- 50. Defines de Plataforma (Windows/Android/Mac)
- 51. Dependencias Avanzadas (Optional & Events)
- 52. Custom Settings en Profundidad (UI y Clases)
- 53. Macro GEODE_EVENT_EXPORT (API Moderna)

---

## 1) Primeros pasos (CLI / template)

- La CLI de Geode es esencial. Úsala siempre.
- Recomendación: usa **CI** (GitHub Actions) para builds.

Comandos clave:
- `geode config setup`: Configura tu entorno (GD path, etc).
- `geode new`: Crea un nuevo proyecto desde el template.
- `geode build`: Compila el mod.
- `geode package`: Empaqueta tu mod en un `.geode`.
- `geode index login`: Loguearse para publicar.

**Nota de estilo**: todos los comentarios en el codigo deben ir en **minusculas** y ser **informales**. nada de formalismos raros.

---

## 2) `mod.json` (configuración del mod)

Archivo vital en la raíz. Define quién eres y qué necesita tu mod.

### Claves requeridas

```json
{
    "geode": "4.10.2",        // versión del sdk que usas
  "gd": {
        "win": "2.2074",        // versión de gd objetivo
        "android": "2.2074"
  },
  "id": "developer.mod-name", // id único (kebab-case, a-z, ., -, _)
  "name": "My Awesome Mod",
    "version": "1.0.0",       // semver
  "developer": "Me"         // o "developers": ["Me", "Friend"]
}
```

### Claves comunes
- `description`: Una frase corta.
- `repository`: Link al repo git (importante para index).
- `tags`: Etiquetas (`interface`, `gameplay`, `utility`, `developer`, etc). Max 4.
- `dependencies`: Otros mods que necesitas (ver sección 22).
- `settings`: Definición de opciones (ver sección 9).
- `resources`: Archivos extra (ver sección 21).

### Archivos Markdown especiales
- `about.md`: Descripción larga (se muestra en el juego).
- `changelog.md`: Historial de cambios.

---

## 3) Hooking con `$modify`

La magia de Geode. Intercepta funciones del juego.

### Patrón básico
```cpp
#include <Geode/modify/MenuLayer.hpp>
using namespace geode::prelude;

class $modify(MyMenuLayer, MenuLayer) {
    // nombre de clase opcional (MyMenuLayer) ayuda a evitar colisiones
    // y permite acceder a miembros estáticos si es necesario.

    bool init() {
        if (!MenuLayer::init()) return false; // llama siempre al original asi

        log::info("menulayer init hookeado");
        return true;
    }
};
```

### Recursión accidental
¡CUIDADO! Nunca llames a `this->init()` dentro del hook de `init`. Eso crea un loop infinito. Usa `ClaseBase::init()`.

### Destructores
Para hookear el destructor, usa una función llamada `destructor()` dentro del `$modify`.
```cpp
void destructor() {
    MenuLayer::~MenuLayer(); // llama al original
    log::info("adios menulayer");
}
```

---

## 4) `Fields` (estado por instancia)

¿Necesitas guardar variables en una clase del juego (ej. `PlayerObject`)? Usa `Fields`.

```cpp
class $modify(PlayerObject) {
    struct Fields {
        int jumps = 0;
        bool isCool = false;
        // los destructores de campos funcionan, útil para limpiar eventos
    };

    void pushButton(PlayerButton btn) {
        m_fields->jumps++;
        log::debug("saltos: {}", m_fields->jumps);
        PlayerObject::pushButton(btn);
    }
};
```

- **Acceso externo**: `static_cast<MyModifyClass*>(player)->m_fields->jumps`.
- **Nota**: Se inicializan "lazy" (al primer acceso).

---

## 5) Prioridad de hooks

Si tu mod choca con otro, ajusta la prioridad en `onModify`.

```cpp
class $modify(MenuLayer) {
    static void onModify(auto& self) {
        // corre antes que otros
        self.setHookPriority("MenuLayer::init", geode::Priority::Early); 
        
        // o corre despues
        self.setHookPriority("MenuLayer::init", geode::Priority::Late);
    }
};
```

---

## 6) Manual hooks

Solo úsalos si no puedes usar `$modify` (ej. funcion sin bindings o dirección a pelo).

```cpp
// ej. hookear por dirección
$execute {
    Mod::get()->hook(
        reinterpret_cast<void*>(base::get() + 0x123456), // address
        &MyHookFunction, // tu funcion
        "NombreFuncion", // nombre display
        tulip::hook::TulipConvention::Thiscall // convención
    );
}
```
Recomendación: Evítalos si es posible.

---

## 7) Casting (static vs `typeinfo_cast`)

- **Sabes el tipo**: `static_cast<CCNode*>(obj)`. Rápido y seguro si estás 100% seguro.
- **No sabes el tipo**: `typeinfo_cast<CCNode*>(obj)`.
    - Geode reimplementa `dynamic_cast` porque en Windows/Android el estándar `dynamic_cast` falla entre módulos (DLLs).
    - Úsalo siempre para chequear tipos desconocidos (ej. `sender` en callbacks).

```cpp
void onCallback(CCObject* sender) {
    if (auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
        // es un botón seguro
    }
}
```

---

## 8) Logging

Usa `fmt` syntax.
```cpp
log::info("cargado mod version {}", "1.0.0");
log::error("falló algo: {}", error_code);
log::debug("variable x es {}", x); // solo en builds debug
log::warn("cuidado con esto");
```

---

## 9) Settings

Geode tiene un sistema de settings robusto.

### Definición en `mod.json`
```json
"settings": {
    "my-toggle": {
        "type": "bool",
        "name": "Activar Cosa",
        "default": true
    },
    "my-slider": {
        "type": "float",
        "default": 1.0,
        "min": 0.0,
        "max": 2.0,
        "control": { "slider": true }
    },
    "my-color": {
        "type": "rgb",
        "default": "#ff0000"
    }
}
```
Tipos soportados: `bool`, `int`, `float`, `string`, `file`, `folder`, `color` (`rgb` / `rgba`), `title` (solo visual).

### Uso en código
```cpp
// leer
bool encendido = Mod::get()->getSettingValue<bool>("my-toggle");

// escuchar cambios
$execute {
    listenForSettingChanges("my-toggle", [](bool val) {
        log::info("nuevo valor: {}", val);
    });
}
```

### Custom Settings
Puedes crear tipos de settings personalizados definiendo clases que hereden de `SettingV3` y `SettingNodeV3`, y registrándolos con `registerCustomSettingType`. Útil para UIs complejas en las opciones.

---

## 10) Eventos

Alternativa moderna a los "delegates" de Cocos.

### Usar eventos globales
```cpp
// escuchar un evento (ej. hipotético DragDrop)
new EventListener<EventFilter<DragDropEvent>>(+[](DragDropEvent* ev) {
    // manejar evento
    return ListenerResult::Propagate;
});
```

### EventListener en Nodos
Si quieres que el listener muera con el nodo, usa `addEventListener`:
```cpp
node->addEventListener<MyEventFilter>([](MyEvent* ev) {
    // ...
});
```
O pon el `EventListener` en un `struct Fields` de la clase.

---

## 11) Tasks (async bien hecho)

Para no congelar el juego en operaciones largas (cálculos, I/O).

Referencia oficial: https://docs.geode-sdk.org/tutorials/tasks

```cpp
Task<int> calcularAlgo() {
    return Task<int>::run([](auto progress, auto hasBeenCancelled) -> Task<int>::Result {
        // codigo en otro thread
        if (hasBeenCancelled()) return Task<int>::Cancel();
        return 42;
    });
}

// uso
m_listener.bind([](Task<int>::Event* e) {
    if (auto val = e->getValue()) {
        log::info("resultado: {}", *val);
    }
});
m_listener.setFilter(calcularAlgo());
```

---

## 12) Web requests (`web::WebRequest`)

Asíncrono por defecto (devuelve un `WebTask`, alias de `Task<WebResponse, WebProgress>`).

Referencia oficial: https://docs.geode-sdk.org/tutorials/fetch

```cpp
web::WebRequest req;
req.bodyString("datos");
req.header("Content-Type", "application/json");

m_listener.setFilter(req.post("https://api.sitio.com"));

m_listener.bind([](web::WebTask::Event* e) {
    if (auto res = e->getValue()) {
        if (res->ok()) log::info("respuesta: {}", res->string().unwrapOr(""));
    }
});
```
**Importante**: El listener debe persistir (campo de clase o `Ref`), si se destruye, la request se cancela.

---

## 13) Memoria (Ref / WeakRef)

Cocos usa contadores de referencia (retain/release).

- **`Ref<T>`**: "Smart pointer" que hace `retain` al crearse y `release` al destruirse.
    - Úsalo para miembros de clase (`Ref<CCNode> myNode`). Te salva de escribir destructores manuales.
- **`WeakRef<T>`**: Referencia débil. No incrementa el contador.
    - Úsalo para guardar referencias a nodos que podrían morir (ej. en un mapa global `std::map<WeakRef<CCNode>, Data>`).
    - Accede con `.lock()`.

---

## 14) Node IDs / NodeTree

**¡NO USES ÍNDICES HARDCODEADOS!**
Geode asigna IDs de string a los nodos de RobTop (ej. `main-menu`, `profile-button`).

```cpp
// SI
auto menu = this->getChildByID("center-menu");

// NO (prohibido)
auto menu = this->getChildren()->objectAtIndex(3); 
```

- Usa el mod **DevTools** para ver los IDs en vivo.
- Ponle IDs a tus propios nodos: `btn->setID("my-button");`.

---

## 15) Layouts

Sistema CRUCIAL para compatibilidad. Organiza nodos automáticamente sin coordenadas absolutas.

### Tipos principales

**1. RowLayout / ColumnLayout** (Heredan de `AxisLayout`)
Organizan hijos en fila o columna.
```cpp
auto layout = RowLayout::create();
layout->setGap(5.f); // espacio entre elementos
layout->setGrowCrossAxis(true); // si true, rompe linea si no cabe
layout->setCrossAxisOverflow(false); // si true, centra las lineas extra
menu->setLayout(layout);
```

**2. AnchorLayout**
Posiciona elementos relativos a los bordes o centro (TopLeft, BottomRight, etc).
```cpp
menu->setLayout(AnchorLayout::create());
// Configurar cada hijo
btn->setLayoutOptions(
    AnchorLayoutOptions::create()
        ->setAnchor(Anchor::TopRight)
        ->setOffset({-10.f, -10.f}) // margen
);
```

### LayoutOptions en Hijos
Si usas `AxisLayout`, puedes configurar comportamientos específicos en los hijos:
```cpp
auto opts = AxisLayoutOptions::create();
opts->setBreakLine(true); // fuerza salto de linea antes de este nodo
opts->setSameLine(true); // fuerza quedarse en la linea
opts->setScaleIndependent(true); // ignora escala del nodo en calculos
node->setLayoutOptions(opts);
```

### Flujo de trabajo
1. Crear contenedor (Menu/Layer).
2. Setear Layout (`setLayout`).
3. Añadir hijos (`addChild`).
4. (Opcional) Setear `LayoutOptions` en hijos.
5. **IMPORTANTE**: `node->updateLayout()` al final.

---

## 16) Positioning

- **Position**: Relativa al padre. `screenSize / 2` es el centro.
- **Content Size**: Tamaño del área del nodo.
- **Anchor Point**: Punto de pivote (0.0 a 1.0).
    - `0.5, 0.5`: Centro (default para Sprites).
    - `0, 0`: Esquina inferior izquierda.
    - **No uses** `ignoreAnchorPointForPosition(true)` en tus nodos a menos que sepas *muy* bien qué haces.

---

## 17) Touch Priority

- GD usa prioridades **inversas** (menor numero = mayor prioridad).
- Menu estándar: -128 (o similar).
- `CCTouchDispatcher::get()->addTargetedDelegate(...)`.
- `geode::cocos::handleTouchPriority(this)` recursivo.

---

## 18) Buttons

```cpp
auto spr = ButtonSprite::create("Texto"); // o CircleButtonSprite
auto btn = CCMenuItemSpriteExtra::create(
    spr,
    this,
    menu_selector(MyLayer::onBtn) // macro obligatoria
);
```

- Pasando data extra: `btn->setTag(1)` (enteros) o `btn->setUserObject(...)` (objetos).

---

## 19) Popups

### Simple (Confirmación)
```cpp
createQuickPopup("Titulo", "Contenido", "No", "Si", [](auto, bool btn2) {
    if (btn2) log::info("dijo que si");
});
```

### Complejo (`geode::Popup`)
Maneja automáticamente el setup de `FLAlertLayer`, fondo, botón cerrar y touch priority.

Patrón recomendado (según docs oficiales):
```cpp
class MyPopup : public geode::Popup {
public:
    static MyPopup* create(std::string value) {
        auto ret = new MyPopup;
        if (ret->init(std::move(value))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

protected:
    bool init(std::string value) {
        if (!Popup::init(320.f, 160.f)) return false;
        this->setTitle("Mi Popup");
        // m_mainLayer es donde pones tus cosas
        return true;
    }
};
```
**Nota sobre `FLAlertLayer`**: Si lo creas en un `init()`, asigna `alert->m_scene = this;` antes de `show()`, o saldrá en la escena anterior.

---

## 20) LazySprite (Imágenes web/disco)

Para cargar imágenes sin congelar el juego.
```cpp
auto spr = LazySprite::create({50, 50});
spr->loadFromUrl("https://sitio.com/imagen.png");
```

---

## 21) Recursos

Pon tus assets en una carpeta (ej. `resources/`) y lístalos en `mod.json`.
- **Sprites**: Geode maneja las calidades (HD/uhd). Usa el sufijo literal `_spr`:
    - `CCSprite::create("mi-imagen.png"_spr);` -> carga `id.mod/mi-imagen.png`.
- **Spritesheets**: Defínelos en `mod.json`. Mejor performance.
- **Fuentes**: `.ttf` en `resources`, Geode las convierte a bitmap fonts.

---

## 22) Dependencias e incompatibilidades

En `mod.json`:
- `"dependencies": { "geode.node-ids": "required" }`
- `"incompatibilities": { "otro.mod": "breaking" }`

Si es dependencia opcional, usa `Loader::get()->isModLoaded("id")` y `getSavedValue` para comunicarte, o eventos dinámicos.

---

## 23) Save data

Guardar datos internos (no visibles en settings).
```cpp
Mod::get()->setSavedValue("mis-puntos", 100);
int pts = Mod::get()->getSavedValue<int>("mis-puntos");
```
Soporta structs custom si implementas `matjson::Serialize`.

---

## 24) Utils útiles

Explora `geode::utils`:
- `file`: leer/escribir string, json, directorios, abrir carpeta file explorer.
- `clipboard`: leer/escribir.
- `web`: abrir url en navegador (`web::openLinkInBrowser`).
- `cocos`: helpers para buscar nodos hijos, convertir tipos de color.
- `ranges`: utilidades tipo LINQ para vectores.

---

## 25) Publicación y guidelines

1. Sube tu código (GitHub).
2. Crea `release` en GitHub con el `.geode`.
3. `geode index mods create`.
4. Espera aprobación.

**Guidelines**:
- No crashees el juego (obvio).
- No rompas otros mods (usa Node IDs y Layouts).
- Open source obligatorio (o source verification privada).
- No malware/rat (obvio x2).

Checklist rápido (segun docs oficiales):
- evita **excepciones** en runtime (usa alternativas que no tiren, o overloads con `std::error_code`)
- evita `dynamic_cast` (usa `typeinfo_cast`)
- evita bloquear el main thread (web/IO siempre async)
- no llames `unwrap()` si no chequeaste `ok()` / `isOk()` antes (esto es red flag fuerte en el index)
- si usas `std::filesystem`, evita `path.string()` en windows/unicode; usa `geode::utils::string::pathToString`

---

## 26) Checklist de compatibilidad y pitfalls

- [ ] ¿Usaste `getChildByID` en vez de `objectAtIndex`?
- [ ] ¿Usaste `Layouts` al añadir botones a menús existentes?
- [ ] ¿Llamaste a `BaseClass::init()` en tus hooks?
- [ ] ¿Usaste `typeinfo_cast` en vez de `dynamic_cast`?
- [ ] ¿Tus variables estáticas están gestionadas (o mejor, usas `Fields`)?
- [ ] ¿Hiciste `updateLayout()` después de tocar un menú ajeno?
- [ ] ¿Evitaste `unwrap()` sin `ok()` (sobre todo en web / json)?
- [ ] ¿Evitaste `std::filesystem::path::string()` y excepciones?

---

## 27) Patrones de UI (recetas)

### Añadir botón a menú existente
1. `getChildByID` para buscar el menú.
2. Crear botón (`CCMenuItemSpriteExtra`).
3. `menu->addChild(btn)`.
4. `btn->setID("mi-boton")`.
5. `menu->updateLayout()`.

### Crear menú propio
1. `auto menu = CCMenu::create();`
2. `menu->setLayout(RowLayout::create());`
3. `menu->setContentSize(...)` (importante si usas layouts).
4. `menu->setPosition(...)`.
5. Añadir items.
6. `menu->updateLayout()`.

### Loading spinner
Usa `LazySprite` o crea un `LoadingCircle` y manéjalo con un `Task` que al terminar lo quite y ponga el contenido real.

---

## 28) Input Handling

Geode maneja inputs a través de cocos2d.

### Teclado
Para capturar teclas en tu layer:
```cpp
class $modify(MyLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        this->setKeyboardEnabled(true);
        return true;
    }

    // Tecla presionada
    void keyDown(enumKeyCodes key) {
        if (key == enumKeyCodes::KEY_Space) {
            log::debug("espacio presionado");
        }
        MenuLayer::keyDown(key);
    }

    // Back / Escape (Android/PC)
    void keyBackClicked() {
        MenuLayer::keyBackClicked();
        // cerrar popup o volver atras
    }
};
```

### Mouse
Los layers suelen recibir touch, no mouse directo. Si necesitas coordenadas de mouse:
```cpp
auto mousePos = cocos2d::getMousePos(); // relativo a pantalla
auto nodeSpace = node->convertToNodeSpace(mousePos); // relativo a nodo
```

---

## 29) Game Managers (Singletons esenciales)

Estos son los "dioses" del juego. Accede a ellos con `::sharedState()` o `::get()`.

### `GameManager`
Gestiona opciones, datos de usuario, y estado global.
```cpp
auto gm = GameManager::sharedState();
auto playLayer = gm->getPlayLayer(); // puede ser null
auto editorLayer = gm->getEditorLayer(); // puede ser null
bool fullscreen = gm->getGameVariable("0028"); // ejemplo variables internas
```

### `GameLevelManager` (o `LocalLevelManager`)
Maneja niveles online y locales.
```cpp
// Descargar nivel
GameLevelManager::sharedState()->downloadLevel(123456, false);
```

### `PlayLayer` / `LevelEditorLayer`
No son singletons persé, pero suelen tener métodos helper en mods.
Si estás en `PlayLayer`, `this->m_level` es el `GJGameLevel` actual.

---

## 30) Audio

Usamos `FMODAudioEngine` (wrapper de Geometry Dash sobre FMOD).

```cpp
auto engine = FMODAudioEngine::sharedEngine();

// Sonido (SFX)
engine->playEffect("playSound_01.ogg"); 
engine->playEffect("mi-sonido.mp3"_spr); // Usando recurso del mod

// Música
engine->playMusic("music.mp3", true, 0.0f, 1);
```

---

## 31) Cocos2d Basics

Funciones "vanilla" de Cocos que usarás todo el tiempo.

### Director (La ventana)
```cpp
auto dir = CCDirector::sharedDirector();
auto winSize = dir->getWinSize(); // Tamaño base (no pixeles reales)
dir->replaceScene(CCTransitionFade::create(0.5f, MyScene::create()));
```

### Scheduler (Updates / Timers)
```cpp
// Hookeando update
class $modify(MyNode, CCNode) {
    void update(float dt) {
        CCNode::update(dt);
        // dt = delta time (segundos desde ultimo frame)
    }
};
// Activar update en init
this->scheduleUpdate();

// Cronómetro de una vez (Wait)
this->getScheduler()->scheduleSelector(
    schedule_selector(MyClass::miFuncion), this, 1.0f, 0, 0.0f, false
);
```

### Acciones (Animaciones)
```cpp
auto move = CCMoveTo::create(1.0f, {100, 100});
auto fade = CCFadeIn::create(0.5f);
auto seq = CCSequence::create(move, fade, nullptr);
node->runAction(seq);
```

---

## 32) Math & Colors

### Tipos comunes
- `CCPoint`: `{x, y}`
- `CCSize`: `{width, height}`
- `CCRect`: `{x, y, width, height}`

### Colores
GD usa `ccColor3B` (0-255) usualmente.
```cpp
ccColor3B rojo = {255, 0, 0};
ccColor4B semitransparente = {255, 0, 0, 128};

// Helpers de geode
auto c = geode::cocos::ccc3FromHexString("#FF00AA");
```

---

## 33) Trabajar con Strings (Textos)

### Etiquetas (Bitmap Font)
GD usa fuentes bitmap (.fnt).
```cpp
// Fuente dorada estandar
auto label = CCLabelBMFont::create("Hola Mundo", "goldFont.fnt");

// Fuente chat
auto label2 = CCLabelBMFont::create("Texto chat", "chatFont.fnt");
```

### Input text fields
Usa `CCTextInputNode`. Es complejo de setear manualmente. Copia como lo hace `FLAlertLayer` o usa wrappers de Geode si existen (actualmente `InputNode` en geode ui helpers).

---

## 34) Debugging (VS Code & launch.json)

No uses solo `log::info` para debugear. Usa un debugger real.

### Configuración `launch.json`
Crea `.vscode/launch.json`:
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug Geometry Dash",
            "type": "cppvsdbg",
            "request": "launch",
            "program": "${config:geode.geodeSdk}/bin/GeometryDash.exe", // Geode llena esto
            "args": [],
            "stopAtEntry": false,
            "cwd": "${config:geode.geodeSdk}/bin",
            "environment": [],
            "console": "externalTerminal"
        }
    ]
}
```
*Si esto falla, apunta `program` a tu `GeometryDash.exe` real.*

### Breakpoints
Pon un punto rojo en el margen izquierdo de VS Code. Cuando el juego pase por ahí, se pausará y podrás inspeccionar variables.

---

## 35) Spritesheets & FrameCache (.plist)

Mejor rendimiento que cargar imágenes sueltas.

### Cargar .plist
En tu `init`:
```cpp
auto cache = CCSpriteFrameCache::sharedSpriteFrameCache();
cache->addSpriteFramesWithFile("MiSheet.plist"); 
// Geode busca "id.mod/MiSheet.plist" si esta en resources
```

### Usar frames
```cpp
// Crear sprite desde frame (notar el createWithSpriteFrameName)
auto spr = CCSprite::createWithSpriteFrameName("mi-icono.png");
// cambiar textura de uno existente
existingSpr->setDisplayFrame(cache->spriteFrameByName("otro-icono.png"));
```

---

## 36) Notificaciones

Muestra alertas no intrusivas (arriba de la pantalla).

```cpp
#include <Geode/ui/Notification.hpp>

Notification::create("Guardado exitosamente", NotificationIcon::Success)->show();
Notification::create("Error de conexión", NotificationIcon::Error, 2.0f)->show();
```
Iconos: `None`, `Success`, `Error`, `Warning`, `Info`.

---

## 37) JSON Avanzado (`matjson`)

Geode usa la librería `matjson`.

### Parsear
```cpp
std::string raw = "{ \"score\": 100 }";
auto json = matjson::parse(raw).unwrapOr(matjson::Value());

if (json.contains("score") && json["score"].isNumber()) {
    int score = json["score"].asInt().unwrapOr(0);
}
```

### Serializar
```cpp
matjson::Value obj;
obj["nombre"] = "Jugador";
obj["items"] = std::vector<int>{1, 2, 3};

std::string dump = obj.dump(matjson::NO_INDENTATION); // o 4 para pretty
```

### Structs Custom
Define `fromJson` devolviendo `Result<T>` y `toJson`.
```cpp
struct MiData { int x; };
template<>
struct matjson::Serialize<MiData> {
    static geode::Result<MiData> fromJson(matjson::Value const& v) {
        GEODE_UNWRAP_INTO(int x, v["x"].asInt());
        return Ok(MiData{ x });
    }
    static matjson::Value toJson(MiData const& d) {
        return matjson::makeObject({ { "x", d.x } });
    }
};
```

---

## 38) Mod Interop (API & Export)

Comunicar dos mods entre sí.

### Proveedor (Mod A)
Declara funciones exportables.
```cpp
// En cabecera publica
#ifdef GEODE_IS_WINDOWS
    #define MY_MOD_DLL __declspec(dllexport)
#else
    #define MY_MOD_DLL __attribute__((visibility("default")))
#endif

extern "C" MY_MOD_DLL void helloWorld() {
    log::info("Hola desde Mod A");
}
```

### Consumidor (Mod B)
Usa `Loader` para importar.
```cpp
// void(*)(void) es el tipo de la funcion
auto func = Mod::get()->getSavedValue<void(*)()>("helloWorld", "id.mod-a"); 
// Ojo: getSavedValue no es lo ideal para funciones dinamicas, mejor dlopen manual o usar el sistema de Dispatch de geode si existe.
// la forma recomendada actual:
if (auto mod = Loader::get()->getLoadedMod("id.mod-a")) {
    if (auto func = mod->getProcAddress("helloWorld")) {
        auto casted = reinterpret_cast<void(*)()>(func);
        casted();
    }
}
```

---

## 39) Errores Comunes (Troubleshooting)

### Crash al inicio
- **Causa**: DLLs faltantes o `mod.json` mal formado.
- **Solución**: Revisa logs en `geode/logs`.

### Crash al entrar a un nivel
- **Causa**: Hookeaste `PlayLayer::init` pero olvidaste `return true` o llamar al original.
- **Causa**: `Fields` en un objeto que se borra y se recrea mal.

### "Symbol not found"
- **Causa**: Usas una función de GD que no tiene bindings (`link`) en Windows.
- **Solución**: Busca en `Wylie's GD Decompiled` si la función es `inline` o si Geode no la expone. Si no está, toca usar address manual (peligroso).

### UI desalineada
- **Causa**: Asumiste pantalla 16:9.
- **Solución**: Usa `CCDrectior::sharedDirector()->getWinSize()` y `AnchorLayout`.

### Access Violation (0xC0000005)
- **Causa**: Puntero nulo.
- **Solución**: Siempre checkea `if (node)` antes de usarlo. Usa `Ref<>` si guardas punteros entre frames.

---

## 40) Custom Events & EventFilter (Avanzado)

Si tu mod expone una API (ej. "DragDrop de archivos"), no uses delegates antiguos. Usa Events.

### 1. Definir la Clase de Evento
Hereda de `geode::Event`.
```cpp
class MyDataEvent : public Event {
protected:
    std::string m_data;
public:
    MyDataEvent(std::string data) : m_data(data) {}
    std::string getData() const { return m_data; }
};
```

### 2. Disparar el Evento
```cpp
// En cualquier parte de tu código, para avisar a los listeners
MyDataEvent("hola").post();
```

### 3. Escuchar el Evento
Usa el listener por defecto o crea un filtro custom.
```cpp
$execute {
    new EventListener<EventFilter<MyDataEvent>>(+[](MyDataEvent* ev) {
        log::info("Recibido: {}", ev->getData());
        return ListenerResult::Propagate;
    });
}
```

### 4. Filtros Personalizados
Si quieres filtrar eventos (ej. solo archivos .png).
```cpp
class MyFilter : public EventFilter<MyEvent> {
public:
    using Callback = ListenerResult(MyEvent*);
    ListenerResult handle(std::function<Callback> fn, MyEvent* e) override {
        if (e->esValido()) return fn(e);
        return ListenerResult::Propagate;
    }
};
```

---

## 41) Platform & Utils

### Addresser (Direcciones de memoria)
Encuentra direcciones de funciones virt/no-virt.
```cpp
auto addr1 = addresser::getNonVirtual(&MenuLayer::onMoreGames);
auto addr2 = addresser::getVirtual(&MenuLayer::init);
```

### File System (`geode::utils::file`)
Wrappers seguros.
```cpp
// Escribir JSON directo
file::writeToJson("save.json", MyStruct());

// Leer string
auto content = file::readString("notes.txt").unwrapOr("");

// Selector de archivos nativo
file::pick(file::FilePickMode::OpenFile, file::FilePickOptions());
```

### Clipboard
```cpp
clipboard::write("Texto copiado");
auto txt = clipboard::read(); // devuelve Task<std::string> o similar
```

### Web (Abrir URLs)
Para abrir navegador predeterminado (no HTTP Requests).
```cpp
web::openLinkInBrowser("https://google.com");
```

### Permission (Android principalmente)
```cpp
permission::requestPermission(Permission::ReadAllFiles);
```

---

## 42) C++ Tips oficiales para Geode

Tips extraidos de la documentación oficial sobre buenas prácticas.

### `auto`
Úsalo, es idiomático en C++. Pero cuidado con strings literales.
```cpp
auto x = 5; // int
auto str = "Hola"; // const char*, NO std::string
auto str2 = std::string("Hola"); // std::string
```

### Casting
- **EVITA** C-style cast: `(int)x`. Es impredecible.
- **USA** `static_cast<int>(x)`.
- **PARA NODOS**: Usa `typeinfo_cast<MyNode*>(node)`. Es el `dynamic_cast` que funciona en GD (RTTI funciona diferente en Windows/modding).

### Memoria (Stack vs Heap)
- Preferencia: **Stack** (variables locales).
- Si usas `new`, debes usar `delete` (a menos que sea `CCNode` con autorelease).
- **Referencias (`&`)**: Úsalas para pasar objetos a funciones sin copiar.
```cpp
void procesar(MyBigStruct const& data); // Bien, constante y referencia
void procesar(MyBigStruct data); // Mal, copia innecesaria
```

### Smart Pointers
Si manejas objetos que NO son de Cocos/GD, usa `std::unique_ptr` o `std::shared_ptr`.
Para objetos de GD/Cocos, usa `Ref<T>`.

---

## 43) Corrutinas y `$async` (C++20)

Geode usa corrutinas para hacer código asíncrono limpio (sin hellscape de callbacks).

Referencia oficial: https://docs.geode-sdk.org/tutorials/coroutines

### `$async` (La forma fácil)
Ejecuta código en otro "hilo" lógico y espera resultados con `co_await`.
Requiere `#include <Geode/utils/web.hpp>` y `#include <Geode/utils/coro.hpp>`.

```cpp
void bajarArchivo(std::string url) {
    $async(url) {
        // Todo esto ocurre "async" en el main thread (frame a frame)
        // o si es WebRequest, espera a que termine.
        
        auto req = web::WebRequest();
        auto res = co_await req.get(url);
        
        if (res.ok()) {
            log::info("Codigo respuesta: {}", res.code());
        }
    }; // se lanza solo
}
```

### `Task<T>` manual
Si quieres retornar algo asíncrono.
```cpp
Task<int> obternerNumeroPausado() {
    // Imagina que esperamos algo aqui (timer simulado)
    co_return 100;
}

// Uso
$async {
    int num = co_await obternerNumeroPausado();
    log::info("Llego: {}", num);
};
```

### Generadores (`coro::Generator`)
Iterar sobre algo infinito o costoso, perezosamente (como yield de Python).
```cpp
#include <Geode/utils/coro.hpp>

coro::Generator<int> range(int start, int end) {
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}

for (int i : range(0, 5)) {
    log::info("Num: {}", i);
}
```

---

## 44) Diccionario de Modding (Vocabulario oficial)

Conceptos exclusivos de GD/Cocos2d que verás en conversaciones.

- **Director**: `CCDirector`. El jefe. Maneja la ventana y la escena actual.
- **Scene**: La pantalla completa (ej. MenuLayer, PlayLayer). Raíz del árbol.
- **Layer**: Contenedor principal de UI.
- **Node Tree**: Jerarquía visual (Padre -> Hijos).
- **Scheduler**: Sistema de timers (`update`, `scheduleOnce`).
- **Hook / Detour**: Tu código interceptando al original.
- **Trampoline**: Llamada a la función original dentro de un hook.
- **Calling Convention**: (`thiscall`, `membercall`, `optcall`). Cómo se pasan los argumentos en CPU (Windows es un infierno con esto, Geode lo abstrae).
- **Z-Order**: Orden de dibujado (mayor número = más al frente).
- **Tag**: Entero para identificar nodos (obsoleto por Node IDs).

---

## 45. Modificando Geode UI (Logos y botones)

Puedes inyectar cosas en la UI del propio Geode (lista de mods).

### Eventos UI
Escucha creación de elementos de Geode.
- `ModLogoUIEvent`: Cuando se crea el icono de un mod.
- `ModItemUIEvent`: Cuando se crea la celda de un mod en la lista.
- `ModPopupUIEvent`: Cuando se abre el popup de info.

```cpp
#include <Geode/ui/GeodeUI.hpp>

$execute {
    new EventListener<EventFilter<ModItemUIEvent>>(+[](ModItemUIEvent* ev) {
        if (ev->getModID() == "geode.loader") {
            // Añadir icono al la celda de geode loader
            auto item = ev->getItem();    
            if (auto btn = item->querySelector("developers-button")) {
                // Hacer algo
            }
        }
        return ListenerResult::Propagate;
    });
}
```

### Reglas de Oro
1. **SIEMPRE checkea null**. La UI de Geode cambia.
2. **NUNCA asumas índices**. Usa `querySelector` o IDs.
3. **Propaga el evento**. No rompas otros mods.

---

## 46) Colecciones Cocos vs C++ (`CCArrayExt`)

Cocos usa `CCArray` y `CCDictionary` (viento legado de Objective-C). Son molestos en C++ moderno. Geode los arregla.

### Iterar CCArray
Hacer cast manual es horrible. Usa `CCArrayExt`.
```cpp
CCArray* arr = someNode->getChildren();

for (auto node : CCArrayExt<CCNode*>(arr)) {
    node->setVisible(false);
}
```

### Iterar CCDictionary
```cpp
CCDictionary* dict = ...;
for (auto [key, obj] : CCDictionaryExt<std::string, CCObject*>(dict)) {
    log::info("Clave: {}", key);
}
```

### Convertir a std::vector
```cpp
auto vec = CCArrayExt<CCNode*>(arr).toVector();
```

---

## 47) Helpers del Juego (`utils::game`)

Controlar el ciclo de vida del juego.

```cpp
#include <Geode/utils/game.hpp>

// Reiniciar Geometry Dash (útil tras aplicar settings gráficos)
geode::utils::game::restart();

// Cerrar el juego
geode::utils::game::exit();
```

---

## 48) Manipulación de Strings y Rangos

### `geode::utils::string`
Funciones que C++ olvidó incluir.
```cpp
using namespace geode::utils::string;

if (contains("Hola Mundo", "Mundo")) { ... }
if (startsWith("prefix_algo", "prefix")) { ... }
std::string limpio = trim("  espacios  ");
std::string upper = toUpper("texto");
std::vector<std::string> partes = split("a,b,c", ",");
```

### `geode::utils::ranges` (Tipo LINQ)
Manipulación funcional de vectores.
```cpp
using namespace geode::utils::ranges;

std::vector<int> nums = {1, 2, 3, 4, 5};

// Filtrar pares y mapear x2
// (Nota: Geode usa sintaxis manual, no pipes de C++20 aun en todos lados)
auto procesados = map(
    filter(nums, [](int x){ return x % 2 == 0; }),
    [](int x){ return x * 2; }
); 
// Resultado: {4, 8}
```

---

## 49) Manejo de Errores (`Result<T>`)

Geode evita las excepciones. Usa `Result<Valor, Error>`.

### Crear funciones seguras
```cpp
Result<int> dividir(int a, int b) {
    if (b == 0) return Err("Division por cero");
    return Ok(a / b);
}
```

### Usar el resultado
```cpp
auto res = dividir(10, 0);

if (res.isErr()) {
    log::error("Fallo: {}", res.unwrapErr());
} else {
    int valor = res.unwrap();
    // o con default
    int valorSeguro = res.unwrapOr(0);
}
```

### `GEODE_UNWRAP`
Macro útil para retornar error automáticamente si falla (como `?` en Rust).
```cpp
Result<void> miFuncionCompleja() {
    int val = GEODE_UNWRAP(dividir(10, 2)); // si dividir falla, retorna el error aqui mismo.
    return Ok();
}
```

---

## 50) Defines de Plataforma

Para escribir compatibilidad multiplataforma.

```cpp
#ifdef GEODE_IS_WINDOWS
    // Codigo solo Windows
#elif defined(GEODE_IS_MACOS)
    // Mac (Intel o ARM)
#elif defined(GEODE_IS_ANDROID)
    // Android (ARM32/64)
#endif
```

### Constantes de directorios
Geode abstrae las rutas con `dirs::`
```cpp
auto saveDir = dirs::getSaveDir(); // AppData/Local/GeometryDash...
auto modDir = dirs::getModsDir(); // geode/mods
auto gameDir = dirs::getGameDir(); // Carpeta del .exe
```

---

## 51) Dependencias Avanzadas (Optional & Events)

Cómo interactuar con otros mods sin requerirlos obligatoriamente (evita crasheos si faltan).

### Configuración en `mod.json`
```json
"dependencies": {
    "hjfod.gmd-api": {
        "version": ">=v1.0.0",
        "importance": "recommended" // O "suggested" para soft-dependency
    }
}
```

### Dispatch System & Eventos
Si el mod opcional no está cargado, no puedes `linkear` sus funciones. Usa eventos.

**Emisor (Mod API)**: Define un `DispatchEvent`.
```cpp
using DragDropEvent = geode::DispatchEvent<std::filesystem::path>;
DragDropEvent("geode.drag-drop/default", "file.png").post();
```

**Receptor (Tu Mod)**: Escucha sin linkear.
```cpp
$execute {
    new EventListener<AttributeSetFilter>(+[](UserObjectSetEvent* ev) {
        log::info("Evento recibido de mod opcional");
        return ListenerResult::Propagate;
    }, AttributeSetFilter("geode.drag-drop/default"));
}
```

### User Objects (Comunicación primitiva)
Pasar datos vía nodos usando strings como keys.
```cpp
// Tu mod chequea si otro mod le puso una flag
if (layer->getUserObject("otro.mod/activar-feature")) {
    // hacer algo
}

// El otro mod pone la flag
layer->setUserObject("otro.mod/activar-feature", CCBool::create(true));
```

---

## 52) Custom Settings en Profundidad

Crear tipos de opciones complejos con UI propia.

### 1. Clase Valor (`SettingBaseValueV3`)
Debe ser serializable con `matjson` y heredar de `SettingBaseValueV3`.
```cpp
enum class MyEnum { A, B };

template<> struct matjson::Serialize<MyEnum> { ... }; // Implementar to/from json

class MySetting : public SettingBaseValueV3<MyEnum> {
public:
    static Result<std::shared_ptr<SettingV3>> parse(...) {
        // Implementar parseo de mod.json
    }
    SettingNodeV3* createNode(float width) override;
};
```

### 2. Clase Nodo UI (`SettingValueNodeV3`)
La UI que ve el usuario.
```cpp
class MySettingNode : public SettingValueNodeV3<MySetting> {
    bool init(std::shared_ptr<MySetting> setting, float width) {
        if (!SettingValueNodeV3::init(setting, width)) return false;
        // Crear botones, labels, sliders y añadirlos a this->getButtonMenu()
        return true;
    }
    void onCommit() override {
        // Guardar valor
    }
};
```

### 3. Registro
```cpp
$execute {
    Mod::get()->registerCustomSettingType("my-custom-type", &MySetting::parse);
}
```

---

## 53) Macro `GEODE_EVENT_EXPORT` (API Moderna)

Para crear APIs públicas robustas (Geode v4.3+). Automatiza la exportación de funciones vía eventos.

**Header API (publico):**
```cpp
#include <Geode/loader/Dispatch.hpp>
#define MY_MOD_ID "dev.my-api"

namespace api {
    // Declara funcion inline que usa el sistema de eventos por debajo
    inline geode::Result<int> sumar(int a, int b) 
        GEODE_EVENT_EXPORT(&sumar, (a, b));
}
```

**Implementación (tu cpp):**
```cpp
#define GEODE_DEFINE_EVENT_EXPORTS
#include "api.hpp"

Result<int> api::sumar(int a, int b) {
    return Ok(a + b);
}
```

Esto permite que otros mods llamen `api::sumar(1, 2)` de forma segura; si tu mod no está, retorna error controlado en vez de crash.







