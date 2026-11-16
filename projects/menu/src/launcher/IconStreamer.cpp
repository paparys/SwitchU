#include "IconStreamer.hpp"
#include "widgets/GlossyIcon.hpp"
#include "core/DebugLog.hpp"
#include "steamgriddb/ArtworkCache.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"
#include <nxui/third_party/stb/stb_image.h>
#include <algorithm>
#include <string>
#include <chrono>
#include <cstring>
#include <future>
#include <limits>
#include <unordered_map>

namespace {

// One log line per scheduled decode put SD log traffic on the same device
// as the icon reads it was racing. Flip this on only when debugging.
constexpr bool kLogDecodeSchedule = false;

constexpr int kPosterFitMaxW = 320;
constexpr int kPosterFitMaxH = 480;

IconStreamer::DecodedIcon decodePosterFromCache(std::uint64_t titleId) {
    IconStreamer::DecodedIcon out{};
    if (titleId == 0 || !SteamGridDbManager::hasPosterCached(titleId))
        return out;
    auto decoded = steamgriddb::artwork::decodeFit(
        SteamGridDbManager::posterPath(titleId), kPosterFitMaxW, kPosterFitMaxH);
    if (decoded.rgba.empty())
        return out;
    out.rgba = std::move(decoded.rgba);
    out.w = decoded.width;
    out.h = decoded.height;
    return out;
}

// One reusable control-data buffer per decode worker. Icons are read at
// ~128 KB each; letting the vector size once and resize afterwards takes the
// per-icon allocation out of the streaming path.
std::vector<uint8_t>& iconLoadScratch() {
    thread_local std::vector<uint8_t> scratch;
    return scratch;
}

} // namespace

const IconStreamer::DecodedIcon* IconStreamer::DecodedIconCache::get(
    std::uint64_t titleId) {
    const auto found = index.find(titleId);
    if (found == index.end())
        return nullptr;
    order.splice(order.begin(), order, found->second);
    return &found->second->icon;
}

void IconStreamer::DecodedIconCache::put(std::uint64_t titleId, DecodedIcon icon) {
    if (titleId == 0 || icon.rgba.empty() || icon.w <= 0 || icon.h <= 0)
        return;
    const std::size_t iconBytes = icon.rgba.size();
    if (iconBytes > kMaxBytes)
        return;

    const auto found = index.find(titleId);
    if (found != index.end()) {
        bytes -= found->second->icon.rgba.size();
        order.erase(found->second);
        index.erase(found);
    }

    order.push_front(Entry{titleId, std::move(icon)});
    index[titleId] = order.begin();
    bytes += iconBytes;
    while (!order.empty()
           && (bytes > kMaxBytes || order.size() > kMaxEntries)) {
        Entry& old = order.back();
        bytes -= old.icon.rgba.size();
        index.erase(old.titleId);
        order.pop_back();
    }
}

void IconStreamer::DecodedIconCache::erase(std::uint64_t titleId) {
    const auto found = index.find(titleId);
    if (found == index.end())
        return;
    bytes -= found->second->icon.rgba.size();
    order.erase(found->second);
    index.erase(found);
}

void IconStreamer::DecodedIconCache::clear() {
    order.clear();
    index.clear();
    bytes = 0;
}

void IconStreamer::rememberDecodedIcon(std::uint64_t titleId, bool poster,
                                        DecodeState& state) {
    if (poster || state.failed || state.decoded.rgba.empty())
        return;
    m_decodedCache.put(titleId, std::move(state.decoded));
}


void IconStreamer::init(int appCount) {
    clear();
    m_compressed.resize(appCount);
    m_titleIds.assign(appCount, 0);
    m_appToSlot.assign(appCount, -1);
    m_artStale.assign(static_cast<std::size_t>(appCount), 0);
    m_artProxy.assign(static_cast<std::size_t>(appCount), 0);
    // Pool churn is bounded by the visible window; reserving keeps the frame
    // path from reallocating these while a page is streaming in.
    m_freeSlots.reserve(static_cast<std::size_t>(std::max(appCount, 32)));
    m_pendingDecodes.reserve(static_cast<std::size_t>(kFastMaxPendingDecodes));
}

void IconStreamer::setIconDataLoader(IconDataLoader loader) {
    m_iconLoader = std::move(loader);
}

void IconStreamer::setIconDataLoaderInto(IconDataLoaderInto loader) {
    m_iconLoaderInto = std::move(loader);
}

void IconStreamer::softInvalidateArt() {
    for (auto& bytes : m_compressed)
        bytes.clear();
    m_decodedCache.clear();
    cancelPending();
    m_pendingDecodes.clear();
    m_failedTitleIds.clear();
    m_artStale.assign(m_appToSlot.size(), 1);
    m_artProxy.assign(m_appToSlot.size(), 0);
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

bool IconStreamer::setPreferPosterArt(bool preferPoster) {
    if (m_preferPosterArt == preferPoster)
        return false;
    m_preferPosterArt = preferPoster;
    softInvalidateArt();
    return true;
}

bool IconStreamer::isArtStale(int appIndex) const {
    return appIndex >= 0
        && appIndex < static_cast<int>(m_artStale.size())
        && m_artStale[static_cast<std::size_t>(appIndex)] != 0;
}

void IconStreamer::setTitleId(int appIndex, uint64_t titleId) {
    if (appIndex >= 0 && appIndex < (int)m_titleIds.size())
        m_titleIds[appIndex] = titleId;
}

void IconStreamer::setIconData(int appIndex, std::vector<uint8_t> compressed) {
    if (appIndex >= 0 && appIndex < (int)m_compressed.size())
        m_compressed[appIndex] = std::move(compressed);
}

void IconStreamer::resize(int appCount) {
    if (appCount < 0)
        appCount = 0;

    for (auto& slot : m_pool) {
        if (slot && slot->appIndex >= appCount)
            slot->appIndex = -1;
    }

    m_compressed.resize(appCount);
    m_titleIds.resize(appCount, 0);
    m_appToSlot.resize(appCount, -1);
    m_artStale.resize(static_cast<std::size_t>(appCount), 0);
    m_artProxy.resize(static_cast<std::size_t>(appCount), 0);
    if (m_pinnedIndex >= appCount)
        m_pinnedIndex = -1;
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::setPinnedIndex(int appIndex) {
    m_pinnedIndex = (appIndex >= 0 && appIndex < (int)m_appToSlot.size()) ? appIndex : -1;
}

void IconStreamer::clearPinnedIndex() {
    m_pinnedIndex = -1;
}

void IconStreamer::clear() {
    m_idleHoldTarget = -1;
    m_decodedCache.clear();
    cancelShadowPrefetch();
    cancelPending();
    m_pool.clear();
    m_compressed.clear();
    m_titleIds.clear();
    m_appToSlot.clear();
    m_artStale.clear();
    m_artProxy.clear();
    m_freeSlots.clear();
    m_pendingDecodes.clear();
    m_failedTitleIds.clear();
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    m_pinnedIndex = -1;
    m_preferPosterArt = false;
}

void IconStreamer::cancelPending() {
    for (auto& pending : m_pendingDecodes) {
        if (pending.state)
            pending.state->cancelled.store(true);
    }
}

void IconStreamer::cancelPendingExcept(std::uint64_t keepTitleId) {
    for (auto& pending : m_pendingDecodes) {
        if (pending.titleId == keepTitleId)
            continue;
        if (pending.state)
            pending.state->cancelled.store(true);
    }
}

bool IconStreamer::swapIndices(int a, int b) {
    if (a < 0 || b < 0 || a >= (int)m_appToSlot.size() || b >= (int)m_appToSlot.size())
        return false;
    if (a == b)
        return true;

    if (a < (int)m_compressed.size() && b < (int)m_compressed.size())
        std::swap(m_compressed[a], m_compressed[b]);
    if (a < (int)m_titleIds.size() && b < (int)m_titleIds.size())
        std::swap(m_titleIds[a], m_titleIds[b]);
    if (a < (int)m_artProxy.size() && b < (int)m_artProxy.size())
        std::swap(m_artProxy[a], m_artProxy[b]);

    if (a < (int)m_appToSlot.size() && b < (int)m_appToSlot.size()) {
        int slotA = m_appToSlot[a];
        int slotB = m_appToSlot[b];
        std::swap(m_appToSlot[a], m_appToSlot[b]);

        if (slotA >= 0 && slotA < (int)m_pool.size())
            m_pool[slotA]->appIndex = b;
        if (slotB >= 0 && slotB < (int)m_pool.size())
            m_pool[slotB]->appIndex = a;
    }

    if (m_pinnedIndex == a)
        m_pinnedIndex = b;
    else if (m_pinnedIndex == b)
        m_pinnedIndex = a;

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    return true;
}

void IconStreamer::reconcileTitleIds(const std::vector<uint64_t>& titleIds) {
    const std::vector<uint64_t> oldTitleIds = m_titleIds;
    const int oldPinnedIndex = m_pinnedIndex;
    const uint64_t pinnedTitle = oldPinnedIndex >= 0 && oldPinnedIndex < (int)oldTitleIds.size()
        ? oldTitleIds[(size_t)oldPinnedIndex] : 0;

    std::unordered_map<uint64_t, int> newIndexByTitle;
    newIndexByTitle.reserve(titleIds.size());
    for (int i = 0; i < (int)titleIds.size(); ++i) {
        if (titleIds[(size_t)i] != 0)
            newIndexByTitle.emplace(titleIds[(size_t)i], i);
    }

    std::vector<int> newAppToSlot(titleIds.size(), -1);
    m_freeSlots.clear();
    for (int slotIndex = 0; slotIndex < (int)m_pool.size(); ++slotIndex) {
        auto& slot = m_pool[(size_t)slotIndex];
        if (!slot || slot->appIndex < 0 || slot->appIndex >= (int)oldTitleIds.size()) {
            if (slot)
                slot->appIndex = -1;
            m_freeSlots.push_back(slotIndex);
            continue;
        }

        const uint64_t titleId = oldTitleIds[(size_t)slot->appIndex];
        auto next = newIndexByTitle.find(titleId);
        if (titleId == 0 || next == newIndexByTitle.end() ||
            newAppToSlot[(size_t)next->second] >= 0) {
            slot->appIndex = -1;
            m_freeSlots.push_back(slotIndex);
            continue;
        }

        slot->appIndex = next->second;
        newAppToSlot[(size_t)next->second] = slotIndex;
    }

    std::vector<std::vector<uint8_t>> newCompressed(titleIds.size());
    std::vector<uint8_t> newArtProxy(titleIds.size(), 0);
    for (int oldIndex = 0; oldIndex < (int)oldTitleIds.size(); ++oldIndex) {
        auto next = newIndexByTitle.find(oldTitleIds[(size_t)oldIndex]);
        if (next == newIndexByTitle.end())
            continue;
        if (oldIndex < (int)m_compressed.size())
            newCompressed[(size_t)next->second] = std::move(m_compressed[(size_t)oldIndex]);
        if (oldIndex < (int)m_artProxy.size() && m_artProxy[(size_t)oldIndex] != 0)
            newArtProxy[(size_t)next->second] = 1;
    }

    m_titleIds = titleIds;
    m_compressed = std::move(newCompressed);
    m_appToSlot = std::move(newAppToSlot);
    m_artStale.assign(titleIds.size(), 0);
    for (std::size_t i = 0; i < newArtProxy.size(); ++i) {
        if (newArtProxy[i] != 0) {
            // Reuse the retained proxy texture, but let the next pump replace it
            // with the final poster now that the catalogue changed.
            m_artStale[i] = 1;
        }
    }
    m_artProxy = std::move(newArtProxy);
    m_pinnedIndex = -1;
    if (pinnedTitle != 0) {
        auto next = newIndexByTitle.find(pinnedTitle);
        if (next != newIndexByTitle.end())
            m_pinnedIndex = next->second;
    }

    m_failedTitleIds.clear();
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::reconcileCatalog(IconStreamer&& catalog) {
    reconcileTitleIds(catalog.m_titleIds);
    const size_t count = std::min(m_compressed.size(), catalog.m_compressed.size());
    for (size_t i = 0; i < count; ++i) {
        if (m_compressed[i].empty() && !catalog.m_compressed[i].empty())
            m_compressed[i] = std::move(catalog.m_compressed[i]);
    }
}

bool IconStreamer::hasData(int index) const {
    if (index < 0 || index >= (int)m_appToSlot.size())
        return false;
    if (index < (int)m_compressed.size() && !m_compressed[index].empty())
        return true;
    return index < (int)m_titleIds.size() && m_titleIds[index] != 0 &&
           ((bool)m_iconLoader || (bool)m_iconLoaderInto);
}

bool IconStreamer::needsVisibleLoads(int currentPage, int iconsPerPage) const {
    if (iconsPerPage <= 0 || m_appToSlot.empty())
        return false;
    const int totalApps = static_cast<int>(m_appToSlot.size());
    const int totalPages = (totalApps + iconsPerPage - 1) / iconsPerPage;
    currentPage = std::clamp(currentPage, 0, std::max(0, totalPages - 1));
    int begin = currentPage * iconsPerPage;
    int end = std::min(totalApps, begin + iconsPerPage);
    // In single-row mode callers use one icon per logical page. Keep nearby
    // icons hot (radius shrinks during soft-landing after HOME return).
    if (iconsPerPage == 1) {
        const int radius = pageCacheRadius();
        begin = std::max(0, currentPage - radius);
        end = std::min(totalApps, currentPage + radius + 1);
    }
    for (int i = begin; i < end; ++i) {
        // Resident tiles cannot need a load, so check that before paying for
        // the failed/pending searches. A settled row used to do both for all
        // 21-25 windowed tiles on every frame.
        if (m_appToSlot[i] >= 0 && !isArtStale(i))
            continue;
        const uint64_t titleId = i < (int)m_titleIds.size() ? m_titleIds[(size_t)i] : 0;
        const bool failed = std::find(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId)
            != m_failedTitleIds.end();
        const bool pending = std::any_of(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [titleId](const PendingDecode& decode) { return decode.titleId == titleId; });
        const bool needsSlot = m_appToSlot[i] < 0 || isArtStale(i);
        if (needsSlot && !failed && (pending || hasData(i)))
            return true;
    }
    return false;
}

int IconStreamer::appIndexFor(const PendingDecode& pending) const {
    const int cached = pending.appIndex;
    if (cached >= 0 && cached < static_cast<int>(m_titleIds.size()) &&
        m_titleIds[static_cast<std::size_t>(cached)] == pending.titleId)
        return cached;
    const auto it = std::find(m_titleIds.begin(), m_titleIds.end(), pending.titleId);
    return it == m_titleIds.end()
        ? -1 : static_cast<int>(it - m_titleIds.begin());
}

void IconStreamer::recordFailedTitle(uint64_t titleId) {
    // Bounded miss cache, mirroring sLaunch's icon cache: a library that asks
    // for hundreds of unresolvable titles must not grow this list forever.
    // Dropping the oldest entry only costs one retried load.
    constexpr std::size_t kMaxFailedTitles = 128;
    if (std::find(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId)
        != m_failedTitleIds.end())
        return;
    if (m_failedTitleIds.size() >= kMaxFailedTitles)
        m_failedTitleIds.erase(m_failedTitleIds.begin());
    m_failedTitleIds.push_back(titleId);
}

std::size_t IconStreamer::pendingDecodeBytes() const {
    std::size_t bytes = 0;
    for (const auto& pending : m_pendingDecodes) {
        const std::size_t actual = pending.state
            ? pending.state->decodedBytes.load() : 0;
        bytes += actual > 0 ? actual : pending.estimatedBytes;
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// Decode a single compressed icon to RGBA, downscaling to kIconSize if needed.
// ---------------------------------------------------------------------------
IconStreamer::DecodedIcon IconStreamer::decodeIconData(const std::vector<uint8_t>& data,
                                                       bool poster) {
    DecodedIcon out{};
    if (data.empty()) return out;

    int w, h, ch;
    uint8_t* raw = stbi_load_from_memory(data.data(), (int)data.size(),
                                         &w, &h, &ch, 4);
    if (!raw) return out;
    std::unique_ptr<uint8_t, decltype(&stbi_image_free)> full(raw, &stbi_image_free);

    // Preserve aspect. Square art is drawn at kIconSize, so scale it on the
    // CPU: a 256×256 control icon becomes 160×160 (102 KB) instead of uploading
    // 262 KB and letting the GPU sample detail that never reaches the screen.
    // Poster art keeps room for a 2× single-row poster (300×450 focused).
    const int kMaxW = poster ? kIconSize * 2 : kIconSize;   // 320 / 160
    const int kMaxH = poster ? kIconSize * 3 : kIconSize;   // 480 / 160
    if (w > kMaxW || h > kMaxH) {
        const float scale = std::min((float)kMaxW / (float)w, (float)kMaxH / (float)h);
        const int dstW = std::max(1, (int)((float)w * scale + 0.5f));
        const int dstH = std::max(1, (int)((float)h * scale + 0.5f));
        std::vector<uint8_t> scaled((size_t)dstW * dstH * 4);
        if (!scaled.empty()) {
            struct XSample {
                int x0;
                int x1;
                float fx;
                float invFx;
            };
            // Column taps are identical on every row. Computing them once
            // takes the division, clamp and fractional extraction out of the
            // per-pixel loop while keeping the same bilinear result.
            std::vector<XSample> xSamples(static_cast<std::size_t>(dstW));
            const float scaleX = (float)w / dstW;
            for (int x = 0; x < dstW; ++x) {
                const float srcXf = (x + 0.5f) * scaleX - 0.5f;
                int x0 = (int)srcXf; if (x0 < 0) x0 = 0;
                int x1 = x0 + 1;     if (x1 >= w) x1 = w - 1;
                const float fx = srcXf - x0;
                xSamples[(std::size_t)x] = {x0, x1, fx, 1.f - fx};
            }

            const float scaleY = (float)h / dstH;
            for (int y = 0; y < dstH; ++y) {
                const float srcYf = (y + 0.5f) * scaleY - 0.5f;
                int y0 = (int)srcYf; if (y0 < 0) y0 = 0;
                int y1 = y0 + 1;     if (y1 >= h) y1 = h - 1;
                const float fy = srcYf - y0;
                const float invFy = 1.f - fy;
                for (int x = 0; x < dstW; ++x) {
                    const XSample& sample = xSamples[(std::size_t)x];
                    const uint8_t* p00 = full.get() + ((size_t)y0 * w + sample.x0) * 4;
                    const uint8_t* p10 = full.get() + ((size_t)y0 * w + sample.x1) * 4;
                    const uint8_t* p01 = full.get() + ((size_t)y1 * w + sample.x0) * 4;
                    const uint8_t* p11 = full.get() + ((size_t)y1 * w + sample.x1) * 4;
                    uint8_t* dst = scaled.data() + ((size_t)y * dstW + x) * 4;
                    for (int c = 0; c < 4; ++c) {
                        dst[c] = (uint8_t)(
                            p00[c] * sample.invFx * invFy +
                            p10[c] * sample.fx    * invFy +
                            p01[c] * sample.invFx * fy    +
                            p11[c] * sample.fx    * fy    + 0.5f);
                    }
                }
            }
            out.rgba = std::move(scaled);
            out.w = dstW;
            out.h = dstH;
        }
    } else {
        out.rgba.assign(full.get(), full.get() + (size_t)w * h * 4);
        out.w = w;
        out.h = h;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Core streaming logic.
// ---------------------------------------------------------------------------
void IconStreamer::startBurst(float seconds) {
    m_burstUntil = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(seconds * 1000.f));
}

void IconStreamer::setPageCacheRadiusCap(int radius) {
    m_pageCacheRadiusCap = radius;
    DebugLog::log("[streamer] page-cache radius cap=%d", radius);
}

IconStreamer::PoolInventory IconStreamer::poolInventory() const {
    PoolInventory inv;
    inv.slots = (int)m_pool.size();
    inv.pendingDecodes = (int)m_pendingDecodes.size();
    for (const auto& slot : m_pool) {
        if (!slot)
            continue;
        if (slot->appIndex >= 0)
            ++inv.bound;
        else if (slot->texture.valid())
            ++inv.freeWithTex;
        else
            ++inv.freeEmpty;
        if (slot->texture.valid()) {
            const std::size_t bytes = slot->texture.allocationSize();
            inv.textureBytes += bytes;
            if (slot->texture.height() > slot->texture.width()) {
                ++inv.posters;
                inv.posterBytes += bytes;
            } else {
                ++inv.icons;
                inv.iconBytes += bytes;
            }
            if (slot->appIndex < 0) {
                ++inv.held;
                inv.heldBytes += bytes;
            }
        }
    }
    for (const auto& bytes : m_compressed)
        inv.compressedBytes += bytes.size();
    return inv;
}

void IconStreamer::releaseUnusedGpuMemory() {
    m_idleHoldTarget = -1;
    const int released = releaseUnusedGpuMemoryDrip(std::numeric_limits<int>::max());
    if (released > 0)
        DebugLog::log("[streamer] released %d unused GPU textures for FT headroom", released);
}

int IconStreamer::releaseUnusedGpuMemoryDrip(int maxCount) {
    if (maxCount <= 0)
        return 0;
    // Quiet menu: stop at the idle hold target instead of draining every free
    // slot, so a single back-step stays warm.
    if (m_idleHoldTarget >= 0)
        return trimIdleTextures(m_idleHoldTarget, maxCount);
    int released = 0;
    for (auto& slot : m_pool) {
        if (released >= maxCount)
            break;
        if (!slot || slot->appIndex >= 0 || !slot->texture.valid())
            continue;
        // Move-assign empty Texture → retireGpuResources() (waitIdle + free).
        slot->texture = nxui::Texture{};
        ++released;
    }
    return released;
}

int IconStreamer::trimIdleTextures(int maxIdle, int maxRetire) {
    if (maxIdle < 0)
        maxIdle = 0;
    if (maxRetire == 0)
        return 0;
    int idleHeld = 0;
    for (const auto& slot : m_pool) {
        if (slot && slot->appIndex < 0 && slot->texture.valid())
            ++idleHeld;
    }
    if (idleHeld <= maxIdle)
        return 0;

    // m_freeSlots is a stack: back() is handed out first, so the front holds
    // the slot that has been free longest and is least likely to be needed.
    int released = 0;
    for (std::size_t i = 0; i < m_freeSlots.size() && idleHeld > maxIdle
                       && (maxRetire < 0 || released < maxRetire); ++i) {
        const int index = m_freeSlots[i];
        if (index < 0 || index >= static_cast<int>(m_pool.size()))
            continue;
        auto& slot = *m_pool[static_cast<std::size_t>(index)];
        if (slot.appIndex >= 0 || !slot.texture.valid())
            continue;
        // Move-assign empty Texture → deferred descriptor/image retirement, so
        // trimming never waits for the GPU mid-scroll.
        slot.texture = nxui::Texture{};
        --idleHeld;
        ++released;
    }
    if (released > 0)
        DebugLog::log("[streamer] trimmed %d idle GPU textures (held=%d cap=%d)",
                      released, idleHeld, maxIdle);
    return released;
}

void IconStreamer::releaseAllGpuExcept(int keepAppIndex) {
    m_idleHoldTarget = -1;
    clearPinnedIndex();
    int released = 0;
    m_freeSlots.clear();
    m_decodedCache.clear();
    for (int i = 0; i < (int)m_pool.size(); ++i) {
        auto& slot = m_pool[(size_t)i];
        if (!slot)
            continue;
        const int app = slot->appIndex;
        if (app >= 0 && app == keepAppIndex) {
            if (app < (int)m_appToSlot.size())
                m_appToSlot[(size_t)app] = i;
            if (app < (int)m_artProxy.size() && m_artProxy[(size_t)app] != 0) {
                m_artProxy[(size_t)app] = 0;
                if (app < (int)m_artStale.size())
                    m_artStale[(size_t)app] = 1;
            }
            continue;
        }
        if (app >= 0 && app < (int)m_appToSlot.size())
            m_appToSlot[(size_t)app] = -1;
        if (app >= 0 && app < (int)m_artProxy.size())
            m_artProxy[(size_t)app] = 0;
        slot->appIndex = -1;
        if (slot->texture.valid()) {
            slot->texture = nxui::Texture{};
            ++released;
        }
        m_freeSlots.push_back(i);
    }
    cancelPending();
    DebugLog::log("[streamer] hard-released %d GPU textures (kept app=%d) for FT",
                  released, keepAppIndex);
}

void IconStreamer::startSoftLanding(float tightSeconds, int tightRadius,
                                    float widenSeconds, int widenRadius) {
    const auto now = std::chrono::steady_clock::now();
    m_softLandingRadius = std::max(0, tightRadius);
    m_softLandingWidenRadius = std::max(0, widenRadius);
    m_softLandingUntil = now + std::chrono::milliseconds(
        static_cast<int>(std::max(0.f, tightSeconds) * 1000.f));
    m_softLandingWidenUntil = m_softLandingUntil + std::chrono::milliseconds(
        static_cast<int>(std::max(0.f, widenSeconds) * 1000.f));
    DebugLog::log("[streamer] soft-landing radius=%d for %.2fs then %d for %.2fs",
                  m_softLandingRadius, tightSeconds,
                  m_softLandingWidenRadius, widenSeconds);
}

void IconStreamer::beginFastCarouselTelemetry() {
    m_fastCarouselTelemetry = {};
    m_fastCarouselTelemetry.active = true;
    m_fastCarouselTelemetry.started = std::chrono::steady_clock::now();
    noteFastCarouselDecodePeak();
}

void IconStreamer::endFastCarouselTelemetry() {
    auto& telemetry = m_fastCarouselTelemetry;
    if (!telemetry.active)
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - telemetry.started).count();
    if (durationMs >= 150 || telemetry.pageChanges != 0 || telemetry.uploads != 0) {
        const auto averageMs = [](std::uint64_t total, std::uint32_t count) -> long {
            return count == 0 ? 0 : static_cast<long>((total / count) / 1000);
        };
        DebugLog::log(
            "[streamer] fast-carousel dur=%ldms pages=%u visible_misses=%u uploads=%u "
            "wait_avg=%ldms wait_max=%ldms upload_avg=%ldms upload_max=%ldms "
            "peak_decode=%uKiB peak_pending=%u",
            static_cast<long>(durationMs),
            static_cast<unsigned>(telemetry.pageChanges),
            static_cast<unsigned>(telemetry.visibleMisses),
            static_cast<unsigned>(telemetry.uploads),
            averageMs(telemetry.waitMicros, telemetry.uploads),
            static_cast<long>(telemetry.waitMaxMicros / 1000),
            averageMs(telemetry.uploadMicros, telemetry.uploads),
            static_cast<long>(telemetry.uploadMaxMicros / 1000),
            static_cast<unsigned>(telemetry.peakDecodeBytes / 1024),
            static_cast<unsigned>(telemetry.peakPending));
    }
    telemetry.active = false;
}

void IconStreamer::noteFastCarouselDecodePeak() {
    if (!m_fastCarouselTelemetry.active)
        return;
    const std::size_t bytes = pendingDecodeBytes();
    if (bytes > m_fastCarouselTelemetry.peakDecodeBytes)
        m_fastCarouselTelemetry.peakDecodeBytes = bytes;
    if (m_pendingDecodes.size() > m_fastCarouselTelemetry.peakPending)
        m_fastCarouselTelemetry.peakPending = m_pendingDecodes.size();
}

void IconStreamer::setScrollActive(bool active) {
    if (m_scrollActive == active)
        return;
    m_scrollActive = active;

    if (active) {
        // A queued full poster cannot be uploaded while the row is moving, and
        // leaving its decode occupying a worker delays the proxy art that would
        // fill the incoming tile. Drop those requests and re-request the small
        // stand-in on the next pump; the poster is rebuilt when motion stops.
        for (auto& pending : m_pendingDecodes) {
            if (pending.poster && pending.state)
                pending.state->cancelled.store(true);
        }
        m_pendingDecodes.erase(
            std::remove_if(m_pendingDecodes.begin(), m_pendingDecodes.end(),
                           [](const PendingDecode& pending) {
                               return pending.poster;
                           }),
            m_pendingDecodes.end());
        // A proxy left from a previous scroll is already the right kind for a
        // moving row; do not make the workers decode it a second time just
        // because the resting pump had marked it for a poster upgrade.
        const std::size_t count = std::min(m_artProxy.size(), m_artStale.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (m_artProxy[i] != 0 && i < m_appToSlot.size() && m_appToSlot[i] >= 0)
                m_artStale[i] = 0;
        }
        m_lastPage = -1;
        m_lastIconsPerPage = -1;
        return;
    }

    // The row has stopped. Proxy textures are cheap but visibly soft, so mark
    // them stale and let the normal poster path swap in the full-resolution art
    // without putting a poster drain back on the scrolling frames.
    int proxied = 0;
    const std::size_t count = std::min(m_artProxy.size(), m_artStale.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (m_artProxy[i] == 0)
            continue;
        if (i < m_appToSlot.size() && m_appToSlot[i] >= 0) {
            m_artStale[i] = 1;
            ++proxied;
        }
        m_artProxy[i] = 0;
    }
    if (proxied > 0) {
        m_lastPage = -1;
        m_lastIconsPerPage = -1;
        startBurst(0.45f);
        DebugLog::log("[streamer] upgrading %d scroll proxy icons to full art", proxied);
    }
}

void IconStreamer::updateFastCarouselDrag(bool dynamicLine, float speed) {
    // A held direction sits close to the boundary between normal and fast, so
    // reuse the previous state to avoid alternating the 4/6-tile window. Enter
    // where the row is clearly quick; leave only once it has nearly settled.
    constexpr float kFastEnterSpeed = 3.5f;
    constexpr float kFastExitSpeed = 1.25f;
    const bool fast = dynamicLine &&
        (m_fastCarouselDrag ? speed >= kFastExitSpeed : speed >= kFastEnterSpeed);
    setFastCarouselDrag(fast);
}

void IconStreamer::setFastCarouselDrag(bool fast) {
    if (m_fastCarouselDrag == fast)
        return;
    m_fastCarouselDrag = fast;
    const auto now = std::chrono::steady_clock::now();
    if (fast) {
        m_fastCarouselLingerUntil = {};
        beginFastCarouselTelemetry();
        startBurst(0.75f);
    } else {
        // Let the settle keep the wider ahead window resident while the spring
        // finishes and queued uploads drain, then return to the 4-tile budget.
        m_fastCarouselLingerUntil = now + std::chrono::milliseconds(450);
        endFastCarouselTelemetry();
        startBurst(0.45f);
    }
}

void IconStreamer::onPageChanged(int currentPage, int iconsPerPage,
                                 nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                 const std::vector<std::shared_ptr<GlossyIcon>>& allIcons)
{
    int totalApps  = (int)m_appToSlot.size();
    if (totalApps == 0 || iconsPerPage <= 0) {
        m_lastPage = currentPage;
        m_lastIconsPerPage = iconsPerPage;
        return;
    }

    int totalPages = (totalApps + iconsPerPage - 1) / iconsPerPage;
    currentPage = std::clamp(currentPage, 0, totalPages - 1);

    const int previousPage = m_lastPage;
    const bool pageChanged = currentPage != m_lastPage ||
                             iconsPerPage != m_lastIconsPerPage;
    m_lastPage = currentPage;
    m_lastIconsPerPage = iconsPerPage;

    // A click-driven carousel step means the user is browsing now: drop any
    // post-HOME soft-landing shrink so the full four-tile-ahead window stays
    // warm instead of only two or three tiles for the first seconds.
    if (pageChanged && iconsPerPage == 1 && inSoftLanding()) {
        m_softLandingUntil = std::chrono::steady_clock::time_point{};
        m_softLandingWidenUntil = std::chrono::steady_clock::time_point{};
        DebugLog::log("[streamer] soft-landing cleared by carousel step");
    }
    int visibleStartApp = currentPage * iconsPerPage;
    int visibleEndApp   = std::min(totalApps, visibleStartApp + iconsPerPage);
    if (m_fastCarouselTelemetry.active && pageChanged) {
        ++m_fastCarouselTelemetry.pageChanges;
        for (int app = visibleStartApp; app < visibleEndApp; ++app) {
            const bool resident = m_appToSlot[(size_t)app] >= 0 &&
                                  !isArtStale(app);
            if (!resident)
                ++m_fastCarouselTelemetry.visibleMisses;
        }
    }
    // Soft-landing can temporarily shrink the single-row radius so post-HOME
    // SD work does not stack on the first interactive seconds. Grid stays ±1.
    // Single-row uses soft-landing / light-path caps. Grid stays ±1 unless a
    // light/launch cap shrinks it further (cap 0 = focused page only).
    int cacheRadius = iconsPerPage == 1 ? pageCacheRadius() : kGridPageCacheRadius;
    if (m_pageCacheRadiusCap >= 0 && cacheRadius > m_pageCacheRadiusCap)
        cacheRadius = m_pageCacheRadiusCap;
    int cacheStartPage  = std::max(0, currentPage - cacheRadius);
    int cacheEndPage    = std::min(totalPages - 1, currentPage + cacheRadius);
    int cacheStartApp   = cacheStartPage * iconsPerPage;    int cacheEndApp   = std::min(totalApps, (cacheEndPage + 1) * iconsPerPage);

    // At-rest frames used to run every scan below with nothing to do. If the
    // page and window are unchanged, no decode is in flight and every windowed
    // tile already has art, there is nothing to schedule.
    const bool windowUnchanged = cacheStartApp == m_lastEvictStart &&
                                 cacheEndApp == m_lastEvictEnd;
    if (!pageChanged && windowUnchanged && m_pendingDecodes.empty()) {
        bool windowComplete = true;
        for (int i = cacheStartApp; i < cacheEndApp; ++i) {
            if (m_appToSlot[(size_t)i] < 0 || isArtStale(i)) {
                windowComplete = false;
                break;
            }
        }
        if (windowComplete)
            return;
    }

    // Decode-ahead means workers can still be holding art for a page that just
    // left the window. Cancel that work so fast scrolling spends CPU only on
    // tiles someone can actually see; entries are dropped as they surface.
    for (auto& pending : m_pendingDecodes) {
        const int app = appIndexFor(pending);
        const bool outsideCache = app < cacheStartApp || app >= cacheEndApp;
        const bool outsideVisible = app < visibleStartApp || app >= visibleEndApp;
        // On a multi-icon grid page change, speculative neighbor work must not
        // occupy the bounded decode queue ahead of the page being entered.
        // Single-row keeps its radius prefetch across steps; canceling it on
        // every carousel move would make the next poster start from scratch.
        const bool prioritizeNewGridPage = pageChanged && iconsPerPage > 1;
        if (pending.state &&
            (outsideCache || (prioritizeNewGridPage && outsideVisible)))
            pending.state->cancelled.store(true);
    }
    m_pendingDecodes.erase(
        std::remove_if(m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [](const PendingDecode& pending) {
                return pending.state && pending.state->cancelled.load();
            }),
        m_pendingDecodes.end());

    // A click or a fling changes which side the next uploads must serve. Order
    // the queue ahead-of-cursor first (near to far), then the trailing side, so
    // a direction change spends the GPU budget on tiles about to be seen rather
    // than on ones just passed. The stable partition below keeps this order.
    if (pageChanged && iconsPerPage == 1 && m_pendingDecodes.size() > 1) {
        const int direction = currentPage >= previousPage ? 1 : -1;
        std::stable_sort(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [this, visibleStartApp, direction](const PendingDecode& lhs,
                                               const PendingDecode& rhs) {
                auto rank = [this, visibleStartApp, direction](
                                const PendingDecode& pending) {
                    const int app = appIndexFor(pending);
                    if (app < 0)
                        return std::pair<int, int>{3, 0};
                    if (app == visibleStartApp)
                        return std::pair<int, int>{0, 0};
                    const int delta = (app - visibleStartApp) * direction;
                    return delta > 0 ? std::pair<int, int>{1, delta}
                                     : std::pair<int, int>{2, -delta};
                };
                return rank(lhs) < rank(rhs);
            });
    }

    // A moving carousel may already have prefetched neighbours in the queue.
    // Put the incoming visible tile(s) first so its upload cannot sit behind
    // speculative work when the row stops.
    if (pageChanged) {
        std::stable_partition(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [this, visibleStartApp, visibleEndApp](const PendingDecode& pending) {
                const int app = appIndexFor(pending);
                return app >= visibleStartApp && app < visibleEndApp;
            });
    }

    // A fast carousel step can leave the queue full of neighbor prefetches.
    // Reserve one slot for the arriving focused tile so its SD read starts now
    // rather than after the speculative work drains.
    if (pageChanged && iconsPerPage == 1 && visibleStartApp < totalApps) {
        const int app = visibleStartApp;
        const int limit = pendingDecodeLimit();
        const bool loaded = m_appToSlot[(size_t)app] >= 0 && !isArtStale(app);
        const uint64_t titleId = app < (int)m_titleIds.size()
            ? m_titleIds[(size_t)app] : 0;
        const bool pending = titleId != 0 && std::any_of(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [titleId](const PendingDecode& decode) {
                return decode.titleId == titleId;
            });
        const bool failed = titleId != 0 && std::find(
            m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId)
            != m_failedTitleIds.end();
        if (!loaded && !pending && !failed && titleId != 0 && hasData(app) &&
            (int)m_pendingDecodes.size() >= limit) {
            int victim = -1;
            int victimDistance = -1;
            for (int i = 0; i < (int)m_pendingDecodes.size(); ++i) {
                const int pendingApp = appIndexFor(m_pendingDecodes[(size_t)i]);
                if (pendingApp >= visibleStartApp && pendingApp < visibleEndApp)
                    continue;
                const int distance = std::abs(pendingApp - app);
                if (distance > victimDistance) {
                    victimDistance = distance;
                    victim = i;
                }
            }
            if (victim >= 0) {
                if (m_pendingDecodes[(size_t)victim].state)
                    m_pendingDecodes[(size_t)victim].state->cancelled.store(true);
                m_pendingDecodes.erase(m_pendingDecodes.begin() + victim);
            }
        }
    }

    // 1. Evict textures outside the local page window. This keeps GPU memory
    //    bounded while preserving quick navigation to nearby pages.
    // Only a moved window can put a slot outside it, so the idle trim below is
    // skipped on at-rest frames instead of scanning the pool every other frame.
    const bool windowMoved = cacheStartApp != m_lastEvictStart ||
                             cacheEndApp != m_lastEvictEnd;
    m_lastEvictStart = cacheStartApp;
    m_lastEvictEnd = cacheEndApp;
    for (int i = 0; i < (int)m_pool.size() && windowMoved; ++i) {
        if (!m_pool[i])
            continue;

        int app = m_pool[i]->appIndex;
        if (app < 0 || app == m_pinnedIndex)
            continue;

        if (app < cacheStartApp || app >= cacheEndApp) {
            if (app < (int)allIcons.size())
                allIcons[app]->setTexture(nullptr);
            if (app < (int)m_appToSlot.size())
                m_appToSlot[app] = -1;
            if (app < (int)m_artProxy.size())
                m_artProxy[app] = 0;
            m_pool[i]->appIndex = -1;
            m_freeSlots.push_back(i);
        }
    }

    // 1b. Freed slots keep their art so a page that comes back is instant, but
    //     only one page's worth: without this cap the pool's high-water mark
    //     from a long scroll stayed resident (icons ~100 KB, posters 0.4-0.6 MB
    //     each). The just-evicted slots sit at the back of the free stack, so
    //     the next page still has reusable textures to upload into.
    m_idleHoldTarget = -1;
    if (windowMoved)
        trimIdleTextures(std::max(2, iconsPerPage));

    // 2. Re-attach already-loaded slots across the cache window. After a
    //    folder/model rebuild GlossyIcons are new widgets; only rebinding the
    //    focused tile left neighbours blank until they gained focus. Cover the
    //    whole hot window so single-row neighbours keep their art.
    for (int i = cacheStartApp; i < cacheEndApp; ++i) {
        int slotIdx = m_appToSlot[i];
        if (slotIdx < 0)
            continue;

        bool validSlot = slotIdx < (int)m_pool.size() &&
                         m_pool[slotIdx] &&
                         m_pool[slotIdx]->appIndex == i &&
                         m_pool[slotIdx]->texture.valid();
        if (validSlot) {
            if (i < (int)allIcons.size())
                allIcons[i]->setTexture(&m_pool[slotIdx]->texture);
        } else {
            m_appToSlot[i] = -1;
            if (i < (int)m_artProxy.size())
                m_artProxy[i] = 0;
            if (i < (int)allIcons.size())
                allIcons[i]->setTexture(nullptr);
        }
    }

    // 3. Consume completed background decodes without ever waiting for them.
    // GPU uploads remain on this thread, but are capped to one per frame so a
    // page cannot monopolise rendering or starve audio updates. Keep the
    // established synchronous upload handoff here: presenting a frame while a
    // newly registered descriptor was still being filled produced a black
    // screen on hardware even though the menu process stayed alive.
    // Square icons are cheap to upload and no longer drain the queue, so the
    // budget covers two of them per frame. A poster-sized upload keeps its
    // drain and therefore consumes the rest of the frame: two drains in one
    // frame is exactly the stutter this loop exists to avoid.
    constexpr std::size_t kCheapUploadBytes = 256u * 1024u;
    // Moving rows upload only cheap proxies, one per frame. The full poster is
    // deliberately absent from this path because its GPU drain is what made the
    // carousel hitch.
    int uploadBudget = m_scrollActive
        ? 1
        : (inBurst() ? kBurstUploadsPerFrame : kUploadsPerFrame);
    int uploads = 0;
    for (size_t pendingIndex = 0;
         pendingIndex < m_pendingDecodes.size() && uploads < uploadBudget;) {
        auto& pending = m_pendingDecodes[pendingIndex];
        if (pending.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++pendingIndex;
            continue;
        }
        // A full poster upload is a GPU drain and never belongs on a moving
        // frame. Proxies are marked poster=false and pass through cheaply; any
        // full decode left from before the scroll settles starts waiting here.
        if (m_scrollActive && pending.poster) {
            ++pendingIndex;
            continue;
        }

        // Text uses the same image-memory pool. Leave this completed decode
        // queued until enough room exists instead of triggering Font's
        // emergency cache eviction and making every label disappear.
        const bool hasReusableTexture = std::any_of(
            m_freeSlots.begin(), m_freeSlots.end(),
            [this](int slotIndex) {
                return slotIndex >= 0 && slotIndex < static_cast<int>(m_pool.size()) &&
                       m_pool[static_cast<std::size_t>(slotIndex)] &&
                       m_pool[static_cast<std::size_t>(slotIndex)]->texture.valid();
            });
        if (!hasReusableTexture &&
            gpu.imageMemoryAvailable() <= kGpuImageReserve +
                                              (kIconSize * 2) * (kIconSize * 3) * 4u) {
            ++pendingIndex;
            continue;
        }

        try {
            pending.future.get();
        } catch (...) {
            pending.state->failed = true;
        }

        const auto uploadStartedAt = std::chrono::steady_clock::now();
        const auto requestedAt = pending.requestedAt;
        const uint64_t titleId = pending.titleId;
        const bool poster = pending.poster;
        const bool proxy = pending.proxy;
        const int appIndex = appIndexFor(pending);
        const bool stillWanted = appIndex >= cacheStartApp && appIndex < cacheEndApp;
        auto state = std::move(pending.state);
        m_pendingDecodes[pendingIndex] = std::move(m_pendingDecodes.back());
        m_pendingDecodes.pop_back();

        if (state->cancelled.load())
            continue;
        if (state->failed || state->decoded.rgba.empty()) {
            recordFailedTitle(titleId);
            DebugLog::log("[streamer] decode failed title=0x%016lX",
                          static_cast<unsigned long>(titleId));
            continue;
        }
        const bool stale = isArtStale(appIndex);
        // Keep showing the previous art kind until a swap-in upload lands. The
        // decoded pixels are still exact art for this title, so keep a square
        // icon for a later visit instead of discarding completed work.
        if (!stillWanted || (m_appToSlot[(size_t)appIndex] >= 0 && !stale)) {
            rememberDecodedIcon(titleId, poster || proxy, *state);
            continue;
        }

        int poolIdx = m_appToSlot[(size_t)appIndex];
        // sLaunch's icon pool only recycles textures of the exact display
        // size: uploading into a differently sized one frees and reallocates
        // its GPU image. Move a free slot of matching dimensions to the back
        // so the pop below picks it, instead of churning a poster slot for an
        // icon or an icon slot for a poster.
        if (!m_freeSlots.empty()) {
            for (auto it = m_freeSlots.begin(); it != m_freeSlots.end(); ++it) {
                const auto& freeSlot = m_pool[(size_t)*it];
                if (freeSlot && freeSlot->texture.valid() &&
                    freeSlot->texture.width() == state->decoded.w &&
                    freeSlot->texture.height() == state->decoded.h) {
                    std::swap(*it, m_freeSlots.back());
                    break;
                }
            }
        }
        if (poolIdx < 0 || poolIdx >= (int)m_pool.size() || !m_pool[(size_t)poolIdx]) {
            if (!m_freeSlots.empty()) {
                poolIdx = m_freeSlots.back();
                m_freeSlots.pop_back();
            } else {
                poolIdx = static_cast<int>(m_pool.size());
                m_pool.emplace_back(std::make_unique<TexSlot>());
            }
        }

        auto& slot = *m_pool[(size_t)poolIdx];
        auto& decoded = state->decoded;
        const std::size_t uploadedBytes = static_cast<std::size_t>(decoded.w)
            * static_cast<std::size_t>(decoded.h) * 4u;
        // In-place swap-in keeps the icon pointer on this texture so the morph
        // never blanks. Only detach foreign consumers of a recycled slot.
        if (slot.appIndex != appIndex) {
            for (const auto& icon : allIcons) {
                if (icon && icon->texture() == &slot.texture)
                    icon->setTexture(nullptr);
            }
        }
        if (slot.texture.loadFromPixels(gpu, ren, decoded.rgba.data(), decoded.w, decoded.h)) {
            slot.appIndex = appIndex;
            m_appToSlot[(size_t)appIndex] = poolIdx;
            if (appIndex < (int)m_artProxy.size()) {
                m_artProxy[(size_t)appIndex] = proxy ? 1 : 0;
            }
            if (appIndex < (int)m_artStale.size()) {
                // A proxy that lands after the row stopped still needs its full
                // poster; the next pump must not mistake it for finished art.
                m_artStale[(size_t)appIndex] = (proxy && !m_scrollActive) ? 1 : 0;
            }
            if (appIndex < (int)allIcons.size())
                allIcons[(size_t)appIndex]->setTexture(&slot.texture);
            if (m_fastCarouselTelemetry.active) {
                const auto now = std::chrono::steady_clock::now();
                const auto waitMicros = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        uploadStartedAt - requestedAt).count());
                const auto uploadMicros = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - uploadStartedAt).count());
                auto& telemetry = m_fastCarouselTelemetry;
                ++telemetry.uploads;
                telemetry.waitMicros += waitMicros;
                telemetry.waitMaxMicros = std::max(telemetry.waitMaxMicros, waitMicros);
                telemetry.uploadMicros += uploadMicros;
                telemetry.uploadMaxMicros = std::max(telemetry.uploadMaxMicros,
                                                     uploadMicros);
            }
            // The GPU now owns a copy; retain the CPU pixels for the next time
            // this tile leaves the texture window and comes back. Proxies are
            // poster crops, not square icons, so they must not enter that cache.
            rememberDecodedIcon(titleId, poster || proxy, *state);
        } else {
            slot.appIndex = -1;
            m_appToSlot[(size_t)appIndex] = -1;
            if (appIndex < (int)m_artProxy.size())
                m_artProxy[(size_t)appIndex] = 0;
            m_freeSlots.push_back(poolIdx);
            recordFailedTitle(titleId);
            DebugLog::log("[streamer] upload failed title=0x%016lX app=%d",
                          static_cast<unsigned long>(titleId), appIndex);
        }
        ++uploads;
        if (uploadedBytes > kCheapUploadBytes)
            uploadBudget = uploads;   // this frame already paid a GPU drain
    }

    // 4. Fill the bounded decode queue for visible icons. In single-row mode,
    // include the nearest neighbours in cursor-distance order. Both the SD read and
    // image conversion happen on a worker; this call returns immediately.
    // Reused across pumps: building the order every frame otherwise allocated
    // a fresh 21-25 entry vector during every scroll step.
    m_loadOrder.clear();
    std::vector<int>& loadOrder = m_loadOrder;
    if (iconsPerPage == 1) {
        loadOrder.push_back(visibleStartApp);
        // Queue in the direction the carousel is travelling, then the other
        // side. With a four-tile window (six during a fast drag) this keeps
        // the upcoming tiles ahead of the cursor instead of splitting evenly.
        const int prefetchDirection = currentPage >= previousPage ? 1 : -1;
        for (int distance = 1; distance <= cacheRadius; ++distance) {
            const int app = visibleStartApp + prefetchDirection * distance;
            if (app >= cacheStartApp && app < cacheEndApp)
                loadOrder.push_back(app);
        }
        for (int distance = 1; distance <= cacheRadius; ++distance) {
            const int app = visibleStartApp - prefetchDirection * distance;
            if (app >= cacheStartApp && app < cacheEndApp)
                loadOrder.push_back(app);
        }
    } else {
        for (int appIndex = visibleStartApp; appIndex < visibleEndApp; ++appIndex)
            loadOrder.push_back(appIndex);
        // The grid cache window already keeps adjacent textures resident, but
        // they were never decoded ahead. Warm those pages so a fast page turn
        // lands on art instead of starting its SD read only when it stops.
        // Visible tiles remain first; neighbors only use the queue space left.
        for (int distance = 1; distance <= cacheRadius; ++distance) {
            const int nextPage = currentPage + distance;
            if (nextPage <= cacheEndPage) {
                const int begin = nextPage * iconsPerPage;
                const int end = std::min(totalApps, begin + iconsPerPage);
                for (int appIndex = begin; appIndex < end; ++appIndex)
                    loadOrder.push_back(appIndex);
            }
            const int prevPage = currentPage - distance;
            if (prevPage >= cacheStartPage) {
                const int begin = prevPage * iconsPerPage;
                const int end = std::min(totalApps, begin + iconsPerPage);
                for (int appIndex = begin; appIndex < end; ++appIndex)
                    loadOrder.push_back(appIndex);
            }
        }
    }
    for (int appIndex : loadOrder) {
        if ((int)m_pendingDecodes.size() >= pendingDecodeLimit())
            break;
        const bool stale = isArtStale(appIndex);
        if ((m_appToSlot[(size_t)appIndex] >= 0 && !stale) || !hasData(appIndex))
            continue;

        const uint64_t titleId = m_titleIds[(size_t)appIndex];
        const bool failed = std::find(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId)
            != m_failedTitleIds.end();
        const bool pending = std::any_of(
            m_pendingDecodes.begin(), m_pendingDecodes.end(),
            [titleId](const PendingDecode& decode) { return decode.titleId == titleId; });
        if (titleId == 0 || failed || pending)
            continue;

        const bool posterWanted = m_preferPosterArt
            || (m_preferPosterQuery && m_preferPosterQuery(titleId));
        // Moving rows decode the same art down to a small proxy; only the
        // resting pump reads and uploads the full poster.
        const bool proxyArt = posterWanted && m_scrollActive;
        const bool preferPoster = posterWanted && !proxyArt;
        // CPU RGBA budget, not just an entry count: a poster page would
        // otherwise hold several megabytes of decoded pixels while the GPU
        // drains two uploads per frame. Always admit the first entry so one
        // oversized tile can never stall the page.
        const std::size_t estimatedBytes = preferPoster
            ? static_cast<std::size_t>(kIconSize * 2) *
                  static_cast<std::size_t>(kIconSize * 3) * 4u
            : static_cast<std::size_t>(kIconSize) *
                  static_cast<std::size_t>(kIconSize) * 4u;
        if (!m_pendingDecodes.empty() &&
            pendingDecodeBytes() + estimatedBytes > decodeAheadBytes())
            break;

        // The pool only keeps one page of GPU textures. A tile that left that
        // window previously paid another SD read and image decode when it came
        // back. The small decoded LRU makes that step a memory copy plus the
        // normal upload, with identical pixels and dimensions.
        if (!stale && !preferPoster) {
            if (const DecodedIcon* cached = m_decodedCache.get(titleId)) {
                auto state = std::make_shared<DecodeState>();
                state->decoded = *cached;
                state->decodedBytes.store(state->decoded.rgba.size());

                PendingDecode decode;
                decode.titleId = titleId;
                decode.appIndex = appIndex;
                decode.state = state;
                decode.requestedAt = std::chrono::steady_clock::now();
                decode.estimatedBytes = state->decoded.rgba.size();
                decode.poster = false;
                decode.proxy = proxyArt;
                std::promise<void> ready;
                decode.future = ready.get_future();
                ready.set_value();
                m_pendingDecodes.push_back(std::move(decode));
                continue;
            }
        }

        // Stale art must re-read from the loader; cached bytes are the wrong kind.
        std::vector<uint8_t> compressed;
        if (!stale && appIndex < (int)m_compressed.size())
            compressed = std::move(m_compressed[(size_t)appIndex]);

        auto state = std::make_shared<DecodeState>();
        IconDataLoader loader = m_iconLoader;
        IconDataLoaderInto intoLoader = m_iconLoaderInto;
        auto work = [state, titleId, loader, intoLoader, preferPoster,
                     compressed = std::move(compressed)]() mutable {
            if (state->cancelled.load())
                return;
            if (preferPoster) {
                state->decoded = decodePosterFromCache(titleId);
                state->decodedBytes.store(state->decoded.rgba.size());
                if (!state->decoded.rgba.empty())
                    return;
                // Wrong-kind square bytes must not win over a missing poster
                // fallback — reload via the loader (which may still prefer poster).
                compressed.clear();
            }
            if (compressed.empty()) {
                if (intoLoader) {
                    std::vector<uint8_t>& scratch = iconLoadScratch();
                    if (intoLoader(titleId, scratch) && !scratch.empty()) {
                        if (state->cancelled.load())
                            return;
                        state->decoded =
                            IconStreamer::decodeIconData(scratch, preferPoster);
                        state->decodedBytes.store(state->decoded.rgba.size());
                        state->failed = state->decoded.rgba.empty();
                        return;
                    }
                } else if (loader) {
                    compressed = loader(titleId);
                }
            }
            if (state->cancelled.load())
                return;
            if (compressed.empty()) {
                state->failed = true;
                return;
            }
            state->decoded =
                IconStreamer::decodeIconData(compressed, preferPoster);
            state->decodedBytes.store(state->decoded.rgba.size());
            state->failed = state->decoded.rgba.empty();
        };

        PendingDecode decode;
        decode.titleId = titleId;
        decode.appIndex = appIndex;
        decode.state = state;
        decode.requestedAt = std::chrono::steady_clock::now();
        decode.estimatedBytes = estimatedBytes;
        decode.poster = preferPoster;
        decode.proxy = proxyArt;
        if (m_threadPool) {
            decode.future = m_threadPool->submit(std::move(work));
        } else {
            std::promise<void> completed;
            decode.future = completed.get_future();
            try {
                work();
                completed.set_value();
            } catch (...) {
                completed.set_exception(std::current_exception());
            }
        }
        m_pendingDecodes.push_back(std::move(decode));
        if (kLogDecodeSchedule)
            DebugLog::log("[streamer] scheduled title=0x%016lX app=%d pending=%d",
                      static_cast<unsigned long>(titleId), appIndex,
                      static_cast<int>(m_pendingDecodes.size()));
    }
    noteFastCarouselDecodePeak();
}

void IconStreamer::forceReload(int currentPage, int iconsPerPage,
                                nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::vector<std::shared_ptr<GlossyIcon>>& allIcons)
{
    // Widgets keep non-owning pointers into m_pool. Detach every pointer before
    // destroying the pool; otherwise the next frame can submit a descriptor
    // backed by a freed MemBlock and put deko3d into a permanent page-fault
    // state. This is especially visible when an overridden icon is applied
    // while edit/move mode still owns a ghost of the focused application.
    for (const auto& icon : allIcons) {
        if (icon)
            icon->setTexture(nullptr);
    }

    // Throw away all loaded state so onPageChanged re-does everything.
    for (auto& slot : m_pool) slot->appIndex = -1;
    m_freeSlots.clear();
    for (int i = 0; i < (int)m_pool.size(); ++i) m_freeSlots.push_back(i);
    std::fill(m_appToSlot.begin(), m_appToSlot.end(), -1);

    // Clear the Texture objects themselves (GPU memory + descriptor slots)
    // because a forceReload typically follows a full GPU reset.
    m_pool.clear();
    m_freeSlots.clear();
    cancelPending();
    m_pendingDecodes.clear();
    m_failedTitleIds.clear();
    // Drop cached JPEG/PNG bytes too — otherwise a Poster loaded for single-row
    // would keep showing after switching back to the square grid.
    for (auto& bytes : m_compressed)
        bytes.clear();
    m_artStale.assign(m_appToSlot.size(), 0);
    m_artProxy.assign(m_appToSlot.size(), 0);

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    onPageChanged(currentPage, iconsPerPage, gpu, ren, allIcons);
}

void IconStreamer::cancelShadowPrefetch() {
    for (auto& entry : m_shadowPrefetch) {
        if (entry.state)
            entry.state->cancelled.store(true);
    }
    m_shadowPrefetch.clear();
}

void IconStreamer::startShadowPrefetch(bool preferPoster,
                                       const std::vector<std::uint64_t>& titleIds,
                                       IconDataLoader loader) {
    cancelShadowPrefetch();
    m_shadowPreferPoster = preferPoster;
    if (!loader)
        return;

    PreferPosterQuery preferQuery = m_preferPosterQuery;
    std::unordered_map<std::uint64_t, bool> seen;
    m_shadowPrefetch.reserve(titleIds.size());
    for (const std::uint64_t titleId : titleIds) {
        if (titleId == 0 || seen[titleId])
            continue;
        seen[titleId] = true;

        ShadowPrefetchEntry entry;
        entry.titleId = titleId;
        entry.state = std::make_shared<DecodeState>();
        auto state = entry.state;
        const bool titlePreferPoster = preferPoster
            || (preferQuery && preferQuery(titleId));
        IconDataLoaderInto intoLoader = m_iconLoaderInto;
        auto work = [state, titleId, loader, intoLoader, titlePreferPoster]() {
            const auto started = std::chrono::steady_clock::now();
            if (state->cancelled.load())
                return;
            const char* source = "jpeg";
            if (titlePreferPoster) {
                state->decoded = decodePosterFromCache(titleId);
                if (!state->decoded.rgba.empty()) {
                    source = "fit-cache";
                    DebugLog::log("[layout-prefetch] shadow decode title=%016lX "
                                  "source=%s %dx%d in %ldms",
                                  static_cast<unsigned long>(titleId), source,
                                  state->decoded.w, state->decoded.h,
                                  static_cast<long>(std::chrono::duration_cast<
                                      std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - started)
                                                        .count()));
                    return;
                }
            }
            std::vector<uint8_t> compressed;
            if (intoLoader) {
                std::vector<uint8_t>& scratch = iconLoadScratch();
                if (intoLoader(titleId, scratch) && !scratch.empty())
                    state->decoded =
                        IconStreamer::decodeIconData(scratch, titlePreferPoster);
            } else if (loader) {
                compressed = loader(titleId);
            }
            if (state->cancelled.load())
                return;
            if (state->decoded.rgba.empty() && compressed.empty()) {
                state->failed = true;
                DebugLog::log("[layout-prefetch] shadow decode title=%016lX FAILED empty",
                              static_cast<unsigned long>(titleId));
                return;
            }
            if (state->decoded.rgba.empty())
                state->decoded =
                    IconStreamer::decodeIconData(compressed, titlePreferPoster);
            state->failed = state->decoded.rgba.empty();
            DebugLog::log("[layout-prefetch] shadow decode title=%016lX source=%s "
                          "%dx%d failed=%d in %ldms",
                          static_cast<unsigned long>(titleId), source,
                          state->decoded.w, state->decoded.h,
                          state->failed ? 1 : 0,
                          static_cast<long>(std::chrono::duration_cast<
                              std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - started).count()));
        };
        if (m_threadPool) {
            entry.future = m_threadPool->submit(std::move(work));
        } else {
            std::promise<void> completed;
            entry.future = completed.get_future();
            try {
                work();
                completed.set_value();
            } catch (...) {
                completed.set_exception(std::current_exception());
            }
        }
        m_shadowPrefetch.push_back(std::move(entry));
    }
}

void IconStreamer::pumpShadowPrefetch(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    for (auto& entry : m_shadowPrefetch) {
        if (entry.decodedReady || !entry.future.valid())
            continue;
        if (entry.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            continue;
        try {
            entry.future.get();
        } catch (...) {
            if (entry.state)
                entry.state->failed = true;
        }
        if (!entry.state || entry.state->cancelled.load()) {
            entry.failed = true;
            entry.decodedReady = true;
            entry.uploaded = true;
            continue;
        }
        entry.failed = entry.state->failed || entry.state->decoded.rgba.empty();
        if (!entry.failed)
            entry.decoded = std::move(entry.state->decoded);
        entry.decodedReady = true;
        if (entry.failed)
            entry.uploaded = true;
        entry.state.reset();
    }

    // Spread GPU uploads across frames so HOME stays responsive during prefetch.
    int uploads = 0;
    for (auto& entry : m_shadowPrefetch) {
        if (uploads >= kShadowUploadsPerPump)
            break;
        if (!entry.decodedReady || entry.failed || entry.uploaded)
            continue;
        if (entry.decoded.rgba.empty()) {
            entry.failed = true;
            entry.uploaded = true;
            continue;
        }
        auto tex = std::make_unique<nxui::Texture>();
        if (!tex->loadFromPixels(gpu, ren, entry.decoded.rgba.data(),
                                 entry.decoded.w, entry.decoded.h)) {
            entry.failed = true;
            entry.uploaded = true;
            continue;
        }
        entry.decoded.rgba.clear();
        entry.decoded.rgba.shrink_to_fit();
        entry.texture = std::move(tex);
        entry.uploaded = true;
        DebugLog::log("[layout-prefetch] shadow upload title=%016lX ok",
                      static_cast<unsigned long>(entry.titleId));
        ++uploads;
    }
}

bool IconStreamer::shadowPrefetchReady() const {
    if (m_shadowPrefetch.empty())
        return true;
    for (const auto& entry : m_shadowPrefetch) {
        if (!entry.decodedReady || !entry.uploaded)
            return false;
    }
    return true;
}

void IconStreamer::installShadowPrefetch(
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons) {
    if (m_shadowPrefetch.empty())
        return;

    std::unordered_map<std::uint64_t, ShadowPrefetchEntry*> byTitle;
    byTitle.reserve(m_shadowPrefetch.size());
    for (auto& entry : m_shadowPrefetch) {
        if (entry.uploaded && !entry.failed && entry.texture && entry.texture->valid())
            byTitle.emplace(entry.titleId, &entry);
    }

    // Destination preference is now live; drop stale bytes so we never re-bind
    // the previous art kind for these titles.
    m_preferPosterArt = m_shadowPreferPoster;
    for (auto& bytes : m_compressed)
        bytes.clear();

    for (int appIndex = 0; appIndex < (int)allIcons.size(); ++appIndex) {
        if (!allIcons[(size_t)appIndex])
            continue;
        const uint64_t titleId = appIndex < (int)m_titleIds.size()
            ? m_titleIds[(size_t)appIndex] : 0;
        if (titleId == 0)
            continue;
        auto found = byTitle.find(titleId);
        if (found == byTitle.end() || !found->second->texture)
            continue;

        int poolIdx = appIndex < (int)m_appToSlot.size() ? m_appToSlot[(size_t)appIndex] : -1;
        if (poolIdx < 0 || poolIdx >= (int)m_pool.size() || !m_pool[(size_t)poolIdx]) {
            if (!m_freeSlots.empty()) {
                poolIdx = m_freeSlots.back();
                m_freeSlots.pop_back();
            } else {
                poolIdx = static_cast<int>(m_pool.size());
                m_pool.emplace_back(std::make_unique<TexSlot>());
            }
        }

        auto& slot = *m_pool[(size_t)poolIdx];
        if (slot.appIndex != appIndex) {
            for (const auto& icon : allIcons) {
                if (icon && icon->texture() == &slot.texture)
                    icon->setTexture(nullptr);
            }
        }
        // Steal the already-uploaded texture — no GPU stall on the morph frame.
        slot.texture = std::move(*found->second->texture);
        found->second->texture.reset();
        slot.appIndex = appIndex;
        if (appIndex < (int)m_appToSlot.size())
            m_appToSlot[(size_t)appIndex] = poolIdx;
        if (appIndex < (int)m_artStale.size())
            m_artStale[(size_t)appIndex] = 0;
        if (appIndex < (int)m_artProxy.size())
            m_artProxy[(size_t)appIndex] = 0;
        allIcons[(size_t)appIndex]->setTexture(&slot.texture);
    }

    cancelShadowPrefetch();
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::invalidateTitleArt(uint64_t titleId) {
    if (titleId == 0)
        return;
    const auto found = std::find(m_titleIds.begin(), m_titleIds.end(), titleId);
    if (found == m_titleIds.end())
        return;
    const int appIndex = static_cast<int>(found - m_titleIds.begin());
    for (auto& pending : m_pendingDecodes) {
        if (pending.titleId == titleId && pending.state)
            pending.state->cancelled.store(true);
    }
    if (appIndex < static_cast<int>(m_compressed.size()))
        m_compressed[static_cast<std::size_t>(appIndex)].clear();
    if (appIndex < static_cast<int>(m_artStale.size()))
        m_artStale[static_cast<std::size_t>(appIndex)] = 1;
    if (appIndex < static_cast<int>(m_artProxy.size()))
        m_artProxy[static_cast<std::size_t>(appIndex)] = 0;
    m_failedTitleIds.erase(
        std::remove(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId),
        m_failedTitleIds.end());
    m_decodedCache.erase(titleId);
    m_lastPage = -1;
    m_lastIconsPerPage = -1;
}

void IconStreamer::reloadTitle(
    uint64_t titleId, int currentPage, int iconsPerPage,
    nxui::GpuDevice& gpu, nxui::Renderer& ren,
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons) {
    const auto found = std::find(m_titleIds.begin(), m_titleIds.end(), titleId);
    if (found == m_titleIds.end()) return;
    const int appIndex = static_cast<int>(found - m_titleIds.begin());

    for (auto& pending : m_pendingDecodes) {
        if (pending.titleId == titleId && pending.state)
            pending.state->cancelled.store(true);
    }
    m_failedTitleIds.erase(
        std::remove(m_failedTitleIds.begin(), m_failedTitleIds.end(), titleId),
        m_failedTitleIds.end());
    m_decodedCache.erase(titleId);
    if (appIndex < static_cast<int>(m_compressed.size()))
        m_compressed[(size_t)appIndex].clear();
    if (appIndex < static_cast<int>(m_artStale.size()))
        m_artStale[(size_t)appIndex] = 1;
    if (appIndex < static_cast<int>(m_artProxy.size()))
        m_artProxy[(size_t)appIndex] = 0;

    const int slotIndex = appIndex < static_cast<int>(m_appToSlot.size())
        ? m_appToSlot[(size_t)appIndex] : -1;
    if (appIndex < static_cast<int>(allIcons.size()) && allIcons[(size_t)appIndex])
        allIcons[(size_t)appIndex]->setTexture(nullptr);
    if (slotIndex >= 0 && slotIndex < static_cast<int>(m_pool.size()) && m_pool[(size_t)slotIndex]) {
        m_pool[(size_t)slotIndex]->appIndex = -1;
        m_appToSlot[(size_t)appIndex] = -1;
        if (std::find(m_freeSlots.begin(), m_freeSlots.end(), slotIndex) == m_freeSlots.end())
            m_freeSlots.push_back(slotIndex);
    }

    m_lastPage = -1;
    m_lastIconsPerPage = -1;
    onPageChanged(currentPage, iconsPerPage, gpu, ren, allIcons);
}
