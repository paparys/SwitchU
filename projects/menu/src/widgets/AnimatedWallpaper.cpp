#include "AnimatedWallpaper.hpp"

#include "core/DebugLog.hpp"

#include <nxui/third_party/stb/stb_image.h>
#include <webp/demux.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

// The HOME background shares the GPU budget with icons, the WaraWara shapes and
// the UI itself, so the animation is bounded rather than fully resident.
constexpr std::size_t kWallpaperGpuBudget = 8u * 1024u * 1024u;
constexpr int kWallpaperMaxWidth = 480;
constexpr int kWallpaperMaxFrames = 48;
constexpr int kWallpaperMinFrames = 3;

constexpr float kDefaultFrameSeconds = 1.f / 15.f;

std::string lowerExtension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return {};
    std::string ext = path.substr(dot);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool readWholeFile(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open())
        return false;
    const std::streamoff size = input.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > 64ull * 1024ull * 1024ull)
        return false;
    input.seekg(0, std::ios::beg);
    out.assign(static_cast<std::size_t>(size), 0);
    return static_cast<bool>(input.read(reinterpret_cast<char*>(out.data()),
                                        static_cast<std::streamsize>(out.size())));
}

/// Nearest-neighbour scale. This is background art, so a box filter would cost
/// decode time for no visible gain at wallpaper resolution.
void scaleRgba(const std::uint8_t* source, int sw, int sh, int dw, int dh,
               std::vector<std::uint8_t>& out) {
    out.resize(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4u);
    if (sw == dw && sh == dh) {
        std::memcpy(out.data(), source, out.size());
        return;
    }
    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(sh - 1, y * sh / dh);
        const std::uint8_t* row = source + static_cast<std::size_t>(sy) * sw * 4u;
        std::uint8_t* target = out.data() + static_cast<std::size_t>(y) * dw * 4u;
        for (int x = 0; x < dw; ++x) {
            const int sx = std::min(sw - 1, x * sw / dw);
            std::memcpy(target + static_cast<std::size_t>(x) * 4u,
                        row + static_cast<std::size_t>(sx) * 4u, 4u);
        }
    }
}

int clampFrameCount(int wanted, int width, int height) {
    if (width <= 0 || height <= 0)
        return 0;
    const std::size_t bytesPerFrame = static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height) * 4u;
    const int byBudget = static_cast<int>(
        std::max<std::size_t>(1u, kWallpaperGpuBudget / std::max<std::size_t>(1u, bytesPerFrame)));
    return std::clamp(std::min(wanted, byBudget), kWallpaperMinFrames, kWallpaperMaxFrames);
}

// ---------------------------------------------------------------------------
// MP4 (ISO-BMFF) Motion-JPEG reader
// ---------------------------------------------------------------------------

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint64_t be64(const std::uint8_t* p) {
    return (static_cast<std::uint64_t>(be32(p)) << 32) | be32(p + 4);
}

struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::size_t size() const { return end > begin ? end - begin : 0; }
};

template <typename Fn>
void forEachBox(const std::vector<std::uint8_t>& data, Span parent, Fn&& fn) {
    std::size_t pos = parent.begin;
    while (pos + 8 <= parent.end) {
        const std::uint64_t declared = be32(&data[pos]);
        std::size_t header = 8;
        std::uint64_t boxSize = declared;
        if (declared == 1) {
            if (pos + 16 > parent.end)
                return;
            boxSize = be64(&data[pos + 8]);
            header = 16;
        } else if (declared == 0) {
            boxSize = parent.end - pos;
        }
        if (boxSize < header || boxSize > parent.end - pos)
            return;
        const Span child{pos + header, pos + static_cast<std::size_t>(boxSize)};
        const char type[5] = {
            static_cast<char>(data[pos + 4]), static_cast<char>(data[pos + 5]),
            static_cast<char>(data[pos + 6]), static_cast<char>(data[pos + 7]), '\0'};
        if (!fn(type, child))
            return;
        pos += static_cast<std::size_t>(boxSize);
    }
}

bool findBox(const std::vector<std::uint8_t>& data, Span parent, const char* type,
             Span& out) {
    bool found = false;
    forEachBox(data, parent, [&](const char* current, Span box) {
        if (std::strncmp(current, type, 4) == 0) {
            out = box;
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

struct Mp4JpegTrack {
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint32_t> sizes;
    double seconds = 0.0;
};

bool parseMp4Jpeg(const std::vector<std::uint8_t>& data, Mp4JpegTrack& out) {
    Span moov;
    if (!findBox(data, Span{0, data.size()}, "moov", moov))
        return false;

    bool parsed = false;
    forEachBox(data, moov, [&](const char* type, Span trak) {
        if (parsed || std::strncmp(type, "trak", 4) != 0)
            return true;

        Span mdia, minf, stbl;
        if (!findBox(data, trak, "mdia", mdia) ||
            !findBox(data, mdia, "minf", minf) ||
            !findBox(data, minf, "stbl", stbl))
            return true;

        Span stsd;
        if (!findBox(data, stbl, "stsd", stsd) || stsd.size() < 16)
            return true;
        const std::uint32_t entryCount = be32(&data[stsd.begin + 4]);
        if (entryCount == 0)
            return true;
        const char* fourcc = reinterpret_cast<const char*>(&data[stsd.begin + 12]);
        if (std::strncmp(fourcc, "jpeg", 4) != 0 &&
            std::strncmp(fourcc, "mjpa", 4) != 0 &&
            std::strncmp(fourcc, "mjpb", 4) != 0) {
            return true; // H.264 and friends need NVDEC; keep looking for a
                         // Motion-JPEG track.
        }

        Span stsz;
        if (!findBox(data, stbl, "stsz", stsz) || stsz.size() < 12)
            return true;
        const std::uint32_t constantSize = be32(&data[stsz.begin + 4]);
        const std::uint32_t sampleCount = be32(&data[stsz.begin + 8]);
        if (sampleCount == 0 || sampleCount > 500000)
            return true;
        std::vector<std::uint32_t> sizes(sampleCount);
        if (constantSize != 0) {
            std::fill(sizes.begin(), sizes.end(), constantSize);
        } else {
            if (stsz.size() < 12u + static_cast<std::size_t>(sampleCount) * 4u)
                return true;
            for (std::uint32_t i = 0; i < sampleCount; ++i)
                sizes[i] = be32(&data[stsz.begin + 12 + static_cast<std::size_t>(i) * 4u]);
        }

        Span stsc;
        if (!findBox(data, stbl, "stsc", stsc) || stsc.size() < 8)
            return true;
        const std::uint32_t stscCount = be32(&data[stsc.begin + 4]);
        if (stscCount == 0 || stsc.size() < 8u + static_cast<std::size_t>(stscCount) * 12u)
            return true;
        struct ChunkRun { std::uint32_t firstChunk; std::uint32_t samplesPerChunk; };
        std::vector<ChunkRun> runs(stscCount);
        for (std::uint32_t i = 0; i < stscCount; ++i) {
            const std::size_t at = stsc.begin + 8 + static_cast<std::size_t>(i) * 12u;
            runs[i].firstChunk = be32(&data[at]);
            runs[i].samplesPerChunk = be32(&data[at + 4]);
        }

        Span chunkOffsets;
        bool chunks64 = false;
        if (!findBox(data, stbl, "stco", chunkOffsets)) {
            if (!findBox(data, stbl, "co64", chunkOffsets))
                return true;
            chunks64 = true;
        }
        if (chunkOffsets.size() < 8)
            return true;
        const std::uint32_t chunkCount = be32(&data[chunkOffsets.begin + 4]);
        if (chunkCount == 0)
            return true;
        const std::size_t stride = chunks64 ? 8u : 4u;
        if (chunkOffsets.size() < 8u + static_cast<std::size_t>(chunkCount) * stride)
            return true;

        std::vector<std::uint64_t> offsets(sampleCount, 0);
        std::size_t sampleIndex = 0;
        std::size_t runIndex = 0;
        for (std::uint32_t chunk = 1; chunk <= chunkCount && sampleIndex < sampleCount; ++chunk) {
            while (runIndex + 1 < runs.size() && runs[runIndex + 1].firstChunk <= chunk)
                ++runIndex;
            const std::uint32_t perChunk = std::max<std::uint32_t>(1, runs[runIndex].samplesPerChunk);
            const std::size_t at = chunkOffsets.begin + 8 +
                static_cast<std::size_t>(chunk - 1) * stride;
            std::uint64_t offset = chunks64 ? be64(&data[at]) : be32(&data[at]);
            for (std::uint32_t i = 0; i < perChunk && sampleIndex < sampleCount; ++i) {
                offsets[sampleIndex] = offset;
                offset += sizes[sampleIndex];
                ++sampleIndex;
            }
        }

        // Timing: prefer the sample table, fall back to the media header.
        double seconds = 0.0;
        Span stts;
        if (findBox(data, stbl, "stts", stts) && stts.size() >= 8) {
            const std::uint32_t sttsCount = be32(&data[stts.begin + 4]);
            if (sttsCount > 0 && stts.size() >= 8u + static_cast<std::size_t>(sttsCount) * 8u) {
                std::uint64_t totalUnits = 0;
                for (std::uint32_t i = 0; i < sttsCount; ++i) {
                    const std::size_t at = stts.begin + 8 + static_cast<std::size_t>(i) * 8u;
                    totalUnits += static_cast<std::uint64_t>(be32(&data[at])) * be32(&data[at + 4]);
                }
                double timescale = 1.0;
                Span mdhd;
                if (findBox(data, mdia, "mdhd", mdhd) && mdhd.size() >= 20) {
                    const std::uint8_t version = data[mdhd.begin];
                    const std::size_t scaleAt = mdhd.begin + (version == 1 ? 20u : 12u);
                    if (scaleAt + 4 <= mdhd.end) {
                        const std::uint32_t ts = be32(&data[scaleAt]);
                        if (ts != 0)
                            timescale = static_cast<double>(ts);
                    }
                }
                seconds = static_cast<double>(totalUnits) / timescale;
            }
        }

        out.offsets = std::move(offsets);
        out.sizes = sizes;
        out.seconds = seconds;
        parsed = true;
        return false;
    });

    return parsed && !out.offsets.empty();
}

} // namespace

bool AnimatedWallpaper::load(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                             const std::string& path, int targetFps) {
    clear();
    m_path = path;
    m_error.clear();
    if (path.empty()) {
        m_error = "No wallpaper configured";
        return false;
    }

    std::vector<std::uint8_t> bytes;
    if (!readWholeFile(path, bytes)) {
        m_error = "Wallpaper file could not be read";
        DebugLog::log("[wallpaper] unreadable: %s", path.c_str());
        return false;
    }

    const int fps = std::clamp(targetFps, 5, 30);
    const std::string ext = lowerExtension(path);
    std::vector<std::uint8_t> framePixels;
    std::vector<float> durations;

    auto upload = [&](const std::uint8_t* rgba, int width, int height) {
        // A single transparent pixel anywhere means the layer no longer hides
        // what is behind it, so the background gradient must keep drawing.
        if (m_opaque) {
            const std::size_t pixels = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height);
            for (std::size_t i = 0; i < pixels; ++i) {
                if (rgba[i * 4u + 3u] != 255u) {
                    m_opaque = false;
                    break;
                }
            }
        }
        nxui::Texture texture;
        if (!texture.loadFromPixels(gpu, ren, rgba, width, height))
            return false;
        m_frames.push_back(std::move(texture));
        return true;
    };

    if (ext == ".mp4" || ext == ".mov" || ext == ".m4v") {
        Mp4JpegTrack track;
        if (!parseMp4Jpeg(bytes, track)) {
            m_error = "MP4 wallpaper needs Motion-JPEG video";
            DebugLog::log("[wallpaper] '%s' is not a Motion-JPEG MP4", path.c_str());
            return false;
        }
        const int wanted = clampFrameCount(kWallpaperMaxFrames, kWallpaperMaxWidth, 360);
        const int sampleTotal = static_cast<int>(track.offsets.size());
        const int frameCount = std::clamp(std::min(wanted, sampleTotal),
                                          std::min(kWallpaperMinFrames, sampleTotal),
                                          sampleTotal);
        if (frameCount <= 0) {
            m_error = "Wallpaper has no frames";
            return false;
        }
        const double perFrame = track.seconds > 0.0
            ? std::max(1.0 / 30.0, track.seconds / frameCount)
            : 1.0 / fps;
        int lastWidth = 0, lastHeight = 0;
        for (int i = 0; i < frameCount; ++i) {
            const int sample = frameCount == 1 ? 0
                : static_cast<int>((static_cast<long long>(i) * (sampleTotal - 1)) /
                                   std::max(1, frameCount - 1));
            const std::uint64_t offset = track.offsets[static_cast<std::size_t>(sample)];
            const std::uint32_t size = track.sizes[static_cast<std::size_t>(sample)];
            if (size == 0 || offset + size > bytes.size())
                continue;
            int width = 0, height = 0, channels = 0;
            stbi_uc* decoded = stbi_load_from_memory(
                bytes.data() + offset, static_cast<int>(size), &width, &height, &channels, 4);
            if (!decoded)
                continue;
            const int targetW = width > kWallpaperMaxWidth ? kWallpaperMaxWidth : width;
            const int targetH = std::max(1, height * targetW / std::max(1, width));
            scaleRgba(decoded, width, height, targetW, targetH, framePixels);
            stbi_image_free(decoded);
            if (!upload(framePixels.data(), targetW, targetH))
                break;
            durations.push_back(static_cast<float>(perFrame));
            lastWidth = targetW;
            lastHeight = targetH;
        }
        if (!m_frames.empty())
            DebugLog::log("[wallpaper] MP4 frames=%d size=%dx%d from %s",
                          static_cast<int>(m_frames.size()), lastWidth, lastHeight,
                          path.c_str());
    } else if (ext == ".webp") {
        WebPData webpData;
        webpData.bytes = bytes.data();
        webpData.size = bytes.size();

        WebPAnimDecoderOptions options;
        if (!WebPAnimDecoderOptionsInit(&options)) {
            m_error = "WebP wallpaper could not be initialised";
            return false;
        }
        options.color_mode = MODE_RGBA;
        WebPAnimDecoder* decoder = WebPAnimDecoderNew(&webpData, &options);
        if (!decoder) {
            m_error = "WebP wallpaper could not be decoded";
            return false;
        }
        WebPAnimInfo info{};
        if (!WebPAnimDecoderGetInfo(decoder, &info) ||
            info.canvas_width == 0 || info.canvas_height == 0) {
            WebPAnimDecoderDelete(decoder);
            m_error = "WebP wallpaper has no frames";
            return false;
        }

        const int sourceW = static_cast<int>(info.canvas_width);
        const int sourceH = static_cast<int>(info.canvas_height);
        const int targetW = std::min(sourceW, kWallpaperMaxWidth);
        const int targetH = std::max(1, sourceH * targetW / std::max(1, sourceW));
        const int totalFrames = static_cast<int>(info.frame_count);
        const int keep = clampFrameCount(std::min(kWallpaperMaxFrames, totalFrames),
                                         targetW, targetH);

        int nextKeep = 0;
        int lastTimestamp = 0;
        int frameIndex = 0;
        while (WebPAnimDecoderHasMoreFrames(decoder) && nextKeep < keep) {
            std::uint8_t* buffer = nullptr;
            int timestamp = 0;
            if (!WebPAnimDecoderGetNext(decoder, &buffer, &timestamp))
                break;
            const int targetIndex = keep <= 1
                ? 0
                : static_cast<int>((static_cast<long long>(nextKeep) * (totalFrames - 1)) /
                                   std::max(1, keep - 1));
            if (frameIndex == targetIndex && buffer) {
                scaleRgba(buffer, sourceW, sourceH, targetW, targetH, framePixels);
                if (!upload(framePixels.data(), targetW, targetH))
                    break;
                if (!durations.empty()) {
                    const float delta = static_cast<float>(timestamp - lastTimestamp) / 1000.f;
                    durations.back() = std::clamp(delta, 1.f / 60.f, 4.f);
                }
                durations.push_back(kDefaultFrameSeconds);
                lastTimestamp = timestamp;
                ++nextKeep;
            }
            ++frameIndex;
        }
        WebPAnimDecoderDelete(decoder);
        if (!durations.empty())
            durations.back() = durations.size() > 1 ? durations[durations.size() - 2]
                                                   : kDefaultFrameSeconds;
        if (!m_frames.empty())
            DebugLog::log("[wallpaper] WebP frames=%d/%d size=%dx%d from %s",
                          static_cast<int>(m_frames.size()), totalFrames,
                          targetW, targetH, path.c_str());
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        // Still wallpapers reuse the frame path: one opaque texture and no
        // per-frame update. Keeping them here means restoreImage() still knows
        // the theme image path and the wallpaper covers the screen exactly like
        // an animated one.
        int width = 0, height = 0, channels = 0;
        stbi_uc* decoded = stbi_load_from_memory(
            bytes.data(), static_cast<int>(bytes.size()), &width, &height,
            &channels, 4);
        if (!decoded || width <= 0 || height <= 0) {
            if (decoded)
                stbi_image_free(decoded);
            m_error = "Image wallpaper could not be decoded";
            DebugLog::log("[wallpaper] image decode failed: %s", path.c_str());
            return false;
        }
        const int targetW = std::min(width, kWallpaperMaxWidth);
        const int targetH = std::max(1, height * targetW / std::max(1, width));
        scaleRgba(decoded, width, height, targetW, targetH, framePixels);
        stbi_image_free(decoded);
        if (!upload(framePixels.data(), targetW, targetH)) {
            m_error = "Image wallpaper could not be uploaded";
            return false;
        }
        durations.push_back(kDefaultFrameSeconds);
        DebugLog::log("[wallpaper] image size=%dx%d from %s", targetW, targetH,
                      path.c_str());
    } else if (ext == ".gif") {
        // stb_image composites the GIF for us: one RGBA buffer holding every
        // frame back to back, plus a per-frame delay table in milliseconds.
        int width = 0, height = 0, frames = 0, channels = 0;
        int* delays = nullptr;
        stbi_uc* decoded = stbi_load_gif_from_memory(
            bytes.data(), static_cast<int>(bytes.size()), &delays, &width,
            &height, &frames, &channels, 4);
        if (!decoded || width <= 0 || height <= 0 || frames <= 0) {
            if (decoded)
                stbi_image_free(decoded);
            if (delays)
                std::free(delays);
            m_error = "GIF wallpaper could not be decoded";
            DebugLog::log("[wallpaper] GIF decode failed: %s", path.c_str());
            return false;
        }
        const int targetW = std::min(width, kWallpaperMaxWidth);
        const int targetH = std::max(1, height * targetW / std::max(1, width));
        const int keep = clampFrameCount(std::min(kWallpaperMaxFrames, frames),
                                         targetW, targetH);
        const std::size_t frameBytes = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height) * 4u;
        for (int i = 0; i < keep; ++i) {
            const int index = keep <= 1 ? 0
                : static_cast<int>((static_cast<long long>(i) * (frames - 1)) /
                                   std::max(1, keep - 1));
            scaleRgba(decoded + frameBytes * static_cast<std::size_t>(index),
                      width, height, targetW, targetH, framePixels);
            if (!upload(framePixels.data(), targetW, targetH))
                break;
            const float seconds = delays
                ? static_cast<float>(delays[index]) / 1000.f
                : kDefaultFrameSeconds;
            durations.push_back(seconds > 0.f ? seconds : kDefaultFrameSeconds);
        }
        stbi_image_free(decoded);
        if (delays)
            std::free(delays);
        if (!m_frames.empty())
            DebugLog::log("[wallpaper] GIF frames=%d/%d size=%dx%d from %s",
                          static_cast<int>(m_frames.size()), frames,
                          targetW, targetH, path.c_str());
    } else {
        m_error = "Unsupported wallpaper format";
        DebugLog::log("[wallpaper] unsupported extension for %s", path.c_str());
        return false;
    }

    if (m_frames.empty()) {
        if (m_error.empty())
            m_error = "Wallpaper could not be decoded";
        return false;
    }

    m_frameSeconds = std::move(durations);
    m_frameSeconds.resize(m_frames.size(), kDefaultFrameSeconds);
    for (float& seconds : m_frameSeconds)
        seconds = std::clamp(seconds, 1.f / 60.f, 4.f);
    m_index = 0;
    m_elapsed = 0.f;
    return true;
}

std::size_t AnimatedWallpaper::gpuBytes() const {
    std::size_t bytes = 0;
    for (const auto& frame : m_frames)
        bytes += frame.allocationSize();
    return bytes;
}

void AnimatedWallpaper::clear() {
    m_frames.clear();
    m_opaque = true;
    m_frameSeconds.clear();
    m_index = 0;
    m_elapsed = 0.f;
    m_path.clear();
    m_error.clear();
}

void AnimatedWallpaper::update(float dt) {
    if (m_frames.size() < 2)
        return;
    m_elapsed += std::max(0.f, dt);
    // Guard against a long frame hitch burning through the whole loop.
    int guard = 0;
    while (m_elapsed >= m_frameSeconds[m_index] && guard++ < 8) {
        m_elapsed -= m_frameSeconds[m_index];
        m_index = (m_index + 1) % m_frames.size();
    }
}

nxui::Texture* AnimatedWallpaper::texture() {
    if (m_frames.empty())
        return nullptr;
    return &m_frames[m_index % m_frames.size()];
}

const nxui::Texture* AnimatedWallpaper::texture() const {
    if (m_frames.empty())
        return nullptr;
    return &m_frames[m_index % m_frames.size()];
}
