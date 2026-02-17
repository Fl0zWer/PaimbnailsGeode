 # v1.0.5

- <cg>**Captures**</c>: Improved capture logic; <cy>full scene detection</c> (UILayer, PlayLayer, overlays) for cleaner thumbnails and previews.
- <cg>**Layer editor**</c>: New editor to <cy>arrange and hide layers</c> when editing thumbnails, so you can get the exact frame you want before capturing.
- <cg>**Preview**</c>: Mini-preview and live recapture now use the same full-scene rendering for consistency.

---

# v1.0.45

- Support for <cy>GD 2.2081</c> and <cy>Geode v5</c>
- <cg>**Dynamic Song**</c>: Play the level's song while browsing (enable in settings)
- <cg>**Performance**</c>: Optimized downloads, disk cache, and GIF RAM caching
- <cg>**UI**</c>: Buttons now use Geode's green circle style
- <cg>**Stability**</c>: Fixed crashes and improved HTTP reliability
- <cy>Bilingual support</c> (English/Spanish)

---

# v1.0.31

- <cg>**Geode Standards**</c>: Renamed resources with `paim_` prefix to prevent conflicts with other mods.
- <cg>**Geode Standards**</c>: Removed custom console window allocation; now uses standard Geode logging.
- <cg>**Settings**</c>: Renamed "Show Console Logs" to "Enable Debug Logs".

# v1.0.3

- fixed <cr>crash</c> when saving images.
- deleted useless code.
- cleaned up everything to prevent issues.

# v1.0.2

- <cg>**Dependencies**</c>: Updated dependency configurations and optimized download handling.
- <cg>**Moderation**</c>: The moderator menu is now hidden by default; added automatic server verification on profile load.

# v1.0.1 (Geode Guidelines & Optimization)

- <cg>**Code Auditing**</c>: Full compliance with Geode Modding Guidelines v4.9.0.
- <cg>**Safety**</c>: Fixed blocking I/O on main thread (saving images now runs in detached threads).
- <cg>**Cleanup**</c>: Removed deprecated features (Dark Mode, Moderator Mode, Profile Gradient) to focus on core functionality.
- <cg>**Stability**</c>: Fixed potential crashes with `dynamic_cast` and Windows path handling.
- <cg>**Refactoring**</c>: Cleaned up header files to prevent namespace pollution.

# v1.0.0 (Public Release)

- <cg>Level thumbnails system.</c>
- <cy>High-quality hybrid capture</c> (Direct/Render) from inside the game.
- Optimized concurrent downloading + local cache.
- Configurable visual effects (gradients/particles/hover).
- Advanced display options (including background styles in the level info screen).
- Mod settings integration (including performance and customization options).


## Coming Soon

- Versions for <cg>**Android**</c>, <cg>**iOS**</c>, and <cg>**macOS**</c>.
