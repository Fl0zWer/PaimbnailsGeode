# v2.2.0

**Custom Transitions / Transiciones Personalizadas**

- <cy>**30+ Screen Transitions**</c>: Choose from over <cg>30 cool animations</c> when switching between menus! Includes fades, slides, flips, zooms, page curls, tile effects, radial wipes, and more.
- <cy>**Transition Settings**</c>: New popup to <cg>browse, preview, and pick</c> your favorite transition with duration and color options.
- <cy>**Level Entry Transition**</c>: Set a <cg>separate animation for entering levels</c> — make it feel special every time you hit Play!
- <cy>**Custom Scripted Transitions**</c>: For advanced users — create your own <cg>custom transition sequences</c> combining fade, move, scale, rotate, and more.

**Thumbnails Everywhere / Miniaturas en Todas Partes**

- <cy>**Gauntlets**</c>: Gauntlet levels now show <cg>blurred thumbnail backgrounds</c> behind the level info.
- <cy>**Official Levels**</c>: The <cg>main level select screen</c> now shows thumbnail backgrounds that change as you scroll between pages.
- <cy>**Daily & Weekly**</c>: Daily and weekly level panels now display <cg>animated thumbnails</c> with a smooth breathing blur effect.
- <cy>**Map Packs**</c>: Map pack cells show a <cg>sliding carousel</c> that cycles through thumbnails of all levels in the pack.
- <cy>**Level Lists**</c>: Level list cells now preview the list's levels with a <cg>thumbnail carousel</c>.
- <cy>**Comment Popups**</c>: The comment/info popup now shows a <cg>blurred profile image</c> as background when viewing someone's profile.

**New Visual Effects / Nuevos Efectos Visuales**

- <cy>**17 Background Styles**</c>: The level info screen now has <cg>17 visual styles</c> for the background — Pixel, Blur, Grayscale, Sepia, Vignette, Scanlines, Bloom, Chromatic, Radial Blur, Glitch, Posterize, Rain, Matrix, Neon Pulse, Wave Distortion, CRT, and Normal.
- <cy>**Stack Effects**</c>: You can now <cg>combine up to 4 extra effects</c> on top of your chosen style for unique looks.
- <cy>**20+ Hover Color Effects**</c>: When hovering over level cells, choose from <cg>20+ color effects</c>: brightness, darken, sepia, red, blue, gold, grayscale, blur, invert, glitch, vignette, pixelate, rainbow, and more.
- <cy>**Better Mod Compatibility**</c>: Thumbnail visuals now <cg>work correctly alongside other mods</c> like Happy Textures without conflicts.

**Moderation Tools / Herramientas de Moderación**

- <cy>**Verification Center**</c>: New full-screen panel for moderators to <cg>review, accept, or reject</c> pending thumbnails, updates, reports, banners, and profile images — all in one place with preview.
- <cy>**Ban System**</c>: Moderators can now <cg>ban and unban users</c> with a reason, and view the full ban list with details.
- <cy>**Moderator Management**</c>: Admins can <cg>add or remove moderators</c> and see the current mod team.
- <cy>**Set Daily/Weekly**</c>: Admins can <cg>set or unset levels as Daily or Weekly</c> featured directly from the game.
- <cy>**Report Thumbnails**</c>: Players can now <cg>report inappropriate thumbnails</c> with a text reason. Reports go to the moderation queue.
- <cy>**Badges**</c>: <cg>Admin and Moderator badges</c> now appear on comments and profiles. Click them to see what each rank means!

**Support Page / Página de Apoyo**

- <cy>**Support Paimbnails**</c>: New in-game <cg>Support page</c> with supporter badge preview, benefits list, and beautiful animated backgrounds. Access it from the mod's info popup.

**Level Cell Settings / Configuración de Celdas**

- <cy>**All-in-One Settings**</c>: New <cg>settings popup</c> to customize everything about level cells in one place: background type, thumbnail size, blur, darkness, separator, view button, compact mode, hover animations, mythic particles, and animated gradients.
- <cy>**Instant Apply**</c>: Changes <cg>apply instantly</c> to all visible cells — no need to reload or restart.

**Bug Fixes & Improvements / Correcciones y Mejoras**

- <cy>**Profile Music Fix**</c>: Fixed a bug where <cg>profile music wouldn't download or play</c>. Music files were being incorrectly rejected — now they download and play correctly.
- <cy>**Brown Background Fix**</c>: The ugly <cg>brown placeholder backgrounds</c> that briefly appeared on profiles and comments are now <cg>hidden almost instantly</c> (15x faster detection).
- <cy>**Better Captures**</c>: Thumbnail captures now work correctly even when <cg>custom shaders</c> (visual effects in levels) are active.
- <cy>**No More Audio Conflicts**</c>: Background music no longer <cg>fights with Dynamic Song or Profile Music</c> — smooth transitions between all audio sources.
- <cy>**Button Animations**</c>: Paimbnails buttons now <cg>animate correctly</c> without glitching when clicked.
- <cy>**Profile Music Crossfade**</c>: New setting for a <cg>smooth fade</c> between background music and profile music, with adjustable duration.
- <cy>**"Same As" Backgrounds**</c>: New option to set a layer's background to be the <cg>same as another layer</c> — configure once, apply everywhere.
- <cy>**General Cleanup**</c>: Removed unused code and improved overall <cg>stability and performance</c>.

---

# v2.1.5

**Pet System Improvements / Mejoras al Sistema de Mascotas**

- <cy>**Pet Shop Download Fix**</c>: Fixed downloaded pets showing as empty squares. The client now validates response magic bytes (PNG, JPEG, GIF, WEBP) before saving, preventing corrupted JSON/HTML error responses from being written to disk.
- <cy>**Gallery Delete All**</c>: New <cg>Delete All</c> button in the Pet Gallery to quickly remove all pet images at once, with a confirmation dialog to prevent accidents.
- <cy>**Gallery Auto-Cleanup**</c>: On opening the gallery, any corrupted or invalid image files are automatically detected and removed.
- <cy>**Individual Delete Confirmation**</c>: The X button on each pet image now shows a confirmation dialog before deleting.
- <cy>**Visible Layers Default On**</c>: All <cg>Visible Layers</c> toggles (MenuLayer, LevelBrowserLayer, etc.) are now <cg>active by default</c>. If all layers are selected the pet shows everywhere.

**Easter Egg / Huevo de Pascua**

- <cy>**Hidden Paimon**</c>: Paimon now appears secretly behind the main menu title at a <cg>random position and angle</c> (−45° to 45°) each time you visit the menu.
- <cy>**Clickable Paimon**</c>: Click on the hidden Paimon to trigger a <cg>random explosion effect</c> with particles and sound from the game, then watch her spin away!

**UI Improvements / Mejoras de UI**

- <cy>**Per-Layer Custom Backgrounds**</c>: New <cg>"Layer Bg"</c> tab in the Configuration popup. Customize the background individually for <cy>CreatorLayer</c>, <cy>LevelBrowserLayer</c>, <cy>LevelSearchLayer</c>, and <cy>LeaderboardsLayer</c>. Each layer supports: <cg>Custom Image</c> (PNG/JPG/GIF), <cg>Random Thumbnail</c>, <cg>Level ID</c>, <cg>Same as Menu</c>, or <cg>Default (GD)</c>. Includes per-layer Dark Mode and Intensity controls.
- <cy>**LevelInfoLayer Settings Button**</c>: New <cg>gear button</c> in LevelInfoLayer's left side menu to quickly open thumbnail visual settings (style, blur intensity, darkness) without needing to open the thumbnail viewer first.


---

# v2.1.3

**Profile Rating System / Sistema de Calificacion de Perfiles**

- <cy>**Rate Profiles**</c>: New star button in profile bottom-menu allows rating other users' profiles with 1-5 stars and an optional message.
- <cy>**Profile Reviews**</c>: New button in left-menu (always visible) opens a popup showing all profile reviews with star ratings and messages.
- <cy>**Server Integration**</c>: Profile ratings are stored server-side with duplicate vote handling and average calculation.

**UI Improvements / Mejoras de UI**

- <cy>**Consistent Button Style**</c>: Profile rating button now uses the same small square style as other bottom-menu buttons (message, friend, block).
- <cy>**Always Visible Reviews**</c>: Profile reviews button is always visible in left-menu, even when user has comments disabled.

---

# v2.1.0

**Leaderboard & Navigation Improvements / Mejoras de Leaderboard y Navegacion**

- <cy>**Leaderboard Button Moved**</c>: The leaderboard button is now located in LevelSearchLayer's "other-filter-menu" instead of CreatorLayer for better accessibility.
- <cy>**Smart Navigation**</c>: Fixed back button functionality - LeaderboardLayer now correctly returns to the previous screen (CreatorLayer or LevelSearchLayer).
- <cy>**Level Viewing**</c>: Viewing levels from leaderboard now uses pushScene instead of replaceScene, maintaining proper navigation history.

**UI & Visual Enhancements / Mejoras Visuales y de UI**

- <cy>**Green Circle Buttons**</c>: Background config button now has the same green circle style as other menu buttons for visual consistency.
- <cy>**Profile Features**</c>: Added VIP restrictions for profile GIFs and profile music - only VIP, Moderators, and Admins can use these features.
- <cy>**Better File Handling**</c>: Improved filesystem error handling with proper error codes for cross-platform compatibility.

**Technical Improvements / Mejoras Tecnicas**

- <cy>**Player Toggle Helper**</c>: New utility class for comprehensive player visibility control including all particle effects and trails.
- <cy>**Code Cleanup**</c>: Removed unused includes and cleaned up commented code throughout the codebase.
- <cy>**Documentation Updates**</c>: Improved comments and documentation for better code maintainability.

**Bug Fixes / Correccion de Errores**

- <cy>**Navigation Stack**</c>: Fixed issue where pressing back from LevelInfoLayer would return to unexpected screens instead of the previous one.
- <cy>**Filesystem Operations**</c>: Fixed potential crashes when checking directory existence on different platforms.
- <cy>**Memory Management**</c>: Improved texture caching and cleanup to prevent memory leaks.

---

# v2.0.0

---

**The Beginning / El Comienzo**  
<cg>Initial Release / Lanzamiento Inicial</c>

- <cg>**Captures**</c>: Complete thumbnail capture system with <cy>full scene detection</c> (UILayer, PlayLayer, overlays) and high-quality hybrid capture.
- <cg>**Layer Editor**</c>: New editor to <cy>arrange and hide layers</c> for perfect thumbnail composition.
- <cg>**Visuals**</c>: Customizable level thumbnails with <cy>gradients, particles, and hover animations.</c>
- <cg>**Preview**</c>: Accurate mini-previews using full-scene rendering.
- <cg>**Dynamic Song**</c>: Option to play the level's song while browsing list menus.
- <cg>**Performance**</c>: Highly optimized with multi-threaded downloads, local disk caching, and <cy>GIF RAM caching</c>.
- <cg>**UI**</c>: Clean interface using Geode's style, with a customizable Button Editor.
- <cg>**Bilingual Support**</c>: Fully translated in <cy>English and Spanish</c>.
- <cg>**Compatibility**</c>: Built for <cy>Geometry Dash 2.2081</c> and <cy>Geode v5</c>.

---

## Coming Soon

- Versions for <cg>**Android**</c>, <cg>**iOS**</c>, and <cg>**macOS**</c>.
