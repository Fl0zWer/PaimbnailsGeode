# Marco Metodológico y Criterios de Auditoría Exhaustiva para Modificaciones en Geode SDK

## Introducción Arquitectónica al Ecosistema de Modding Moderno

El panorama de las modificaciones de software (modding) para el videojuego Geometry Dash ha experimentado una evolución técnica profunda, transitando desde la manipulación rudimentaria de memoria y el uso de herramientas obsoletas hasta la adopción de infraestructuras de desarrollo estandarizadas y multiplataforma. En el centro de esta revolución tecnológica se encuentra el **Geode SDK**, un marco de trabajo integral que actúa como la capa de abstracción definitiva entre el código fuente escrito por desarrolladores independientes y el motor subyacente del juego. Históricamente, los creadores dependían de bibliotecas como CappuccinoSDK o Cocos-Headers, las cuales proporcionaban definiciones básicas y a menudo inexactas del motor. Estos métodos tradicionales han sido completamente desaprobados y marcados como obsoletos, dado que introducían vulnerabilidades críticas de estabilidad y carecían de un sistema de resolución de dependencias.

Para comprender la necesidad de un marco de auditoría estricto, es imperativo analizar la arquitectura base sobre la cual opera Geometry Dash. El juego está construido sobre una bifurcación altamente modificada del motor **Cocos2d-x**, específicamente la versión 2.2.3. Este motor utiliza una arquitectura basada en nodos para la renderización de interfaces de usuario y la gestión del flujo lógico. El bloque de construcción fundamental es la clase `CCNode`, la cual soporta jerarquías complejas mediante relaciones de parentesco (padres e hijos) y transformaciones espaciales como posición, rotación y escala. En la cúspide de esta jerarquía operativa se encuentra el `CCScene`, el único nodo visible que carece de un nodo padre, cuya gestión recae exclusivamente en el `CCDirector`.

La complejidad del desarrollo radica en que las cabeceras oficiales de Cocos2d-x v2.2.3 son incompatibles con el entorno de ejecución de Geometry Dash debido a las modificaciones propietarias implementadas por el desarrollador original. Estas alteraciones abarcan desde la reescritura de sistemas de renderizado traseros (backends) hasta la adición de funciones auxiliares especializadas, tales como `CCLabelBMFont::limitLabelSize`. El Geode SDK mitiga esta discrepancia proporcionando cabeceras (headers) obtenidas mediante ingeniería inversa que reflejan con precisión el estado actual de la memoria del juego.

El propósito de este documento es establecer un **marco de evaluación metodológico**, funcional como un protocolo de revisión exhaustiva (comúnmente requerido por desarrolladores como una guía o sistema de evaluación integral), para auditar cualquier modificación (mod) antes de su compilación final y su posterior envío al índice oficial de Geode. La transición hacia Geode exige que el código fuente cumpla con directrices inflexibles de gestión de memoria, priorización de intercepciones (hooking), convenciones de estilo C++ y compatibilidad multiplataforma. Una revisión arquitectónica completa debe evaluar el proyecto a través de múltiples fases de escrutinio técnico, las cuales se detallan extensamente en las siguientes secciones.

---

## Fase de Auditoría 1: Configuración Estructural y Metadatos del Proyecto

La integridad estructural y la identidad operativa de cualquier modificación desarrollada bajo el Geode SDK están definidas enteramente por su archivo de configuración principal, denominado **mod.json**. Este archivo, que debe residir inexcusablemente en el directorio raíz del proyecto, actúa como el contrato de interfaz primordial entre el binario compilado y el cargador en tiempo de ejecución de Geode. El primer paso en cualquier protocolo de revisión exhaustiva consiste en la validación estricta de esta estructura JSON, ya que la presencia de metadatos malformados o incompletos constituye un criterio de rechazo inmediato y automático para la publicación en el índice oficial.

### Validación de Propiedades de Configuración Obligatorias

El esquema del entorno de ejecución exige que todo proyecto declare un conjunto inmutable de **seis propiedades fundamentales**. La omisión o el error tipográfico en cualquiera de estos campos resultará en el fracaso de la compilación o en la negativa del cargador a inyectar el código en la memoria del juego:

| Clave de Propiedad | Tipo de Dato JSON | Especificaciones Técnicas y Criterios de Auditoría |
|--------------------|-------------------|-----------------------------------------------------|
| **geode** | Cadena de texto (String) | Define la versión objetivo del Geode SDK requerida por el mod (por ejemplo, "v1.0.0"). La auditoría debe confirmar que esta cadena coincide exactamente con la versión del SDK utilizada en el entorno de desarrollo local para evitar discrepancias en la Interfaz Binaria de Aplicación (ABI). |
| **gd** | Cadena o Entidad (Object) | Especifica la versión de Geometry Dash compatible. Se puede emplear un asterisco ("*") para declarar soporte universal indiscriminado, o un objeto para delimitar versiones por arquitectura específica (ej. "win": "2.206", "android": "2.206"). |
| **id** | Cadena de texto (String) | Un identificador globalmente único que sigue una convención de nomenclatura de dominio inverso (ej. desarrollador.nombre-del-mod). El escrutinio debe garantizar que solo contenga caracteres alfanuméricos ASCII en minúscula, puntos y guiones, sin espacios. |
| **name** | Cadena de texto (String) | El nombre de visualización que se renderizará en la interfaz gráfica del gestor de mods dentro del juego. Aunque el formato UTF-8 está técnicamente soportado, la revisión debe aconsejar el uso estricto de caracteres ASCII. |
| **version** | Cadena de texto (String) | La iteración actual del software. Es un requisito absoluto que el formato cumpla con el estándar de Control de Versiones Semántico (SemVer) (ej. "v1.0.0"). |
| **developer** o **developers** | Cadena o Arreglo (Array) | Identifica la autoría. Se utiliza `developer` para un individuo singular, o un arreglo bajo la clave `developers` si es un esfuerzo colaborativo. En el caso del arreglo, el analizador lógico considerará la primera entrada como el autor principal. |

El proceso de escritura y verificación de este archivo se optimiza considerablemente si el IDE (específicamente Visual Studio Code) cuenta con la extensión oficial de Geode, la cual inyecta capacidades de autocompletado y validación de esquemas JSON en tiempo real.

### Resolución Avanzada de Dependencias y Manejo de Incompatibilidades

- **Dependencias**: Declarar correctamente en la clave `dependencies`. Las dependencias requeridas exigen el identificador del mod seguido de un operador de restricción (ej. `">=v1.0.5"`). La CLI de Geode (desde 1.4.0) descarga cabeceras a `build/geode-deps`.
- **Incompatibilidades**: Revisar la matriz `incompatibilities` con niveles: `breaking`, `conflicting`, `superseded`.

### Categorización Taxonómica y Enlaces Comunitarios

- **Tags**: Entre 1 y 4 etiquetas (universal, offline, online; gameplay, editor, interface; enhancement, bugfix, utility; performance, customization, content; developer, cheat, paid, joke).
- **Links** e **issues**: Vectores de contacto y documentación. Se recomienda `about.md` e icono `logo.png` para la revisión manual.

---

## Fase de Auditoría 2: Paradigmas de Gestión de Memoria en Cocos2d-x

La causa subyacente de la inmensa mayoría de las fallas catastróficas, cierres inesperados (crashes) y degradaciones de rendimiento proviene del manejo inadecuado de la memoria en C++ dentro del ecosistema Cocos2d-x. El motor se fundamenta en un **sistema de conteo de referencias intrusivo** para toda clase que derive de `CCObject`.

### Mecánica del Conteo de Referencias

- **retain()** incrementa el contador; **release()** lo decrementa. Cuando el contador llega a cero, el objeto se destruye.
- **Obligatorio**: usar `CC_SAFE_RETAIN(node)` y `CC_SAFE_RELEASE(node)` para evitar operaciones sobre punteros nulos.
- Los nodos añadidos con `addChild()` son retenidos automáticamente por el padre; al removerlos o destruir el padre, se hace `release()` en cascada. Los elementos correctamente anclados al grafo de escena rara vez requieren gestión manual.

### Trampa del Autorelease Pool

- Las funciones de fábrica (`create()`) suelen invocar `autorelease()` antes de devolver. El objeto queda en una pool y recibe `release()` al inicio del siguiente frame.
- **Fallos graves**: almacenar un puntero autoreleased como variable miembro cruda sin invocar `retain()`. En el frame siguiente el puntero queda colgante (dangling pointer) → `EXCEPTION_ACCESS_VIOLATION` (c0000005).

### Ref y WeakRef (RAII)

- **Ref&lt;T&gt;**: gestor de persistencia fuerte; en asignación hace `retain()`, en destructor `release()`. Elimina fugas por omisión de limpieza.
- **WeakRef&lt;T&gt;**: observación sin incrementar el contador. El acceso debe hacerse vía `.lock()`; si el objeto ya fue destruido, devuelve `nullptr`. Obligatorio en contenedores globales (mapas, listas) para evitar fugas por referencias fuertes persistentes. Implementar ciclos de limpieza para purgar entradas invalidadas.

---

## Fase de Auditoría 3: Intercepción de Flujo, Hooking y Modificación Estructural

### Macro $modify y Prioridades

- La macro **$modify** permite alterar el flujo del ejecutable mediante desvíos en vtables.
- **Prioridades**: usar `Priority::First` con `setHookPriorityPre` si la lógica debe ejecutarse antes que todas; `Priority::Last` con `setHookPriorityPost` si debe ejecutarse después.
- **Prohibido**: aritmética sobre valores de prioridad (ej. `Priority::Last - 1`); usar solo `setHookPriorityAfterPre` y `setHookPriorityBeforePre` para orden relativo.
- **Prohibido**: asignar `INT_MAX` como prioridad de un gancho (colisiona con la dirección de la función original).

### Hooks Manuales (Mod::addHook)

- **ASLR**: no usar direcciones absolutas hardcodeadas. Resolución dinámica: `geode::base::get() + offset` (offset por ingeniería inversa).
- **Convenciones de llamada**: declarar correctamente (Thiscall, Optcall, Membercall, etc.) vía metadatos (ej. `tulip::hook::TulipConvention::Thiscall`). Una discrepancia causa corrupción de pila.

### Parcheo de Bytes (Patch)

- Usar `geode::Patch::create` para reemplazar instrucciones; resolver ubicación con `geode::base::get() + offset`.
- **Ciclo de vida**: mantener el puntero al `Patch*` y deshabilitar o ceder propiedad cuando la funcionalidad se desactiva; no abandonar parches activos tras desactivar el mod.

---

## Fase de Auditoría 4: Campos Personalizados (struct Fields)

- **Prohibido**: añadir variables miembro directamente en clases modificadas vía herencia, ya que alteraría el layout binario (tamaño de la clase) y provocaría desalineación y desbordamientos.
- **Obligatorio**: extender estado dentro de **struct Fields { ... }**. Geode inyecta estos datos en mapas externos sin modificar la huella de memoria nativa.
- **Inicialización perezosa**: los Fields se materializan en el primer acceso a `m_fields->variable`. Construir interfaces protectoras y, en APIs externas, usar `static_cast<ClaseModificada*>(objetoCrudo)->m_fields->variable`.

---

## Fase de Auditoría 5: Subsistemas de UI y Estándares de Configuración

- **Settings V3**: todas las opciones ajustables deben declararse en el objeto `settings` de mod.json. Cada opción con `type` (string, int, bool, title, o `custom:identificador-de-tipo`).
- Controles custom deben derivar de **SettingV3** (datos/serialización) y **SettingNodeV3** (renderizado). No reinventar nodos base en bruto; delegar a la lógica de SettingNodeV3 y `getButtonMenu`.
- **Keybinds**: en mod.json incluir `priority`, `category` y, si aplica, `migrate-from`. Especial atención en macOS (Command vs Control). Implementar `SettingTypeForValueType` y sobrecarga de `getValue()` para tipado seguro.

---

## Fase de Auditoría 6: Integración Continua, Cadenas de Compilación y Multiplataforma

- **Prohibido**: publicar binarios .geode compilados localmente y subidos manualmente al repositorio. La publicación oficial debe realizarse mediante **CI** (p. ej. GitHub Actions con flujo tipo "build-geode-mod") que compile desde código fuente y emita binarios para Windows, macOS, iOS y Android.
- En desarrollo cruzado (p. ej. desde Linux), el manifiesto de construcción debe especificar `toolchainFile` y `HOST_ARCH` (ej. x86_64) de forma coherente con el destino.

---

## Fase de Auditoría 7: Utilidades Nativas (geode::utils) y Excepciones

- Sustituir primitivas de plataforma por **geode::utils**: operaciones de archivo (readBinary, readJson, Zip/Unzip, watchFile; rutas vía Mod::getSaveDir/getConfigDir), cadenas (split, join, toLower, wideToUtf8/utf8ToWide), rangos (map, reduce, filter, indexOf). Evitar `#ifdef _WIN32` para lógica que no sea específica del motor.
- **Prohibición de excepciones C++**: el ecosistema Geode proscribe `throw`/`catch` por roturas en fronteras ABI entre DLLs. Sustituir `std::stoi`/`std::stof` y similares por **geode::utils::numFromString** con wrappers de resultado (Results) para manejo de fallos sin excepciones.

---

## Fase de Auditoría 8: Análisis Forense de Colisiones y Crash Logs

- **EXCEPTION_ACCESS_VIOLATION (c0000005)**: suele indicar lectura/escritura en memoria no válida (puntero nulo, puntero colgante por autorelease, o buffer overflow).
- **Stack trace**: coordenadas en 0xFFFFFFFFFFFFFFFF en rutinas como `CCScene::getHighestChildZ` sugieren nodos fuera del grafo de escena o jerarquía corrupta. Offsets pequeños (ej. 0x5C) en texturas sugieren nullptr por objeto ya liberado (autorelease sin retain/Ref). Recursión masiva en "Hook handler" sugiere conflicto de prioridades entre mods.
- **Metodología**: uso de Safe Mode (cargar sin mods o con mods individuales) para aislar el mod responsable.

---

## Fase de Auditoría 9: Estandarización C++ y Políticas de Repositorio

### Estilo y Convenciones

| Aspecto | Regla |
|--------|--------|
| Variables/objetos | camelCase (ej. miVariableEficiente). Prohibido guiones bajos interiores. |
| Clases | PascalCase. |
| Variables estáticas permitidas | Solo con prefijo `s_` (ej. `static CCNode* s_compartidoNodo = nullptr`). Evitar globales huérfanos (Static Initialization Order Fiasco). |
| Punteros y referencias | Pegados al tipo: `CCNode* nombreNodo`, no `CCNode *nombreNodo`. |
| Nulidad | Siempre `nullptr`, nunca `NULL` ni `0`. |
| const | A la derecha de lo que limita: `CCNode const* ptr`. |
| Una declaración por línea | No concatenar con coma. |
| Llaves | Apertura `{` en la misma línea que la firma, sin espacio errático antes del paréntesis. |
| Alias de tipos | Usar `using`, no `typedef`. |

### Políticas de Índice y Repositorio

- No inyección maliciosa (RCE); no eliminar funcionalidad vanilla sin justificación consensuada.
- Normas de civilidad; rechazo ante bigotismo, agresión, sustracción intelectual o comportamiento hostil.
- Distribución preferentemente Open Source con repositorio (ej. GitHub) referenciado.
- **Versiones**: SemVer; no reemplazar ni sobreescribir revisiones ya publicadas (rompe verificaciones del indexador).

---

## Conclusiones del Marco de Auditoría

Una auditoría efectiva bajo este marco implica asimilar cómo el flujo en C++ interactúa con el tejido de Cocos2d-x y su conteo de referencias. Toda modificación debe demostrar:

- Uso correcto de **Ref** y **WeakRef** y evitación de punteros autoreleased sin retención.
- Prioridades de hook coherentes y sin aritmética peligrosa; hooks manuales con resolución ASLR y convenciones correctas; parches con ciclo de vida controlado.
- Extensión de estado solo mediante **struct Fields**.
- Configuración vía **Settings V3** y keybinds bien declarados.
- Build y publicación mediante **CI** multiplataforma.
- Uso de **geode::utils** y ausencia de **throw/catch**.
- Estilo C++ y políticas de repositorio alineados con las directrices del índice oficial.

Solo un proyecto que converja en seguridad de memoria, rigor estético y trazabilidad de publicación será ratificado como parte de la infraestructura del índice oficial de Geode.

---

## Matriz Final de Estado para Paimbnails2.4.1

La siguiente matriz resume el estado real del repositorio después de la pasada de auditoría e implementación sobre `src/` y metadata Geode.

| Categoría | Estado | Resultado resumido |
|--------|--------|--------|
| Async / Threading | **Corregido** | Se blindaron callbacks de captura y guardado para no tocar UI/estado tras salir del árbol (`PlayLayer`, `PauseLayer`, `CapturePreviewPopup`, `ProfileThumbs`). Los workers grandes de I/O, CPU y FMOD se mantienen porque ya usan `queueInMainThread`, tokens de lifetime o singletons estables. |
| Hooks / Prioridades | **Cumple** | La base sigue patrones Geode actuales con `$modify`, prioridades simbólicas y orden relativo. No se detectó aritmética peligrosa sobre prioridades en la superficie auditada. |
| IDs / UI Nodes | **Corregido** | Se normalizaron IDs propios y dinámicos, incluyendo prefijos de mod cuando `_spr` no aplica, eliminación de un doble prefijo y kebab-case en popup propio. Los IDs vanilla/canónicos preservados por compatibilidad se dejaron intactos. |
| Marcadores Geode UI | **Corregido** | `DynamicPopupRegistry` migró de `setUserData` a `setUserFlag`, alineado con la documentación moderna de Geode. |
| Casting / Node Metadata | **Corregido** | `PaimonLoadingOverlay` dejó de depender de `dynamic_cast` y ahora usa `setUserObject` con clave propia. |
| Memory / Lifetime | **Corregido** | Se sustituyeron varios puntos de retención manual evitable por `Ref<>` (`InfoButton`, `CustomTransitionEditorPopup`) y se eliminaron capturas crudas redundantes donde ya existía ownership seguro. |
| Memory / FMOD y escenas | **Deuda aceptada** | Persisten `retain/release` y releases manuales en subsistemas de transición, audio y texturas donde el ownership depende de FMOD, Cocos raw APIs o transferencia temporal entre escenas. No se reescribieron sin una justificación funcional más fuerte. |
| Result / JSON / Parsing | **Cumple** | Los `unwrap()` revisados en red/servicios estaban guardados por `isOk()` o checks equivalentes. Se limpió un caso menor de `unwrap()` duplicado en `HttpTransport`. No se detectaron conversiones `std::stoi` / `std::stof` activas en `src/`. |
| Metadata Geode (`mod.json`) | **Cumple** | Se mantiene en sintaxis vigente de Geode v5, con `dependencies`, `keybind`, `resources` y settings compatibles con la línea actual del SDK. |
| Markdown del mod | **Cumple con reservas** | `about.md` y `support.md` siguen razonablemente alineados. Si se desea publicar el resultado de esta auditoría como historial visible, conviene reflejar en `changelog.md` los ajustes internos de conformidad Geode. |

### Deuda Aceptada y Motivo

- Los fades y crossfades basados en threads de `DynamicSongManager`, `ProfileMusicManager` y `LeaderboardLayer` no se migraron automáticamente porque dependen de sincronización con FMOD y singletons internos del juego. Ahí el riesgo de regresión funcional supera el beneficio de una conversión mecánica.
- La lógica de texturas manuales en captura, GIF y algunas rutas de thumbnail sigue usando ownership Cocos explícito donde el flujo depende de `CCTexture2D`, `CCImage`, `RenderTexture` o buffers compartidos. No se detectó un reemplazo local claramente más seguro sin ampliar el alcance a un refactor mayor.
- Los diagnósticos estáticos de recursos que seguían abiertos en el editor ya fueron limpiados en código, corrigiendo frame names inválidos y evitando falsos positivos donde el analizador confundía nombres de archivo o audio con recursos del mod. La validación runtime completa de esos assets queda fuera de esta pasada porque no se ejecutó build ni smoke test.

### Conclusión Operativa

Para el alcance auditado, el mod quedó **alineado en lo esencial con las recomendaciones actuales de Geode**. Las desviaciones inequívocas ya fueron corregidas y el workspace quedó limpio de diagnósticos editoriales en esta pasada. Lo restante entra en la categoría de deuda técnica aceptada o revisión funcional más profunda, no en incumplimientos documentales claros del SDK Geode.
