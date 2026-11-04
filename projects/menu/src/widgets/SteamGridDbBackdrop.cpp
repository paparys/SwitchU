#include "SteamGridDbBackdrop.hpp"

#include "steamgriddb/ArtworkCache.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"
#include "core/DebugLog.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

// Cheap existence probe for the decoded hero at the backdrop's canvas. When it
// is already on the card the worker only has to read it back, so the focus
// debounce can be skipped and the hero shows a couple of frames sooner.
// Canvas a fast-scroll preview is decoded at. Decoding straight from the
// source at this size beats reading the full cache off the SD card, and the
// result is replaced by the configured canvas once the selection settles.
constexpr int kPreviewCanvasW = 960;
constexpr int kPreviewCanvasH = 540;

// Hero blur runs on the decode worker, so a slider change never costs the
// render thread a frame. The image is box-averaged down to a quarter, blurred
// there with three sliding-window passes and scaled back up: a quarter canvas
// is 1/16th of the pixels per pass, and the result is indistinguishable from a
// full-size blur because it is soft to begin with.
void blurRgba(std::vector<std::uint8_t>& rgba, int width, int height, float strength) {
    if (strength <= 0.001f || width < 8 || height < 8)
        return;
    if (rgba.size() != static_cast<std::size_t>(width) * height * 4u)
        return;

    const int sw = std::max(4, width / 4);
    const int sh = std::max(4, height / 4);
    std::vector<std::uint8_t> small(static_cast<std::size_t>(sw) * sh * 4u);
    for (int y = 0; y < sh; ++y) {
        const int y0 = y * height / sh;
        const int y1 = std::max(y0 + 1, (y + 1) * height / sh);
        for (int x = 0; x < sw; ++x) {
            const int x0 = x * width / sw;
            const int x1 = std::max(x0 + 1, (x + 1) * width / sw);
            std::uint32_t acc[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy) {
                const std::uint8_t* row = rgba.data()
                    + static_cast<std::size_t>(sy) * width * 4u;
                for (int sx = x0; sx < x1; ++sx)
                    for (int c = 0; c < 4; ++c)
                        acc[c] += row[sx * 4 + c];
            }
            const std::uint32_t n = static_cast<std::uint32_t>((y1 - y0) * (x1 - x0));
            std::uint8_t* dst = small.data()
                + (static_cast<std::size_t>(y) * sw + x) * 4u;
            for (int c = 0; c < 4; ++c)
                dst[c] = static_cast<std::uint8_t>(acc[c] / n);
        }
    }

    // A single quarter-res box pass is already visible on its own, so the bottom
    // of the slider must not jump straight to it: the first fifth fades the
    // blurred result in against the sharp original, and only past that does the
    // radius grow. That keeps 1-15% reading as a gentle softening instead of
    // "blur switched on".
    const float t = std::clamp(strength, 0.f, 1.f);
    const float fade = std::clamp(t / 0.20f, 0.f, 1.f);
    const float growth = std::clamp((t - 0.15f) / 0.85f, 0.f, 1.f);
    const int radius = 1 + static_cast<int>(std::lround(growth * 8.f));
    const std::uint32_t count = static_cast<std::uint32_t>(radius * 2 + 1);
    std::vector<std::uint8_t> scratch(small.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < sh; ++y) {
            const std::uint8_t* src = small.data()
                + static_cast<std::size_t>(y) * sw * 4u;
            std::uint8_t* dst = scratch.data()
                + static_cast<std::size_t>(y) * sw * 4u;
            std::uint32_t sum[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const int x = std::clamp(i, 0, sw - 1);
                for (int c = 0; c < 4; ++c) sum[c] += src[x * 4 + c];
            }
            for (int x = 0; x < sw; ++x) {
                for (int c = 0; c < 4; ++c)
                    dst[x * 4 + c] = static_cast<std::uint8_t>(sum[c] / count);
                const int addX = std::min(x + radius + 1, sw - 1);
                const int subX = std::max(x - radius, 0);
                for (int c = 0; c < 4; ++c) {
                    sum[c] += src[addX * 4 + c];
                    sum[c] -= src[subX * 4 + c];
                }
            }
        }
        for (int x = 0; x < sw; ++x) {
            std::uint32_t sum[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const int y = std::clamp(i, 0, sh - 1);
                const std::uint8_t* px = scratch.data()
                    + (static_cast<std::size_t>(y) * sw + x) * 4u;
                for (int c = 0; c < 4; ++c) sum[c] += px[c];
            }
            for (int y = 0; y < sh; ++y) {
                std::uint8_t* dst = small.data()
                    + (static_cast<std::size_t>(y) * sw + x) * 4u;
                for (int c = 0; c < 4; ++c)
                    dst[c] = static_cast<std::uint8_t>(sum[c] / count);
                const int addY = std::min(y + radius + 1, sh - 1);
                const int subY = std::max(y - radius, 0);
                const std::uint8_t* addPx = scratch.data()
                    + (static_cast<std::size_t>(addY) * sw + x) * 4u;
                const std::uint8_t* subPx = scratch.data()
                    + (static_cast<std::size_t>(subY) * sw + x) * 4u;
                for (int c = 0; c < 4; ++c) {
                    sum[c] += addPx[c];
                    sum[c] -= subPx[c];
                }
            }
        }
    }

    for (int y = 0; y < height; ++y) {
        const float sy = (static_cast<float>(y) + 0.5f) * sh / height - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, sh - 1);
        const int y1 = std::min(y0 + 1, sh - 1);
        const float fy = std::clamp(sy - static_cast<float>(y0), 0.f, 1.f);
        std::uint8_t* dst = rgba.data() + static_cast<std::size_t>(y) * width * 4u;
        for (int x = 0; x < width; ++x) {
            const float sx = (static_cast<float>(x) + 0.5f) * sw / width - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, sw - 1);
            const int x1 = std::min(x0 + 1, sw - 1);
            const float fx = std::clamp(sx - static_cast<float>(x0), 0.f, 1.f);
            const std::uint8_t* p00 = small.data()
                + (static_cast<std::size_t>(y0) * sw + x0) * 4u;
            const std::uint8_t* p10 = small.data()
                + (static_cast<std::size_t>(y0) * sw + x1) * 4u;
            const std::uint8_t* p01 = small.data()
                + (static_cast<std::size_t>(y1) * sw + x0) * 4u;
            const std::uint8_t* p11 = small.data()
                + (static_cast<std::size_t>(y1) * sw + x1) * 4u;
            for (int c = 0; c < 4; ++c) {
                const float top = p00[c] + (p10[c] - p00[c]) * fx;
                const float bottom = p01[c] + (p11[c] - p01[c]) * fx;
                const float blurred = std::clamp(top + (bottom - top) * fy, 0.f, 255.f);
                // Read the untouched pixel before overwriting it, so the fade-in
                // always blends against the sharp hero, not against an earlier
                // channel's result.
                const float original = static_cast<float>(dst[x * 4 + c]);
                dst[x * 4 + c] = static_cast<std::uint8_t>(
                    original + (blurred - original) * fade + 0.5f);
            }
        }
    }
}

bool heroCachePresent(std::uint64_t titleId, int width, int height) {
    std::error_code ec;
    const std::string cache = SteamGridDbManager::heroPath(titleId) + "."
        + std::to_string(width) + "x" + std::to_string(height) + ".rgba-cache";
    return std::filesystem::is_regular_file(cache, ec);
}

} // namespace

SteamGridDbBackdrop::SteamGridDbBackdrop(nxui::GpuDevice& gpu,
                                         nxui::Renderer& renderer,
                                         nxui::ThreadPool* threadPool)
    : m_gpu(gpu), m_renderer(renderer), m_threadPool(threadPool) {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setFocusable(false);
    setFrameworkTouchEnabled(false);
    m_fade.setImmediate(1.f);
}

void SteamGridDbBackdrop::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled)
        cancelPendingDecode();
    setVisible(enabled);
}

void SteamGridDbBackdrop::setHeroCanvas(int width, int height) {
    const int w = std::clamp(width, 320, 1280);
    const int h = std::clamp(height, 180, 720);
    if (w == m_heroCanvasW && h == m_heroCanvasH)
        return;
    m_heroCanvasW = w;
    m_heroCanvasH = h;
    // Uploaded sets are the old size and the pending decode has the old
    // canvas baked in, so drop both and let the next focus rebuild.
    cancelPendingDecode();
    m_readyArtwork.reset();
    m_uploadStage = 0;
    invalidateArtworkCaches();
    DebugLog::log("[steamgriddb-ui] hero canvas %dx%d", m_heroCanvasW, m_heroCanvasH);
}

void SteamGridDbBackdrop::setBlur(float strength) {
    const float value = std::clamp(strength, 0.f, 1.f);
    if (std::abs(value - m_heroBlur) < 0.001f)
        return;
    m_heroBlur = value;
    // The disk cache holds unblurred pixels, so a new amount only invalidates
    // what is on the GPU: the next decode blurs again on the worker.
    cancelPendingDecode();
    m_readyArtwork.reset();
    m_uploadStage = 0;
    invalidateArtworkCaches();
    DebugLog::log("[steamgriddb-ui] hero blur %.2f", m_heroBlur);
}

void SteamGridDbBackdrop::setDrift(bool enabled, int speedPercent) {
    m_driftSpeed = std::clamp(speedPercent, 1, 100);
    if (enabled && !m_driftEnabled)
        m_driftPhase = 0.f;
    const bool changed = m_driftEnabled != enabled || m_driftSpeed != speedPercent;
    m_driftEnabled = enabled;
    if (changed)
        DebugLog::log("[steamgriddb-ui] drift enabled=%d speed=%d", enabled ? 1 : 0,
                      m_driftSpeed);
}

void SteamGridDbBackdrop::invalidateArtworkCaches() {
    // Forget which title each uploaded set belongs to. No texture is released
    // here: a settings change can land mid-frame while the GPU still reads these
    // images, and clearing the title id is enough to miss the gpu-cache test so
    // the next decode overwrites the slot through the upload path that already
    // waits on the fence. The decoded and "missing artwork" lists go too: a
    // SteamGridDB download is exactly what turns one of the latter into art.
    for (auto& set : m_sets)
        set.titleId = 0;
    m_decodedCache.clear();
    m_missingArtworkTitleIds.clear();
}

void SteamGridDbBackdrop::releaseGpuArtwork() {
    cancelPendingDecode();
    m_pendingDecode.reset();
    m_readyArtwork.reset();
    m_decodedCache.clear();
    m_missingArtworkTitleIds.clear();
    m_preloadTitleIds.clear();
    for (auto& set : m_sets) {
        set.hero = nxui::Texture{};
        set.logo = nxui::Texture{};
        set.titleId = 0;
        set.hasHero = false;
        set.hasLogo = false;
        set.heroLuminance = -1.f;
    }
    m_uploadStage = 0;
    m_waitForGpuBeforeUpload = false;
}

bool SteamGridDbBackdrop::hasGpuArtwork(std::uint64_t titleId) const {
    return std::any_of(m_sets.begin(), m_sets.end(), [titleId](const ArtworkSet& set) {
        return set.titleId == titleId && (set.hasHero || set.hasLogo);
    });
}

void SteamGridDbBackdrop::setPreloadTitles(std::vector<std::uint64_t> titleIds) {
    m_preloadTitleIds.clear();
    for (std::uint64_t titleId : titleIds) {
        if (titleId == 0 || titleId == m_requestedTitleId
            || std::find(m_preloadTitleIds.begin(), m_preloadTitleIds.end(), titleId)
                != m_preloadTitleIds.end())
            continue;
        m_preloadTitleIds.push_back(titleId);
        // Keep this in lockstep with the decoded-cache capacity. Queuing more
        // titles than can be retained makes the oldest entry get evicted and
        // immediately decoded again forever.
        if (m_preloadTitleIds.size() >= kDecodedCacheLimit)
            break;
    }
}

void SteamGridDbBackdrop::setPreloadMinInterval(float seconds) {
    m_preloadMinInterval = std::max(0.f, seconds);
}

void SteamGridDbBackdrop::setPreloadPaused(bool paused) {
    m_preloadPaused = paused;
}

bool SteamGridDbBackdrop::isArtworkBusy() const {
    return m_pendingDecode.has_value()
        || m_readyArtwork.has_value()
        || m_uploadStage > 0;
}

void SteamGridDbBackdrop::showTitle(std::uint64_t titleId, bool forceReload) {
    // Clearing (titleId == 0) must run even when disabled so folder close /
    // view toggles never leave a stale hero on screen.
    if (!m_enabled && titleId != 0) return;
    if (!forceReload && m_requestedTitleId == titleId) return;

    // Dwell of the title we are leaving. Short means the user is scrolling, and
    // the next hero is worth decoding small: the SD cache read is what makes a
    // 720p backdrop lag behind the selection.
    const float dwell = m_sinceFocusChange;
    m_sinceFocusChange = 0.f;

    // While scrolling, nothing on this path may touch the SD card: a stat() per
    // focus step on the render thread is what made fast scrolling hitch. The
    // worker verifies the files anyway, so skipping the probes only costs a
    // frame or two of the previous hero for titles that have no artwork.
    const bool scrolling = !forceReload && dwell < 0.22f;

    // A direct artwork-to-artwork change may crossfade from the current set.
    // Once focus has visited a title without artwork, the old set is stale and
    // must never be exposed again while the next image is decoded/uploaded.
    m_showPreviousDuringCrossfade = titleId != 0
        && m_sets[m_current].titleId == m_requestedTitleId
        && (m_sets[m_current].hasHero || m_sets[m_current].hasLogo)
        && m_artworkOpacity.value() > 0.002f;

    if (titleId == 0) {
        cancelPendingDecode();
        m_readyArtwork.reset();
        m_uploadStage = 0;
        m_requestedTitleId = 0;
        ++m_requestGeneration;
        m_appliedGeneration = m_requestGeneration;
        m_showPreviousDuringCrossfade = false;
        if (forceReload) {
            // Instant wipe (folder close) — no fade of the previous hero.
            for (auto& set : m_sets) {
                set.titleId = 0;
                set.hasHero = false;
                set.hasLogo = false;
                set.heroLuminance = -1.f;
            }
            m_artworkOpacity.setImmediate(0.f);
            m_fade.setImmediate(1.f);
        } else {
            m_artworkOpacity.set(0.f, 0.22f, nxui::Easing::outCubic);
        }
        DebugLog::log("[steamgriddb-ui] artwork cleared: selection has no title");
        return;
    }

    if (forceReload) {
        m_waitForGpuBeforeUpload = true;
        // Evict only transient UI state. The disk cache fingerprints its source,
        // so a replaced asset invalidates itself while a freshly prepared cache
        // remains available for this reload.
        m_decodedCache.erase(
            std::remove_if(m_decodedCache.begin(), m_decodedCache.end(),
                           [titleId](const auto& artwork) { return artwork.titleId == titleId; }),
            m_decodedCache.end());
        m_missingArtworkTitleIds.erase(
            std::remove(m_missingArtworkTitleIds.begin(), m_missingArtworkTitleIds.end(), titleId),
            m_missingArtworkTitleIds.end());
    }

    auto missingArtwork = std::find(m_missingArtworkTitleIds.begin(),
                                    m_missingArtworkTitleIds.end(), titleId);
    if (!scrolling && missingArtwork != m_missingArtworkTitleIds.end()
        && SteamGridDbManager::hasArtwork(titleId)) {
        // A SteamGridDB download may have completed since this title was
        // classified as missing. Revalidate the negative cache during the same
        // menu session instead of requiring a restart.
        m_missingArtworkTitleIds.erase(missingArtwork);
        missingArtwork = m_missingArtworkTitleIds.end();
        DebugLog::log("[steamgriddb-ui] artwork became available: title=%016llX",
                      static_cast<unsigned long long>(titleId));
    }

    if (!forceReload && missingArtwork != m_missingArtworkTitleIds.end()) {
        cancelPendingDecode();
        m_readyArtwork.reset();
        m_uploadStage = 0;
        m_requestedTitleId = titleId;
        ++m_requestGeneration;
        m_appliedGeneration = m_requestGeneration;
        m_showPreviousDuringCrossfade = false;
        m_artworkOpacity.set(0.f, 0.22f, nxui::Easing::outCubic);
        DebugLog::log("[steamgriddb-ui] artwork cleared: title=%016llX has no assets",
                      static_cast<unsigned long long>(titleId));
        return;
    }

    // A directory lookup is much cheaper than decoding an image and lets a
    // missing-artwork selection start fading immediately instead of retaining
    // the previous game's hero until the worker completes.
    if (!scrolling && !SteamGridDbManager::hasArtwork(titleId)) {
        cancelPendingDecode();
        m_readyArtwork.reset();
        m_uploadStage = 0;
        m_requestedTitleId = titleId;
        ++m_requestGeneration;
        m_appliedGeneration = m_requestGeneration;
        if (std::find(m_missingArtworkTitleIds.begin(), m_missingArtworkTitleIds.end(), titleId)
            == m_missingArtworkTitleIds.end()) {
            m_missingArtworkTitleIds.push_back(titleId);
            if (m_missingArtworkTitleIds.size() > kMissingCacheLimit)
                m_missingArtworkTitleIds.erase(m_missingArtworkTitleIds.begin());
        }
        m_showPreviousDuringCrossfade = false;
        m_artworkOpacity.set(0.f, 0.22f, nxui::Easing::outCubic);
        DebugLog::log("[steamgriddb-ui] artwork fading out: title=%016llX has no files",
                      static_cast<unsigned long long>(titleId));
        return;
    }

    if (!forceReload && titleId != 0) {
        for (int i = 0; i < static_cast<int>(m_sets.size()); ++i) {
            const auto& cached = m_sets[i];
            if (cached.titleId != titleId
                || (!cached.hasHero && !cached.hasLogo))
                continue;

            cancelPendingDecode();
            m_readyArtwork.reset();
            m_uploadStage = 0;
            m_requestedTitleId = titleId;
            ++m_requestGeneration;
            m_appliedGeneration = m_requestGeneration;
            if (i != m_current)
                beginCrossfade(i, titleId);
            else {
                m_fade.setImmediate(1.f);
                m_artworkOpacity.set(1.f, 0.18f, nxui::Easing::outCubic);
            }
            DebugLog::log("[steamgriddb-ui] gpu cache hit title=%016llX",
                          static_cast<unsigned long long>(titleId));
            return;
        }

        auto decoded = std::find_if(m_decodedCache.begin(), m_decodedCache.end(),
            [titleId](const auto& artwork) { return artwork.titleId == titleId; });
        if (decoded != m_decodedCache.end()) {
            cancelPendingDecode();
            m_requestedTitleId = titleId;
            ++m_requestGeneration;
            decoded->generation = m_requestGeneration;
            m_readyArtwork = std::move(*decoded);
            m_decodedCache.erase(decoded);
            m_uploadStage = 0;
            DebugLog::log("[steamgriddb-ui] decoded cache hit title=%016llX",
                          static_cast<unsigned long long>(titleId));
            return;
        }
    }

    m_requestedTitleId = titleId;
    ++m_requestGeneration;
    // Coalesce very rapid focus changes. GPU/decoded cache hits above remain
    // immediate, while disk decoding waits until the selection has been stable
    // briefly instead of repeatedly decoding artwork the user has passed.
    // A hero that is already cached only needs the SD read, so start it at
    // once; a cold one keeps a short coalescing window so fast scrolling does
    // not queue decodes the user has already scrolled past.
    const bool preview = scrolling
        && (m_heroCanvasW > kPreviewCanvasW || m_heroCanvasH > kPreviewCanvasH);
    m_nextCanvasW = preview ? std::min(m_heroCanvasW, kPreviewCanvasW) : m_heroCanvasW;
    m_nextCanvasH = preview ? std::min(m_heroCanvasH, kPreviewCanvasH) : m_heroCanvasH;
    m_nextUseCache = !preview;
    if (preview)
        DebugLog::log("[steamgriddb-ui] fast scroll preview %dx%d title=%016llX",
                      m_nextCanvasW, m_nextCanvasH,
                      static_cast<unsigned long long>(titleId));
    // A preview is decoded straight from the source, so there is no cache read
    // to wait for and no reason to hold it behind the coalescing window.
    m_decodeDebounce = forceReload || preview ? 0.f
        : (heroCachePresent(titleId, m_heroCanvasW, m_heroCanvasH) ? 0.f : 0.06f);
    m_readyArtwork.reset();
    m_uploadStage = 0;
    cancelPendingDecode();
    DebugLog::log("[steamgriddb-ui] cache miss title=%016llX",
                  static_cast<unsigned long long>(titleId));
}

SteamGridDbBackdrop::DecodedImage SteamGridDbBackdrop::decodeImage(
    const std::string& path, int outputWidth, int outputHeight, bool fill,
    const std::atomic<bool>* cancel, bool useCache) {
    auto decoded = steamgriddb::artwork::decode(path, outputWidth, outputHeight,
                                                fill, cancel, useCache);
    DecodedImage out;
    out.rgba = std::move(decoded.rgba);
    out.width = decoded.width;
    out.height = decoded.height;
    return out;
}

float SteamGridDbBackdrop::sampleLuminance(const DecodedImage& image) {
    if (image.rgba.empty() || image.width <= 0 || image.height <= 0)
        return -1.f;
    // Sparse grid (~32×18) — cheap enough for the decode worker thread.
    const int stepX = std::max(1, image.width / 32);
    const int stepY = std::max(1, image.height / 18);
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < image.height; y += stepY) {
        for (int x = 0; x < image.width; x += stepX) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)
                                   + static_cast<std::size_t>(x)) * 4u;
            if (i + 2 >= image.rgba.size())
                continue;
            const float r = image.rgba[i] / 255.f;
            const float g = image.rgba[i + 1] / 255.f;
            const float b = image.rgba[i + 2] / 255.f;
            sum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : -1.f;
}

bool SteamGridDbBackdrop::useFullHeroOpacity() const {
    // Full-opacity heroes are single-row only; folder grid stays dimmed.
    return m_layoutMode == AppLayoutMode::DynamicLine;
}

bool SteamGridDbBackdrop::hasDisplayedHero() const {
    if (!m_enabled || !isVisible())
        return false;
    if (m_artworkOpacity.value() <= 0.05f)
        return false;
    const auto& set = m_sets[m_current];
    return set.hasHero && set.hero.valid();
}

float SteamGridDbBackdrop::displayedHeroLuminance() const {
    if (!hasDisplayedHero())
        return -1.f;
    return m_sets[m_current].heroLuminance;
}

void SteamGridDbBackdrop::startPendingDecode(std::uint64_t titleId, int canvasW,
                                             int canvasH, bool useCache) {
    auto state = std::make_shared<DecodeState>();
    state->artwork.titleId = titleId;
    state->artwork.generation = titleId == m_requestedTitleId ? m_requestGeneration : 0;

    const float blur = m_heroBlur;
    auto work = [state, titleId, canvasW, canvasH, useCache, blur]() {
        const auto started = std::chrono::steady_clock::now();
        if (titleId != 0 && SteamGridDbManager::hasArtwork(titleId)) {
            // Fixed canvases let both double-buffer slots reuse their existing
            // MemBlocks. Variable source dimensions previously triggered a
            // free + waitIdle on the render thread during focus changes.
            //
            // Canvas comes from Settings → Backgrounds (default 1280×720,
            // i.e. 1:1 with the display). Lower sizes cut the RGBA per slot
            // and the SD cache file, at the cost of stretching the hero back
            // up on screen.
            state->artwork.hero = decodeImage(
                SteamGridDbManager::heroPath(titleId), canvasW, canvasH, true,
                &state->cancelled, useCache);
            if (!state->cancelled.load() && blur > 0.001f)
                blurRgba(state->artwork.hero.rgba, state->artwork.hero.width,
                         state->artwork.hero.height, blur);
            if (!state->cancelled.load())
                state->artwork.heroLuminance = sampleLuminance(state->artwork.hero);
            if (!state->cancelled.load())
                state->artwork.logo = decodeImage(
                    SteamGridDbManager::logoPath(titleId), 384, 192, false,
                    &state->cancelled);
        }
        state->artwork.decodeMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    };

    PendingDecode pending;
    pending.state = state;
    if (m_threadPool)
        pending.future = m_threadPool->submit(std::move(work));
    else
        pending.future = std::async(std::launch::async, std::move(work));
    m_pendingDecode = std::move(pending);
}

void SteamGridDbBackdrop::cancelPendingDecode() {
    // Mark first so a worker that is mid-scale bails at its next row check,
    // then release the slot: waiting for a stale 720p decode to finish is what
    // made fast scrolling feel like the background was chasing the selection.
    if (m_pendingDecode && m_pendingDecode->state)
        m_pendingDecode->state->cancelled.store(true);
    m_pendingDecode.reset();
}

void SteamGridDbBackdrop::beginCrossfade(int nextSet, std::uint64_t titleId) {
    m_sets[nextSet].titleId = titleId;
    m_current = nextSet;
    m_fade.setImmediate(0.f);
    m_fade.set(1.f, 0.30f, nxui::Easing::outCubic);
    m_artworkOpacity.set(1.f, 0.14f, nxui::Easing::outCubic);
}

nxui::Rect SteamGridDbBackdrop::fillRect(const nxui::Texture& texture,
                                         const nxui::Rect& area) {
    if (!texture.valid()) return area;
    const float scale = std::max(area.width / texture.width(), area.height / texture.height());
    const float w = texture.width() * scale;
    const float h = texture.height() * scale;
    return {area.x + (area.width - w) * 0.5f, area.y + (area.height - h) * 0.5f, w, h};
}

nxui::Rect SteamGridDbBackdrop::containRect(const nxui::Texture& texture,
                                            const nxui::Rect& area) {
    if (!texture.valid()) return area;
    const float scale = std::min(area.width / texture.width(), area.height / texture.height());
    const float w = texture.width() * scale;
    const float h = texture.height() * scale;
    return {area.x + (area.width - w) * 0.5f, area.y + (area.height - h) * 0.5f, w, h};
}

void SteamGridDbBackdrop::drawSet(nxui::Renderer& renderer,
                                  const ArtworkSet& set,
                                  float alpha) const {
    if (alpha <= 0.002f) return;
    const nxui::Rect screen = rect();

    if (set.hasHero && set.hero.valid()) {
        const bool fullHero = useFullHeroOpacity();
        const float heroAlpha = fullHero ? 0.94f : 0.24f;
        nxui::Rect heroRect = fillRect(set.hero, screen);
        if (m_driftEnabled) {
            // Same 8% overscan the wallpaper uses, so the pan has slack on
            // both sides and the full-bleed hero keeps covering the screen.
            constexpr float kDriftOverscan = 0.08f;
            const float w = heroRect.width * (1.f + kDriftOverscan);
            const float h = heroRect.height * (1.f + kDriftOverscan);
            heroRect = {screen.x + (screen.width - w) * 0.5f,
                        screen.y + (screen.height - h) * 0.5f, w, h};
            heroRect.x += std::sin(m_driftPhase) * (w - screen.width) * 0.5f;
        }
        renderer.pushClipRect(screen);
        renderer.drawTexture(&set.hero, heroRect,
                             nxui::Color::white().withAlpha(alpha * heroAlpha));
        renderer.popClipRect();
        // Soft bottom scrim so chrome stays legible over bright/dark heroes.
        if (fullHero) {
            renderer.drawGradientRect(
                screen,
                nxui::Color(0.f, 0.f, 0.f, 0.04f * alpha),
                nxui::Color(0.f, 0.f, 0.f, 0.46f * alpha));
        }
    }

    if (m_layoutMode != AppLayoutMode::DynamicLine) return;

    if (m_showLogo && set.hasLogo && set.logo.valid()) {
        // containRect centres and constrains the logo inside the gutter band.
        renderer.drawTexture(&set.logo, containRect(set.logo, m_logoArea),
                             nxui::Color::white().withAlpha(0.96f * alpha));
    }
}

void SteamGridDbBackdrop::onUpdate(float dt) {
    m_sinceFocusChange += dt;
    if (m_driftEnabled) {
        const float period = std::max(6.f, 75.f - 0.63f * static_cast<float>(m_driftSpeed));
        m_driftPhase += dt * (6.2831853f / period);
        if (m_driftPhase > 6.2831853f)
            m_driftPhase -= 6.2831853f;
    }
    m_fade.update(std::min(dt, 0.04f));
    m_artworkOpacity.update(std::min(dt, 0.04f));
    if (m_preloadGapSec > 0.f)
        m_preloadGapSec = std::max(0.f, m_preloadGapSec - dt);

    if (m_pendingDecode
        && m_pendingDecode->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            m_pendingDecode->future.get();
        } catch (...) {
        }
        auto state = std::move(m_pendingDecode->state);
        m_pendingDecode.reset();
        if (state && !state->cancelled.load()
            && state->artwork.titleId == m_requestedTitleId
            && state->artwork.generation == m_requestGeneration) {
            DebugLog::log("[steamgriddb-ui] decoded title=%016llX in %ldms hero=%d logo=%d",
                          static_cast<unsigned long long>(state->artwork.titleId),
                          state->artwork.decodeMilliseconds,
                          state->artwork.hero.rgba.empty() ? 0 : 1,
                          state->artwork.logo.rgba.empty() ? 0 : 1);
            const bool hasArtwork = !state->artwork.hero.rgba.empty()
                                 || !state->artwork.logo.rgba.empty();
            if (hasArtwork) {
                m_readyArtwork = std::move(state->artwork);
                m_uploadStage = 0;
            } else {
                m_missingArtworkTitleIds.push_back(state->artwork.titleId);
                if (m_missingArtworkTitleIds.size() > kMissingCacheLimit)
                    m_missingArtworkTitleIds.erase(m_missingArtworkTitleIds.begin());
                m_appliedGeneration = m_requestGeneration;
                m_artworkOpacity.set(0.f, 0.22f, nxui::Easing::outCubic);
            }
        } else if (state && !state->cancelled.load()
                   && std::find(m_preloadTitleIds.begin(), m_preloadTitleIds.end(),
                                state->artwork.titleId) != m_preloadTitleIds.end()) {
            DebugLog::log("[steamgriddb-ui] prefetched title=%016llX in %ldms hero=%d logo=%d",
                          static_cast<unsigned long long>(state->artwork.titleId),
                          state->artwork.decodeMilliseconds,
                          state->artwork.hero.rgba.empty() ? 0 : 1,
                          state->artwork.logo.rgba.empty() ? 0 : 1);
            m_decodedCache.erase(
                std::remove_if(m_decodedCache.begin(), m_decodedCache.end(),
                    [&state](const auto& artwork) {
                        return artwork.titleId == state->artwork.titleId;
                    }),
                m_decodedCache.end());
            const bool hasArtwork = !state->artwork.hero.rgba.empty()
                                 || !state->artwork.logo.rgba.empty();
            if (hasArtwork) {
                m_decodedCache.push_back(std::move(state->artwork));
                if (m_decodedCache.size() > kDecodedCacheLimit)
                    m_decodedCache.erase(m_decodedCache.begin());
            } else if (std::find(m_missingArtworkTitleIds.begin(),
                                 m_missingArtworkTitleIds.end(), state->artwork.titleId)
                       == m_missingArtworkTitleIds.end()) {
                m_missingArtworkTitleIds.push_back(state->artwork.titleId);
                if (m_missingArtworkTitleIds.size() > kMissingCacheLimit)
                    m_missingArtworkTitleIds.erase(m_missingArtworkTitleIds.begin());
            }
        }
    }

    if (m_readyArtwork && m_readyArtwork->generation != m_requestGeneration) {
        m_readyArtwork.reset();
        m_uploadStage = 0;
    }

    if (m_readyArtwork) {
        auto& decoded = *m_readyArtwork;
        auto& target = m_sets[1 - m_current];
        if (m_uploadStage == 0) {
            if (m_waitForGpuBeforeUpload) {
                // The inactive set may still be referenced by an in-flight
                // crossfade command buffer. Replacing its image storage before
                // the queue drains causes a GPU read page fault.
                DebugLog::log("[steamgriddb-ui] draining GPU before forced backdrop upload title=%016lX",
                              static_cast<unsigned long>(decoded.titleId));
                m_gpu.waitIdle();
#ifdef NXUI_BACKEND_DEKO3D
                m_renderer.reclaimReleasedTextureSlotsAfterIdle();
#endif
                m_waitForGpuBeforeUpload = false;
            }
            target.hasHero = !decoded.hero.rgba.empty()
                && target.hero.loadFromPixels(m_gpu, m_renderer, decoded.hero.rgba.data(),
                                              decoded.hero.width, decoded.hero.height);
            target.heroLuminance = target.hasHero ? decoded.heroLuminance : -1.f;
            // Recorded even when the upload fails: the upgrade check treats the
            // stored canvas as "already attempted" and must not retry forever.
            target.canvasW = decoded.hero.width;
            target.canvasH = decoded.hero.height;
            decoded.hero.rgba.clear();
        } else if (m_uploadStage == 1) {
            target.hasLogo = !decoded.logo.rgba.empty()
                && target.logo.loadFromPixels(m_gpu, m_renderer, decoded.logo.rgba.data(),
                                              decoded.logo.width, decoded.logo.height);
            decoded.logo.rgba.clear();
        }
        ++m_uploadStage;
        if (m_uploadStage >= 2) {
            const int next = 1 - m_current;
            const std::uint64_t titleId = decoded.titleId;
            m_appliedGeneration = decoded.generation;
            m_readyArtwork.reset();
            m_uploadStage = 0;
            beginCrossfade(next, titleId);
        }
        return; // Strictly cap artwork uploads to one per frame.
    }

    if (!m_pendingDecode && m_appliedGeneration != m_requestGeneration) {
        m_decodeDebounce = std::max(0.f, m_decodeDebounce - dt);
        if (m_decodeDebounce <= 0.f)
            startPendingDecode(m_requestedTitleId, m_nextCanvasW, m_nextCanvasH,
                               m_nextUseCache);
    } else if (!m_pendingDecode && !m_readyArtwork && !m_preloadPaused
               && m_sets[m_current].hasHero
               && m_sets[m_current].titleId == m_requestedTitleId
               && (m_sets[m_current].canvasW < m_heroCanvasW
                   || m_sets[m_current].canvasH < m_heroCanvasH)
               && m_sinceFocusChange > 0.35f) {
        // Fast scrolling decoded this hero at the preview canvas. The selection
        // has been still for a moment, so redo it at the configured size and
        // crossfade — the preview only has to cover the settle, not stay.
        DebugLog::log("[steamgriddb-ui] upgrading hero title=%016llX to %dx%d",
                      static_cast<unsigned long long>(m_requestedTitleId),
                      m_heroCanvasW, m_heroCanvasH);
        m_showPreviousDuringCrossfade = true;
        startPendingDecode(m_requestedTitleId, m_heroCanvasW, m_heroCanvasH, true);
    } else if (!m_pendingDecode && !m_readyArtwork
               && !m_preloadPaused && m_preloadGapSec <= 0.f) {
        for (std::uint64_t titleId : m_preloadTitleIds) {
            const bool decoded = std::any_of(
                m_decodedCache.begin(), m_decodedCache.end(),
                [titleId](const auto& artwork) { return artwork.titleId == titleId; });
            const bool missing = std::find(m_missingArtworkTitleIds.begin(),
                                           m_missingArtworkTitleIds.end(), titleId)
                != m_missingArtworkTitleIds.end();
            if (!decoded && !missing && !hasGpuArtwork(titleId)) {
                DebugLog::log("[steamgriddb-ui] prefetch scheduled title=%016llX",
                              static_cast<unsigned long long>(titleId));
                startPendingDecode(titleId, m_heroCanvasW, m_heroCanvasH, true);
                m_preloadGapSec = m_preloadMinInterval;
                break;
            }
        }
    }
}

void SteamGridDbBackdrop::onRender(nxui::Renderer& renderer) {
    if (!m_enabled || opacity() <= 0.002f) return;
    const float t = std::clamp(m_fade.value(), 0.f, 1.f);
    const float artworkOpacity = std::clamp(m_artworkOpacity.value(), 0.f, 1.f)
                               * opacity();
    if (m_showPreviousDuringCrossfade)
        drawSet(renderer, m_sets[1 - m_current], (1.f - t) * artworkOpacity);
    drawSet(renderer, m_sets[m_current], t * artworkOpacity);
}
