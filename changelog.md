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
