#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/Theme.hpp>
#include "core/GridModel.hpp"
#include "widgets/FolderStyleDraw.hpp"
#include "sidebar/SidebarAnimation.hpp"
#include <array>
#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <future>

namespace nxui { class Font; class ThreadPool; }
struct WidgetGifDecodeState;



class GlossyIcon : public nxui::GlassWidget {
public:
    GlossyIcon();
    ~GlossyIcon() override;

    void setTitle(const std::string& t) {
        if (t == m_title) return;
        m_title = t;
        m_titleOverlayValid = false;
    }
    const std::string& title() const    { return m_title; }

    void setTexture(nxui::Texture* tex) { m_tex = tex; }
    nxui::Texture* texture() const      { return m_tex; }

    // Single-row only: tall 2:3 frame (apps with poster art, or folders when
    // posters are enabled). Grid view ignores this.
    void setPosterFrame(bool enabled) { m_posterFrame = enabled; }
    bool usesPosterFrame() const { return m_posterFrame; }

    // Shape of this tile's icon face. Width / height; 1 = square.
    void setAspectRatio(float ratio) {
        m_aspectRatio = std::clamp(ratio, 0.2f, 4.f);
    }
    float aspectRatio() const { return m_aspectRatio; }

    void setGameCardTexture(nxui::Texture* tex) { m_gameCardTex = tex; }
    nxui::Texture* gameCardTexture() const      { return m_gameCardTex; }

    void setTitleId(uint64_t id) {
        if (id == m_titleId) return;
        m_titleId = id;
        m_playtimeSourceValid = false;
    }
    uint64_t titleId() const      { return m_titleId; }

    void setSuspended(bool s)     { m_suspended = s; }
    bool isSuspended() const      { return m_suspended; }
    void setMotionPaused(bool paused) { m_motionPaused = paused; }
    // Motion-only LOD: neighbours skip their captions and extra shadow passes
    // while the carousel crosses tiles quickly. Resting frames keep full detail.
    void setScrollLowDetail(bool low) { m_scrollLowDetail = low; }
    // Drop shadow under the tile: offset in px, strength 0-1.
    void setShadow(float offsetY, float alpha) {
        m_shadowOffsetY = offsetY;
        m_shadowAlpha = alpha;
    }
    // Edge darkening drawn over the artwork, 0..1.
    void setVignette(float strength) {
        m_vignetteStrength = std::clamp(strength, 0.f, 1.f);
    }
    // Glass sheen + rim light drawn over the artwork, 0..1.
    void setHighlight(float strength) {
        m_highlightStrength = std::clamp(strength, 0.f, 1.f);
    }
    float m_shadowOffsetY = 6.f;
    float m_shadowAlpha = 0.20f;
    float m_vignetteStrength = 0.f;
    float m_highlightStrength = 0.f;
    bool motionPaused() const { return m_motionPaused; }

    void setIsGameCard(bool gc)     { m_isGameCard = gc; }
    bool isGameCard() const         { return m_isGameCard; }

    void setNotLaunchable(bool nl)  { m_notLaunchable = nl; }
    bool isNotLaunchable() const    { return m_notLaunchable; }
    void setLoadingColor(const nxui::Color& color) { m_loadingColor = color; }
    // Scales the bottom-right playtime badge for denser grids.
    void setPlaytimePillScale(float scale) {
        m_playtimePillScale = std::clamp(scale, 0.3f, 1.5f);
    }
    // All Apps shows playtime in the metadata strip instead of over the art.
    void setShowPlaytimeBadge(bool show) { m_showPlaytimeBadge = show; }
    // All Apps: draw the artwork edge-to-edge instead of inside an 8px glass rim.
    void setArtworkFullBleed(bool full) { m_artworkFullBleed = full; }
    // All Apps: caption the artwork with a small bottom-left title.
    void setTitleOverlay(bool show) { m_titleOverlay = show; }
    // All Apps: thin inner edge so bright art does not merge with the backdrop.
    void setInnerHairline(bool show) { m_innerHairline = show; }
    void setEntryKind(GridEntryKind kind) { m_entryKind = kind; }
    GridEntryKind entryKind() const { return m_entryKind; }
    void setFolderPreviewCount(int count) { m_folderPreviewCount = count; }
    void setFolderVisualSeed(std::uint32_t seed) { m_folderVisualSeed = seed; }
    void setFolderColorIndex(int index) { m_folderColorIndex = index; }
    void setFolderStyleIndex(int index) { m_folderStyleIndex = index; }
    void setFolderShowCover(bool show) { m_folderShowCover = show; }
    void setFolderCoverTexture(nxui::Texture* tex) { m_folderCover = tex; }
    nxui::Texture* folderCoverTexture() const { return m_folderCover; }
    void setFolderCoverTitleId(std::uint64_t id) { m_folderCoverTitleId = id; }
    void setFolderCoverIsPoster(bool isPoster) { m_folderCoverIsPoster = isPoster; }
    bool folderCoverIsPoster() const { return m_folderCoverIsPoster; }
    void setFolderPeekComposite(bool composite) { m_folderPeekComposite = composite; }
    bool folderPeekComposite() const { return m_folderPeekComposite; }
    void setFolderPeekTextures(const std::array<nxui::Texture*, switchu::folders::kFolderPeekMax>& peeks,
                               int count) {
        m_folderPeeks = peeks;
        m_folderPeekCount = std::clamp(count, 0, switchu::folders::kFolderPeekMax);
    }
    int folderPeekCount() const { return m_folderPeekCount; }
    void clearFolderPeeks() {
        m_folderPeeks.fill(nullptr);
        m_folderPeekCount = 0;
    }
    void setThemeMode(nxui::ThemeMode mode) { m_themeMode = mode; }
    void setFont(nxui::Font* font) {
        if (font == m_font) return;
        m_font = font;
        m_playtimeMeasureValid = false;
        m_titleOverlayValid = false;
    }  // folder tiles carry their name
    void setWidgetData(switchu::widgets::WidgetType type, int columns, int rows,
                       std::string primary, std::string secondary,
                       const std::string& assetPath,
                       nxui::GpuDevice* gpu, nxui::Renderer* renderer,
                       bool deferAssetLoad = false);
    bool hasWidgetImageAsset() const { return !m_widgetAssetPath.empty(); }
    const std::string& widgetImageAssetPath() const { return m_widgetAssetPath; }
    bool isWidgetImageAssetLoaded() const;
    bool widgetImageAssetLoadAttempted() const { return m_widgetAssetLoadAttempted; }
    bool loadWidgetImageAsset(nxui::GpuDevice& gpu, nxui::Renderer& renderer);
    bool startWidgetImageAssetLoad(nxui::ThreadPool& threadPool,
                                   nxui::GpuDevice& gpu,
                                   nxui::Renderer& renderer);
    bool pollWidgetImageAssetLoad(nxui::GpuDevice& gpu,
                                  nxui::Renderer& renderer);
    bool isWidgetImageAssetLoading() const;
    bool unloadWidgetImageAsset();
    void allowWidgetImageAssetRetry();
    void setWidgetHeader(std::string header) { m_widgetHeader = std::move(header); }
    void setWidgetGameAssets(std::uint64_t titleId,
                             const std::string& heroPath,
                             const std::string& logoPath,
                             const std::vector<std::uint8_t>& iconData,
                             nxui::GpuDevice* gpu, nxui::Renderer* renderer);
    void setWidgetGameTextures(std::uint64_t titleId,
                               nxui::Texture* hero,
                               nxui::Texture* logo,
                               nxui::Texture* gameIcon);
    switchu::widgets::WidgetType widgetType() const { return m_widgetType; }
    std::uint64_t widgetGameTitleId() const { return m_widgetGameTitleId; }
    void copyWidgetPresentationFrom(GlossyIcon& source);
    void setGridSpan(int columns, int rows) {
        m_widgetColumns = std::max(1, columns);
        m_widgetRows = std::max(1, rows);
    }
    void setConsoleBattery(int percentage, bool charging) {
        m_consoleBatteryPercent = std::clamp(percentage, 0, 100);
        m_consoleBatteryCharging = charging;
    }
    void setBatteryIconTextures(nxui::Texture* console,
                                nxui::Texture*,
                                nxui::Texture*) {
        m_batteryConsoleIcon = console;
    }
    void setWideGameTextures(nxui::Texture* hero, nxui::Texture* logo) {
        m_wideGameHero = hero;
        m_wideGameLogo = logo;
    }
    nxui::Texture* wideGameHero() const { return m_wideGameHero; }
    nxui::Texture* wideGameLogo() const { return m_wideGameLogo; }
    int gridSpanColumns() const { return m_widgetColumns; }
    int gridSpanRows() const { return m_widgetRows; }

    void startAppear(float delay);
    // Reverse of a tile appear: fly back into `target` while shrinking out.
    void startDisappear(const nxui::Rect& target, float delay, float dur);
    // Travel into the laid-out slot from `from` without scaling or fading.
    // Used when two tiles trade places and each one slides across the gap the
    // other left behind.
    void startGlideFrom(const nxui::Rect& from, float dur);
    void forceVisible();

    void setAppearOrigin(const nxui::Rect& origin) {
        m_appearOrigin = origin; m_hasAppearOrigin = true;
    }

    void setJiggle(bool on, float phaseSeed = 0.f);
    bool isJiggling() const { return m_jiggle; }

    void setFocusable(bool f) { m_focusable = f; }
    bool isFocusable() const override { return m_focusable; }
    void onFocusGained() override;
    void onFocusLost() override;
    /// Live focus pop applied at draw time (SelectionCursor must match this).
    float focusScaleValue() const { return m_focusScale.value(); }
    float focusScaleTarget() const { return m_focusScale.target(); }

protected:
    void onRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    // Vignette / highlight pass over a tile face, clamped to its rounded shape.
    void drawIconOverlays(nxui::Renderer& ren, const nxui::Rect& r, float radius);
    // Bottom-left caption used by the full-bleed All Apps grid.
    void drawTitleOverlay(nxui::Renderer& ren, const nxui::Rect& r, float radius,
                          bool reservePill);
    // One-shot light sweep across the tile face, fired when the tile takes
    // focus. Empty/continuation tiles have no face to light up, so they skip it.
    void drawShine(nxui::Renderer& ren, const nxui::Rect& r, float radius);

private:
    std::string m_title;
    nxui::Texture*    m_tex = nullptr;
    nxui::Texture*    m_gameCardTex = nullptr;
    nxui::Texture*    m_wideGameHero = nullptr;
    nxui::Texture*    m_wideGameLogo = nullptr;
    uint64_t    m_titleId = 0;
    bool        m_focused = false;
    bool        m_focusable = true;
    bool        m_suspended = false;
    bool        m_motionPaused = false;
    bool        m_isGameCard = false;
    bool        m_notLaunchable = false;
    bool        m_posterFrame = false;
    float       m_aspectRatio = 1.f;
    nxui::Color m_loadingColor = nxui::Color::white();
    float       m_playtimePillScale = 1.f;
    bool        m_showPlaytimeBadge = true;
    bool        m_artworkFullBleed = false;
    bool        m_titleOverlay = false;
    bool        m_innerHairline = false;
    float       m_suspendPulse = 0.f;
    // Seconds spent without artwork; drives the short loading shimmer.
    float       m_skeletonT = 0.f;
    int         m_consoleBatteryPercent = 0;
    bool        m_consoleBatteryCharging = false;
    nxui::Texture* m_batteryConsoleIcon = nullptr;

    nxui::AnimatedFloat m_animScale;
    nxui::AnimatedFloat m_appearOpacity;
    nxui::AnimatedFloat m_focusScale;
    nxui::AnimatedFloat m_focusGlow;
    // Progress of the focus shine, 0..1; negative means idle.
    float         m_shineT      = -1.f;
    nxui::AnimatedFloat m_jiggleAmount;
    bool          m_jiggle      = false;
    float         m_jigglePhase = 0.f;
    float         m_jiggleSeed  = 0.f;
    float         m_appearDelay = 0.f;
    float         m_appearTimer = 0.f;
    bool          m_appearing   = false;
    nxui::Rect    m_appearOrigin{};
    bool          m_hasAppearOrigin = false;
    bool          m_disappearing = false;
    GridEntryKind m_entryKind = GridEntryKind::Application;
    int           m_folderPreviewCount = 0;
    std::uint32_t m_folderVisualSeed = 0;
    int           m_folderColorIndex = 0;
    int           m_folderStyleIndex = 0;
    bool          m_folderShowCover = false;
    nxui::Texture* m_folderCover = nullptr;
    bool          m_folderCoverIsPoster = false;
    bool          m_folderPeekComposite = false;
    std::array<nxui::Texture*, switchu::folders::kFolderPeekMax> m_folderPeeks{};
    int           m_folderPeekCount = 0;
    std::uint64_t m_folderCoverTitleId = 0;
    nxui::ThemeMode m_themeMode = nxui::ThemeMode::Dark;
    nxui::Font*   m_font = nullptr;
    // Playtime label is rebuilt only when the value changes: it was an
    // allocation and a text measure for every visible tile, every frame.
    std::string m_playtimeLabel;
    std::uint64_t m_playtimeSeconds = 0;
    std::uint64_t m_playtimeSourceSeconds = 0;
    float m_playtimeRefresh = 0.f;
    bool m_playtimeSourceValid = false;
    float m_playtimeTextWidth = 0.f;
    float m_playtimeTextHeight = 0.f;
    bool m_playtimeMeasureValid = false;
    // The All Apps caption is ellipsized against the tile width. Keep the
    // finished label so a wall of captions does not re-measure every frame.
    std::string m_titleOverlayLabel;
    float m_titleOverlayMaxW = -1.f;
    float m_titleOverlayScale = -1.f;
    float m_titleOverlayTextH = 0.f;
    bool m_titleOverlayValid = false;
    // Set for small or faded tiles, which skip the soft shadow and the fine
    // edge detail. The silhouette is unchanged; only those extra passes are.
    bool m_lowDetail = false;
    bool m_scrollLowDetail = false;
    switchu::widgets::WidgetType m_widgetType = switchu::widgets::WidgetType::Clock;
    int m_widgetColumns = 1;
    int m_widgetRows = 1;
    std::string m_widgetPrimary;
    std::string m_widgetSecondary;
    std::string m_widgetHeader;
    std::string m_widgetAssetPath;
    bool m_widgetAssetLoadAttempted = false;
    std::shared_ptr<WidgetGifDecodeState> m_widgetGifDecode;
    std::future<void> m_widgetGifDecodeFuture;
    std::size_t m_widgetGifUploadIndex = 0;
    std::unique_ptr<nxui::Texture> m_widgetTexture;
    nxui::Texture* m_widgetExternalTexture = nullptr;
    std::unique_ptr<nxui::Texture> m_widgetHeroTexture;
    std::unique_ptr<nxui::Texture> m_widgetLogoTexture;
    std::unique_ptr<nxui::Texture> m_widgetGameIconTexture;
    nxui::Texture* m_widgetHero = nullptr;
    nxui::Texture* m_widgetLogo = nullptr;
    nxui::Texture* m_widgetGameIcon = nullptr;
    std::uint64_t m_widgetGameTitleId = 0;
    SidebarAnimation m_widgetAnimation;
};
