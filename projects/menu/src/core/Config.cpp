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

std::string normalizeProfileUid(std::string uid) {
    if (uid.size() != 32)
        return {};
    for (char& c : uid) {
        if (c >= 'a' && c <= 'f')
            c = static_cast<char>(c - 'a' + 'A');
        else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
            return {};
    }
    return uid;
}

// Security/system state is shared by every account. Keeping these keys out of
// the per-user snapshot prevents a stale profile from changing who may unlock
// the console or which account launches by default.
bool isGlobalConfigKey(const std::string& key) {
    if (key == "activeProfileUid" || key == "defaultProfileEnabled"
        || key == "defaultProfileUid" || key == "tutorialCompleted"
        || key == "powerButtonAction")
        return true;
    return key.rfind("lock", 0) == 0;
}

std::string userConfigPath(const std::string& uid) {
    return std::string(AppConfig::kUsersDir) + "/" + uid + ".json";
}

bool userConfigExists(const std::string& uid) {
    if (uid.empty())
        return false;
    std::ifstream f(userConfigPath(uid));
    return f.is_open();
}

bool writeUserConfig(const nlohmann::json& configJson, const std::string& uid) {
    nlohmann::json j = configJson;
    for (auto it = j.begin(); it != j.end();) {
        if (isGlobalConfigKey(it.key()))
            it = j.erase(it);
        else
            ++it;
    }

    std::error_code ec;
    std::filesystem::create_directories(AppConfig::kUsersDir, ec);
    const std::string path = userConfigPath(uid);
    const std::string staging = path + ".part";
    const std::string backup = path + ".bak";

    {
        std::ofstream f(staging, std::ios::trunc);
        if (!f.is_open())
            return false;
        f << j.dump(2);
        f.flush();
        if (!f.good()) {
            f.close();
            std::remove(staging.c_str());
            return false;
        }
    }

    std::remove(backup.c_str());
    std::rename(path.c_str(), backup.c_str());
    if (std::rename(staging.c_str(), path.c_str()) != 0) {
        std::remove(staging.c_str());
        std::rename(backup.c_str(), path.c_str());
        return false;
    }
    return true;
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

    readJsonOpt(j, "activeProfileUid", activeProfileUid);
    activeProfileUid = normalizeProfileUid(activeProfileUid);
    if (activeProfileUid.empty()) {
        // Migration: the old Home launch profile becomes the first per-user
        // settings owner, so selecting that same account on boot does not force
        // a menu reload just to migrate.
        bool legacyEnabled = false;
        std::string legacyUid;
        readJsonOpt(j, "defaultProfileEnabled", legacyEnabled);
        readJsonOpt(j, "defaultProfileUid", legacyUid);
        if (legacyEnabled)
            activeProfileUid = normalizeProfileUid(legacyUid);
    }
    if (!activeProfileUid.empty()) {
        std::ifstream uf(userConfigPath(activeProfileUid));
        if (uf.is_open()) {
            try {
                nlohmann::json userJson;
                uf >> userJson;
                if (userJson.is_object()) {
                    for (auto it = userJson.begin(); it != userJson.end();) {
                        if (isGlobalConfigKey(it.key()))
                            it = userJson.erase(it);
                        else
                            ++it;
                    }
                    j.update(userJson);
                }
            } catch (...) {
                // A damaged profile falls back to the last global snapshot.
            }
        }
    }

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
    readJsonOpt(j, "allAppsColumns", allAppsColumns);
    readJsonOpt(j, "allAppsRows", allAppsRows);
    // 6x4 was the old default. Move it once to the larger 6x3 grid; a later
    // deliberate change is preserved by allAppsGridRevision.
    int allAppsGridRevision = 0;
    readJsonOpt(j, "allAppsGridRevision", allAppsGridRevision);
    if (allAppsGridRevision < 1 && allAppsColumns == 6 && allAppsRows == 4)
        allAppsRows = 3;
    readJsonOpt(j, "allAppsSort", allAppsSort);
    readJsonOpt(j, "allAppsFavourites", allAppsFavourites);
    readJsonOpt(j, "allAppsLastFocus", allAppsLastFocus);
    // Older configs have no homeLayout. A legacy "grid" choice becomes Shelf;
    // "dynamic_line" (and everything else) stays on the carousel.
    if (j.contains("homeLayout")) {
        std::string homeLayoutValue = "carousel";
        readJsonOpt(j, "homeLayout", homeLayoutValue);
        homeLayout = homeLayoutFromKey(homeLayoutValue);
    } else {
        std::string legacyMode = "dynamic_line";
        readJsonOpt(j, "appLayoutMode", legacyMode);
        homeLayout = legacyMode == "grid" ? HomeLayoutPreset::Shelf
                                            : HomeLayoutPreset::Carousel;
    }
    const HomeLayoutGeometry homeGeometry = homeLayoutGeometry(homeLayout);
    gridColumns = homeGeometry.columns;
    gridRows = homeGeometry.rows;
    appLayoutMode = homeLayoutIsGrid(homeLayout) ? AppLayoutMode::Grid
                                                 : AppLayoutMode::DynamicLine;
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
    readJsonOpt(j, "steamGridDbShowHeroes", steamGridDbShowHeroes);
    readJsonOpt(j, "steamGridDbShowInGrid", steamGridDbShowInGrid);
    readJsonOpt(j, "steamGridDbShowInDynamicLine", steamGridDbShowInDynamicLine);
    readJsonOpt(j, "steamGridDbShowInFolders", steamGridDbShowInFolders);
    readJsonOpt(j, "steamGridDbShowPosterInDynamicLine", steamGridDbShowPosterInDynamicLine);
    readJsonOpt(j, "steamGridDbShowLogoInDynamicLine", steamGridDbShowLogoInDynamicLine);
    readJsonOpt(j, "steamGridDbAspect", steamGridDbAspect);
    readJsonOpt(j, "steamGridDbApiKey", steamGridDbApiKey);
    // Older builds stored the opacity style under glass* keys.
    readJsonOpt(j, "glassTint", opacityTint);
    readJsonOpt(j, "glassSolid", opacitySolid);
    readJsonOpt(j, "opacityTint", opacityTint);
    readJsonOpt(j, "opacitySolid", opacitySolid);
    readJsonOpt(j, "highlightTint", highlightTint);
    readJsonOpt(j, "highlightGradientA", highlightGradientA);
    readJsonOpt(j, "highlightGradientB", highlightGradientB);
    readJsonOpt(j, "highlightThicknessPx", highlightThicknessPx);
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
    readJsonOpt(j, "shortcutGradient", shortcutGradient);
    readJsonOpt(j, "shortcutGradientA", shortcutGradientA);
    readJsonOpt(j, "shortcutGradientB", shortcutGradientB);
    shortcutGradientA = std::clamp(shortcutGradientA, 1, 9);
    shortcutGradientB = std::clamp(shortcutGradientB, 1, 9);
    readJsonOpt(j, "shortcutFlushTop", shortcutFlushTop);
    readJsonOpt(j, "shortcutNudgeY", shortcutNudgeY);
    readJsonOpt(j, "iconSpacing", iconSpacing);
    // Distance shrink replaced the old signed carousel depth. Map an existing
    // value so the row keeps the same look (depth 0 -> shrink 50).
    const bool legacyCarouselDepth =
        j.find("carouselShrink") == j.end() && j.find("carouselDepth") != j.end();
    if (j.find("carouselShrink") != j.end()) {
        readJsonOpt(j, "carouselShrink", carouselShrink);
    } else if (legacyCarouselDepth) {
        int depth = 0;
        readJsonOpt(j, "carouselDepth", depth);
        carouselShrink = std::clamp(50 - depth / 2, 0, 100);
    }
    readJsonOpt(j, "carouselFade", carouselFade);
    readJsonOpt(j, "carouselScrollSpeed", carouselScrollSpeed);
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
    readJsonOpt(j, "panelTintMix", panelTintMix);
    readJsonOpt(j, "panelBorderPct", panelBorderPct);
    readJsonOpt(j, "panelGrainPct", panelGrainPct);
    readJsonOpt(j, "panelShadowPct", panelShadowPct);
    readJsonOpt(j, "focusRingPx", focusRingPx);
    readJsonOpt(j, "focusGlowPct", focusGlowPct);
    readJsonOpt(j, "focusLiftPct", focusLiftPct);
    readJsonOpt(j, "focusOvershootPct", focusOvershootPct);
    readJsonOpt(j, "titleScalePct", titleScalePct);
    readJsonOpt(j, "titleCornerStyle", titleCornerStyle);
    readJsonOpt(j, "titleBackgroundPct", titleBackgroundPct);
    readJsonOpt(j, "hudGapPx", hudGapPx);
    readJsonOpt(j, "showHudClock", showHudClock);
    readJsonOpt(j, "showHudDate", showHudDate);
    readJsonOpt(j, "showHudBattery", showHudBattery);
    readJsonOpt(j, "showHudProfile", showHudProfile);
    readJsonOpt(j, "wallpaperBlurPx", wallpaperBlurPx);
    readJsonOpt(j, "wallpaperSaturationPct", wallpaperSaturationPct);
    readJsonOpt(j, "wallpaperSharpnessPct", wallpaperSharpnessPct);
    readJsonOpt(j, "wallpaperVibrancePct", wallpaperVibrancePct);
    readJsonOpt(j, "wallpaperNoisePct", wallpaperNoisePct);
    readJsonOpt(j, "backdropTintPct", backdropTintPct);
    readJsonOpt(j, "pageSlideMs", pageSlideMs);
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
        // Presets saved before the rename store the signed depth at index 5.
        if (legacyCarouselDepth && layoutPreset[preset].size() >= 6)
            layoutPreset[preset][5] =
                std::clamp(50 - layoutPreset[preset][5] / 2, 0, 100);
    }
    for (int slot = 0; slot < 3; ++slot) {
        auto& look = lookPresets[slot];
        const std::string key = "lookPreset" + std::to_string(slot + 1);
        auto it = j.find(key);
        if (it == j.end() || !it->is_object())
            continue;
        const auto& p = *it;
        readJsonOpt(p, "filled", look.filled);
        readJsonOpt(p, "name", look.name);
        readJsonOpt(p, "themePreset", look.themePreset);
        readJsonOpt(p, "opacityTint", look.opacityTint);
        readJsonOpt(p, "opacitySolid", look.opacitySolid);
        readJsonOpt(p, "highlightTint", look.highlightTint);
        readJsonOpt(p, "highlightGradientA", look.highlightGradientA);
        readJsonOpt(p, "highlightGradientB", look.highlightGradientB);
        readJsonOpt(p, "highlightThicknessPx", look.highlightThicknessPx);
        readJsonOpt(p, "panelTintMix", look.panelTintMix);
        readJsonOpt(p, "panelBorderPct", look.panelBorderPct);
        readJsonOpt(p, "panelGrainPct", look.panelGrainPct);
        readJsonOpt(p, "panelShadowPct", look.panelShadowPct);
        readJsonOpt(p, "focusRingPx", look.focusRingPx);
        readJsonOpt(p, "focusGlowPct", look.focusGlowPct);
        readJsonOpt(p, "focusLiftPct", look.focusLiftPct);
        readJsonOpt(p, "focusOvershootPct", look.focusOvershootPct);
        readJsonOpt(p, "titleScalePct", look.titleScalePct);
        readJsonOpt(p, "titleCornerStyle", look.titleCornerStyle);
        readJsonOpt(p, "titleBackgroundPct", look.titleBackgroundPct);
        readJsonOpt(p, "hudGapPx", look.hudGapPx);
        readJsonOpt(p, "showHudClock", look.showHudClock);
        readJsonOpt(p, "showHudDate", look.showHudDate);
        readJsonOpt(p, "showHudBattery", look.showHudBattery);
        readJsonOpt(p, "showHudProfile", look.showHudProfile);
        readJsonOpt(p, "wallpaperBlurPx", look.wallpaperBlurPx);
        readJsonOpt(p, "wallpaperSaturationPct", look.wallpaperSaturationPct);
        readJsonOpt(p, "wallpaperSharpnessPct", look.wallpaperSharpnessPct);
        readJsonOpt(p, "wallpaperVibrancePct", look.wallpaperVibrancePct);
        readJsonOpt(p, "wallpaperNoisePct", look.wallpaperNoisePct);
        readJsonOpt(p, "backdropTintPct", look.backdropTintPct);
        readJsonOpt(p, "pageSlideMs", look.pageSlideMs);
        readJsonOpt(p, "motionStyle", look.motionStyle);
        readJsonOpt(p, "uiRadiusPct", look.uiRadiusPct);
        readJsonOpt(p, "iconScale", look.iconScale);
        readJsonOpt(p, "iconSpacing", look.iconSpacing);
        readJsonOpt(p, "carouselShrink", look.carouselShrink);
        readJsonOpt(p, "iconVignette", look.iconVignette);
        readJsonOpt(p, "iconHighlight", look.iconHighlight);
        readJsonOpt(p, "iconShadowOffset", look.iconShadowOffset);
        readJsonOpt(p, "iconShadowAlpha", look.iconShadowAlpha);
        auto layout = p.find("layout");
        if (layout != p.end() && layout->is_array()) {
            look.layout.clear();
            for (const auto& value : *layout) {
                if (value.is_number_integer() && look.layout.size() < 9)
                    look.layout.push_back(value.get<int>());
            }
        }
    }
    readJsonOpt(j, "wallpaperPath", wallpaperPath);
    readJsonOpt(j, "wallpaperLastUsedPath", wallpaperLastUsedPath);
    readJsonOpt(j, "wallpaperAnimated", wallpaperAnimated);
    readJsonOpt(j, "wallpaperFps", wallpaperFps);
    readJsonOpt(j, "backgroundDrift", backgroundDrift);
    readJsonOpt(j, "backgroundDriftSpeed", backgroundDriftSpeed);
    readJsonOpt(j, "backgroundShapes", backgroundShapes);
    readJsonOpt(j, "heroFadeInMs", heroFadeInMs);
    readJsonOpt(j, "sortMode", sortMode);
    readJsonOpt(j, "alphaOrder", alphaOrder);
    readJsonOpt(j, "sortLargeTiles", sortLargeTiles);
    readJsonOpt(j, "iconContrastAdapt", iconContrastAdapt);
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
    lockPadCodes.clear();
    if (auto it = j.find("lockPadCodes"); it != j.end() && it->is_object()) {
        for (auto& [uid, value] : it->items()) {
            if (uid.empty() || !value.is_string())
                continue;
            const std::string raw = value.get<std::string>();
            std::string code;
            for (char c : raw) {
                if ((c == 'U' || c == 'D' || c == 'L' || c == 'R') && code.size() < 4)
                    code.push_back(c);
            }
            while (!raw.empty() && code.size() < 4)
                code.push_back('U');
            lockPadCodes[uid] = std::move(code);
        }
    }
    readJsonOpt(j, "shortcutBootTitleId", shortcutBootTitleId);
    readJsonOpt(j, "gameShortcuts", gameShortcuts);
    readJsonOpt(j, "gameShortcutSides", gameShortcutSides);
    readJsonOpt(j, "hiddenGames", hiddenGames);
    readJsonOpt(j, "gameNames", gameNames);
    heroOffsets.clear();
    if (auto it = j.find("heroOffsets"); it != j.end() && it->is_object()) {
        for (const auto& [key, value] : it->items()) {
            if (!value.is_array() || value.size() != 2
                || !value[0].is_number_integer() || !value[1].is_number_integer())
                continue;
            try {
                const auto id = static_cast<std::uint64_t>(std::stoull(key));
                if (id == 0)
                    continue;
                heroOffsets[id] = {
                    std::clamp(value[0].get<int>(), -100, 100),
                    std::clamp(value[1].get<int>(), -100, 100)};
            } catch (...) {
            }
        }
    }
    readJsonOpt(j, "dockedSizeOverrides", dockedSizeOverrides);
    readJsonOpt(j, "handheldIconScale", handheldIconScale);
    readJsonOpt(j, "handheldShortcutScale", handheldShortcutScale);
    readJsonOpt(j, "dockedIconScale", dockedIconScale);
    readJsonOpt(j, "dockedShortcutScale", dockedShortcutScale);
    const bool hasDockedCarouselX = j.find("dockedCarouselX") != j.end();
    const bool hasDockedCarouselY = j.find("dockedCarouselY") != j.end();
    readJsonOpt(j, "dockedCarouselX", dockedCarouselX);
    readJsonOpt(j, "dockedCarouselY", dockedCarouselY);
    if (!hasDockedCarouselX) dockedCarouselX = lineNudgeX;
    if (!hasDockedCarouselY) dockedCarouselY = lineNudgeY;
    if (handheldIconScale == 0) handheldIconScale = iconScale;
    if (handheldShortcutScale == 0) handheldShortcutScale = sidebarScale;
    if (dockedIconScale <= 0) dockedIconScale = handheldIconScale;
    if (dockedShortcutScale <= 0) dockedShortcutScale = handheldShortcutScale;
    dockedIconScale = std::clamp(dockedIconScale, 70, 130);
    dockedShortcutScale = std::clamp(dockedShortcutScale, 70, 150);
    dockedCarouselX = std::clamp(dockedCarouselX, -25, 25);
    dockedCarouselY = std::clamp(dockedCarouselY, -25, 25);
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
    allAppsColumns = std::clamp(allAppsColumns, 3, 8);
    allAppsRows = std::clamp(allAppsRows, 2, 5);
    allAppsSort = std::clamp(allAppsSort, 0, 2);
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
    opacityTint = std::clamp(opacityTint, 0, 9);
    highlightTint = std::clamp(highlightTint, 0, 10);
    highlightGradientA = std::clamp(highlightGradientA, 1, 9);
    highlightGradientB = std::clamp(highlightGradientB, 1, 9);
    highlightThicknessPx = std::clamp(highlightThicknessPx, 1, 8);
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
    carouselShrink = std::clamp(carouselShrink, 0, 100);
    carouselFade = std::clamp(carouselFade, 0, 100);
    carouselScrollSpeed = std::clamp(carouselScrollSpeed, 0, 100);
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
    panelTintMix = std::clamp(panelTintMix, 0, 100);
    panelBorderPct = std::clamp(panelBorderPct, 0, 200);
    panelGrainPct = std::clamp(panelGrainPct, 0, 100);
    panelShadowPct = std::clamp(panelShadowPct, 0, 100);
    focusRingPx = std::clamp(focusRingPx, 1, 6);
    focusGlowPct = std::clamp(focusGlowPct, 0, 100);
    focusLiftPct = std::clamp(focusLiftPct, 0, 150);
    focusOvershootPct = std::clamp(focusOvershootPct, 0, 100);
    titleScalePct = std::clamp(titleScalePct, 80, 120);
    titleCornerStyle = std::clamp(titleCornerStyle, 0, 2);
    titleBackgroundPct = std::clamp(titleBackgroundPct, 0, 100);
    hudGapPx = std::clamp(hudGapPx, 0, 24);
    wallpaperBlurPx = std::clamp(wallpaperBlurPx, 0, 16);
    wallpaperSaturationPct = std::clamp(wallpaperSaturationPct, 0, 150);
    wallpaperSharpnessPct = std::clamp(wallpaperSharpnessPct, 0, 100);
    wallpaperVibrancePct = std::clamp(wallpaperVibrancePct, 0, 100);
    wallpaperNoisePct = std::clamp(wallpaperNoisePct, 0, 100);
    backdropTintPct = std::clamp(backdropTintPct, 0, 100);
    pageSlideMs = std::clamp(pageSlideMs, 150, 600);
    for (auto& look : lookPresets) {
        look.opacityTint = std::clamp(look.opacityTint, 0, 9);
        look.highlightTint = std::clamp(look.highlightTint, 0, 10);
        look.highlightGradientA = std::clamp(look.highlightGradientA, 1, 9);
        look.highlightGradientB = std::clamp(look.highlightGradientB, 1, 9);
        look.highlightThicknessPx = std::clamp(look.highlightThicknessPx, 1, 8);
        look.panelTintMix = std::clamp(look.panelTintMix, 0, 100);
        look.panelBorderPct = std::clamp(look.panelBorderPct, 0, 200);
        look.panelGrainPct = std::clamp(look.panelGrainPct, 0, 100);
        look.panelShadowPct = std::clamp(look.panelShadowPct, 0, 100);
        look.focusRingPx = std::clamp(look.focusRingPx, 1, 6);
        look.focusGlowPct = std::clamp(look.focusGlowPct, 0, 100);
        look.focusLiftPct = std::clamp(look.focusLiftPct, 0, 150);
        look.focusOvershootPct = std::clamp(look.focusOvershootPct, 0, 100);
        look.titleScalePct = std::clamp(look.titleScalePct, 80, 120);
        look.titleCornerStyle = std::clamp(look.titleCornerStyle, 0, 2);
        look.titleBackgroundPct = std::clamp(look.titleBackgroundPct, 0, 100);
        look.hudGapPx = std::clamp(look.hudGapPx, 0, 24);
        look.wallpaperBlurPx = std::clamp(look.wallpaperBlurPx, 0, 16);
        look.wallpaperSaturationPct = std::clamp(look.wallpaperSaturationPct, 0, 150);
        look.wallpaperSharpnessPct = std::clamp(look.wallpaperSharpnessPct, 0, 100);
        look.wallpaperVibrancePct = std::clamp(look.wallpaperVibrancePct, 0, 100);
        look.wallpaperNoisePct = std::clamp(look.wallpaperNoisePct, 0, 100);
        look.backdropTintPct = std::clamp(look.backdropTintPct, 0, 100);
        look.pageSlideMs = std::clamp(look.pageSlideMs, 150, 600);
        look.motionStyle = std::clamp(look.motionStyle, 0, 2);
        look.uiRadiusPct = std::clamp(look.uiRadiusPct, 50, 200);
        look.iconScale = std::clamp(look.iconScale, 70, 130);
        look.iconSpacing = std::clamp(look.iconSpacing, -50, 100);
        look.carouselShrink = std::clamp(look.carouselShrink, 0, 100);
        look.iconVignette = std::clamp(look.iconVignette, 0, 100);
        look.iconHighlight = std::clamp(look.iconHighlight, 0, 100);
        look.iconShadowOffset = std::clamp(look.iconShadowOffset, 0, 20);
        look.iconShadowAlpha = std::clamp(look.iconShadowAlpha, 0, 60);
    }
    iconVignette = std::clamp(iconVignette, 0, 100);
    iconHighlight = std::clamp(iconHighlight, 0, 100);
    motionStyle = std::clamp(motionStyle, 0, 2);
    lineNudgeX = std::clamp(lineNudgeX, -25, 25);
    wallpaperFps = std::clamp(wallpaperFps, 5, 30);
    backgroundDriftSpeed = std::clamp(backgroundDriftSpeed, 1, 100);
    heroFadeInMs = std::clamp(heroFadeInMs, 0, 10000);
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

bool AppConfig::save(bool writeUserSettings) const {
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
    j["allAppsColumns"] = std::clamp(allAppsColumns, 3, 8);
    j["allAppsRows"] = std::clamp(allAppsRows, 2, 5);
    j["allAppsGridRevision"] = 1;
    j["allAppsSort"] = std::clamp(allAppsSort, 0, 2);
    j["allAppsFavourites"] = allAppsFavourites;
    j["allAppsLastFocus"] = allAppsLastFocus;
    j["homeLayout"] = homeLayoutKey(homeLayout);
    j["appLayoutMode"] = appLayoutMode == AppLayoutMode::Grid
        ? "grid" : "dynamic_line";
    j["actionHintStyle"] = actionHintStyle == "panel" ? "panel" : "capsules";
    j["cursorMotionMode"] = std::clamp(cursorMotionMode, 0, 1);
    j["pageTransitionMode"] = std::clamp(pageTransitionMode, 0, 1);
    j["uiLanguageOverride"] = uiLanguageOverride;
    j["soundPreset"] = soundPreset;
    j["defaultProfileEnabled"] = defaultProfileEnabled;
    j["defaultProfileUid"] = defaultProfileEnabled ? defaultProfileUid : std::string();
    j["activeProfileUid"] = activeProfileUid;
    j["tutorialCompleted"] = tutorialCompleted;
    j["clockUse12Hour"] = clockUse12Hour;
    j["webClockSyncEnabled"] = webClockSyncEnabled;
    j["accessibilityEnabled"] = accessibilityEnabled;
    j["accessibilitySpeakHints"] = accessibilitySpeakHints;
    j["accessibilitySpeakContextEveryFocus"] = accessibilitySpeakContextEveryFocus;
    j["accessibilitySpeakPosition"] = accessibilitySpeakPosition;
    j["accessibilitySpeechRate"] = std::clamp(accessibilitySpeechRate, 120, 320);
    j["steamGridDbEnabled"] = steamGridDbEnabled;
    j["steamGridDbShowHeroes"] = steamGridDbShowHeroes;
    j["steamGridDbShowInGrid"] = steamGridDbShowInGrid;
    j["steamGridDbShowInDynamicLine"] = steamGridDbShowInDynamicLine;
    j["steamGridDbShowInFolders"] = steamGridDbShowInFolders;
    j["steamGridDbShowPosterInDynamicLine"] = steamGridDbShowPosterInDynamicLine;
    j["steamGridDbShowLogoInDynamicLine"] = steamGridDbShowLogoInDynamicLine;
    j["steamGridDbAspect"] = std::clamp(steamGridDbAspect, 0, 3);
    j["steamGridDbApiKey"] = steamGridDbApiKey;
    j["opacityTint"] = std::clamp(opacityTint, 0, 9);
    j["opacitySolid"] = opacitySolid;
    j["highlightTint"] = std::clamp(highlightTint, 0, 10);
    j["highlightGradientA"] = std::clamp(highlightGradientA, 1, 9);
    j["highlightGradientB"] = std::clamp(highlightGradientB, 1, 9);
    j["highlightThicknessPx"] = std::clamp(highlightThicknessPx, 1, 8);
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
    j["shortcutGradient"] = shortcutGradient;
    j["shortcutGradientA"] = std::clamp(shortcutGradientA, 1, 9);
    j["shortcutGradientB"] = std::clamp(shortcutGradientB, 1, 9);
    j["shortcutFlushTop"] = shortcutFlushTop;
    j["shortcutNudgeY"] = std::clamp(shortcutNudgeY, -24, 24);
    j["iconSpacing"] = std::clamp(iconSpacing, -50, 100);
    j["carouselShrink"] = std::clamp(carouselShrink, 0, 100);
    j["carouselFade"] = std::clamp(carouselFade, 0, 100);
    j["carouselScrollSpeed"] = std::clamp(carouselScrollSpeed, 0, 100);
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
    j["panelTintMix"] = std::clamp(panelTintMix, 0, 100);
    j["panelBorderPct"] = std::clamp(panelBorderPct, 0, 200);
    j["panelGrainPct"] = std::clamp(panelGrainPct, 0, 100);
    j["panelShadowPct"] = std::clamp(panelShadowPct, 0, 100);
    j["focusRingPx"] = std::clamp(focusRingPx, 1, 6);
    j["focusGlowPct"] = std::clamp(focusGlowPct, 0, 100);
    j["focusLiftPct"] = std::clamp(focusLiftPct, 0, 150);
    j["focusOvershootPct"] = std::clamp(focusOvershootPct, 0, 100);
    j["titleScalePct"] = std::clamp(titleScalePct, 80, 120);
    j["titleCornerStyle"] = std::clamp(titleCornerStyle, 0, 2);
    j["titleBackgroundPct"] = std::clamp(titleBackgroundPct, 0, 100);
    j["hudGapPx"] = std::clamp(hudGapPx, 0, 24);
    j["showHudClock"] = showHudClock;
    j["showHudDate"] = showHudDate;
    j["showHudBattery"] = showHudBattery;
    j["showHudProfile"] = showHudProfile;
    j["wallpaperBlurPx"] = std::clamp(wallpaperBlurPx, 0, 16);
    j["wallpaperSaturationPct"] = std::clamp(wallpaperSaturationPct, 0, 150);
    j["wallpaperSharpnessPct"] = std::clamp(wallpaperSharpnessPct, 0, 100);
    j["wallpaperVibrancePct"] = std::clamp(wallpaperVibrancePct, 0, 100);
    j["wallpaperNoisePct"] = std::clamp(wallpaperNoisePct, 0, 100);
    j["backdropTintPct"] = std::clamp(backdropTintPct, 0, 100);
    j["pageSlideMs"] = std::clamp(pageSlideMs, 150, 600);
    j["motionStyle"] = std::clamp(motionStyle, 0, 2);
    j["lineNudgeX"] = std::clamp(lineNudgeX, -25, 25);
    for (int preset = 0; preset < 3; ++preset)
        j["layoutPreset" + std::to_string(preset + 1)] = layoutPreset[preset];
    for (int slot = 0; slot < 3; ++slot) {
        const auto& look = lookPresets[slot];
        nlohmann::json p;
        p["filled"] = look.filled;
        p["name"] = look.name;
        p["themePreset"] = look.themePreset;
        p["opacityTint"] = look.opacityTint;
        p["opacitySolid"] = look.opacitySolid;
        p["highlightTint"] = look.highlightTint;
        p["highlightGradientA"] = look.highlightGradientA;
        p["highlightGradientB"] = look.highlightGradientB;
        p["highlightThicknessPx"] = look.highlightThicknessPx;
        p["panelTintMix"] = look.panelTintMix;
        p["panelBorderPct"] = look.panelBorderPct;
        p["panelGrainPct"] = look.panelGrainPct;
        p["panelShadowPct"] = look.panelShadowPct;
        p["focusRingPx"] = look.focusRingPx;
        p["focusGlowPct"] = look.focusGlowPct;
        p["focusLiftPct"] = look.focusLiftPct;
        p["focusOvershootPct"] = look.focusOvershootPct;
        p["titleScalePct"] = look.titleScalePct;
        p["titleCornerStyle"] = look.titleCornerStyle;
        p["titleBackgroundPct"] = look.titleBackgroundPct;
        p["hudGapPx"] = look.hudGapPx;
        p["showHudClock"] = look.showHudClock;
        p["showHudDate"] = look.showHudDate;
        p["showHudBattery"] = look.showHudBattery;
        p["showHudProfile"] = look.showHudProfile;
        p["wallpaperBlurPx"] = look.wallpaperBlurPx;
        p["wallpaperSaturationPct"] = look.wallpaperSaturationPct;
        p["wallpaperSharpnessPct"] = look.wallpaperSharpnessPct;
        p["wallpaperVibrancePct"] = look.wallpaperVibrancePct;
        p["wallpaperNoisePct"] = look.wallpaperNoisePct;
        p["backdropTintPct"] = look.backdropTintPct;
        p["pageSlideMs"] = look.pageSlideMs;
        p["motionStyle"] = look.motionStyle;
        p["uiRadiusPct"] = look.uiRadiusPct;
        p["iconScale"] = look.iconScale;
        p["iconSpacing"] = look.iconSpacing;
        p["carouselShrink"] = look.carouselShrink;
        p["iconVignette"] = look.iconVignette;
        p["iconHighlight"] = look.iconHighlight;
        p["iconShadowOffset"] = look.iconShadowOffset;
        p["iconShadowAlpha"] = look.iconShadowAlpha;
        p["layout"] = look.layout;
        j["lookPreset" + std::to_string(slot + 1)] = std::move(p);
    }
    j["wallpaperPath"] = wallpaperPath;
    j["wallpaperLastUsedPath"] = wallpaperLastUsedPath;
    j["wallpaperAnimated"] = wallpaperAnimated;
    j["wallpaperFps"] = std::clamp(wallpaperFps, 5, 30);
    j["backgroundDrift"] = backgroundDrift;
    j["backgroundDriftSpeed"] = std::clamp(backgroundDriftSpeed, 1, 100);
    j["backgroundShapes"] = backgroundShapes;
    j["heroFadeInMs"] = std::clamp(heroFadeInMs, 0, 10000);
    j["sortMode"] = std::clamp(sortMode, 0, 2);
    j["alphaOrder"] = alphaOrder;
    j["sortLargeTiles"] = sortLargeTiles;
    j["iconContrastAdapt"] = iconContrastAdapt;
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
    {
        nlohmann::json codes = nlohmann::json::object();
        for (const auto& [uid, code] : lockPadCodes) {
            if (!uid.empty())
                codes[uid] = code;
        }
        j["lockPadCodes"] = std::move(codes);
    }
    j["shortcutBootTitleId"] = shortcutBootTitleId;
    j["gameShortcuts"] = gameShortcuts;
    j["gameShortcutSides"] = gameShortcutSides;
    j["hiddenGames"] = hiddenGames;
    j["gameNames"] = gameNames;
    {
        nlohmann::json offsets = nlohmann::json::object();
        for (const auto& [id, offset] : heroOffsets) {
            if (id == 0)
                continue;
            offsets[std::to_string(id)] = {
                std::clamp(offset.first, -100, 100),
                std::clamp(offset.second, -100, 100)};
        }
        j["heroOffsets"] = std::move(offsets);
    }
    j["dockedSizeOverrides"] = dockedSizeOverrides;
    j["handheldIconScale"] = handheldIconScale;
    j["handheldShortcutScale"] = handheldShortcutScale;
    j["dockedIconScale"] = dockedIconScale;
    j["dockedShortcutScale"] = dockedShortcutScale;
    j["dockedCarouselX"] = std::clamp(dockedCarouselX, -25, 25);
    j["dockedCarouselY"] = std::clamp(dockedCarouselY, -25, 25);
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
    if (writeUserSettings && !activeProfileUid.empty()
        && !writeUserConfig(j, activeProfileUid))
        return false;
    // No commit here: save() is submitted to the thread pool, so this ran on a
    // worker while the main thread was also writing. Committing an fs session
    // from two threads at once is its own hazard, and the author's build -
    // which does not corrupt - commits nowhere.
    return true;
}

bool AppConfig::selectActiveUser(const std::string& uidHex) {
    const std::string uid = normalizeProfileUid(uidHex);
    const bool hasUser = !uid.empty();
    const bool seed = hasUser && !userConfigExists(uid);
    activeProfileUid = uid;
    defaultProfileEnabled = hasUser;
    defaultProfileUid = uid;
    return save(seed);
}
