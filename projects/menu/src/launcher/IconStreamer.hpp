#pragma once

#include <chrono>
#include <nxui/core/Texture.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <future>
#include <atomic>
#include <list>
#include <unordered_map>

class GlossyIcon;

// Streams icon textures on demand based on the currently visible page.
// Loaded textures are kept for a small window around the current page, so
// nearby navigation is instant without letting GPU memory grow unbounded.
class IconStreamer {
public:
    using IconDataLoader = std::function<std::vector<uint8_t>(uint64_t titleId)>;
    // Reusable-buffer variant: fills `out` and returns false when the title has
    // no art. Decode workers keep `out` alive per thread, so streaming a page
    // stops allocating a fresh ~128 KB control-data buffer per icon.
    using IconDataLoaderInto = std::function<bool(uint64_t titleId,
                                                  std::vector<uint8_t>& out)>;

    struct DecodedIcon {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
    };

    // CPU-only helper. Safe on a ThreadPool worker; upload stays on the UI thread.
    // `poster` keeps room for a 2× single-row poster (320×480); square art is
    // scaled to kIconSize on the CPU so the GPU uploads and samples a much
    // smaller texture.
    static DecodedIcon decodeIconData(const std::vector<uint8_t>& data,
                                      bool poster = false);

    // Prepare icon metadata for all apps after the app list has been fetched.
    void init(int appCount);
    void setIconDataLoader(IconDataLoader loader);
    void setIconDataLoaderInto(IconDataLoaderInto loader);
    /// Per-title poster preference (e.g. 2x3 grid tiles) layered on top of the
    /// global single-row poster flag. Queried during decode scheduling.
    using PreferPosterQuery = std::function<bool(std::uint64_t)>;
    void setPreferPosterQuery(PreferPosterQuery query) {
        m_preferPosterQuery = std::move(query);
    }
    // Prefer tall Poster files vs square icons. When the preference flips,
    // compressed bytes are dropped and loaded GPU textures are marked stale so
    // the next onPageChanged() can swap art in place without blanking tiles.
    // Returns true when the preference actually changed.
    bool setPreferPosterArt(bool preferPoster);
    bool preferPosterArt() const { return m_preferPosterArt; }
    void setThreadPool(nxui::ThreadPool* pool) { m_threadPool = pool; }
    void setTitleId(int appIndex, uint64_t titleId);
    void setIconData(int appIndex, std::vector<uint8_t> compressed);
    void resize(int appCount);
    void setPinnedIndex(int appIndex);
    void clearPinnedIndex();

    // While a page slide or carousel spring is moving, full posters are held
    // off the frame path. The streamer requests small proxy art instead so
    // incoming tiles fill cheaply; complete posters swap in once the row stops.
    void setScrollActive(bool active);

    // Call when the visible page changes (or on first display).
    // Decodes + uploads textures for the new visible range and evicts
    // textures that are no longer needed.  Updates GlossyIcon texture
    // pointers directly.
    void onPageChanged(int currentPage, int iconsPerPage,
                       nxui::GpuDevice& gpu, nxui::Renderer& ren,
                       const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Force-reload textures for the current page (e.g. after a theme change
    // that resets the GPU pool).
    void forceReload(int currentPage, int iconsPerPage,
                     nxui::GpuDevice& gpu, nxui::Renderer& ren,
                     const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Reload one title in place (for an icon override) without destroying the
    // complete GPU pool or stalling on unrelated textures.
    void reloadTitle(uint64_t titleId, int currentPage, int iconsPerPage,
                     nxui::GpuDevice& gpu, nxui::Renderer& ren,
                     const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    /// Drop cached compressed bytes for one title so the next page stream
    /// re-reads via the icon data loader (e.g. 2x3 grid tiles switching to poster).
    void invalidateTitleArt(uint64_t titleId);

    // Temporarily allow more decodes and uploads per frame, e.g. right after a
    // fast HOME return so the visible page fills in at once instead of one icon
    // per frame. Reverts to the normal limits by itself.
    void startBurst(float seconds);

    // Update the fast window from the live row speed. Separate enter/exit
    // thresholds keep a speed hovering near the boundary from resizing the
    // lookahead every frame, and the wider window lingers briefly while the
    // row settles so it does not shrink on the exact frame the drag ends.
    void updateFastCarouselDrag(bool dynamicLine, float speed);
    bool fastCarouselDrag() const { return m_fastCarouselDrag; }

    // Very fast carousel drags need a deeper window and more decode-ahead;
    // both return to the normal budget as soon as the row slows down.
    void setFastCarouselDrag(bool fast);

    // After splash handoff: keep single-row neighbor warming tight, then widen
    // gradually so radius does not jump 2→4 in one frame. Reverts by itself.
    void startSoftLanding(float tightSeconds, int tightRadius = 2,
                          float widenSeconds = 0.f, int widenRadius = 3);
    bool softLandingActive() const { return inSoftLanding(); }

    // Cap neighbor page radius during a Frame Tap headroom reclaim.
    // Pass <0 to clear the cap (restore normal kPageCacheRadius behavior).
    void setPageCacheRadiusCap(int radius);
    int pageCacheRadiusCap() const { return m_pageCacheRadiusCap; }

    // Destroy GPU textures on free pool slots. onPageChanged normally keeps
    // them for reuse; Frame Tap needs the bytes returned to process free.
    void releaseUnusedGpuMemory();
    // Retire at most maxCount free-slot MemBlocks (1/frame idle drip).
    int releaseUnusedGpuMemoryDrip(int maxCount = 1);
    // Freed slots keep their texture so a page that comes back is instant.
    // Keeping more than one page of them means the high-water mark of a long
    // scroll stays resident, so page changes trim the oldest excess.
    // maxRetire caps how many are retired in this call (<0 = no cap) and the
    // return value is the number retired.
    int trimIdleTextures(int maxIdle, int maxRetire = -1);
    // Quiet-menu target: while set, the idle drip only retires free textures
    // beyond maxIdle instead of draining every free slot. Cleared by page
    // changes and by hard reclaims.
    void requestIdleTrim(int maxIdle) {
        m_idleHoldTarget = maxIdle < 0 ? 0 : maxIdle;
    }

    // Hard reclaim: destroy every pool texture except keepAppIndex (−1 = none).
    // Launch/resume path — soft eviction left ~25 MiB bound (soak 2026-10-27).
    void releaseAllGpuExcept(int keepAppIndex);

    // Off-screen art prefetch for layout transitions: decode destination art
    // (poster or icon) by titleId without touching textures already shown on
    // tiles. pumpShadowPrefetch() uploads a few GPU textures per frame so the
    // loader animation keeps running; installShadowPrefetch() only attaches.
    void cancelShadowPrefetch();
    void startShadowPrefetch(bool preferPoster,
                             const std::vector<std::uint64_t>& titleIds,
                             IconDataLoader loader);
    void pumpShadowPrefetch(nxui::GpuDevice& gpu, nxui::Renderer& ren);
    bool shadowPrefetchReady() const;
    void installShadowPrefetch(
        const std::vector<std::shared_ptr<GlossyIcon>>& allIcons);

    // Release everything (textures + compressed data).
    void clear();
    void cancelPending();
    // Drop in-flight decodes except one title (keep focused art during morph).
    void cancelPendingExcept(std::uint64_t keepTitleId);

    // Keep internal compressed data and loaded-slot mappings aligned when
    // app entries are swapped in the grid model.
    bool swapIndices(int a, int b);

    // Update indices after a catalogue refresh while retaining textures for
    // titles that still exist. This avoids tearing down the complete GPU icon
    // cache just because one application was installed or removed.
    void reconcileTitleIds(const std::vector<uint64_t>& titleIds);
    void reconcileCatalog(IconStreamer&& catalog);

    int  iconCount()         const { return (int)m_appToSlot.size(); }
    bool hasData(int index)  const;
    // Compressed bytes a title still holds. Null once the icon has been
    // uploaded and released, so callers fall back to the SD cache.
    const std::vector<uint8_t>* compressedIcon(std::uint64_t titleId) const {
        const std::size_t count = std::min(m_titleIds.size(), m_compressed.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (m_titleIds[i] == titleId && !m_compressed[i].empty())
                return &m_compressed[i];
        }
        return nullptr;
    }
    bool needsVisibleLoads(int currentPage, int iconsPerPage) const;

    // Soak inventory: bound slots vs free-with-MemBlock vs empty free.
    struct PoolInventory {
        int slots = 0;
        int bound = 0;
        int freeWithTex = 0;
        int freeEmpty = 0;
        std::size_t compressedBytes = 0;
        // GPU bytes the pool holds, split by art kind: portrait textures are
        // the single-row posters, everything else is a tile icon. Idle slots
        // keep their texture for reuse, so they are counted as well.
        std::size_t textureBytes = 0;
        int icons = 0;
        std::size_t iconBytes = 0;
        int posters = 0;
        std::size_t posterBytes = 0;
        int held = 0;
        std::size_t heldBytes = 0;
        int pendingDecodes = 0;
    };
    PoolInventory poolInventory() const;

private:
    struct DecodeState {
        DecodedIcon decoded;
        bool failed = false;
        std::atomic<bool> cancelled{false};
        // Filled by the worker once decoded; read by the scheduler to budget
        // the CPU-side RGBA held by the decode-ahead queue.
        std::atomic<std::size_t> decodedBytes{0};
    };

    struct PendingDecode {
        uint64_t titleId = 0;
        // Resolved once at schedule time: the per-frame cancel/sort/partition
        // passes used to search m_titleIds inside their comparators.
        int appIndex = -1;
        std::shared_ptr<DecodeState> state;
        std::future<void> future;
        // Wall-clock time the decode was requested; used by fast-carousel
        // telemetry to report request-to-upload latency.
        std::chrono::steady_clock::time_point requestedAt{};
        // Poster/icon worst case, used until the worker reports actual bytes.
        std::size_t estimatedBytes = 0;
        // True when this request wants tall Poster art. Only square icons enter
        // the decoded RAM cache; posters already have their fit cache.
        bool poster = false;
        // True when this is a small scroll-time stand-in decoded from the same
        // art. It uploads cheaply now and is marked stale when motion stops so
        // the full poster can replace it.
        bool proxy = false;
    };

    // Small LRU of CPU-decoded square icons. The GPU pool keeps one page of
    // textures warm; without this, stepping farther away and back re-opened the
    // SD file and re-ran the JPEG/PNG decode even though the pixels never
    // changed. Capped so this never becomes a library-sized allocation.
    struct DecodedIconCache {
        static constexpr std::size_t kMaxBytes = 4u * 1024u * 1024u;
        static constexpr std::size_t kMaxEntries = 48;

        struct Entry {
            std::uint64_t titleId = 0;
            DecodedIcon icon;
        };

        std::list<Entry> order; // front = most recently used
        std::unordered_map<std::uint64_t, std::list<Entry>::iterator> index;
        std::size_t bytes = 0;

        const DecodedIcon* get(std::uint64_t titleId);
        void put(std::uint64_t titleId, DecodedIcon icon);
        void erase(std::uint64_t titleId);
        void clear();
    };

    // UI-thread only: lookup happens while scheduling and insertion happens
    // after a completed decode is consumed by onPageChanged().
    DecodedIconCache m_decodedCache;
    void rememberDecodedIcon(std::uint64_t titleId, bool poster, DecodeState& state);

    struct FastCarouselTelemetry {
        bool active = false;
        std::chrono::steady_clock::time_point started{};
        std::uint32_t pageChanges = 0;
        std::uint32_t visibleMisses = 0;
        std::uint32_t uploads = 0;
        std::uint64_t waitMicros = 0;
        std::uint64_t waitMaxMicros = 0;
        std::uint64_t uploadMicros = 0;
        std::uint64_t uploadMaxMicros = 0;
        std::size_t peakDecodeBytes = 0;
        std::size_t peakPending = 0;
    };

    // Transient compressed JPEG/PNG bytes, normally only used for prefetched
    // data. App icons are otherwise fetched on demand and released after upload.
    std::vector<std::vector<uint8_t>> m_compressed;
    std::vector<uint64_t> m_titleIds;
    // Per-app: GPU slot still shows the previous art kind and needs a swap-in.
    std::vector<uint8_t> m_artStale;
    // Per-app: current texture is a low-res scroll proxy, not final poster art.
    std::vector<uint8_t> m_artProxy;
    bool m_preferPosterArt = false;
    PreferPosterQuery m_preferPosterQuery;
    IconDataLoader m_iconLoader;
    IconDataLoaderInto m_iconLoaderInto;
    nxui::ThreadPool* m_threadPool = nullptr;
    std::vector<PendingDecode> m_pendingDecodes;
    // Scheduling order scratch, kept so the per-frame pump does not allocate.
    std::vector<int> m_loadOrder;
    std::vector<uint64_t> m_failedTitleIds;

    struct ShadowPrefetchEntry {
        std::uint64_t titleId = 0;
        DecodedIcon decoded;
        std::unique_ptr<nxui::Texture> texture;
        bool failed = false;
        bool decodedReady = false;
        bool uploaded = false;
        std::shared_ptr<DecodeState> state;
        std::future<void> future;
    };
    std::vector<ShadowPrefetchEntry> m_shadowPrefetch;
    bool m_shadowPreferPoster = false;
    static constexpr int kShadowUploadsPerPump = 1;

    void softInvalidateArt();
    bool isArtStale(int appIndex) const;
    std::size_t pendingDecodeBytes() const;
    int appIndexFor(const PendingDecode& pending) const;
    // Bounded negative cache, so failed titles cannot grow without limit.
    void recordFailedTitle(std::uint64_t titleId);
    void beginFastCarouselTelemetry();
    void endFastCarouselTelemetry();
    void noteFastCarouselDecodePeak();

    // Pool of reusable GPU textures.
    struct TexSlot {
        nxui::Texture texture;
        int appIndex = -1;   // which app currently occupies this slot (-1 = free)
    };
    std::vector<std::unique_ptr<TexSlot>> m_pool;

    // Maps app index → pool slot index (-1 = not loaded).
    std::vector<int> m_appToSlot;

    // Indices of free pool slots.
    std::vector<int> m_freeSlots;
    // Idle hold target (-1 = unset, drip frees every free slot).
    int m_idleHoldTarget = -1;

    int m_lastPage = -1;
    int m_lastIconsPerPage = -1;
    // App-index window the pool was last evicted for. Invalidated by -2 so the
    // first page change always evicts and trims.
    int m_lastEvictStart = -2;
    int m_lastEvictEnd = -2;
    bool m_scrollActive = false;
    bool m_fastCarouselDrag = false;
    std::chrono::steady_clock::time_point m_fastCarouselLingerUntil{};
    FastCarouselTelemetry m_fastCarouselTelemetry;
    int m_pinnedIndex = -1;

    // Single-row keeps four titles warm ahead of the cursor, widening to six
    // while the carousel is moving fast. Moving rows stream cheap proxy art,
    // so this window only has to hide the SD read/decode latency; grid uses its
    // own page window below.
    static constexpr int kPageCacheRadius = 4;
    static constexpr int kGridPageCacheRadius = 1;
    static constexpr int kFastPageCacheRadius = 6;
    // Base decode cap per side; portraits may use up to 2× / 3× (see decodeIconData).
    static constexpr int kIconSize        = 160;
    // Icon-sized uploads no longer drain the GPU (Texture defers the previous
    // image/slot instead), so two can share a frame while the page fills. A
    // large upload still ends the frame's budget in onPageChanged().
    static constexpr int kUploadsPerFrame = 2;
    // CPU decode-ahead. The GPU takes two uploads per frame, so holding more
    // completed/pending decodes than that keeps both workers busy and makes the
    // upload slots always find art ready instead of waiting on a decode.
    static constexpr int kMaxPendingDecodes = 16;
    static constexpr int kBurstUploadsPerFrame = 4;
    static constexpr int kBurstMaxPendingDecodes = 20;
    static constexpr int kFastMaxPendingDecodes = 24;
    // CPU RGBA budget for decodes that have not been uploaded yet. Posters are
    // ~0.6 MB each, so the entry cap alone could pin several megabytes of
    // decoded pixels; this keeps the queue under ~8 MB normally and ~10 MB
    // during a very fast carousel drag.
    static constexpr std::size_t kDecodeAheadBytes = 8u * 1024u * 1024u;
    static constexpr std::size_t kFastDecodeAheadBytes = 10u * 1024u * 1024u;
    std::chrono::steady_clock::time_point m_burstUntil{};
    bool inBurst() const {
        return m_fastCarouselDrag ||
               std::chrono::steady_clock::now() < m_burstUntil;
    }
    int pendingDecodeLimit() const {
        if (m_fastCarouselDrag)
            return kFastMaxPendingDecodes;
        return inBurst() ? kBurstMaxPendingDecodes : kMaxPendingDecodes;
    }
    std::size_t decodeAheadBytes() const {
        return m_fastCarouselDrag ? kFastDecodeAheadBytes : kDecodeAheadBytes;
    }
    std::chrono::steady_clock::time_point m_softLandingUntil{};
    std::chrono::steady_clock::time_point m_softLandingWidenUntil{};
    int m_softLandingRadius = kPageCacheRadius;
    int m_softLandingWidenRadius = kPageCacheRadius;
    bool inSoftLanding() const {
        const auto now = std::chrono::steady_clock::now();
        return now < m_softLandingUntil || now < m_softLandingWidenUntil;
    }
    bool fastCarouselWindow() const {
        return m_fastCarouselDrag ||
               std::chrono::steady_clock::now() < m_fastCarouselLingerUntil;
    }
    int pageCacheRadius() const {
        const auto now = std::chrono::steady_clock::now();
        const bool fastWindow = fastCarouselWindow();
        auto clampRadius = [this, fastWindow](int radius) {
            if (radius <= 0)
                return 0;
            const int maxRadius = fastWindow
                ? kFastPageCacheRadius : kPageCacheRadius;
            int capped = radius < maxRadius ? radius : maxRadius;
            if (m_pageCacheRadiusCap >= 0 && capped > m_pageCacheRadiusCap)
                capped = m_pageCacheRadiusCap;
            return capped;
        };
        if (fastWindow)
            return clampRadius(kFastPageCacheRadius);
        if (now < m_softLandingUntil)
            return clampRadius(m_softLandingRadius);
        if (now < m_softLandingWidenUntil)
            return clampRadius(m_softLandingWidenRadius);
        return clampRadius(kPageCacheRadius);
    }
    int m_pageCacheRadiusCap = -1;
    static constexpr std::uint64_t kGpuImageReserve = 4u * 1024u * 1024u;
};
