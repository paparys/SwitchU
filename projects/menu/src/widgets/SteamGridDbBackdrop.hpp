#pragma once

#include "core/AppLayoutMode.hpp"

#include <nxui/core/Animation.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/widgets/Widget.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <vector>

class SteamGridDbBackdrop final : public nxui::Widget {
public:
    SteamGridDbBackdrop(nxui::GpuDevice& gpu, nxui::Renderer& renderer,
                        nxui::ThreadPool* threadPool);

    void setEnabled(bool enabled);
    // Canvas the per-game hero is decoded at: 1280x720 (Full), 960x540
    // (Balanced) or 640x360 (Fast). Changing it drops the uploaded sets so the
    // next focus decodes at the new size.
    void setHeroCanvas(int width, int height);
    // Softens the per-game hero, 0..1. Applied on the decode worker, so the
    // uploaded pixels are already blurred and the render thread never pays for
    // it; changing it drops the sets so the next decode re-applies the amount.
    void setBlur(float strength);
    // Slow horizontal pan of the hero. The art is overscanned while drifting
    // so the pan never exposes an edge.
    void setDrift(bool enabled, int speedPercent);
    // Drops every artwork cache (uploaded sets, decoded images and the list of
    // titles known to have none) so the next showTitle() re-reads the card.
    // Needed whenever SteamGridDB settings change what is available.
    void invalidateArtworkCaches();
    // Destroy hero/logo MemBlocks (FT NeedHeadroom). invalidateArtworkCaches
    // alone keeps GPU textures alive for crossfade reuse.
    void releaseGpuArtwork();
    void setLayoutMode(AppLayoutMode mode) { m_layoutMode = mode; }
    // True when a hero texture is on-screen at meaningful opacity.
    bool hasDisplayedHero() const;
    // Average relative luminance of the current hero [0..1], or <0 if none.
    float displayedHeroLuminance() const;
    // Screen-space band the logo is centred in (single-row mode only).
    void setLogoArea(const nxui::Rect& area) { m_logoArea = area; }
    void setShowLogo(bool show) { m_showLogo = show; }
    void setPreloadTitles(std::vector<std::uint64_t> titleIds);
    // Pace neighbour hero/logo warm so return soft-land does not dump a burst
    // of 3.6MB SD reads the moment defer expires. 0 = as soon as free.
    void setPreloadMinInterval(float seconds);
    void setPreloadPaused(bool paused);
    bool isArtworkBusy() const;
    void showTitle(std::uint64_t titleId, bool forceReload = false);

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;

private:
    struct ArtworkSet {
        nxui::Texture hero;
        nxui::Texture logo;
        std::uint64_t titleId = 0;
        // Canvas this set's hero was decoded at. A reduced one is upgraded to
        // the configured canvas once the selection stops moving.
        int canvasW = 0;
        int canvasH = 0;
        bool hasHero = false;
        bool hasLogo = false;
        float heroLuminance = -1.f;
    };

    struct DecodedImage {
        std::vector<std::uint8_t> rgba;
        int width = 0;
        int height = 0;
    };

    struct DecodedArtwork {
        DecodedImage hero;
        DecodedImage logo;
        std::uint64_t titleId = 0;
        std::uint64_t generation = 0;
        long decodeMilliseconds = 0;
        float heroLuminance = -1.f;
    };

    struct DecodeState {
        DecodedArtwork artwork;
        std::atomic<bool> cancelled{false};
    };

    struct PendingDecode {
        std::shared_ptr<DecodeState> state;
        std::future<void> future;
    };

    static DecodedImage decodeImage(const std::string& path, int outputWidth,
                                    int outputHeight, bool fill,
                                    const std::atomic<bool>* cancel = nullptr,
                                    bool useCache = true);
    static float sampleLuminance(const DecodedImage& image);
    static nxui::Rect fillRect(const nxui::Texture& texture, const nxui::Rect& area);
    static nxui::Rect containRect(const nxui::Texture& texture, const nxui::Rect& area);
    bool useFullHeroOpacity() const;
    void drawSet(nxui::Renderer& renderer, const ArtworkSet& set, float alpha) const;
    void startPendingDecode(std::uint64_t titleId, int canvasW, int canvasH,
                            bool useCache);
    void cancelPendingDecode();
    void beginCrossfade(int nextSet, std::uint64_t titleId);
    bool hasGpuArtwork(std::uint64_t titleId) const;

    nxui::GpuDevice& m_gpu;
    nxui::Renderer& m_renderer;
    nxui::ThreadPool* m_threadPool = nullptr;
    std::array<ArtworkSet, 2> m_sets;
    int m_current = 0;
    bool m_enabled = true;
    std::uint64_t m_requestedTitleId = 0;
    std::uint64_t m_requestGeneration = 0;
    std::uint64_t m_appliedGeneration = 0;
    float m_decodeDebounce = 0.f;
    std::optional<PendingDecode> m_pendingDecode;
    std::optional<DecodedArtwork> m_readyArtwork;
    std::vector<std::uint64_t> m_preloadTitleIds;
    std::vector<std::uint64_t> m_missingArtworkTitleIds;
    std::vector<DecodedArtwork> m_decodedCache;
    int m_uploadStage = 0;
    bool m_waitForGpuBeforeUpload = false;
    // Cruise neighbour warm (~3.5 MiB hero reads). 0 = as soon as free (too
    // aggressive on SD while browsing single-row with heroes).
    float m_preloadMinInterval = 0.35f;
    float m_preloadGapSec = 0.f;
    bool m_preloadPaused = false;
    int m_heroCanvasW = 1280;
    int m_heroCanvasH = 720;
    float m_heroBlur = 0.f;
    bool m_driftEnabled = false;
    int m_driftSpeed = 35;
    float m_driftPhase = 0.f;
    // Seconds the current selection has been on screen. Short means the user is
    // scrolling, so the next hero is decoded at the preview canvas.
    float m_sinceFocusChange = 10.f;
    int m_nextCanvasW = 1280;
    int m_nextCanvasH = 720;
    bool m_nextUseCache = true;
    AppLayoutMode m_layoutMode = AppLayoutMode::Grid;
    // Band the logo is centred in while single-row mode is active. The app
    // raises it above the folder title bubble when a folder is open.
    nxui::Rect m_logoArea{370.f, 149.f, 540.f, 150.f};
    bool m_showLogo = true;
    bool m_showPreviousDuringCrossfade = false;
    nxui::AnimatedFloat m_fade{1.f};
    nxui::AnimatedFloat m_artworkOpacity{1.f};
    // One prefetched neighbour at 1280x720 (~3.5 MiB) instead of two at
    // 640x360. The two uploaded slots plus this entry are the whole budget.
    static constexpr std::size_t kDecodedCacheLimit = 1;
    static constexpr std::size_t kMissingCacheLimit = 64;
};
