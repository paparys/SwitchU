#pragma once

#include "TabbedOverlayScreen.hpp"
#include "core/AppLayoutMode.hpp"
#include <cstdint>
#include <functional>
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
class StyleTab;
class AppearanceTab;
class CarouselMovementTab;
class BackgroundsTab;
class ShortcutsTab;
class AllAppsTab;
class OpacityTab;
}

class SettingsScreen : public TabbedOverlayScreen {
public:
    struct LockProfileState {
        std::string uid;
        std::string nickname;
        bool enabled = false;
        std::string code = "UDLR";
    };

    // Read-only view of the display path that is actually presenting.
    struct DockedOutputState {
        int width = 1280;
        int height = 720;
        float textScale = 1.f;
        bool docked = false;
    };
    using DockedOutputProvider = std::function<DockedOutputState()>;

    SettingsScreen();
    ~SettingsScreen() override = default;

    void onWireframeChange(BoolCb cb)   { m_wireframeCb = std::move(cb); }
    void onHomeLayoutChange(std::function<void(HomeLayoutPreset)> cb) {
        m_homeLayoutCb = std::move(cb);
    }
    void onGridColumnsChange(IntCb cb)  { m_gridColumnsCb = std::move(cb); }
    void onGridRowsChange(IntCb cb)     { m_gridRowsCb = std::move(cb); }
    void onAllAppsColumnsChange(IntCb cb) { m_allAppsColumnsCb = std::move(cb); }
    void onAllAppsRowsChange(IntCb cb)    { m_allAppsRowsCb = std::move(cb); }
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
    void onSteamGridDbShowHeroesChange(BoolCb cb) {
        m_steamGridDbShowHeroesCb = std::move(cb);
    }
    void onSteamGridDbAspectChange(IntCb cb) { m_steamGridDbAspectCb = std::move(cb); }
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
    void onDockedCarouselXChange(IntCb cb) { m_dockedCarouselXCb = std::move(cb); }
    void onDockedCarouselYChange(IntCb cb) { m_dockedCarouselYCb = std::move(cb); }
    void setDockedState(bool overrides, int iconScale, int shortcutScale) {
        m_dockedOverrides = overrides;
        m_dockedIconScale = std::clamp(iconScale, 70, 130);
        m_dockedShortcutScale = std::clamp(shortcutScale, 70, 150);
    }
    void setDockedCarouselState(int x, int y) {
        m_dockedCarouselX = std::clamp(x, -25, 25);
        m_dockedCarouselY = std::clamp(y, -25, 25);
    }
    // The provider reads the live framebuffer instead of keeping a stale copy
    // pushed only at settings creation and dock/undock transitions.
    void setDockedOutputProvider(DockedOutputProvider provider) {
        m_dockedOutputProvider = std::move(provider);
    }
    const DockedOutputState& dockedOutputState() const {
        if (m_dockedOutputProvider)
            m_dockedOutput = m_dockedOutputProvider();
        return m_dockedOutput;
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
    int dockedCarouselX() const { return m_dockedCarouselX; }
    int dockedCarouselY() const { return m_dockedCarouselY; }
    void setDockedIconScale(int v) { m_dockedIconScale = std::clamp(v, 70, 130); }
    void setDockedShortcutScale(int v) { m_dockedShortcutScale = std::clamp(v, 70, 150); }
    void setDockedCarouselX(int v) { m_dockedCarouselX = std::clamp(v, -25, 25); }
    void setDockedCarouselY(int v) { m_dockedCarouselY = std::clamp(v, -25, 25); }
    void notifyDockedOverrides(bool on) { if (m_dockedOverridesCb) m_dockedOverridesCb(on); }
    void notifyDockedIcon(int v) { if (m_dockedIconCb) m_dockedIconCb(v); }
    void notifyDockedShortcut(int v) { if (m_dockedShortcutCb) m_dockedShortcutCb(v); }
    void notifyDockedCarouselX(int v) { if (m_dockedCarouselXCb) m_dockedCarouselXCb(v); }
    void notifyDockedCarouselY(int v) { if (m_dockedCarouselYCb) m_dockedCarouselYCb(v); }
    using LockPadCodeCb = std::function<void(const std::string&, bool,
                                                const std::string&)>;
    void onLockPadCodeChange(LockPadCodeCb cb) {
        m_lockPadCodeCb = std::move(cb);
    }
    void setLockProfilesState(std::vector<LockProfileState> profiles);
    void refreshLockProfilesTab();
    void setLockPadProfileIndex(int index);
    /** Account whose code the editor opens on until the user picks a row. */
    void setLockPadPreferredProfile(const std::string& uid) {
        m_lockPadPreferredUid = uid;
    }
    void requestLockPadProfileRefresh() { m_lockProfileRefreshPending = true; }
    const std::vector<LockProfileState>& lockProfiles() const { return m_lockProfiles; }
    int lockPadProfileIndex() const { return m_lockPadProfileIndex; }
    bool lockPadCodeEnabled() const {
        const auto* profile = selectedLockProfile();
        return profile && profile->enabled;
    }
    const std::string& lockPadCode() const {
        static const std::string kDefaultCode = "UDLR";
        const auto* profile = selectedLockProfile();
        return profile ? profile->code : kDefaultCode;
    }
    void setLockPadCodeEnabledState(bool enabled) {
        if (auto* profile = selectedLockProfile())
            profile->enabled = enabled;
    }
    void setLockPadCodeStep(int index, char step) {
        if (auto* profile = selectedLockProfile(); profile && index >= 0 &&
            index < static_cast<int>(profile->code.size()))
            profile->code[static_cast<std::size_t>(index)] = step;
    }
    void notifyLockPadCodeChange() {
        auto* profile = selectedLockProfile();
        if (m_lockPadCodeCb && profile)
            m_lockPadCodeCb(profile->uid, profile->enabled, profile->code);
    }
    LockProfileState* selectedLockProfile();
    const LockProfileState* selectedLockProfile() const;
    void onShortcutEdgeChange(IntCb cb) { m_shortcutEdgeCb = std::move(cb); }
    void onShortcutNudgeChange(IntCb cb) { m_shortcutNudgeXCb = std::move(cb); }
    void onShortcutNudgeRightChange(IntCb cb) {
        m_shortcutNudgeXCbRight = std::move(cb);
    }
    void onShortcutStripChange(BoolCb cb) { m_shortcutStripCb = std::move(cb); }
    void onShortcutGradientChange(BoolCb cb) { m_shortcutGradientCb = std::move(cb); }
    void onShortcutGradientColorAChange(IntCb cb) {
        m_shortcutGradientACb = std::move(cb);
    }
    void onShortcutGradientColorBChange(IntCb cb) {
        m_shortcutGradientBCb = std::move(cb);
    }
    void onShortcutFlushTopChange(BoolCb cb) { m_shortcutFlushTopCb = std::move(cb); }
    void onShortcutNudgeYChange(IntCb cb) { m_shortcutNudgeYCb = std::move(cb); }
    void onIconSpacingChange(IntCb cb) { m_iconSpacingCb = std::move(cb); }
    void onCarouselShrinkChange(IntCb cb) { m_carouselShrinkCb = std::move(cb); }
    void onLineNudgeXChange(IntCb cb) { m_lineNudgeXCb = std::move(cb); }
    void onSaveLayoutPreset(IntCb cb) { m_saveLayoutPresetCb = std::move(cb); }
    void onLoadLayoutPreset(IntCb cb) { m_loadLayoutPresetCb = std::move(cb); }
    void onResetIconLayout(VoidCb cb) { m_resetIconLayoutCb = std::move(cb); }
    void onWallpaperDimChange(IntCb cb) { m_wallpaperDimCb = std::move(cb); }
    void onVignetteChange(IntCb cb) { m_vignetteCb = std::move(cb); }
    void onUiRadiusPctChange(IntCb cb) { m_uiRadiusPctCb = std::move(cb); }
    void onStyleChanged(VoidCb cb) { m_styleChangedCb = std::move(cb); }
    void notifyStyleChanged() { if (m_styleChangedCb) m_styleChangedCb(); }
    void onSaveLookPreset(IntCb cb) { m_saveLookPresetCb = std::move(cb); }
    void onLoadLookPreset(IntCb cb) { m_loadLookPresetCb = std::move(cb); }
    void onRenameLookPreset(std::function<void(int, const std::string&)> cb) {
        m_renameLookPresetCb = std::move(cb);
    }
    void requestLookPresetRename(int slot);
    void applyLookPresetName(int slot, const std::string& name);
    void onIconShadowChange(VoidCb cb) { m_iconShadowCb = std::move(cb); }
    void onIconOverlayChange(VoidCb cb) { m_iconOverlayCb = std::move(cb); }
    void onMotionStyleChange(IntCb cb) { m_motionStyleCb = std::move(cb); }
    void onCarouselScrollSpeedChange(IntCb cb) {
        m_carouselScrollSpeedCb = std::move(cb);
    }
    void onOpacityTintChange(IntCb cb) { m_opacityTintCb = std::move(cb); }
    void onOpacitySolidChange(BoolCb cb) { m_opacitySolidCb = std::move(cb); }
    void onHighlightTintChange(IntCb cb) { m_highlightTintCb = std::move(cb); }
    void onHighlightGradientColorAChange(IntCb cb) { m_highlightGradientACb = std::move(cb); }
    void onHighlightGradientColorBChange(IntCb cb) { m_highlightGradientBCb = std::move(cb); }
    void onHighlightThicknessChange(IntCb cb) { m_highlightThicknessCb = std::move(cb); }
    void setHighlightTint(int tint) { m_highlightTint = std::clamp(tint, 0, 10); }
    void setHighlightGradientState(int a, int b, int thicknessPx) {
        m_highlightGradientA = std::clamp(a, 1, 9);
        m_highlightGradientB = std::clamp(b, 1, 9);
        m_highlightThicknessPx = std::clamp(thicknessPx, 1, 8);
    }
    void setOpacityState(int tint, bool solid) {
        m_opacityTint = std::clamp(tint, 0, 9);
        m_opacitySolid = solid;
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
    void onWallpaperCycle(VoidCb cb) { m_wallpaperCycleCb = std::move(cb); }
    void onWallpaperPicker(VoidCb cb) { m_wallpaperPickerCb = std::move(cb); }
    void notifyWallpaperPicker() { if (m_wallpaperPickerCb) m_wallpaperPickerCb(); }
    void onWallpaperAnimatedChange(BoolCb cb) { m_wallpaperAnimatedCb = std::move(cb); }
    void onWallpaperFpsChange(IntCb cb) { m_wallpaperFpsCb = std::move(cb); }
    const std::string& wallpaperPath() const { return m_wallpaperPath; }
    bool wallpaperAnimated() const { return m_wallpaperAnimated; }
    int wallpaperFps() const { return m_wallpaperFps; }
    void setWallpaperAnimated(bool on) { m_wallpaperAnimated = on; }
    void notifyWallpaperAnimated(bool on) {
        if (m_wallpaperAnimatedCb) m_wallpaperAnimatedCb(on);
    }
    void setWallpaperFps(int fps) { m_wallpaperFps = std::clamp(fps, 5, 30); }
    void notifyWallpaperFps(int fps) {
        if (m_wallpaperFpsCb) m_wallpaperFpsCb(fps);
    }
    void onBackgroundDriftChange(BoolCb cb) { m_backgroundDriftCb = std::move(cb); }
    void onBackgroundDriftSpeedChange(IntCb cb) { m_backgroundDriftSpeedCb = std::move(cb); }
    void setIconPresentationState(int corners, int gridNudge, int lineNudge,
                                  int iconScale = 100, int iconSpacing = 0,
                                  int carouselShrink = 50, int lineNudgeX = 0,
                                  int iconVignette = 0, int iconHighlight = 0) {
        m_iconCorners = std::clamp(corners, 0, 1);
        m_gridNudgeX = std::clamp(gridNudge, -25, 25);
        m_lineNudgeY = std::clamp(lineNudge, -25, 25);
        m_iconScale = std::clamp(iconScale, 70, 130);
        m_iconSpacing = std::clamp(iconSpacing, -50, 100);
        m_carouselShrink = std::clamp(carouselShrink, 0, 100);
        m_lineNudgeX = std::clamp(lineNudgeX, -25, 25);
        m_iconVignette = std::clamp(iconVignette, 0, 100);
        m_iconHighlight = std::clamp(iconHighlight, 0, 100);
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
    void setBackgroundShapesState(bool on) { m_backgroundShapes = on; }
    bool backgroundShapes() const { return m_backgroundShapes; }
    void setBackgroundShapes(bool on) { m_backgroundShapes = on; }
    void setHeroFadeInState(int ms) { m_heroFadeInMs = std::clamp(ms, 0, 10000); }
    void setHeroFadeInMs(int ms) { m_heroFadeInMs = std::clamp(ms, 0, 10000); }
    int heroFadeInMs() const { return m_heroFadeInMs; }
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
    void notifyBackgroundShapes(bool on) {
        if (m_backgroundShapesCb) m_backgroundShapesCb(on);
    }
    void notifyHeroFadeIn(int ms) {
        if (m_heroFadeInMsCb) m_heroFadeInMsCb(ms);
    }
    // Settings keeps its own shell: a persistent header that names the tab and
    // explains the focused row, flat single-line rows, and a footer with the
    // controller hints.
    float overlayHeaderHeight() const override { return 116.f; }
    void drawOverlayHeader(nxui::Renderer&, const nxui::Rect&, float) override;
    float overlayFooterHeight() const override { return 34.f; }
    void drawOverlayFooter(nxui::Renderer&, const nxui::Rect&, float) override;
    bool showsRowDescriptions() const override { return false; }

    void onBackgroundShapesChange(BoolCb cb) { m_backgroundShapesCb = std::move(cb); }
    void onHeroFadeInChange(IntCb cb) { m_heroFadeInMsCb = std::move(cb); }
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
    void setShortcutLayoutState(int edge, int nudgeLeft, int nudgeRight) {
        m_shortcutEdge = std::clamp(edge, 0, 1);
        m_shortcutNudgeX = std::clamp(nudgeLeft, -25, 25);
        m_shortcutNudgeXRight = std::clamp(nudgeRight, -25, 25);
    }
    void setShortcutStripState(bool on) { m_shortcutStrip = on; }
    void setShortcutGradientState(bool on, int a, int b) {
        m_shortcutGradient = on;
        m_shortcutGradientA = std::clamp(a, 1, 9);
        m_shortcutGradientB = std::clamp(b, 1, 9);
    }
    void setShortcutFlushTopState(bool on) { m_shortcutFlushTop = on; }
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
    int panelTintMix() const { return m_panelTintMix; }
    int panelBorderPct() const { return m_panelBorderPct; }
    int panelGrainPct() const { return m_panelGrainPct; }
    int panelShadowPct() const { return m_panelShadowPct; }
    int focusRingPx() const { return m_focusRingPx; }
    int focusGlowPct() const { return m_focusGlowPct; }
    int focusLiftPct() const { return m_focusLiftPct; }
    int focusOvershootPct() const { return m_focusOvershootPct; }
    int titleScalePct() const { return m_titleScalePct; }
    int titleCornerStyle() const { return m_titleCornerStyle; }
    int titleBackgroundPct() const { return m_titleBackgroundPct; }
    int hudGapPx() const { return m_hudGapPx; }
    bool showHudClock() const { return m_showHudClock; }
    bool showHudDate() const { return m_showHudDate; }
    bool showHudBattery() const { return m_showHudBattery; }
    bool showHudProfile() const { return m_showHudProfile; }
    int wallpaperBlurPx() const { return m_wallpaperBlurPx; }
    int wallpaperSaturationPct() const { return m_wallpaperSaturationPct; }
    int wallpaperSharpnessPct() const { return m_wallpaperSharpnessPct; }
    int wallpaperVibrancePct() const { return m_wallpaperVibrancePct; }
    int wallpaperNoisePct() const { return m_wallpaperNoisePct; }
    int backdropTintPct() const { return m_backdropTintPct; }
    int pageSlideMs() const { return m_pageSlideMs; }
    void setStyleState(int panelTint, int panelBorder, int panelGrain, int panelShadow,
                       int ringPx, int glowPct, int liftPct, int overshootPct,
                       int titleScale, int titleCorner, int titleBg, int hudGap,
                       bool clock, bool date, bool battery, bool profile,
                       int blurPx, int saturationPct, int sharpnessPct,
                       int vibrancePct, int noisePct, int tintPct, int slideMs) {
        m_panelTintMix = std::clamp(panelTint, 0, 100);
        m_panelBorderPct = std::clamp(panelBorder, 0, 200);
        m_panelGrainPct = std::clamp(panelGrain, 0, 100);
        m_panelShadowPct = std::clamp(panelShadow, 0, 100);
        m_focusRingPx = std::clamp(ringPx, 1, 6);
        m_focusGlowPct = std::clamp(glowPct, 0, 100);
        m_focusLiftPct = std::clamp(liftPct, 0, 100);
        m_focusOvershootPct = std::clamp(overshootPct, 0, 100);
        m_titleScalePct = std::clamp(titleScale, 80, 120);
        m_titleCornerStyle = std::clamp(titleCorner, 0, 2);
        m_titleBackgroundPct = std::clamp(titleBg, 0, 100);
        m_hudGapPx = std::clamp(hudGap, 0, 24);
        m_showHudClock = clock;
        m_showHudDate = date;
        m_showHudBattery = battery;
        m_showHudProfile = profile;
        m_wallpaperBlurPx = std::clamp(blurPx, 0, 16);
        m_wallpaperSaturationPct = std::clamp(saturationPct, 0, 150);
        m_wallpaperSharpnessPct = std::clamp(sharpnessPct, 0, 100);
        m_wallpaperVibrancePct = std::clamp(vibrancePct, 0, 100);
        m_wallpaperNoisePct = std::clamp(noisePct, 0, 100);
        m_backdropTintPct = std::clamp(tintPct, 0, 100);
        m_pageSlideMs = std::clamp(slideMs, 150, 600);
    }
    void setLookPresetState(int slot, const std::string& name, bool filled) {
        if (slot < 0 || slot > 2)
            return;
        const std::string fallback = "Look " + std::to_string(slot + 1);
        m_lookPresetNames[slot] = (name.empty() || name == "Look") ? fallback : name;
        m_lookPresetFilled[slot] = filled;
    }
    void setMotionStyleState(int style) { m_motionStyle = std::clamp(style, 0, 2); }
    void setCarouselScrollSpeedState(int speed) {
        m_carouselScrollSpeed = std::clamp(speed, 0, 100);
    }
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
    void setHomeLayoutState(HomeLayoutPreset preset) { m_homeLayout = preset; }
    void setGridLayoutState(int columns, int rows) {
        m_gridColumns = std::clamp(columns, 1, 8);
        m_gridRows = std::clamp(rows, 1, 5);
    }
    void setAllAppsLayoutState(int columns, int rows) {
        m_allAppsColumns = std::clamp(columns, 3, 8);
        m_allAppsRows = std::clamp(rows, 2, 5);
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
                             bool showLogoInDynamicLine = true,
                             bool showHeroes = true) {
        m_steamGridDbEnabled = enabled;
        m_steamGridDbHasApiKey = hasApiKey;
        m_steamGridDbShowInGrid = showInGrid;
        m_steamGridDbShowInDynamicLine = showInDynamicLine;
        m_steamGridDbShowInFolders = showInFolders;
        m_steamGridDbShowPosterInDynamicLine = showPosterInDynamicLine;
        m_steamGridDbShowLogoInDynamicLine = showLogoInDynamicLine;
        m_steamGridDbShowHeroes = showHeroes;
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
    friend class settings::tabs::CarouselMovementTab;
    friend class settings::tabs::LayoutTab;
    friend class settings::tabs::TreatmentTab;
    friend class settings::tabs::StyleTab;
    friend class settings::tabs::ShortcutsTab;
    friend class settings::tabs::AllAppsTab;
    friend class settings::tabs::OpacityTab;

    BoolCb m_wireframeCb;
    std::function<void(HomeLayoutPreset)> m_homeLayoutCb;
    IntCb m_gridColumnsCb;
    IntCb m_gridRowsCb;
    IntCb m_allAppsColumnsCb;
    IntCb m_allAppsRowsCb;
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
    IntCb m_iconCornersCb;
    IntCb m_iconCornerPctCb;
    IntCb m_gridNudgeCb;
    IntCb m_lineNudgeCb;
    IntCb m_iconScaleCb;
    IntCb m_sidebarScaleCb;
    IntCb m_heroBackdropResolutionCb;
    IntCb m_heroBackdropBlurCb;
    LockPadCodeCb m_lockPadCodeCb;
    std::function<void(std::uint64_t)> m_restoreHiddenCb;
    IntCb m_sortModeCb;
    BoolCb m_alphaOrderCb;
    bool m_alphaOrder = false;
    BoolCb m_dockedOverridesCb;
    IntCb m_dockedIconCb;
    IntCb m_dockedShortcutCb;
    IntCb m_dockedCarouselXCb;
    IntCb m_dockedCarouselYCb;
    bool m_dockedOverrides = true;
    int m_dockedIconScale = 100;
    int m_dockedShortcutScale = 100;
    int m_dockedCarouselX = 0;
    int m_dockedCarouselY = 0;
    mutable DockedOutputState m_dockedOutput;
    DockedOutputProvider m_dockedOutputProvider;
    int m_sortMode = 0;
    std::vector<std::pair<std::uint64_t, std::string>> m_hiddenGames;
    IntCb m_shortcutEdgeCb;
    IntCb m_shortcutNudgeXCb;
    IntCb m_shortcutNudgeXCbRight;
    BoolCb m_shortcutStripCb;
    BoolCb m_shortcutFlushTopCb;
    bool m_shortcutStrip = true;
    BoolCb m_shortcutGradientCb;
    IntCb m_shortcutGradientACb;
    IntCb m_shortcutGradientBCb;
    bool m_shortcutGradient = true;
    int m_shortcutGradientA = 1;
    int m_shortcutGradientB = 9;
    bool m_shortcutFlushTop = false;
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
    IntCb m_carouselShrinkCb;
    IntCb m_lineNudgeXCb;
    IntCb m_saveLayoutPresetCb;
    IntCb m_loadLayoutPresetCb;
    VoidCb m_resetIconLayoutCb;
    VoidCb m_iconShadowCb;
    VoidCb m_iconOverlayCb;
    IntCb m_motionStyleCb;
    IntCb m_carouselScrollSpeedCb;
    IntCb m_opacityTintCb;
    BoolCb m_opacitySolidCb;
    int m_opacityTint = 0;
    bool m_opacitySolid = false;
    IntCb m_highlightTintCb;
    IntCb m_highlightGradientACb;
    IntCb m_highlightGradientBCb;
    IntCb m_highlightThicknessCb;
    int m_highlightTint = 0;
    int m_highlightGradientA = 1;
    int m_highlightGradientB = 9;
    int m_highlightThicknessPx = 3;
    IntCb m_carouselFadeCb;
    IntCb m_titlePillNudgeCb;
    IntCb m_titlePillNudgeYCb;
    BoolCb m_gameTitleVisibilityCb;
    IntCb m_wallpaperDimCb;
    IntCb m_vignetteCb;
    IntCb m_uiRadiusPctCb;
    VoidCb m_styleChangedCb;
    IntCb m_saveLookPresetCb;
    IntCb m_loadLookPresetCb;
    std::function<void(int, const std::string&)> m_renameLookPresetCb;
    int m_lookPresetRow[3] {-1, -1, -1};
    int m_lookRenameRow[3] {-1, -1, -1};
    VoidCb m_wallpaperCycleCb;
    VoidCb m_wallpaperPickerCb;
    BoolCb m_wallpaperAnimatedCb;
    IntCb m_wallpaperFpsCb;
    BoolCb m_backgroundDriftCb;
    IntCb m_backgroundDriftSpeedCb;
    BoolCb m_backgroundShapesCb;
    IntCb m_heroFadeInMsCb;
    bool m_backgroundDrift = false;
    int m_backgroundDriftSpeed = 35;
    bool m_backgroundShapes = true;
    int m_heroFadeInMs = 3000;
    BoolCb m_steamGridDbEnabledCb;
    BoolCb m_steamGridDbShowHeroesCb;
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
    HomeLayoutPreset m_homeLayout = HomeLayoutPreset::Carousel;
    int m_gridColumns = 5;
    int m_gridRows = 3;
    int m_allAppsColumns = 6;
    int m_allAppsRows = 3;
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
    int m_iconCorners = 0;
    int m_iconCornerPct = 100;
    int m_wallpaperDim = 0;
    int m_vignette = 0;
    int m_uiRadiusPct = 100;
    int m_panelTintMix = 0;
    int m_panelBorderPct = 100;
    int m_panelGrainPct = 0;
    int m_panelShadowPct = 0;
    int m_focusRingPx = 3;
    int m_focusGlowPct = 100;
    int m_focusLiftPct = 100;
    int m_focusOvershootPct = 0;
    int m_titleScalePct = 100;
    int m_titleCornerStyle = 0;
    int m_titleBackgroundPct = 100;
    int m_hudGapPx = 0;
    bool m_showHudClock = true;
    bool m_showHudDate = true;
    bool m_showHudBattery = true;
    bool m_showHudProfile = true;
    int m_wallpaperBlurPx = 0;
    int m_wallpaperSaturationPct = 100;
    int m_wallpaperSharpnessPct = 0;
    int m_wallpaperVibrancePct = 0;
    int m_wallpaperNoisePct = 0;
    int m_backdropTintPct = 0;
    int m_pageSlideMs = 340;
    std::string m_lookPresetNames[3] {"Look 1", "Look 2", "Look 3"};
    bool m_lookPresetFilled[3] {false, false, false};
    int m_iconShadowOffset = 6;
    int m_iconShadowAlpha = 20;
    int m_iconVignette = 0;
    int m_iconHighlight = 0;
    int m_motionStyle = 1;
    int m_carouselScrollSpeed = 50;
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
    std::vector<LockProfileState> m_lockProfiles;
    int m_lockPadProfileIndex = 0;
    std::string m_lockPadPreferredUid;
    bool m_lockPadUserChoseProfile = false;
    bool m_lockProfileRefreshPending = false;
    bool m_lockProfilesTabDirty = false;
    int m_shortcutEdge = 0;
    int m_shortcutNudgeX = 0;
    int m_shortcutNudgeXRight = 0;
    int m_iconSpacing = 0;
    int m_carouselShrink = 50;
    int m_lineNudgeX = 0;
    std::string m_wallpaperPath;
    bool m_wallpaperAnimated = true;
    int m_wallpaperFps = 15;
    bool m_steamGridDbEnabled = true;
    bool m_steamGridDbShowHeroes = true;
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
    // The header and footer draw every frame; their i18n strings only change
    // on a language switch, which rebuilds the tabs.
    std::string m_headerTitleText;
    std::string m_footerHintsText;
    int m_footerHintsKey = -1;
    std::string m_footerAutoSaveText;
    std::vector<bool> m_loadedTabs;
    std::vector<bool> m_loadingTabs;
    std::vector<std::future<Tab>> m_tabTasks;
    int m_nextPrefetchTab = 0;
    // Quiet period after a tab switch before a neighbour is built in the
    // background, so the switch animation is not competing with a build.
    float m_tabPrefetchDelay = 0.f;
    int m_prefetchAnchor = -1;

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
