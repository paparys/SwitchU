#include "Config.hpp"
#include "FolderStore.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <mutex>
#include <system_error>

namespace {

static constexpr const char* kLegacyConfigPath = "sdmc:/config/SwitchU/config.json";
static constexpr const char* kLegacyBackupPath = "sdmc:/config/SwitchU/config.json.bak";
std::mutex g_configSaveMutex;

template <typename T>
void readJsonOpt(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) {
        try {
            out = it->get<T>();
        } catch (...) {
        }
    }
}

} // namespace

bool AppConfig::load() {
    // The previous copy is tried when the current one is missing or unreadable.
    // A crash used to cost the player every setting they had ever chosen --
    // including tutorialCompleted, which is why the tutorial greeted them again
    // after the theme crash. Losing the last change is acceptable; losing the
    // whole file is not. Legacy paths remain last so existing installations
    // migrate without taking precedence over the new settings file.
    nlohmann::json j;
    bool parsed = false;
    for (const char* path : {kConfigPath, kBackupPath,
                             kLegacyConfigPath, kLegacyBackupPath}) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        try {
            f >> j;
            parsed = true;
            break;
        } catch (...) {
            // Truncated or half-written: fall through to the backup.
        }
    }
    if (!parsed) return false;

    readJsonOpt(j, "musicEnabled", musicEnabled);
    readJsonOpt(j, "musicVolume", musicVolume);
    readJsonOpt(j, "sfxVolume", sfxVolume);
    readJsonOpt(j, "musicTrackIndex", musicTrackIndex);
    readJsonOpt(j, "musicTrackKey", musicTrackKey);
    readJsonOpt(j, "musicPositionSeconds", musicPositionSeconds);
    readJsonOpt(j, "musicShuffle", musicShuffle);
    readJsonOpt(j, "musicRepeatMode", musicRepeatMode);
    musicPlaylistOrder.clear();
    if (auto it = j.find("musicPlaylistOrder"); it != j.end() && it->is_array()) {
        for (const auto& item : *it) {
            if (item.is_string())
                musicPlaylistOrder.push_back(item.get<std::string>());
        }
    }
    readJsonOpt(j, "gridColumns", gridColumns);
    readJsonOpt(j, "gridRows", gridRows);
    // Grid view removed: HOME is always the single row. Old configs that still
    // say "grid" are migrated on load instead of being asked which view to use.
    appLayoutMode = AppLayoutMode::DynamicLine;
    readJsonOpt(j, "actionHintStyle", actionHintStyle);
    readJsonOpt(j, "cursorMotionMode", cursorMotionMode);
    readJsonOpt(j, "pageTransitionMode", pageTransitionMode);
    readJsonOpt(j, "uiLanguageOverride", uiLanguageOverride);
    readJsonOpt(j, "soundPreset", soundPreset);
    readJsonOpt(j, "defaultProfileEnabled", defaultProfileEnabled);
    readJsonOpt(j, "defaultProfileUid", defaultProfileUid);
    readJsonOpt(j, "tutorialCompleted", tutorialCompleted);
    readJsonOpt(j, "clockUse12Hour", clockUse12Hour);
    readJsonOpt(j, "webClockSyncEnabled", webClockSyncEnabled);
    readJsonOpt(j, "accessibilityEnabled", accessibilityEnabled);
    readJsonOpt(j, "accessibilitySpeakHints", accessibilitySpeakHints);
    readJsonOpt(j, "accessibilitySpeakContextEveryFocus", accessibilitySpeakContextEveryFocus);
    readJsonOpt(j, "accessibilitySpeakPosition", accessibilitySpeakPosition);
    readJsonOpt(j, "accessibilitySpeechRate", accessibilitySpeechRate);
    readJsonOpt(j, "steamGridDbEnabled", steamGridDbEnabled);
    readJsonOpt(j, "steamGridDbShowInGrid", steamGridDbShowInGrid);
    readJsonOpt(j, "steamGridDbShowInDynamicLine", steamGridDbShowInDynamicLine);
    readJsonOpt(j, "steamGridDbShowInFolders", steamGridDbShowInFolders);
    readJsonOpt(j, "steamGridDbShowPosterInDynamicLine", steamGridDbShowPosterInDynamicLine);
    readJsonOpt(j, "steamGridDbShowLogoInDynamicLine", steamGridDbShowLogoInDynamicLine);
    readJsonOpt(j, "steamGridDbAspect", steamGridDbAspect);
    readJsonOpt(j, "steamGridDbApiKey", steamGridDbApiKey);
    readJsonOpt(j, "glassTint", glassTint);
    readJsonOpt(j, "glassSolid", glassSolid);
    readJsonOpt(j, "highlightTint", highlightTint);
    readJsonOpt(j, "iconAspect", iconAspect);
    readJsonOpt(j, "iconCorners", iconCorners);
    readJsonOpt(j, "iconCornerPct", iconCornerPct);
    readJsonOpt(j, "iconScale", iconScale);
    readJsonOpt(j, "sidebarScale", sidebarScale);
    readJsonOpt(j, "shortcutEdge", shortcutEdge);
    readJsonOpt(j, "shortcutSide", shortcutSide);
    readJsonOpt(j, "shortcutNudgeX", shortcutNudgeX);
    if (j.find("shortcutNudgeXRight") == j.end())
        shortcutNudgeXRight = shortcutNudgeX;
    else
        readJsonOpt(j, "shortcutNudgeXRight", shortcutNudgeXRight);
    readJsonOpt(j, "shortcutStrip", shortcutStrip);
    readJsonOpt(j, "shortcutNudgeY", shortcutNudgeY);
    readJsonOpt(j, "iconSpacing", iconSpacing);
    readJsonOpt(j, "carouselDepth", carouselDepth);
    readJsonOpt(j, "carouselFade", carouselFade);
    readJsonOpt(j, "titlePillNudgeX", titlePillNudgeX);
    readJsonOpt(j, "hideGameTitle", hideGameTitle);
    readJsonOpt(j, "hideActionHints", hideActionHints);
    readJsonOpt(j, "hudScale", hudScale);
    readJsonOpt(j, "clockScale", clockScale);
    readJsonOpt(j, "clockNudgeX", clockNudgeX);
    readJsonOpt(j, "clockNudgeY", clockNudgeY);
    readJsonOpt(j, "titlePillNudgeY", titlePillNudgeY);
    readJsonOpt(j, "gridNudgeX", gridNudgeX);
    readJsonOpt(j, "lineNudgeY", lineNudgeY);
    readJsonOpt(j, "wallpaperDim", wallpaperDim);
    readJsonOpt(j, "vignetteStrength", vignetteStrength);
    readJsonOpt(j, "uiRadiusPct", uiRadiusPct);
    readJsonOpt(j, "iconShadowOffset", iconShadowOffset);
    readJsonOpt(j, "iconShadowAlpha", iconShadowAlpha);
    readJsonOpt(j, "iconVignette", iconVignette);
    readJsonOpt(j, "iconHighlight", iconHighlight);
    readJsonOpt(j, "iconDepth", iconDepth);
    readJsonOpt(j, "motionStyle", motionStyle);
    readJsonOpt(j, "lineNudgeX", lineNudgeX);
    for (int preset = 0; preset < 3; ++preset) {
        layoutPreset[preset].clear();
        const std::string key = "layoutPreset" + std::to_string(preset + 1);
        auto it = j.find(key);
        if (it == j.end() || !it->is_array())
            continue;
        for (const auto& value : *it) {
            if (value.is_number_integer() && layoutPreset[preset].size() < 9)
                layoutPreset[preset].push_back(value.get<int>());
        }
    }
    readJsonOpt(j, "wallpaperPath", wallpaperPath);
    readJsonOpt(j, "wallpaperAnimated", wallpaperAnimated);
    readJsonOpt(j, "wallpaperFps", wallpaperFps);
    readJsonOpt(j, "backgroundDrift", backgroundDrift);
    readJsonOpt(j, "backgroundDriftSpeed", backgroundDriftSpeed);
    readJsonOpt(j, "sortMode", sortMode);
    readJsonOpt(j, "alphaOrder", alphaOrder);
    readJsonOpt(j, "sortLargeTiles", sortLargeTiles);
    readJsonOpt(j, "iconContrastAdapt", iconContrastAdapt);
    readJsonOpt(j, "batterySaver", batterySaver);
    readJsonOpt(j, "lockScreenEnabled", lockScreenEnabled);
    readJsonOpt(j, "powerButtonAction", powerButtonAction);
    powerButtonAction = std::clamp(powerButtonAction, 0, 2);
    readJsonOpt(j, "lockScreenClockFont", lockScreenClockFont);
    readJsonOpt(j, "lockScreenTransition", lockScreenTransition);
    lockScreenTransition = std::clamp(lockScreenTransition, 0, 3);
    readJsonOpt(j, "lockScreenZoomDir", lockScreenZoomDir);
    lockScreenZoomDir = std::clamp(lockScreenZoomDir, 0, 1);
    readJsonOpt(j, "lockScreenWipeDir", lockScreenWipeDir);
    readJsonOpt(j, "heroBackdropResolution", heroBackdropResolution);
    heroBackdropResolution = std::clamp(heroBackdropResolution, 0, 2);
    readJsonOpt(j, "heroBackdropBlur", heroBackdropBlur);
    heroBackdropBlur = std::clamp(heroBackdropBlur, 0, 100);
    lockScreenWipeDir = std::clamp(lockScreenWipeDir, 0, 3);
    readJsonOpt(j, "lockPadCodeEnabled", lockPadCodeEnabled);
    readJsonOpt(j, "lockPadCode", lockPadCode);
    readJsonOpt(j, "gameShortcuts", gameShortcuts);
    readJsonOpt(j, "hiddenGames", hiddenGames);
    readJsonOpt(j, "gameNames", gameNames);
    readJsonOpt(j, "dockedSizeOverrides", dockedSizeOverrides);
    readJsonOpt(j, "handheldIconScale", handheldIconScale);
    readJsonOpt(j, "handheldShortcutScale", handheldShortcutScale);
    readJsonOpt(j, "dockedIconScale", dockedIconScale);
    readJsonOpt(j, "dockedShortcutScale", dockedShortcutScale);
    if (handheldIconScale == 0) handheldIconScale = iconScale;
    if (handheldShortcutScale == 0) handheldShortcutScale = sidebarScale;
    if (dockedIconScale <= 0) dockedIconScale = handheldIconScale;
    if (dockedShortcutScale <= 0) dockedShortcutScale = handheldShortcutScale;
    dockedIconScale = std::clamp(dockedIconScale, 70, 130);
    dockedShortcutScale = std::clamp(dockedShortcutScale, 70, 150);
    readJsonOpt(j, "themeLockClockFontOverride", themeLockClockFontOverride);
    readJsonOpt(j, "themeIconPackOverride", themeIconPackOverride);
    readJsonOpt(j, "themeMusicOverride", themeMusicOverride);
    readJsonOpt(j, "themeMusicAddToPlaylist", themeMusicAddToPlaylist);
    readJsonOpt(j, "themeMusicPlayOnApply", themeMusicPlayOnApply);
    iconPackTints.clear();
    if (auto it = j.find("iconPackTints"); it != j.end() && it->is_object()) {
        for (auto& [packId, value] : it->items()) {
            if (!value.is_object() || packId.empty())
                continue;
            IconPackTint tint;
            readJsonOpt(value, "enabled", tint.enabled);
            readJsonOpt(value, "flatten", tint.flatten);
            readJsonOpt(value, "grayscale", tint.grayscale);
            readJsonOpt(value, "matchTheme", tint.matchTheme);
            readJsonOpt(value, "hue", tint.hue);
            readJsonOpt(value, "sat", tint.sat);
            readJsonOpt(value, "light", tint.light);
            if (tint.matchTheme)
                tint.enabled = true;
            tint.hue = std::clamp(tint.hue, 0.f, 1.f);
            tint.sat = std::clamp(tint.sat, 0.f, 1.f);
            tint.light = std::clamp(tint.light, 0.f, 1.f);
            // Legacy pack ids (grayscale/solid/tinted) → atmos.
            std::string id = packId;
            if (id == "tinted" || id == "grayscale" || id == "solid")
                id = "atmos";
            iconPackTints[id] = tint;
        }
    } else {
        bool legacyEnabled = false;
        float legacyHue = 0.5f, legacySat = 0.5f, legacyLight = 0.5f;
        readJsonOpt(j, "iconCustomTint", legacyEnabled);
        readJsonOpt(j, "iconTintHue", legacyHue);
        readJsonOpt(j, "iconTintSat", legacySat);
        readJsonOpt(j, "iconTintLight", legacyLight);
        if (legacyEnabled
            || std::fabs(legacyHue - 0.5f) > 0.0005f
            || std::fabs(legacySat - 0.5f) > 0.0005f
            || std::fabs(legacyLight - 0.5f) > 0.0005f) {
            IconPackTint tint;
            tint.enabled = legacyEnabled;
            tint.hue = std::clamp(legacyHue, 0.f, 1.f);
            tint.sat = std::clamp(legacySat, 0.f, 1.f);
            tint.light = std::clamp(legacyLight, 0.f, 1.f);
            std::string packId = "default";
            readJsonOpt(j, "iconPack", packId);
            if (packId.empty())
                packId = "default";
            iconPackTints[packId] = tint;
        }
    }
    readJsonOpt(j, "lastOpenedSequence", lastOpenedSequence);
    lastOpened.clear();
    if (auto it = j.find("lastOpened"); it != j.end() && it->is_object()) {
        for (auto& [key, value] : it->items()) {
            if (!value.is_number_unsigned()) continue;
            const auto titleId = std::strtoull(key.c_str(), nullptr, 16);
            if (titleId != 0)
                lastOpened.emplace_back(titleId, value.get<std::uint64_t>());
        }
    }
    readJsonOpt(j, "themePreset", themePreset);
    readJsonOpt(j, "iconPack", iconPack);
    if (iconPack.empty())
        iconPack = "default";
    else if (iconPack == "tinted" || iconPack == "grayscale" || iconPack == "solid")
        iconPack = "atmos";
    readJsonOpt(j, "autoThemeMode", autoThemeMode);
    readJsonOpt(j, "autoThemeDayPreset", autoThemeDayPreset);
    readJsonOpt(j, "autoThemeNightPreset", autoThemeNightPreset);
    readJsonOpt(j, "autoThemeDayStartHour", autoThemeDayStartHour);
    readJsonOpt(j, "autoThemeNightStartHour", autoThemeNightStartHour);
    readJsonOpt(j, "autoThemeGeoResolved", autoThemeGeoResolved);
    readJsonOpt(j, "autoThemeGeoLat", autoThemeGeoLat);
    readJsonOpt(j, "autoThemeGeoLon", autoThemeGeoLon);
    readJsonOpt(j, "autoThemeGeoCity", autoThemeGeoCity);
    readJsonOpt(j, "folderStyle", folderStyle);
    const bool hasShowCoverKey = j.find("folderShowCover") != j.end();
    readJsonOpt(j, "folderShowCover", folderShowCover);
    readJsonOpt(j, "folderDefaultView", folderDefaultView);
    folderDefaultView = std::clamp(folderDefaultView, 0, 2);

    if (musicVolume < 0.f) musicVolume = 0.f;
    if (musicVolume > 1.f) musicVolume = 1.f;
    if (sfxVolume   < 0.f) sfxVolume   = 0.f;
    if (sfxVolume   > 1.f) sfxVolume   = 1.f;
    gridColumns = std::clamp(gridColumns, 1, 8);
    gridRows = std::clamp(gridRows, 1, 5);
    if (actionHintStyle != "panel" && actionHintStyle != "capsules")
        actionHintStyle = "capsules";
    cursorMotionMode = std::clamp(cursorMotionMode, 0, 1);
    pageTransitionMode = std::clamp(pageTransitionMode, 0, 1);
    if (uiLanguageOverride.empty()) uiLanguageOverride = "auto";
    if (soundPreset.empty()) soundPreset = "wiiu";
    if (!defaultProfileEnabled) defaultProfileUid.clear();
    accessibilitySpeechRate = std::clamp(accessibilitySpeechRate, 120, 320);
    sortMode = std::clamp(sortMode, 0, 2);
    steamGridDbAspect = std::clamp(steamGridDbAspect, 0, 3);
    glassTint = std::clamp(glassTint, 0, 9);
    highlightTint = std::clamp(highlightTint, 0, 9);
    iconAspect = std::clamp(iconAspect, 0, 2);
    iconCorners = std::clamp(iconCorners, 0, 1);
    iconCornerPct = std::clamp(iconCornerPct, 0, 100);
    iconScale = std::clamp(iconScale, 70, 130);
    sidebarScale = std::clamp(sidebarScale, 70, 150);
    shortcutEdge = std::clamp(shortcutEdge, 0, 1);
    shortcutSide = std::clamp(shortcutSide, 0, 1);
    shortcutNudgeX = std::clamp(shortcutNudgeX, -25, 25);
    shortcutNudgeXRight = std::clamp(shortcutNudgeXRight, -25, 25);
    // 2px steps only: truncate toward zero, which also covers negatives.
    shortcutNudgeY = (std::clamp(shortcutNudgeY, -24, 24) / 2) * 2;
    iconSpacing = std::clamp(iconSpacing, -50, 100);
    carouselDepth = std::clamp(carouselDepth, -100, 100);
    carouselFade = std::clamp(carouselFade, 0, 100);
    titlePillNudgeX = std::clamp(titlePillNudgeX, -25, 25);
    hudScale = std::clamp(hudScale, 70, 160);
    clockScale = std::clamp(clockScale, 70, 160);
    clockNudgeX = std::clamp(clockNudgeX, -25, 25);
    clockNudgeY = std::clamp(clockNudgeY, -14, 14);
    titlePillNudgeY = std::clamp(titlePillNudgeY, -25, 25);
    gridNudgeX = std::clamp(gridNudgeX, -25, 25);
    lineNudgeY = std::clamp(lineNudgeY, -25, 25);
    wallpaperDim = std::clamp(wallpaperDim, 0, 100);
    vignetteStrength = std::clamp(vignetteStrength, 0, 100);
    uiRadiusPct = std::clamp(uiRadiusPct, 50, 200);
    iconShadowOffset = std::clamp(iconShadowOffset, 0, 20);
    iconShadowAlpha = std::clamp(iconShadowAlpha, 0, 60);
    iconVignette = std::clamp(iconVignette, 0, 100);
    iconHighlight = std::clamp(iconHighlight, 0, 100);
    iconDepth = std::clamp(iconDepth, 0, 100);
    motionStyle = std::clamp(motionStyle, 0, 2);
    lineNudgeX = std::clamp(lineNudgeX, -25, 25);
    wallpaperFps = std::clamp(wallpaperFps, 5, 30);
    backgroundDriftSpeed = std::clamp(backgroundDriftSpeed, 1, 100);
    // Animated wallpapers are no longer offered in Settings. Drop a path saved
    // by an earlier build so the background stays on the theme image.
    wallpaperPath.clear();
    if (themePreset.empty()) themePreset = "Default Light";
    if (!hasShowCoverKey) {
        // Local 9-style table used while vetting Cover/Plate as separate styles.
        switch (folderStyle) {
            case 0: // Classic
            case 1: // Simple
                break;
            case 2: // Cover → Simple + cover
                folderStyle = switchu::folders::kFolderStyleSimple;
                folderShowCover = true;
                break;
            case 3: folderStyle = switchu::folders::kFolderStyleMinimal; break;
            case 4: folderStyle = switchu::folders::kFolderStyleTab; break;
            case 5: folderStyle = switchu::folders::kFolderStyleRing; break;
            case 6: folderStyle = switchu::folders::kFolderStyleManila; break;
            case 7: folderStyle = switchu::folders::kFolderStyleClassic; break; // Plate
            case 8: folderStyle = switchu::folders::kFolderStyleLabel; break;
            default:
                folderStyle = switchu::folders::kDefaultFolderStyle;
                break;
        }
    }
    folderStyle = std::clamp(folderStyle, 0, switchu::folders::kFolderStyleCount - 1);

    if (autoThemeMode != "off" && autoThemeMode != "manual" && autoThemeMode != "geo")
        autoThemeMode = "off";
    autoThemeDayStartHour = std::clamp(autoThemeDayStartHour, 0, 23);
    autoThemeNightStartHour = std::clamp(autoThemeNightStartHour, 0, 23);
    if (autoThemeGeoLat < -90.0 || autoThemeGeoLat > 90.0
        || autoThemeGeoLon < -180.0 || autoThemeGeoLon > 180.0) {
        autoThemeGeoResolved = false;
        autoThemeGeoLat = 0.0;
        autoThemeGeoLon = 0.0;
        autoThemeGeoCity.clear();
    }

    return true;
}

bool AppConfig::save() const {
    // Settings closes are persisted by a worker while launch actions
    // can save on the UI thread. Both use the same staging filename, so those
    // writes must never overlap.
    const std::lock_guard<std::mutex> saveLock(g_configSaveMutex);
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory(kConfigDir, ec);

    nlohmann::json j;
    j["musicEnabled"] = musicEnabled;
    j["musicVolume"] = musicVolume;
    j["sfxVolume"] = sfxVolume;
    j["musicTrackIndex"] = musicTrackIndex;
    j["musicTrackKey"] = musicTrackKey;
    j["musicPositionSeconds"] = musicPositionSeconds;
    j["musicShuffle"] = musicShuffle;
    j["musicRepeatMode"] = std::clamp(musicRepeatMode, 0, 2);
    j["musicPlaylistOrder"] = musicPlaylistOrder;
    j["gridColumns"] = std::clamp(gridColumns, 1, 8);
    j["gridRows"] = std::clamp(gridRows, 1, 5);
    j["appLayoutMode"] = "dynamic_line";
    j["actionHintStyle"] = actionHintStyle == "panel" ? "panel" : "capsules";
    j["cursorMotionMode"] = std::clamp(cursorMotionMode, 0, 1);
    j["pageTransitionMode"] = std::clamp(pageTransitionMode, 0, 1);
    j["uiLanguageOverride"] = uiLanguageOverride;
    j["soundPreset"] = soundPreset;
    j["defaultProfileEnabled"] = defaultProfileEnabled;
    j["defaultProfileUid"] = defaultProfileEnabled ? defaultProfileUid : std::string();
    j["tutorialCompleted"] = tutorialCompleted;
    j["clockUse12Hour"] = clockUse12Hour;
    j["webClockSyncEnabled"] = webClockSyncEnabled;
    j["accessibilityEnabled"] = accessibilityEnabled;
    j["accessibilitySpeakHints"] = accessibilitySpeakHints;
    j["accessibilitySpeakContextEveryFocus"] = accessibilitySpeakContextEveryFocus;
    j["accessibilitySpeakPosition"] = accessibilitySpeakPosition;
    j["accessibilitySpeechRate"] = std::clamp(accessibilitySpeechRate, 120, 320);
    j["steamGridDbEnabled"] = steamGridDbEnabled;
    j["steamGridDbShowInGrid"] = steamGridDbShowInGrid;
    j["steamGridDbShowInDynamicLine"] = steamGridDbShowInDynamicLine;
    j["steamGridDbShowInFolders"] = steamGridDbShowInFolders;
    j["steamGridDbShowPosterInDynamicLine"] = steamGridDbShowPosterInDynamicLine;
    j["steamGridDbShowLogoInDynamicLine"] = steamGridDbShowLogoInDynamicLine;
    j["steamGridDbAspect"] = std::clamp(steamGridDbAspect, 0, 3);
    j["steamGridDbApiKey"] = steamGridDbApiKey;
    j["glassTint"] = std::clamp(glassTint, 0, 9);
    j["glassSolid"] = glassSolid;
    j["highlightTint"] = std::clamp(highlightTint, 0, 9);
    j["iconAspect"] = std::clamp(iconAspect, 0, 2);
    j["iconCorners"] = std::clamp(iconCorners, 0, 1);
    j["iconCornerPct"] = std::clamp(iconCornerPct, 0, 100);
    j["iconScale"] = std::clamp(iconScale, 70, 130);
    j["sidebarScale"] = std::clamp(sidebarScale, 70, 150);
    j["shortcutEdge"] = std::clamp(shortcutEdge, 0, 1);
    j["shortcutSide"] = std::clamp(shortcutSide, 0, 1);
    j["shortcutNudgeX"] = std::clamp(shortcutNudgeX, -25, 25);
    j["shortcutNudgeXRight"] = std::clamp(shortcutNudgeXRight, -25, 25);
    j["shortcutStrip"] = shortcutStrip;
    j["shortcutNudgeY"] = std::clamp(shortcutNudgeY, -24, 24);
    j["iconSpacing"] = std::clamp(iconSpacing, -50, 100);
    j["carouselDepth"] = std::clamp(carouselDepth, -100, 100);
    j["carouselFade"] = std::clamp(carouselFade, 0, 100);
    j["titlePillNudgeX"] = std::clamp(titlePillNudgeX, -25, 25);
    j["hideGameTitle"] = hideGameTitle;
    j["hideActionHints"] = hideActionHints;
    j["hudScale"] = std::clamp(hudScale, 70, 160);
    j["clockScale"] = std::clamp(clockScale, 70, 160);
    j["clockNudgeX"] = std::clamp(clockNudgeX, -25, 25);
    j["clockNudgeY"] = std::clamp(clockNudgeY, -14, 14);
    j["titlePillNudgeY"] = std::clamp(titlePillNudgeY, -25, 25);
    j["gridNudgeX"] = std::clamp(gridNudgeX, -25, 25);
    j["lineNudgeY"] = std::clamp(lineNudgeY, -25, 25);
    j["wallpaperDim"] = std::clamp(wallpaperDim, 0, 100);
    j["vignetteStrength"] = std::clamp(vignetteStrength, 0, 100);
    j["uiRadiusPct"] = std::clamp(uiRadiusPct, 50, 200);
    j["iconShadowOffset"] = std::clamp(iconShadowOffset, 0, 20);
    j["iconShadowAlpha"] = std::clamp(iconShadowAlpha, 0, 60);
    j["iconVignette"] = std::clamp(iconVignette, 0, 100);
    j["iconHighlight"] = std::clamp(iconHighlight, 0, 100);
    j["iconDepth"] = std::clamp(iconDepth, 0, 100);
    j["motionStyle"] = std::clamp(motionStyle, 0, 2);
    j["lineNudgeX"] = std::clamp(lineNudgeX, -25, 25);
    for (int preset = 0; preset < 3; ++preset)
        j["layoutPreset" + std::to_string(preset + 1)] = layoutPreset[preset];
    j["wallpaperPath"] = wallpaperPath;
    j["wallpaperAnimated"] = wallpaperAnimated;
    j["wallpaperFps"] = std::clamp(wallpaperFps, 5, 30);
    j["backgroundDrift"] = backgroundDrift;
    j["backgroundDriftSpeed"] = std::clamp(backgroundDriftSpeed, 1, 100);
    j["sortMode"] = std::clamp(sortMode, 0, 2);
    j["alphaOrder"] = alphaOrder;
    j["sortLargeTiles"] = sortLargeTiles;
    j["iconContrastAdapt"] = iconContrastAdapt;
    j["batterySaver"] = batterySaver;
    j["lockScreenEnabled"] = lockScreenEnabled;
    j["powerButtonAction"] = std::clamp(powerButtonAction, 0, 2);
    j["lockScreenClockFont"] = lockScreenClockFont;
    j["lockScreenTransition"] = std::clamp(lockScreenTransition, 0, 3);
    j["lockScreenZoomDir"] = std::clamp(lockScreenZoomDir, 0, 1);
    j["lockScreenWipeDir"] = std::clamp(lockScreenWipeDir, 0, 3);
    j["heroBackdropResolution"] = std::clamp(heroBackdropResolution, 0, 2);
    j["heroBackdropBlur"] = std::clamp(heroBackdropBlur, 0, 100);
    j["lockPadCodeEnabled"] = lockPadCodeEnabled;
    j["lockPadCode"] = lockPadCode;
    j["gameShortcuts"] = gameShortcuts;
    j["hiddenGames"] = hiddenGames;
    j["gameNames"] = gameNames;
    j["dockedSizeOverrides"] = dockedSizeOverrides;
    j["handheldIconScale"] = handheldIconScale;
    j["handheldShortcutScale"] = handheldShortcutScale;
    j["dockedIconScale"] = dockedIconScale;
    j["dockedShortcutScale"] = dockedShortcutScale;
    j["themeLockClockFontOverride"] = themeLockClockFontOverride;
    j["themeIconPackOverride"] = themeIconPackOverride;
    j["themeMusicOverride"] = themeMusicOverride;
    j["themeMusicAddToPlaylist"] = themeMusicAddToPlaylist;
    j["themeMusicPlayOnApply"] = themeMusicPlayOnApply;
    {
        nlohmann::json tints = nlohmann::json::object();
        for (const auto& [packId, tint] : iconPackTints) {
            if (packId.empty())
                continue;
            tints[packId] = {
                {"enabled", tint.enabled},
                {"flatten", tint.flatten},
                {"grayscale", tint.grayscale},
                {"matchTheme", tint.matchTheme},
                {"hue", std::clamp(tint.hue, 0.f, 1.f)},
                {"sat", std::clamp(tint.sat, 0.f, 1.f)},
                {"light", std::clamp(tint.light, 0.f, 1.f)},
            };
        }
        j["iconPackTints"] = std::move(tints);
    }
    j["lastOpenedSequence"] = lastOpenedSequence;
    {
        nlohmann::json opened = nlohmann::json::object();
        char key[17];
        for (const auto& entry : lastOpened) {
            std::snprintf(key, sizeof(key), "%016llX",
                          static_cast<unsigned long long>(entry.first));
            opened[key] = entry.second;
        }
        j["lastOpened"] = std::move(opened);
    }
    j["themePreset"] = themePreset;
    j["iconPack"] = iconPack.empty() ? "default" : iconPack;
    j["autoThemeMode"] = (autoThemeMode == "manual" || autoThemeMode == "geo") ? autoThemeMode : "off";
    j["autoThemeDayPreset"] = autoThemeDayPreset;
    j["autoThemeNightPreset"] = autoThemeNightPreset;
    j["autoThemeDayStartHour"] = std::clamp(autoThemeDayStartHour, 0, 23);
    j["autoThemeNightStartHour"] = std::clamp(autoThemeNightStartHour, 0, 23);
    j["autoThemeGeoResolved"] = autoThemeGeoResolved;
    j["autoThemeGeoLat"] = autoThemeGeoLat;
    j["autoThemeGeoLon"] = autoThemeGeoLon;
    j["autoThemeGeoCity"] = autoThemeGeoCity;
    j["folderStyle"] = std::clamp(folderStyle, 0, switchu::folders::kFolderStyleCount - 1);
    j["folderShowCover"] = folderShowCover;
    j["folderDefaultView"] = std::clamp(folderDefaultView, 0, 2);

    // Written beside the real file and swapped in, never over it. Truncating
    // the live config and then dying mid-write is how a crash used to reset
    // every setting to its default: load() found a half-written file, failed to
    // parse it, and started from scratch. The staging file absorbs that risk.
    const std::string staging = std::string(kConfigPath) + ".part";

    {
        std::ofstream f(staging, std::ios::trunc);
        if (!f.is_open()) return false;
        f << j.dump(2);
        f.flush();
        if (!f.good()) {
            f.close();
            std::remove(staging.c_str());
            return false;
        }
    }

    // The outgoing copy becomes the backup rather than being deleted, so the
    // window in which neither file is complete costs nothing.
    std::remove(kBackupPath);
    std::rename(kConfigPath, kBackupPath);   // absent on the very first save
    if (std::rename(staging.c_str(), kConfigPath) != 0) {
        std::remove(staging.c_str());
        std::rename(kBackupPath, kConfigPath);
        return false;
    }
    // No commit here: save() is submitted to the thread pool, so this ran on a
    // worker while the main thread was also writing. Committing an fs session
    // from two threads at once is its own hazard, and the author's build -
    // which does not corrupt - commits nowhere.
    return true;
}
