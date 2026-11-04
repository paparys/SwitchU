#pragma once
#include "core/AppLayoutMode.hpp"
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct AppConfig {
    bool  musicEnabled = true;
    float musicVolume  = 0.4f;
    float sfxVolume    = 0.7f;
    int   musicTrackIndex = 0;
    // Stable id ("track.mp3" or "Album/track.mp3"). Preferred over musicTrackIndex
    // when users add/remove/reorder files between boots.
    std::string musicTrackKey;
    float musicPositionSeconds = 0.f;
    bool  musicShuffle = false;
    int   musicRepeatMode = 1; // MusicRepeatMode::All
    std::vector<std::string> musicPlaylistOrder;
    int   gridColumns  = 5;
    int   gridRows     = 3;
    // The grid view is gone: HOME is always the single row (carousel). Keep the
    // field so older configs still round-trip through load/save cleanly.
    AppLayoutMode appLayoutMode = AppLayoutMode::DynamicLine;
    std::string actionHintStyle = "capsules";
    // 0 = animated translation, 1 = immediate jump.
    int cursorMotionMode = 0;
    // 0 = pages slide sideways, 1 = pages swap instantly.
    int pageTransitionMode = 0;
    std::string uiLanguageOverride = "auto";
    std::string soundPreset = "wiiu";
    bool  defaultProfileEnabled = false;
    std::string defaultProfileUid;
    bool  tutorialCompleted = false;
    bool  clockUse12Hour = false;
    // Alternate to Nintendo auto clock correction: HTTPS Date sync on boot.
    bool  webClockSyncEnabled = false;
    bool  accessibilityEnabled = true;
    bool  accessibilitySpeakHints = true;
    bool  accessibilitySpeakContextEveryFocus = false;
    bool  accessibilitySpeakPosition = true;
    int   accessibilitySpeechRate = 190;
    bool  steamGridDbEnabled = true;
    // Sub-toggles used when steamGridDbEnabled is on. Defaults keep prior
    // behaviour (artwork everywhere the focused title allows).
    bool  steamGridDbShowInGrid = true;
    bool  steamGridDbShowInDynamicLine = true;
    bool  steamGridDbShowInFolders = true;
    // Tall SteamGridDB portrait covers (Poster) on the single-row carousel only.
    // Settings → SteamGridDB toggle; Change View stays grid ↔ single-row.
    bool  steamGridDbShowPosterInDynamicLine = true;
    // Logo overlay above / beside the single-row carousel.
    bool  steamGridDbShowLogoInDynamicLine = true;
    // Preferred landscape artwork aspect for SteamGridDB searches:
    // Bitmask of the shapes to request: 1 = 16:9, 2 = 4:3 (3 = both, 0 = any).
    // Each shape asks for every resolution it exists at.
    int   steamGridDbAspect = 0;
    std::string steamGridDbApiKey;

    // Home icon presentation, shared by the HOME grid and the single-row
    // carousel so both views keep one look.
    // 0 = square (1:1), 1 = widescreen (16:9), 2 = classic (4:3).
    int iconAspect = 0;
    // 0 = rounded tiles, 1 = perfectly square tiles.
    int iconCorners = 0;
    // Percentage of the theme's corner radius; 100 keeps the stock curve,
    // 0 draws square tiles without changing the Rounded/Square switch.
    int iconCornerPct = 100;
    // Uniform tile size in percent of the density-fitted cells (70..130).
    int iconScale = 100;
    // Sidebar / top-strip shortcut button size, percent (70..150). Separate
    // from iconScale so shortcuts and game tiles size independently.
    int sidebarScale = 100;
    // Where the shortcut strip sits: 0 = top of the screen, 1 = bottom.
    int shortcutEdge = 0;
    // Which applet cluster pinned game sockets sit next to: 0 = left, 1 = right.
    int shortcutSide = 0;
    // Horizontal travel for the shortcuts, in 1/25 screen steps (like the
    // other nudges): the strip slides, the two columns move in/out.
    int shortcutNudgeX = 0;
    // Right group / right column travels on its own paddle. Older configs
    // have no key for it, so load() seeds it from shortcutNudgeX.
    int shortcutNudgeXRight = 0;
    // One continuous plate behind the whole strip instead of a pill per
    // shortcut. Only the top / bottom strip layout draws it.
    bool shortcutStrip = true;
    // Vertical travel of the shortcut row in pixels (-24..24, snapped to 2px
    // steps). Applied at whichever edge the strip sits on.
    int shortcutNudgeY = 0;
    // Extra gap between tiles, percent of the fitted spacing (-50..100).
    int iconSpacing = 0;
    // Carousel depth, percent: negative pulls the row back so smaller tiles
    // and more of them are on screen, positive brings it forward.
    int carouselDepth = 0;
    int carouselFade = 50;
    int titlePillNudgeX = 0;
    // Hides the title capsule under the carousel entirely (clean shelf).
    bool hideGameTitle = false;
    // Bottom-right action capsules (A / B / + hints).
    bool hideActionHints = false;
    // Profile tile and battery pill size in the top HUD, percent (70..160).
    int hudScale = 100;
    // Date/time capsule: size percent and horizontal travel (+/-25 steps of
    // 6.4 px), same scale as the other nudges.
    int clockScale = 100;
    int clockNudgeX = 0;
    // Vertical travel of the clock in pixels, 2px steps (-14..14, 0 centred).
    // Leaving the bar row hands the clock its own capsule back.
    int clockNudgeY = 0;
    // Vertical travel for the capsule on the same +/-25 step scale as the
    // horizontal nudge. Positive moves it down.
    int titlePillNudgeY = 0;
    // Layout nudges as a percentage of one cell, clamped to +/-25.
    // The grid moves left/right; the carousel row moves up/down.
    int gridNudgeX = 0;
    int lineNudgeY = 0;
    // Whole-screen treatment. Dim is a flat veil; vignette darkens
    // the edges so tiles read against busy wallpaper.
    int wallpaperDim = 0;
    int vignetteStrength = 0;
    // Shared multiplier for every UI radius (tiles, cursor, panels,
    // cells, folders). 100 = the theme's own values.
    int uiRadiusPct = 100;
    // Tile drop shadow: vertical offset in px and veil strength in %.
    int iconShadowOffset = 6;
    int iconShadowAlpha = 20;
    // Darkens every tile edge so the artwork sits inside the glass (0..100).
    int iconVignette = 0;
    // Glass sheen across the top of each tile (0..100).
    int iconHighlight = 0;
    // Bevel depth on every tile: light along the top/left inner edge, shadow
    // along the bottom/right, so icons read as raised plates (0..100).
    int iconDepth = 35;
    int motionStyle = 1;
    // Liquid glass. 0 = the theme's own glass colour; 1..9 = a fixed tint
    // applied to every glass surface. glassSolid drops refraction entirely and
    // draws an opaque plate in that same colour.
    int glassTint = 0;
    bool glassSolid = false;
    // Focus ring around the highlighted icon, same palette (0 = theme).
    int highlightTint = 0;
    // Carousel horizontal offset: moves the focused tile left or right of
    // centre without touching the grid.
    int lineNudgeX = 0;
    // Layout presets, 9 values each: aspect, corners, roundness,
    // scale, spacing, depth, gridNudgeX, lineNudgeX, lineNudgeY.
    std::vector<int> layoutPreset[3];

    // Animated wallpaper (animated WebP, or Motion-JPEG inside an .mp4).
    // Empty keeps the theme's still image.
    std::string wallpaperPath;
    bool wallpaperAnimated = true;
    int  wallpaperFps = 15;
    // Slow horizontal pan of the wallpaper / per-game backdrop. The speed is a
    // 1..100 travel rate, not a pixel value, so it reads the same on any art.
    bool backgroundDrift = false;
    int  backgroundDriftSpeed = 35;

    float iconAspectRatio() const {
        switch (iconAspect) {
            case 1:  return 16.f / 9.f;
            case 2:  return 4.f / 3.f;
            default: return 1.f;
        }
    }

    // 0 keeps the hand-made layout. The other modes are display-only
    // projections and never overwrite layout.json.
    int sortMode = 0;
    bool alphaOrder = false;
    // A-Z / Recent also reorder applications that use a larger tile size.
    bool sortLargeTiles = true;
    // Colour HOME chrome icons follow single-row hero light/dark chrome when on
    // (Theme > Options). Default on — matches pre-PoloNX enjoyment.
    bool iconContrastAdapt = true;
    // Battery Saver = user-forced lite HOME path (same reclaim as Frame Tap
    // coexistence). Quick Settings + Theme Shop → Interface, synced.
    bool batterySaver = false;
    // Lock view (Theme Shop → Lock Screen). Gates Power→Lock entries too.
    bool lockScreenEnabled = false;
    // Hardware power short-press when Lock Screen is enabled:
    // 0 = Sleep (stock; still presents Lock before sleep when enabled)
    // 1 = Lock only (stay awake)
    // 2 = Lock then Sleep (handoff then sleep)
    int powerButtonAction = 0;
    // Lock clock face id (see LockClockFonts.hpp). Empty / "theme" = theme regular.
    std::string lockScreenClockFont;
    // Lock enter/exit motion: 0 = Curtain, 1 = Dissolve, 2 = Zoom, 3 = Wipe.
    int lockScreenTransition = 0;
    // Zoom: 0 = Out (grow away), 1 = In (shrink in).
    int lockScreenZoomDir = 0;
    // Wipe exit direction: 0 = Up, 1 = Right, 2 = Down, 3 = Left.
    int lockScreenWipeDir = 1;
    // Lock view D-pad passcode: four directional steps (U/D/L/R) that must be
    // pressed in order. Off by default; the lock screen falls back to swipe /
    // any-button unlock while this is false.
    bool lockPadCodeEnabled = false;
    std::string lockPadCode = "UDLR";
    // Per-game SteamGridDB backdrop canvas: 0 = 1280x720 (full), 1 = 960x540,
    // 2 = 640x360. Lower sizes cut RAM and SD traffic while scrolling; the
    // sharpness of the hero follows whatever canvas it is decoded at.
    int heroBackdropResolution = 0;
    // Blur applied to the per-game backdrop: 0 = untouched, 100 = softest. The
    // decode worker applies it, so the cost is paid once per decoded hero.
    int heroBackdropBlur = 0;
    // Six comma-separated shortcut slot ids (0 = stock applet). Any installed
    // game or homebrew can occupy a slot from its Game Options page.
    std::string gameShortcuts;
    // Comma-separated title ids hidden from HOME. Restored from Settings.
    std::string hiddenGames;
    // Per-title display names: id<TAB>name lines.
    std::string gameNames;
    // Docked-mode size overrides. handheld* keeps the normal values so
    // undocking restores them; docked* is applied only while docked.
    bool dockedSizeOverrides = true;
    int handheldIconScale = 0;
    int handheldShortcutScale = 0;
    // 0 = not configured yet: Config::load inherits the handheld sizes, so a
    // custom icon/shortcut size keeps working when the console is docked.
    int dockedIconScale = 0;
    int dockedShortcutScale = 0;
    // Theme Shop → Lock Screen: applying a theme with fonts may set clock face.
    bool themeLockClockFontOverride = true;
    // Theme Shop → Interface: applying a theme with icons may switch icon pack.
    bool themeIconPackOverride = true;
    // Theme Shop → Sounds: applying a theme with bundled music.
    // Override on = replace the current playlist (legacy). Off = keep the
    // user's library; optionally append theme tracks and/or start them.
    bool themeMusicOverride = true;
    bool themeMusicAddToPlaylist = true;
    bool themeMusicPlayOnApply = true;
    // Per-pack Photoshop-style HSL adjust (0.5 pivots = leave original) plus
    // Solid / Grayscale presentation toggles. matchTheme keeps pivots synced
    // to the active theme accent until the user edits HSL or turns it off.
    struct IconPackTint {
        bool enabled = false;
        bool flatten = false;
        bool grayscale = false;
        bool matchTheme = false;
        float hue = 0.5f;
        float sat = 0.5f;
        float light = 0.5f;
    };
    std::unordered_map<std::string, IconPackTint> iconPackTints;

    IconPackTint iconPackTintFor(const std::string& packId) const {
        auto it = iconPackTints.find(packId.empty() ? "default" : packId);
        if (it != iconPackTints.end())
            return it->second;
        return {};
    }
    void setIconPackTint(const std::string& packId, const IconPackTint& tint) {
        const std::string id = packId.empty() ? "default" : packId;
        if (!tint.enabled && !tint.flatten && !tint.grayscale && !tint.matchTheme
            && std::fabs(tint.hue - 0.5f) < 0.0005f
            && std::fabs(tint.sat - 0.5f) < 0.0005f
            && std::fabs(tint.light - 0.5f) < 0.0005f) {
            iconPackTints.erase(id);
            return;
        }
        iconPackTints[id] = tint;
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> lastOpened;
    std::uint64_t lastOpenedSequence = 0;

    std::uint64_t lastOpenedAt(std::uint64_t titleId) const {
        for (const auto& entry : lastOpened)
            if (entry.first == titleId) return entry.second;
        return 0;
    }
    void noteOpened(std::uint64_t titleId) {
        for (const auto& entry : lastOpened)
            lastOpenedSequence = std::max(lastOpenedSequence, entry.second);
        if (lastOpenedSequence != std::numeric_limits<std::uint64_t>::max())
            ++lastOpenedSequence;
        for (auto& entry : lastOpened) {
            if (entry.first == titleId) {
                entry.second = lastOpenedSequence;
                return;
            }
        }
        lastOpened.emplace_back(titleId, lastOpenedSequence);
    }

    std::string themePreset = "Default Light";
    // HOME sidebar glyph pack under romfs:/icons/<id>/.
    // Missing files fall back to the "default" pack.
    std::string iconPack = "default";
    // See switchu::folders::kFolderStyle*. Applies to every folder tile.
    int folderStyle = 0;
    // First-game icon overlay. Ignored by Classic (the mosaic is the cover).
    bool folderShowCover = false;
    // Layout when opening a folder: 0 = Auto (keep current), 1 = Grid,
    // 2 = Single-row. Does not change the HOME layout preference on its own.
    int folderDefaultView = 0;

    // Automatic day/night theme switching.
    std::string autoThemeMode = "off";       // "off" | "manual" | "geo"
    std::string autoThemeDayPreset;          // preset id/name used during the day
    std::string autoThemeNightPreset;        // preset id/name used during the night
    int         autoThemeDayStartHour = 7;   // manual boundary [0..23]
    int         autoThemeNightStartHour = 19;// manual boundary [0..23]
    // Cached IP-geolocated position for the geolocation mode.
    bool        autoThemeGeoResolved = false;
    double      autoThemeGeoLat = 0.0;       // degrees, north positive
    double      autoThemeGeoLon = 0.0;       // degrees, east positive
    std::string autoThemeGeoCity;            // human-readable resolved location

    bool load();

    bool save() const;

    static constexpr const char* kConfigDir  = "sdmc:/config/SwitchU";
    static constexpr const char* kConfigPath = "sdmc:/config/SwitchU/settings.json";
    // The copy that was current before the last save. Read only when the live
    // settings file is missing or does not parse.
    static constexpr const char* kBackupPath = "sdmc:/config/SwitchU/settings.json.bak";
};
