# Support Paimbnails

Thank you for using Paimbnails! If you enjoy the mod, consider supporting its development.

## Need Help?

### Common Issues

**Thumbnails not loading**
- Check your internet connection
- Try clearing the thumbnail cache: Settings → Maintenance → Clear Cache
- If using a VPN, the CDN may be blocked — try disabling it temporarily

**Game lag with visual effects**
- Disable adaptive colors in Settings → Performance
- Lower the number of simultaneous effects
- Enable the Performance Optimizer in Settings → Performance

**Profile music not playing**
- Enable "Profile Music" in Paimbnails settings
- The profile owner must have configured a song
- Check that your game music volume is not muted

**Mod conflicts**
- Paimbnails uses `Priority::Late` for most hooks to minimize conflicts
- If another mod overrides the same UI elements, try loading Paimbnails last
- Report conflicts on our Discord with both mod names and versions

### Reporting Bugs

1. Enable debug logs: Settings → Test/Debug → Enable Debug Logs
2. Reproduce the issue
3. Find the log file in the Geode logs folder
4. Report on our [Discord server](https://discord.gg/5N5vpSfZwY) with:
   - What you were doing when the issue occurred
   - Your Paimbnails version and GD version
   - The log file

### Known Issues

- Adaptive colors may cause frame drops on low-end devices when many cells are visible
- GIF thumbnails consume more RAM than static images — the cache is capped at 60 entries
- Profile music crossfade spawns a single background thread per fade — this is intentional and safe

### Community

Join our [Discord server](https://discord.gg/5N5vpSfZwY) for:
- Help and troubleshooting
- Feature requests and suggestions
- Thumbnail submission guidelines
- Mod development updates
