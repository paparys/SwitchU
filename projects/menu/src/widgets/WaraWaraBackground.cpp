#include "WaraWaraBackground.hpp"
#include "AnimatedWallpaper.hpp"
#include "core/DebugLog.hpp"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <nxui/third_party/stb/stb_image.h>

namespace {

constexpr int kBackgroundRenderReserveVertices = 13312;
constexpr int kWorstCaseShapeVertices = 36;
constexpr int kGlassShapeLayers = 3;
// Theme manifests are untrusted configuration. A 14x8 grid combined with
// quad symmetry previously expanded to 448 shapes per frame. Preserve the
// composition while bounding fill/geometry cost on the handheld GPU.
constexpr int kGridRenderedShapeBudget = 160;
constexpr int kFloatingRenderedShapeBudget = 80;
constexpr int kDenseRenderCopyThreshold = 64;
constexpr int kRoundedPointCapacity = 16;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;
constexpr float kShapeBodyAlphaMin = 0.84f;
constexpr float kShapeBodyAlphaMax = 0.92f;
constexpr float kShapeHighlightAlpha = 0.18f;
constexpr float kFloatingShapeEdgeAlpha = 0.11f;
constexpr float kGridShapeEdgeAlpha = 0.08f;

float degreesToRadians(float degrees) {
    return degrees * kPi / 180.f;
}

void appendArcPoints(std::array<nxui::Vec2, kRoundedPointCapacity>& points,
                     int& count,
                     float centerX,
                     float centerY,
                     float radius,
                     float startAngle,
                     float endAngle,
                     int segments,
                     bool includeStart)
{
    int startIndex = includeStart ? 0 : 1;
    for (int i = startIndex; i <= segments && count < kRoundedPointCapacity; ++i) {
        float t = segments > 0 ? (float)i / (float)segments : 0.f;
        float angle = startAngle + (endAngle - startAngle) * t;
        points[count++] = {centerX + std::cos(angle) * radius,
                           centerY + std::sin(angle) * radius};
    }
}

float random01() {
    return (std::rand() % 1000) / 1000.f;
}

float randomRange(float minValue, float maxValue) {
    return minValue + (maxValue - minValue) * random01();
}

float wrapValue(float value, float minValue, float maxValue) {
    float span = maxValue - minValue;
    if (span <= 0.f)
        return minValue;
    while (value < minValue)
        value += span;
    while (value > maxValue)
        value -= span;
    return value;
}

int symmetryMultiplier(WaraWaraBackground::Symmetry symmetry) {
    switch (symmetry) {
        case WaraWaraBackground::Symmetry::MirrorHorizontal:
        case WaraWaraBackground::Symmetry::MirrorVertical:
            return 2;
        case WaraWaraBackground::Symmetry::Quad:
            return 4;
        case WaraWaraBackground::Symmetry::None:
        default:
            return 1;
    }
}

int maxSafeBaseShapeCount(const WaraWaraBackground::Config& config) {
    const int multiplier = std::max(1, symmetryMultiplier(config.symmetry));
    const int maxBudget = std::max(1, nxui::GpuDevice::MAX_VERTICES - kBackgroundRenderReserveVertices);
    const int perShapeBudget = std::max(1, kWorstCaseShapeVertices * kGlassShapeLayers * multiplier);
    return std::max(1, maxBudget / perShapeBudget);
}

int maxVisualBaseShapeCount(const WaraWaraBackground::Config& config) {
    const int multiplier = std::max(1, symmetryMultiplier(config.symmetry));
    const int renderedBudget = (config.layout == WaraWaraBackground::Layout::Grid)
        ? kGridRenderedShapeBudget
        : kFloatingRenderedShapeBudget;
    return std::max(1, renderedBudget / multiplier);
}

int renderedShapeCopies(const WaraWaraBackground::Config& config, int baseShapeCount) {
    return std::max(0, baseShapeCount) * std::max(1, symmetryMultiplier(config.symmetry));
}

bool useDenseRenderMode(const WaraWaraBackground::Config& config, int baseShapeCount) {
    return config.layout == WaraWaraBackground::Layout::Grid
        && renderedShapeCopies(config, baseShapeCount) >= kDenseRenderCopyThreshold;
}

std::uint64_t estimateVertexCount(const WaraWaraBackground::Config& config, int baseShapeCount) {
    return (std::uint64_t)std::max(0, baseShapeCount)
        * (std::uint64_t)std::max(1, symmetryMultiplier(config.symmetry))
        * (std::uint64_t)kWorstCaseShapeVertices
        * (std::uint64_t)kGlassShapeLayers;
}

} // namespace

WaraWaraBackground::WaraWaraBackground() { regenerate(30); }

void WaraWaraBackground::setShapesEnabled(bool enabled) {
    if (m_shapesEnabled == enabled)
        return;
    m_shapesEnabled = enabled;
    if (!enabled) {
        m_shapes.clear();
    } else {
        regenerate(0);
    }
    DebugLog::log("[background] shapes enabled=%d", enabled ? 1 : 0);
}

void WaraWaraBackground::setConfig(const Config& config) {
    m_config = config;
    m_config.shapeCount = std::max(1, m_config.shapeCount);
    m_config.gridColumns = std::max(1, m_config.gridColumns);
    m_config.gridRows = std::max(1, m_config.gridRows);
    m_config.spacingX = std::max(1.f, m_config.spacingX);
    m_config.spacingY = std::max(1.f, m_config.spacingY);
    m_config.sizeMin = std::max(1.f, m_config.sizeMin);
    m_config.sizeMax = std::max(m_config.sizeMin, m_config.sizeMax);
    m_config.speedMin = std::max(0.f, m_config.speedMin);
    m_config.speedMax = std::max(m_config.speedMin, m_config.speedMax);
    m_config.wobble = std::max(0.f, m_config.wobble);
    m_config.opacity = std::clamp(m_config.opacity, 0.f, 1.f);
    m_config.cornerRoundness = std::clamp(m_config.cornerRoundness, 0.f, 1.f);
    m_config.imageOpacity = std::clamp(m_config.imageOpacity, 0.f, 1.f);
    regenerate(0);
}

bool WaraWaraBackground::loadSourcePixels(const std::string& path) {
    int w = 0, h = 0, ch = 0;
    uint8_t* raw = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!raw || w <= 0 || h <= 0) {
        if (raw)
            stbi_image_free(raw);
        m_sourceRgba.clear();
        m_sourceW = m_sourceH = 0;
        return false;
    }
    // Match Texture::loadFromFile's max-side 1280 nearest downscale so the
    // untreated bake is pixel-identical to the previous upload path.
    constexpr int kMaxSide = 1280;
    if (w > kMaxSide || h > kMaxSide) {
        const float scale = std::min(static_cast<float>(kMaxSide) / w,
                                     static_cast<float>(kMaxSide) / h);
        const int dw = std::max(1, static_cast<int>(w * scale));
        const int dh = std::max(1, static_cast<int>(h * scale));
        m_sourceRgba.resize(static_cast<std::size_t>(dw) * dh * 4u);
        for (int y = 0; y < dh; ++y) {
            const int sy = y * h / dh;
            for (int x = 0; x < dw; ++x) {
                const int sx = x * w / dw;
                std::memcpy(&m_sourceRgba[(static_cast<std::size_t>(y) * dw + x) * 4u],
                            raw + (static_cast<std::size_t>(sy) * w + sx) * 4u, 4u);
            }
        }
        m_sourceW = dw;
        m_sourceH = dh;
    } else {
        m_sourceRgba.assign(raw, raw + static_cast<std::size_t>(w) * h * 4u);
        m_sourceW = w;
        m_sourceH = h;
    }
    stbi_image_free(raw);
    return !m_sourceRgba.empty();
}

bool WaraWaraBackground::bakeTreatment() {
    if (m_sourceRgba.empty() || m_sourceW <= 0 || m_sourceH <= 0 ||
        !m_gpu || !m_ren || hasAnimatedWallpaper())
        return false;

    m_treatedRgba = m_sourceRgba;
    const int w = m_sourceW;
    const int h = m_sourceH;

    const int vibrance = std::clamp(m_treatmentVibrance, 0, 100);
    if (vibrance > 0) {
        const float amount = vibrance / 100.f * 0.65f;
        auto toByte = [](float v) {
            return static_cast<std::uint8_t>(std::clamp(v * 255.f + 0.5f, 0.f, 255.f));
        };
        for (std::size_t i = 0; i + 3 < m_treatedRgba.size(); i += 4) {
            std::uint8_t* p = &m_treatedRgba[i];
            const float r = p[0] / 255.f, g = p[1] / 255.f, b = p[2] / 255.f;
            const float maxc = std::max({r, g, b});
            const float minc = std::min({r, g, b});
            const float sat = maxc > 0.0001f ? (maxc - minc) / maxc : 0.f;
            // Muted colours gain the most, already-saturated ones barely move.
            const float boost = 1.f + amount * (1.f - sat);
            const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
            p[0] = toByte(lum + (r - lum) * boost);
            p[1] = toByte(lum + (g - lum) * boost);
            p[2] = toByte(lum + (b - lum) * boost);
        }
    }

    const int sharpness = std::clamp(m_treatmentSharpness, 0, 100);
    if (sharpness > 0) {
        m_treatScratch = m_treatedRgba;
        const float amount = sharpness / 100.f;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int sumR = 0, sumG = 0, sumB = 0, taps = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    const int sy = std::clamp(y + oy, 0, h - 1);
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int sx = std::clamp(x + ox, 0, w - 1);
                        const std::uint8_t* q =
                            &m_treatScratch[(static_cast<std::size_t>(sy) * w + sx) * 4u];
                        sumR += q[0]; sumG += q[1]; sumB += q[2];
                        ++taps;
                    }
                }
                std::uint8_t* p =
                    &m_treatedRgba[(static_cast<std::size_t>(y) * w + x) * 4u];
                auto sharpen = [amount](int src, int avg) {
                    return static_cast<std::uint8_t>(std::clamp(
                        static_cast<float>(src) +
                        (static_cast<float>(src) - static_cast<float>(avg)) * amount,
                        0.f, 255.f));
                };
                p[0] = sharpen(p[0], sumR / taps);
                p[1] = sharpen(p[1], sumG / taps);
                p[2] = sharpen(p[2], sumB / taps);
            }
        }
    }

    const int noise = std::clamp(m_treatmentNoise, 0, 100);
    if (noise > 0) {
        const float amplitude = noise / 100.f * 20.f;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                std::uint32_t n = static_cast<std::uint32_t>(x) * 374761393u +
                                  static_cast<std::uint32_t>(y) * 668265263u +
                                  0x9E3779B9u;
                n = (n ^ (n >> 13)) * 1274126177u;
                const float delta =
                    ((n & 0xFFFFu) / 65535.f - 0.5f) * 2.f * amplitude;
                std::uint8_t* p =
                    &m_treatedRgba[(static_cast<std::size_t>(y) * w + x) * 4u];
                for (int c = 0; c < 3; ++c)
                    p[c] = static_cast<std::uint8_t>(std::clamp(
                        static_cast<float>(p[c]) + delta, 0.f, 255.f));
            }
        }
    }

    // Fresh texture: no in-place reload, so the old image retires on the normal
    // deferred queue instead of draining the GPU on every slider change.
    nxui::Texture baked;
    if (!baked.loadFromPixels(*m_gpu, *m_ren, m_treatedRgba.data(), w, h))
        return false;
    m_backgroundImage = std::move(baked);
    m_treatedRgba.clear();
    m_treatedRgba.shrink_to_fit();
    m_treatScratch.clear();
    m_treatScratch.shrink_to_fit();
    return true;
}

bool WaraWaraBackground::loadImage(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path) {
    if (path.empty()) {
        clearImage();
        return false;
    }

    // Remember the path even while a wallpaper owns the background slot: the
    // image can then be re-read when the wallpaper is turned off without
    // holding a texture nobody samples (1280x720 RGBA is ~3.7 MB).
    m_imagePath = path;
    if (hasAnimatedWallpaper()) {
        m_backgroundImage = nxui::Texture{};
        return true;
    }

    m_gpu = &gpu;
    m_ren = &ren;
    if (treatmentActive()) {
        if (!loadSourcePixels(path))
            return false;
        return bakeTreatment();
    }
    // Untreated wallpapers take the existing path and keep no source pixels.
    nxui::Texture texture;
    // Theme packages are untrusted input and may contain multi-megapixel
    // backgrounds. The display is 1280x720, so larger images only increase
    // decode time, heap pressure and GPU memory use.
    if (!texture.loadFromFile(gpu, ren, path, 1280))
        return false;

    m_backgroundImage = std::move(texture);
    m_sourceRgba.clear();
    m_sourceW = m_sourceH = 0;
    m_treatDirty = false;
    return true;
}

void WaraWaraBackground::clearImage() {
    m_backgroundImage = nxui::Texture{};
    m_imagePath.clear();
    m_sourceRgba.clear();
    m_sourceW = m_sourceH = 0;
    m_treatDirty = false;
}

bool WaraWaraBackground::restoreImage(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    if (m_backgroundImage.valid())
        return true;
    if (m_imagePath.empty() || hasAnimatedWallpaper())
        return false;
    return loadImage(gpu, ren, m_imagePath);
}

WaraWaraBackground::~WaraWaraBackground() = default;

std::size_t WaraWaraBackground::gpuBytes() const {
    std::size_t bytes = m_backgroundImage.valid() ? m_backgroundImage.allocationSize() : 0;
    if (m_animWallpaper)
        bytes += m_animWallpaper->gpuBytes();
    return bytes;
}

bool WaraWaraBackground::hasAnimatedWallpaper() const {
    return m_animWallpaper && m_animWallpaper->valid();
}

bool WaraWaraBackground::loadAnimatedWallpaper(nxui::GpuDevice& gpu,
                                               nxui::Renderer& ren,
                                               const std::string& path, int fps) {
    // Keep the current wallpaper alive until the replacement decodes. A failed
    // reload should leave the last used wallpaper on screen, not the theme.
    if (path.empty())
        return false;
    auto wallpaper = std::make_unique<AnimatedWallpaper>();
    if (!wallpaper->load(gpu, ren, path, fps))
        return false;
    m_animWallpaper = std::move(wallpaper);
    // The wallpaper owns the background slot now: release the still image's
    // texture but keep its path so clearing the wallpaper can restore it.
    m_backgroundImage = nxui::Texture{};
    return true;
}

void WaraWaraBackground::clearAnimatedWallpaper() {
    m_animWallpaper.reset();
}

const nxui::Texture* WaraWaraBackground::activeWallpaperTexture() const {
    if (m_animWallpaper) {
        const nxui::Texture* frame = m_animWallpaper->texture();
        if (frame && frame->valid())
            return frame;
    }
    return m_backgroundImage.valid() ? &m_backgroundImage : nullptr;
}

void WaraWaraBackground::drawWallpaper(nxui::Renderer& ren, float opacityScale) const {
    const nxui::Texture* wallpaper = activeWallpaperTexture();
    if (!wallpaper)
        return;
    // A moving wallpaper owns the background slot, so it draws at full strength
    // even when the theme ships no still image. ThemePreset defaults
    // imageOpacity to 0, and gating on it made every wallpaper invisible unless
    // the theme happened to set a background image.
    const bool animated = hasAnimatedWallpaper();
    float alpha = m_opacity * std::clamp(opacityScale, 0.f, 1.f);
    if (!animated)
        alpha *= m_config.imageOpacity;
    if (alpha <= 0.002f)
        return;

    const nxui::Rect dest = backgroundImageRect();
    auto* texture = const_cast<nxui::Texture*>(wallpaper);
    ren.drawTexture(texture, dest, nxui::Color::white().withAlpha(alpha));

    // Multi-tap soften: a handful of offset copies read as a real blur on a
    // still and stay far cheaper than a full-screen shader pass every frame.
    const int blur = std::clamp(m_treatmentBlur, 0, 16);
    if (blur > 0) {
        const float spread = static_cast<float>(blur) * 0.9f;
        const float tapAlpha = alpha * 0.22f;
        const float offsets[4][2] = {
            {-spread, 0.f}, {spread, 0.f}, {0.f, -spread}, {0.f, spread}
        };
        for (const auto& offset : offsets) {
            ren.drawTexture(texture,
                {dest.x + offset[0], dest.y + offset[1],
                 dest.width, dest.height},
                nxui::Color::white().withAlpha(tapAlpha));
        }
    }

    const int saturation = std::clamp(m_treatmentSaturation, 0, 150);
    if (saturation < 100) {
        const float amount = (100 - saturation) / 100.f * 0.30f * alpha;
        ren.drawRect(dest, nxui::Color(0.52f, 0.54f, 0.58f, amount));
    } else if (saturation > 100) {
        // Above neutral there is no colour matrix in the flat renderer, so a
        // light accent wash provides the visible boost without a new shader.
        const float amount = (saturation - 100) / 50.f * 0.12f * alpha;
        ren.drawRect(dest, nxui::Color(m_treatmentTintColor.r,
                                       m_treatmentTintColor.g,
                                       m_treatmentTintColor.b, amount));
    }

    const int tintPct = std::clamp(m_treatmentTint, 0, 100);
    if (tintPct > 0) {
        ren.drawRect(dest, m_treatmentTintColor.withAlpha(
            m_treatmentTintColor.a * tintPct / 100.f * 0.35f * alpha));
    }
}

void WaraWaraBackground::setTreatment(int blurPx, int saturationPct, int tintPct,
                                      int sharpnessPct, int vibrancePct,
                                      int noisePct, const nxui::Color& tint) {
    m_treatmentBlur = std::clamp(blurPx, 0, 16);
    m_treatmentSaturation = std::clamp(saturationPct, 0, 150);
    m_treatmentTint = std::clamp(tintPct, 0, 100);
    m_treatmentTintColor = tint;

    const int sharpness = std::clamp(sharpnessPct, 0, 100);
    const int vibrance = std::clamp(vibrancePct, 0, 100);
    const int noise = std::clamp(noisePct, 0, 100);
    if (sharpness == m_treatmentSharpness &&
        vibrance == m_treatmentVibrance &&
        noise == m_treatmentNoise)
        return;
    m_treatmentSharpness = sharpness;
    m_treatmentVibrance = vibrance;
    m_treatmentNoise = noise;
    m_treatDirty = true;
    // The first change bakes immediately; later ticks coalesce in onUpdate so
    // dragging a slider cannot re-upload the wallpaper on every frame.
    m_lastTreat = std::chrono::steady_clock::time_point{};
}

WaraWaraBackground::ShapeType WaraWaraBackground::pickShapeType() const {
    switch (m_config.shapeSet) {
        case ShapeSet::Circle:
            return Circle;
        case ShapeSet::Triangle:
            return Triangle;
        case ShapeSet::Square:
            return Square;
        case ShapeSet::Diamond:
            return Diamond;
        case ShapeSet::Hexagon:
            return Hexagon;
        case ShapeSet::Mixed:
        default:
            return static_cast<ShapeType>(std::rand() % ShapeCount);
    }
}

void WaraWaraBackground::regenerate(int count) {
    if (!m_shapesEnabled) {
        m_shapes.clear();
        return;
    }
    float areaX = m_rect.x;
    float areaY = m_rect.y;
    float areaW = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
    float areaH = (m_rect.height > 1.f) ? m_rect.height : 720.f;

    int requestedShapeCount = count > 0 ? count : m_config.shapeCount;
    const int requestedGridCells = std::max(1, m_config.gridColumns * m_config.gridRows);
    if (m_config.layout == Layout::Grid)
        requestedShapeCount = requestedGridCells;

    const int safeShapeCount = maxSafeBaseShapeCount(m_config);
    const int visualShapeCount = maxVisualBaseShapeCount(m_config);
    const int shapeCount = std::min(requestedShapeCount, std::min(safeShapeCount, visualShapeCount));
    if (shapeCount != requestedShapeCount) {
        DebugLog::log("[background] shape request clamped: layout=%s requested=%d effective=%d symmetry=%d estVerts=%llu safeVerts=%d visualCap=%d renderedCopies=%d",
                      m_config.layout == Layout::Grid ? "grid" : "floating",
                      requestedShapeCount,
                      shapeCount,
                      symmetryMultiplier(m_config.symmetry),
                      (unsigned long long)estimateVertexCount(m_config, requestedShapeCount),
                      nxui::GpuDevice::MAX_VERTICES - kBackgroundRenderReserveVertices,
                      visualShapeCount,
                      renderedShapeCopies(m_config, shapeCount));
    }

    m_shapes.resize(shapeCount);
    for (int index = 0; index < shapeCount; ++index) {
        Shape& s = m_shapes[index];
        s.type = pickShapeType();
        if (m_config.layout == Layout::Grid) {
            const int cellIndex = std::min(requestedGridCells - 1,
                                           (shapeCount >= requestedGridCells)
                                               ? index
                                               : (int)(((long long)index * requestedGridCells) / shapeCount));
            int col = cellIndex % m_config.gridColumns;
            int row = cellIndex / m_config.gridColumns;
            float gridW = (m_config.gridColumns - 1) * m_config.spacingX;
            float gridH = (m_config.gridRows - 1) * m_config.spacingY;
            float startX = areaX + (areaW - gridW) * 0.5f;
            float startY = areaY + (areaH - gridH) * 0.5f;
            s.pos = {startX + col * m_config.spacingX, startY + row * m_config.spacingY};
        } else {
            s.pos = {areaX + random01() * areaW, areaY + random01() * areaH};
        }
        s.size = randomRange(m_config.sizeMin, m_config.sizeMax);
        s.speed = randomRange(m_config.speedMin, m_config.speedMax);
        s.phase = random01() * 6.28f;
        s.wobble = m_config.layout == Layout::Grid ? 0.f : randomRange(m_config.wobble * 0.4f, m_config.wobble);
        if (m_config.fixedOrientation) {
            s.rotation = degreesToRadians(m_config.orientationDegrees);
            s.rotSpeed = m_config.rotationSpeed;
        } else {
            s.rotation = random01() * 6.28f;
            s.rotSpeed = randomRange(m_config.rotationSpeed * 0.35f, m_config.rotationSpeed);
            if (std::rand() % 2) s.rotSpeed = -s.rotSpeed;
        }
        s.glassAlpha = randomRange(kShapeBodyAlphaMin, kShapeBodyAlphaMax) * m_config.opacity;
        s.color = nxui::Color::white().withAlpha(s.glassAlpha);
    }
}

void WaraWaraBackground::setDrift(bool enabled, int speedPercent) {
    const int speed = std::clamp(speedPercent, 1, 100);
    // Restart the pass from centre when the drift is switched on, so the first
    // frame does not jump halfway through a sweep that was never seen.
    if (enabled && !m_driftEnabled)
        m_driftPhase = 0.f;
    const bool changed = m_driftEnabled != enabled || m_driftSpeed != speed;
    m_driftEnabled = enabled;
    m_driftSpeed = speed;
    if (changed)
        DebugLog::log("[background] drift enabled=%d speed=%d", enabled ? 1 : 0, speed);
}

void WaraWaraBackground::onUpdate(float dt) {
    if (m_treatDirty && m_gpu && m_ren) {
        const auto now = std::chrono::steady_clock::now();
        if (m_lastTreat.time_since_epoch().count() == 0 ||
            now - m_lastTreat >= std::chrono::milliseconds(200)) {
            m_lastTreat = now;
            m_treatDirty = false;
            if (m_sourceRgba.empty() && !m_imagePath.empty() &&
                treatmentActive())
                loadSourcePixels(m_imagePath);
            bakeTreatment();
        }
    }
    // A covering hero masks these layers, so their animation (and an animated
    // wallpaper's frame decode/upload) only burns frame budget behind it.
    if (!m_heroCover && m_animWallpaper && !m_motionPaused)
        m_animWallpaper->update(dt);
    if (!m_heroCover && m_driftEnabled && !m_motionPaused) {
        // Percent to seconds: 1% crawls a pass in 75 s, 100% in about 12 s.
        const float period = 75.f - 0.63f * static_cast<float>(m_driftSpeed);
        m_driftPhase += dt * (6.2831853f / std::max(6.f, period));
        if (m_driftPhase > 6.2831853f)
            m_driftPhase -= 6.2831853f;
    }
    if (m_heroCover || m_motionPaused || !m_shapesEnabled)
        return;
    m_time += dt;
    for (auto& s : m_shapes) {
        if (m_config.layout == Layout::Floating) {
            float top = m_rect.y - s.size - 20.f;
            float bottom = m_rect.y + ((m_rect.height > 1.f) ? m_rect.height : 720.f) + s.size + 20.f;
            float left = m_rect.x;
            float width = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
            s.pos.y -= s.speed * dt;
            s.pos.x += std::sin(m_time * 0.7f + s.phase) * s.wobble * dt;
            s.pos.x = wrapValue(s.pos.x, left - s.size, left + width + s.size);
            if (s.pos.y + s.size < top) {
                s.pos.y = bottom;
                s.pos.x = left + random01() * width;
            }
        }
        s.rotation += s.rotSpeed * dt;
    }
}

nxui::Rect WaraWaraBackground::backgroundImageRect() const {
    const nxui::Texture* wallpaper = activeWallpaperTexture();
    if (!wallpaper || wallpaper->width() <= 0 || wallpaper->height() <= 0)
        return m_rect;

    float areaX = m_rect.x;
    float areaY = m_rect.y;
    float areaW = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
    float areaH = (m_rect.height > 1.f) ? m_rect.height : 720.f;
    float texW = static_cast<float>(wallpaper->width());
    float texH = static_cast<float>(wallpaper->height());
    float sx = areaW / texW;
    float sy = areaH / texH;
    // Wallpapers always fill the screen; only theme still images may letterbox.
    // Drift needs slack, so a letterboxed still is filled while it is on —
    // sliding a contained image would just rub it against the black bars.
    const bool cover = m_config.imageCover || hasAnimatedWallpaper() || m_driftEnabled;
    float scale = cover ? std::max(sx, sy) : std::min(sx, sy);
    float drawW = texW * scale;
    float drawH = texH * scale;
    // Drift needs slack on both sides or the pan would expose the clear colour
    // at an edge. Overscan the cover art a little and slide it inside that
    // margin; letterboxed still images (cover == false) stay centred.
    if (m_driftEnabled && cover) {
        constexpr float kDriftOverscan = 0.08f;
        drawW *= 1.f + kDriftOverscan;
        drawH *= 1.f + kDriftOverscan;
        const float slack = (drawW - areaW) * 0.5f;
        const float x = areaX + (areaW - drawW) * 0.5f
                      + std::sin(m_driftPhase) * slack;
        return {x, areaY + (areaH - drawH) * 0.5f, drawW, drawH};
    }
    return {areaX + (areaW - drawW) * 0.5f, areaY + (areaH - drawH) * 0.5f, drawW, drawH};
}

bool WaraWaraBackground::backgroundCoversGradient() const {
    // Only a fully opaque, full-bleed animated wallpaper hides the gradient.
    // Theme still images are not included: a PNG may carry transparency and
    // the load path does not report it, so their gradient always stays.
    return hasAnimatedWallpaper()
        && m_opacity >= 0.999f
        && m_animWallpaper->opaque();
}

void WaraWaraBackground::onRender(nxui::Renderer& ren) {
    // Keep the first fullscreen draw on the basic renderer path. The animated
    // gradient shader was attractive but fragile as the very first draw after a
    // menu applet launch: if shader constants or descriptor state are rejected
    // on-device, the whole tree can fail before the activity debug pass runs.
    // A batched gradient is cheaper, deterministic and visually close enough.
    // Skip it entirely when the layer above it covers the whole screen: that is
    // a full framebuffer fill saved every frame a wallpaper or opaque image is
    // playing.
    if (!backgroundCoversGradient())
        ren.drawGradientRect(m_rect, m_secondary, m_accent);

    drawWallpaper(ren, 1.f);

    // A full-screen hero covers the shapes entirely, so skip their draw too.
    if (!m_heroCover && m_shapesEnabled) {
        for (const auto& s : m_shapes)
            drawShapeWithSymmetry(ren, s);
    }

    ren.flush();
}

void WaraWaraBackground::renderLetterboxOverlay(nxui::Renderer& ren, float opacityMul) {
    const float prev = m_opacity;
    m_opacity = prev * std::clamp(opacityMul, 0.f, 1.f);
    // Skip the opaque gradient so the blurred cover stays visible in the bars.
    drawWallpaper(ren, 0.55f);
    if (m_shapesEnabled) {
        for (const auto& s : m_shapes)
            drawShapeWithSymmetry(ren, s);
    }
    ren.flush();
    m_opacity = prev;
}

void WaraWaraBackground::renderShifted(nxui::Renderer& ren, float dy, float opacityMul,
                                       float scale) {
    const nxui::Rect savedRect = m_rect;
    const float prev = m_opacity;
    const float s = std::max(0.01f, scale);
    const float cx = savedRect.x + savedRect.width * 0.5f;
    const float cy = savedRect.y + savedRect.height * 0.5f;

    m_opacity = prev * std::clamp(opacityMul, 0.f, 1.f);
    // Scale around screen center, then apply curtain dy.
    m_rect.x = cx + (savedRect.x - cx) * s;
    m_rect.y = cy + (savedRect.y - cy) * s + dy;
    m_rect.width = savedRect.width * s;
    m_rect.height = savedRect.height * s;

    m_savedShapePos.clear();
    m_savedShapeSize.clear();
    m_savedShapePos.reserve(m_shapes.size());
    m_savedShapeSize.reserve(m_shapes.size());
    for (auto& sh : m_shapes) {
        m_savedShapePos.push_back(sh.pos);
        m_savedShapeSize.push_back(sh.size);
        sh.pos.x = cx + (sh.pos.x - cx) * s;
        sh.pos.y = cy + (sh.pos.y - cy) * s + dy;
        sh.size *= s;
    }

    // Gradient must honor opacity — an opaque wash over HOME looked muddy green-grey.
    // Same skip as onRender: a covering wallpaper makes the fill invisible.
    if (!backgroundCoversGradient()) {
        ren.drawGradientRect(m_rect,
                             m_secondary.withAlpha(m_secondary.a * m_opacity),
                             m_accent.withAlpha(m_accent.a * m_opacity));
    }
    drawWallpaper(ren, 1.f);
    if (m_shapesEnabled) {
        for (const auto& sh : m_shapes)
            drawShapeWithSymmetry(ren, sh);
    }
    ren.flush();

    for (size_t i = 0; i < m_shapes.size(); ++i) {
        m_shapes[i].pos = m_savedShapePos[i];
        m_shapes[i].size = m_savedShapeSize[i];
    }
    m_opacity = prev;
    m_rect = savedRect;
}

void WaraWaraBackground::drawShapeWithSymmetry(nxui::Renderer& ren, const Shape& s) const {
    drawGlassShape(ren, s);

    float left = m_rect.x;
    float top = m_rect.y;
    float width = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
    float height = (m_rect.height > 1.f) ? m_rect.height : 720.f;

    auto mirrorHorizontal = [&](const Shape& source) {
        Shape mirrored = source;
        mirrored.pos.x = left + width - (source.pos.x - left);
        return mirrored;
    };

    auto mirrorVertical = [&](const Shape& source) {
        Shape mirrored = source;
        mirrored.pos.y = top + height - (source.pos.y - top);
        return mirrored;
    };

    if (m_config.symmetry == Symmetry::MirrorHorizontal || m_config.symmetry == Symmetry::Quad)
        drawGlassShape(ren, mirrorHorizontal(s));

    if (m_config.symmetry == Symmetry::MirrorVertical || m_config.symmetry == Symmetry::Quad)
        drawGlassShape(ren, mirrorVertical(s));

    if (m_config.symmetry == Symmetry::Quad)
        drawGlassShape(ren, mirrorVertical(mirrorHorizontal(s)));
}

void WaraWaraBackground::drawGlassShape(nxui::Renderer& ren, const Shape& s) const {
    float a = s.color.a * m_opacity;
    if (a < 0.003f) return;

    // Floating shapes wrap outside the screen and mirrored copies are drawn
    // independently, so cull each copy by its own bounds. The reach covers the
    // rotated silhouette (sqrt(2) * size) with a little slack.
    const float reach = s.size * 1.6f;
    if (s.pos.x + reach < m_rect.x || s.pos.x - reach > m_rect.right()
        || s.pos.y + reach < m_rect.y || s.pos.y - reach > m_rect.bottom())
        return;

    // Every glass layer shares the shape rotation. Compute it once here rather
    // than once per layer inside drawRoundedShape.
    float cs = 1.f, sn = 0.f;
    if (s.type != Circle) {
        cs = std::cos(s.rotation);
        sn = std::sin(s.rotation);
    }

    nxui::Color body = m_shapeColor.withAlpha(a);
    drawRoundedShape(ren, s, body, cs, sn);

    if (m_scrollActive || useDenseRenderMode(m_config, (int)m_shapes.size()))
        return;

    Shape highlight = s;
    highlight.pos.y -= s.size * 0.08f;
    highlight.size   = s.size * 0.85f;
    const float effectAlpha = m_config.opacity * m_opacity;
    nxui::Color hi = nxui::Color(1.f, 1.f, 1.f, kShapeHighlightAlpha * effectAlpha);
    drawRoundedShape(ren, highlight, hi, cs, sn);

    Shape edge = s;
    edge.size = s.size * 1.06f;
    const float edgeAlpha = (m_config.layout == Layout::Grid) ? kGridShapeEdgeAlpha : kFloatingShapeEdgeAlpha;
    nxui::Color edgeC = nxui::Color(1.f, 1.f, 1.f, edgeAlpha * effectAlpha);
    drawRoundedShape(ren, edge, edgeC, cs, sn);
}

void WaraWaraBackground::drawRoundedShape(nxui::Renderer& ren, const Shape& s,
                                          const nxui::Color& c,
                                          float cs, float sn) const {
    const float sz = s.size;

    auto rot = [&](float lx, float ly) -> nxui::Vec2 {
        return {s.pos.x + lx * cs - ly * sn,
                s.pos.y + lx * sn + ly * cs};
    };

    switch (s.type) {
    case Circle:
        ren.drawCircle(s.pos, sz, c, 12);
        break;
    case Triangle: {
        nxui::Vec2 p0 = rot(0,             -sz);
        nxui::Vec2 p1 = rot(-sz * 0.866f,   sz * 0.5f);
        nxui::Vec2 p2 = rot( sz * 0.866f,   sz * 0.5f);
        ren.drawTriangle(p0, p1, p2, c);
        break;
    }
    case Square: {
        float h = sz * 0.707f;
        float roundness = std::clamp(m_config.cornerRoundness, 0.f, 1.f);
        if (roundness > 0.001f) {
            if (roundness != m_roundedUnitRoundness) {
                std::array<nxui::Vec2, kRoundedUnitCapacity> unit{};
                int unitCount = 0;
                const float radius = roundness;
                appendArcPoints(unit, unitCount,  1.f - radius, -1.f + radius, radius, -kHalfPi, 0.f,      3, true);
                appendArcPoints(unit, unitCount,  1.f - radius,  1.f - radius, radius,  0.f,      kHalfPi,  3, false);
                appendArcPoints(unit, unitCount, -1.f + radius,  1.f - radius, radius,  kHalfPi,  kPi,      3, false);
                appendArcPoints(unit, unitCount, -1.f + radius, -1.f + radius, radius,  kPi,      kPi * 1.5f, 3, false);
                m_roundedUnitPoints = unit;
                m_roundedUnitCount = unitCount;
                m_roundedUnitRoundness = roundness;
            }

            std::array<nxui::Vec2, kRoundedPointCapacity> points{};
            const int pointCount = m_roundedUnitCount;
            for (int i = 0; i < pointCount; ++i)
                points[i] = rot(m_roundedUnitPoints[(std::size_t)i].x * h,
                                m_roundedUnitPoints[(std::size_t)i].y * h);

            for (int i = 0; i < pointCount; ++i)
                ren.drawTriangle(s.pos, points[i], points[(i + 1) % pointCount], c);
            break;
        }

        nxui::Vec2 p0 = rot(-h, -h);
        nxui::Vec2 p1 = rot( h, -h);
        nxui::Vec2 p2 = rot( h,  h);
        nxui::Vec2 p3 = rot(-h,  h);
        ren.drawTriangle(p0, p1, p2, c);
        ren.drawTriangle(p0, p2, p3, c);
        break;
    }
    case Diamond: {
        nxui::Vec2 p0 = rot(0,            -sz);
        nxui::Vec2 p1 = rot( sz * 0.6f,    0);
        nxui::Vec2 p2 = rot(0,             sz);
        nxui::Vec2 p3 = rot(-sz * 0.6f,    0);
        ren.drawTriangle(p0, p1, p2, c);
        ren.drawTriangle(p0, p2, p3, c);
        break;
    }
    case Hexagon: {
        constexpr int N = 6;
        // Unit hexagon vertices never change; only rotation and size do.
        static const std::array<nxui::Vec2, N> kUnit = [] {
            std::array<nxui::Vec2, N> out{};
            constexpr float step = 6.2831853f / N;
            for (int i = 0; i < N; ++i)
                out[(std::size_t)i] = {std::cos(step * i), std::sin(step * i)};
            return out;
        }();
        nxui::Vec2 pts[N];
        for (int i = 0; i < N; ++i)
            pts[i] = rot(kUnit[(std::size_t)i].x * sz, kUnit[(std::size_t)i].y * sz);
        for (int i = 1; i < N - 1; ++i)
            ren.drawTriangle(pts[0], pts[i], pts[i + 1], c);
        break;
    }
    default: break;
    }
}
