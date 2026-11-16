#include "WiiUMenuApp.hpp"
#include "ui/UiStyle.hpp"
#include "ui/FocusHighlight.hpp"
#include <switch.h>
#include <switchu/control_cache.hpp>
#include "ui/AccentPalette.hpp"
#include "icons/IconPackInstaller.hpp"
#include "icons/IconPackManager.hpp"
#include "themeshop/ThemePackageInstaller.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "widgets/AdaptiveIconLoad.hpp"
#include "widgets/GlossyIcon.hpp"
#include "DebugLog.hpp"
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#endif

#include <nxui/core/I18n.hpp>
#include <switchu/control_cache.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

// Defined in WiiUMenuApp.cpp (shared theme-sound helpers).
std::string resolveThemeSoundBase(const std::string& installPath);
size_t appendMp3TracksFromDir(AudioManager& audio,
                              const std::string& musicDir,
                              const std::string& albumFolder);

namespace {

std::string accountUidToHex(const AccountUid& uid) {
    char buf[33] = {};
    std::snprintf(buf, sizeof(buf), "%016llX%016llX",
                  static_cast<unsigned long long>(uid.uid[0]),
                  static_cast<unsigned long long>(uid.uid[1]));
    return std::string(buf);
}

// The dock state last acted on can lag a real dock/undock by a frame, and the
// settings callbacks used to trust that cached value. Read the mode at the
// edit itself so a docked-size change always lands on the sizes in use.
bool dockedNow() {
    return appletGetOperationMode() == AppletOperationMode_Console;
}

bool removeDirectoryRecursive(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
}

bool pathExists(const std::string& path) {
    if (path.empty())
        return false;

    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty())
        return name;
    if (name.empty())
        return base;
    if (base.back() == '/')
        return base + name;
    return base + "/" + name;
}

std::string themeFolderFromInstallPath(const std::string& installPath) {
    if (installPath.empty())
        return {};
    const auto pos = installPath.find_last_of("/\\");
    return (pos == std::string::npos) ? installPath : installPath.substr(pos + 1);
}

std::string themeFolderFromPreset(const ThemePreset& preset) {
    std::string folder = themeFolderFromInstallPath(preset.installPath);
    if (!folder.empty())
        return folder;
    std::string id = preset.id.empty() ? preset.name : preset.id;
    if (id.rfind("package:", 0) == 0)
        id = id.substr(8);
    return id;
}

std::string resolvedThemeIconPackId(const ThemePreset& preset, const std::string& themeFolder) {
    if (!preset.icons.packId.empty())
        return preset.icons.packId;
    return themeFolder;
}

bool writeIconPackManifest(const std::string& destDir,
                           const std::string& packId,
                           const std::string& name,
                           const std::string& author) {
    if (destDir.empty() || packId.empty())
        return false;
    nlohmann::json root = {
        {"id", packId},
        {"name", name.empty() ? packId : name},
        {"author", author.empty() ? "Theme" : author},
        {"version", "1.0.0"},
    };
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::ofstream out(joinPath(destDir, "pack.json"), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << root.dump(2) << '\n';
    return static_cast<bool>(out);
}

bool isAbsoluteThemePath(const std::string& path) {
    return (!path.empty() && path.front() == '/') || (path.find(":/") != std::string::npos);
}

std::string trimSlashes(std::string path) {
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    return path;
}

bool readJsonString(const nlohmann::json& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        return false;
    out = it->get<std::string>();
    return !out.empty();
}

std::string installedThemePreviewPath(const ThemePreset& preset) {
    if (preset.installPath.empty())
        return {};

    auto resolveLocal = [&](const std::string& raw) -> std::string {
        if (raw.empty() || raw.rfind("http://", 0) == 0 || raw.rfind("https://", 0) == 0)
            return {};
        std::string path = isAbsoluteThemePath(raw) ? raw : joinPath(preset.installPath, trimSlashes(raw));
        return pathExists(path) ? path : std::string();
    };

    nlohmann::json manifest;
    std::ifstream input(joinPath(preset.installPath, "theme.json"));
    if (input) {
        try {
            input >> manifest;
        } catch (...) {
            manifest = nlohmann::json{};
        }
    }

    if (manifest.is_object()) {
        std::string raw;
        if ((readJsonString(manifest, "cover", raw)
             || readJsonString(manifest, "screenshot", raw)
             || readJsonString(manifest, "thumbnail", raw))) {
            std::string path = resolveLocal(raw);
            if (!path.empty())
                return path;
        }

        auto previewIt = manifest.find("preview");
        if (previewIt != manifest.end() && previewIt->is_object()) {
            raw.clear();
            if ((readJsonString(*previewIt, "cover", raw)
                 || readJsonString(*previewIt, "screenshot", raw)
                 || readJsonString(*previewIt, "thumbnail", raw))) {
                std::string path = resolveLocal(raw);
                if (!path.empty())
                    return path;
            }

            auto screenshotsIt = previewIt->find("screenshots");
            if (screenshotsIt != previewIt->end() && screenshotsIt->is_array()) {
                for (const auto& value : *screenshotsIt) {
                    if (!value.is_string())
                        continue;
                    std::string path = resolveLocal(value.get<std::string>());
                    if (!path.empty())
                        return path;
                }
            }
        }

        auto screenshotsIt = manifest.find("screenshots");
        if (screenshotsIt != manifest.end() && screenshotsIt->is_array()) {
            for (const auto& value : *screenshotsIt) {
                if (!value.is_string())
                    continue;
                std::string path = resolveLocal(value.get<std::string>());
                if (!path.empty())
                    return path;
            }
        }
    }

    static constexpr const char* kFallbacks[] = {
        "media/screenshots/00.png",
        "media/screenshots/cover.png",
        "preview/cover.png",
        "preview/screenshot.png",
        "preview/cover.jpg",
        "preview/screenshot.jpg",
        "screenshots/cover.png",
        "screenshots/screenshot.png",
        "cover.png",
        "screenshot.png",
    };
    for (const char* fallback : kFallbacks) {
        std::string path = joinPath(preset.installPath, fallback);
        if (pathExists(path))
            return path;
    }

    return {};
}

const char* safeLogPath(const std::string& path) {
    return path.empty() ? "<empty>" : path.c_str();
}

const char* themeSourceName(ThemePresetSource source) {
    switch (source) {
        case ThemePresetSource::BuiltIn: return "builtin";
        case ThemePresetSource::UserPreset: return "user";
        case ThemePresetSource::InstalledPackage: return "package";
        default: return "unknown";
    }
}

WaraWaraBackground::Layout toBackgroundLayout(ThemeBackgroundLayout layout) {
    switch (layout) {
        case ThemeBackgroundLayout::Grid:
            return WaraWaraBackground::Layout::Grid;
        case ThemeBackgroundLayout::Floating:
        default:
            return WaraWaraBackground::Layout::Floating;
    }
}

WaraWaraBackground::ShapeSet toBackgroundShapeSet(ThemeBackgroundShapeSet shapeSet) {
    switch (shapeSet) {
        case ThemeBackgroundShapeSet::Circle:
            return WaraWaraBackground::ShapeSet::Circle;
        case ThemeBackgroundShapeSet::Triangle:
            return WaraWaraBackground::ShapeSet::Triangle;
        case ThemeBackgroundShapeSet::Square:
            return WaraWaraBackground::ShapeSet::Square;
        case ThemeBackgroundShapeSet::Diamond:
            return WaraWaraBackground::ShapeSet::Diamond;
        case ThemeBackgroundShapeSet::Hexagon:
            return WaraWaraBackground::ShapeSet::Hexagon;
        case ThemeBackgroundShapeSet::Mixed:
        default:
            return WaraWaraBackground::ShapeSet::Mixed;
    }
}

WaraWaraBackground::Symmetry toBackgroundSymmetry(ThemeBackgroundSymmetry symmetry) {
    switch (symmetry) {
        case ThemeBackgroundSymmetry::MirrorX:
            return WaraWaraBackground::Symmetry::MirrorHorizontal;
        case ThemeBackgroundSymmetry::MirrorY:
            return WaraWaraBackground::Symmetry::MirrorVertical;
        case ThemeBackgroundSymmetry::Quad:
            return WaraWaraBackground::Symmetry::Quad;
        case ThemeBackgroundSymmetry::None:
        default:
            return WaraWaraBackground::Symmetry::None;
    }
}

std::string sourceLabel(ThemePresetSource source) {
    auto& i18n = nxui::I18n::instance();
    switch (source) {
        case ThemePresetSource::BuiltIn:
            return i18n.tr("themeshop.source.builtin", "Built-in");
        case ThemePresetSource::UserPreset:
            return i18n.tr("themeshop.source.custom", "Custom");
        case ThemePresetSource::InstalledPackage:
            return i18n.tr("themeshop.source.community", "Community");
    }

    return i18n.tr("themeshop.source.unknown", "Unknown");
}

std::string defaultThemeRef() {
    return "builtin:Default Light";
}

} // namespace

void WiiUMenuApp::applyHeroBackdropResolution() {
    if (!m_steamGridDbBackdrop)
        return;
    const int level = std::clamp(m_config.heroBackdropResolution, 0, 2);
    const int width = (level == 2) ? 640 : (level == 1 ? 960 : 1280);
    const int height = (level == 2) ? 360 : (level == 1 ? 540 : 720);
    m_steamGridDbBackdrop->setHeroCanvas(width, height);
    m_steamGridDbBackdrop->setHeroFadeIn(m_config.heroFadeInMs / 1000.f);
}

void WiiUMenuApp::applyHeroBackdropBlur() {
    if (!m_steamGridDbBackdrop)
        return;
    m_steamGridDbBackdrop->setBlur(
        static_cast<float>(std::clamp(m_config.heroBackdropBlur, 0, 100)) / 100.f);
}

void WiiUMenuApp::createSettings() {
    if (m_settings) return;

    m_settings = std::make_shared<SettingsScreen>();
    if (m_overlayLayer) {
        m_overlayLayer->addChild(m_settings);
    }
    m_settings->setFont(&m_fontNormal);
    m_settings->setSmallFont(&m_fontSmall);
    m_settings->setTheme(&m_theme);
    m_settings->setInstantCursorMotion(m_config.cursorMotionMode == 1);
    m_settings->setWireframeState(m_showWireframe);
    m_settings->setHomeLayoutState(m_config.homeLayout);
    m_settings->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_settings->onHomeLayoutChange([this](HomeLayoutPreset preset) {
        setHomeLayoutPreset(preset);
    });
    m_settings->setAllAppsLayoutState(m_config.allAppsColumns, m_config.allAppsRows);
    m_settings->onAllAppsColumnsChange([this](int columns) {
        columns = std::clamp(columns, 3, 8);
        if (m_config.allAppsColumns == columns)
            return;
        m_config.allAppsColumns = columns;
        m_config.save();
        if (m_allAppsScreen)
            m_allAppsScreen->setGridShape(m_config.allAppsColumns, m_config.allAppsRows);
    });
    m_settings->onAllAppsRowsChange([this](int rows) {
        rows = std::clamp(rows, 2, 5);
        if (m_config.allAppsRows == rows)
            return;
        m_config.allAppsRows = rows;
        m_config.save();
        if (m_allAppsScreen)
            m_allAppsScreen->setGridShape(m_config.allAppsColumns, m_config.allAppsRows);
    });
    m_settings->setUiLanguageOverride(m_config.uiLanguageOverride);
    m_settings->setClockUse12HourState(m_config.clockUse12Hour);
    m_settings->setWebClockSyncState(m_config.webClockSyncEnabled);
    m_settings->setAccessibilityEnabledState(m_config.accessibilityEnabled);
    m_settings->setAccessibilitySpeechState(m_config.accessibilitySpeakHints,
                                            m_config.accessibilitySpeakContextEveryFocus,
                                            m_config.accessibilitySpeakPosition,
                                            m_config.accessibilitySpeechRate);
    m_settings->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                  m_config.accessibilitySpeakPosition);
    m_settings->setSteamGridDbState(m_config.steamGridDbEnabled,
                                    !m_config.steamGridDbApiKey.empty(),
                                    m_config.steamGridDbShowInGrid,
                                    m_config.steamGridDbShowInDynamicLine,
                                    m_config.steamGridDbShowInFolders,
                                    m_config.steamGridDbShowPosterInDynamicLine,
                                    m_config.steamGridDbShowLogoInDynamicLine,
                                    m_config.steamGridDbShowHeroes);
    m_settings->setHeroBackdropResolutionState(m_config.heroBackdropResolution);
    m_settings->setHeroBackdropBlurState(m_config.heroBackdropBlur);

    m_settings->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_settings->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_settings->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_settings->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_settings->onAccessibilityStructuredAnnouncement([this](const std::string& context,
                                                             const std::string& position,
                                                             const std::string& summary,
                                                             bool forceRepeat,
                                                             bool forceContext) {
        m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
    });
    m_settings->onToggleSfx([this](bool on) {
        m_audio.playSfx(on ? Sfx::ThemeToggle : Sfx::ToggleOff);
    });
    m_settings->onSliderSfx([this](bool up) {
        m_audio.playSfx(up ? Sfx::SliderUp : Sfx::SliderDown);
    });
    m_settings->onWireframeChange([this](bool enabled) {
        m_showWireframe = enabled;
        app().renderer().setBoxWireframeEnabled(enabled);
    });
    m_settings->onGridColumnsChange([this](int cols) {
        cols = std::clamp(cols, 1, 8);
        if (m_config.gridColumns == cols)
            return;
        m_config.gridColumns = cols;
        reflowHomeGrid();
    });
    m_settings->onGridRowsChange([this](int rows) {
        rows = std::clamp(rows, 1, 5);
        if (m_config.gridRows == rows)
            return;
        m_config.gridRows = rows;
        reflowHomeGrid();
    });
    m_settings->setIconPresentationState(m_config.iconCorners,
                                         m_config.gridNudgeX, m_config.lineNudgeY,
                                         m_config.iconScale, m_config.iconSpacing,
                                         m_config.carouselShrink, m_config.lineNudgeX,
                                         m_config.iconVignette, m_config.iconHighlight);
    m_settings->setIconCornerPctState(m_config.iconCornerPct);
    refreshLockProfilesState();
    refreshHiddenGamesState();
    m_settings->setSortModeState(m_config.sortMode);
    m_settings->setAlphaOrderState(m_config.alphaOrder);
    m_settings->setDockedState(m_config.dockedSizeOverrides, m_config.dockedIconScale, m_config.dockedShortcutScale);
    m_settings->setDockedCarouselState(m_config.dockedCarouselX, m_config.dockedCarouselY);
    m_settings->setDockedOutputProvider([this]() {
        const auto& gpu = app().gpu();
        const bool docked = dockedNow();
        return SettingsScreen::DockedOutputState{
            gpu.framebufferWidth(), gpu.framebufferHeight(), gpu.displayScale(),
            docked};
    });
    m_settings->setSidebarScaleState(m_config.sidebarScale);
    m_settings->setShortcutLayoutState(m_config.shortcutEdge, m_config.shortcutNudgeX,
                                       m_config.shortcutNudgeXRight);
    m_settings->setShortcutStripState(m_config.shortcutStrip);
    m_settings->setShortcutGradientState(m_config.shortcutGradient,
                                         m_config.shortcutGradientA,
                                         m_config.shortcutGradientB);
    m_settings->setShortcutFlushTopState(m_config.shortcutFlushTop);
    m_settings->setShortcutNudgeYState(m_config.shortcutNudgeY);
    m_settings->setHudChromeState(m_config.hideActionHints, m_config.hudScale);
    m_settings->setClockLayoutState(m_config.clockNudgeX, m_config.clockScale);
    m_settings->setClockVerticalState(m_config.clockNudgeY);
    m_settings->setVisualTreatmentState(m_config.wallpaperDim, m_config.vignetteStrength, m_config.uiRadiusPct);
    m_settings->setMotionStyleState(m_config.motionStyle);
    m_settings->setCarouselFadeState(m_config.carouselFade);
    m_settings->setCarouselScrollSpeedState(m_config.carouselScrollSpeed);
    m_settings->setTitlePillNudgeState(m_config.titlePillNudgeX,
                                      m_config.titlePillNudgeY);
    m_settings->setGameTitleVisibilityState(!m_config.hideGameTitle);
    m_settings->onTitlePillNudgeYChange([this](int nudge) {
        nudge = std::clamp(nudge, -25, 25);
        if (m_config.titlePillNudgeY == nudge)
            return;
        m_config.titlePillNudgeY = nudge;
        m_config.save();
        applyTitlePillLayout();
    });
    m_settings->onGameTitleVisibilityChange([this](bool show) {
        if (m_config.hideGameTitle == !show)
            return;
        m_config.hideGameTitle = !show;
        m_config.save();
        applyTitlePillVisibility();
    });
    m_settings->setWallpaperState(m_config.wallpaperPath, m_config.wallpaperAnimated,
                                  m_config.wallpaperFps);
    m_settings->setBackgroundDriftState(m_config.backgroundDrift,
                                        m_config.backgroundDriftSpeed);
    m_settings->setBackgroundShapesState(m_config.backgroundShapes);
    m_settings->setHeroFadeInState(m_config.heroFadeInMs);
    m_settings->setSteamGridDbAspectState(m_config.steamGridDbAspect);
    m_settings->onIconCornersChange([this](int corners) {
        corners = std::clamp(corners, 0, 1);
        if (m_config.iconCorners == corners)
            return;
        m_config.iconCorners = corners;
        m_config.save();
        applyIconPresentation();
        applySidebarPresentation();
    });
    auto pushLayoutToSettings = [this]() {
        m_settings->setHomeLayoutState(m_config.homeLayout);
        m_settings->setIconPresentationState(
            m_config.iconCorners, m_config.gridNudgeX,
            m_config.lineNudgeY, m_config.iconScale, m_config.iconSpacing,
            m_config.carouselShrink, m_config.lineNudgeX,
            m_config.iconVignette, m_config.iconHighlight);
        m_settings->setIconCornerPctState(m_config.iconCornerPct);
    };
    m_settings->onSaveLayoutPreset([this](int slot) {
        if (slot < 0 || slot > 2)
            return;
        m_config.layoutPreset[slot] = {
            m_config.iconAspect, m_config.iconCorners, m_config.iconCornerPct,
            m_config.iconScale, m_config.iconSpacing, m_config.carouselShrink,
            m_config.gridNudgeX, m_config.lineNudgeX, m_config.lineNudgeY};
        m_config.save();
    });
    m_settings->onLoadLayoutPreset([this, pushLayoutToSettings](int slot) {
        if (slot < 0 || slot > 2)
            return;
        const auto& preset = m_config.layoutPreset[slot];
        if (preset.size() < 9)
            return;
        m_config.iconAspect    = std::clamp(preset[0], 0, 2);
        m_config.iconCorners   = std::clamp(preset[1], 0, 1);
        m_config.iconCornerPct = std::clamp(preset[2], 0, 100);
        m_config.iconScale     = std::clamp(preset[3], 70, 130);
        m_config.iconSpacing   = std::clamp(preset[4], -50, 100);
        m_config.carouselShrink = std::clamp(preset[5], 0, 100);
        m_config.gridNudgeX    = std::clamp(preset[6], -25, 25);
        m_config.lineNudgeX    = std::clamp(preset[7], -25, 25);
        m_config.lineNudgeY    = std::clamp(preset[8], -25, 25);
        m_config.save();
        applyIconPresentation();
        applySidebarPresentation();
        pushLayoutToSettings();
    });
    m_settings->onResetIconLayout([this, pushLayoutToSettings]() {
        m_config.iconScale = 100;
        m_config.iconSpacing = 0;
        m_config.carouselShrink = 50;
        m_config.gridNudgeX = 0;
        m_config.lineNudgeX = 0;
        m_config.lineNudgeY = 0;
        m_config.iconCornerPct = 100;
        m_config.save();
        applyIconPresentation();
        applySidebarPresentation();
        pushLayoutToSettings();
    });
    m_settings->onIconCornerPctChange([this](int pct) {
        pct = std::clamp(pct, 0, 100);
        if (m_config.iconCornerPct == pct)
            return;
        m_config.iconCornerPct = pct;
        m_config.save();
        applyIconPresentation();
        applySidebarPresentation();
    });
    m_settings->onWallpaperDimChange([this](int pct) {
        m_config.wallpaperDim = std::clamp(pct, 0, 100);
        m_config.save();
    });
    m_settings->onVignetteChange([this](int pct) {
        m_config.vignetteStrength = std::clamp(pct, 0, 100);
        m_config.save();
    });
    m_settings->onUiRadiusPctChange([this](int pct) {
        m_config.uiRadiusPct = std::clamp(pct, 50, 200);
        m_config.save();
        applyTheme();
        applyIconPresentation();
        applySidebarPresentation();
    });
    m_settings->onIconShadowChange([this]() {
        m_config.iconShadowOffset = std::clamp(m_settings->iconShadowOffset(), 0, 20);
        m_config.iconShadowAlpha = std::clamp(m_settings->iconShadowAlpha(), 0, 60);
        m_config.save();
        applyIconPresentation();
    });
    m_settings->onIconOverlayChange([this]() {
        m_config.iconVignette = std::clamp(m_settings->iconVignette(), 0, 100);
        m_config.iconHighlight = std::clamp(m_settings->iconHighlight(), 0, 100);
        m_config.save();
        applyIconPresentation();
    });
    m_settings->onMotionStyleChange([this](int style) {
        style = std::clamp(style, 0, 2);
        if (m_config.motionStyle == style)
            return;
        m_config.motionStyle = style;
        m_config.save();
        applyMotionStyle();
    });
    m_settings->onCarouselScrollSpeedChange([this](int speed) {
        speed = std::clamp(speed, 0, 100);
        if (m_config.carouselScrollSpeed == speed)
            return;
        m_config.carouselScrollSpeed = speed;
        // A Settings-close save may still be writing an older snapshot. Let it
        // finish before this save so the newest speed cannot be overwritten.
        if (m_configSaveFuture.valid()) {
            m_configSaveFuture.wait();
            try { m_configSaveFuture.get(); } catch (...) {}
        }
        m_config.save();
        if (m_grid)
            m_grid->setCarouselScrollSpeed(speed);
    });
    // Opacity layer: one palette shared with the Opacity color selector. Index
    // 0 means "leave the theme alone"; 1..9 are the fixed layer colours. Solid
    // mode reuses the same colour for the opaque layer.
    // Colours come from the shared palette in ui/AccentPalette.hpp, the same
    // one the Opacity tab lists, so labels and applied values cannot drift.
    m_settings->setOpacityState(m_config.opacityTint, m_config.opacitySolid);
    auto applyOpacityStyle = [this]() {
        const int idx = std::clamp(m_config.opacityTint, 0, 9);
        const bool tinted = switchu::ui::accent::valid(idx);
        const nxui::Color tint = switchu::ui::accent::colorFor(idx);
        app().renderer().setOpacityStyle(tinted, tint, m_config.opacitySolid);
        // The shortcut strip and the title capsule read the tint directly so a
        // chosen colour shows on every theme, not only ones that already have
        // the matching panel mix.
        m_sidebar.setOpacityTint(tinted, tint);
        if (m_titlePill)
            applyHomeChrome();
    };
    applyOpacityStyle();
    // Style lab. The tab edits its own copy while dragging, then one callback
    // commits every value and repaints the live chrome.
    m_settings->onStyleChanged([this]() {
        if (!m_settings)
            return;
        m_config.panelTintMix = m_settings->panelTintMix();
        m_config.panelBorderPct = m_settings->panelBorderPct();
        m_config.panelGrainPct = m_settings->panelGrainPct();
        m_config.panelShadowPct = m_settings->panelShadowPct();
        m_config.focusRingPx = m_settings->focusRingPx();
        m_config.focusGlowPct = m_settings->focusGlowPct();
        m_config.focusLiftPct = m_settings->focusLiftPct();
        m_config.focusOvershootPct = m_settings->focusOvershootPct();
        m_config.titleScalePct = m_settings->titleScalePct();
        m_config.titleCornerStyle = m_settings->titleCornerStyle();
        m_config.titleBackgroundPct = m_settings->titleBackgroundPct();
        m_config.hudGapPx = m_settings->hudGapPx();
        m_config.showHudClock = m_settings->showHudClock();
        m_config.showHudDate = m_settings->showHudDate();
        m_config.showHudBattery = m_settings->showHudBattery();
        m_config.showHudProfile = m_settings->showHudProfile();
        m_config.wallpaperBlurPx = m_settings->wallpaperBlurPx();
        m_config.wallpaperSaturationPct = m_settings->wallpaperSaturationPct();
        m_config.wallpaperSharpnessPct = m_settings->wallpaperSharpnessPct();
        m_config.wallpaperVibrancePct = m_settings->wallpaperVibrancePct();
        m_config.wallpaperNoisePct = m_settings->wallpaperNoisePct();
        m_config.backdropTintPct = m_settings->backdropTintPct();
        m_config.pageSlideMs = m_settings->pageSlideMs();
        m_config.save();
        applyStyleSettings();
    });
    m_settings->onSaveLookPreset([this](int slot) { saveLookPreset(slot); });
    m_settings->onLoadLookPreset([this](int slot) { loadLookPreset(slot); });
    m_settings->onRenameLookPreset([this](int slot, const std::string& name) {
        if (slot < 0 || slot > 2 || name.empty())
            return;
        m_config.lookPresets[slot].name = name;
        m_config.save();
    });
    syncStyleSettingsToScreen();
    applyStyleSettings();
    m_settings->onOpacityTintChange([this, applyOpacityStyle](int tint) {
        tint = std::clamp(tint, 0, 9);
        if (m_config.opacityTint == tint)
            return;
        m_config.opacityTint = tint;
        m_config.save();
        applyOpacityStyle();
    });
    m_settings->onOpacitySolidChange([this, applyOpacityStyle](bool solid) {
        if (m_config.opacitySolid == solid)
            return;
        m_config.opacitySolid = solid;
        m_config.save();
        applyOpacityStyle();
    });
    m_settings->setHighlightTint(m_config.highlightTint);
    m_settings->setHighlightGradientState(
        m_config.highlightGradientA, m_config.highlightGradientB,
        m_config.highlightThicknessPx);
    // The accent is applied through the theme, so a change recolours every
    // consumer in one pass rather than only the focus ring.
    m_settings->onHighlightTintChange([this](int tint) {
        tint = std::clamp(tint, 0, 10);
        if (m_config.highlightTint == tint)
            return;
        m_config.highlightTint = tint;
        m_config.save();
        applyTheme();
    });
    m_settings->onHighlightGradientColorAChange([this](int color) {
        color = std::clamp(color, 1, 9);
        if (m_config.highlightGradientA == color)
            return;
        m_config.highlightGradientA = color;
        m_config.save();
        if (m_config.highlightTint == 10)
            applyTheme();
    });
    m_settings->onHighlightGradientColorBChange([this](int color) {
        color = std::clamp(color, 1, 9);
        if (m_config.highlightGradientB == color)
            return;
        m_config.highlightGradientB = color;
        m_config.save();
        if (m_config.highlightTint == 10)
            applyTheme();
    });
    m_settings->onHighlightThicknessChange([this](int px) {
        px = std::clamp(px, 1, 8);
        if (m_config.highlightThicknessPx == px)
            return;
        m_config.highlightThicknessPx = px;
        m_config.save();
        applyCursorHighlight();
    });
    m_settings->onIconScaleChange([this](int scale) {
        scale = std::clamp(scale, 70, 130);
        // This control is the undocked size. While the dock overrides are on it
        // is stored for undocking but must not fight the docked value live.
        m_config.handheldIconScale = scale;
        if (dockedNow() && m_config.dockedSizeOverrides) {
            m_config.save();
            return;
        }
        if (m_config.iconScale == scale)
            return;
        m_config.iconScale = scale;
        m_config.save();
        applyIconPresentation();
        // Cell size moved, so page capacity and hit rects have to rebuild.
        reflowHomeGrid();
    });
    m_settings->onDockedSizeOverridesChange([this](bool enabled) {
        m_config.dockedSizeOverrides = enabled;
        // applyDockedMode writes the resolved handheld/docked pair once.
        applyDockedMode(dockedNow());
    });
    m_settings->onDockedIconScaleChange([this](int pct) {
        m_config.dockedIconScale = std::clamp(pct, 70, 130);
        applyDockedMode(dockedNow());
    });
    m_settings->onDockedShortcutScaleChange([this](int pct) {
        m_config.dockedShortcutScale = std::clamp(pct, 70, 150);
        applyDockedMode(dockedNow());
    });
    m_settings->onDockedCarouselXChange([this](int nudge) {
        nudge = std::clamp(nudge, -25, 25);
        if (m_config.dockedCarouselX == nudge)
            return;
        m_config.dockedCarouselX = nudge;
        m_config.save();
        if (dockedNow() && m_config.dockedSizeOverrides)
            applyIconPresentation();
    });
    m_settings->onDockedCarouselYChange([this](int nudge) {
        nudge = std::clamp(nudge, -25, 25);
        if (m_config.dockedCarouselY == nudge)
            return;
        m_config.dockedCarouselY = nudge;
        m_config.save();
        if (dockedNow() && m_config.dockedSizeOverrides)
            applyIconPresentation();
    });
    m_settings->onSortModeChange([this](int mode) {
        mode = std::clamp(mode, 0, 2);
        if (m_config.sortMode == mode)
            return;
        m_config.sortMode = mode;
        m_config.save();
        reflowHomeGrid();
    });
    m_settings->onAlphaOrderChange([this](bool on) {
        if (m_config.alphaOrder == on)
            return;
        m_config.alphaOrder = on;
        m_config.save();
        reflowHomeGrid();
    });
    m_settings->onRestoreHiddenGame([this](std::uint64_t id) {
        restoreGameToHome(id);
    });
    m_settings->onLockPadCodeChange([this](const std::string& uid, bool enabled,
                                           const std::string& code) {
        std::string clean;
        for (char c : code) {
            if ((c == 'U' || c == 'D' || c == 'L' || c == 'R') && clean.size() < 4)
                clean.push_back(c);
        }
        while (clean.size() < 4)
            clean.push_back('U');
        if (uid.empty()) {
            m_config.lockPadCodeEnabled = enabled;
            m_config.lockPadCode = clean;
        } else {
            // An empty value is an explicit per-user opt-out; a missing entry
            // falls back to the old global code.
            m_config.lockPadCodes[uid] = enabled ? clean : std::string();
        }
        m_config.save();
        // If Lock was enabled after this session started, remember the profile
        // being configured as the session's direct-lock user.
        if (enabled && !m_lockEntryUid && uid.size() == 32) {
            AccountUid account{};
            account.uid[0] = std::strtoull(uid.substr(0, 16).c_str(), nullptr, 16);
            account.uid[1] = std::strtoull(uid.substr(16, 16).c_str(), nullptr, 16);
            if (accountUidIsValid(&account))
                m_lockEntryUid = account;
        }
        DebugLog::log("[lock] user=%s code enabled=%d code=%s",
                      uid.empty() ? "<default>" : uid.c_str(),
                      enabled ? 1 : 0, clean.c_str());
    });
    m_settings->onSidebarScaleChange([this](int scale) {
        scale = std::clamp(scale, 70, 150);
        m_config.handheldShortcutScale = scale;
        if (dockedNow() && m_config.dockedSizeOverrides) {
            m_config.save();
            return;
        }
        if (m_config.sidebarScale == scale)
            return;
        m_config.sidebarScale = scale;
        // The shortcuts and the top bar are one row, so one control sizes the
        // whole thing: shortcuts, profile tile, clock and battery together.
        m_config.hudScale = std::clamp(scale, 70, 160);
        m_config.clockScale = std::clamp(scale, 70, 160);
        m_config.save();
        if (m_settings) {
            m_settings->setSidebarScaleState(m_config.sidebarScale);
            m_settings->setHudChromeState(m_config.hideActionHints, m_config.hudScale);
            m_settings->setClockLayoutState(m_config.clockNudgeX, m_config.clockScale);
        }
        applySidebarPresentation();
        applyHudScale();
        applyClockLayout();
    });
    m_settings->onShortcutEdgeChange([this](int edge) {
        edge = std::clamp(edge, 0, 1);
        if (m_config.shortcutEdge == edge)
            return;
        m_config.shortcutEdge = edge;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onShortcutNudgeChange([this](int nudge) {
        nudge = std::clamp(nudge, -25, 25);
        if (m_config.shortcutNudgeX == nudge)
            return;
        m_config.shortcutNudgeX = nudge;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onShortcutNudgeRightChange([this](int nudge) {
        nudge = std::clamp(nudge, -25, 25);
        if (m_config.shortcutNudgeXRight == nudge)
            return;
        m_config.shortcutNudgeXRight = nudge;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onShortcutStripChange([this](bool on) {
        if (m_config.shortcutStrip == on)
            return;
        m_config.shortcutStrip = on;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onShortcutGradientChange([this](bool on) {
        if (m_config.shortcutGradient == on)
            return;
        m_config.shortcutGradient = on;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onShortcutGradientColorAChange([this](int color) {
        color = std::clamp(color, 1, 9);
        if (m_config.shortcutGradientA == color)
            return;
        m_config.shortcutGradientA = color;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onShortcutGradientColorBChange([this](int color) {
        color = std::clamp(color, 1, 9);
        if (m_config.shortcutGradientB == color)
            return;
        m_config.shortcutGradientB = color;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onShortcutFlushTopChange([this](bool on) {
        if (m_config.shortcutFlushTop == on)
            return;
        m_config.shortcutFlushTop = on;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onShortcutNudgeYChange([this](int px) {
        px = (std::clamp(px, -24, 24) / 2) * 2;
        if (m_config.shortcutNudgeY == px)
            return;
        m_config.shortcutNudgeY = px;
        m_config.save();
        applySidebarPresentation();
    });
    m_settings->onHideActionHintsChange([this](bool hide) {
        if (m_config.hideActionHints == hide)
            return;
        m_config.hideActionHints = hide;
        m_config.save();
    });
    m_settings->onHudScaleChange([this](int scale) {
        scale = std::clamp(scale, 70, 160);
        if (m_config.hudScale == scale)
            return;
        m_config.hudScale = scale;
        m_config.save();
        applyHudScale();
    });
    m_settings->onClockScaleChange([this](int scale) {
        scale = std::clamp(scale, 70, 160);
        if (m_config.clockScale == scale)
            return;
        m_config.clockScale = scale;
        m_config.save();
        applyClockLayout();
    });
    m_settings->onClockNudgeXChange([this](int nudge) {
        nudge = std::clamp(nudge, -25, 25);
        if (m_config.clockNudgeX == nudge)
            return;
        m_config.clockNudgeX = nudge;
        m_config.save();
        applyClockLayout();
    });
    m_settings->onClockNudgeYChange([this](int px) {
        px = std::clamp(px, -14, 14);
        if (m_config.clockNudgeY == px)
            return;
        m_config.clockNudgeY = px;
        m_config.save();
        applyClockLayout();
    });
    m_settings->onIconSpacingChange([this](int spacing) {
        spacing = std::clamp(spacing, -50, 100);
        if (m_config.iconSpacing == spacing)
            return;
        m_config.iconSpacing = spacing;
        m_config.save();
        // Padding feeds the cell fit, so the page model has to rebuild.
        reflowHomeGrid();
    });
    m_settings->onCarouselShrinkChange([this](int shrink) {
        shrink = std::clamp(shrink, 0, 100);
        if (m_config.carouselShrink == shrink)
            return;
        m_config.carouselShrink = shrink;
        m_config.save();
        applyIconPresentation();
    });
    m_settings->onCarouselFadeChange([this](int fade) {
        fade = std::clamp(fade, 0, 100);
        if (m_config.carouselFade == fade)
            return;
        m_config.carouselFade = fade;
        m_config.save();
        applyIconPresentation();
    });
    m_settings->onTitlePillNudgeChange([this](int nudge) {
        nudge = std::clamp(nudge, -25, 25);
        if (m_config.titlePillNudgeX == nudge)
            return;
        m_config.titlePillNudgeX = nudge;
        m_config.save();
        // 12 px per step: the ends reach ~300 px either side of centre, enough
        // to park the capsule under the edge tiles without leaving the frame.
        if (m_titlePill)
            m_titlePill->setNudgeX(12.8f * static_cast<float>(nudge));
    });
    m_settings->onGridNudgeChange([this](int nudge) {
        m_config.gridNudgeX = std::clamp(nudge, -25, 25);
        m_config.save();
        applyIconPresentation();
    });
    m_settings->onLineNudgeChange([this](int nudge) {
        m_config.lineNudgeY = std::clamp(nudge, -25, 25);
        // Nudges are presentation settings like the others: without the save
        // the strip snapped back to stock on the next boot.
        m_config.save();
        applyIconPresentation();
    });
    m_settings->onLineNudgeXChange([this](int nudge) {
        m_config.lineNudgeX = std::clamp(nudge, -25, 25);
        m_config.save();
        applyIconPresentation();
    });
    m_settings->onWallpaperCycle([this]() { cycleWallpaper(); });
    m_settings->onWallpaperPicker([this]() { openWallpaperPicker(); });
    m_settings->onBackgroundDriftChange([this](bool on) {
        m_config.backgroundDrift = on;
        m_config.save();
        if (m_background)
            m_background->setDrift(m_config.backgroundDrift, m_config.backgroundDriftSpeed);
        if (m_steamGridDbBackdrop)
            m_steamGridDbBackdrop->setDrift(m_config.backgroundDrift,
                                            m_config.backgroundDriftSpeed);
    });
    m_settings->onBackgroundDriftSpeedChange([this](int pct) {
        m_config.backgroundDriftSpeed = std::clamp(pct, 1, 100);
        m_config.save();
        if (m_background)
            m_background->setDrift(m_config.backgroundDrift, m_config.backgroundDriftSpeed);
        if (m_steamGridDbBackdrop)
            m_steamGridDbBackdrop->setDrift(m_config.backgroundDrift,
                                            m_config.backgroundDriftSpeed);
    });
    m_settings->onBackgroundShapesChange([this](bool on) {
        m_config.backgroundShapes = on;
        m_config.save();
        if (m_background)
            m_background->setShapesEnabled(on);
    });
    m_settings->onHeroFadeInChange([this](int ms) {
        m_config.heroFadeInMs = std::clamp(ms, 0, 10000);
        m_config.save();
        if (m_steamGridDbBackdrop)
            m_steamGridDbBackdrop->setHeroFadeIn(m_config.heroFadeInMs / 1000.f);
    });
    m_settings->onWallpaperAnimatedChange([this](bool animated) {
        m_config.wallpaperAnimated = animated;
        m_config.save();
        reloadAnimatedWallpaper();
    });
    m_settings->onWallpaperFpsChange([this](int fps) {
        m_config.wallpaperFps = std::clamp(fps, 5, 30);
        m_config.save();
        reloadAnimatedWallpaper(true);
    });
    m_settings->onSteamGridDbAspectChange([this](int aspect) {
        m_config.steamGridDbAspect = std::clamp(aspect, 0, 3);
        m_config.save();
    });
    m_settings->onUiLanguageChange([this](const std::string& tag) {
        m_config.uiLanguageOverride = tag;
        if (m_settings) m_settings->setUiLanguageOverride(tag);
        applyUiLanguage();
        app().gpu().waitIdle();
        m_fontNormal.clearCache();
        m_fontSmall.clearCache();
#ifdef NXUI_BACKEND_DEKO3D
        app().renderer().reclaimReleasedTextureSlotsAfterIdle();
#endif
        m_settingsNeedRefresh = true;
    });
    m_settings->onClockUse12HourChange([this](bool enabled) {
        if (m_config.clockUse12Hour == enabled)
            return;
        m_config.clockUse12Hour = enabled;
        if (m_clock)
            m_clock->setUse12HourClock(enabled);
    });
    m_settings->onWebClockSyncChange([this](bool enabled) {
        if (m_config.webClockSyncEnabled == enabled)
            return;
        m_config.webClockSyncEnabled = enabled;
    });
    m_settings->onExternalToast([this](const std::string& msg, float) {
        if (!m_dialog || msg.empty())
            return;
        auto& i18n = nxui::I18n::instance();
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            i18n.tr("settings.system.web_time",
                    "Synchronize Clock via Public Servers"),
            msg,
            {
                { i18n.tr("button.ok", "OK"), []() {} }
            });
        focusManager().setFocus(m_dialog.get());
    });
    m_settings->onClockSyncBootRetry([this](const std::string& error) {
        constexpr int kMaxBootClockSyncFailures = 12;
        m_webClockBootSyncStarted = false;
        ++m_webClockBootSyncFailures;
        DebugLog::log("[boot] public web clock sync retry %d/%d: %s",
                      m_webClockBootSyncFailures,
                      kMaxBootClockSyncFailures,
                      error.c_str());
        if (m_webClockBootSyncFailures >= kMaxBootClockSyncFailures) {
            m_webClockBootSyncStarted = true;
            if (!m_settings)
                return;
            auto& i18n = nxui::I18n::instance();
            const std::string prefix = i18n.tr(
                "settings.system.web_time_failed",
                "Could not sync clock from the web.");
            const std::string msg = error.empty() ? prefix : prefix + " " + error;
            if (m_dialog) {
                m_dialogReturnFocus = focusManager().current();
                m_dialog->show(
                    i18n.tr("settings.system.web_time",
                            "Synchronize Clock via Public Servers"),
                    msg,
                    {{ i18n.tr("button.ok", "OK"), []() {} }});
                focusManager().setFocus(m_dialog.get());
            } else {
                m_settings->requestToast(msg, 3.5f);
            }
            return;
        }
        m_webClockBootSyncDelayFrames = 120;
    });
    m_settings->onTextEntryRequest(
        [this](const std::string& title, const std::string& guide,
               const std::string& initial, int maxLength,
               TabbedOverlayScreen::TextEntryAcceptCb onAccept) {
            requestTextEntry(title, guide, initial, maxLength, false,
                             std::move(onAccept));
        });
    m_settings->onAccessibilityEnabledChange([this](bool enabled) {
        if (m_config.accessibilityEnabled == enabled)
            return;
        m_config.accessibilityEnabled = enabled;
        if (m_settings)
            m_settings->setAccessibilityVoiceEnabled(enabled);
        if (m_themeShop)
            m_themeShop->setAccessibilityVoiceEnabled(enabled);
        if (m_gameOptions)
            m_gameOptions->setAccessibilityVoiceEnabled(enabled);
        if (m_folderOptions)
            m_folderOptions->setAccessibilityVoiceEnabled(enabled);
        if (enabled) {
            m_accessibility.setEnabled(true);
            // Startup skips espeak init when voice guidance is off, so bring it
            // up on first enable.
            if (!m_accessibilityReady)
                m_accessibilityReady = m_accessibility.initializeConfigured(
                    SD_ASSETS, nxui::I18n::instance().activeLanguageTag(),
                    m_config.accessibilitySpeechRate);
            m_accessibility.announce(nxui::I18n::instance().tr(
                "accessibility.speech.enabled",
                "Voice guidance enabled."));
        } else {
            m_accessibility.announceAndDisable(nxui::I18n::instance().tr(
                "accessibility.speech.disabled",
                "Voice guidance disabled."));
        }
    });
    m_settings->onAccessibilitySpeakHintsChange([this](bool enabled) {
        m_config.accessibilitySpeakHints = enabled;
        m_accessibility.setSpeakHints(enabled);
        if (m_settings)
            m_settings->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                          m_config.accessibilitySpeakPosition);
        if (m_themeShop)
            m_themeShop->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                           m_config.accessibilitySpeakPosition);
        if (m_gameOptions)
            m_gameOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                              m_config.accessibilitySpeakPosition);
        if (m_folderOptions)
            m_folderOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                                m_config.accessibilitySpeakPosition);
        if (m_dialog)
            m_dialog->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                        m_config.accessibilitySpeakPosition);
        if (m_userSelect)
            m_userSelect->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                            m_config.accessibilitySpeakPosition);
    });
    m_settings->onAccessibilitySpeakContextEveryFocusChange([this](bool enabled) {
        m_config.accessibilitySpeakContextEveryFocus = enabled;
        m_accessibility.setSpeakContextEveryFocus(enabled);
    });
    m_settings->onAccessibilitySpeakPositionChange([this](bool enabled) {
        m_config.accessibilitySpeakPosition = enabled;
        if (m_settings)
            m_settings->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                          m_config.accessibilitySpeakPosition);
        if (m_themeShop)
            m_themeShop->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                           m_config.accessibilitySpeakPosition);
        if (m_gameOptions)
            m_gameOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                              m_config.accessibilitySpeakPosition);
        if (m_folderOptions)
            m_folderOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                                m_config.accessibilitySpeakPosition);
        if (m_dialog)
            m_dialog->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                        m_config.accessibilitySpeakPosition);
        if (m_userSelect)
            m_userSelect->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                            m_config.accessibilitySpeakPosition);
    });
    m_settings->onAccessibilitySpeechRateChange([this](int rate) {
        m_config.accessibilitySpeechRate = std::clamp(rate, 120, 320);
        m_accessibility.setSpeechRate(m_config.accessibilitySpeechRate);
    });
    m_settings->onNetConnect([this]() {
        m_pendingNetConnect = true;
        m_settings->hide();
        m_navigator.resetToHome();
    });
    m_settings->onSteamGridDbEnabledChange([this](bool enabled) {
        m_config.steamGridDbEnabled = enabled;
        if (m_steamGridDbBackdrop) {
            m_steamGridDbBackdrop->invalidateArtworkCaches();
            m_steamGridDbBackdrop->setEnabled(enabled && steamGridDbArtworkAllowedHere());
            if (enabled) showFocusedSteamGridDbArtwork(true);
            else m_steamGridDbBackdrop->showTitle(0, true);
        }
        syncSteamGridDbLogoArea();
        syncSingleRowPosterPresentation(true);
    });
    m_settings->onSteamGridDbShowHeroesChange([this](bool enabled) {
        if (m_config.steamGridDbShowHeroes == enabled)
            return;
        m_config.steamGridDbShowHeroes = enabled;
        m_config.save();
        if (m_steamGridDbBackdrop) {
            m_steamGridDbBackdrop->setShowHero(enabled);
            showFocusedSteamGridDbArtwork(true);
        }
    });
    m_settings->onHeroBackdropResolutionChange([this](int level) {
        m_config.heroBackdropResolution = std::clamp(level, 0, 2);
        m_config.save();
        applyHeroBackdropResolution();
        showFocusedSteamGridDbArtwork(true);
    });
    m_settings->onHeroBackdropBlurChange([this](int pct) {
        m_config.heroBackdropBlur = std::clamp(pct, 0, 100);
        m_config.save();
        applyHeroBackdropBlur();
        showFocusedSteamGridDbArtwork(true);
    });
    m_settings->onSteamGridDbViewFlagsChange([this](bool grid, bool line, bool folders) {
        m_config.steamGridDbShowInGrid = grid;
        m_config.steamGridDbShowInDynamicLine = line;
        m_config.steamGridDbShowInFolders = folders;
        m_config.save();
        if (m_steamGridDbBackdrop) {
            // The focused title may already have been classified as "no
            // artwork" or kept a stale set from before the toggle; drop both so
            // the hero comes back instead of staying blank.
            m_steamGridDbBackdrop->invalidateArtworkCaches();
            m_steamGridDbBackdrop->setEnabled(m_config.steamGridDbEnabled
                                              && steamGridDbArtworkAllowedHere());
        }
        showFocusedSteamGridDbArtwork(true);
    });
    m_settings->onSteamGridDbPosterInLineChange([this](bool enabled) {
        m_config.steamGridDbShowPosterInDynamicLine = enabled;
        m_config.save();
        syncSingleRowPosterPresentation(true);
    });
    m_settings->onSteamGridDbLogoInLineChange([this](bool enabled) {
        m_config.steamGridDbShowLogoInDynamicLine = enabled;
        m_config.save();
        syncSteamGridDbLogoArea();
    });
    m_settings->onSteamGridDbApiKeyRequest([this]() {
        editSteamGridDbApiKey();
    });
    m_settings->onSteamGridDbScrapeRequest([this]() {
        startSteamGridDbScrape();
    });
    m_settings->onControllerPairing([this]() {
        if (m_settings) m_settings->hide();
        m_navigator.resetToHome();
        scheduleLeaveCapture([this]() { m_launcher.launchControllerPairing(); });
    });
    m_settings->onControllerRemapping([this]() {
        if (m_settings) m_settings->hide();
        m_navigator.resetToHome();
        scheduleLeaveCapture([this]() { m_launcher.launchControllerRemapping(); });
    });
    m_settings->onControllerTest([this]() {
        if (!m_controllerTest) return;
        m_navigator.navigate(switchu::navigation::Route::ControllerTest);
        m_controllerTest->show();
        focusManager().setFocus(m_controllerTest.get());
        m_audio.playSfx(Sfx::ModalShow);
    });
    m_settings->onSoftwareDelete([this](uint64_t titleId, const std::string& title) {
        startSoftwareDeletion(titleId, title, false);
    });
    m_settings->onSleepRequest([this]() {
        if (m_settings) m_settings->hide();
        m_launcher.enterSleep();
    });
    m_settings->onShutdownRequest([this]() {
        if (m_settings) m_settings->hide();
        m_launcher.shutdown();
    });
    m_settings->onRebootRequest([this]() {
        if (m_settings) m_settings->hide();
        m_launcher.reboot();
    });
    m_settings->onDialogRequest([this](const std::string& title,
                                       const std::string& msg,
                                       std::vector<SettingsScreen::DialogButtonDef> buttons) {
        if (!m_dialog) return;
        std::vector<OverlayDialog::ButtonDef> dlgButtons;
        bool preserveReturnFocus = (m_dialog && m_dialog->isActive() && focusManager().current() == m_dialog.get() && m_dialogReturnFocus != nullptr);
        for (size_t i = 0; i < buttons.size(); ++i) {
            auto cb = buttons[i].onPress;
            bool isLast = (i == buttons.size() - 1);
            if (buttons.size() == 1) {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            } else if (isLast) {
                dlgButtons.push_back({buttons[i].label, [cb]() { if (cb) cb(); }, true});
            } else {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            }
        }
        if (!preserveReturnFocus)
            m_dialogReturnFocus = focusManager().current();
        m_dialog->show(title, msg, std::move(dlgButtons));
        focusManager().setFocus(m_dialog.get());
    });
    m_settings->onDateTimeEditorRequest(
        [this](const TabbedOverlayScreen::DateTimeEditorValue& initial,
               TabbedOverlayScreen::DateTimeCommitCb onCommit) {
            if (!m_dialog) return;
            m_dialogReturnFocus = m_settings.get();
            OverlayDialog::DateTimeValue dialogValue;
            dialogValue.year = initial.year;
            dialogValue.month = initial.month;
            dialogValue.day = initial.day;
            dialogValue.hour = initial.hour;
            dialogValue.minute = initial.minute;
            m_dialog->showDateTimeEditor(
                dialogValue,
                [this, onCommit = std::move(onCommit)](
                    const OverlayDialog::DateTimeValue& value) mutable {
                    TabbedOverlayScreen::DateTimeEditorValue committed;
                    committed.year = value.year;
                    committed.month = value.month;
                    committed.day = value.day;
                    committed.hour = value.hour;
                    committed.minute = value.minute;
                    const bool saved = !onCommit || onCommit(committed);
                    if (saved)
                        m_clockService.invalidate();
                    return saved;
                });
            focusManager().setFocus(m_dialog.get());
        });
    m_settings->onTimezonePickerRequest(
        [this](const std::vector<std::string>& zones, int selectedIndex,
               TabbedOverlayScreen::TimezoneSelectCb onSelect,
               TabbedOverlayScreen::TimezoneSearchCb onSearch) {
            if (!m_dialog) return;
            m_dialogReturnFocus = m_settings.get();
            m_dialog->showTimezonePicker(
                zones,
                selectedIndex,
                [this, onSelect = std::move(onSelect)](const std::string& zone) mutable {
                    if (onSelect) onSelect(zone);
                    m_clockService.invalidate();
                },
                [this, onSearch = std::move(onSearch)]() mutable {
                    if (onSearch) onSearch();
                });
            focusManager().setFocus(m_dialog.get());
        });
    m_settings->onClosed([this]() {
        m_navigator.routeDidClose(switchu::navigation::Route::Settings);
        if (m_configSaveFuture.valid())
            m_configSaveFuture.wait();
        m_configSaveFuture = m_threadPool.submit([cfg = m_config]() {
            cfg.save();
        });
        DebugLog::log("[config] save queued");
        // Any reflow that ran behind the panel must not be what HOME keeps.
        endHomeLayoutHold();
        // SteamGridDB toggles / scans may have changed what artwork exists while
        // the overlay owned focus; re-evaluate the hero once the menu is back.
        showFocusedSteamGridDbArtwork(true);
        if (isCurrentFocusableWidget(m_sidebar.settingsButton())) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_sidebar.settingsButton());
        }
    });

}

void WiiUMenuApp::editSteamGridDbApiKey() {
    auto& i18n = nxui::I18n::instance();
    requestTextEntry(i18n.tr("settings.steamgriddb.api_key", "SteamGridDB API key"),
        i18n.tr("settings.steamgriddb.api_key_guide", "Enter your API key"),
        m_config.steamGridDbApiKey, 192, true,
        [this](const std::string& value) {
            m_config.steamGridDbApiKey = value;
            m_config.save();
            if (!m_settings) return;
            m_settings->setSteamGridDbState(m_config.steamGridDbEnabled,
                                             !value.empty(),
                                             m_config.steamGridDbShowInGrid,
                                             m_config.steamGridDbShowInDynamicLine,
                                             m_config.steamGridDbShowInFolders,
                                             m_config.steamGridDbShowPosterInDynamicLine,
                                             m_config.steamGridDbShowLogoInDynamicLine,
                                             m_config.steamGridDbShowHeroes);
            m_settings->refreshCurrentTabWidgets();
            m_settings->requestToast(value.empty()
                ? nxui::I18n::instance().tr("settings.steamgriddb.key_cleared", "API key cleared.")
                : nxui::I18n::instance().tr("settings.steamgriddb.key_saved", "API key saved."));
        });
}

void WiiUMenuApp::createQuickSettings() {
    if (m_quickSettings) return;

    m_quickSettings = std::make_shared<QuickSettingsOverlay>();
    m_quickSettings->setFont(&m_fontNormal);
    m_quickSettings->setSmallFont(&m_fontSmall);
    m_quickSettings->setIconFont(&m_fontIcons);
    m_quickSettings->setTheme(&m_theme);
    m_quickSettings->setInstantCursorMotion(m_config.cursorMotionMode == 1);
    m_quickSettings->setInput(&app().input());

    QuickSettingsOverlay::Callbacks callbacks;
    callbacks.onBgmVolumeChanged = [this](float value) {
        m_config.musicVolume = value;
        m_audio.setVolume(value);
    };
    callbacks.onSfxVolumeChanged = [this](float value) {
        m_config.sfxVolume = value;
        m_audio.setSfxVolume(value);
    };
    callbacks.onMusicPlayPause = [this]() {
        m_audio.togglePlayPause();
        m_config.musicEnabled = m_audio.isPlaying();
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicPrev = [this]() {
        m_audio.prevTrack();
        persistMusicPlaybackState(false);
        m_config.musicEnabled = true;
        m_config.save();
    };
    callbacks.onMusicNext = [this]() {
        m_audio.nextTrack();
        persistMusicPlaybackState(false);
        m_config.musicEnabled = true;
        m_config.save();
    };
    callbacks.onMusicToggleShuffle = [this]() {
        m_audio.setShuffle(!m_audio.shuffle());
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicCycleRepeat = [this]() {
        const int next = (static_cast<int>(m_audio.repeatMode()) + 1) % 3;
        m_audio.setRepeatMode(static_cast<MusicRepeatMode>(next));
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicSelectTrack = [this](int index) {
        m_audio.playTrackAt(index);
        m_config.musicEnabled = true;
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicMoveTrack = [this](int from, int to) {
        if (!m_audio.moveTrack(from, to))
            return;
        persistMusicPlaybackState(false);
        m_config.save();
    };
    callbacks.onMusicSeek = [this](float seconds) {
        m_audio.seekTo(seconds);
    };
    callbacks.onMusicQueryState = [this]() {
        QuickSettingsOverlay::MusicUiState state;
        state.nowPlaying = m_audio.currentDisplayTitle();
        if (state.nowPlaying.empty())
            state.nowPlaying = nxui::I18n::instance().tr(
                "quicksettings.music_empty", "No music loaded");
        state.playing = m_audio.isPlaying();
        state.shuffle = m_audio.shuffle();
        state.repeat = m_audio.repeatMode();
        state.volume = m_audio.volume();
        state.positionSeconds = m_audio.positionSeconds();
        state.durationSeconds = m_audio.durationSeconds();
        state.currentIndex = m_audio.currentIndex();
        state.hasAlbumFolders = m_audio.hasAlbumFolders();
        state.playlistFilenames = m_audio.playlistFilenames();
        state.playlistOrderKeys = m_audio.playlistOrderKeys();
        state.playlistTitles.reserve(m_audio.tracks().size());
        state.playlistAlbumFolders.reserve(m_audio.tracks().size());
        for (const auto& track : m_audio.tracks()) {
            state.playlistTitles.push_back(track.displayTitle());
            state.playlistAlbumFolders.push_back(track.albumFolder);
        }
        return state;
    };
    callbacks.onMusicQueryCoverArt = [this]() {
        return m_audio.currentCoverArt();
    };
    callbacks.onMusicQueryTrackCover = [this](int index) -> const std::vector<uint8_t>* {
        return m_audio.trackCoverArt(index);
    };
    callbacks.onMusicQueryFolderCover = [this](const std::string& folder) -> const std::vector<uint8_t>* {
        return m_audio.folderCoverArt(folder);
    };
    callbacks.onRenderMenuBackground = [this](nxui::Renderer& ren, float alpha) {
        if (m_background)
            m_background->renderLetterboxOverlay(ren, alpha);
    };
    callbacks.onSleepRequested = [this]() {
        if (!m_dialog) return;
        auto& i18n = nxui::I18n::instance();
        m_dialogReturnFocus = m_quickSettings.get();
        m_dialog->show(i18n.tr("power.title", "Power"),
            i18n.tr("settings.sleep.sleep_confirm", "Put the console into sleep mode?"),
            {{i18n.tr("button.cancel", "Cancel"), []() {}, true},
             {i18n.tr("power.sleep", "Sleep"), [this]() {
#ifdef SWITCHU_MENU
                 m_launcher.enterSleep();
#else
                 app().requestExit();
#endif
             }, true}}, 1, {});
        focusManager().setFocus(m_dialog.get());
    };
    callbacks.onLockRequested = [this]() {
        if (!m_config.lockScreenEnabled)
            return;
        if (m_quickSettings)
            m_quickSettings->hideCursorInstant();
        closeQuickSettings();
        m_audio.playSfx(Sfx::ConfirmPositive);
        requestLockEntry(false);
    };
    callbacks.onRebootRequested = [this]() {
        if (!m_dialog) return;
        auto& i18n = nxui::I18n::instance();
        m_dialogReturnFocus = m_quickSettings.get();
        m_dialog->show(i18n.tr("power.title", "Power"),
            i18n.tr("settings.sleep.reboot_confirm", "Restart the console?"),
            {{i18n.tr("button.cancel", "Cancel"), []() {}, true},
             {i18n.tr("power.reboot", "Reboot"), [this]() {
#ifdef SWITCHU_MENU
                 m_launcher.reboot();
#else
                 app().requestExit();
#endif
             }, true}}, 1, {});
        focusManager().setFocus(m_dialog.get());
    };
    callbacks.onShutdownRequested = [this]() {
        if (!m_dialog) return;
        auto& i18n = nxui::I18n::instance();
        m_dialogReturnFocus = m_quickSettings.get();
        m_dialog->show(i18n.tr("power.title", "Power"),
            i18n.tr("settings.sleep.shutdown_confirm", "Power off the console?"),
            {{i18n.tr("button.cancel", "Cancel"), []() {}, true},
             {i18n.tr("power.shutdown", "Shutdown"), [this]() {
#ifdef SWITCHU_MENU
                 m_launcher.shutdown();
#else
                 app().requestExit();
#endif
             }, true}}, 1, {});
        focusManager().setFocus(m_dialog.get());
    };
    callbacks.onClose = [this]() { closeQuickSettings(); };
    callbacks.onNavigateSfx = [this]() { m_audio.playSfx(Sfx::Navigate); };
    callbacks.onActivateSfx = [this]() { m_audio.playSfx(Sfx::Activate); };
    callbacks.onToggleOffSfx = [this]() { m_audio.playSfx(Sfx::ToggleOff); };
    m_quickSettings->setCallbacks(callbacks);
    m_quickSettings->setLockScreenEnabled(m_config.lockScreenEnabled);
    m_quickSettings->setIconGlyphResolver([this](const char* fileName) {
        return resolveActiveIconGlyphPath(fileName);
    });

    if (m_overlayLayer) {
        m_overlayLayer->addChild(m_quickSettings);
        // Confirmation dialogs and transition overlays must remain above the
        // drawer even though it is created lazily.
        for (const auto& overlay : {std::static_pointer_cast<nxui::Widget>(m_dialog),
                                   std::static_pointer_cast<nxui::Widget>(m_progressDialog),
                                   std::static_pointer_cast<nxui::Widget>(m_launchAnim)}) {
            if (!overlay) continue;
            m_overlayLayer->removeChild(overlay.get());
            m_overlayLayer->addChild(overlay);
        }
    }
}

void WiiUMenuApp::openQuickSettings() {
    createQuickSettings();
    if (!m_quickSettings || m_quickSettings->isActive() || m_editMode ||
        (m_dialog && m_dialog->isActive()) ||
        (m_userSelect && m_userSelect->isActive()))
        return;
    m_dialogReturnFocus = focusManager().current();
    m_quickSettings->setInitialValues(0.5f, m_config.musicVolume,
                                      m_config.sfxVolume, false, true);
    m_quickSettings->setBatteryStatus(m_consoleBatteryPercent,
                                      m_consoleBatteryCharging);
    m_quickSettings->show();
    focusManager().setFocus(m_quickSettings.get());
    if (m_cursor) m_cursor->setVisible(false);
    m_audio.playSfx(Sfx::ModalShow);
}

void WiiUMenuApp::closeQuickSettings() {
    if (!m_quickSettings || !m_quickSettings->isActive()) return;
    m_quickSettings->hide();
    persistMusicPlaybackState(false);
    m_config.save();
    nxui::Widget* target = m_dialogReturnFocus;
    if (!isCurrentFocusableWidget(target) && m_grid)
        target = m_grid->focusManager().current();
    if (isCurrentFocusableWidget(target)) {
        m_suppressNextNavigateSfx = true;
        focusManager().setFocus(target);
    }
    m_dialogReturnFocus = nullptr;
    m_audio.playSfx(Sfx::ModalHide);
}

void WiiUMenuApp::startSteamGridDbScrape() {
    if (m_config.steamGridDbApiKey.empty()) {
        if (m_settings)
            m_settings->requestToast(nxui::I18n::instance().tr(
                "settings.steamgriddb.need_key", "Configure an API key first."));
        return;
    }
    if (m_steamGridDbBrowseFuture.valid() || m_steamGridDbApplyFuture.valid()) {
        if (m_settings)
            m_settings->requestToast(nxui::I18n::instance().tr(
                "settings.steamgriddb.already_running", "A SteamGridDB operation is already running."));
        return;
    }
    // Stamp grid tile sizes so the scrape can pull landscape grids for 2x1/4x2
    // titles even when HOME is currently in single-row (where sizes force 1x1).
    std::vector<AppEntry> scrapeApps = m_allApps;
    for (auto& app : scrapeApps) {
        if (!app.isApplication() || app.titleId == 0)
            continue;
        const auto size = gameGridSize(app.titleId, AppLayoutMode::Grid);
        app.widgetColumns = size.columns;
        app.widgetRows = size.rows;
    }
    if (!m_steamGridDb.start(m_config.steamGridDbApiKey, scrapeApps)) {
        if (m_settings)
            m_settings->requestToast(nxui::I18n::instance().tr(
                "settings.steamgriddb.start_failed", "The artwork scan could not be started."));
        return;
    }
    m_steamGridDbWasRunning = true;
    if (m_progressDialog) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->setCancellable(true, [this]() {
            cancelSteamGridDbScrape();
        });
        raiseProgressDialogAboveOverlays();
        m_progressDialog->show(nxui::I18n::instance().tr(
            "settings.steamgriddb.download_title", "Downloading artwork"),
            nxui::I18n::instance().tr(
                "settings.steamgriddb.download_start", "Searching SteamGridDB..."), 0.f);
        focusManager().setFocus(m_progressDialog.get());
    }
    if (m_settings)
        m_settings->requestToast(nxui::I18n::instance().tr(
            "settings.steamgriddb.started", "SteamGridDB scan started."));
}

void WiiUMenuApp::cancelSteamGridDbScrape() {
    if (!m_steamGridDb.running())
        return;
    m_steamGridDb.requestCancel();
    if (m_progressDialog)
        m_progressDialog->updateState(nxui::I18n::instance().tr(
            "settings.steamgriddb.cancelling", "Cancelling..."), -1.f);
}

void WiiUMenuApp::raiseProgressDialogAboveOverlays() {
    if (!m_overlayLayer || !m_progressDialog)
        return;
    // Picker show() re-parents itself to the top of the overlay stack, which
    // buries the download dialog. Keep progress on top while it is active.
    m_overlayLayer->removeChild(m_progressDialog.get());
    m_overlayLayer->addChild(m_progressDialog);
}

void WiiUMenuApp::cancelSteamGridDbApply() {
    if (!m_steamGridDbApplyProgress)
        return;
    m_steamGridDbApplyProgress->cancelRequested.store(true);
    {
        std::lock_guard<std::mutex> lock(m_steamGridDbApplyProgress->mutex);
        m_steamGridDbApplyProgress->message = nxui::I18n::instance().tr(
            "settings.steamgriddb.cancelling", "Cancelling...");
        m_steamGridDbApplyProgress->progress01 = -1.f;
        ++m_steamGridDbApplyProgress->revision;
    }
    if (m_progressDialog)
        m_progressDialog->updateState(nxui::I18n::instance().tr(
            "settings.steamgriddb.cancelling", "Cancelling..."), -1.f);
}

void WiiUMenuApp::openSteamGridDbPicker(GameOptionsScreen::ArtworkKind kind,
                                        const std::string& requestedQuery) {
    if (!m_gameOptions || m_gameOptionsTitleId == 0) return;
    std::string title;
    std::string defaultQuery;
    for (const auto& appEntry : m_allApps) {
        if (appEntry.titleId == m_gameOptionsTitleId) {
            title = appEntry.title;
            defaultQuery = appEntry.steamGridDbTitle();
            break;
        }
    }
    // Prefer an explicit query, else the per-title saved search, else the
    // English/display title — so Hero/Logo/Icon/Poster share one search.
    std::string query = requestedQuery;
    if (query.empty())
        query = SteamGridDbManager::savedSearchQuery(m_gameOptionsTitleId);
    if (query.empty())
        query = defaultQuery;
    openSteamGridDbPickerForTitle(m_gameOptionsTitleId, title, kind, query);
}

void WiiUMenuApp::openSteamGridDbPickerForTitle(std::uint64_t titleId,
                                                const std::string& title,
                                                GameOptionsScreen::ArtworkKind kind,
                                                const std::string& requestedQuery) {
    if (titleId == 0) return;
    auto toast = [&](const std::string& msg) {
        if (m_gameOptions && m_gameOptions->isActive()) m_gameOptions->requestToast(msg);
        else if (m_folderOptions && m_folderOptions->isActive()) m_folderOptions->requestToast(msg);
        else if (m_settings) m_settings->requestToast(msg);
    };
    if (m_config.steamGridDbApiKey.empty()) {
        toast(nxui::I18n::instance().tr(
            "settings.steamgriddb.need_key", "Configure an API key first."));
        return;
    }
    SteamGridDbManager::ArtworkKind managerKind = SteamGridDbManager::ArtworkKind::Hero;
    if (kind == GameOptionsScreen::ArtworkKind::Logo)
        managerKind = SteamGridDbManager::ArtworkKind::Logo;
    else if (kind == GameOptionsScreen::ArtworkKind::Icon)
        managerKind = SteamGridDbManager::ArtworkKind::Icon;
    else if (kind == GameOptionsScreen::ArtworkKind::Poster)
        managerKind = SteamGridDbManager::ArtworkKind::Poster;
    else if (kind == GameOptionsScreen::ArtworkKind::PosterWide)
        managerKind = SteamGridDbManager::ArtworkKind::GridWide;

    // Apply / scrape still block; an in-flight browse is abandoned so X Search
    // from inside a match folder can always return to a fresh top-level list.
    if (m_steamGridDbApplyFuture.valid() || m_steamGridDb.running()) {
        toast(nxui::I18n::instance().tr(
            "settings.steamgriddb.already_running", "A SteamGridDB operation is already running."));
        return;
    }
    std::string query = requestedQuery;
    if (query.empty())
        query = SteamGridDbManager::savedSearchQuery(titleId);
    if (query.empty())
        query = title;
    if (query.empty()) {
        toast(nxui::I18n::instance().tr(
            "settings.steamgriddb.need_query", "Enter a search name first."));
        return;
    }
    SteamGridDbManager::saveSearchQuery(titleId, query);
    ++m_steamGridDbBrowseGeneration;
    m_steamGridDbBrowseFutureGeneration = m_steamGridDbBrowseGeneration;
    // Park any in-flight browse so X Search from a match folder can start a
    // fresh top-level list without blocking the UI thread on the old request.
    if (m_steamGridDbBrowseFuture.valid())
        m_steamGridDbRetiredBrowses.push_back(std::move(m_steamGridDbBrowseFuture));
    m_gameOptionsTitleId = titleId;
    if (m_overlayLayer && m_steamGridDbPicker) {
        m_overlayLayer->removeChild(m_steamGridDbPicker.get());
        m_overlayLayer->addChild(m_steamGridDbPicker);
    }
    // Top-level browse — never preserve a drilled-in game folder.
    m_steamGridDbPicker->showLoading(titleId, title, query, managerKind, false);
    focusManager().setFocus(m_steamGridDbPicker.get());
    m_steamGridDbBrowseFuture = std::async(std::launch::async,
        [apiKey = m_config.steamGridDbApiKey, titleId, title, query, managerKind,
         aspect = m_config.steamGridDbAspect]() {
            return SteamGridDbManager::browse(
                apiKey, titleId, title, query, managerKind,
                static_cast<SteamGridDbManager::ArtworkAspect>(aspect));
        });
}

void WiiUMenuApp::clearSteamGridDbArtwork(std::uint64_t titleId,
                                          GameOptionsScreen::ArtworkKind kind) {
    if (titleId == 0) return;
    SteamGridDbManager::ArtworkKind managerKind = SteamGridDbManager::ArtworkKind::Hero;
    if (kind == GameOptionsScreen::ArtworkKind::Logo)
        managerKind = SteamGridDbManager::ArtworkKind::Logo;
    else if (kind == GameOptionsScreen::ArtworkKind::Icon)
        managerKind = SteamGridDbManager::ArtworkKind::Icon;
    else if (kind == GameOptionsScreen::ArtworkKind::Poster)
        managerKind = SteamGridDbManager::ArtworkKind::Poster;
    else if (kind == GameOptionsScreen::ArtworkKind::PosterWide)
        managerKind = SteamGridDbManager::ArtworkKind::GridWide;

    const bool removed = SteamGridDbManager::clearArtwork(titleId, managerKind);
    auto& i18n = nxui::I18n::instance();
    const std::string msg = removed
        ? i18n.tr("game.steamgriddb.cleared", "SteamGridDB artwork cleared.")
        : i18n.tr("game.steamgriddb.nothing_to_clear", "No SteamGridDB artwork to clear.");

    if ((managerKind == SteamGridDbManager::ArtworkKind::Icon
         || managerKind == SteamGridDbManager::ArtworkKind::Poster) && m_grid) {
        m_folderCoverCache.erase(titleId);
        m_folderPeekCache.erase(titleId);
        m_folderPeekBorrowed.erase(titleId);
        if (titleId >= kFolderTitleIdPrefix)
            invalidateFolderPeekComposite(static_cast<std::uint32_t>(
                titleId & 0xFFFFFFFFu));
        else
            invalidateFolderPeekCompositesForTitle(titleId);
        const bool preferPoster = preferSingleRowPosters();
        const bool isFolderArt = titleId >= kFolderTitleIdPrefix;
        if (managerKind == SteamGridDbManager::ArtworkKind::Poster) {
            for (const auto& icon : m_grid->allIcons()) {
                if (!icon || icon->titleId() != titleId) continue;
                // Folders stay tall in single-row poster mode even without art.
                if (icon->entryKind() == GridEntryKind::Folder)
                    icon->setPosterFrame(preferPoster);
                else
                    icon->setPosterFrame(false);
            }
            m_grid->invalidateLineLayout();
        }
        if (!isFolderArt) {
            m_iconStreamer.reloadTitle(titleId, m_grid->currentPage(),
                                       m_grid->iconsPerPage(),
                                       app().gpu(), app().renderer(), m_grid->allIcons());
        }
        applyFolderCoversToIcons();
        // Sync-load the post-clear icon so Game Options header updates immediately
        // instead of waiting for the async grid streamer (same approach as folders).
        if (m_gameOptions && m_gameOptions->isActive()
            && titleId < kFolderTitleIdPrefix)
            m_gameOptions->setGameIcon(folderCoverTexture(titleId));
        else if (m_gameOptions)
            m_gameOptions->setGameIcon(nullptr);
    }
    if (managerKind == SteamGridDbManager::ArtworkKind::GridWide
        || managerKind == SteamGridDbManager::ArtworkKind::Hero
        || managerKind == SteamGridDbManager::ArtworkKind::Logo) {
        m_gameArtwork.erase(titleId);
        const auto size = gameGridSize(titleId, AppLayoutMode::Grid);
        if (switchu::widgets::isLandscapeWideGameTile(size))
            ensureGameArtwork(titleId);
    }
    showFocusedSteamGridDbArtwork(true);
    m_audio.playSfx(removed ? Sfx::ConfirmPositive : Sfx::ModalHide);
    if (m_gameOptions && m_gameOptions->isActive()) {
        std::error_code ec;
        const bool hasHero = std::filesystem::exists(
            SteamGridDbManager::heroPath(titleId), ec);
        ec.clear();
        const bool hasLogo = std::filesystem::exists(
            SteamGridDbManager::logoPath(titleId), ec);
        m_gameOptions->refreshArtworkPresence(
            hasHero, hasLogo, SteamGridDbManager::hasIcon(titleId),
            SteamGridDbManager::hasPoster(titleId),
            SteamGridDbManager::hasGridWide(titleId));
        m_gameOptions->requestToast(msg, 2.6f);
    } else if (m_folderOptions && m_folderOptions->isActive())
        m_folderOptions->requestToast(msg, 2.6f);
}

void WiiUMenuApp::editSteamGridDbPickerQuery() {
    if (!m_steamGridDbPicker || !m_steamGridDbPicker->isActive()) return;
    const auto kind = m_steamGridDbPicker->artworkKind();
    const auto mappedKind = kind == SteamGridDbManager::ArtworkKind::Logo
        ? GameOptionsScreen::ArtworkKind::Logo
        : kind == SteamGridDbManager::ArtworkKind::Icon
            ? GameOptionsScreen::ArtworkKind::Icon
            : kind == SteamGridDbManager::ArtworkKind::Poster
                ? GameOptionsScreen::ArtworkKind::Poster
                : kind == SteamGridDbManager::ArtworkKind::GridWide
                    ? GameOptionsScreen::ArtworkKind::PosterWide
                    : GameOptionsScreen::ArtworkKind::Hero;
    const std::uint64_t titleId = m_steamGridDbPicker->titleId();
    const std::string title = m_steamGridDbPicker->title();
    // Prefer the picker's live search string (survives match-folder drill-in).
    const std::string initial = !m_steamGridDbPicker->query().empty()
        ? m_steamGridDbPicker->query()
        : SteamGridDbManager::savedSearchQuery(titleId);
    requestTextEntry("SteamGridDB", "Search name", initial,
        128, false, [this, mappedKind, titleId, title](const std::string& value) {
            if (value.empty() || titleId == 0)
                return;
            SteamGridDbManager::saveSearchQuery(titleId, value);
            // Always top-level browse — exits any match folder and refreshes the list.
            openSteamGridDbPickerForTitle(titleId, title, mappedKind, value);
        });
}

void WiiUMenuApp::applySteamGridDbCandidate(
    const SteamGridDbManager::BrowseResult& browse,
    const SteamGridDbManager::Candidate& candidate) {
    if (m_steamGridDbApplyFuture.valid() || m_steamGridDb.running()) return;
    auto progress = std::make_shared<SteamGridDbApplyProgressShared>();
    progress->message = nxui::I18n::instance().tr(
        "settings.steamgriddb.download_start", "Starting download...");
    m_steamGridDbApplyProgress = progress;
    m_steamGridDbApplyProgressUiRevision = 0;
    if (m_progressDialog) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->setCancellable(true, [this]() {
            cancelSteamGridDbApply();
        });
        raiseProgressDialogAboveOverlays();
        m_progressDialog->show(nxui::I18n::instance().tr(
            "settings.steamgriddb.download_title", "Downloading artwork"),
            progress->message, 0.f);
        focusManager().setFocus(m_progressDialog.get());
    }
    m_steamGridDbApplyFuture = std::async(std::launch::async, [browse, candidate, progress]() {
        return SteamGridDbManager::applyCandidate(
            browse, candidate,
            [progress](const std::string& message, float value) {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->message = message;
                progress->progress01 = value;
                ++progress->revision;
            },
            [progress]() {
                return progress->cancelRequested.load();
            });
    });
}

void WiiUMenuApp::syncSteamGridDb() {
    for (auto it = m_steamGridDbRetiredBrowses.begin();
         it != m_steamGridDbRetiredBrowses.end();) {
        if (it->valid()
            && it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it->get(); // discard abandoned browse
            it = m_steamGridDbRetiredBrowses.erase(it);
        } else {
            ++it;
        }
    }
    if (m_steamGridDbBrowseFuture.valid()
        && m_steamGridDbBrowseFuture.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        const std::uint64_t futureGen = m_steamGridDbBrowseFutureGeneration;
        auto result = m_steamGridDbBrowseFuture.get();
        // Abandon results from a browse that X Search already replaced.
        if (futureGen != m_steamGridDbBrowseGeneration)
            return;
        if (m_steamGridDbPicker && m_steamGridDbPicker->isActive()
            && m_steamGridDbPicker->titleId() == result.titleId) {
            if (result.query.empty())
                result.query = m_steamGridDbPicker->query();
            m_steamGridDbPicker->setResult(std::move(result));
            focusManager().setFocus(m_steamGridDbPicker.get());
        }
    }
    if (m_steamGridDbApplyProgress) {
        std::string message;
        float progress01 = 0.f;
        std::uint64_t revision = 0;
        {
            std::lock_guard<std::mutex> lock(m_steamGridDbApplyProgress->mutex);
            message = m_steamGridDbApplyProgress->message;
            progress01 = m_steamGridDbApplyProgress->progress01;
            revision = m_steamGridDbApplyProgress->revision;
        }
        if (revision != m_steamGridDbApplyProgressUiRevision && m_progressDialog) {
            m_steamGridDbApplyProgressUiRevision = revision;
            m_progressDialog->updateState(message, progress01);
        }
        if (m_progressDialog && m_progressDialog->isActive()) {
            raiseProgressDialogAboveOverlays();
            if (focusManager().current() != m_progressDialog.get())
                focusManager().setFocus(m_progressDialog.get());
        }
    }
    if (m_steamGridDbApplyFuture.valid()
        && m_steamGridDbApplyFuture.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        const auto result = m_steamGridDbApplyFuture.get();
        if (result.success) {
            // Wide tiles and recent widgets can still be referenced by frames
            // already submitted to deko3d. Drain them before destroying their
            // old textures after a manual artwork replacement.
            DebugLog::log("[steamgriddb-ui] draining GPU before replacing artwork title=%016lX",
                          static_cast<unsigned long>(result.titleId));
            app().gpu().waitIdle();
#ifdef NXUI_BACKEND_DEKO3D
            app().renderer().reclaimReleasedTextureSlotsAfterIdle();
#endif
            m_gameArtwork.erase(result.titleId);
            if (m_recentWidgetAssetTitleId == result.titleId) {
                m_recentWidgetAssetTitleId = 0;
                m_recentWidgetHero.reset();
                m_recentWidgetLogo.reset();
                m_recentWidgetIcon.reset();
            }
            if ((result.kind == SteamGridDbManager::ArtworkKind::Icon
                 || result.kind == SteamGridDbManager::ArtworkKind::Poster) && m_grid) {
                m_folderCoverCache.erase(result.titleId);
                m_folderPeekCache.erase(result.titleId);
                m_folderPeekBorrowed.erase(result.titleId);
                const bool preferPoster = preferSingleRowPosters();
                const bool isFolderArt = result.titleId >= kFolderTitleIdPrefix;
                for (const auto& icon : m_grid->allIcons()) {
                    if (!icon || icon->titleId() != result.titleId) continue;
                    if (icon->entryKind() == GridEntryKind::Folder)
                        icon->setPosterFrame(preferPoster);
                    else if (icon->entryKind() == GridEntryKind::Application)
                        icon->setPosterFrame(
                            preferPoster
                            && SteamGridDbManager::hasPoster(result.titleId));
                    break;
                }
                m_grid->invalidateLineLayout();
                if (!isFolderArt) {
                    m_iconStreamer.reloadTitle(result.titleId, m_grid->currentPage(),
                                               m_grid->iconsPerPage(), app().gpu(),
                                               app().renderer(), m_grid->allIcons());
                }
                // Folder custom icon/poster: refresh cover tiles (+ enable Show
                // cover when applying an icon so grid/cover styles can use it).
                if (isFolderArt) {
                    if (result.kind == SteamGridDbManager::ArtworkKind::Icon
                        && !m_config.folderShowCover) {
                        m_config.folderShowCover = true;
                        if (m_configSaveFuture.valid())
                            m_configSaveFuture.wait();
                        m_configSaveFuture = m_threadPool.submit([config = m_config]() {
                            config.save();
                        });
                    }
                    if (m_openFolderId == 0)
                        applyDisplayModel(buildRootFolderModel(), result.titleId, false);
                    else
                        applyFolderCoversToIcons();
                    if (m_folderOptions && m_folderOptions->isActive()
                        && m_folderOptionsId != 0) {
                        if (const auto* folder = m_folderStore.find(m_folderOptionsId)) {
                            FolderOptionsScreen::FolderInfo info;
                            info.id = folder->id;
                            info.name = folder->name;
                            info.itemCount = static_cast<int>(folder->titleCount());
                            info.colorIndex = folder->colorIndex;
                            info.sizeIndex = folder->sizeIndex;
                            info.styleIndex = m_config.folderStyle;
                            info.defaultViewIndex =
                                std::clamp(m_config.folderDefaultView, 0, 2);
                            info.showCover = m_config.folderShowCover;
                            info.hasCustomIcon =
                                SteamGridDbManager::hasIcon(result.titleId);
                            info.hasCustomPoster =
                                SteamGridDbManager::hasPoster(result.titleId);
                            const auto coverId = resolveFolderCoverTitleId(
                                *folder, preferPoster);
                            info.cover = folderCoverTexture(coverId, preferPoster);
                            m_folderOptions->setFolder(info);
                        }
                    }
                } else {
                    invalidateFolderPeekCompositesForTitle(result.titleId);
                    applyFolderCoversToIcons();
                }
            } else {
                showFocusedSteamGridDbArtwork(true);
            }
            if (result.kind == SteamGridDbManager::ArtworkKind::GridWide
                || result.kind == SteamGridDbManager::ArtworkKind::Hero
                || result.kind == SteamGridDbManager::ArtworkKind::Logo) {
                m_gameArtwork.erase(result.titleId);
                const auto size = gameGridSize(result.titleId, AppLayoutMode::Grid);
                if (switchu::widgets::isLandscapeWideGameTile(size))
                    ensureGameArtwork(result.titleId);
            }
            if (m_grid && m_openFolderId == 0 &&
                gameGridSize(result.titleId, AppLayoutMode::Grid) !=
                    switchu::widgets::WidgetSize{1, 1})
                applyDisplayModel(buildRootFolderModel(), result.titleId, false);
            if (m_gameOptions && m_gameOptions->isActive()
                && m_gameOptionsTitleId == result.titleId) {
                std::error_code ec;
                const bool hasHero = std::filesystem::exists(
                    SteamGridDbManager::heroPath(result.titleId), ec);
                ec.clear();
                const bool hasLogo = std::filesystem::exists(
                    SteamGridDbManager::logoPath(result.titleId), ec);
                m_gameOptions->refreshArtworkPresence(
                    hasHero, hasLogo, SteamGridDbManager::hasIcon(result.titleId),
                    SteamGridDbManager::hasPoster(result.titleId),
                    SteamGridDbManager::hasGridWide(result.titleId));
                if (result.kind == SteamGridDbManager::ArtworkKind::Icon
                    && result.titleId < kFolderTitleIdPrefix) {
                    // Load from disk immediately - grid reloadTitle is async and
                    // still holds the previous texture when we get here.
                    m_gameOptions->setGameIcon(folderCoverTexture(result.titleId));
                }
            }
        }
        if (m_progressDialog) m_progressDialog->hide();
        m_steamGridDbApplyProgress.reset();
        m_steamGridDbApplyProgressUiRevision = 0;
        auto toast = [&](const std::string& msg) {
            if (m_gameOptions && m_gameOptions->isActive())
                m_gameOptions->requestToast(msg, 2.6f);
            else if (m_folderOptions && m_folderOptions->isActive())
                m_folderOptions->requestToast(msg, 2.6f);
            else if (m_settings) m_settings->requestToast(msg, 2.6f);
        };
        if (result.success) {
            // Close the picker after a successful apply so the user lands back
            // on game/folder options with the new artwork visible.
            if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
                m_steamGridDbPicker->hide();
            if (!result.message.empty()) toast(result.message);
        } else if (result.cancelled) {
            toast(nxui::I18n::instance().tr(
                "settings.steamgriddb.cancelled", "Artwork download cancelled."));
            if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
                focusManager().setFocus(m_steamGridDbPicker.get());
        } else if (m_steamGridDbPicker && m_steamGridDbPicker->isActive()) {
            m_steamGridDbPicker->setMessage(result.message, false);
            focusManager().setFocus(m_steamGridDbPicker.get());
        }
    }

    const auto state = m_steamGridDb.status();
    if (state.revision != m_steamGridDbUiRevision) {
        m_steamGridDbUiRevision = state.revision;
        if (m_settings) {
            m_settings->setSteamGridDbProgress(
                state.running, state.finished, state.completed, state.total,
                state.matched, state.failed, state.currentTitle, state.message);
        }
        if (state.running && m_steamGridDbWasRunning && m_progressDialog) {
            m_progressDialog->updateState(
                state.message.empty() ? state.currentTitle : state.message,
                state.progress01);
            if (focusManager().current() != m_progressDialog.get())
                focusManager().setFocus(m_progressDialog.get());
        }
        if (state.lastCompletedTitleId != 0
            && state.lastCompletedTitleId != m_steamGridDbLastCompletedTitleId) {
            m_steamGridDbLastCompletedTitleId = state.lastCompletedTitleId;
            if (m_grid && m_grid->focusManager().current()) {
                auto* focused = m_grid->focusManager().current();
                if (focused->tag() == "glossy_icon"
                    && static_cast<GlossyIcon*>(focused)->titleId() == state.lastCompletedTitleId) {
                    showFocusedSteamGridDbArtwork(true);
                }
            }
        }
    }

    if (m_steamGridDbWasRunning && !state.running && state.finished) {
        m_steamGridDbWasRunning = false;
        if (m_progressDialog) m_progressDialog->hide();
        // The scan wrote new files under steamgriddb/; drop the backdrop caches
        // so the focused hero and the prefetch ring pick them up immediately
        // instead of serving a set (or a "no artwork" verdict) from before.
        if (m_steamGridDbBackdrop)
            m_steamGridDbBackdrop->invalidateArtworkCaches();
        showFocusedSteamGridDbArtwork(true);
        const bool preferPosterTiles =
            m_appLayoutMode == AppLayoutMode::DynamicLine
            && m_config.steamGridDbEnabled
            && m_config.steamGridDbShowPosterInDynamicLine;
        if (state.matched > 0 && m_grid
            && (state.selectedKind == SteamGridDbManager::ArtworkKind::Icon
                || state.selectedKind == SteamGridDbManager::ArtworkKind::Poster
                || state.selectedKind == SteamGridDbManager::ArtworkKind::GridWide
                || preferPosterTiles)) {
            // forceReload releases the pool texture used by the panel header;
            // detach it first so the overlay never renders a stale pointer.
            if (m_gameOptions) m_gameOptions->setGameIcon(nullptr);
            // Also re-mark per-tile Poster frames so new poster.img files get
            // tall cells immediately (without a grid ↔ single-row toggle).
            syncSingleRowPosterPresentation(true);
        }
        if (state.matched > 0 && m_grid) {
            for (const auto& icon : m_grid->allIcons()) {
                if (!icon || icon->entryKind() != GridEntryKind::Application)
                    continue;
                const auto tid = icon->titleId();
                const int cols = icon->gridSpanColumns();
                const int rows = icon->gridSpanRows();
                if (switchu::widgets::isLandscapeWideGameTile(cols, rows)) {
                    m_gameArtwork.erase(tid);
                    ensureGameArtwork(tid);
                } else if (preferPosterIconData(tid) && !preferPosterTiles) {
                    m_iconStreamer.reloadTitle(tid, m_grid->currentPage(),
                                               m_grid->iconsPerPage(),
                                               app().gpu(), app().renderer(),
                                               m_grid->allIcons());
                }
            }
        }
        if (state.selectedKind != SteamGridDbManager::ArtworkKind::None
            && m_gameOptions && m_gameOptions->isActive()) {
            m_gameOptions->requestToast(state.message, 3.2f);
        }
        if (m_settings && m_settings->isActive()) {
            const bool cancelled = state.message.find("cancelled") != std::string::npos;
            m_settings->requestToast(
                cancelled
                    ? nxui::I18n::instance().tr("settings.steamgriddb.scan_cancelled",
                                                "Artwork scan cancelled.")
                    : (std::to_string(state.matched) + " artwork sets found, "
                       + std::to_string(state.failed) + " missing."),
                3.2f);
            focusManager().setFocus(m_settings.get());
        }
    }
}

AppLayoutMode WiiUMenuApp::presentationLayoutMode() const {
    // During prefetch the logical mode is already single-row, but tiles and
    // hero must keep the previous arrangement until the morph commits.
    if (m_layoutTransition.kind == LayoutTransitionKind::PrefetchThenMorph)
        return m_layoutTransition.visualMode;
    if (m_grid)
        return m_grid->layoutMode();
    return m_appLayoutMode;
}

bool WiiUMenuApp::steamGridDbArtworkAllowedHere() const {
    if (!m_config.steamGridDbEnabled)
        return false;
    if (m_openFolderId != 0)
        return m_config.steamGridDbShowInFolders;
    if (presentationLayoutMode() == AppLayoutMode::DynamicLine)
        return m_config.steamGridDbShowInDynamicLine;
    return m_config.steamGridDbShowInGrid;
}

void WiiUMenuApp::showFocusedSteamGridDbArtwork(bool forceReload) {
    if (!m_steamGridDbBackdrop) return;
    // Keep hero decode/upload off the scroll path entirely. This is set every
    // frame, so a selection that settles immediately re-enables artwork.
    m_steamGridDbBackdrop->setNavigationBusy(
        m_grid && m_grid->isDynamicLineScrolling());
    // The reclaim already released hero GPU for the donor map. Keep it down
    // only for that window; restoreFrameTapBrowse() reloads it once browse is
    // safe again, so this is not a persistent Frame Tap visual mode.
    if (m_ftHeadroomHoldActive && !m_ftRemountBrowseRestored) {
        m_steamGridDbBackdrop->setPreloadPaused(true);
        return;
    }
    if (!steamGridDbArtworkAllowedHere()) {
        m_steamGridDbBackdrop->setEnabled(false);
        m_steamGridDbBackdrop->showTitle(0, forceReload);
        syncHeroBackdropPresentation();
        return;
    }
    m_steamGridDbBackdrop->setEnabled(true);
    m_steamGridDbBackdrop->setLayoutMode(presentationLayoutMode());

    std::uint64_t titleId = 0;
    bool nearbyChanged = false;
    std::vector<std::uint64_t> nearbyTitleIds;
    // While carrying a title, keep its hero — hopping focus across slots for
    // placement would otherwise thrash hero loads and lag the folder.
    if (m_editMode && m_editHeldTitleId != 0
        && m_editHeldTitleId < kFolderTitleIdPrefix
        && !switchu::widgets::isWidgetTitleId(m_editHeldTitleId)) {
        titleId = m_editHeldTitleId;
    } else if (m_grid) {
        auto* current = m_grid->focusManager().current();
        if (current && current->tag() == "glossy_icon") {
            const int focused = m_grid->focusedGlobalIndex();
            if (focused >= 0 && focused < m_model.count()
                && m_model.at(focused).isApplication())
                titleId = static_cast<GlossyIcon*>(current)->titleId();
        }
        const int focused = m_grid->focusedGlobalIndex();
        // This runs from the per-frame HOME update, so the neighbour list must
        // only be rebuilt when it can actually differ: otherwise every frame
        // pays an allocation plus a scan of the model for nothing.
        nearbyChanged = forceReload
            || focused != m_lastBackdropFocusIndex
            || m_model.count() != m_lastBackdropModelCount
            || titleId != m_lastBackdropTitleId;
        m_lastBackdropFocusIndex = focused;
        m_lastBackdropModelCount = m_model.count();
        m_lastBackdropTitleId = titleId;
        for (int distance = 1; nearbyChanged && focused >= 0
                               && nearbyTitleIds.size() < 8
                               && distance < m_model.count(); ++distance) {
            for (int direction : {1, -1}) {
                const int index = focused + direction * distance;
                if (index < 0 || index >= m_model.count()) continue;
                const auto& entry = m_model.at(index);
                if (!entry.isApplication() || entry.titleId == 0) continue;
                nearbyTitleIds.push_back(entry.titleId);
                if (nearbyTitleIds.size() >= 8) break;
            }
        }
    }
    // Zero is meaningful: it clears artwork when focus moves to a folder,
    // an empty slot or any non-application widget.
    m_steamGridDbBackdrop->showTitle(titleId, forceReload);
    {
        const auto heroOffset = heroOffsetFor(titleId);
        m_steamGridDbBackdrop->setHeroOffset(heroOffset.first, heroOffset.second);
    }
    // After layout morph, skip neighbour hero/logo warm for a beat so HOME
    // stays responsive; focused title still updates above.
    if (nearbyChanged) {
        if (m_deferBackdropPreloadSec > 0.f)
            nearbyTitleIds.clear();
        m_steamGridDbBackdrop->setPreloadTitles(std::move(nearbyTitleIds));
    }
    syncSteamGridDbLogoArea();
    updateHeroAwareChrome();
}

void WiiUMenuApp::createGameOptions() {
    if (m_gameOptions) return;
    m_gameOptions = std::make_shared<GameOptionsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_gameOptions);
    m_gameOptions->setFont(&m_fontNormal);
    m_gameOptions->setSmallFont(&m_fontSmall);
    m_gameOptions->setTheme(&m_theme);
    m_gameOptions->setInstantCursorMotion(m_config.cursorMotionMode == 1);
    m_gameOptions->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_gameOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                     m_config.accessibilitySpeakPosition);
    m_gameOptions->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_gameOptions->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_gameOptions->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_gameOptions->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_gameOptions->onAccessibilityStructuredAnnouncement(
        [this](const std::string& context, const std::string& position,
               const std::string& summary, bool forceRepeat, bool forceContext) {
            m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
        });
    m_gameOptions->onClosed([this]() {
        m_navigator.routeDidClose(switchu::navigation::Route::GameOptions);
        if (m_allAppsScreen && m_allAppsScreen->isActive()) {
            m_navigator.navigate(switchu::navigation::Route::AllApps);
            focusManager().setFocus(m_allAppsScreen.get());
            return;
        }
        if (focusTitle(m_gameOptionsTitleId)) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
    });
}

void WiiUMenuApp::createAllAppsScreen() {
    if (m_allAppsScreen) return;
    m_allAppsScreen = std::make_shared<AllAppsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_allAppsScreen);
    m_allAppsScreen->setFont(&m_fontNormal);
    m_allAppsScreen->setSmallFont(&m_fontSmall);
    m_allAppsScreen->setTheme(&m_theme);
    m_allAppsScreen->configure(&app().gpu(), &app().renderer());
    m_allAppsScreen->setIconDataLoader(
        [this](std::uint64_t id) { return loadAllAppsIconData(id); });
    m_allAppsScreen->onSortModeChange([this](int mode) {
        mode = std::clamp(mode, 0, 2);
        if (m_config.allAppsSort == mode)
            return;
        m_config.allAppsSort = mode;
        m_config.save();
    });
    m_allAppsScreen->onFavouriteToggle([this](std::uint64_t id, bool favourite) {
        setAllAppsFavourite(id, favourite);
    });
    m_allAppsScreen->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_allAppsScreen->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_allAppsScreen->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_allAppsScreen->onLaunch([this](GlossyIcon* icon) {
        if (!icon) return;
        const std::uint64_t titleId = icon->titleId();
        AppEntry* entry = nullptr;
        for (auto& app : m_allApps)
            if (app.titleId == titleId) { entry = &app; break; }
        if (!entry) return;
        rememberAllAppsFocus();
        if (m_allAppsScreen) m_allAppsScreen->hide();
        m_navigator.resetToHome();
        activateApplication(icon, entry, titleId, icon->title());
    });
    m_allAppsScreen->onOptions([this](GlossyIcon* icon) {
        if (icon) showGameContextMenu(icon);
    });
    m_allAppsScreen->onClosed([this]() {
        rememberAllAppsFocus();
        m_navigator.routeDidClose(switchu::navigation::Route::AllApps);
        if (m_sidebar.allAppsButton())
            focusManager().setFocus(m_sidebar.allAppsButton());
        else if (m_grid) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
    });
}

std::vector<std::uint8_t> WiiUMenuApp::loadAllAppsIconData(std::uint64_t titleId) {
    auto data = AppListLoader::loadIconData(titleId, false);
    if (!data.empty() || titleId == 0)
        return data;

    // The daemon fills the SD control cache in the background; a title it has
    // not reached yet would otherwise stay blank forever. Ask NS directly and
    // park the bytes in the same cache so the next open is instant. The lookup
    // runs on an All Apps worker, so the HOS 20+ per-title latency never blocks
    // a frame; the mutex keeps the shared NS session single-threaded.
    static std::mutex nsMutex;
    std::lock_guard<std::mutex> lock(nsMutex);
    auto control = std::make_unique<NsApplicationControlData>();
    if (!control)
        return data;
    std::size_t controlSize = 0;
    const Result rc = nsGetApplicationControlData(
        NsApplicationControlSource_Storage, titleId, control.get(),
        sizeof(*control), &controlSize);
    if (R_FAILED(rc) || controlSize <= sizeof(NacpStruct)) {
        DebugLog::log("[allapps] control data failed tid=%016lX rc=0x%X size=%u",
                      static_cast<unsigned long>(titleId), rc,
                      static_cast<unsigned>(controlSize));
        return data;
    }
    const std::size_t iconSize = controlSize - sizeof(NacpStruct);
    if (iconSize == 0 || iconSize > sizeof(control->icon))
        return data;
    data.assign(control->icon, control->icon + iconSize);
    switchu::control_cache::writeIcon(titleId, data.data(), data.size());
    return data;
}

std::vector<std::uint64_t> WiiUMenuApp::allAppsFavouriteIds() const {
    std::vector<std::uint64_t> ids;
    const std::string& csv = m_config.allAppsFavourites;
    std::size_t start = 0;
    while (start <= csv.size()) {
        const std::size_t comma = csv.find(',', start);
        const std::string part = csv.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        std::uint64_t id = 0;
        if (!part.empty()) {
            try {
                id = std::stoull(part);
            } catch (...) {
                id = 0;
            }
        }
        if (id != 0)
            ids.push_back(id);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

void WiiUMenuApp::setAllAppsFavourite(std::uint64_t titleId, bool favourite) {
    if (titleId == 0)
        return;
    auto ids = allAppsFavouriteIds();
    if (favourite) {
        if (std::find(ids.begin(), ids.end(), titleId) == ids.end())
            ids.push_back(titleId);
    } else {
        ids.erase(std::remove(ids.begin(), ids.end(), titleId), ids.end());
    }
    std::sort(ids.begin(), ids.end());
    std::string csv;
    for (const auto id : ids) {
        if (!csv.empty())
            csv += ',';
        csv += std::to_string(id);
    }
    m_config.allAppsFavourites = std::move(csv);
    m_config.save();
}

void WiiUMenuApp::rememberAllAppsFocus() {
    if (!m_allAppsScreen)
        return;
    const std::uint64_t titleId = m_allAppsScreen->focusedTitleId();
    if (titleId == 0 || titleId == m_config.allAppsLastFocus)
        return;
    m_config.allAppsLastFocus = titleId;
    m_config.save();
}

std::vector<std::uint64_t> WiiUMenuApp::allAppsRecentIds() const {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> opened = m_config.lastOpened;
    std::stable_sort(opened.begin(), opened.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::uint64_t> ids;
    ids.reserve(opened.size());
    for (const auto& entry : opened) {
        if (entry.first != 0)
            ids.push_back(entry.first);
    }
    return ids;
}

void WiiUMenuApp::openAllApps() {
    if (!m_allAppsScreen) createAllAppsScreen();
    if (!m_allAppsScreen) return;
    m_audio.playSfx(Sfx::ModalShow);
    std::vector<AllAppsScreen::AppItem> items;
    items.reserve(m_allApps.size());
    for (const auto& app : m_allApps) {
        if (app.titleId == 0 || app.kind != GridEntryKind::Application) continue;
        items.push_back({app.titleId, app.title});
    }
    m_allAppsScreen->setTheme(&m_theme);
    m_allAppsScreen->setIconCornerRadius(effectiveIconCornerRadius());
    m_allAppsScreen->setRestoreFocus(m_config.allAppsLastFocus);
    m_allAppsScreen->setSortMode(m_config.allAppsSort);
    m_allAppsScreen->setFavourites(allAppsFavouriteIds());
    m_allAppsScreen->setRecentOrder(allAppsRecentIds());
    m_allAppsScreen->setApps(std::move(items));
    m_allAppsScreen->setGridShape(m_config.allAppsColumns, m_config.allAppsRows);
    refreshAllAppsMovement();
    if (m_settings && m_settings->isActive()) m_settings->hide();
    if (m_themeShop && m_themeShop->isActive()) m_themeShop->hide();
    m_navigator.navigate(switchu::navigation::Route::AllApps);
    m_allAppsScreen->show();
    focusManager().setFocus(m_allAppsScreen.get());
}

void WiiUMenuApp::refreshAllAppsMovement() {
    if (!m_allAppsScreen) return;
    m_allAppsScreen->setMovement(m_config.motionStyle, m_config.carouselScrollSpeed,
                                 m_config.carouselShrink, m_config.carouselFade);
}

void WiiUMenuApp::createFolderOptions() {
    if (m_folderOptions) return;
    m_folderOptions = std::make_shared<FolderOptionsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_folderOptions);
    m_folderOptions->setFont(&m_fontNormal);
    m_folderOptions->setSmallFont(&m_fontSmall);
    m_folderOptions->setTheme(&m_theme);
    m_folderOptions->setInstantCursorMotion(m_config.cursorMotionMode == 1);
    m_folderOptions->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_folderOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                       m_config.accessibilitySpeakPosition);
    m_folderOptions->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_folderOptions->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_folderOptions->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_folderOptions->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_folderOptions->onAccessibilityStructuredAnnouncement(
        [this](const std::string& context, const std::string& position,
               const std::string& summary, bool forceRepeat, bool forceContext) {
            m_accessibility.announceStructuredFocus(context, position, summary,
                                                    forceRepeat, forceContext);
        });
    m_folderOptions->onClosed([this]() {
        m_navigator.routeDidClose(switchu::navigation::Route::FolderOptions);
        if (focusTitle(folderTitleId(m_folderOptionsId))) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
    });
}

void WiiUMenuApp::createAutoThemeScreen() {
    if (m_autoThemeScreen) return;
    m_autoThemeScreen = std::make_shared<AutoThemeScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_autoThemeScreen);
    m_autoThemeScreen->setFont(&m_fontNormal);
    m_autoThemeScreen->setSmallFont(&m_fontSmall);
    m_autoThemeScreen->setTheme(&m_theme);
    m_autoThemeScreen->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_autoThemeScreen->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                         m_config.accessibilitySpeakPosition);
    m_autoThemeScreen->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_autoThemeScreen->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_autoThemeScreen->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_autoThemeScreen->onToggleSfx([this](bool on) {
        m_audio.playSfx(on ? Sfx::ThemeToggle : Sfx::ToggleOff);
    });
    m_autoThemeScreen->onSliderSfx([this](bool up) {
        m_audio.playSfx(up ? Sfx::SliderUp : Sfx::SliderDown);
    });
    m_autoThemeScreen->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_autoThemeScreen->onAccessibilityStructuredAnnouncement(
        [this](const std::string& context, const std::string& position,
               const std::string& summary, bool forceRepeat, bool forceContext) {
            m_accessibility.announceStructuredFocus(context, position, summary,
                                                    forceRepeat, forceContext);
        });
    m_autoThemeScreen->onChanged([this](const AutoThemeScreen::State& st) {
        m_config.autoThemeMode = st.mode == 1 ? "manual"
                               : (st.mode == 2 ? "geo" : "off");
        m_config.autoThemeDayPreset = st.dayPreset;
        m_config.autoThemeNightPreset = st.nightPreset;
        m_config.autoThemeDayStartHour = std::clamp(st.dayStartHour, 0, 23);
        m_config.autoThemeNightStartHour = std::clamp(st.nightStartHour, 0, 23);
        applyAutoThemeConfig();
        maybeFetchGeoLocation();
        evaluateAutoTheme(true);
        refreshAutoThemeSummary();
        pushGeoDisplayToScreen();
    });
    m_autoThemeScreen->onClosed([this]() {
        if (m_configSaveFuture.valid())
            m_configSaveFuture.wait();
        m_configSaveFuture = m_threadPool.submit([cfg = m_config]() { cfg.save(); });
        DebugLog::log("[config] save queued");
        // Return to the Theme Shop it was opened from.
        if (m_themeShop && m_themeShop->isActive()) {
            m_navigator.navigate(switchu::navigation::Route::ThemeShop);
            m_themeShop->rebuildCurrentTab();
            focusManager().setFocus(m_themeShop.get());
        } else {
            m_navigator.routeDidClose(switchu::navigation::Route::AutoTheme);
        }
    });
}

void WiiUMenuApp::openAutoThemeSettings() {
    createAutoThemeScreen();
    if (!m_autoThemeScreen) return;

    std::vector<AutoThemeScreen::PresetOption> presets;
    presets.reserve(m_allPresets.size());
    for (const auto& p : m_allPresets) {
        AutoThemeScreen::PresetOption opt;
        opt.id = p.id.empty() ? p.name : p.id;
        opt.name = p.name;
        presets.push_back(std::move(opt));
    }

    AutoThemeScreen::State st;
    st.mode = m_config.autoThemeMode == "manual" ? 1
            : (m_config.autoThemeMode == "geo" ? 2 : 0);
    st.dayPreset = m_config.autoThemeDayPreset;
    st.nightPreset = m_config.autoThemeNightPreset;
    st.dayStartHour = m_config.autoThemeDayStartHour;
    st.nightStartHour = m_config.autoThemeNightStartHour;
    st.geoResolved = m_config.autoThemeGeoResolved;
    st.geoCity = m_config.autoThemeGeoResolved
        ? (m_config.autoThemeGeoCity.empty()
               ? fmt::format("{:.2f}, {:.2f}", m_config.autoThemeGeoLat, m_config.autoThemeGeoLon)
               : m_config.autoThemeGeoCity)
        : std::string();

    m_autoThemeScreen->setTheme(&m_theme);
    m_autoThemeScreen->configure(st, std::move(presets));
    pushGeoDisplayToScreen(); // fill in computed sunrise/sunset

    m_navigator.navigate(switchu::navigation::Route::AutoTheme);
    m_autoThemeScreen->show();
    focusManager().setFocus(m_autoThemeScreen.get());
    m_audio.playSfx(Sfx::ModalShow);
}

void WiiUMenuApp::refreshAutoThemeSummary() {
    if (!m_themeShop) return;
    auto& i18n = nxui::I18n::instance();
    std::string summary;
    if (m_config.autoThemeMode == "manual") {
        summary = fmt::format("{} ({:02d}:00 / {:02d}:00)",
                              i18n.tr("autotheme.summary.manual", "Manual hours"),
                              std::clamp(m_config.autoThemeDayStartHour, 0, 23),
                              std::clamp(m_config.autoThemeNightStartHour, 0, 23));
    } else if (m_config.autoThemeMode == "geo") {
        summary = i18n.tr("autotheme.summary.geo", "Geolocation (IP)");
    } else {
        summary = i18n.tr("autotheme.summary.off", "Off");
    }
    m_themeShop->setAutoThemeSummary(summary);
}

void WiiUMenuApp::createControllerTest() {
    if (m_controllerTest) return;
    m_controllerTest = std::make_shared<ControllerTestScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_controllerTest);
    m_controllerTest->setFont(&m_fontNormal);
    m_controllerTest->setSmallFont(&m_fontSmall);
    m_controllerTest->setTheme(&m_theme);
    m_controllerTest->setInput(&app().input());
    m_controllerTest->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_controllerTest->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_controllerTest->onClosed([this]() {
        if (m_navigator.route() == switchu::navigation::Route::ControllerTest) {
            if (m_settings && m_settings->isActive())
                m_navigator.navigate(switchu::navigation::Route::Settings);
            else
                m_navigator.resetToHome();
        }
        if (m_settings && m_settings->isActive())
            focusManager().setFocus(m_settings.get());
    });
}

void WiiUMenuApp::createProfileCarousel() {
    if (m_profileCarousel) return;
    m_profileCarousel = std::make_shared<ProfileCarouselScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_profileCarousel);
    m_profileCarousel->setFont(&m_fontNormal);
    m_profileCarousel->setSmallFont(&m_fontSmall);
    m_profileCarousel->setTheme(&m_theme);
    m_profileCarousel->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_profileCarousel->onLockSfx([this]() { m_audio.playSfx(Sfx::ConfirmPositive); });
    m_profileCarousel->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_profileCarousel->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_profileCarousel->onLockProfile([this](std::optional<AccountUid> uid) {
        setLockedProfile(uid);
    });
    m_profileCarousel->onSelectProfile([this](AccountUid uid) {
        if (!m_lockProfileSelectActive)
            return;
        m_audio.playSfx(Sfx::ConfirmPositive);
        m_lockEntryUid = uid;
        m_lockProfileSelectActive = false;
        m_lockAfterProfilePick = true;
        // Lock follows immediately: closing with the normal fade would show
        // HOME under the picker before the Lock view appears.
        m_profileCarousel->hideInstantly();
    });
    m_profileCarousel->onOpenProfile([this](AccountUid uid) {
        m_audio.playSfx(Sfx::Activate);
        m_profileCarousel->hide();
#ifdef SWITCHU_MENU
        // The leave capture waits for the carousel to finish fading out.
        scheduleLeaveCapture([this, uid]() { m_launcher.launchUserPage(uid); });
#else
        (void)uid;
#endif
    });
    m_profileCarousel->onAddUser([this]() {
        m_audio.playSfx(Sfx::Activate);
        m_profileCarousel->hide();
#ifdef SWITCHU_MENU
        scheduleLeaveCapture([this]() { m_launcher.launchUserCreator(); });
#endif
    });
    m_profileCarousel->onClosed([this]() {
        if (m_lockAfterProfilePick) {
            m_lockAfterProfilePick = false;
            m_lockProfileSelectActive = false;
            if (m_navigator.route() == switchu::navigation::Route::ProfileSelect)
                m_navigator.resetToHome();
            m_profileCarousel->setMode(ProfileCarouselScreen::Mode::DefaultProfile);
            if (m_lockEntryUid) {
                const std::string uidHex = accountUidToHex(*m_lockEntryUid);
                if (m_config.activeProfileUid != uidHex) {
                    // Per-user settings are loaded before the menu UI is built,
                    // so switch accounts by replacing the activity. Wait for any
                    // queued save first: it still belongs to the outgoing user.
                    if (m_configSaveFuture.valid()) {
                        m_configSaveFuture.wait();
                        try { m_configSaveFuture.get(); } catch (...) {}
                    }
                    if (m_config.selectActiveUser(uidHex)) {
                        restartForBootProfile(*m_lockEntryUid);
                        return;
                    }
                    DebugLog::log("[profiles] per-user settings switch failed; staying in current session");
                }
                // The owner switch above already ran or failed; retrying it
                // here could skip the Lock gate, so only sync the profile.
                setLockedProfile(*m_lockEntryUid, false);
            }
            enterLockView(m_lockEntryInstant);
            return;
        }
        if (m_lockEntryPending) {
            const bool instant = m_lockEntryInstant;
            m_lockEntryPending = false;
            if (m_navigator.route() == switchu::navigation::Route::ProfileSelect)
                m_navigator.resetToHome();
            requestLockEntry(instant, true);
            return;
        }
        if (m_navigator.route() != switchu::navigation::Route::ProfileSelect)
            return;
        m_navigator.resetToHome();
        if (m_profileLock)
            focusManager().setFocus(m_profileLock.get());
    });
}

void WiiUMenuApp::openProfileCarousel() {
    createProfileCarousel();
    if (!m_profileCarousel || m_profileCarousel->isActive())
        return;
    m_profileCarousel->setMode(ProfileCarouselScreen::Mode::DefaultProfile);
    refreshProfileLockButton();
    m_navigator.navigate(switchu::navigation::Route::ProfileSelect);
    m_profileCarousel->show();
    focusManager().setFocus(m_profileCarousel.get());
}

void WiiUMenuApp::createThemeShop() {
    if (m_themeShop) return;

    auto showThemeShopInfo = [this](const std::string& title, const std::string& message) {
        if (!m_dialog) return;
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(title, message, {{"OK", {}, true}});
        focusManager().setFocus(m_dialog.get());
    };
    auto queueThemeTransfer = [this, showThemeShopInfo](const std::string& themeId, bool applyAfterInstall) {
        if (!m_themeShop)
            return;

        const auto* entry = m_themeShop->findCommunityThemeEntry(themeId);
        if (!entry) {
            auto& i18n = nxui::I18n::instance();
            showThemeShopInfo(i18n.tr("sidebar.theme_shop", "Theme Shop"),
                              i18n.tr("themeshop.community.selected_missing",
                                      "The selected community theme is no longer available in the catalog."));
            return;
        }

        ThemeCatalogClient::Entry entryCopy = *entry;
        ThemePackageInstaller::Mode mode = applyAfterInstall
            ? ThemePackageInstaller::Mode::InstallAndApply
            : ThemePackageInstaller::Mode::InstallOnly;
        std::string destination = ThemePackageInstaller::destinationRootFor(entryCopy.id, mode);

        auto startTransfer = [this, entryCopy, applyAfterInstall]() {
            startThemePackageTransfer(entryCopy, applyAfterInstall);
        };

        // Finish any in-flight uninstall before we decide "already installed".
        if (m_themeDeleteFuture.valid()) {
            m_themeDeleteFuture.wait();
            m_themeDeleteFuture = {};
        }

        // Only a real package (theme.json) counts. Empty/orphan folders left by a
        // failed async delete used to trigger a bogus "Replace Installed Theme".
        const bool packagePresent = pathExists(joinPath(destination, "theme.json"));
        if (!packagePresent) {
            if (pathExists(destination)) {
                const bool cleaned = removeDirectoryRecursive(destination);
                DebugLog::log("[themeshop] cleared orphan theme folder before install: %s (%s)",
                              safeLogPath(destination), cleaned ? "ok" : "failed");
            }
            startTransfer();
            return;
        }

        auto& i18n = nxui::I18n::instance();
        if (!m_dialog) {
            startTransfer();
            return;
        }

        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            applyAfterInstall
                ? i18n.tr("themeshop.community.overwrite_apply_title", "Replace And Apply Theme")
                : i18n.tr("themeshop.community.overwrite_install_title", "Replace Installed Theme"),
            applyAfterInstall
                ? i18n.tr("themeshop.community.overwrite_apply_message",
                          "A package with this theme ID is already installed. Replace it with the GitHub version, then apply it?")
                : i18n.tr("themeshop.community.overwrite_install_message",
                          "A package with this theme ID is already installed. Replace it with the version from GitHub?"),
            {
                {i18n.tr("button.cancel", "Cancel"), {}, true},
                {i18n.tr("button.replace", "Replace"), [startTransfer]() { startTransfer(); }, true}
            },
            1,
            {});
        focusManager().setFocus(m_dialog.get());
    };
    auto clearCompletedThemeTransferState = [this]() {
        if (!m_themePackageTransfer)
            return;

        bool isRunning = false;
        {
            std::lock_guard<std::mutex> lk(m_themePackageTransfer->mutex);
            isRunning = m_themePackageTransfer->state.isRunning();
        }

        if (isRunning)
            return;

        m_themePackageTransfer.reset();
        m_themePackageTransferUiRevision = 0;
        m_themePackageTransferHandledRevision = 0;
        if (m_themeShop) {
            ThemeTransferState cleared;
            m_themeShop->setPackageTransferState(cleared, {}, false);
        }
    };

    m_themeShop = std::make_shared<ThemeShopScreen>();
    if (m_overlayLayer) {
        m_overlayLayer->addChild(m_themeShop);
    }
    m_themeShop->setFont(&m_fontNormal);
    m_themeShop->setSmallFont(&m_fontSmall);
    m_themeShop->setTheme(&m_theme);
    m_themeShop->setInstantCursorMotion(m_config.cursorMotionMode == 1);
    m_themeShop->setThreadPool(&m_threadPool);
    m_themeShop->setRenderContext(&app().gpu(), &app().renderer());
    m_themeShop->setMusicState(m_audio.isPlaying(), m_audio.volume(), m_audio.sfxVolume());
    m_themeShop->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_themeShop->setActionHintStyleState(m_config.actionHintStyle);
    m_themeShop->setSortLargeTilesState(m_config.sortLargeTiles);
    m_themeShop->setSortLargeTilesAvailable(homeLayoutSupportsLargeTiles());
    m_themeShop->setLockScreenState(m_config.lockScreenEnabled);
    m_themeShop->setPowerButtonAction(m_config.powerButtonAction);
    m_themeShop->setLockScreenClockFontState(m_config.lockScreenClockFont);
    m_themeShop->setLockClockFontContext(
        SD_ASSETS,
        m_loadedRegularFontPath.empty()
            ? std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf"
            : m_loadedRegularFontPath);
    refreshLockClockFontEntries();
    m_themeShop->setIconContrastAdaptState(m_config.iconContrastAdapt);
    m_themeShop->setLockScreenTransitionState(m_config.lockScreenTransition);
    m_themeShop->setLockScreenZoomDirState(m_config.lockScreenZoomDir);
    m_themeShop->setLockScreenWipeDirState(m_config.lockScreenWipeDir);
    m_themeShop->setThemeLockClockFontOverrideState(m_config.themeLockClockFontOverride);
    m_themeShop->setThemeIconPackOverrideState(m_config.themeIconPackOverride);
    m_themeShop->setThemeMusicOverrideState(m_config.themeMusicOverride);
    m_themeShop->setThemeMusicAddToPlaylistState(m_config.themeMusicAddToPlaylist);
    m_themeShop->setThemeMusicPlayOnApplyState(m_config.themeMusicPlayOnApply);
    m_themeShop->setThemeMatchAccent(m_activeColors.accentH,
                                     m_activeColors.accentS,
                                     m_activeColors.accentL);
    {
        std::unordered_map<std::string, ThemeShopScreen::IconPackTintState> tints;
        for (const auto& [packId, tint] : m_config.iconPackTints) {
            ThemeShopScreen::IconPackTintState state;
            state.enabled = tint.enabled;
            state.flatten = tint.flatten;
            state.grayscale = tint.grayscale;
            state.matchTheme = tint.matchTheme;
            state.hue = tint.hue;
            state.sat = tint.sat;
            state.light = tint.light;
            tints[packId] = state;
        }
        m_themeShop->setIconPackTintState(tints);
    }
    m_themeShop->setInputSource(&app().input());
    refreshAutoThemeSummary();
    m_themeShop->setCursorMotionModeState(m_config.cursorMotionMode);
    m_themeShop->setPageTransitionModeState(m_config.pageTransitionMode);
    m_themeShop->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_themeShop->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                   m_config.accessibilitySpeakPosition);
    m_themeShop->onSearchRequest([this](const std::string& current) {
        requestTextEntry(nxui::I18n::instance().tr("hint.search", "Search"),
            nxui::I18n::instance().tr("themeshop.search.guide", "Search themes"),
            current, 64, false, [this](const std::string& query) {
                if (m_themeShop) m_themeShop->setSearchQuery(query);
            });
    });

    m_themeShop->onMusicEnabledChange([this](bool enabled) {
        m_config.musicEnabled = enabled;
        if (enabled)
            m_audio.play();
        else
            m_audio.pause();
        persistMusicPlaybackState(false);
        m_config.save();
    });
    m_themeShop->onMusicVolumeChange([this](float v) {
        m_audio.setVolume(v);
        m_config.musicVolume = v;
    });
    m_themeShop->onSfxVolumeChange([this](float v) {
        m_audio.setSfxVolume(v);
        m_config.sfxVolume = v;
    });
    m_themeShop->onGridColumnsChange([this](int cols) {
        cols = std::clamp(cols, 1, 8);
        if (m_config.gridColumns == cols)
            return;
        m_config.gridColumns = cols;
        reflowHomeGrid();
    });
    m_themeShop->onGridRowsChange([this](int rows) {
        rows = std::clamp(rows, 1, 5);
        if (m_config.gridRows == rows)
            return;
        m_config.gridRows = rows;
        reflowHomeGrid();
    });
    m_themeShop->onActionHintStyleChange([this](int style) {
        m_config.actionHintStyle = style == 0 ? "panel" : "capsules";
    });
    m_themeShop->onSortLargeTilesChange([this](bool enabled) {
        if (m_config.sortLargeTiles == enabled)
            return;
        m_config.sortLargeTiles = enabled;
        m_config.save();
        reflowHomeGrid();
    });
    m_themeShop->onLockScreenChange([this](bool enabled) {
        if (m_config.lockScreenEnabled == enabled)
            return;
        m_config.lockScreenEnabled = enabled;
        m_config.save();
        if (m_quickSettings)
            m_quickSettings->setLockScreenEnabled(enabled);
#ifdef SWITCHU_MENU
        switchu::menu::smi_cmd::setLockScreen(enabled, m_config.powerButtonAction);
#endif
    });
    m_themeShop->onPowerButtonActionChange([this](int action) {
        action = std::clamp(action, 0, 2);
        if (m_config.powerButtonAction == action)
            return;
        m_config.powerButtonAction = action;
        m_config.save();
#ifdef SWITCHU_MENU
        switchu::menu::smi_cmd::setLockScreen(m_config.lockScreenEnabled, action);
#endif
    });
    m_themeShop->onLockScreenClockFontChange([this](const std::string& id) {
        if (m_config.lockScreenClockFont == id)
            return;
        m_config.lockScreenClockFont = id;
        m_config.save();
        applyLockScreenClockFont();
    });
    m_themeShop->onAutoThemeOpen([this]() { openAutoThemeSettings(); });
    m_themeShop->onCursorMotionModeChange([this](int mode) {
        m_config.cursorMotionMode = std::clamp(mode, 0, 1);
        const bool instant = m_config.cursorMotionMode == 1;
        if (m_cursor) m_cursor->setInstantMotion(instant);
        if (m_settings) m_settings->setInstantCursorMotion(instant);
        if (m_themeShop) m_themeShop->setInstantCursorMotion(instant);
        if (m_gameOptions) m_gameOptions->setInstantCursorMotion(instant);
        if (m_folderOptions) m_folderOptions->setInstantCursorMotion(instant);
        if (m_quickSettings) m_quickSettings->setInstantCursorMotion(instant);
        if (m_dialog) m_dialog->cursor().setInstantMotion(instant);
        if (m_userSelect) m_userSelect->cursor().setInstantMotion(instant);
        updateCursor();
    });
    m_themeShop->onPageTransitionModeChange([this](int mode) {
        m_config.pageTransitionMode = std::clamp(mode, 0, 1);
        if (m_grid)
            m_grid->setSlideTransition(m_config.pageTransitionMode == 0);
        m_config.save();
    });
    m_themeShop->onNextTrack([this]() {
        m_audio.nextTrack();
        m_audio.playSfx(Sfx::ConfirmPositive);
    });
    m_themeShop->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_themeShop->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_themeShop->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_themeShop->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_themeShop->onAccessibilityStructuredAnnouncement([this](const std::string& context,
                                                              const std::string& position,
                                                              const std::string& summary,
                                                              bool forceRepeat,
                                                              bool forceContext) {
        m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
    });
    m_themeShop->onToggleSfx([this](bool on) {
        m_audio.playSfx(on ? Sfx::ThemeToggle : Sfx::ToggleOff);
    });
    m_themeShop->onSliderSfx([this](bool up) {
        m_audio.playSfx(up ? Sfx::SliderUp : Sfx::SliderDown);
    });
    m_themeShop->onNetConnectRequest([this]() {
        m_pendingNetConnect = true;
        if (m_themeShop) m_themeShop->hide();
        m_navigator.resetToHome();
    });
        m_themeShop->onIconContrastAdaptChange([this](bool enabled) {
        if (m_config.iconContrastAdapt == enabled)
            return;
        m_config.iconContrastAdapt = enabled;
        m_config.save();
        syncColourIconContrast();
    });
    m_themeShop->onLockScreenTransitionChange([this](int mode) {
        mode = std::clamp(mode, 0, 3);
        if (m_config.lockScreenTransition == mode)
            return;
        m_config.lockScreenTransition = mode;
        m_config.save();
        if (m_lockScreen)
            m_lockScreen->setTransition(static_cast<LockScreen::Transition>(mode));
    });
    m_themeShop->onLockScreenZoomDirChange([this](int dir) {
        dir = std::clamp(dir, 0, 1);
        if (m_config.lockScreenZoomDir == dir)
            return;
        m_config.lockScreenZoomDir = dir;
        m_config.save();
        if (m_lockScreen)
            m_lockScreen->setZoomDir(dir);
    });
    m_themeShop->onLockScreenWipeDirChange([this](int dir) {
        dir = std::clamp(dir, 0, 3);
        if (m_config.lockScreenWipeDir == dir)
            return;
        m_config.lockScreenWipeDir = dir;
        m_config.save();
        if (m_lockScreen)
            m_lockScreen->setWipeDir(dir);
    });
    m_themeShop->onThemeLockClockFontOverrideChange([this](bool enabled) {
        if (m_config.themeLockClockFontOverride == enabled)
            return;
        m_config.themeLockClockFontOverride = enabled;
        m_config.save();
    });
    m_themeShop->onThemeIconPackOverrideChange([this](bool enabled) {
        if (m_config.themeIconPackOverride == enabled)
            return;
        m_config.themeIconPackOverride = enabled;
        m_config.save();
    });
    m_themeShop->onThemeMusicOverrideChange([this](bool enabled) {
        if (m_config.themeMusicOverride == enabled)
            return;
        m_config.themeMusicOverride = enabled;
        m_config.save();
    });
    m_themeShop->onThemeMusicAddToPlaylistChange([this](bool enabled) {
        if (m_config.themeMusicAddToPlaylist == enabled)
            return;
        m_config.themeMusicAddToPlaylist = enabled;
        m_config.save();
    });
    m_themeShop->onThemeMusicPlayOnApplyChange([this](bool enabled) {
        if (m_config.themeMusicPlayOnApply == enabled)
            return;
        m_config.themeMusicPlayOnApply = enabled;
        m_config.save();
    });
    m_themeShop->onIconPackTintChange([this](const std::string& packId,
                                             const ThemeShopScreen::IconPackTintState& tint) {
        AppConfig::IconPackTint cfg;
        cfg.enabled = tint.enabled;
        cfg.flatten = tint.flatten;
        cfg.grayscale = tint.grayscale;
        cfg.matchTheme = tint.matchTheme;
        cfg.hue = std::clamp(tint.hue, 0.f, 1.f);
        cfg.sat = std::clamp(tint.sat, 0.f, 1.f);
        cfg.light = std::clamp(tint.light, 0.f, 1.f);
        const AppConfig::IconPackTint previous = m_config.iconPackTintFor(packId);
        if (previous.enabled == cfg.enabled
            && previous.flatten == cfg.flatten
            && previous.grayscale == cfg.grayscale
            && previous.matchTheme == cfg.matchTheme
            && std::fabs(previous.hue - cfg.hue) < 0.0005f
            && std::fabs(previous.sat - cfg.sat) < 0.0005f
            && std::fabs(previous.light - cfg.light) < 0.0005f)
            return;
        m_config.setIconPackTint(packId, cfg);
        m_config.save();
        const std::string activeId = m_config.iconPack.empty()
            ? switchu::icons::kDefaultPackId : m_config.iconPack;
        const std::string id = packId.empty() ? switchu::icons::kDefaultPackId : packId;
        if (id == activeId)
            syncColourIconContrast();
    });
    m_themeShop->onIconPackApply([this](const std::string& packId) {
        applyIconPack(packId);
    });
    m_themeShop->onIconPackDownload([this](const std::string& packId) {
        const auto* entry = findCatalogIconEntry(packId);
        if (!entry)
            return;
        startIconPackTransfer(*entry, false);
    });
    m_themeShop->onIconPackDownloadApply([this](const std::string& packId) {
        const auto* entry = findCatalogIconEntry(packId);
        if (!entry)
            return;
        startIconPackTransfer(*entry, true);
    });
    m_themeShop->onIconPackDelete([this](const std::string& packId) {
        auto& i18n = nxui::I18n::instance();
        if (!m_dialog) return;
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            i18n.tr("themeshop.icons.remove", "Remove Icon Pack"),
            i18n.tr("themeshop.icons.remove_confirm",
                    "Delete this icon pack from the console?"),
            {{i18n.tr("button.cancel", "Cancel"), {}, false},
             {i18n.tr("themeshop.installed.remove_confirm_yes", "Remove"),
              [this, packId]() { removeIconPack(packId); }, true}});
        focusManager().setFocus(m_dialog.get());
    });
    m_themeShop->onIconCatalogUpdated([this]() {
        refreshThemeShopState();
    });
    m_themeShop->onThemeShopApply([this](const std::string& presetId) {
        DebugLog::log("[theme-apply] request from Theme Shop: preset=%s", presetId.c_str());
        ThemePreset* preset = findPresetPtr(presetId);
        if (!preset) {
            DebugLog::log("[theme-apply] preset not found: %s", presetId.c_str());
            return;
        }

        activateThemePreset(preset, true);
    });
    m_themeShop->onThemeShopDelete([this](const std::string& presetId) {
        auto& i18n = nxui::I18n::instance();
        ThemePreset* preset = findPresetPtr(presetId);
        if (!preset || preset->source == ThemePresetSource::BuiltIn) {
            if (!m_dialog) return;
            m_dialogReturnFocus = focusManager().current();
            m_dialog->show(
                i18n.tr("themeshop.installed.remove", "Remove Theme"),
                i18n.tr("settings.theme.builtin_readonly", "Built-in presets cannot be modified."),
                {{ i18n.tr("button.ok", "OK"), {}, true }});
            focusManager().setFocus(m_dialog.get());
            return;
        }

        if (!m_dialog) {
            deletePreset(presetId);
            return;
        }
        m_dialogReturnFocus = focusManager().current();
        m_dialog->show(
            i18n.tr("themeshop.installed.remove", "Remove Theme"),
            i18n.tr("themeshop.installed.remove_confirm",
                    "Remove this theme from the console?"),
            {{i18n.tr("button.cancel", "Cancel"), {}, false},
             {i18n.tr("themeshop.installed.remove_confirm_yes", "Remove"),
              [this, presetId]() { deletePreset(presetId); }, true}});
        focusManager().setFocus(m_dialog.get());
    });
    m_themeShop->onThemeShopDownload([queueThemeTransfer](const std::string& themeId) {
        queueThemeTransfer(themeId, false);
    });
    m_themeShop->onThemeShopDownloadInstall([queueThemeTransfer](const std::string& themeId) {
        queueThemeTransfer(themeId, true);
    });
    m_themeShop->onDialogRequest([this](const std::string& title,
                                        const std::string& msg,
                                        std::vector<ThemeShopScreen::DialogButtonDef> buttons) {
        if (!m_dialog) return;
        std::vector<OverlayDialog::ButtonDef> dlgButtons;
        bool preserveReturnFocus = (m_dialog && m_dialog->isActive() && focusManager().current() == m_dialog.get() && m_dialogReturnFocus != nullptr);
        for (size_t i = 0; i < buttons.size(); ++i) {
            auto cb = buttons[i].onPress;
            bool isLast = (i == buttons.size() - 1);
            if (buttons.size() == 1) {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            } else if (isLast) {
                dlgButtons.push_back({buttons[i].label, [cb]() { if (cb) cb(); }, true});
            } else {
                dlgButtons.push_back({buttons[i].label, [this, cb]() {
                    m_audio.playSfx(Sfx::ConfirmPositive);
                    m_dialog->hide();
                    if (cb) cb();
                }, true});
            }
        }
        if (!preserveReturnFocus)
            m_dialogReturnFocus = focusManager().current();
        m_dialog->show(title, msg, std::move(dlgButtons));
        focusManager().setFocus(m_dialog.get());
    });
    m_themeShop->onClosed([this, clearCompletedThemeTransferState]() {
        m_navigator.routeDidClose(switchu::navigation::Route::ThemeShop);
        if (m_configSaveFuture.valid())
            m_configSaveFuture.wait();
        m_configSaveFuture = m_threadPool.submit([cfg = m_config]() {
            cfg.save();
        });
        DebugLog::log("[config] save queued");
        clearCompletedThemeTransferState();
        if (isCurrentFocusableWidget(m_sidebar.themeShopButton())) {
            m_suppressNextNavigateSfx = true;
            focusManager().setFocus(m_sidebar.themeShopButton());
        }
    });

}

void WiiUMenuApp::reloadThemePresets() {
    m_allPresets = ThemePreset::builtInPresets();
    auto userPresets = ThemePreset::loadUserPresets();
    auto installedPackages = ThemePreset::loadInstalledPackages();
    m_allPresets.insert(m_allPresets.end(), userPresets.begin(), userPresets.end());
    m_allPresets.insert(m_allPresets.end(), installedPackages.begin(), installedPackages.end());
    refreshLockClockFontEntries();
}

void WiiUMenuApp::startThemePackageTransfer(const ThemeCatalogClient::Entry& entry, bool installMode) {
    syncThemePackageTransfer();

    if (m_themePackageTransferFuture.valid()
        && m_themePackageTransferFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        if (m_themeShop) {
            auto& i18n = nxui::I18n::instance();
            m_themeShop->requestToast(i18n.tr("themeshop.community.transfer_busy",
                                              "Another theme transfer is already running."),
                                      2.5f);
        }
        return;
    }

    auto shared = std::make_shared<ThemePackageTransferShared>();
    shared->themeId = entry.id;
    shared->installMode = installMode;
    shared->destinationPath = ThemePackageInstaller::destinationRootFor(
        entry.id,
        installMode ? ThemePackageInstaller::Mode::InstallAndApply
                    : ThemePackageInstaller::Mode::InstallOnly);
    auto& i18n = nxui::I18n::instance();
    const std::string preparingLabel = installMode
        ? i18n.tr("themeshop.transfer.prepare_apply", "Preparing download + apply...")
        : i18n.tr("themeshop.transfer.prepare_install", "Preparing download + install...");
    const std::string unknownTransferError = i18n.tr("themeshop.transfer.unknown_error", "Unknown transfer error");
    shared->state.begin(preparingLabel);
    shared->revision = 1;

    m_themePackageTransfer = shared;
    m_themePackageTransferUiRevision = 0;
    m_themePackageTransferHandledRevision = 0;

    if (m_themeShop)
        m_themeShop->setPackageTransferState(shared->state, shared->themeId, shared->installMode);
    // Theme Shop shows on-card / thin-sheet progress — skip the modal so we
    // don't steal focus or pressure the Community preview GPU budget.
    const bool inlineThemeShopProgress = m_themeShop && m_themeShop->isActive();
    if (m_progressDialog && !inlineThemeShopProgress) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->show(i18n.tr("themeshop.transfer.dialog_title", "Downloading Theme"),
                               preparingLabel,
                               -1.f);
        focusManager().setFocus(m_progressDialog.get());
    }

    const std::string catalogUrl = m_themeShop
        ? m_themeShop->communityCatalogUrl()
        : ThemeCatalogClient::kDefaultCatalogUrl;
    ThemePackageInstaller::Mode mode = installMode
        ? ThemePackageInstaller::Mode::InstallAndApply
        : ThemePackageInstaller::Mode::InstallOnly;

    DebugLog::log("[themeshop] package transfer start: id=%s apply=%d",
                  entry.id.c_str(),
                  installMode ? 1 : 0);

    m_themePackageTransferFuture = m_threadPool.submit([shared, catalogUrl, entry, mode, unknownTransferError]() {
        try {
            auto result = ThemePackageInstaller::run(
                catalogUrl,
                entry,
                mode,
                [shared](std::string label, float progress) {
                    std::lock_guard<std::mutex> lk(shared->mutex);
                    shared->state.update(std::move(label), progress);
                    ++shared->revision;
                });

            std::lock_guard<std::mutex> lk(shared->mutex);
            shared->destinationPath = result.destinationPath;
            shared->state.succeed(result.message);
            ++shared->revision;
            DebugLog::log("[themeshop] package transfer success: %s", result.themeId.c_str());
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lk(shared->mutex);
            shared->state.fail(ex.what());
            ++shared->revision;
            DebugLog::log("[themeshop] package transfer failed: %s", ex.what());
        } catch (...) {
            std::lock_guard<std::mutex> lk(shared->mutex);
            shared->state.fail(unknownTransferError);
            ++shared->revision;
            DebugLog::log("[themeshop] package transfer failed: unknown error");
        }
    });
}

const ThemeCatalogClient::IconEntry* WiiUMenuApp::findCatalogIconEntry(
    const std::string& packId) const {
    if (!m_themeShop)
        return nullptr;
    const auto& icons = m_themeShop->catalogIconEntries();
    for (const auto& icon : icons) {
        if (icon.id == packId)
            return &icon;
    }
    return nullptr;
}

void WiiUMenuApp::startIconPackTransfer(const ThemeCatalogClient::IconEntry& entry,
                                        bool applyAfter) {
    syncThemePackageTransfer();

    if (m_themePackageTransferFuture.valid()
        && m_themePackageTransferFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        if (m_themeShop) {
            auto& i18n = nxui::I18n::instance();
            m_themeShop->requestToast(i18n.tr("themeshop.community.transfer_busy",
                                              "Another theme transfer is already running."),
                                      2.5f);
        }
        return;
    }
    if (!m_themeShop)
        return;

    auto& i18n = nxui::I18n::instance();
    auto shared = std::make_shared<ThemePackageTransferShared>();
    shared->themeId = entry.id;
    shared->installMode = applyAfter;
    shared->isIconPack = true;
    shared->destinationPath =
        switchu::icons::IconPackInstaller::destinationRootFor(entry.id);
    const std::string preparingLabel =
        i18n.tr("themeshop.icons.prepare_download", "Preparing icon pack download...");
    const std::string unknownTransferError =
        i18n.tr("themeshop.transfer.unknown_error", "Unknown transfer error");
    shared->state.begin(preparingLabel);
    shared->revision = 1;

    m_themePackageTransfer = shared;
    m_themePackageTransferUiRevision = 0;
    m_themePackageTransferHandledRevision = 0;

    m_themeShop->setPackageTransferState(shared->state, shared->themeId, true);
    const bool inlineThemeShopProgress = m_themeShop->isActive();
    if (m_progressDialog && !inlineThemeShopProgress) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->show(i18n.tr("themeshop.icons.dialog_title", "Downloading Icon Pack"),
                               preparingLabel,
                               -1.f);
        focusManager().setFocus(m_progressDialog.get());
    }

    const std::string catalogUrl = m_themeShop->communityCatalogUrl();
    DebugLog::log("[icon-install] transfer start: id=%s apply=%d",
                  entry.id.c_str(), applyAfter ? 1 : 0);

    m_themePackageTransferFuture = m_threadPool.submit(
        [shared, catalogUrl, entry, unknownTransferError]() {
            try {
                auto result = switchu::icons::IconPackInstaller::install(
                    catalogUrl,
                    entry,
                    [shared](std::string label, float progress) {
                        std::lock_guard<std::mutex> lk(shared->mutex);
                        shared->state.update(std::move(label), progress);
                        ++shared->revision;
                    });

                std::lock_guard<std::mutex> lk(shared->mutex);
                shared->destinationPath = result.destinationPath;
                shared->state.succeed(result.message);
                ++shared->revision;
                DebugLog::log("[icon-install] transfer success: %s", result.packId.c_str());
            } catch (const std::exception& ex) {
                std::lock_guard<std::mutex> lk(shared->mutex);
                shared->state.fail(ex.what());
                ++shared->revision;
                DebugLog::log("[icon-install] transfer failed: %s", ex.what());
            } catch (...) {
                std::lock_guard<std::mutex> lk(shared->mutex);
                shared->state.fail(unknownTransferError);
                ++shared->revision;
                DebugLog::log("[icon-install] transfer failed: unknown error");
            }
        });
}

void WiiUMenuApp::removeIconPack(const std::string& packId) {
    if (packId.empty())
        return;
    auto& i18n = nxui::I18n::instance();

    if (resolvedIconPackId() == packId) {
        // Fall back to Default before the glyphs disappear.
        applyIconPack(switchu::icons::kDefaultPackId);
    }

    if (!switchu::icons::IconPackInstaller::remove(packId)) {
        if (m_themeShop)
            m_themeShop->requestToast(
                i18n.tr("themeshop.icons.remove_failed", "Could not remove this icon pack."), 2.8f);
        return;
    }

    m_config.iconPackTints.erase(packId);
    m_config.save();
    if (m_themeShop)
        m_themeShop->invalidateIconPackPreviews();
    refreshThemeShopState();
    if (m_themeShop && m_themeShop->detailOpen())
        m_themeShop->closeDetailView();
    if (m_themeShop)
        m_themeShop->requestToast(
            i18n.tr("themeshop.icons.removed", "Icon pack removed."), 2.4f);
}

void WiiUMenuApp::extractThemeIconPack(const std::string& packId, const std::string& themeRoot) {
    if (packId.empty() || themeRoot.empty())
        return;

    const std::string themeFolder = themeFolderFromInstallPath(themeRoot);
    std::string targetPackId = packId;
    std::string themeName;
    std::string themeAuthor;
    {
        // Prefer theme.icons.packId when present (e.g. Atmosphere → atmos).
        nlohmann::json manifest;
        std::ifstream input(joinPath(themeRoot, "theme.json"));
        if (input) {
            try {
                input >> manifest;
            } catch (...) {
                manifest = nlohmann::json{};
            }
        }
        if (manifest.is_object()) {
            readJsonString(manifest, "name", themeName);
            readJsonString(manifest, "author", themeAuthor);
            auto readPackId = [&](const nlohmann::json& iconsNode) {
                if (!iconsNode.is_object())
                    return;
                for (const char* key : {"packId", "pack_id", "pack", "iconPack", "icon_pack"}) {
                    std::string value;
                    if (readJsonString(iconsNode, key, value)) {
                        targetPackId = std::move(value);
                        return;
                    }
                }
            };
            if (auto icons = manifest.find("icons"); icons != manifest.end())
                readPackId(*icons);
            if (auto theme = manifest.find("theme"); theme != manifest.end() && theme->is_object()) {
                if (auto icons = theme->find("icons"); icons != theme->end())
                    readPackId(*icons);
            }
        }
    }

    const std::string sourceDir = themeRoot + "/assets/icons";
    const std::string destDir =
        switchu::icons::IconPackInstaller::destinationRootFor(targetPackId);

    // Companion pack already on the console (builtin atmos, prior download):
    // drop the theme-local copy and any slug-named orphan clone.
    const std::string existing =
        switchu::icons::resolvePackIconsDirectory(SD_ASSETS, targetPackId);
    if (!existing.empty()) {
        std::error_code ec;
        if (switchu::icons::iconDirLooksUsable(sourceDir))
            std::filesystem::remove_all(sourceDir, ec);
        if (!themeFolder.empty() && themeFolder != targetPackId) {
            const std::string orphan =
                switchu::icons::IconPackInstaller::destinationRootFor(themeFolder);
            if (orphan != existing && pathExists(orphan)) {
                std::filesystem::remove_all(orphan, ec);
                DebugLog::log("[icon-pack] removed orphan theme clone pack=%s",
                              themeFolder.c_str());
            }
            if (m_config.iconPack == themeFolder) {
                m_config.iconPack = targetPackId;
                m_config.save();
            }
        }
        DebugLog::log("[icon-pack] theme icons skipped; pack %s already present at %s",
                      targetPackId.c_str(), existing.c_str());
        return;
    }

    if (!switchu::icons::iconDirLooksUsable(sourceDir))
        return;

    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    int copied = 0;
    for (const auto& entry : std::filesystem::directory_iterator(sourceDir, ec)) {
        if (ec || !entry.is_regular_file(ec))
            continue;
        const std::string name = entry.path().filename().string();
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".png") != 0)
            continue; // skip the theme's own *_disabled markers

        std::ifstream input(entry.path(), std::ios::binary);
        if (!input)
            continue;
        std::string bytes((std::istreambuf_iterator<char>(input)),
                          std::istreambuf_iterator<char>());
        std::ofstream output(destDir + "/" + name, std::ios::binary | std::ios::trunc);
        if (!output)
            continue;
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (output)
            ++copied;
    }

    if (copied == 0) {
        DebugLog::log("[icon-pack] theme icons copy failed: %s", sourceDir.c_str());
        return;
    }

    writeIconPackManifest(destDir, targetPackId,
                          themeName.empty() ? targetPackId : themeName,
                          themeAuthor);

    // The pack now lives in the shared icon directory: drop the copy inside the
    // theme so it is not listed twice.
    std::filesystem::remove_all(sourceDir, ec);
    if (!themeFolder.empty() && themeFolder != targetPackId) {
        const std::string orphan =
            switchu::icons::IconPackInstaller::destinationRootFor(themeFolder);
        if (pathExists(orphan))
            std::filesystem::remove_all(orphan, ec);
    }
    DebugLog::log("[icon-pack] theme icons extracted: theme=%s pack=%s files=%d",
                  themeFolder.c_str(), targetPackId.c_str(), copied);
}

void WiiUMenuApp::syncThemePackageTransfer() {
    auto shared = m_themePackageTransfer;
    if (!shared)
        return;

    bool futureReady = false;
    if (m_themePackageTransferFuture.valid()
        && m_themePackageTransferFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_themePackageTransferFuture.get();
        futureReady = true;
    }

    ThemeTransferState state;
    std::string themeId;
    bool installMode = false;
    bool isIconPack = false;
    std::uint64_t revision = 0;
    std::string destinationPath;
    {
        std::lock_guard<std::mutex> lk(shared->mutex);
        state = shared->state;
        themeId = shared->themeId;
        installMode = shared->installMode;
        isIconPack = shared->isIconPack;
        revision = shared->revision;
        destinationPath = shared->destinationPath;
    }

    if (revision != m_themePackageTransferUiRevision) {
        if (m_themeShop)
            m_themeShop->setPackageTransferState(state, themeId, installMode);
        const bool inlineThemeShopProgress = m_themeShop && m_themeShop->isActive();
        if (m_progressDialog && state.isRunning() && !inlineThemeShopProgress) {
            auto& i18n = nxui::I18n::instance();
            m_progressDialog->updateState(
                state.label().empty()
                    ? i18n.tr("themeshop.transfer.downloading", "Downloading theme...")
                    : state.label(),
                state.progress01());
            if (focusManager().current() != m_progressDialog.get())
                focusManager().setFocus(m_progressDialog.get());
        }
        m_themePackageTransferUiRevision = revision;
    }

    if (!futureReady || revision == m_themePackageTransferHandledRevision)
        return;

    if (state.isReady() && isIconPack) {
        auto& i18n = nxui::I18n::instance();
        DebugLog::log("[icon-install] transfer ready: id=%s apply=%d destination=%s",
                      themeId.c_str(), installMode ? 1 : 0, safeLogPath(destinationPath));
        if (installMode)
            applyIconPack(themeId);
        else
            refreshThemeShopState();
        state.succeed(i18n.tr("themeshop.icons.installed", "Icon pack installed."));
        if (m_themeShop)
            m_themeShop->setPackageTransferState(state, themeId, installMode);
    }

    if (state.isReady() && !isIconPack) {
        DebugLog::log("[theme-apply] transfer ready: id=%s installMode=%d destination=%s",
                      themeId.c_str(),
                      installMode ? 1 : 0,
                      safeLogPath(destinationPath));

        // Older theme packages still bundle sidebar glyphs. Move them into the
        // shared icon directory so they show up as a normal downloaded pack
        // (and keep working after the theme is removed).
        extractThemeIconPack(themeId, destinationPath);
        if (m_themeShop)
            m_themeShop->invalidateIconPackPreviews();

        reloadThemePresets();
        DebugLog::log("[theme-apply] presets reloaded after transfer: count=%zu", m_allPresets.size());

        auto& i18n = nxui::I18n::instance();
        std::string successMessage;
        if (installMode) {
            ThemePreset* preset = findPresetPtr("package:" + themeId);
            if (preset) {
                m_forceThemeResourceReload = !preset->fonts.regularPath.empty()
                    || !preset->fonts.smallPath.empty()
                    || !preset->background.imagePath.empty();
                if (!preset->icons.basePath.empty())
                    m_sidebar.invalidateAssetsCache();
                DebugLog::log("[theme-apply] auto-applying installed package: id=%s", themeId.c_str());
                activateThemePreset(preset, true);
                successMessage = i18n.tr("themeshop.transfer.installed_applied", "Theme installed and applied.");
            } else {
                DebugLog::log("[theme-apply] auto-apply failed, preset missing after install: id=%s", themeId.c_str());
                refreshThemeShopState();
                successMessage = i18n.tr("themeshop.transfer.installed_apply_failed",
                                         "Theme installed, but it could not be applied automatically.");
            }
        } else {
            DebugLog::log("[theme-apply] install-only transfer complete: id=%s", themeId.c_str());
            refreshThemeShopState();
            successMessage = i18n.tr("themeshop.transfer.installed", "Theme installed.");
        }

        state.succeed(successMessage);
        if (m_themeShop)
            m_themeShop->setPackageTransferState(state, themeId, installMode);
    }

    if (m_themeShop && !state.label().empty())
        m_themeShop->requestToast(state.label(), state.hasFailed() ? 3.5f : 2.8f);
    if (m_progressDialog && m_progressDialog->isActive()) {
        m_progressDialog->updateState(state.label(), state.progress01());
        m_progressDialog->hide();
        if (m_themeShop && m_themeShop->isActive())
            focusManager().setFocus(m_themeShop.get());
    }

    m_themePackageTransferHandledRevision = revision;
}

std::vector<ThemeShopScreen::ThemeShopEntry> WiiUMenuApp::buildThemeShopEntries() {
    std::vector<ThemeShopScreen::ThemeShopEntry> entries;
    entries.reserve(m_allPresets.size());

    ThemePreset* activePreset = findPresetPtr(m_activePresetName);
    std::string activeId = activePreset ? (activePreset->id.empty() ? activePreset->name : activePreset->id)
                                        : m_activePresetName;

    for (const auto& preset : m_allPresets) {
        ThemeShopScreen::ThemeShopEntry entry;
        entry.id = preset.id.empty() ? preset.name : preset.id;
        entry.name = preset.name;
        entry.author = preset.author;
        entry.version = preset.version;
        entry.source = sourceLabel(preset.source);
        if (preset.soundPreset.rfind("package:", 0) == 0)
            entry.soundPreset = "Bundled";
        else if (preset.soundPreset == "wiiu")
            entry.soundPreset = "Wii U";
        else
            entry.soundPreset = preset.soundPreset;
        entry.coverPath = installedThemePreviewPath(preset);
        entry.active = (entry.id == activeId);
        entry.removable = (preset.source != ThemePresetSource::BuiltIn);
        {
            const std::string folder = themeFolderFromPreset(preset);
            const std::string companionId = resolvedThemeIconPackId(preset, folder);
            const std::string iconsDir = resolveThemeAssetPath(preset, preset.icons.basePath);
            entry.hasIcons = switchu::icons::iconDirLooksUsable(iconsDir);
            if (!entry.hasIcons && !companionId.empty()) {
                const std::string packDir =
                    switchu::icons::resolvePackIconsDirectory(SD_ASSETS, companionId);
                entry.hasIcons = switchu::icons::iconDirLooksUsable(packDir);
            }
            if (!entry.hasIcons && !folder.empty() && folder != companionId) {
                const std::string folderPackDir =
                    switchu::icons::resolvePackIconsDirectory(SD_ASSETS, folder);
                entry.hasIcons = switchu::icons::iconDirLooksUsable(folderPackDir);
            }
        }
        // Cheap local dir checks — no I/O beyond a couple of is_directory calls.
        if (!preset.installPath.empty()) {
            const std::string soundBase = resolveThemeSoundBase(preset.installPath);
            const std::string root = soundBase.empty() ? preset.installPath : soundBase;
            entry.hasMusic = pathExists(root + "/music");
            entry.hasSounds = pathExists(root + "/sfx");
        }
        entries.push_back(std::move(entry));
    }

    return entries;
}


const std::string& WiiUMenuApp::resolvedIconPackId() const {
    // Compare the config directly first so the common cached path does not
    // construct a temporary string every frame.
    if (m_resolvedIconPackValid
        && (m_config.iconPack == m_resolvedIconPackInput
            || (m_config.iconPack.empty()
                && m_resolvedIconPackInput == switchu::icons::kDefaultPackId)))
        return m_resolvedIconPackCache;
    const std::string configured = m_config.iconPack.empty()
        ? switchu::icons::kDefaultPackId : m_config.iconPack;
    // Normalize legacy theme:package:<id> ids to theme:<folder>.
    const std::string normalized = switchu::icons::isThemePackId(configured)
        ? switchu::icons::makeThemePackId(configured)
        : configured;
    const std::string dir = switchu::icons::resolvePackIconsDirectory(SD_ASSETS, normalized);
    std::string resolved;
    if (dir.empty()) {
        // Log once per distinct id — resolvedIconPackId is hit every frame.
        static std::string s_lastFailed;
        if (s_lastFailed != normalized) {
            s_lastFailed = normalized;
            DebugLog::log("[icon-pack] resolve failed pack=%s (configured=%s); healing to default",
                          normalized.c_str(), configured.c_str());
        }
        resolved = switchu::icons::kDefaultPackId;
    } else {
        resolved = normalized;
    }
    m_resolvedIconPackInput = configured;
    m_resolvedIconPackCache = resolved;
    m_resolvedIconPackValid = true;
    return m_resolvedIconPackCache;
}

std::string WiiUMenuApp::iconsDirectoryForPack(const std::string& packId) const {
    const std::string id = switchu::icons::isThemePackId(packId)
        ? switchu::icons::makeThemePackId(packId) : packId;
    if (!switchu::icons::isThemePackId(id))
        return {};
    return switchu::icons::resolvePackIconsDirectory(SD_ASSETS, id);
}

namespace {

// Packs that ship with SwitchU: always present, never removable from the shop.
bool isBuiltinIconPack(const std::string& packId) {
    return packId == switchu::icons::kDefaultPackId
        || packId == switchu::icons::kAtmosPackId;
}

} // namespace

std::vector<ThemeShopScreen::ThemeShopEntry> WiiUMenuApp::buildIconPackEntries() {
    std::vector<ThemeShopScreen::ThemeShopEntry> entries;
    const std::string activeId = resolvedIconPackId();

    // Companion pack ids claimed by installed themes (theme.icons.packId).
    std::unordered_map<std::string, std::string> companionByThemeFolder;
    std::unordered_set<std::string> companionPackIds;
    for (const auto& preset : m_allPresets) {
        if (preset.source != ThemePresetSource::InstalledPackage)
            continue;
        const std::string folder = themeFolderFromPreset(preset);
        if (folder.empty())
            continue;
        if (!preset.icons.packId.empty()) {
            companionByThemeFolder[folder] = preset.icons.packId;
            companionPackIds.insert(preset.icons.packId);
        }
    }

    // Heal slug-named clones when the theme now points at a companion pack.
    for (const auto& [folder, companionId] : companionByThemeFolder) {
        if (folder.empty() || folder == companionId)
            continue;
        const std::string companionDir =
            switchu::icons::resolvePackIconsDirectory(SD_ASSETS, companionId);
        if (companionDir.empty())
            continue;
        const std::string orphan =
            switchu::icons::IconPackInstaller::destinationRootFor(folder);
        if (!pathExists(orphan))
            continue;
        std::error_code ec;
        std::filesystem::remove_all(orphan, ec);
        DebugLog::log("[icon-pack] healed orphan clone pack=%s -> %s",
                      folder.c_str(), companionId.c_str());
        if (m_config.iconPack == folder) {
            m_config.iconPack = companionId;
            m_config.save();
        }
    }

    auto addPack = [&](const switchu::icons::IconPackInfo& pack) {
        // Hide theme-folder clones that duplicate a companion pack id.
        if (companionByThemeFolder.count(pack.id)
            && companionByThemeFolder[pack.id] != pack.id
            && companionPackIds.count(companionByThemeFolder[pack.id])) {
            const std::string& companion = companionByThemeFolder[pack.id];
            if (!switchu::icons::resolvePackIconsDirectory(SD_ASSETS, companion).empty())
                return;
        }

        const bool alreadyListed = std::any_of(
            entries.begin(), entries.end(),
            [&](const ThemeShopScreen::ThemeShopEntry& e) { return e.id == pack.id; });
        if (alreadyListed)
            return;

        ThemeShopScreen::ThemeShopEntry entry;
        entry.id = pack.id;
        entry.name = pack.name;
        entry.author = pack.author;
        entry.version = "1.0.0";
        entry.builtin = isBuiltinIconPack(pack.id);
        entry.source = pack.fromTheme ? "Theme"
            : (entry.builtin ? "Built-in" : "Installed");
        entry.coverPath = switchu::icons::ensurePackPreview(pack.directory, pack.id);
        entry.active = (pack.id == activeId);
        // Downloaded / extracted packs on SD can be removed; builtins and
        // in-theme glyphs stay tied to their theme.
        entry.installed = true;
        entry.removable = !pack.fromTheme && !entry.builtin;
        entry.colourPack = pack.colourPack;
        entries.push_back(std::move(entry));
    };

    for (const auto& pack : switchu::icons::discoverPacks(SD_ASSETS))
        addPack(pack);
    // Theme icon folders are discovered from disk the same way as SD packs.
    for (const auto& pack : switchu::icons::discoverThemePacks())
        addPack(pack);

    // Online catalog: list every catalog pack (installed ones stay installed —
    // Icon Shop shows them with an Installed badge, like Theme Shop Community).
    if (m_themeShop) {
        const std::string& catalogUrl = m_themeShop->communityCatalogUrl();
        const std::size_t slash = catalogUrl.find_last_of('/');
        const std::string catalogBase =
            slash == std::string::npos ? std::string() : catalogUrl.substr(0, slash + 1);
        auto resolveCoverUrl = [&](const std::string& cover) -> std::string {
            if (cover.empty())
                return {};
            const bool absolute = cover.rfind("https://", 0) == 0
                || cover.rfind("http://", 0) == 0;
            return absolute ? cover : catalogBase + cover;
        };
        for (const auto& icon : m_themeShop->catalogIconEntries()) {
            auto existing = std::find_if(
                entries.begin(), entries.end(),
                [&](const ThemeShopScreen::ThemeShopEntry& e) { return e.id == icon.id; });
            if (existing != entries.end()) {
                // Enrich the on-console row so Icon Shop can list it.
                existing->catalog = true;
                if (!icon.name.empty())
                    existing->name = icon.name;
                if (!icon.author.empty())
                    existing->author = icon.author;
                if (!icon.version.empty())
                    existing->version = icon.version;
                if (!icon.description.empty())
                    existing->description = icon.description;
                existing->colourPack = icon.colour;
                // Keep both: Installed shelf uses coverPath (live mosaic + tint);
                // Icon Shop uses coverUrl (vanilla catalog art).
                existing->coverUrl = resolveCoverUrl(icon.cover);
                continue;
            }
            // Catalog row whose theme companion is already installed as another id.
            if (!icon.theme.empty()) {
                std::string linkedPack;
                if (auto it = companionByThemeFolder.find(icon.theme);
                    it != companionByThemeFolder.end())
                    linkedPack = it->second;
                const bool themeCompanionInstalled = std::any_of(
                    entries.begin(), entries.end(),
                    [&](const ThemeShopScreen::ThemeShopEntry& e) {
                        return e.installed && (e.id == icon.theme
                            || (!linkedPack.empty() && e.id == linkedPack));
                    });
                if (themeCompanionInstalled)
                    continue;
            }

            ThemeShopScreen::ThemeShopEntry entry;
            entry.id = icon.id;
            entry.name = icon.name;
            entry.author = icon.author;
            entry.version = icon.version;
            entry.description = icon.description;
            entry.builtin = icon.builtin;
            entry.catalog = true;
            entry.installed = false;
            entry.removable = false;
            entry.active = false;
            entry.colourPack = icon.colour;
            entry.source = "Shop";
            entry.coverUrl = resolveCoverUrl(icon.cover);
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

std::string WiiUMenuApp::resolveActiveIconGlyphPath(const char* fileName) const {
    return switchu::icons::resolveGlyphPath(SD_ASSETS, resolvedIconPackId(), fileName);
}

void WiiUMenuApp::reloadSidebarIcons() {
    // A reload is also the point where a pack may have just appeared on SD.
    m_resolvedIconPackValid = false;
    const std::string& packId = resolvedIconPackId();
    // Theme packs resolve to their on-disk icons dir; SD packs leave this empty
    // and SidebarManager loads from icons/<packId>/ via packId.
    const std::string customIconsBase = iconsDirectoryForPack(packId);
    const std::string fallbackPackId = switchu::icons::isThemePackId(packId)
        ? switchu::icons::kDefaultPackId : packId;
    DebugLog::log("[icon-pack] reload pack=%s custom=%s fallback=%s",
                  packId.c_str(),
                  customIconsBase.empty() ? "<empty>" : customIconsBase.c_str(),
                  fallbackPackId.c_str());
    m_sidebar.invalidateAssetsCache();
    m_sidebar.reloadAssets(app().gpu(), app().renderer(), SD_ASSETS, customIconsBase, fallbackPackId);
    // The layout-flip chrome button uses the same pack, so refresh it too.
    if (m_layoutFlip) {
        m_layoutFlip->setIconHslAdjust(m_sidebar.iconHslAdjust());
        m_layoutFlip->setColourIconContrast(m_sidebar.colourIconContrast());
        m_layoutFlip->reloadIcon(app().gpu(), app().renderer(),
                                 resolveActiveIconGlyphPath(switchu::icons::kChangeViewFile));
    }
}

void WiiUMenuApp::applyIconPack(const std::string& packId) {
    std::string id = packId.empty() ? switchu::icons::kDefaultPackId : packId;
    if (switchu::icons::isThemePackId(id))
        id = switchu::icons::makeThemePackId(id);
    else
        id = switchu::icons::canonicalizeIconPackId(id);
    const bool packChanged = m_config.iconPack != id;
    if (packChanged) {
        m_config.iconPack = id;
        m_config.save();
    }
    // Reapplying the same id can be how a freshly downloaded pack becomes
    // visible, so do not let the resolve cache hide it.
    m_resolvedIconPackValid = false;
    DebugLog::log("[icon-pack] apply pack=%s changed=%d", id.c_str(), packChanged ? 1 : 0);
    // Bake once with the pack's current HSL (avoid reload → sync → reload).
    applyActivePackHslToSidebar();
    reloadSidebarIcons();
    if (m_quickSettings)
        m_quickSettings->invalidateMusicControlIcons();
    syncColourIconContrast();
    refreshThemeShopState();
    if (m_themeShop)
        m_themeShop->requestToast(
            nxui::I18n::instance().tr("themeshop.icons.applied", "Icon pack applied."), 2.0f);
}

void WiiUMenuApp::refreshThemeShopState(bool rebuildTab) {
    if (!m_themeShop) return;

    ThemePreset* activePreset = findPresetPtr(m_activePresetName);
    std::string activeId = activePreset ? (activePreset->id.empty() ? activePreset->name : activePreset->id)
                                        : m_activePresetName;

    // Normalize legacy theme:package:* ids without clobbering a valid selection.
    const std::string& activePackId = resolvedIconPackId();
    if (m_config.iconPack != activePackId && switchu::icons::isThemePackId(m_config.iconPack)
        && activePackId != switchu::icons::kDefaultPackId) {
        m_config.iconPack = activePackId;
        m_config.save();
    }

    m_themeShop->setTheme(&m_theme);
    m_themeShop->setMusicState(m_audio.isPlaying(), m_audio.volume(), m_audio.sfxVolume());
    m_themeShop->setGridLayoutState(m_config.gridColumns, m_config.gridRows);
    m_themeShop->setActionHintStyleState(m_config.actionHintStyle);
    m_themeShop->setSortLargeTilesState(m_config.sortLargeTiles);
    m_themeShop->setSortLargeTilesAvailable(homeLayoutSupportsLargeTiles());
    m_themeShop->setLockScreenState(m_config.lockScreenEnabled);
    m_themeShop->setPowerButtonAction(m_config.powerButtonAction);
    m_themeShop->setLockScreenClockFontState(m_config.lockScreenClockFont);
    m_themeShop->setLockClockFontContext(
        SD_ASSETS,
        m_loadedRegularFontPath.empty()
            ? std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf"
            : m_loadedRegularFontPath);
    refreshLockClockFontEntries();
    m_themeShop->setIconContrastAdaptState(m_config.iconContrastAdapt);
    m_themeShop->setLockScreenTransitionState(m_config.lockScreenTransition);
    m_themeShop->setLockScreenZoomDirState(m_config.lockScreenZoomDir);
    m_themeShop->setLockScreenWipeDirState(m_config.lockScreenWipeDir);
    m_themeShop->setThemeLockClockFontOverrideState(m_config.themeLockClockFontOverride);
    m_themeShop->setThemeIconPackOverrideState(m_config.themeIconPackOverride);
    m_themeShop->setThemeMusicOverrideState(m_config.themeMusicOverride);
    m_themeShop->setThemeMusicAddToPlaylistState(m_config.themeMusicAddToPlaylist);
    m_themeShop->setThemeMusicPlayOnApplyState(m_config.themeMusicPlayOnApply);
    m_themeShop->setCursorMotionModeState(m_config.cursorMotionMode);
    m_themeShop->setPageTransitionModeState(m_config.pageTransitionMode);
    m_themeShop->setThemeMatchAccent(m_activeColors.accentH,
                                     m_activeColors.accentS,
                                     m_activeColors.accentL);
    {
        std::unordered_map<std::string, ThemeShopScreen::IconPackTintState> tints;
        for (const auto& [packId, tint] : m_config.iconPackTints) {
            ThemeShopScreen::IconPackTintState state;
            state.enabled = tint.enabled;
            state.flatten = tint.flatten;
            state.grayscale = tint.grayscale;
            state.matchTheme = tint.matchTheme;
            state.hue = tint.hue;
            state.sat = tint.sat;
            state.light = tint.light;
            tints[packId] = state;
        }
        m_themeShop->setIconPackTintState(tints);
    }
    m_themeShop->setThemeShopState(buildThemeShopEntries(), activeId);
    m_themeShop->setIconPackState(buildIconPackEntries(), activePackId);
    if (rebuildTab)
        m_themeShop->rebuildCurrentTab();
}

void WiiUMenuApp::applyActivePackHslToSidebar() {
    const AppConfig::IconPackTint packTint = m_config.iconPackTintFor(resolvedIconPackId());
    switchu::icons::HslAdjust hsl;
    hsl.enabled = packTint.enabled;
    hsl.flatten = packTint.flatten;
    hsl.grayscale = packTint.grayscale;
    hsl.hue = std::clamp(packTint.hue, 0.f, 1.f);
    hsl.sat = std::clamp(packTint.sat, 0.f, 1.f);
    hsl.light = std::clamp(packTint.light, 0.f, 1.f);
    m_sidebar.setIconHslAdjust(hsl);
    if (m_layoutFlip)
        m_layoutFlip->setIconHslAdjust(hsl);
}

void WiiUMenuApp::applyStyleSettings() {
    using namespace switchu::ui;
    style::focusRingPx = std::clamp(m_config.focusRingPx, 1, 6);
    style::focusGlowPct = std::clamp(m_config.focusGlowPct, 0, 100);
    style::focusLiftPct = std::clamp(m_config.focusLiftPct, 0, 100);
    style::focusOvershootPct = std::clamp(m_config.focusOvershootPct, 0, 100);
    style::titleScalePct = std::clamp(m_config.titleScalePct, 80, 120);
    style::titleCornerStyle = std::clamp(m_config.titleCornerStyle, 0, 2);
    style::titleBackgroundPct = std::clamp(m_config.titleBackgroundPct, 0, 100);
    style::hudGapPx = std::clamp(m_config.hudGapPx, 0, 24);
    style::showClock = m_config.showHudClock;
    style::showDate = m_config.showHudDate;
    style::showBattery = m_config.showHudBattery;
    style::showProfile = m_config.showHudProfile;
    style::wallpaperBlurPx = std::clamp(m_config.wallpaperBlurPx, 0, 16);
    style::wallpaperSaturationPct = std::clamp(m_config.wallpaperSaturationPct, 0, 150);
    style::wallpaperSharpnessPct = std::clamp(m_config.wallpaperSharpnessPct, 0, 100);
    style::wallpaperVibrancePct = std::clamp(m_config.wallpaperVibrancePct, 0, 100);
    style::wallpaperNoisePct = std::clamp(m_config.wallpaperNoisePct, 0, 100);
    style::backdropTintPct = std::clamp(m_config.backdropTintPct, 0, 100);
    style::pageSlideMs = std::clamp(m_config.pageSlideMs, 150, 600);

    auto& ren = app().renderer();
    ren.setPanelStyle(m_config.panelTintMix, m_config.panelBorderPct,
                      m_config.panelGrainPct, m_config.panelShadowPct);
    const int opacityIndex = std::clamp(m_config.opacityTint, 0, 9);
    ren.setOpacityStyle(switchu::ui::accent::valid(opacityIndex),
                        switchu::ui::accent::colorFor(opacityIndex),
                        m_config.opacitySolid);
    m_sidebar.setOpacityTint(switchu::ui::accent::valid(opacityIndex),
                             switchu::ui::accent::colorFor(opacityIndex));

    if (m_titlePill) {
        m_titlePill->setTextScale(m_config.titleScalePct);
        m_titlePill->setCornerStyle(m_config.titleCornerStyle);
        m_titlePill->setBackgroundStrength(m_config.titleBackgroundPct);
    }
    if (m_topHud) {
        m_topHud->setGap(static_cast<float>(m_config.hudGapPx));
        m_topHud->layout();
    }
    if (m_clock) {
        m_clock->setVisible(m_config.showHudClock);
        m_clock->setShowDate(m_config.showHudDate);
    }
    if (m_battery)
        m_battery->setVisible(m_config.showHudBattery);
    if (m_profileLock)
        m_profileLock->setVisible(m_config.showHudProfile);
    if (m_grid)
        m_grid->setPageSlideMs(m_config.pageSlideMs);
    if (m_background)
        m_background->setTreatment(m_config.wallpaperBlurPx,
                                   m_config.wallpaperSaturationPct,
                                   m_config.wallpaperSharpnessPct,
                                   m_config.wallpaperVibrancePct,
                                   m_config.wallpaperNoisePct,
                                   m_config.backdropTintPct,
                                   m_theme.backgroundAccent);
    configureDynamicLineNavigation();
}

void WiiUMenuApp::syncStyleSettingsToScreen() {
    if (!m_settings)
        return;
    m_settings->setHighlightTint(m_config.highlightTint);
    m_settings->setHighlightGradientState(
        m_config.highlightGradientA, m_config.highlightGradientB,
        m_config.highlightThicknessPx);
    m_settings->setStyleState(
        m_config.panelTintMix, m_config.panelBorderPct,
        m_config.panelGrainPct, m_config.panelShadowPct,
        m_config.focusRingPx, m_config.focusGlowPct,
        m_config.focusLiftPct, m_config.focusOvershootPct,
        m_config.titleScalePct, m_config.titleCornerStyle,
        m_config.titleBackgroundPct, m_config.hudGapPx,
        m_config.showHudClock, m_config.showHudDate,
        m_config.showHudBattery, m_config.showHudProfile,
        m_config.wallpaperBlurPx, m_config.wallpaperSaturationPct,
        m_config.wallpaperSharpnessPct, m_config.wallpaperVibrancePct,
        m_config.wallpaperNoisePct, m_config.backdropTintPct,
        m_config.pageSlideMs);
    for (int slot = 0; slot < 3; ++slot) {
        const auto& preset = m_config.lookPresets[slot];
        m_settings->setLookPresetState(slot, preset.name, preset.filled);
    }
}

void WiiUMenuApp::saveLookPreset(int slot) {
    if (slot < 0 || slot > 2)
        return;
    auto& preset = m_config.lookPresets[slot];
    preset.filled = true;
    if (preset.name.empty() || preset.name == "Look")
        preset.name = "Look " + std::to_string(slot + 1);
    preset.themePreset = m_config.themePreset;
    preset.opacityTint = m_config.opacityTint;
    preset.opacitySolid = m_config.opacitySolid;
    preset.highlightTint = m_config.highlightTint;
    preset.highlightGradientA = m_config.highlightGradientA;
    preset.highlightGradientB = m_config.highlightGradientB;
    preset.highlightThicknessPx = m_config.highlightThicknessPx;
    preset.panelTintMix = m_config.panelTintMix;
    preset.panelBorderPct = m_config.panelBorderPct;
    preset.panelGrainPct = m_config.panelGrainPct;
    preset.panelShadowPct = m_config.panelShadowPct;
    preset.focusRingPx = m_config.focusRingPx;
    preset.focusGlowPct = m_config.focusGlowPct;
    preset.focusLiftPct = m_config.focusLiftPct;
    preset.focusOvershootPct = m_config.focusOvershootPct;
    preset.titleScalePct = m_config.titleScalePct;
    preset.titleCornerStyle = m_config.titleCornerStyle;
    preset.titleBackgroundPct = m_config.titleBackgroundPct;
    preset.hudGapPx = m_config.hudGapPx;
    preset.showHudClock = m_config.showHudClock;
    preset.showHudDate = m_config.showHudDate;
    preset.showHudBattery = m_config.showHudBattery;
    preset.showHudProfile = m_config.showHudProfile;
    preset.wallpaperBlurPx = m_config.wallpaperBlurPx;
    preset.wallpaperSaturationPct = m_config.wallpaperSaturationPct;
    preset.wallpaperSharpnessPct = m_config.wallpaperSharpnessPct;
    preset.wallpaperVibrancePct = m_config.wallpaperVibrancePct;
    preset.wallpaperNoisePct = m_config.wallpaperNoisePct;
    preset.backdropTintPct = m_config.backdropTintPct;
    preset.pageSlideMs = m_config.pageSlideMs;
    preset.motionStyle = m_config.motionStyle;
    preset.uiRadiusPct = m_config.uiRadiusPct;
    preset.iconScale = m_config.iconScale;
    preset.iconSpacing = m_config.iconSpacing;
    preset.carouselShrink = m_config.carouselShrink;
    preset.iconVignette = m_config.iconVignette;
    preset.iconHighlight = m_config.iconHighlight;
    preset.iconShadowOffset = m_config.iconShadowOffset;
    preset.iconShadowAlpha = m_config.iconShadowAlpha;
    preset.layout = {
        m_config.iconAspect, m_config.iconCorners, m_config.iconCornerPct,
        m_config.iconScale, m_config.iconSpacing, m_config.carouselShrink,
        m_config.gridNudgeX, m_config.lineNudgeX, m_config.lineNudgeY};
    m_config.save();
    syncStyleSettingsToScreen();
}

void WiiUMenuApp::loadLookPreset(int slot) {
    if (slot < 0 || slot > 2)
        return;
    const auto& preset = m_config.lookPresets[slot];
    if (!preset.filled)
        return;
    m_config.themePreset = preset.themePreset;
    m_config.opacityTint = preset.opacityTint;
    m_config.opacitySolid = preset.opacitySolid;
    m_config.highlightTint = preset.highlightTint;
    m_config.highlightGradientA = preset.highlightGradientA;
    m_config.highlightGradientB = preset.highlightGradientB;
    m_config.highlightThicknessPx = preset.highlightThicknessPx;
    m_config.panelTintMix = preset.panelTintMix;
    m_config.panelBorderPct = preset.panelBorderPct;
    m_config.panelGrainPct = preset.panelGrainPct;
    m_config.panelShadowPct = preset.panelShadowPct;
    m_config.focusRingPx = preset.focusRingPx;
    m_config.focusGlowPct = preset.focusGlowPct;
    m_config.focusLiftPct = preset.focusLiftPct;
    m_config.focusOvershootPct = preset.focusOvershootPct;
    m_config.titleScalePct = preset.titleScalePct;
    m_config.titleCornerStyle = preset.titleCornerStyle;
    m_config.titleBackgroundPct = preset.titleBackgroundPct;
    m_config.hudGapPx = preset.hudGapPx;
    m_config.showHudClock = preset.showHudClock;
    m_config.showHudDate = preset.showHudDate;
    m_config.showHudBattery = preset.showHudBattery;
    m_config.showHudProfile = preset.showHudProfile;
    m_config.wallpaperBlurPx = preset.wallpaperBlurPx;
    m_config.wallpaperSaturationPct = preset.wallpaperSaturationPct;
    m_config.wallpaperSharpnessPct = preset.wallpaperSharpnessPct;
    m_config.wallpaperVibrancePct = preset.wallpaperVibrancePct;
    m_config.wallpaperNoisePct = preset.wallpaperNoisePct;
    m_config.backdropTintPct = preset.backdropTintPct;
    m_config.pageSlideMs = preset.pageSlideMs;
    m_config.motionStyle = preset.motionStyle;
    m_config.uiRadiusPct = preset.uiRadiusPct;
    m_config.iconScale = preset.iconScale;
    m_config.iconSpacing = preset.iconSpacing;
    m_config.carouselShrink = preset.carouselShrink;
    m_config.iconVignette = preset.iconVignette;
    m_config.iconHighlight = preset.iconHighlight;
    m_config.iconShadowOffset = preset.iconShadowOffset;
    m_config.iconShadowAlpha = preset.iconShadowAlpha;
    if (preset.layout.size() >= 9) {
        m_config.iconAspect    = std::clamp(preset.layout[0], 0, 2);
        m_config.iconCorners   = std::clamp(preset.layout[1], 0, 1);
        m_config.iconCornerPct = std::clamp(preset.layout[2], 0, 100);
        m_config.iconScale     = std::clamp(preset.layout[3], 70, 130);
        m_config.iconSpacing   = std::clamp(preset.layout[4], -50, 100);
        m_config.carouselShrink = std::clamp(preset.layout[5], 0, 100);
        m_config.gridNudgeX    = std::clamp(preset.layout[6], -25, 25);
        m_config.lineNudgeX    = std::clamp(preset.layout[7], -25, 25);
        m_config.lineNudgeY    = std::clamp(preset.layout[8], -25, 25);
        m_config.layoutPreset[slot] = preset.layout;
    }
    m_config.save();

    if (ThemePreset* theme = findPresetPtr(preset.themePreset)) {
        m_forceThemeResourceReload = true;
        activateThemePreset(theme, false);
    }
    applyTheme();
    applyIconPresentation();
    applySidebarPresentation();
    applyMotionStyle();
    applyStyleSettings();
    syncStyleSettingsToScreen();
    reflowHomeGrid();
}

void WiiUMenuApp::activateThemePreset(ThemePreset* preset, bool applyBundledSound) {
    if (!preset) return;

    const std::string presetRef = preset->id.empty() ? preset->name : preset->id;
    DebugLog::log("[theme-apply] begin preset=%s name=%s source=%s installPath=%s applyBundledSound=%d soundPreset=%s",
                  presetRef.c_str(),
                  preset->name.c_str(),
                  themeSourceName(preset->source),
                  safeLogPath(preset->installPath),
                  applyBundledSound ? 1 : 0,
                  safeLogPath(preset->soundPreset));

    m_activePresetName = preset->id.empty() ? preset->name : preset->id;
    m_activeColors = preset->colors;
    m_activeMode = preset->mode;
    m_config.themePreset = m_activePresetName;

    // Themes that ship icons become the active Icons-library pack (unless opted out).
    if (m_config.themeIconPackOverride) {
        std::string folder = themeFolderFromPreset(*preset);
        if (folder.empty())
            folder = presetRef;
        const std::string companionId = resolvedThemeIconPackId(*preset, folder);
        // Prefer theme.icons.packId (e.g. atmos), then folder extract, then theme:*.
        std::string selectedId;
        std::string selectedDir;
        auto tryPack = [&](const std::string& id) {
            if (id.empty() || !selectedId.empty())
                return;
            const std::string dir =
                switchu::icons::resolvePackIconsDirectory(SD_ASSETS, id);
            if (dir.empty())
                return;
            selectedId = id;
            selectedDir = dir;
        };
        tryPack(companionId);
        if (folder != companionId)
            tryPack(folder);
        tryPack(switchu::icons::makeThemePackId(folder));
        if (!selectedId.empty() && m_config.iconPack != selectedId) {
            m_config.iconPack = selectedId;
            DebugLog::log("[icon-pack] theme apply selected pack=%s dir=%s",
                          selectedId.c_str(), selectedDir.c_str());
        }
    } else {
        DebugLog::log("[icon-pack] theme apply skipped (icon override off): preset=%s",
                      presetRef.c_str());
    }

    // Lock clock: optionally follow a theme that ships fonts.
    if (m_config.themeLockClockFontOverride) {
        std::string nextFontId;
        if (!preset->fonts.regularPath.empty() || !preset->fonts.smallPath.empty()) {
            nextFontId = "theme";
        } else {
            const auto packageFonts = switchu::lockfonts::themePackageEntries({*preset});
            if (!packageFonts.empty())
                nextFontId = packageFonts.front().id;
        }
        if (!nextFontId.empty() && m_config.lockScreenClockFont != nextFontId) {
            m_config.lockScreenClockFont = nextFontId;
            DebugLog::log("[lock] theme apply clock font=%s", nextFontId.c_str());
            if (m_themeShop)
                m_themeShop->setLockScreenClockFontState(nextFontId);
            applyLockScreenClockFont();
        }
    }

    m_config.save();

    // Rematch Match-theme packs to the new accent before baking sidebar glyphs
    // so we only reloadAssets once (old path: bake → rematch → bake again).
    rematchIconPackTintsToTheme(/*syncSidebar=*/false);
    applyActivePackHslToSidebar();

    // Cancel in-flight Theme Shop preview workers so Apply does not fight
    // cover uploads for GPU / waitIdle under Applet pressure (Frame Tap).
    if (m_themeShop)
        m_themeShop->pausePreviewWorkForSleep();

    DebugLog::log("[theme-apply] rebuildThemeFromColors start: preset=%s", presetRef.c_str());
    rebuildThemeFromColors();
    DebugLog::log("[theme-apply] rebuildThemeFromColors done: preset=%s", presetRef.c_str());

    // HSL already applied above - sync should be a no-op reload when unchanged.
    syncColourIconContrast();

    // Sound already loads off-thread; queue after visuals so Apply returns sooner.
    // Theme Shop Options: replace playlist (legacy), merge into it, or leave alone.
    if (applyBundledSound && !preset->soundPreset.empty()) {
        if (m_config.themeMusicOverride) {
            DebugLog::log("[theme-apply] changeSoundPreset queued: preset=%s sound=%s",
                          presetRef.c_str(),
                          preset->soundPreset.c_str());
            m_config.soundPreset = preset->soundPreset;
            changeSoundPreset(preset->soundPreset);
        } else if (m_config.themeMusicAddToPlaylist) {
            const std::string base = resolveThemeSoundBase(preset->installPath);
            const std::string musicDir = base.empty() ? std::string() : (base + "/music");
            std::string album = "theme";
            if (!preset->installPath.empty()) {
                const auto pos = preset->installPath.find_last_of("/\\");
                album = "theme-" + ((pos == std::string::npos)
                    ? preset->installPath
                    : preset->installPath.substr(pos + 1));
            } else if (!presetRef.empty()) {
                album = "theme-" + presetRef;
            }
            std::error_code musicDirEc;
            if (!musicDir.empty() && std::filesystem::is_directory(musicDir, musicDirEc)
                && !m_presetChangePending && !m_themeMusicMergePending) {
                DebugLog::log("[theme-apply] merge theme music: dir=%s album=%s play=%d",
                              safeLogPath(musicDir),
                              album.c_str(),
                              (m_config.themeMusicPlayOnApply && m_config.musicEnabled) ? 1 : 0);
                m_themeMusicMergePending = true;
                m_themeMusicMergePlay = m_config.themeMusicPlayOnApply && m_config.musicEnabled;
                m_themeMusicMergeFirstIndex = m_audio.trackCount();
                m_audioFuture = m_threadPool.submit([this, musicDir, album]() {
                    const size_t n = appendMp3TracksFromDir(m_audio, musicDir, album);
                    DebugLog::log("[audio] merged %zu theme track(s) from %s",
                                  n, safeLogPath(musicDir));
                });
            } else {
                DebugLog::log("[theme-apply] merge theme music skipped: dir=%s pending=%d",
                              safeLogPath(musicDir),
                              (m_presetChangePending || m_themeMusicMergePending) ? 1 : 0);
            }
        } else {
            DebugLog::log("[theme-apply] bundled sound skipped by Options (override off, add off): preset=%s",
                          presetRef.c_str());
        }
    } else {
        DebugLog::log("[theme-apply] bundled sound skipped: preset=%s apply=%d soundPreset=%s",
                      presetRef.c_str(),
                      applyBundledSound ? 1 : 0,
                      safeLogPath(preset->soundPreset));
    }

    // Light shop refresh: update Applied badges / tints without rebuildCurrentTab.
    DebugLog::log("[theme-apply] refreshThemeShopState start: preset=%s (light)", presetRef.c_str());
    refreshThemeShopState(/*rebuildTab=*/false);
    DebugLog::log("[theme-apply] refreshThemeShopState done: preset=%s", presetRef.c_str());
    m_audio.playSfx(Sfx::ThemeToggle);
    DebugLog::log("[theme-apply] complete preset=%s", presetRef.c_str());
}

void WiiUMenuApp::applyAutoThemeConfig() {
    using switchu::services::AutoThemeMode;
    using switchu::services::AutoThemeSettings;

    AutoThemeSettings settings;
    if (m_config.autoThemeMode == "manual")
        settings.mode = AutoThemeMode::ManualHours;
    else if (m_config.autoThemeMode == "geo")
        settings.mode = AutoThemeMode::Geolocation;
    else
        settings.mode = AutoThemeMode::Off;

    settings.dayPreset = m_config.autoThemeDayPreset;
    settings.nightPreset = m_config.autoThemeNightPreset;
    settings.dayStartHour = m_config.autoThemeDayStartHour;
    settings.nightStartHour = m_config.autoThemeNightStartHour;

    m_autoTheme.configure(settings);

    // Seed the geolocation provider with the cached position, if any.
    if (m_config.autoThemeGeoResolved)
        m_autoTheme.setGeoLocation(switchu::services::GeoLocation{
            m_config.autoThemeGeoLat, m_config.autoThemeGeoLon});
    else
        m_autoTheme.setGeoLocation(std::nullopt);

    DebugLog::log("[auto-theme] configured: mode=%s day=%s night=%s dayStart=%d nightStart=%d",
                  m_config.autoThemeMode.c_str(),
                  safeLogPath(settings.dayPreset),
                  safeLogPath(settings.nightPreset),
                  settings.dayStartHour,
                  settings.nightStartHour);
}

void WiiUMenuApp::evaluateAutoTheme(bool force) {
    if (force)
        m_autoTheme.reset();

    const auto snapshot = m_clockService.refresh();
    auto desired = m_autoTheme.evaluate(snapshot);
    if (!desired)
        return;

    if (*desired == m_activePresetName)
        return;

    ThemePreset* preset = findPresetPtr(*desired);
    if (!preset) {
        DebugLog::log("[auto-theme] target preset not found: %s", desired->c_str());
        return;
    }

    DebugLog::log("[auto-theme] switching to preset=%s (hour=%u)",
                  desired->c_str(),
                  static_cast<unsigned>(snapshot.calendar.hour));
    // Auto switching keeps the current sound preset (applyBundledSound=false).
    activateThemePreset(preset, false);
}

void WiiUMenuApp::maybeFetchGeoLocation() {
    if (!m_autoTheme.needsGeoLocation() || m_geoFetchInFlight)
        return;

    m_geoFetchInFlight = true;
    m_geoFetchResult = std::make_shared<GeoFetchResult>();
    DebugLog::log("[auto-theme][geo] starting IP geolocation lookup");
    auto result = m_geoFetchResult;
    m_geoFetchFuture = m_threadPool.submit([result]() {
        try {
            // The Switch has no GPS; approximate the position from the public IP.
            const std::string body = themeshop::http::getText(
                "http://ip-api.com/json/?fields=status,message,lat,lon,city");
            const auto j = nlohmann::json::parse(body);
            if (j.value("status", std::string()) == "success") {
                result->lat = j.value("lat", 0.0);
                result->lon = j.value("lon", 0.0);
                result->city = j.value("city", std::string());
                result->ok = true;
            } else {
                DebugLog::log("[auto-theme][geo] api status not success: %s",
                              j.value("message", std::string("?")).c_str());
            }
        } catch (const std::exception& ex) {
            DebugLog::log("[auto-theme][geo] lookup failed: %s", ex.what());
        } catch (...) {
            DebugLog::log("[auto-theme][geo] lookup failed: unknown error");
        }
    });
}

void WiiUMenuApp::pollGeoLocationFetch() {
    if (!m_geoFetchInFlight || !m_geoFetchFuture.valid())
        return;
    if (m_geoFetchFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    m_geoFetchFuture.get();
    const GeoFetchResult r = m_geoFetchResult ? *m_geoFetchResult : GeoFetchResult{};
    m_geoFetchResult.reset();
    m_geoFetchInFlight = false;

    if (!r.ok) {
        DebugLog::log("[auto-theme][geo] no location resolved (offline or API error)");
        return;
    }

    DebugLog::log("[auto-theme][geo] resolved lat=%.4f lon=%.4f city=%s",
                  r.lat, r.lon, r.city.c_str());

    m_config.autoThemeGeoResolved = true;
    m_config.autoThemeGeoLat = r.lat;
    m_config.autoThemeGeoLon = r.lon;
    m_config.autoThemeGeoCity = r.city;
    m_autoTheme.setGeoLocation(switchu::services::GeoLocation{r.lat, r.lon});

    // Persist the resolved location so future launches skip the network request.
    if (m_configSaveFuture.valid())
        m_configSaveFuture.wait();
    m_configSaveFuture = m_threadPool.submit([cfg = m_config]() { cfg.save(); });

    evaluateAutoTheme(true);
    pushGeoDisplayToScreen();
}

void WiiUMenuApp::pushGeoDisplayToScreen() {
    if (!m_autoThemeScreen)
        return;

    const bool resolved = m_config.autoThemeGeoResolved;
    std::string city, sunrise, sunset;

    if (resolved) {
        city = m_config.autoThemeGeoCity.empty()
            ? fmt::format("{:.2f}, {:.2f}", m_config.autoThemeGeoLat, m_config.autoThemeGeoLon)
            : m_config.autoThemeGeoCity;

        auto& i18n = nxui::I18n::instance();
        const auto snap = m_clockService.refresh();
        const auto sun = m_autoTheme.geoSunTimes(snap);
        if (sun.valid) {
            if (sun.polarDay) {
                sunrise = sunset = i18n.tr("autotheme.geo_midnight_sun", "Midnight sun");
            } else if (sun.polarNight) {
                sunrise = sunset = i18n.tr("autotheme.geo_polar_night", "Polar night");
            } else {
                auto fmtHour = [this](double h) {
                    int hh = static_cast<int>(std::floor(h));
                    int mm = static_cast<int>(std::llround((h - hh) * 60.0));
                    if (mm >= 60) { mm -= 60; ++hh; }
                    hh = ((hh % 24) + 24) % 24;
                    if (m_config.clockUse12Hour) {
                        int h12 = hh % 12;
                        if (h12 == 0) h12 = 12;
                        return fmt::format("{}:{:02d} {}", h12, mm, hh >= 12 ? "PM" : "AM");
                    }
                    return fmt::format("{:02d}:{:02d}", hh, mm);
                };
                sunrise = fmtHour(sun.sunriseLocalHours);
                sunset = fmtHour(sun.sunsetLocalHours);
            }
        }
    }

    m_autoThemeScreen->updateGeoDisplay(resolved, city, sunrise, sunset);
}

void WiiUMenuApp::applyUiLanguage() {
    auto& i18n = nxui::I18n::instance();
    if (m_config.uiLanguageOverride == "auto" || m_config.uiLanguageOverride.empty())
        i18n.setLanguageAuto();
    else
        i18n.setLanguage(m_config.uiLanguageOverride);
    m_accessibility.setVoiceForLanguageTag(i18n.activeLanguageTag());
}

std::string WiiUMenuApp::resolveThemeAssetPath(const ThemePreset& preset, const std::string& rawPath) const {
    if (rawPath.empty())
        return {};
    if (isAbsoluteThemePath(rawPath))
        return rawPath;
    if ((preset.source == ThemePresetSource::InstalledPackage || preset.source == ThemePresetSource::BuiltIn)
        && !preset.installPath.empty())
        return joinPath(preset.installPath, rawPath);
    return joinPath(SD_ASSETS, rawPath);
}

ThemePreset WiiUMenuApp::buildEffectiveThemePreset() {
    ThemePreset effective;
    if (ThemePreset* preset = findPresetPtr(m_activePresetName))
        effective = *preset;

    effective.mode = m_activeMode;
    effective.colors = m_activeColors;
    return effective;
}

void WiiUMenuApp::applyThemeResources(const ThemePreset& preset) {
    auto& gpu = app().gpu();
    auto& ren = app().renderer();
    const bool forceResourceReload = m_forceThemeResourceReload;

    const std::string fallbackFontPath = std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf";
    const std::string regularFontPath = resolveThemeAssetPath(preset, preset.fonts.regularPath);
    const std::string smallFontPath = resolveThemeAssetPath(
        preset,
        !preset.fonts.smallPath.empty() ? preset.fonts.smallPath : preset.fonts.regularPath);
    const std::string themeIconsBase = resolveThemeAssetPath(preset, preset.icons.basePath);
    const std::string backgroundImagePath = resolveThemeAssetPath(preset, preset.background.imagePath);

    // This application supersedes any retry left by the previous theme.
    m_pendingBackgroundImagePath.clear();
    m_backgroundImageRetryFrames = 0;
    m_backgroundImageRetryAttempts = 0;

    // Animated / video wallpaper shares the background slot with the theme
    // image and wins while it plays.
    reloadAnimatedWallpaper();

    const std::string presetRef = preset.id.empty() ? preset.name : preset.id;
    DebugLog::log("[theme-apply] resources start: preset=%s regularFont=%s smallFont=%s iconsBase=%s bgImage=%s",
                  presetRef.c_str(),
                  safeLogPath(regularFontPath),
                  safeLogPath(smallFontPath),
                  safeLogPath(themeIconsBase),
                  safeLogPath(backgroundImagePath));

    bool settingsLayoutNeedsRebuild = false;

    const bool regularFontExists = !regularFontPath.empty() && pathExists(regularFontPath);
    const std::string desiredRegularFontPath = regularFontExists ? regularFontPath : fallbackFontPath;
    const bool regularFontNeedsReload = forceResourceReload || m_loadedRegularFontPath != desiredRegularFontPath;

    const bool smallFontExists = !smallFontPath.empty() && pathExists(smallFontPath);
    const std::string desiredSmallFontPath = smallFontExists ? smallFontPath : fallbackFontPath;
    const bool smallFontNeedsReload = forceResourceReload || m_loadedSmallFontPath != desiredSmallFontPath;

    const std::string defaultGameCardPath = std::string(SD_ASSETS) + "/icons/default/gamecard.png";
    const std::string gameCardPath = !themeIconsBase.empty() ? joinPath(themeIconsBase, "gamecard.png") : std::string();
    const bool gameCardExists = !gameCardPath.empty() && pathExists(gameCardPath);
    const std::string desiredGameCardPath = gameCardExists ? gameCardPath : defaultGameCardPath;
    const bool gameCardNeedsReload = forceResourceReload || m_loadedGameCardPath != desiredGameCardPath;

    const bool imageExists = !backgroundImagePath.empty() && pathExists(backgroundImagePath);
    const bool wantsBackgroundImage = imageExists;
    const bool backgroundImageNeedsReload = forceResourceReload
        || m_loadedBackgroundImagePath != backgroundImagePath
        || m_backgroundImageLoaded != wantsBackgroundImage;

    const bool needsGpuResourceReload = regularFontNeedsReload
        || smallFontNeedsReload
        || gameCardNeedsReload
        || backgroundImageNeedsReload;
    if (needsGpuResourceReload) {
        DebugLog::log("[theme-apply] waiting for GPU idle before reloading theme resources");
        gpu.waitIdle();
    }

    if (regularFontNeedsReload) {
        bool regularFontLoaded = regularFontExists && m_fontNormal.load(gpu, ren, regularFontPath, 24);
        DebugLog::log("[theme-apply] regular font: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(regularFontPath),
                      regularFontExists ? 1 : 0,
                      1,
                      regularFontLoaded ? 1 : 0);
        if (!regularFontLoaded) {
            regularFontLoaded = m_fontNormal.load(gpu, ren, fallbackFontPath, 24);
            DebugLog::log("[theme-apply] regular font fallback: path=%s loaded=%d",
                          fallbackFontPath.c_str(),
                          regularFontLoaded ? 1 : 0);
        }
        if (regularFontLoaded) {
            m_loadedRegularFontPath = desiredRegularFontPath;
            settingsLayoutNeedsRebuild = true;
            applyLockScreenClockFont();
            if (m_themeShop) {
                m_themeShop->setLockClockFontContext(
                    SD_ASSETS,
                    m_loadedRegularFontPath.empty()
                        ? std::string(SD_ASSETS) + "/fonts/DejaVuSans.ttf"
                        : m_loadedRegularFontPath);
            }
        }
    } else {
        DebugLog::log("[theme-apply] regular font: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(regularFontPath),
                      regularFontExists ? 1 : 0,
                      0,
                      1);
    }

    if (smallFontNeedsReload) {
        bool smallFontLoaded = smallFontExists && m_fontSmall.load(gpu, ren, smallFontPath, 18);
        DebugLog::log("[theme-apply] small font: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(smallFontPath),
                      smallFontExists ? 1 : 0,
                      1,
                      smallFontLoaded ? 1 : 0);
        if (!smallFontLoaded) {
            smallFontLoaded = m_fontSmall.load(gpu, ren, fallbackFontPath, 18);
            DebugLog::log("[theme-apply] small font fallback: path=%s loaded=%d",
                          fallbackFontPath.c_str(),
                          smallFontLoaded ? 1 : 0);
        }
        if (smallFontLoaded) {
            m_loadedSmallFontPath = desiredSmallFontPath;
            settingsLayoutNeedsRebuild = true;
        }
    } else {
        DebugLog::log("[theme-apply] small font: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(smallFontPath),
                      smallFontExists ? 1 : 0,
                      0,
                      1);
    }

    if (gameCardNeedsReload) {
        bool gameCardLoaded = gameCardExists && m_gameCardTex.loadFromFile(gpu, ren, gameCardPath);
        DebugLog::log("[theme-apply] gamecard icon: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(gameCardPath),
                      gameCardExists ? 1 : 0,
                      1,
                      gameCardLoaded ? 1 : 0);
        if (!gameCardLoaded) {
            gameCardLoaded = m_gameCardTex.loadFromFile(gpu, ren, defaultGameCardPath);
            DebugLog::log("[theme-apply] gamecard fallback: path=%s loaded=%d",
                          defaultGameCardPath.c_str(),
                          gameCardLoaded ? 1 : 0);
        }
        if (gameCardLoaded)
            m_loadedGameCardPath = desiredGameCardPath;
    } else {
        DebugLog::log("[theme-apply] gamecard icon: path=%s exists=%d reloaded=%d loaded=%d",
                      safeLogPath(gameCardPath),
                      gameCardExists ? 1 : 0,
                      0,
                      1);
    }

    if (m_background) {
        DebugLog::log("[theme-apply] background config start: preset=%s", presetRef.c_str());
        WaraWaraBackground::Config backgroundConfig;
        backgroundConfig.layout = toBackgroundLayout(preset.background.layout);
        backgroundConfig.shapeSet = toBackgroundShapeSet(preset.background.shapeSet);
        backgroundConfig.symmetry = toBackgroundSymmetry(preset.background.symmetry);
        backgroundConfig.shapeCount = preset.background.shapeCount;
        backgroundConfig.gridColumns = preset.background.gridColumns;
        backgroundConfig.gridRows = preset.background.gridRows;
        backgroundConfig.spacingX = preset.background.spacingX;
        backgroundConfig.spacingY = preset.background.spacingY;
        backgroundConfig.sizeMin = preset.background.sizeMin;
        backgroundConfig.sizeMax = preset.background.sizeMax;
        backgroundConfig.speedMin = preset.background.speedMin;
        backgroundConfig.speedMax = preset.background.speedMax;
        backgroundConfig.wobble = preset.background.wobble;
        backgroundConfig.opacity = preset.background.opacity;
        backgroundConfig.rotationSpeed = preset.background.rotationSpeed;
        backgroundConfig.fixedOrientation = preset.background.fixedOrientation;
        backgroundConfig.orientationDegrees = preset.background.orientationDegrees;
        backgroundConfig.cornerRoundness = preset.background.cornerRoundness;
        backgroundConfig.imageOpacity = preset.background.imageOpacity;
        backgroundConfig.imageCover = preset.background.imageCover;
        m_background->setConfig(backgroundConfig);
        if (backgroundImageNeedsReload) {
            const bool imageLoaded = imageExists && m_background->loadImage(gpu, ren, backgroundImagePath);
            DebugLog::log("[theme-apply] background image: path=%s exists=%d reloaded=%d loaded=%d",
                          safeLogPath(backgroundImagePath),
                          imageExists ? 1 : 0,
                          1,
                          imageLoaded ? 1 : 0);
            if (!imageLoaded) {
                if (m_backgroundImageLoaded)
                    m_background->clearImage();
                m_backgroundImageLoaded = false;
                m_loadedBackgroundImagePath.clear();
                DebugLog::log("[theme-apply] background image cleared");
                if (!backgroundImagePath.empty()) {
                    m_pendingBackgroundImagePath = backgroundImagePath;
                    m_backgroundImageRetryFrames = 2;
                    DebugLog::log("[theme-apply] background image retry scheduled: path=%s",
                                  safeLogPath(backgroundImagePath));
                }
            } else {
                m_backgroundImageLoaded = true;
                m_loadedBackgroundImagePath = backgroundImagePath;
            }
        } else {
            DebugLog::log("[theme-apply] background image: path=%s exists=%d reloaded=%d loaded=%d",
                          safeLogPath(backgroundImagePath),
                          imageExists ? 1 : 0,
                          0,
                          m_backgroundImageLoaded ? 1 : 0);
        }
    } else {
        DebugLog::log("[theme-apply] background widget missing");
    }

    if (settingsLayoutNeedsRebuild && m_settings && m_settings->isActive()) {
        m_settings->rebuildCurrentTab();
        DebugLog::log("[theme-apply] settings current tab rebuilt for font change");
    }

    if (needsGpuResourceReload)
        ren.reclaimReleasedTextureSlotsAfterIdle();

    DebugLog::log("[theme-apply] resources done: preset=%s", presetRef.c_str());
}

void WiiUMenuApp::retryPendingBackgroundImage() {
    if (m_pendingBackgroundImagePath.empty() || !m_background)
        return;
    if (m_backgroundImageRetryFrames > 0) {
        --m_backgroundImageRetryFrames;
        return;
    }

    const std::string expectedPath = resolveThemeAssetPath(
        m_effectivePreset, m_effectivePreset.background.imagePath);
    if (expectedPath != m_pendingBackgroundImagePath) {
        DebugLog::log("[theme-apply] background retry cancelled after theme changed");
        m_pendingBackgroundImagePath.clear();
        return;
    }

    ++m_backgroundImageRetryAttempts;
    const bool exists = pathExists(m_pendingBackgroundImagePath);
    bool loaded = false;
    if (exists) {
        app().gpu().waitIdle();
        loaded = m_background->loadImage(app().gpu(), app().renderer(),
                                         m_pendingBackgroundImagePath);
    }
    DebugLog::log("[theme-apply] background retry %d/3: path=%s exists=%d loaded=%d",
                  m_backgroundImageRetryAttempts,
                  safeLogPath(m_pendingBackgroundImagePath),
                  exists ? 1 : 0, loaded ? 1 : 0);

    if (loaded) {
        m_backgroundImageLoaded = true;
        m_loadedBackgroundImagePath = m_pendingBackgroundImagePath;
        m_pendingBackgroundImagePath.clear();
        m_backgroundImageRetryAttempts = 0;
        return;
    }

    if (m_backgroundImageRetryAttempts >= 3) {
        DebugLog::log("[theme-apply] background retry exhausted: path=%s",
                      safeLogPath(m_pendingBackgroundImagePath));
        m_pendingBackgroundImagePath.clear();
        return;
    }
    m_backgroundImageRetryFrames = 2 << (m_backgroundImageRetryAttempts - 1);
}

void WiiUMenuApp::applyHomeChrome() {
    const nxui::Theme& chrome = m_homeChrome;

    if (m_clock) {
        m_clock->setBaseColor(chrome.panelBase);
        m_clock->setBorderColor(chrome.panelBorder);
        m_clock->setHighlightColor(chrome.panelHighlight);
        m_clock->setTextColor(chrome.textPrimary);
        m_clock->setSecondaryTextColor(chrome.textSecondary);
    }

    if (m_battery) {
        m_battery->setBaseColor(chrome.panelBase);
        m_battery->setBorderColor(chrome.panelBorder);
        m_battery->setHighlightColor(chrome.panelHighlight);
        m_battery->setTextColor(chrome.textPrimary);
    }

    if (m_titlePill) {
        // The opacity colour reaches the game name too: the text and its
        // accent bar blend toward it while keeping enough of textPrimary to
        // stay legible on both light and dark themes.
        nxui::Color nameColor = chrome.textPrimary;
        nxui::Color accentColor = chrome.cursorNormal;
        auto& ren = app().renderer();
        if (ren.opacityTintEnabled()) {
            const nxui::Color& tint = ren.opacityTint();
            const auto mix = [&tint](const nxui::Color& c, float amount) {
                return nxui::Color(c.r + (tint.r - c.r) * amount,
                                   c.g + (tint.g - c.g) * amount,
                                   c.b + (tint.b - c.b) * amount,
                                   c.a);
            };
            nameColor = mix(nameColor, 0.72f);
            accentColor = mix(accentColor, 0.85f);
        }
        m_titlePill->setBaseColor(chrome.panelBase.withAlpha(
            chrome.mode == nxui::ThemeMode::Dark ? 0.90f : 0.86f));
        m_titlePill->setBorderColor(chrome.panelBorder.withAlpha(0.84f));
        m_titlePill->setHighlightColor(chrome.panelHighlight.withAlpha(0.42f));
        m_titlePill->setTextColor(nameColor);
        m_titlePill->setAccentColor(accentColor);
    }

    if (m_folderHeader) {
        m_folderHeader->setBaseColor(chrome.panelBase);
        m_folderHeader->setBorderColor(chrome.panelBorder);
        m_folderHeader->setHighlightColor(chrome.panelHighlight);
    }
    if (m_folderHeaderLabel)
        m_folderHeaderLabel->setTextColor(chrome.textPrimary);
    if (m_profileLock)
        m_profileLock->setTheme(&m_homeChrome);
    if (m_layoutFlip)
        m_layoutFlip->setTheme(&m_homeChrome);

    m_sidebar.applyTheme(m_homeChrome);
    syncColourIconContrast();
}

void WiiUMenuApp::updateHeroAwareChrome(bool force) {
    HeroChromeAdapt desired = HeroChromeAdapt::Off;
    // Full-opacity heroes are single-row only; dimmed grid heroes leave chrome alone.
    const bool fullHeroContext =
        presentationLayoutMode() == AppLayoutMode::DynamicLine;

    if (fullHeroContext && m_steamGridDbBackdrop
        && m_steamGridDbBackdrop->hasDisplayedHero()) {
        const float luma = m_steamGridDbBackdrop->displayedHeroLuminance();
        if (luma >= 0.f) {
            // Hysteresis avoids flicker when focus skims near-threshold heroes.
            const float darkEnter = 0.42f;
            const float darkExit = 0.50f;
            const float lightEnter = 0.58f;
            const float lightExit = 0.50f;
            if (m_theme.mode == nxui::ThemeMode::Light) {
                const float limit = (m_heroChromeAdapt == HeroChromeAdapt::ForceDark)
                    ? darkExit : darkEnter;
                if (luma < limit)
                    desired = HeroChromeAdapt::ForceDark;
            } else if (m_theme.mode == nxui::ThemeMode::Dark) {
                const float limit = (m_heroChromeAdapt == HeroChromeAdapt::ForceLight)
                    ? lightExit : lightEnter;
                if (luma > limit)
                    desired = HeroChromeAdapt::ForceLight;
            }
        }
    }

    // Colour-icon contrast must track every hero change even when chrome mode
    // stays put (otherwise adapt never releases after the first hero).
    if (!force && desired == m_heroChromeAdapt) {
        syncColourIconContrast();
        return;
    }

    m_heroChromeAdapt = desired;
    if (desired == HeroChromeAdapt::ForceDark) {
        m_homeChrome = nxui::Theme::dark();
    } else if (desired == HeroChromeAdapt::ForceLight) {
        m_homeChrome = nxui::Theme::light();
    } else {
        m_homeChrome = m_theme;
    }
    // Keep the user's cursor brand while chrome contrast flips.
    m_homeChrome.cursorNormal = m_theme.cursorNormal;
    m_homeChrome.cursorGlow = m_theme.cursorGlow;
    // Ring curvature is the tile curvature plus the focus inset, so a square
    // tile gets a square ring and a pill tile gets a pill ring automatically.
    m_homeChrome.cursorCornerRadius = effectiveIconCornerRadius() + 4.f;
    m_homeChrome.cursorBorderWidth = m_theme.cursorBorderWidth;
    applyHomeChrome();
}

void WiiUMenuApp::syncHeroBackdropPresentation() {
    if (!m_steamGridDbBackdrop)
        return;
    m_steamGridDbBackdrop->setLayoutMode(presentationLayoutMode());
    updateHeroAwareChrome(true);
}

void WiiUMenuApp::applyAccentColor() {
    const auto same = [](const nxui::Color& a, const nxui::Color& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    // Undo our previous tint, but only while the theme still carries it: a
    // freshly loaded theme keeps its own accent as the new baseline.
    if (m_accentBaseValid && same(m_theme.cursorNormal, m_appliedAccent)) {
        m_theme.cursorNormal = m_accentBase;
        m_theme.cursorGlow = m_accentBaseGlow;
    }
    m_accentBase = m_theme.cursorNormal;
    m_accentBaseGlow = m_theme.cursorGlow;
    m_accentBaseValid = true;

    const int idx = std::clamp(m_config.highlightTint, 0, 10);
    if (idx == 10) {
        const nxui::Color first = switchu::ui::accent::colorFor(
            std::clamp(m_config.highlightGradientA, 1, 9));
        const nxui::Color second = switchu::ui::accent::colorFor(
            std::clamp(m_config.highlightGradientB, 1, 9));
        m_theme.cursorNormal = first;
        m_theme.cursorGlow = second.withAlpha(0.14f);
        m_appliedAccent = first;
    } else if (switchu::ui::accent::valid(idx)) {
        const nxui::Color accent = switchu::ui::accent::colorFor(idx);
        m_theme.cursorNormal = accent;
        m_theme.cursorGlow = accent.withAlpha(0.14f);
        m_appliedAccent = accent;
    } else {
        m_appliedAccent = m_accentBase;
    }

    m_homeChrome.cursorNormal = m_theme.cursorNormal;
    m_homeChrome.cursorGlow = m_theme.cursorGlow;
    applyCursorHighlight();
}

void WiiUMenuApp::applyCursorHighlight() {
    const bool gradient = std::clamp(m_config.highlightTint, 0, 10) == 10;
    const nxui::Color first = switchu::ui::accent::colorFor(
        std::clamp(m_config.highlightGradientA, 1, 9));
    const nxui::Color second = switchu::ui::accent::colorFor(
        std::clamp(m_config.highlightGradientB, 1, 9));
    const float thickness = static_cast<float>(
        std::clamp(m_config.highlightThicknessPx, 1, 8));
    // Settings, Game Options, Theme Shop and the overlays share this style, so
    // one accent change reaches every focus ring on the next frame.
    switchu::ui::highlight::configure(gradient, first, second, thickness);
    if (!m_cursor)
        return;
    m_cursor->setColor(gradient ? first : m_theme.cursorNormal);
    m_cursor->setGradient(gradient, first, second);
    m_cursor->setBorderWidth(thickness);
}

void WiiUMenuApp::applyTheme() {
    DebugLog::log("[theme-apply] widget recolor start");
    applyAccentColor();
    m_homeChrome = m_theme;
    m_heroChromeAdapt = HeroChromeAdapt::Off;
    m_background->setAccentColor(m_theme.backgroundAccent);
    m_background->setSecondaryColor(m_theme.background);
    m_background->setShapeColor(m_theme.shapeColor);
    m_background->setDrift(m_config.backgroundDrift, m_config.backgroundDriftSpeed);
    m_background->setShapesEnabled(m_config.backgroundShapes);
    DebugLog::log("[theme-apply] widget recolor background done");

    for (auto& icon : m_grid->allIcons()) {
        icon->setBaseColor(m_theme.iconDefault);
        icon->setBorderColor(m_theme.panelBorder);
        icon->setHighlightColor(m_theme.panelHighlight);
        icon->setCornerRadius(effectiveIconCornerRadius());
        icon->setShadow(static_cast<float>(m_config.iconShadowOffset), static_cast<float>(m_config.iconShadowAlpha) / 100.f);
        icon->setVignette(static_cast<float>(std::clamp(m_config.iconVignette, 0, 100)) / 100.f);
        icon->setHighlight(static_cast<float>(std::clamp(m_config.iconHighlight, 0, 100)) / 100.f);
        icon->setLoadingColor(m_theme.cursorNormal);
        icon->setThemeMode(m_theme.mode);
    }
    DebugLog::log("[theme-apply] widget recolor grid icons done");

    m_cursor->setCornerRadius(effectiveIconCornerRadius() + 4.f);
    applyCursorHighlight();
    DebugLog::log("[theme-apply] widget recolor cursors done");

    applyHomeChrome();
    if (m_profileCarousel)
        m_profileCarousel->setTheme(&m_theme);
    DebugLog::log("[theme-apply] widget recolor HUD done");

    m_userSelect->setTheme(&m_theme);
    m_userSelect->cursor().setColor(m_theme.cursorNormal);

    if (m_dialog) {
        m_dialog->setTheme(&m_theme);
        m_dialog->setBaseColor(m_theme.panelBase);
        m_dialog->setBorderColor(m_theme.panelBorder);
        m_dialog->setHighlightColor(m_theme.panelHighlight);
        m_dialog->cursor().setColor(m_theme.cursorNormal);
    }
    if (m_contextMenu)
        m_contextMenu->setTheme(&m_theme);
    DebugLog::log("[theme-apply] widget recolor overlays done");

    if (m_settings)
        m_settings->setTheme(&m_theme);
    if (m_themeShop)
        m_themeShop->setTheme(&m_theme);
    if (m_gameOptions)
        m_gameOptions->setTheme(&m_theme);
    if (m_steamGridDbPicker)
        m_steamGridDbPicker->setTheme(&m_theme);
    if (m_folderOptions)
        m_folderOptions->setTheme(&m_theme);
    if (m_controllerTest)
        m_controllerTest->setTheme(&m_theme);
    if (m_quickSettings)
        m_quickSettings->setTheme(&m_theme);
    if (m_textEntry)
        m_textEntry->setTheme(&m_theme);

    // Re-evaluate hero contrast after a theme change (may re-apply chrome).
    updateHeroAwareChrome(true);
    DebugLog::log("[theme-apply] widget recolor complete");
}

void WiiUMenuApp::rebuildThemeFromColors() {
    DebugLog::log("[theme-apply] rebuild start: activePreset=%s", m_activePresetName.c_str());
    m_effectivePreset = buildEffectiveThemePreset();
    DebugLog::log("[theme-apply] effective preset resolved: id=%s name=%s source=%s installPath=%s",
                  safeLogPath(m_effectivePreset.id),
                  m_effectivePreset.name.c_str(),
                  themeSourceName(m_effectivePreset.source),
                  safeLogPath(m_effectivePreset.installPath));
    m_theme = m_effectivePreset.toTheme();
    DebugLog::log("[theme-apply] theme object rebuilt");

    DebugLog::log("[theme-apply] applyThemeResources start: preset=%s",
                  safeLogPath(m_effectivePreset.id.empty() ? m_effectivePreset.name : m_effectivePreset.id));
    applyThemeResources(m_effectivePreset);
    DebugLog::log("[theme-apply] applyThemeResources done: preset=%s",
                  safeLogPath(m_effectivePreset.id.empty() ? m_effectivePreset.name : m_effectivePreset.id));

    DebugLog::log("[theme-apply] sidebar.reloadAssets start: pack=%s",
                  m_config.iconPack.empty() ? switchu::icons::kDefaultPackId
                                            : m_config.iconPack.c_str());
    reloadSidebarIcons();
    DebugLog::log("[theme-apply] sidebar.reloadAssets done");

    DebugLog::log("[theme-apply] applyTheme start");
    applyTheme();
    m_forceThemeResourceReload = false;
    DebugLog::log("[theme-apply] rebuild complete: activePreset=%s", m_activePresetName.c_str());
}

ThemePreset* WiiUMenuApp::findPresetPtr(const std::string& name) {
    for (auto& p : m_allPresets)
        if (p.id == name || p.name == name) return &p;
    return nullptr;
}

void WiiUMenuApp::deletePreset(const std::string& presetId) {
    ThemePreset* preset = findPresetPtr(presetId);
    if (!preset) return;

    ThemePreset* activePreset = findPresetPtr(m_activePresetName);
    std::string activeId = activePreset ? (activePreset->id.empty() ? activePreset->name : activePreset->id)
                                        : m_activePresetName;
    std::string idToDelete = preset->id.empty() ? preset->name : preset->id;
    std::string nameToDelete = preset->name;
    std::string installPath = preset->installPath;
    std::string soundPreset = preset->soundPreset;
    ThemePresetSource source = preset->source;
    bool deletingActive = (activeId == idToDelete);

    m_allPresets.erase(
        std::remove_if(m_allPresets.begin(), m_allPresets.end(),
            [&](const ThemePreset& p) { return p.id == idToDelete || p.name == nameToDelete; }),
        m_allPresets.end());

    auto userPresets = ThemePreset::loadUserPresets();
    userPresets.erase(
        std::remove_if(userPresets.begin(), userPresets.end(),
            [&](const ThemePreset& p) { return p.id == idToDelete || p.name == nameToDelete; }),
        userPresets.end());
    ThemePreset::saveUserPresets(userPresets);

    if (!soundPreset.empty() && m_config.soundPreset == soundPreset) {
        m_config.soundPreset = "wiiu";
        changeSoundPreset(m_config.soundPreset);
    }

    {
        std::string folder = idToDelete;
        if (!installPath.empty()) {
            const auto pos = installPath.find_last_of("/\\");
            folder = (pos == std::string::npos) ? installPath : installPath.substr(pos + 1);
        }
        // package:slug ids store the folder under installPath; strip prefix for pack ids.
        if (folder.rfind("package:", 0) == 0)
            folder = folder.substr(8);
        const std::string themePackId = switchu::icons::makeThemePackId(folder);
        const std::string legacyPackId = switchu::icons::makeThemePackId(idToDelete);
        std::string companionPackId;
        if (const ThemePreset* dying = findPresetPtr(idToDelete))
            companionPackId = dying->icons.packId;
        // Snapshot before preset list changes: folder-named extract is orphaned
        // when the theme goes away (unless it is a builtin like atmos).
        if (!folder.empty() && !isBuiltinIconPack(folder)) {
            const std::string orphan =
                switchu::icons::IconPackInstaller::destinationRootFor(folder);
            if (pathExists(orphan)) {
                switchu::icons::IconPackInstaller::remove(folder);
                DebugLog::log("[icon-pack] removed theme orphan pack=%s", folder.c_str());
            }
        }
        if ((!themePackId.empty() && m_config.iconPack == themePackId)
            || (!legacyPackId.empty() && m_config.iconPack == legacyPackId)
            || (!folder.empty() && m_config.iconPack == folder)
            || (switchu::icons::isThemePackId(m_config.iconPack)
                && switchu::icons::themeFolderFromPackId(m_config.iconPack) == folder)) {
            m_config.iconPack = switchu::icons::kDefaultPackId;
            m_config.save();
            DebugLog::log("[icon-pack] theme removed; pack reset to default");
        }
        (void)companionPackId; // builtins referenced via packId are kept
    }

    // Leave the package BEFORE deleting files — otherwise open fonts/textures can
    // lock the folder, remove_all fails, and Community still thinks it's installed.
    if (deletingActive) {
        m_activePresetName = defaultThemeRef();
        m_config.themePreset = defaultThemeRef();

        ThemePreset* fallback = findPresetPtr(defaultThemeRef());
        if (!fallback)
            fallback = findPresetPtr("Default Light");
        if (fallback) {
            m_activePresetName = fallback->id.empty() ? fallback->name : fallback->id;
            m_config.themePreset = m_activePresetName;
            m_activeColors = fallback->colors;
            m_activeMode = fallback->mode;
            m_forceThemeResourceReload = true;
        }
        rebuildThemeFromColors();
    }

    if (source == ThemePresetSource::InstalledPackage && !installPath.empty()) {
        if (m_themeDeleteFuture.valid()) {
            m_themeDeleteFuture.wait();
            m_themeDeleteFuture = {};
        }
        // Sync remove so Installed → Community reinstall does not race an orphan folder.
        const bool removed = removeDirectoryRecursive(installPath);
        DebugLog::log("[theme-delete] removal %s: %s",
                      removed ? "complete" : "failed", safeLogPath(installPath));
        if (!removed || pathExists(installPath)) {
            // One more pass after resources have switched away.
            const bool retry = removeDirectoryRecursive(installPath);
            DebugLog::log("[theme-delete] retry %s: %s",
                          retry ? "complete" : "failed", safeLogPath(installPath));
        }
    }

    refreshThemeShopState();
    // Removing from the product sheet used to leave detail open on the next
    // theme (selection falls through after the list rebuild). Return to shelf.
    if (m_themeShop && m_themeShop->detailOpen())
        m_themeShop->closeDetailView();
    m_audio.playSfx(Sfx::ConfirmPositive);
}

void WiiUMenuApp::startSoftwareDeletion(uint64_t titleId, const std::string& title,
                                        bool closeGameOptionsOnSuccess) {
    if (m_softwareDeleteFuture.valid()
        && m_softwareDeleteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    auto& i18n = nxui::I18n::instance();
    m_softwareDeleteTitleId = titleId;
    m_softwareDeleteTitle = title;
    m_softwareDeleteClosesGameOptions = closeGameOptionsOnSuccess;
    m_dialogReturnFocus = closeGameOptionsOnSuccess
        ? static_cast<nxui::Widget*>(m_gameOptions.get())
        : static_cast<nxui::Widget*>(m_settings.get());
    if (m_progressDialog) {
        m_progressDialog->show(
            i18n.tr("game.uninstall_progress_title", "Deleting software"),
            i18n.tr("game.uninstall_progress", "Please wait. Do not turn off the console.")
                + std::string("\n") + title,
            -1.f);
        focusManager().setFocus(m_progressDialog.get());
    }

    DebugLog::log("[software-delete] queued title=%016llX",
                  static_cast<unsigned long long>(titleId));
    m_softwareDeleteResult = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    m_softwareDeleteFuture = m_threadPool.submit([this, titleId]() {
        m_softwareDeleteResult = nsDeleteApplicationCompletely(titleId);
    });
}

void WiiUMenuApp::syncSoftwareDeletion() {
    if (!m_softwareDeleteFuture.valid()
        || m_softwareDeleteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    Result rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    try {
        m_softwareDeleteFuture.get();
        rc = m_softwareDeleteResult;
    } catch (const std::exception& ex) {
        DebugLog::log("[software-delete] worker exception: %s", ex.what());
    } catch (...) {
        DebugLog::log("[software-delete] worker exception: unknown");
    }

    if (m_progressDialog)
        m_progressDialog->hide();

    auto& i18n = nxui::I18n::instance();
    if (R_SUCCEEDED(rc)) {
        DebugLog::log("[software-delete] complete: %s", m_softwareDeleteTitle.c_str());
        // Drop NACP/icon cache so a reinstall with a new icon is not stuck
        // on the previous control_cache entry.
        if (m_softwareDeleteTitleId != 0)
            switchu::control_cache::remove(m_softwareDeleteTitleId);
        m_audio.playSfx(Sfx::ConfirmPositive);
        if (m_softwareDeleteClosesGameOptions && m_gameOptions) {
            m_gameOptions->hide();
        } else if (m_settings) {
            m_settings->requestToast(
                i18n.tr("settings.storage.uninstall_success", "Uninstalled successfully."), 2.8f);
            m_settings->rebuildCurrentTab();
            focusManager().setFocus(m_settings.get());
        }
        m_refreshQueued = true;
        m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 3);
    } else {
        DebugLog::log("[software-delete] failed rc=0x%08X module=%u description=%u",
                      rc, R_MODULE(rc), R_DESCRIPTION(rc));
        const std::string details = fmt::format(
            "{}\nResult: 0x{:08X} (module {}, description {})",
            i18n.tr("settings.storage.uninstall_failed", "Failed to uninstall the selected title."),
            rc, R_MODULE(rc), R_DESCRIPTION(rc));
        if (m_dialog) {
            m_dialog->show(
                i18n.tr("settings.storage.uninstall_failed_title", "Uninstall Failed"),
                details,
                {{i18n.tr("button.ok", "OK"), []() {}, true}});
            focusManager().setFocus(m_dialog.get());
        }
    }

    m_softwareDeleteTitleId = 0;
    m_softwareDeleteTitle.clear();
    m_softwareDeleteClosesGameOptions = false;
}

void WiiUMenuApp::rematchIconPackTintsToTheme(bool syncSidebar) {
    float accentH = m_activeColors.accentH;
    float accentS = m_activeColors.accentS;
    float accentL = m_activeColors.accentL;
    if (accentS < 0.05f && accentL < 0.08f
        && (m_activeColors.cursorS >= 0.05f || m_activeColors.cursorL >= 0.08f)) {
        accentH = m_activeColors.cursorH;
        accentS = std::max(0.f, m_activeColors.cursorS);
        accentL = std::max(0.f, m_activeColors.cursorL);
    }
    if (m_themeShop)
        m_themeShop->setThemeMatchAccent(accentH, accentS, accentL);

    // Rematch every pack that already has Match on. Do NOT move/clear Match
    // when a theme switches the active icon pack (e.g. iiSU) - Atmos keeps Auto.
    bool any = false;
    for (auto& [packId, tint] : m_config.iconPackTints) {
        if (!tint.matchTheme)
            continue;
        float meanH = 0.f, meanS = 0.45f, meanL = 0.55f;
        const std::string sample =
            switchu::icons::resolveGlyphPath(SD_ASSETS, packId, "album.png");
        if (!sample.empty())
            switchu::icons::probeIconMeanHsl(sample, meanH, meanS, meanL);
        const auto pivots = switchu::icons::computeMatchThemePivots(
            meanH, meanS, meanL, accentH, accentS, accentL);
        tint.enabled = true;
        tint.matchTheme = true;
        if (tint.grayscale) {
            // Absolute accent H/S for grayscale colorize; keep Light pivot.
            tint.hue = accentH - std::floor(accentH);
            tint.sat = accentS < 0.08f ? 0.45f : accentS;
            tint.light = pivots.light;
        } else {
            tint.hue = pivots.hue;
            tint.sat = pivots.sat;
            tint.light = pivots.light;
        }
        any = true;
    }
    if (!any)
        return;

    m_config.save();
    if (syncSidebar)
        syncColourIconContrast();
    // Push rematched tints into Theme Shop before invalidating so hslKeys match.
    if (m_themeShop) {
        std::unordered_map<std::string, ThemeShopScreen::IconPackTintState> tints;
        for (const auto& [packId, tint] : m_config.iconPackTints) {
            ThemeShopScreen::IconPackTintState state;
            state.enabled = tint.enabled;
            state.flatten = tint.flatten;
            state.grayscale = tint.grayscale;
            state.matchTheme = tint.matchTheme;
            state.hue = tint.hue;
            state.sat = tint.sat;
            state.light = tint.light;
            tints[packId] = state;
        }
        m_themeShop->setIconPackTintState(tints);
        m_themeShop->setThemeMatchAccent(accentH, accentS, accentL);
        m_themeShop->invalidateIconPackPreviews();
    }
}

void WiiUMenuApp::syncColourIconContrast() {
    // Single-row + hero: colour icons follow HOME chrome (same light/dark flip
    // as mono glyphs). No hero / grid: authored colour, optionally HSL-shifted
    // when custom tint is on. Theme > Options arms the feature.
    const bool fullHero =
        presentationLayoutMode() == AppLayoutMode::DynamicLine
        && m_steamGridDbBackdrop
        && m_steamGridDbBackdrop->hasDisplayedHero();
    const bool adaptColour = m_config.iconContrastAdapt && fullHero;

    const std::string& activePackId = resolvedIconPackId();
    const AppConfig::IconPackTint packTint = m_config.iconPackTintFor(activePackId);
    switchu::icons::HslAdjust hsl;
    hsl.enabled = packTint.enabled;
    hsl.flatten = packTint.flatten;
    hsl.grayscale = packTint.grayscale;
    hsl.hue = std::clamp(packTint.hue, 0.f, 1.f);
    hsl.sat = std::clamp(packTint.sat, 0.f, 1.f);
    hsl.light = std::clamp(packTint.light, 0.f, 1.f);

    const auto& prev = m_sidebar.iconHslAdjust();
    const bool hslChanged =
        prev.enabled != hsl.enabled
        || prev.flatten != hsl.flatten
        || prev.grayscale != hsl.grayscale
        || std::fabs(prev.hue - hsl.hue) > 0.0005f
        || std::fabs(prev.sat - hsl.sat) > 0.0005f
        || std::fabs(prev.light - hsl.light) > 0.0005f;

    m_sidebar.setIconHslAdjust(hsl);
    if (m_layoutFlip)
        m_layoutFlip->setIconHslAdjust(hsl);
    m_sidebar.setColourIconContrast(adaptColour);
    if (m_layoutFlip)
        m_layoutFlip->setColourIconContrast(adaptColour);

    if (hslChanged) {
        // Rebake colour glyphs (and Change View) with the new HSL adjust.
        // Do not log the unchanged path — syncColourIconContrast runs every
        // frame from updateHeroAwareChrome; SD log spam soft-locks the console.
        reloadSidebarIcons();
        m_sidebar.setColourIconContrast(adaptColour);
        if (m_layoutFlip)
            m_layoutFlip->setColourIconContrast(adaptColour);
    }
}

void WiiUMenuApp::applyMotionStyle() {
    const int style = std::clamp(m_config.motionStyle, 0, 2);
    m_config.motionStyle = style;
    if (m_grid)
        m_grid->setMotionStyle(style);
    if (m_cursor)
        m_cursor->setMotionStyle(style);
}

void WiiUMenuApp::applyIconPresentation() {
    // Every icon push also re-asserts the motion style, so a style picked in
    // Settings survives a reflow that rebuilds the grid widget tree.
    applyMotionStyle();
    if (!m_grid)
        return;
    m_grid->setIconScale(
        static_cast<float>(std::clamp(m_config.iconScale, 70, 130)) / 100.f);
    m_grid->setCarouselShrink(
        static_cast<float>(std::clamp(m_config.carouselShrink, 0, 100)) / 100.f);
    m_grid->setCarouselFade(
        static_cast<float>(std::clamp(m_config.carouselFade, 0, 100)) / 100.f);
    m_grid->setCarouselScrollSpeed(
        std::clamp(m_config.carouselScrollSpeed, 0, 100));
    if (m_titlePill)
        m_titlePill->setNudgeX(12.8f * static_cast<float>(
            std::clamp(m_config.titlePillNudgeX, -25, 25)));
    const GridLayoutMetrics metrics = computeGridLayoutMetrics();
    // The grid nudge is a percentage of one cell, so it stays proportional when
    // the grid density changes. The carousel has no cell of its own, so its steps
    // are screen-relative: ~6 px each, about +/-150 px at the ends.
    // Snap applied offsets to whole 2px steps: the slider still has fine
    // granularity, but accidental 1px drift cannot persist in the layout.
    auto snap2 = [](float v) { return std::round(v * 0.5f) * 2.f; };
    m_grid->setGridNudgeX(snap2(metrics.cellW
        * (static_cast<float>(m_config.gridNudgeX) / 100.f)));
    // The dock has its own carousel position so the focused tile can sit
    // differently there without moving the handheld layout. The normal
    // carousel offsets remain the restore point when undocked.
    const bool dockedCarousel = m_dockedNow && m_config.dockedSizeOverrides;
    const int carouselX = std::clamp(
        dockedCarousel ? m_config.dockedCarouselX : m_config.lineNudgeX, -25, 25);
    const int carouselY = std::clamp(
        dockedCarousel ? m_config.dockedCarouselY : m_config.lineNudgeY, -25, 25);
    m_grid->setLineNudgeY(snap2(6.f * static_cast<float>(carouselY)));
    // Slider ends carry the focused tile to either screen edge, not just
    // 150 px off centre.
    m_grid->setLineNudgeX(snap2((1280.f * 0.44f)
        * (static_cast<float>(carouselX) / 25.f)));
    const float radius = effectiveIconCornerRadius();
    const float vignette = static_cast<float>(
        std::clamp(m_config.iconVignette, 0, 100)) / 100.f;
    const float highlight = static_cast<float>(
        std::clamp(m_config.iconHighlight, 0, 100)) / 100.f;
    for (const auto& icon : m_grid->allIcons()) {
        if (!icon)
            continue;
        icon->setCornerRadius(radius);
        icon->setVignette(vignette);
        icon->setHighlight(highlight);
    }
    m_grid->invalidateLineLayout();
    // Nudges and scale change existing tile rects; without this the grid keeps
    // the old positions until the next page change.
    m_grid->refreshLayout();
}

void WiiUMenuApp::rememberLoadedWallpaper(const std::string& path) {
    m_loadedWallpaperPath = path;
    if (path.empty() || m_config.wallpaperLastUsedPath == path)
        return;
    m_config.wallpaperLastUsedPath = path;
    if (!m_config.save())
        DebugLog::log("[wallpaper] could not persist last used path '%s'", path.c_str());
}

bool WiiUMenuApp::reloadAnimatedWallpaper(bool force) {
    if (!m_background)
        return false;
    // The folder is part of the feature, not just a scan side effect: create it
    // on boot so users can drop PNG/JPEG files in before touching Settings.
    std::error_code dirEc;
    std::filesystem::create_directories("sdmc:/config/SwitchU/wallpapers", dirEc);
    const std::string path = m_config.wallpaperPath;
    auto isStillImage = [](const std::string& candidate) {
        std::string ext = std::filesystem::path(candidate).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
    };
    // A still image is a wallpaper even when the animation toggle is off; the
    // toggle only governs animated formats.
    auto canPlay = [&](const std::string& candidate) {
        return !candidate.empty()
            && (m_config.wallpaperAnimated || isStillImage(candidate));
    };
    if (!force && canPlay(path) && path == m_loadedWallpaperPath
        && m_background->hasAnimatedWallpaper())
        return true;
    if (!canPlay(path)) {
        m_background->clearAnimatedWallpaper();
        m_loadedWallpaperPath.clear();
        // The wallpaper owned the background slot; bring the theme image back.
        m_background->restoreImage(app().gpu(), app().renderer());
        DebugLog::log("[wallpaper] disabled (path='%s' animated=%d)", path.c_str(),
                      m_config.wallpaperAnimated ? 1 : 0);
        return false;
    }
    const std::string previousPath = m_loadedWallpaperPath;
    if (m_background->loadAnimatedWallpaper(app().gpu(), app().renderer(), path,
                                             m_config.wallpaperFps)) {
        rememberLoadedWallpaper(path);
        DebugLog::log("[wallpaper] playing '%s'", path.c_str());
        return true;
    }
    if (m_background->hasAnimatedWallpaper()) {
        // The new decode failed: keep the previous animation on screen and
        // its path so the next attempt/return can still identify it.
        m_loadedWallpaperPath = previousPath;
        DebugLog::log("[wallpaper] '%s' could not be played; keeping last used wallpaper",
                      path.c_str());
        return false;
    }
    // No in-memory animation survived; try the last wallpaper that decoded
    // successfully, including one persisted by an earlier HOME process.
    const std::string fallback = m_config.wallpaperLastUsedPath;
    if (fallback != path && canPlay(fallback)
        && m_background->loadAnimatedWallpaper(app().gpu(), app().renderer(), fallback,
                                                m_config.wallpaperFps)) {
        rememberLoadedWallpaper(fallback);
        DebugLog::log("[wallpaper] '%s' failed; restored last used '%s'",
                      path.c_str(), fallback.c_str());
        return true;
    }
    m_loadedWallpaperPath.clear();
    m_background->restoreImage(app().gpu(), app().renderer());
    if (fallback != path && canPlay(fallback))
        DebugLog::log("[wallpaper] last used '%s' could not be restored", fallback.c_str());
    DebugLog::log("[wallpaper] '%s' could not be played", path.c_str());
    return false;
}

void WiiUMenuApp::cycleWallpaper() {
    namespace fs = std::filesystem;
    std::vector<std::string> options;
    std::error_code ec;
    const fs::path dir = "sdmc:/config/SwitchU/wallpapers";
    fs::create_directories(dir, ec);
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc))
            continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp"
            || ext == ".gif" || ext == ".mp4" || ext == ".mov"
            || ext == ".m4v")
            options.push_back(entry.path().string());
    }
    std::sort(options.begin(), options.end());

    std::string next;
    if (!options.empty()) {
        if (m_config.wallpaperPath.empty()) {
            next = options.front();
        } else {
            const auto found = std::find(options.begin(), options.end(), m_config.wallpaperPath);
            auto following = found;
            if (found == options.end() || (++following) == options.end())
                next = options.front();
            else
                next = *following;
        }
    }

    m_config.wallpaperPath = next;
    m_config.save();
    reloadAnimatedWallpaper();
    if (!m_settings)
        return;
    m_settings->setWallpaperState(m_config.wallpaperPath, m_config.wallpaperAnimated,
                                  m_config.wallpaperFps);
    if (next.empty()) {
        m_settings->requestToast(nxui::I18n::instance().tr(
            "settings.appearance.wallpaper_none",
            "No wallpaper files in sdmc:/config/SwitchU/wallpapers - using the theme image."));
    } else {
        const std::size_t slash = next.find_last_of('/');
        m_settings->requestToast(nxui::I18n::instance().tr(
            "settings.appearance.wallpaper_applied", "Wallpaper: ") +
            (slash == std::string::npos ? next : next.substr(slash + 1)));
    }
}

void WiiUMenuApp::openWallpaperPicker() {
    if (!m_wallpaperPicker)
        return;
    if (m_overlayLayer) {
        m_overlayLayer->removeChild(m_wallpaperPicker.get());
        m_overlayLayer->addChild(m_wallpaperPicker);
    }
    m_wallpaperPicker->show(m_config.wallpaperPath);
    focusManager().setFocus(m_wallpaperPicker.get());
}

void WiiUMenuApp::applySidebarPresentation() {
    // Shortcuts are chrome: they follow the shared UI radius and the
    // rounded/square switch so they stop looking like a second visual language
    // next to the tiles, while keeping their own size setting.
    const float radiusPct = static_cast<float>(std::clamp(m_config.iconCornerPct, 0, 100)) / 100.f;
    const float radius = (m_config.iconCorners == 1)
        ? 0.f
        : m_theme.iconCornerRadius * radiusPct * uiRadiusScale();
    // Square corners read better with a thinner rim; rounded keeps the glass
    // frame weight the rest of the menu uses.
    const float border = (m_config.iconCorners == 1) ? 1.6f : 2.2f;
    // Before the chrome pass: the plate decides whether the buttons paint
    // their own pills or go transparent.
    m_sidebar.setStripPlate(m_config.shortcutStrip);
    m_sidebar.setStripGradient(
        m_config.shortcutGradient,
        switchu::ui::accent::colorFor(std::clamp(m_config.shortcutGradientA, 1, 9)),
        switchu::ui::accent::colorFor(std::clamp(m_config.shortcutGradientB, 1, 9)));
    m_sidebar.setStripFlushTop(m_config.shortcutFlushTop);
    m_sidebar.setChromeStyle(radius, border);
    m_sidebar.setButtonScale(m_config.sidebarScale);
    m_sidebar.setStripEdge(m_config.shortcutEdge);
    m_sidebar.setStripNudge(
        static_cast<float>(std::clamp(m_config.shortcutNudgeX, -25, 25)) * 12.8f,
        static_cast<float>(std::clamp(m_config.shortcutNudgeXRight, -25, 25)) * 12.8f);
    m_sidebar.setStripNudgeY(
        static_cast<float>((std::clamp(m_config.shortcutNudgeY, -24, 24) / 2) * 2));
    syncGridShortcutInsets();
    applyTitlePillLayout();
}

namespace {
std::uint64_t shortcutSlotId(const std::string& csv, int slot) {
    std::size_t start = 0;
    for (int i = 0; i < slot; ++i) {
        const std::size_t comma = csv.find(',', start);
        if (comma == std::string::npos)
            return 0;
        start = comma + 1;
    }
    const std::size_t comma = csv.find(',', start);
    const std::string part = csv.substr(start, comma == std::string::npos
                                                   ? std::string::npos
                                                   : comma - start);
    try {
        return std::stoull(part);
    } catch (...) {
        return 0;
    }
}

std::string shortcutSetSlot(const std::string& csv, int slot, std::uint64_t id) {
    std::uint64_t ids[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; ++i)
        ids[i] = shortcutSlotId(csv, i);
    if (slot >= 0 && slot < 6)
        ids[slot] = id;
    std::string out;
    for (int i = 0; i < 6; ++i) {
        if (i)
            out += ',';
        out += std::to_string(ids[i]);
    }
    return out;
}

int shortcutSlotSide(const std::string& csv, int slot, int fallback) {
    if (slot < 0 || slot >= 6)
        return fallback;
    std::size_t start = 0;
    for (int i = 0; i < 6; ++i) {
        const std::size_t comma = csv.find(',', start);
        const std::string part = csv.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        if (i == slot) {
            try {
                return std::clamp(std::stoi(part), 0, 1);
            } catch (...) {
                return fallback;
            }
        }
        if (comma == std::string::npos)
            return fallback;
        start = comma + 1;
    }
    return fallback;
}

std::string shortcutSetSlotSide(const std::string& csv, int slot, int side,
                                int fallback) {
    int sides[6];
    for (int i = 0; i < 6; ++i)
        sides[i] = shortcutSlotSide(csv, i, fallback);
    if (slot >= 0 && slot < 6)
        sides[slot] = std::clamp(side, 0, 1);
    std::string out;
    for (int i = 0; i < 6; ++i) {
        if (i)
            out += ',';
        out += std::to_string(sides[i]);
    }
    return out;
}
} // namespace

bool WiiUMenuApp::isGameShortcutPinned(std::uint64_t titleId) const {
    if (titleId == 0)
        return false;
    for (int slot = 0; slot < 6; ++slot) {
        if (shortcutSlotId(m_config.gameShortcuts, slot) == titleId)
            return true;
    }
    return false;
}

bool WiiUMenuApp::isTitleHidden(std::uint64_t titleId) const {
    if (titleId == 0)
        return false;
    if (!m_hiddenTitleIdsCacheValid ||
        m_hiddenTitleIdsSource != m_config.hiddenGames) {
        m_hiddenTitleIdsCache.clear();
        const std::string& csv = m_config.hiddenGames;
        std::size_t start = 0;
        while (start <= csv.size()) {
            const std::size_t comma = csv.find(',', start);
            const std::string part = csv.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            try {
                const std::uint64_t id = std::stoull(part);
                if (id != 0)
                    m_hiddenTitleIdsCache.push_back(id);
            } catch (...) {
            }
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        m_hiddenTitleIdsSource = csv;
        m_hiddenTitleIdsCacheValid = true;
    }
    return std::find(m_hiddenTitleIdsCache.begin(), m_hiddenTitleIdsCache.end(),
                     titleId) != m_hiddenTitleIdsCache.end();
}

std::pair<int, int> WiiUMenuApp::heroOffsetFor(std::uint64_t titleId) const {
    const auto it = m_config.heroOffsets.find(titleId);
    if (it == m_config.heroOffsets.end())
        return {0, 0};
    return {std::clamp(it->second.first, -100, 100),
            std::clamp(it->second.second, -100, 100)};
}

void WiiUMenuApp::setHeroOffset(std::uint64_t titleId, int xPercent, int yPercent) {
    if (titleId == 0)
        return;
    const int x = std::clamp(xPercent, -100, 100);
    const int y = std::clamp(yPercent, -100, 100);
    if (x == 0 && y == 0)
        m_config.heroOffsets.erase(titleId);
    else
        m_config.heroOffsets[titleId] = {x, y};
    m_config.save();
    if (m_steamGridDbBackdrop)
        m_steamGridDbBackdrop->setHeroOffset(x, y);
}

std::string WiiUMenuApp::customTitleFor(std::uint64_t titleId) const {
    const std::string& store = m_config.gameNames;
    std::size_t start = 0;
    while (start < store.size()) {
        const std::size_t nl = store.find('\n', start);
        const std::string line = store.substr(
            start, nl == std::string::npos ? std::string::npos : nl - start);
        const std::size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            try {
                if (std::stoull(line.substr(0, tab)) == titleId)
                    return line.substr(tab + 1);
            } catch (...) {
            }
        }
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    return std::string();
}

void WiiUMenuApp::setGameName(std::uint64_t titleId, const std::string& name) {
    if (titleId == 0)
        return;
    std::string out;
    const std::string& store = m_config.gameNames;
    std::size_t start = 0;
    while (start < store.size()) {
        const std::size_t nl = store.find('\n', start);
        const std::string line = store.substr(
            start, nl == std::string::npos ? std::string::npos : nl - start);
        const std::size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            try {
                if (std::stoull(line.substr(0, tab)) != titleId) {
                    out += line;
                    out += '\n';
                }
            } catch (...) {
            }
        }
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    if (!name.empty())
        out += std::to_string(titleId) + "\t" + name + "\n";
    m_config.gameNames = out;
    m_config.save();
    applyDisplayModel(buildRootFolderModel(), titleId, false);
}

void WiiUMenuApp::applyDockedMode(bool docked) {
    // Both modes present the same 1280x720 framebuffer; only the layout
    // overrides below change. There is no swapchain or framebuffer rebuild.
    m_dockedNow = docked;
    // handheld* is the restore point and Config::load always seeds it from the
    // saved sizes. Deriving the base from it every time (instead of capturing
    // once) keeps the 100% member defaults from overwriting a saved size on the
    // first frame, when -1 -> Handheld already counts as a mode change.
    const int handheldIcon = std::clamp(
        m_config.handheldIconScale > 0 ? m_config.handheldIconScale
                                       : m_config.iconScale, 70, 130);
    const int handheldSide = std::clamp(
        m_config.handheldShortcutScale > 0 ? m_config.handheldShortcutScale
                                           : m_config.sidebarScale, 70, 150);
    m_config.handheldIconScale = handheldIcon;
    m_config.handheldShortcutScale = handheldSide;
    m_baseIconScale = handheldIcon;
    m_baseShortcutScale = handheldSide;
    // 0 means "never set separately": inherit the sizes in use instead of
    // snapping a custom size back to a hard 100% the first time docked.
    if (m_config.dockedIconScale <= 0)
        m_config.dockedIconScale = handheldIcon;
    if (m_config.dockedShortcutScale <= 0)
        m_config.dockedShortcutScale = handheldSide;
    const bool override = docked && m_config.dockedSizeOverrides;
    m_config.iconScale = override
        ? std::clamp(m_config.dockedIconScale, 70, 130) : handheldIcon;
    m_config.sidebarScale = override
        ? std::clamp(m_config.dockedShortcutScale, 70, 150) : handheldSide;
    // One control sizes the whole chrome row: shortcuts, profile, clock, battery.
    m_config.hudScale = std::clamp(m_config.sidebarScale, 70, 160);
    m_config.clockScale = std::clamp(m_config.sidebarScale, 70, 160);
    // Boot persistence: write the resolved handheld/docked pair so the sizes
    // applied here are the same ones loaded on the next boot.
    m_config.save();
    // Keep the Docked tab showing the values that are actually in use, so the
    // row the user just moved never reads back a stale size.
    if (m_settings)
        m_settings->setDockedState(m_config.dockedSizeOverrides,
                                   m_config.dockedIconScale,
                                   m_config.dockedShortcutScale);
    if (m_settings)
        m_settings->setDockedCarouselState(m_config.dockedCarouselX,
                                           m_config.dockedCarouselY);
    DebugLog::log("[dock] mode=%d overrides=%d icon=%d side=%d carousel=%d,%d",
                  docked ? 1 : 0, m_config.dockedSizeOverrides ? 1 : 0,
                  m_config.iconScale, m_config.sidebarScale,
                  m_config.dockedCarouselX, m_config.dockedCarouselY);
    applyIconPresentation();
    applySidebarPresentation();
    applyHudScale();
    applyClockLayout();
    if (m_allApps.empty()) {
        // The async list has not landed yet. Reflowing now would rebuild the
        // grid from an empty model; finalizeRefresh() applies it instead.
        m_dockedReflowPending = true;
    } else {
        m_dockedReflowPending = false;
        reflowHomeGrid();
    }
}

void WiiUMenuApp::refreshHiddenGamesState() {
    if (!m_settings)
        return;
    std::vector<std::pair<std::uint64_t, std::string>> games;
    const std::string& csv = m_config.hiddenGames;
    std::size_t start = 0;
    while (start <= csv.size()) {
        const std::size_t comma = csv.find(',', start);
        const std::string part = csv.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        std::uint64_t id = 0;
        try { id = std::stoull(part); } catch (...) { id = 0; }
        if (id != 0) {
            std::string title;
            for (const auto& entry : m_allApps)
                if (entry.titleId == id) { title = entry.title; break; }
            games.emplace_back(id, title.empty() ? "Unknown title" : title);
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    m_settings->setHiddenGamesState(games);
    if (m_settings->isActive())
        m_settings->rebuildCurrentTab();
}

void WiiUMenuApp::hideGameFromHome(std::uint64_t titleId) {
    if (titleId == 0 || isTitleHidden(titleId))
        return;
    if (!m_config.hiddenGames.empty())
        m_config.hiddenGames += ',';
    m_config.hiddenGames += std::to_string(titleId);
    m_config.save();
    refreshHiddenGamesState();
    applyDisplayModel(buildRootFolderModel(), 0, false);
}

void WiiUMenuApp::restoreGameToHome(std::uint64_t titleId) {
    if (titleId == 0)
        return;
    std::string out;
    std::size_t start = 0;
    const std::string& csv = m_config.hiddenGames;
    while (start <= csv.size()) {
        const std::size_t comma = csv.find(',', start);
        const std::string part = csv.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        std::uint64_t id = 0;
        try { id = std::stoull(part); } catch (...) { id = 0; }
        if (id != 0 && id != titleId) {
            if (!out.empty()) out += ',';
            out += std::to_string(id);
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    m_config.hiddenGames = out;
    m_config.save();
    refreshHiddenGamesState();
    applyDisplayModel(buildRootFolderModel(), titleId, false);
}

void WiiUMenuApp::launchPinnedTitle(std::uint64_t titleId) {
    if (titleId == 0 || leaveSplashActive())
        return;
    AppEntry* entry = nullptr;
    const int modelIndex = findTitleIndex(titleId);
    if (modelIndex >= 0)
        entry = &m_model.at(modelIndex);
    if (!entry) {
        for (auto& app : m_allApps) {
            if (app.titleId == titleId) {
                entry = &app;
                break;
            }
        }
    }
#ifdef SWITCHU_MENU
    // Reuse the tile's own activation so a shortcut gets the launch zoom, the
    // startup-user handling and the suspended-app resume that pressing A on
    // the icon gets. Focusing alone never launched anything.
    if (m_grid) {
        for (const auto& icon : m_grid->allIcons()) {
            if (!icon || icon->titleId() != titleId)
                continue;
            activateApplication(icon.get(), entry, titleId, icon->title(), true);
            return;
        }
    }
    // Hidden titles (and titles inside a folder) have no tile on the visible
    // grid. Launch from the shortcut socket itself instead of trying to focus a
    // tile that does not exist.
    if (entry) {
        const auto& pins = m_sidebar.pinButtons();
        for (int slot = 0; slot < static_cast<int>(pins.size()); ++slot) {
            if (shortcutSlotId(m_config.gameShortcuts, slot) != titleId)
                continue;
            const auto& anchor = pins[static_cast<std::size_t>(slot)];
            if (!anchor)
                break;
            std::string launchTitle = entry->title;
            const std::string renamed = customTitleFor(titleId);
            if (!renamed.empty())
                launchTitle = renamed;
            activateApplicationFromShortcut(anchor.get(), anchor->icon(),
                                            anchor->cornerRadius(), entry,
                                            titleId, launchTitle);
            return;
        }
    }
#endif
    // The title is inside a folder (or otherwise off this grid): no tile to
    // zoom from, so at least bring it into view.
    focusTitle(titleId, true);
}

void WiiUMenuApp::refreshPinnedIcons() {
    const auto& pins = m_sidebar.pinButtons();
    for (int slot = 0; slot < SidebarManager::kPinnedSlotCount; ++slot) {
        const std::uint64_t id = shortcutSlotId(m_config.gameShortcuts, slot);
        if (id == 0 || slot >= static_cast<int>(pins.size()) || !pins[slot])
            continue;
        AppletButton* btn = pins[static_cast<std::size_t>(slot)].get();
        if (btn->icon() && btn->icon()->valid())
            continue;
        auto iconData = AppListLoader::loadIconData(id, false);
        if (iconData.empty()) {
            if (const auto* held = m_iconStreamer.compressedIcon(id);
                held && !held->empty()) {
                iconData = *held;
                switchu::control_cache::writeIcon(
                    id, iconData.data(), iconData.size());
            }
        }
        const bool bound = m_sidebar.setGameShortcutIcon(
            slot, iconData, app().gpu(), app().renderer());
        DebugLog::log("[shortcuts] icon retry slot=%d tid=%016lX bytes=%u bound=%d",
                      slot, static_cast<unsigned long>(id),
                      static_cast<unsigned>(iconData.size()), bound ? 1 : 0);
    }
}

void WiiUMenuApp::applyGameShortcuts() {
    for (int slot = 0; slot < 6; ++slot)
        m_sidebar.clearGameShortcut(slot);
    for (int slot = 0; slot < 6; ++slot) {
        const std::uint64_t id = shortcutSlotId(m_config.gameShortcuts, slot);
        m_sidebar.setGameShortcutSide(slot,
            shortcutSlotSide(m_config.gameShortcutSides, slot, m_config.shortcutSide));
        if (id == 0)
            continue;
        std::string title;
        for (const auto& entry : m_allApps) {
            if (entry.titleId == id) {
                title = entry.title;
                break;
            }
        }
        if (title.empty())
            continue;
        auto iconData = AppListLoader::loadIconData(id, false);
        if (iconData.empty()) {
            // The grid receives its icons inline from the daemon catalogue; the
            // SD cache the loader reads can still be empty. Reuse the bytes the
            // streamer is holding and park them in that cache so the retry path
            // (and the next boot) finds them too.
            if (const auto* held = m_iconStreamer.compressedIcon(id);
                held && !held->empty()) {
                iconData = *held;
                switchu::control_cache::writeIcon(
                    id, iconData.data(), iconData.size());
            }
        }
        DebugLog::log("[shortcuts] bind slot=%d tid=%016lX bytes=%u",
                      slot, static_cast<unsigned long>(id),
                      static_cast<unsigned>(iconData.size()));
        m_sidebar.setGameShortcut(slot, title, iconData,
                                  app().gpu(), app().renderer(),
                                  [this, id]() {
            m_audio.playSfx(Sfx::Activate);
            launchPinnedTitle(id);
        });
    }
    // Sockets just appeared or vanished, so the row has to be re-placed before
    // the new buttons are drawn. That pass also refreshes the visible count the
    // grid inset below is derived from.
    m_sidebar.refreshLayout();
    syncGridShortcutInsets();
    // Boot applies the saved shortcuts after the first layout pass, so the
    // grid's left/right pin targets have to be rebuilt once the sides are set.
    configureDynamicLineNavigation();
    // A socket bound here can miss its icon when the control cache has not been
    // written yet; the frame loop retries a few times while any pin is blank.
    m_pinIconRetryFrames = 120;
    m_pinIconRetryAttempts = 3;
}

void WiiUMenuApp::toggleGameShortcut(std::uint64_t titleId) {
    if (titleId == 0)
        return;
    if (isGameShortcutPinned(titleId)) {
        removeGameShortcut(titleId);
        return;
    }
    addGameShortcut(titleId, m_config.shortcutSide);
}

void WiiUMenuApp::addGameShortcut(std::uint64_t titleId, int side) {
    if (titleId == 0)
        return;
    side = std::clamp(side, 0, 1);
    int existingSlot = -1;
    int freeSlot = -1;
    for (int slot = 0; slot < 6; ++slot) {
        const std::uint64_t id = shortcutSlotId(m_config.gameShortcuts, slot);
        if (id == titleId)
            existingSlot = slot;
        else if (id == 0 && freeSlot < 0)
            freeSlot = slot;
    }
    if (existingSlot < 0 && freeSlot < 0) {
        if (m_gameOptions) {
            m_gameOptions->requestToast(
                nxui::I18n::instance().tr("game.shortcut_full",
                    "All six shortcut slots are taken."));
        }
        return;
    }
    const int slot = existingSlot >= 0 ? existingSlot : freeSlot;
    // Clear the title from any slot that held it, then seat it once. Re-adding
    // an already pinned title only moves it to the requested side.
    std::string csv = m_config.gameShortcuts;
    for (int i = 0; i < 6; ++i) {
        if (shortcutSlotId(csv, i) == titleId)
            csv = shortcutSetSlot(csv, i, 0);
    }
    csv = shortcutSetSlot(csv, slot, titleId);
    m_config.gameShortcuts = csv;
    m_config.gameShortcutSides = shortcutSetSlotSide(
        m_config.gameShortcutSides, slot, side, m_config.shortcutSide);
    // The generic Add row follows the last side the player picked.
    m_config.shortcutSide = side;
    m_config.save();
    applyGameShortcuts();
    if (m_gameOptions)
        m_gameOptions->setShortcutPinned(true);
}

void WiiUMenuApp::removeGameShortcut(std::uint64_t titleId) {
    if (titleId == 0)
        return;
    std::string csv = m_config.gameShortcuts;
    for (int i = 0; i < 6; ++i) {
        if (shortcutSlotId(csv, i) == titleId)
            csv = shortcutSetSlot(csv, i, 0);
    }
    m_config.gameShortcuts = csv;
    m_config.save();
    applyGameShortcuts();
    if (m_gameOptions)
        m_gameOptions->setShortcutPinned(false);
}

void WiiUMenuApp::applyHudScale() {
    const float s = static_cast<float>(std::clamp(m_config.hudScale, 70, 160)) / 100.f;
    if (m_battery) {
        m_battery->setVisualScale(s);
        // lround keeps the call valid whether setSize takes floats or ints.
        m_battery->setSize(std::lround(150.f * s), std::lround(62.f * s));
        m_battery->setMarginTop(14.f * s);
        m_battery->setMarginRight(24.f * s);
    }
    if (m_profileLock) {
        // Stock profile tile is 62x62 (kProfileLockWidth/Height live in
        // WiiUMenuApp.cpp and are not visible from this translation unit).
        const float w = 62.f * s;
        const float h = 62.f * s;
        m_profileLock->setSize(w, h);
        m_profileLock->setMinWidth(w);
        m_profileLock->setMinHeight(h);
        m_profileLock->setMarginTop(14.f * s);
        m_profileLock->setMarginLeft(24.f * s);
        // The HUD row spreads evenly; keep the tile balanced against the
        // battery pill so the clock stays on the screen's centre line.
        m_profileLock->setMarginRight((150.f * s + 24.f * s) - (w + 24.f * s));
    }
    if (m_topHud) {
        m_topHud->layout();
        placeLayoutFlipButton();
        // The row reset both capsules to the top line; send them back down if
        // the strip lives at the bottom.
        applyHudTravel();
    }
}

void WiiUMenuApp::applyClockLayout() {
    if (m_clock) {
        const float s = static_cast<float>(std::clamp(m_config.clockScale, 70, 160)) / 100.f;
        m_clock->setVisualScale(s);
        m_clock->setSize(std::lround(150.f * s), std::lround(62.f * s));
        // The row aligns children to the top margin, so vertical travel is a
        // margin delta in the same 2px steps the slider uses.
        // Symmetric travel: 0 is the bar's centre line, so the readout is a
        // real offset instead of a travel percentage. Never let the capsule
        // leave the top edge.
        float dy = static_cast<float>(std::clamp(m_config.clockNudgeY, -14, 14));
        // While the bar is behind the clock, travel stops at the band's edges:
        // the clock slides inside its cell instead of stepping out of the bar.
        if (m_appLayoutMode == AppLayoutMode::DynamicLine
            && std::clamp(m_config.shortcutEdge, 0, 1) == 0
            && m_config.shortcutStrip) {
            const float bandH = 62.f
                * static_cast<float>(std::clamp(m_config.hudScale, 70, 160)) / 100.f;
            const float clockH = 62.f * s;
            const float slack = std::max(0.f, (bandH - clockH) * 0.5f);
            dy = std::clamp(dy, -slack, slack);
        }
        m_clock->setMarginTop(std::max(0.f, 14.f * s + dy));
        // The HUD row splits its leftover air evenly, so a margin only moves
        // the clock half of its own width. Doubling it makes the slider steps
        // land where the setting says they do.
        const float d = static_cast<float>(std::clamp(m_config.clockNudgeX, -25, 25)) * 6.4f;
        m_clock->setMarginLeft(d > 0.f ? 2.f * d : 0.f);
        m_clock->setMarginRight(d < 0.f ? -2.f * d : 0.f);
    }
    if (m_topHud) {
        m_topHud->layout();
        placeLayoutFlipButton();
        // The row reset both capsules to the top line; send them back down if
        // the strip lives at the bottom.
        applyHudTravel();
    }
}

void WiiUMenuApp::applyHudTravel() {
    if (!m_topHud || !m_profileLock || !m_battery)
        return;
    // Only travel while the bar itself is on screen. Settings and the Theme
    // Shop are fullscreen panels over HOME, so the bar stays arranged behind
    // them; withdrawing it there made the capsules jump on open and back on
    // close. Folders genuinely hide the bar.
    const auto route = m_navigator.route();
    const bool home = route == switchu::navigation::Route::Home
        || route == switchu::navigation::Route::Settings
        || route == switchu::navigation::Route::ThemeShop;
    const bool bottom = home
        && m_appLayoutMode == AppLayoutMode::DynamicLine
        && std::clamp(m_config.shortcutEdge, 0, 1) == 1
        && m_config.shortcutStrip
        && !m_sidebar.stripPlateSuppressed();
    if (!bottom) {
        if (m_hudTravelled) {
            // Let the HUD row put both capsules back on the top line.
            m_hudTravelled = false;
            m_topHud->layout();
            placeLayoutFlipButton();
        }
        return;
    }

    const nxui::Rect band = m_sidebar.stripBandTarget();
    if (band.height <= 1.f)
        return;
    const float s = static_cast<float>(std::clamp(m_config.hudScale, 70, 160)) / 100.f;
    const float h = 62.f * s;
    const float y = band.y + (band.height - h) * 0.5f;
    const float margin = 24.f * s;
    const float batteryW = 150.f * s;
    // Outboard of the two shortcut clusters, so nothing overlaps.
    m_profileLock->setRect({band.x + margin, y, 62.f * s, h});
    m_battery->setRect({band.x + band.width - margin - batteryW, y, batteryW, h});
    // The clock is the middle cell of the bar, so it travels with the capsules
    // rather than staying behind on the top line. Its own scale decides the
    // rect, and the nudges keep working inside the band.
    if (m_clock) {
        const float clockS = static_cast<float>(
            std::clamp(m_config.clockScale, 70, 160)) / 100.f;
        const float clockW = 150.f * clockS;
        const float clockH = 62.f * clockS;
        float clockX = band.x + (band.width - clockW) * 0.5f
            + static_cast<float>(std::clamp(m_config.clockNudgeX, -25, 25)) * 12.8f;
        clockX = std::clamp(clockX, band.x + 8.f,
                            std::max(band.x + 8.f, band.right() - clockW - 8.f));
        float clockDy = static_cast<float>(
            std::clamp(m_config.clockNudgeY, -14, 14));
        const float slack = std::max(0.f, (band.height - clockH) * 0.5f);
        clockDy = std::clamp(clockDy, -slack, slack);
        m_clock->setRect({clockX,
                          band.y + (band.height - clockH) * 0.5f + clockDy,
                          clockW, clockH});
    }
    m_hudTravelled = true;
}

void WiiUMenuApp::syncGridShortcutInsets() {
    // Authoritative size for the shortcut row. Re-placing the sidebar (strip
    // paddle, edge switch, rebuild after an asset reload) used to happen with
    // whatever scale that pass had, which read as the icons snapping back to
    // stock after a resize. Asserting it here makes the setting win every frame.
    m_sidebar.setButtonScale(m_config.sidebarScale);
    if (!m_grid)
        return;
    // Only HOME carries the strip, and the fullscreen Settings / Theme Shop
    // panels sit on top of HOME rather than replacing it. Treating those two
    // routes as "not home" withdrew the strip reserve and unmerged the plate
    // behind the panel, so opening a menu visibly re-arranged the top row and
    // re-laid the grid underneath. Folders keep the whole frame.
    const auto route = m_navigator.route();
    const bool home = route == switchu::navigation::Route::Home
        || route == switchu::navigation::Route::Settings
        || route == switchu::navigation::Route::ThemeShop;
    const float top = home ? m_sidebar.reservedTop() : 0.f;
    const float bottom = home ? m_sidebar.reservedBottom() : 0.f;
    m_grid->setVerticalInsets(top, bottom);
    // Hand the live tile band to the sidebar so its left/right sliders use the
    // grid as their inner limit instead of sliding across it.
    const nxui::Rect band = m_grid->tileBand();
    m_sidebar.setGridClearance(home ? band.x : 0.f,
                               home ? std::min(1280.f, band.right()) : 1280.f);
    // Park the capsules on the strip's row first, then hand over the rects.
    applyHudTravel();
    // With the strip on top the plate becomes the whole top bar, so hand over
    // the capsule rects (they move with the HUD size and clock settings).
    // The capsules sit on whichever row the strip uses, so the band merges at
    // either edge. Dropping their own glass only makes sense while it is drawn:
    // Settings hides the plate with the sidebar, and a bare capsule with no bar
    // behind it read as detached.
    const bool hudAnchors = home
        && m_appLayoutMode == AppLayoutMode::DynamicLine
        && m_profileLock && m_clock && m_battery
        && m_profileLock->isVisible() && m_clock->isVisible()
        && m_battery->isVisible();
    const bool mergedBar = hudAnchors
        && m_config.shortcutStrip
        && !m_sidebar.stripPlateSuppressed();
    if (hudAnchors) {
        m_sidebar.setHudAnchors(m_profileLock->rect(), m_clock->rect(),
                               m_battery->rect());
    } else {
        m_sidebar.setHudAnchors(nxui::Rect{}, nxui::Rect{}, nxui::Rect{});
    }
    // One bar: the capsules give up their own glass so the profile, the clock
    // and the battery read as contents of the bar instead of three widgets
    // sitting on top of it with their own rims.
    // The clock never travels, so it only gives up its glass when the band is
    // actually behind it (strip at the top).
    // The clock is a cell of the bar, not a capsule parked on top of it: once
    // the band is behind it the glass stays off at every nudge value, and its
    // travel is confined to the band (see applyClockLayout). At the bottom
    // edge the band is only behind it while the capsules have travelled down.
    const bool clockOnBand = mergedBar
        && (std::clamp(m_config.shortcutEdge, 0, 1) == 0 || m_hudTravelled);
    if (m_profileLock) m_profileLock->setBackgroundVisible(!mergedBar);
    if (m_clock) {
        m_clock->setBackgroundVisible(!clockOnBand);
        m_clock->setCellDividers(clockOnBand);
    }
    if (m_battery)     m_battery->setBackgroundVisible(!mergedBar);
}

void WiiUMenuApp::applyTitlePillVisibility() {
    if (!m_titlePill)
        return;
    m_titlePill->setSuppressed(m_config.hideGameTitle);
    if (m_config.hideGameTitle || !m_grid)
        return;
    // Bring the capsule back on the spot rather than waiting for the next focus
    // move, which could be a page away.
    const int idx = m_grid->focusedGlobalIndex();
    const auto& icons = m_grid->allIcons();
    if (idx < 0 || idx >= static_cast<int>(icons.size()))
        return;
    const auto& icon = icons[static_cast<std::size_t>(idx)];
    if (icon)
        m_titlePill->setText(icon->title());
}

void WiiUMenuApp::applyTitlePillLayout() {
    if (!m_titlePill)
        return;
    constexpr float kStockPillY = 630.f;
    m_titlePill->setNudgeX(
        12.8f * static_cast<float>(std::clamp(m_config.titlePillNudgeX, -25, 25)));
    // A bottom shortcut strip already takes that space, so the up/down setting
    // starts from a position that is clear of the chrome instead of fighting it.
    const float baseY = kStockPillY - m_sidebar.reservedBottom();
    const float dy = 6.4f * static_cast<float>(std::clamp(m_config.titlePillNudgeY, -25, 25));
    const nxui::Rect current = m_titlePill->rect();
    // Clamped so the capsule cannot be nudged off either edge of the screen.
    m_titlePill->setPosition(current.x, std::clamp(baseY + dy, 260.f, 660.f));
    m_titlePill->setSuppressed(m_config.hideGameTitle);
}
