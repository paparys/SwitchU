#include "ArtworkCache.hpp"

#include "core/DebugLog.hpp"

#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <system_error>
#include <unordered_map>

namespace {

constexpr std::uint32_t kArtworkCacheMagic = 0x43415553; // "SUAC"
// 2: decode() now samples bilinearly, so every older point-sampled cache is
// stale and must be rebuilt instead of reused.
constexpr std::uint32_t kArtworkCacheVersion = 2;

// Decoded fit RGBA in RAM so neighbour poster warm after layout morph skips
// repeated SD .fit320x480.rgba-cache reads (~10–30ms each). Disk cache remains.
// Cap ~4 MiB / 6 entries (was 10 MiB / 16) — MEMORY_OPTIMIZATION_PLAN.
constexpr std::size_t kFitRamMaxEntries = 6;
constexpr std::size_t kFitRamMaxBytes = 4ull * 1024ull * 1024ull;

struct ArtworkCacheHeader {
    std::uint32_t magic = kArtworkCacheMagic;
    std::uint32_t version = kArtworkCacheVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t sourceSize = 0;
    std::int64_t sourceWriteTime = 0;
    std::uint64_t payloadSize = 0;
};
static_assert(sizeof(ArtworkCacheHeader) == 40);

struct SourceStamp {
    std::uint64_t size = 0;
    std::int64_t writeTime = 0;
    bool valid = false;
};

SourceStamp sourceStamp(const std::string& path) {
    std::error_code ec;
    SourceStamp stamp;
    stamp.size = std::filesystem::file_size(path, ec);
    if (ec) return stamp;
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) return stamp;
    stamp.writeTime = static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    stamp.valid = true;
    return stamp;
}

std::string cachePathFor(const std::string& source, int width, int height) {
    return source + "." + std::to_string(width) + "x" + std::to_string(height)
        + ".rgba-cache";
}

std::string fitCachePathFor(const std::string& source, int maxWidth, int maxHeight) {
    return source + ".fit" + std::to_string(maxWidth) + "x" + std::to_string(maxHeight)
        + ".rgba-cache";
}

std::string fitRamKey(const std::string& source, int maxWidth, int maxHeight) {
    return source + "\n" + std::to_string(maxWidth) + "x" + std::to_string(maxHeight);
}

struct FitRamEntry {
    std::string key;
    SourceStamp stamp;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct FitRamCache {
    std::mutex mutex;
    std::list<FitRamEntry> order; // front = most recently used
    std::unordered_map<std::string, std::list<FitRamEntry>::iterator> index;
    std::size_t bytes = 0;

    void evictIfNeeded() {
        while ((!order.empty())
               && (order.size() > kFitRamMaxEntries || bytes > kFitRamMaxBytes)) {
            const FitRamEntry& old = order.back();
            bytes -= old.rgba.size();
            index.erase(old.key);
            order.pop_back();
        }
    }

    bool tryGet(const std::string& key, const SourceStamp& stamp,
                std::vector<std::uint8_t>& rgba, int& outWidth, int& outHeight) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = index.find(key);
        if (found == index.end())
            return false;
        auto it = found->second;
        if (!stamp.valid
            || it->stamp.size != stamp.size
            || it->stamp.writeTime != stamp.writeTime
            || it->rgba.empty()) {
            bytes -= it->rgba.size();
            order.erase(it);
            index.erase(found);
            return false;
        }
        order.splice(order.begin(), order, it);
        rgba = it->rgba;
        outWidth = it->width;
        outHeight = it->height;
        return true;
    }

    void put(const std::string& key, const SourceStamp& stamp,
             int width, int height, const std::vector<std::uint8_t>& rgba) {
        if (!stamp.valid || width <= 0 || height <= 0 || rgba.empty())
            return;
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = index.find(key);
        if (found != index.end()) {
            bytes -= found->second->rgba.size();
            order.erase(found->second);
            index.erase(found);
        }
        order.push_front(FitRamEntry{key, stamp, width, height, rgba});
        index[key] = order.begin();
        bytes += rgba.size();
        evictIfNeeded();
    }

    void erase(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = index.find(key);
        if (found == index.end())
            return;
        bytes -= found->second->rgba.size();
        order.erase(found->second);
        index.erase(found);
    }
};

FitRamCache& fitRamCache() {
    static FitRamCache cache;
    return cache;
}

bool loadFit(const std::string& source, int maxWidth, int maxHeight,
             std::vector<std::uint8_t>& rgba, int& outWidth, int& outHeight) {
    const SourceStamp stamp = sourceStamp(source);
    if (!stamp.valid) return false;
    const std::string ramKey = fitRamKey(source, maxWidth, maxHeight);
    if (fitRamCache().tryGet(ramKey, stamp, rgba, outWidth, outHeight)) {
        DebugLog::log("[steamgriddb-ui] fit ram hit path=%s %dx%d",
                      source.c_str(), outWidth, outHeight);
        return true;
    }

    const std::string cachePath = fitCachePathFor(source, maxWidth, maxHeight);
    const auto started = std::chrono::steady_clock::now();
    std::ifstream input(cachePath, std::ios::binary);
    if (!input) return false;

    ArtworkCacheHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!input || header.magic != kArtworkCacheMagic
        || header.version != kArtworkCacheVersion
        || header.width == 0 || header.height == 0
        || header.width > static_cast<std::uint32_t>(maxWidth)
        || header.height > static_cast<std::uint32_t>(maxHeight)
        || header.sourceSize != stamp.size
        || header.sourceWriteTime != stamp.writeTime
        || header.payloadSize
            != static_cast<std::uint64_t>(header.width) * header.height * 4ULL) {
        return false;
    }

    rgba.resize(static_cast<std::size_t>(header.payloadSize));
    input.read(reinterpret_cast<char*>(rgba.data()),
               static_cast<std::streamsize>(header.payloadSize));
    if (!input) {
        rgba.clear();
        return false;
    }
    outWidth = static_cast<int>(header.width);
    outHeight = static_cast<int>(header.height);
    fitRamCache().put(ramKey, stamp, outWidth, outHeight, rgba);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    DebugLog::log("[steamgriddb-ui] fit cache hit path=%s %dx%d read=%ldms",
                  cachePath.c_str(), outWidth, outHeight, elapsed);
    return true;
}

void saveFit(const std::string& source, int maxWidth, int maxHeight,
             int width, int height, const std::vector<std::uint8_t>& rgba) {
    const SourceStamp stamp = sourceStamp(source);
    const std::uint64_t expected = static_cast<std::uint64_t>(width)
                                 * static_cast<std::uint64_t>(height) * 4ULL;
    if (!stamp.valid || width <= 0 || height <= 0 || width > maxWidth
        || height > maxHeight || rgba.size() != expected) {
        return;
    }

    ArtworkCacheHeader header;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.sourceSize = stamp.size;
    header.sourceWriteTime = stamp.writeTime;
    header.payloadSize = expected;

    const std::string cachePath = fitCachePathFor(source, maxWidth, maxHeight);
    const std::string temporaryPath = cachePath + ".tmp";
    const auto started = std::chrono::steady_clock::now();
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(rgba.data()),
                 static_cast<std::streamsize>(rgba.size()));
    output.close();
    if (!output) {
        std::error_code cleanupEc;
        std::filesystem::remove(temporaryPath, cleanupEc);
        return;
    }
    std::error_code ec;
    std::filesystem::remove(cachePath, ec);
    ec.clear();
    std::filesystem::rename(temporaryPath, cachePath, ec);
    if (ec) {
        std::filesystem::remove(temporaryPath, ec);
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    DebugLog::log("[steamgriddb-ui] fit cache saved path=%s %dx%d write=%ldms",
                  cachePath.c_str(), width, height, elapsed);
}

bool load(const std::string& source, int width, int height,
          std::vector<std::uint8_t>& rgba, const std::atomic<bool>* cancel = nullptr) {
    const SourceStamp stamp = sourceStamp(source);
    if (!stamp.valid) return false;
    const std::string cachePath = cachePathFor(source, width, height);
    const auto started = std::chrono::steady_clock::now();
    std::ifstream input(cachePath, std::ios::binary);
    if (!input) return false;

    ArtworkCacheHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    const std::uint64_t expected = static_cast<std::uint64_t>(width)
                                 * static_cast<std::uint64_t>(height) * 4ULL;
    if (!input || header.magic != kArtworkCacheMagic
        || header.version != kArtworkCacheVersion
        || header.width != static_cast<std::uint32_t>(width)
        || header.height != static_cast<std::uint32_t>(height)
        || header.sourceSize != stamp.size
        || header.sourceWriteTime != stamp.writeTime
        || header.payloadSize != expected) {
        return false;
    }

    // Read in chunks so a superseded request can stop early: at 1280x720 this
    // is a 3.5 MiB read, the single biggest cost of a cached hero while
    // scrolling through titles that have already been decoded.
    rgba.resize(static_cast<std::size_t>(expected));
    constexpr std::size_t kChunk = 1u << 20;
    std::size_t offset = 0;
    while (offset < rgba.size()) {
        if (cancel && cancel->load()) {
            rgba.clear();
            return false;
        }
        const std::size_t block = std::min(kChunk, rgba.size() - offset);
        input.read(reinterpret_cast<char*>(rgba.data() + offset),
                   static_cast<std::streamsize>(block));
        if (!input) {
            rgba.clear();
            return false;
        }
        offset += block;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    DebugLog::log("[steamgriddb-ui] raw cache hit path=%s bytes=%llu read=%ldms",
                  cachePath.c_str(), static_cast<unsigned long long>(expected), elapsed);
    return true;
}

void save(const std::string& source, int width, int height,
          const std::vector<std::uint8_t>& rgba) {
    const SourceStamp stamp = sourceStamp(source);
    const std::uint64_t expected = static_cast<std::uint64_t>(width)
                                 * static_cast<std::uint64_t>(height) * 4ULL;
    if (!stamp.valid || rgba.size() != expected) return;

    ArtworkCacheHeader header;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.sourceSize = stamp.size;
    header.sourceWriteTime = stamp.writeTime;
    header.payloadSize = expected;

    const std::string cachePath = cachePathFor(source, width, height);
    const std::string temporaryPath = cachePath + ".tmp";
    const auto started = std::chrono::steady_clock::now();
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(rgba.data()),
                 static_cast<std::streamsize>(rgba.size()));
    output.close();
    if (!output) {
        std::error_code cleanupEc;
        std::filesystem::remove(temporaryPath, cleanupEc);
        return;
    }
    std::error_code ec;
    std::filesystem::remove(cachePath, ec);
    ec.clear();
    std::filesystem::rename(temporaryPath, cachePath, ec);
    if (ec) {
        std::filesystem::remove(temporaryPath, ec);
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    DebugLog::log("[steamgriddb-ui] raw cache saved path=%s bytes=%llu write=%ldms",
                  cachePath.c_str(), static_cast<unsigned long long>(expected), elapsed);
}

} // namespace

namespace steamgriddb::artwork {

DecodedImage decode(const std::string& source, int outputWidth, int outputHeight,
                    bool fill, const std::atomic<bool>* cancel, bool useCache) {
    DecodedImage out;
    out.width = std::max(1, outputWidth);
    out.height = std::max(1, outputHeight);
    auto cacheSave = [useCache](const std::string& file, int w, int h,
                                const std::vector<std::uint8_t>& pixels) {
        if (useCache)
            save(file, w, h, pixels);
    };
    if (cancel && cancel->load()) {
        out.width = 0;
        out.height = 0;
        return out;
    }
    if (useCache && load(source, out.width, out.height, out.rgba, cancel))
        return out;
    if (cancel && cancel->load()) {
        out.width = 0;
        out.height = 0;
        return out;
    }

    const auto loadStarted = std::chrono::steady_clock::now();
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load(source.c_str(), &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) stbi_image_free(raw);
        return out;
    }
    const auto loadedAt = std::chrono::steady_clock::now();
    // Bail before the scale pass: the selection has already moved on.
    if (cancel && cancel->load()) {
        stbi_image_free(raw);
        out.rgba.clear();
        out.width = 0;
        out.height = 0;
        return out;
    }

    if (width == out.width && height == out.height) {
        out.rgba.assign(raw, raw + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(raw);
        cacheSave(source, out.width, out.height, out.rgba);
        DebugLog::log("[steamgriddb-ui] image path=%s source=%dx%d load=%ldms scale=0ms",
                      source.c_str(), width, height,
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          loadedAt - loadStarted).count());
        return out;
    }

    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    const float scaleX = static_cast<float>(out.width) / width;
    const float scaleY = static_cast<float>(out.height) / height;
    const float scale = fill ? std::max(scaleX, scaleY) : std::min(scaleX, scaleY);
    const float drawnWidth = width * scale;
    const float drawnHeight = height * scale;
    const float offsetX = (out.width - drawnWidth) * 0.5f;
    const float offsetY = (out.height - drawnHeight) * 0.5f;
    const float inverseScale = 1.f / scale;

    // Bilinear sampling, with the per-axis source maps built once. Doing a
    // floor() per pixel per channel made the 1280x720 canvas noticeably more
    // expensive to fill than the old 640x360 one.
    struct AxisSample {
        int i0 = 0;
        int i1 = 0;
        // 15-bit fixed-point weights instead of a float fraction. The hero
        // scale pass runs over ~1M pixels per decode; sharing four integer
        // weights across the four channels is markedly cheaper on the ARM
        // cores than eight float multiplies per channel.
        int w0 = 0;
        int w1 = 0;
        bool valid = true;
    };
    // The source maps depend only on the source/output dimensions and fill
    // mode, never on the pixels. Hero and logo decodes alternate between the
    // same few size pairs, so reuse the exact fixed-point maps per worker.
    struct AxisMapCache {
        int sourceW = 0;
        int sourceH = 0;
        int outputW = 0;
        int outputH = 0;
        bool fill = false;
        std::vector<AxisSample> x;
        std::vector<AxisSample> y;
    };
    static thread_local std::array<AxisMapCache, 4> axisCache;
    static thread_local std::size_t axisNext = 0;
    AxisMapCache* maps = nullptr;
    for (auto& entry : axisCache) {
        if (entry.sourceW == width && entry.sourceH == height
            && entry.outputW == out.width && entry.outputH == out.height
            && entry.fill == fill) {
            maps = &entry;
            break;
        }
    }
    if (!maps) {
        AxisMapCache& entry = axisCache[axisNext++ % axisCache.size()];
        entry.sourceW = width;
        entry.sourceH = height;
        entry.outputW = out.width;
        entry.outputH = out.height;
        entry.fill = fill;
        entry.x.resize(static_cast<std::size_t>(out.width));
        entry.y.resize(static_cast<std::size_t>(out.height));
        for (int x = 0; x < out.width; ++x) {
            const float srcX = (static_cast<float>(x) - offsetX) * inverseScale;
            AxisSample& a = entry.x[static_cast<std::size_t>(x)];
            a.valid = true;
            if (!fill && (srcX < 0.f || srcX >= static_cast<float>(width))) {
                a.valid = false;
                continue;
            }
            const int base = static_cast<int>(std::floor(srcX));
            a.i0 = std::clamp(base, 0, width - 1);
            a.i1 = std::clamp(base + 1, 0, width - 1);
            a.w1 = static_cast<int>((srcX - static_cast<float>(base)) * 32768.f + 0.5f);
            a.w0 = 32768 - a.w1;
        }
        for (int y = 0; y < out.height; ++y) {
            const float srcY = (static_cast<float>(y) - offsetY) * inverseScale;
            AxisSample& a = entry.y[static_cast<std::size_t>(y)];
            a.valid = true;
            if (!fill && (srcY < 0.f || srcY >= static_cast<float>(height))) {
                a.valid = false;
                continue;
            }
            const int base = static_cast<int>(std::floor(srcY));
            a.i0 = std::clamp(base, 0, height - 1);
            a.i1 = std::clamp(base + 1, 0, height - 1);
            a.w1 = static_cast<int>((srcY - static_cast<float>(base)) * 32768.f + 0.5f);
            a.w0 = 32768 - a.w1;
        }
        maps = &entry;
    }
    const auto& xAxis = maps->x;
    const auto& yAxis = maps->y;

    for (int y = 0; y < out.height; ++y) {
        if (cancel && (y & 31) == 0 && cancel->load()) {
            out.rgba.clear();
            out.width = 0;
            out.height = 0;
            stbi_image_free(raw);
            return out;
        }
        const AxisSample& ya = yAxis[static_cast<std::size_t>(y)];
        if (!ya.valid) continue;
        const std::uint8_t* row0 =
            raw + static_cast<std::size_t>(ya.i0) * static_cast<std::size_t>(width) * 4;
        const std::uint8_t* row1 =
            raw + static_cast<std::size_t>(ya.i1) * static_cast<std::size_t>(width) * 4;
        std::uint8_t* destinationRow =
            out.rgba.data() + static_cast<std::size_t>(y) * out.width * 4;
        for (int x = 0; x < out.width; ++x) {
            const AxisSample& xa = xAxis[static_cast<std::size_t>(x)];
            if (!xa.valid) continue;
            const std::uint8_t* p00 = row0 + static_cast<std::size_t>(xa.i0) * 4;
            const std::uint8_t* p10 = row0 + static_cast<std::size_t>(xa.i1) * 4;
            const std::uint8_t* p01 = row1 + static_cast<std::size_t>(xa.i0) * 4;
            const std::uint8_t* p11 = row1 + static_cast<std::size_t>(xa.i1) * 4;
            // 15-bit bilinear weights, computed once per pixel and reused by
            // every channel. Same sampling as the float version, which rounded
            // the same way (add half, truncate).
            const int w00 = (xa.w0 * ya.w0) >> 15;
            const int w10 = (xa.w1 * ya.w0) >> 15;
            const int w01 = (xa.w0 * ya.w1) >> 15;
            const int w11 = (xa.w1 * ya.w1) >> 15;
            std::uint8_t* dst = destinationRow + static_cast<std::size_t>(x) * 4;
            for (int c = 0; c < 4; ++c) {
                dst[c] = static_cast<std::uint8_t>(
                    (p00[c] * w00 + p10[c] * w10
                     + p01[c] * w01 + p11[c] * w11 + 16384) >> 15);
            }
        }
    }
    const auto scaledAt = std::chrono::steady_clock::now();
    stbi_image_free(raw);
    cacheSave(source, out.width, out.height, out.rgba);
    DebugLog::log("[steamgriddb-ui] image path=%s source=%dx%d load=%ldms scale=%ldms",
                  source.c_str(), width, height,
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      loadedAt - loadStarted).count(),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      scaledAt - loadedAt).count());
    return out;
}

bool prepare(const std::string& source, int outputWidth, int outputHeight, bool fill) {
    return !decode(source, outputWidth, outputHeight, fill).rgba.empty();
}

void remove(const std::string& source, int outputWidth, int outputHeight) {
    std::error_code ec;
    std::filesystem::remove(cachePathFor(source, outputWidth, outputHeight), ec);
}

DecodedImage decodeFit(const std::string& source, int maxWidth, int maxHeight) {
    DecodedImage out;
    const int limitW = std::max(1, maxWidth);
    const int limitH = std::max(1, maxHeight);
    if (loadFit(source, limitW, limitH, out.rgba, out.width, out.height))
        return out;

    const auto loadStarted = std::chrono::steady_clock::now();
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load(source.c_str(), &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw) stbi_image_free(raw);
        return out;
    }
    const auto loadedAt = std::chrono::steady_clock::now();

    int dstW = width;
    int dstH = height;
    if (width > limitW || height > limitH) {
        const float scale = std::min(static_cast<float>(limitW) / static_cast<float>(width),
                                     static_cast<float>(limitH) / static_cast<float>(height));
        dstW = std::max(1, static_cast<int>(static_cast<float>(width) * scale + 0.5f));
        dstH = std::max(1, static_cast<int>(static_cast<float>(height) * scale + 0.5f));
    }

    out.width = dstW;
    out.height = dstH;
    const SourceStamp stamp = sourceStamp(source);
    const std::string ramKey = fitRamKey(source, limitW, limitH);
    if (dstW == width && dstH == height) {
        out.rgba.assign(raw, raw + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(raw);
        saveFit(source, limitW, limitH, out.width, out.height, out.rgba);
        fitRamCache().put(ramKey, stamp, out.width, out.height, out.rgba);
        DebugLog::log("[steamgriddb-ui] fit path=%s source=%dx%d load=%ldms scale=0ms",
                      source.c_str(), width, height,
                      std::chrono::duration_cast<std::chrono::milliseconds>(
                          loadedAt - loadStarted).count());
        return out;
    }

    out.rgba.assign(static_cast<std::size_t>(dstW) * dstH * 4, 0);
    const float scaleX = static_cast<float>(width) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(height) / static_cast<float>(dstH);
    for (int y = 0; y < dstH; ++y) {
        const float srcYf = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        int y0 = static_cast<int>(srcYf);
        if (y0 < 0) y0 = 0;
        int y1 = y0 + 1;
        if (y1 >= height) y1 = height - 1;
        const float fy = srcYf - static_cast<float>(y0);
        for (int x = 0; x < dstW; ++x) {
            const float srcXf = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            int x0 = static_cast<int>(srcXf);
            if (x0 < 0) x0 = 0;
            int x1 = x0 + 1;
            if (x1 >= width) x1 = width - 1;
            const float fx = srcXf - static_cast<float>(x0);
            const std::uint8_t* p00 = raw + (static_cast<std::size_t>(y0) * width + x0) * 4;
            const std::uint8_t* p10 = raw + (static_cast<std::size_t>(y0) * width + x1) * 4;
            const std::uint8_t* p01 = raw + (static_cast<std::size_t>(y1) * width + x0) * 4;
            const std::uint8_t* p11 = raw + (static_cast<std::size_t>(y1) * width + x1) * 4;
            std::uint8_t* dst = out.rgba.data()
                + (static_cast<std::size_t>(y) * dstW + x) * 4;
            for (int c = 0; c < 4; ++c) {
                dst[c] = static_cast<std::uint8_t>(
                    p00[c] * (1.f - fx) * (1.f - fy)
                    + p10[c] * fx * (1.f - fy)
                    + p01[c] * (1.f - fx) * fy
                    + p11[c] * fx * fy + 0.5f);
            }
        }
    }
    stbi_image_free(raw);
    saveFit(source, limitW, limitH, out.width, out.height, out.rgba);
    fitRamCache().put(ramKey, stamp, out.width, out.height, out.rgba);
    DebugLog::log("[steamgriddb-ui] fit path=%s source=%dx%d -> %dx%d load=%ldms scale=%ldms",
                  source.c_str(), width, height, dstW, dstH,
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      loadedAt - loadStarted).count(),
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - loadedAt).count());
    return out;
}

bool prepareFit(const std::string& source, int maxWidth, int maxHeight) {
    return !decodeFit(source, maxWidth, maxHeight).rgba.empty();
}

void removeFit(const std::string& source, int maxWidth, int maxHeight) {
    fitRamCache().erase(fitRamKey(source, maxWidth, maxHeight));
    std::error_code ec;
    std::filesystem::remove(fitCachePathFor(source, maxWidth, maxHeight), ec);
}

} // namespace steamgriddb::artwork
