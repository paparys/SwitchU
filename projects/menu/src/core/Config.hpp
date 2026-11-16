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
    // Current HOME presentation. homeLayout is the user-facing preset; the
    // appLayoutMode field stays as the derived renderer mode for compatibility.
    HomeLayoutPreset homeLayout = HomeLayoutPreset::Carousel;
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
    // Account whose per-user settings are currently loaded. The boot picker
    // writes this, and Config::load() overlays users/<uid>.json on settings.json
    // so the next menu build starts with that account's preferences.
    std::string activeProfileUid;
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
    // Full-screen per-game hero artwork. Turning this off keeps covers,
    // posters, icons and logos but never loads a hero backdrop.
    bool  steamGridDbShowHeroes = true;
    // Sub-toggles used when steamGridDbEnabled is on. Defaults keep prior
    // behaviour (artwork everywhere the focused title allows).
    bool  steamGridDbShowInGrid = true;
    bool  steamGridDbShowInDynamicLine = true;
    bool  steamGridDbShowInFolders = true;
    // Tall SteamGridDB portrait covers (Poster) on the single-row carousel.
    // Poster Wall uses the grid toggle below; Change View cycles HOME presets.
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
    // Moving two-colour gradient across the single strip plate. Colours index
    // the shared accent palette (1..9), like the focus highlight gradient.
    bool shortcutGradient = true;
    int shortcutGradientA = 1;
    int shortcutGradientB = 9;
    // Stretches the strip out to whichever screen edge it sits on and squares
    // that edge while the opposite corners stay rounded. The config key keeps
    // its original name so existing settings files remain compatible.
    bool shortcutFlushTop = false;
    // Vertical travel of the shortcut row in pixels (-24..24, snapped to 2px
    // steps). Applied at whichever edge the strip sits on.
    int shortcutNudgeY = 0;
    // Extra gap between tiles, percent of the fitted spacing (-50..100).
    int iconSpacing = 0;
    // All Apps grid shape: how many tiles one page holds. The screen fits
    // itself around these counts, so both directions stay usable on a TV.
    int allAppsColumns = 6;
    // 6x3 is the comfortable default: dropping one row makes every tile about
    // a third larger on a 720p handheld. The slider still supports 2..5 rows.
    int allAppsRows = 3;
    // All Apps Y menu: 0 = A-Z, 1 = recently played, 2 = favourites only.
    int allAppsSort = 0;
    // Comma-separated favourited title ids (decimal).
    std::string allAppsFavourites;
    // Title restored when All Apps reopens; 0 means start at the top-left.
    std::uint64_t allAppsLastFocus = 0;
    // Carousel distance shrink, percent: 0 keeps every tile the same size,
    // 100 shrinks the neighbours hardest. 50 is the shipped falloff.
    int carouselShrink = 50;
    int carouselFade = 50;
    // Carousel glide speed as a percentage of the stock pace. 50 = stock;
    // 0 = half speed, 100 = 1.5x.
    int carouselScrollSpeed = 50;
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
    int motionStyle = 1;
    // Opacity layer. 0 = the theme's own panel colour; 1..9 = a fixed colour
    // applied to the flat layer behind panels, dialogs and capsules.
    // opacitySolid draws that layer fully opaque.
    int opacityTint = 0;
    bool opacitySolid = false;
    // Focus ring around the highlighted icon, same palette (0 = theme).
    // 10 = animated two-colour gradient using highlightGradientA/B.
    int highlightTint = 0;
    int highlightGradientA = 1;   // palette index 1..9
    int highlightGradientB = 9;   // palette index 1..9
    int highlightThicknessPx = 3; // focus ring stroke width 1..8

    // Style lab values. Every default reproduces the shipped look exactly.
    // Panel tint mix blends the theme's panel colour toward the opacity tint:
    // 0 keeps the theme, 100 uses the tint colour throughout.
    int panelTintMix = 0;
    int panelBorderPct = 100;
    int panelGrainPct = 0;
    int panelShadowPct = 0;
    // Focus treatment on tiles and cards.
    int focusRingPx = 3;
    int focusGlowPct = 100;
    int focusLiftPct = 100;
    int focusOvershootPct = 0;
    // Capsule title: text scale, corner shape and backing strength.
    int titleScalePct = 100;
    int titleCornerStyle = 0; // 0 = pill, 1 = rounded, 2 = rect
    int titleBackgroundPct = 100;
    // Top HUD spacing and per-item visibility.
    int hudGapPx = 0;
    bool showHudClock = true;
    bool showHudDate = true;
    bool showHudBattery = true;
    bool showHudProfile = true;
    // Wallpaper grading.
    int wallpaperBlurPx = 0;
    int wallpaperSaturationPct = 100;
    int wallpaperSharpnessPct = 0;
    int wallpaperVibrancePct = 0;
    int wallpaperNoisePct = 0;
    int backdropTintPct = 0;
    // Page slide duration in milliseconds (150..600).
    int pageSlideMs = 340;

    // A saved look captures the visual identity, not per-app layout.
    struct LookPreset {
        bool filled = false;
        std::string name = "Look";
        std::string themePreset = "Default Light";
        int opacityTint = 0;
        bool opacitySolid = false;
        int highlightTint = 0;
        int highlightGradientA = 1;
        int highlightGradientB = 9;
        int highlightThicknessPx = 3;
        int panelTintMix = 0;
        int panelBorderPct = 100;
        int panelGrainPct = 0;
        int panelShadowPct = 0;
        int focusRingPx = 3;
        int focusGlowPct = 100;
        int focusLiftPct = 100;
        int focusOvershootPct = 0;
        int titleScalePct = 100;
        int titleCornerStyle = 0;
        int titleBackgroundPct = 100;
        int hudGapPx = 0;
        bool showHudClock = true;
        bool showHudDate = true;
        bool showHudBattery = true;
        bool showHudProfile = true;
        int wallpaperBlurPx = 0;
        int wallpaperSaturationPct = 100;
        int wallpaperSharpnessPct = 0;
        int wallpaperVibrancePct = 0;
        int wallpaperNoisePct = 0;
        int backdropTintPct = 0;
        int pageSlideMs = 340;
        int motionStyle = 1;
        int uiRadiusPct = 100;
        int iconScale = 100;
        int iconSpacing = 0;
        int carouselShrink = 50;
        int iconVignette = 0;
        int iconHighlight = 0;
        int iconShadowOffset = 6;
        int iconShadowAlpha = 20;
        std::vector<int> layout;
    };
    LookPreset lookPresets[3];
    // Carousel horizontal offset: moves the focused tile left or right of
    // centre without touching the grid.
    int lineNudgeX = 0;
    // Layout presets, 9 values each: aspect, corners, roundness,
    // scale, spacing, shrink, gridNudgeX, lineNudgeX, lineNudgeY.
    std::vector<int> layoutPreset[3];

    // Animated wallpaper (animated WebP, or Motion-JPEG inside an .mp4).
    // Empty keeps the theme's still image.
    std::string wallpaperPath;
    // Last wallpaper that decoded successfully. Kept separately from the
    // selection so a later boot can recover to it if wallpaperPath fails.
    std::string wallpaperLastUsedPath;
    bool wallpaperAnimated = true;
    int  wallpaperFps = 15;
    // Slow horizontal pan of the wallpaper / per-game backdrop. The speed is a
    // 1..100 travel rate, not a pixel value, so it reads the same on any art.
    bool backgroundDrift = false;
    int  backgroundDriftSpeed = 35;
    // Decorative drifting shapes on the theme background. Off keeps the gradient
    // and the wallpaper, dropping only the shapes.
    bool backgroundShapes = true;
    // Per-game hero fade-in, milliseconds (0..10000). 0 shows the art on the
    // frame its upload completes.
    int  heroFadeInMs = 3000;
    // Per-title hero alignment, each axis -100..100 (0 = centred). Stored as a
    // map so a game only carries an entry when it has been moved.
    std::unordered_map<std::uint64_t, std::pair<int, int>> heroOffsets;

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
    // Per-user lock codes keyed by AccountUid hex. An empty value explicitly
    // disables the code for that user; a missing entry inherits the legacy
    // global code above so existing installations stay protected.
    std::unordered_map<std::string, std::string> lockPadCodes;
    // Per-game SteamGridDB backdrop canvas: 0 = 1280x720 (full), 1 = 960x540,
    // 2 = 640x360. Lower sizes cut RAM and SD traffic while scrolling; the
    // sharpness of the hero follows whatever canvas it is decoded at.
    int heroBackdropResolution = 0;
    // Blur applied to the per-game backdrop: 0 = untouched, 100 = softest. The
    // decode worker applies it, so the cost is paid once per decoded hero.
    int heroBackdropBlur = 0;
    // Six comma-separated shortcut slot ids (0 = stock applet). Any installed
    // game or homebrew can occupy a slot from its Game Options page.
    // Title started from the HOME shortcut row. The menu process exits while
    // launching, so this survives the boundary and tells the next menu boot to
    // close the suspended title instead of offering it as resumable.
    std::uint64_t shortcutBootTitleId = 0;
    std::string gameShortcuts;
    // Per-slot placement for those six pins: comma-separated 0/1 values
    // (0 = left cluster, 1 = right cluster). Missing entries fall back to
    // shortcutSide, which keeps existing installs where they were.
    std::string gameShortcutSides;
    // Comma-separated title ids hidden from HOME. Restored from Settings.
    std::string hiddenGames;
    // Per-title display names: id<TAB>name lines.
    std::string gameNames;
    // Docked-mode layout overrides. handheld* keeps the normal sizes so
    // undocking restores them; docked* values are applied only while docked.
    bool dockedSizeOverrides = true;
    int handheldIconScale = 0;
    int handheldShortcutScale = 0;
    // 0 = not configured yet: Config::load inherits the handheld sizes, so a
    // custom icon/shortcut size keeps working when the console is docked.
    int dockedIconScale = 0;
    int dockedShortcutScale = 0;
    // Carousel position while docked, +/-25 steps like lineNudgeX/Y. Missing
    // keys inherit the current carousel position so an update does not
    // recentre an existing docked layout.
    int dockedCarouselX = 0;
    int dockedCarouselY = 0;
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

    // writeUserSettings=false is used when switching accounts: it commits the
    // new active UID without copying the outgoing account's settings over the
    // incoming account's file.
    bool save(bool writeUserSettings = true) const;
    // Selects the boot account, seeds its file on first use, and persists the
    // selection. Returns false when the global settings file could not be
    // written.
    bool selectActiveUser(const std::string& uidHex);

    static constexpr const char* kConfigDir  = "sdmc:/config/SwitchU";
    static constexpr const char* kConfigPath = "sdmc:/config/SwitchU/settings.json";
    static constexpr const char* kUsersDir   = "sdmc:/config/SwitchU/users";
    // The copy that was current before the last save. Read only when the live
    // settings file is missing or does not parse.
    static constexpr const char* kBackupPath = "sdmc:/config/SwitchU/settings.json.bak";
};
