#pragma once

#include "TabbedOverlayScreen.hpp"
#include <cstdint>
#include <future>

namespace settings::tabs {
class SystemTab;
class AccessibilityTab;
class AudioTab;
class DisplayTab;
class InternetTab;
class SteamGridDbTab;
class ControllersTab;
class BluetoothTab;
class SleepTab;
class StorageTab;
class AboutTab;
class LayoutTab;
class TreatmentTab;
class AppearanceTab;
class BackgroundsTab;
class ShortcutsTab;
class GlassTab;
}

class SettingsScreen : public TabbedOverlayScreen {
public:
    SettingsScreen();
    ~SettingsScreen() override = default;

    void onWireframeChange(BoolCb cb)   { m_wireframeCb = std::move(cb); }
    void onGridColumnsChange(IntCb cb)  { m_gridColumnsCb = std::move(cb); }
    void onGridRowsChange(IntCb cb)     { m_gridRowsCb = std::move(cb); }
    void onUiLanguageChange(StringCb cb) { m_uiLanguageCb = std::move(cb); }
    void onClockUse12HourChange(BoolCb cb) { m_clockUse12HourCb = std::move(cb); }
    void onWebClockSyncChange(BoolCb cb) { m_webClockSyncCb = std::move(cb); }
    using ExternalToastCb = std::function<void(const std::string& msg, float holdSeconds)>;
    void onExternalToast(ExternalToastCb cb) { m_externalToastCb = std::move(cb); }
    using ClockSyncBootRetryCb = std::function<void(const std::string& error)>;
    void onClockSyncBootRetry(ClockSyncBootRetryCb cb) {
        m_clockSyncBootRetryCb = std::move(cb);
    }

    void startWebClockSync(bool toastFailureOnly = false);
    void pollClockSync();
    void openSettingDropdown(int rawIdx) { openDropdown(rawIdx); }
    void onAccessibilityEnabledChange(BoolCb cb) { m_accessibilityEnabledCb = std::move(cb); }
    void onAccessibilitySpeakHintsChange(BoolCb cb) { m_accessibilitySpeakHintsCb = std::move(cb); }
    void onAccessibilitySpeakContextEveryFocusChange(BoolCb cb) { m_accessibilitySpeakContextEveryFocusCb = std::move(cb); }
    void onAccessibilitySpeakPositionChange(BoolCb cb) { m_accessibilitySpeakPositionCb = std::move(cb); }
    void onAccessibilitySpeechRateChange(IntCb cb) { m_accessibilitySpeechRateCb = std::move(cb); }
    void onNetConnect(VoidCb cb)        { m_netConnectCb = std::move(cb); }
    void onSteamGridDbEnabledChange(BoolCb cb) { m_steamGridDbEnabledCb = std::move(cb); }
    void onSteamGridDbAspectChange(IntCb cb) { m_steamGridDbAspectCb = std::move(cb); }
    void onIconAspectChange(IntCb cb) { m_iconAspectCb = std::move(cb); }
    void onIconCornersChange(IntCb cb) { m_iconCornersCb = std::move(cb); }
    void onIconCornerPctChange(IntCb cb) { m_iconCornerPctCb = std::move(cb); }
    void onGridNudgeChange(IntCb cb) { m_gridNudgeCb = std::move(cb); }
    void onLineNudgeChange(IntCb cb) { m_lineNudgeCb = std::move(cb); }
    void onIconScaleChange(IntCb cb) { m_iconScaleCb = std::move(cb); }
    void onSidebarScaleChange(IntCb cb) { m_sidebarScaleCb = std::move(cb); }
    void onHeroBackdropResolutionChange(IntCb cb) {
        m_heroBackdropResolutionCb = std::move(cb);
    }
    void onHeroBackdropBlurChange(IntCb cb) {
        m_heroBackdropBlurCb = std::move(cb);
    }
    void onSortModeChange(IntCb cb) { m_sortModeCb = std::move(cb); }
    void onAlphaOrderChange(BoolCb cb) { m_alphaOrderCb = std::move(cb); }
    void setAlphaOrderState(bool on) { m_alphaOrder = on; }
    bool alphaOrder() const { return m_alphaOrder; }
    void setAlphaOrder(bool on) { m_alphaOrder = on; }
    void notifyAlphaOrder(bool on) { if (m_alphaOrderCb) m_alphaOrderCb(on); }
    void onDockedSizeOverridesChange(BoolCb cb) { m_dockedOverridesCb = std::move(cb); }
    void onDockedIconScaleChange(IntCb cb) { m_dockedIconCb = std::move(cb); }
    void onDockedShortcutScaleChange(IntCb cb) { m_dockedShortcutCb = std::move(cb); }
    void setDockedState(bool overrides, int iconScale, int shortcutScale) {
        m_dockedOverrides = overrides;
        m_dockedIconScale = std::clamp(iconScale, 70, 130);
        m_dockedShortcutScale = std::clamp(shortcutScale, 70, 150);
    }
    void setSortModeState(int mode) { m_sortMode = std::clamp(mode, 0, 2); }
    void setHiddenGamesState(const std::vector<std::pair<std::uint64_t, std::string>>& games) {
        m_hiddenGames = games;
    }
    const std::vector<std::pair<std::uint64_t, std::string>>& hiddenGames() const { return m_hiddenGames; }
    void onRestoreHiddenGame(std::function<void(std::uint64_t)> cb) { m_restoreHiddenCb = std::move(cb); }
    void notifyRestoreHidden(std::uint64_t id) { if (m_restoreHiddenCb) m_restoreHiddenCb(id); }
    bool dockedSizeOverrides() const { return m_dockedOverrides; }
    void setDockedSizeOverrides(bool on) { m_dockedOverrides = on; }
    int dockedIconScale() const { return m_dockedIconScale; }
    int dockedShortcutScale() const { return m_dockedShortcutScale; }
    void setDockedIconScale(int v) { m_dockedIconScale = std::clamp(v, 70, 130); }
    void setDockedShortcutScale(int v) { m_dockedShortcutScale = std::clamp(v, 70, 150); }
    void notifyDockedOverrides(bool on) { if (m_dockedOverridesCb) m_dockedOverridesCb(on); }
    void notifyDockedIcon(int v) { if (m_dockedIconCb) m_dockedIconCb(v); }
    void notifyDockedShortcut(int v) { if (m_dockedShortcutCb) m_dockedShortcutCb(v); }
    void onLockPadCodeChange(std::function<void(bool, const std::string&)> cb) {
        m_lockPadCodeCb = std::move(cb);
    }
    void onShortcutEdgeChange(IntCb cb) { m_shortcutEdgeCb = std::move(cb); }
    void onShortcutNudgeChange(IntCb cb) { m_shortcutNudgeXCb = std::move(cb); }
    void onShortcutNudgeRightChange(IntCb cb) {
        m_shortcutNudgeXCbRight = std::move(cb);
    }
    void onShortcutStripChange(BoolCb cb) { m_shortcutStripCb = std::move(cb); }
    void onPinSideChange(IntCb cb) { m_pinSideCb = std::move(cb); }
    void onShortcutNudgeYChange(IntCb cb) { m_shortcutNudgeYCb = std::move(cb); }
    void onIconSpacingChange(IntCb cb) { m_iconSpacingCb = std::move(cb); }
    void onCarouselDepthChange(IntCb cb) { m_carouselDepthCb = std::move(cb); }
    void onLineNudgeXChange(IntCb cb) { m_lineNudgeXCb = std::move(cb); }
    void onSaveLayoutPreset(IntCb cb) { m_saveLayoutPresetCb = std::move(cb); }
    void onLoadLayoutPreset(IntCb cb) { m_loadLayoutPresetCb = std::move(cb); }
    void onResetIconLayout(VoidCb cb) { m_resetIconLayoutCb = std::move(cb); }
    void onWallpaperDimChange(IntCb cb) { m_wallpaperDimCb = std::move(cb); }
    void onVignetteChange(IntCb cb) { m_vignetteCb = std::move(cb); }
    void onUiRadiusPctChange(IntCb cb) { m_uiRadiusPctCb = std::move(cb); }
    void onIconShadowChange(VoidCb cb) { m_iconShadowCb = std::move(cb); }
    void onIconOverlayChange(VoidCb cb) { m_iconOverlayCb = std::move(cb); }
    void onMotionStyleChange(IntCb cb) { m_motionStyleCb = std::move(cb); }
    void onGlassTintChange(IntCb cb) { m_glassTintCb = std::move(cb); }
    void onGlassSolidChange(BoolCb cb) { m_glassSolidCb = std::move(cb); }
    void onHighlightTintChange(IntCb cb) { m_highlightTintCb = std::move(cb); }
    void setHighlightTint(int tint) { m_highlightTint = std::clamp(tint, 0, 9); }
    void setGlassState(int tint, bool solid) {
        m_glassTint = std::clamp(tint, 0, 9);
        m_glassSolid = solid;
    }
    void onCarouselFadeChange(IntCb cb) { m_carouselFadeCb = std::move(cb); }
    void onTitlePillNudgeChange(IntCb cb) { m_titlePillNudgeCb = std::move(cb); }
    void onTitlePillNudgeYChange(IntCb cb) { m_titlePillNudgeYCb = std::move(cb); }
    void onGameTitleVisibilityChange(BoolCb cb) {
        m_gameTitleVisibilityCb = std::move(cb);
    }
    void onHideActionHintsChange(BoolCb cb) { m_hideActionHintsCb = std::move(cb); }
    void onHudScaleChange(IntCb cb) { m_hudScaleCb = std::move(cb); }
    void onClockScaleChange(IntCb cb) { m_clockScaleCb = std::move(cb); }
    void onClockNudgeXChange(IntCb cb) { m_clockNudgeXCb = std::move(cb); }
    void onClockNudgeYChange(IntCb cb) { m_clockNudgeYCb = std::move(cb); }
    int iconShadowOffset() const { return m_iconShadowOffset; }
    int iconShadowAlpha() const { return m_iconShadowAlpha; }
    int iconVignette() const { return m_iconVignette; }
    int iconHighlight() const { return m_iconHighlight; }
    int iconDepth() const { return m_iconDepth; }
    void onWallpaperCycle(VoidCb cb) { m_wallpaperCycleCb = std::move(cb); }
    void onWallpaperAnimatedChange(BoolCb cb) { m_wallpaperAnimatedCb = std::move(cb); }
    void onWallpaperFpsChange(IntCb cb) { m_wallpaperFpsCb = std::move(cb); }
    void onBackgroundDriftChange(BoolCb cb) { m_backgroundDriftCb = std::move(cb); }
    void onBackgroundDriftSpeedChange(IntCb cb) { m_backgroundDriftSpeedCb = std::move(cb); }
    void setIconPresentationState(int aspect, int corners, int gridNudge, int lineNudge,
                                  int iconScale = 100, int iconSpacing = 0,
                                  int carouselDepth = 0, int lineNudgeX = 0,
                                  int iconVignette = 0, int iconHighlight = 0,
                                  int iconDepth = 35) {
        m_iconAspect = std::clamp(aspect, 0, 2);
        m_iconCorners = std::clamp(corners, 0, 1);
        m_gridNudgeX = std::clamp(gridNudge, -25, 25);
        m_lineNudgeY = std::clamp(lineNudge, -25, 25);
        m_iconScale = std::clamp(iconScale, 70, 130);
        m_iconSpacing = std::clamp(iconSpacing, -50, 100);
        m_carouselDepth = std::clamp(carouselDepth, -100, 100);
        m_lineNudgeX = std::clamp(lineNudgeX, -25, 25);
        m_iconVignette = std::clamp(iconVignette, 0, 100);
        m_iconHighlight = std::clamp(iconHighlight, 0, 100);
        m_iconDepth = std::clamp(iconDepth, 0, 100);
    }
    void setWallpaperState(const std::string& path, bool animated, int fps) {
        m_wallpaperPath = path;
        m_wallpaperAnimated = animated;
        m_wallpaperFps = std::clamp(fps, 5, 30);
    }
    void setBackgroundDriftState(bool on, int speed) {
        m_backgroundDrift = on;
        m_backgroundDriftSpeed = std::clamp(speed, 1, 100);
    }
    bool backgroundDrift() const { return m_backgroundDrift; }
    int backgroundDriftSpeed() const { return m_backgroundDriftSpeed; }
    void setBackgroundDrift(bool on) { m_backgroundDrift = on; }
    void setBackgroundDriftSpeed(int v) {
        m_backgroundDriftSpeed = std::clamp(v, 1, 100);
    }
    void notifyBackgroundDrift(bool on) {
        if (m_backgroundDriftCb) m_backgroundDriftCb(on);
    }
    void notifyBackgroundDriftSpeed(int v) {
        if (m_backgroundDriftSpeedCb) m_backgroundDriftSpeedCb(v);
    }
    void setIconCornerPctState(int pct) {
        m_iconCornerPct = std::clamp(pct, 0, 100);
    }
    void setSidebarScaleState(int pct) { m_sidebarScale = std::clamp(pct, 70, 150); }
    void setHeroBackdropResolutionState(int level) {
        m_heroBackdropResolution = std::clamp(level, 0, 2);
    }
    int heroBackdropResolution() const { return m_heroBackdropResolution; }
    void notifyHeroBackdropResolution() {
        if (m_heroBackdropResolutionCb)
            m_heroBackdropResolutionCb(m_heroBackdropResolution);
    }
    void setHeroBackdropBlurState(int pct) {
        m_heroBackdropBlur = std::clamp(pct, 0, 100);
    }
    int heroBackdropBlur() const { return m_heroBackdropBlur; }
    void notifyHeroBackdropBlur() {
        if (m_heroBackdropBlurCb)
            m_heroBackdropBlurCb(m_heroBackdropBlur);
    }
    void setLockPadCodeState(bool enabled, const std::string& code) {
        m_lockPadCodeEnabled = enabled;
        m_lockPadCode.clear();
        for (char c : code) {
            if ((c == 'U' || c == 'D' || c == 'L' || c == 'R') && m_lockPadCode.size() < 4)
                m_lockPadCode.push_back(c);
        }
        while (m_lockPadCode.size() < 4)
            m_lockPadCode.push_back('U');
    }
    void setLockPadCodeEnabledState(bool enabled) { m_lockPadCodeEnabled = enabled; }
    void setLockPadCodeStep(int index, char step) {
        if (index >= 0 && index < static_cast<int>(m_lockPadCode.size()))
            m_lockPadCode[static_cast<std::size_t>(index)] = step;
    }
    void notifyLockPadCodeChange() {
        if (m_lockPadCodeCb)
            m_lockPadCodeCb(m_lockPadCodeEnabled, m_lockPadCode);
    }
    bool lockPadCodeEnabled() const { return m_lockPadCodeEnabled; }
    const std::string& lockPadCode() const { return m_lockPadCode; }
    void setShortcutLayoutState(int edge, int nudgeLeft, int nudgeRight) {
        m_shortcutEdge = std::clamp(edge, 0, 1);
        m_shortcutNudgeX = std::clamp(nudgeLeft, -25, 25);
        m_shortcutNudgeXRight = std::clamp(nudgeRight, -25, 25);
    }
    void setShortcutStripState(bool on) { m_shortcutStrip = on; }
    void setPinSideState(int side) { m_pinSide = std::clamp(side, 0, 1); }
    void setShortcutNudgeYState(int px) {
        m_shortcutNudgeY = (std::clamp(px, -24, 24) / 2) * 2;
    }
    void setHudChromeState(bool hideHints, int scale) {
        m_hideActionHints = hideHints;
        m_hudScale = std::clamp(scale, 70, 160);
    }
    void setClockLayoutState(int nudgeX, int scale) {
        m_clockNudgeX = std::clamp(nudgeX, -25, 25);
        m_clockScale = std::clamp(scale, 70, 160);
    }
    void setClockVerticalState(int px) { m_clockNudgeY = std::clamp(px, -14, 14); }
    void setVisualTreatmentState(int dim, int vignette, int radiusPct) {
        m_wallpaperDim = std::clamp(dim, 0, 100);
        m_vignette = std::clamp(vignette, 0, 100);
        m_uiRadiusPct = std::clamp(radiusPct, 50, 200);
    }
    void setMotionStyleState(int style) { m_motionStyle = std::clamp(style, 0, 2); }
    void setCarouselFadeState(int fade) { m_carouselFade = std::clamp(fade, 0, 100); }
    void setTitlePillNudgeState(int nudgeX, int nudgeY) {
        m_titlePillNudgeX = std::clamp(nudgeX, -25, 25);
        m_titlePillNudgeY = std::clamp(nudgeY, -25, 25);
    }
    void setGameTitleVisibilityState(bool show) { m_showGameTitle = show; }
    void setSteamGridDbAspectState(int aspect) {
        m_steamGridDbAspect = std::clamp(aspect, 0, 3);
    }
    void onSteamGridDbViewFlagsChange(std::function<void(bool, bool, bool)> cb) {
        m_steamGridDbViewFlagsCb = std::move(cb);
    }
    void onSteamGridDbPosterInLineChange(BoolCb cb) {
        m_steamGridDbPosterInLineCb = std::move(cb);
    }
    void onSteamGridDbLogoInLineChange(BoolCb cb) {
        m_steamGridDbLogoInLineCb = std::move(cb);
    }
    void onSteamGridDbApiKeyRequest(VoidCb cb) { m_steamGridDbApiKeyCb = std::move(cb); }
    void onSteamGridDbScrapeRequest(VoidCb cb) { m_steamGridDbScrapeCb = std::move(cb); }
    void onControllerPairing(VoidCb cb) { m_controllerPairingCb = std::move(cb); }
    void onControllerRemapping(VoidCb cb) { m_controllerRemappingCb = std::move(cb); }
    void onControllerTest(VoidCb cb) { m_controllerTestCb = std::move(cb); }
    using SoftwareDeleteCb = std::function<void(uint64_t, const std::string&)>;
    void onSoftwareDelete(SoftwareDeleteCb cb) { m_softwareDeleteCb = std::move(cb); }
    void onSleepRequest(VoidCb cb)      { m_sleepCb = std::move(cb); }
    void onShutdownRequest(VoidCb cb)   { m_shutdownCb = std::move(cb); }
    void onRebootRequest(VoidCb cb)     { m_rebootCb = std::move(cb); }

    void setWireframeState(bool enabled) { m_wireframeEnabled = enabled; }
    void setGridLayoutState(int columns, int rows) {
        m_gridColumns = std::clamp(columns, 1, 8);
        m_gridRows = std::clamp(rows, 1, 5);
    }
    void setUiLanguageOverride(const std::string& tag) {
        m_uiLanguageOverride = tag.empty() ? "auto" : tag;
    }
    void setClockUse12HourState(bool enabled) {
        m_clockUse12Hour = enabled;
    }
    void setWebClockSyncState(bool enabled) {
        m_webClockSyncEnabled = enabled;
    }
    bool webClockSyncEnabled() const { return m_webClockSyncEnabled; }
    void setAccessibilityEnabledState(bool enabled) {
        m_accessibilityEnabled = enabled;
        setAccessibilityVoiceEnabled(enabled);
    }
    void setAccessibilitySpeechState(bool speakHints, bool speakContextEveryFocus,
                                     bool speakPosition, int speechRate) {
        m_accessibilitySpeakHints = speakHints;
        m_accessibilitySpeakContextEveryFocus = speakContextEveryFocus;
        m_accessibilitySpeakPosition = speakPosition;
        m_accessibilitySpeechRate = std::clamp(speechRate, 120, 320);
        setAccessibilitySpeechPreferences(speakHints, speakPosition);
    }
    void setSteamGridDbState(bool enabled, bool hasApiKey,
                             bool showInGrid = true,
                             bool showInDynamicLine = true,
                             bool showInFolders = true,
                             bool showPosterInDynamicLine = true,
                             bool showLogoInDynamicLine = true) {
        m_steamGridDbEnabled = enabled;
        m_steamGridDbHasApiKey = hasApiKey;
        m_steamGridDbShowInGrid = showInGrid;
        m_steamGridDbShowInDynamicLine = showInDynamicLine;
        m_steamGridDbShowInFolders = showInFolders;
        m_steamGridDbShowPosterInDynamicLine = showPosterInDynamicLine;
        m_steamGridDbShowLogoInDynamicLine = showLogoInDynamicLine;
    }
    void setSteamGridDbProgress(bool running, bool finished, int completed, int total,
                                int matched, int failed, const std::string& current,
                                const std::string& message) {
        m_steamGridDbRunning = running;
        m_steamGridDbFinished = finished;
        m_steamGridDbCompleted = completed;
        m_steamGridDbTotal = total;
        m_steamGridDbMatched = matched;
        m_steamGridDbFailed = failed;
        m_steamGridDbCurrent = current;
        m_steamGridDbMessage = message;
    }

protected:
    void buildTabs() override;
    void ensureTabLoaded(int tabIndex) override;
    void onContentUpdate(float dt) override;

private:
    friend class settings::tabs::SystemTab;
    friend class settings::tabs::AccessibilityTab;
    friend class settings::tabs::AudioTab;
    friend class settings::tabs::DisplayTab;
    friend class settings::tabs::InternetTab;
    friend class settings::tabs::SteamGridDbTab;
    friend class settings::tabs::ControllersTab;
    friend class settings::tabs::BluetoothTab;
    friend class settings::tabs::SleepTab;
    friend class settings::tabs::StorageTab;
    friend class settings::tabs::AboutTab;
    friend class settings::tabs::AppearanceTab;
    friend class settings::tabs::LayoutTab;
    friend class settings::tabs::TreatmentTab;
    friend class settings::tabs::ShortcutsTab;
    friend class settings::tabs::GlassTab;

    BoolCb m_wireframeCb;
    IntCb m_gridColumnsCb;
    IntCb m_gridRowsCb;
    StringCb m_uiLanguageCb;
    BoolCb m_clockUse12HourCb;
    BoolCb m_webClockSyncCb;
    ExternalToastCb m_externalToastCb;
    ClockSyncBootRetryCb m_clockSyncBootRetryCb;
    BoolCb m_accessibilityEnabledCb;
    BoolCb m_accessibilitySpeakHintsCb;
    BoolCb m_accessibilitySpeakContextEveryFocusCb;
    BoolCb m_accessibilitySpeakPositionCb;
    IntCb m_accessibilitySpeechRateCb;
    VoidCb m_netConnectCb;
    IntCb m_steamGridDbAspectCb;
    IntCb m_iconAspectCb;
    IntCb m_iconCornersCb;
    IntCb m_iconCornerPctCb;
    IntCb m_gridNudgeCb;
    IntCb m_lineNudgeCb;
    IntCb m_iconScaleCb;
    IntCb m_sidebarScaleCb;
    IntCb m_heroBackdropResolutionCb;
    IntCb m_heroBackdropBlurCb;
    std::function<void(bool, const std::string&)> m_lockPadCodeCb;
    std::function<void(std::uint64_t)> m_restoreHiddenCb;
    IntCb m_sortModeCb;
    BoolCb m_alphaOrderCb;
    bool m_alphaOrder = false;
    BoolCb m_dockedOverridesCb;
    IntCb m_dockedIconCb;
    IntCb m_dockedShortcutCb;
    bool m_dockedOverrides = true;
    int m_dockedIconScale = 100;
    int m_dockedShortcutScale = 100;
    int m_sortMode = 0;
    std::vector<std::pair<std::uint64_t, std::string>> m_hiddenGames;
    IntCb m_shortcutEdgeCb;
    IntCb m_shortcutNudgeXCb;
    IntCb m_shortcutNudgeXCbRight;
    BoolCb m_shortcutStripCb;
    IntCb m_pinSideCb;
    bool m_shortcutStrip = true;
    BoolCb m_hideActionHintsCb;
    IntCb m_hudScaleCb;
    bool m_hideActionHints = false;
    int m_hudScale = 100;
    int m_clockScale = 100;
    int m_clockNudgeX = 0;
    int m_clockNudgeY = 0;
    IntCb m_clockNudgeYCb;
    IntCb m_clockScaleCb;
    IntCb m_clockNudgeXCb;
    int m_shortcutNudgeY = 0;
    IntCb m_shortcutNudgeYCb;
    IntCb m_iconSpacingCb;
    IntCb m_carouselDepthCb;
    IntCb m_lineNudgeXCb;
    IntCb m_saveLayoutPresetCb;
    IntCb m_loadLayoutPresetCb;
    VoidCb m_resetIconLayoutCb;
    VoidCb m_iconShadowCb;
    VoidCb m_iconOverlayCb;
    IntCb m_motionStyleCb;
    IntCb m_glassTintCb;
    BoolCb m_glassSolidCb;
    int m_glassTint = 0;
    bool m_glassSolid = false;
    IntCb m_highlightTintCb;
    int m_highlightTint = 0;
    IntCb m_carouselFadeCb;
    IntCb m_titlePillNudgeCb;
    IntCb m_titlePillNudgeYCb;
    BoolCb m_gameTitleVisibilityCb;
    IntCb m_wallpaperDimCb;
    IntCb m_vignetteCb;
    IntCb m_uiRadiusPctCb;
    VoidCb m_wallpaperCycleCb;
    BoolCb m_wallpaperAnimatedCb;
    IntCb m_wallpaperFpsCb;
    BoolCb m_backgroundDriftCb;
    IntCb m_backgroundDriftSpeedCb;
    bool m_backgroundDrift = false;
    int m_backgroundDriftSpeed = 35;
    BoolCb m_steamGridDbEnabledCb;
    std::function<void(bool, bool, bool)> m_steamGridDbViewFlagsCb;
    BoolCb m_steamGridDbPosterInLineCb;
    BoolCb m_steamGridDbLogoInLineCb;
    VoidCb m_steamGridDbApiKeyCb;
    VoidCb m_steamGridDbScrapeCb;
    VoidCb m_controllerPairingCb;
    VoidCb m_controllerRemappingCb;
    VoidCb m_controllerTestCb;
    SoftwareDeleteCb m_softwareDeleteCb;
    VoidCb m_sleepCb;
    VoidCb m_shutdownCb;
    VoidCb m_rebootCb;

    bool m_wireframeEnabled = false;
    int m_gridColumns = 5;
    int m_gridRows = 3;
    std::string m_uiLanguageOverride = "auto";
    bool m_clockUse12Hour = false;
    bool m_webClockSyncEnabled = false;
    bool m_clockSyncToastFailureOnly = false;
    bool m_accessibilityEnabled = true;
    bool m_accessibilitySpeakHints = true;
    bool m_accessibilitySpeakContextEveryFocus = false;
    bool m_accessibilitySpeakPosition = true;
    int m_accessibilitySpeechRate = 190;
    int m_steamGridDbAspect = 0;
    int m_iconAspect = 0;
    int m_iconCorners = 0;
    int m_iconCornerPct = 100;
    int m_wallpaperDim = 0;
    int m_vignette = 0;
    int m_uiRadiusPct = 100;
    int m_iconShadowOffset = 6;
    int m_iconShadowAlpha = 20;
    int m_iconVignette = 0;
    int m_iconHighlight = 0;
    int m_iconDepth = 35;
    int m_motionStyle = 1;
    int m_carouselFade = 50;
    int m_titlePillNudgeX = 0;
    int m_titlePillNudgeY = 0;
    bool m_showGameTitle = true;
    int m_gridNudgeX = 0;
    int m_lineNudgeY = 0;
    int m_iconScale = 100;
    int m_heroBackdropResolution = 0;
    int m_heroBackdropBlur = 0;
    int m_sidebarScale = 100;
    bool m_lockPadCodeEnabled = false;
    std::string m_lockPadCode = "UDLR";
    int m_shortcutEdge = 0;
    int m_pinSide = 0;
    int m_shortcutNudgeX = 0;
    int m_shortcutNudgeXRight = 0;
    int m_iconSpacing = 0;
    int m_carouselDepth = 0;
    int m_lineNudgeX = 0;
    std::string m_wallpaperPath;
    bool m_wallpaperAnimated = true;
    int m_wallpaperFps = 15;
    bool m_steamGridDbEnabled = true;
    bool m_steamGridDbShowInGrid = true;
    bool m_steamGridDbShowInDynamicLine = true;
    bool m_steamGridDbShowInFolders = true;
    bool m_steamGridDbShowPosterInDynamicLine = true;
    bool m_steamGridDbShowLogoInDynamicLine = true;
    bool m_steamGridDbHasApiKey = false;
    bool m_steamGridDbRunning = false;
    bool m_steamGridDbFinished = false;
    int m_steamGridDbCompleted = 0;
    int m_steamGridDbTotal = 0;
    int m_steamGridDbMatched = 0;
    int m_steamGridDbFailed = 0;
    std::string m_steamGridDbCurrent;
    std::string m_steamGridDbMessage;
    std::vector<bool> m_loadedTabs;
    std::vector<bool> m_loadingTabs;
    std::vector<std::future<Tab>> m_tabTasks;
    int m_nextPrefetchTab = 0;

    struct ClockSyncFetch {
        bool ok = false;
        std::uint64_t posixUtc = 0;
        std::string error;
    };
    std::future<ClockSyncFetch> m_clockSyncFuture;

    Tab buildTabNow(int tabIndex);
    Tab makeLoadingTab(int tabIndex) const;
    void startAsyncTabLoad(int tabIndex);
    void pollTabLoaders();
    void prefetchOneTab();
    void emitClockSyncToast(const std::string& msg, float holdSeconds = 2.5f);
};
