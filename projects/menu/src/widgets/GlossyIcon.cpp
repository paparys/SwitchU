#include "GlossyIcon.hpp"
#include "core/PlaytimeStore.hpp"
#include "FolderPalette.hpp"
#include "FolderStyleDraw.hpp"
#include "core/FolderStore.hpp"
#include "core/WidgetStore.hpp"
#include "BatteryDrawing.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <nxui/third_party/stb/stb_image.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <fstream>
struct WidgetGifDecodeState {
    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<int> durationsMs;
    int width = 0;
    int height = 0;
    long decodeMilliseconds = 0;
    std::atomic<bool> cancelled{false};
};

namespace {

constexpr int kWidgetAnimationMaximumSide = 192;
constexpr std::size_t kWidgetAnimationGpuBytes = 1u * 1024u * 1024u;

bool hasGifExtension(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return path.ends_with(".gif");
}

nxui::Rect centeredCoverSource(const nxui::Texture& texture,
                               const nxui::Rect& destination) {
    const float sourceWidth = static_cast<float>(texture.width());
    const float sourceHeight = static_cast<float>(texture.height());
    if (sourceWidth <= 0.f || sourceHeight <= 0.f ||
        destination.width <= 0.f || destination.height <= 0.f)
        return {0.f, 0.f, sourceWidth, sourceHeight};

    const float sourceAspect = sourceWidth / sourceHeight;
    const float targetAspect = destination.width / destination.height;
    if (sourceAspect > targetAspect) {
        const float croppedWidth = sourceHeight * targetAspect;
        return {(sourceWidth - croppedWidth) * 0.5f, 0.f,
                croppedWidth, sourceHeight};
    }
    const float croppedHeight = sourceWidth / targetAspect;
    return {0.f, (sourceHeight - croppedHeight) * 0.5f,
            sourceWidth, croppedHeight};
}

void decodeWidgetGif(const std::string& path,
                     const std::shared_ptr<WidgetGifDecodeState>& state) {
    if (!state || state->cancelled.load()) return;
    const auto started = std::chrono::steady_clock::now();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) return;
    const std::streamoff size = input.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(64u * 1024u * 1024u)) return;
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())) ||
        state->cancelled.load())
        return;

    int* delays = nullptr;
    int width = 0;
    int height = 0;
    int frameCount = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_gif_from_memory(
        bytes.data(), static_cast<int>(bytes.size()), &delays,
        &width, &height, &frameCount, &channels, 4);
    if (!decoded || width <= 0 || height <= 0 || frameCount <= 0) {
        if (decoded) stbi_image_free(decoded);
        if (delays) stbi_image_free(delays);
        return;
    }

    int outputWidth = width;
    int outputHeight = height;
    if (outputWidth > kWidgetAnimationMaximumSide ||
        outputHeight > kWidgetAnimationMaximumSide) {
        const float scale = std::min(
            static_cast<float>(kWidgetAnimationMaximumSide) / outputWidth,
            static_cast<float>(kWidgetAnimationMaximumSide) / outputHeight);
        outputWidth = std::max(1, static_cast<int>(std::round(outputWidth * scale)));
        outputHeight = std::max(1, static_cast<int>(std::round(outputHeight * scale)));
    }
    const std::size_t bytesPerFrame = static_cast<std::size_t>(outputWidth) *
        outputHeight * 4u;
    const int selectedFrameCount = std::min(frameCount, static_cast<int>(
        std::max<std::size_t>(1u, kWidgetAnimationGpuBytes /
            std::max<std::size_t>(1u, bytesPerFrame))));

    state->width = outputWidth;
    state->height = outputHeight;
    state->frames.reserve(static_cast<std::size_t>(selectedFrameCount));
    state->durationsMs.reserve(static_cast<std::size_t>(selectedFrameCount));
    for (int frame = 0; frame < selectedFrameCount && !state->cancelled.load(); ++frame) {
        const int sourceFrame = frame * frameCount / selectedFrameCount;
        const int nextSourceFrame = (frame + 1) * frameCount / selectedFrameCount;
        const auto* source = decoded +
            static_cast<std::size_t>(sourceFrame) * width * height * 4u;
        std::vector<std::uint8_t> pixels(bytesPerFrame);
        if (outputWidth == width && outputHeight == height) {
            std::copy_n(source, bytesPerFrame, pixels.data());
        } else {
            for (int y = 0; y < outputHeight; ++y) {
                const int sourceY = y * height / outputHeight;
                for (int x = 0; x < outputWidth; ++x) {
                    const int sourceX = x * width / outputWidth;
                    const auto* pixel = source +
                        (static_cast<std::size_t>(sourceY) * width + sourceX) * 4u;
                    auto* target = pixels.data() +
                        (static_cast<std::size_t>(y) * outputWidth + x) * 4u;
                    std::copy_n(pixel, 4, target);
                }
            }
        }
        int duration = 0;
        for (int sourceIndex = sourceFrame;
             sourceIndex < nextSourceFrame; ++sourceIndex)
            duration += delays ? delays[sourceIndex] : 100;
        state->frames.push_back(std::move(pixels));
        state->durationsMs.push_back(std::max(20, duration));
    }
    stbi_image_free(decoded);
    if (delays) stbi_image_free(delays);
    if (state->cancelled.load()) {
        state->frames.clear();
        state->durationsMs.clear();
    }
    state->decodeMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
}

void drawBatteryRing(nxui::Renderer& ren, const nxui::Vec2& center,
                     float radius, int percent, bool charging,
                     bool console, nxui::Texture* deviceIcon,
                     nxui::Font* font, float opacity) {
    constexpr int segments = 96;
    constexpr float tau = 6.28318530718f;
    constexpr float startAngle = -1.57079632679f;
    const float thickness = std::max(4.f, radius * 0.13f);
    const float progress = std::clamp(percent / 100.f, 0.f, 1.f);
    const nxui::Color activeColor = percent <= 20
        ? nxui::Color(1.f, 0.25f, 0.22f, opacity)
        : nxui::Color(0.25f, 0.92f, 0.48f, opacity);
    for (int i = 0; i < segments; ++i) {
        const float a0 = startAngle + tau * static_cast<float>(i) / segments;
        const float a1 = startAngle + tau * static_cast<float>(i + 1) / segments;
        ren.drawLine({center.x + std::cos(a0) * radius,
                      center.y + std::sin(a0) * radius},
                     {center.x + std::cos(a1) * radius,
                      center.y + std::sin(a1) * radius},
                     nxui::Color::white().withAlpha(0.13f * opacity), thickness);
    }
    if (progress > 0.f) {
        const int activeSegments = std::max(1,
            static_cast<int>(std::ceil(progress * segments)));
        for (int i = 0; i < activeSegments; ++i) {
            const float t0 = std::min(progress, static_cast<float>(i) / segments);
            const float t1 = std::min(progress, static_cast<float>(i + 1) / segments);
            const float a0 = startAngle + tau * t0;
            const float a1 = startAngle + tau * t1;
            ren.drawLine({center.x + std::cos(a0) * radius,
                          center.y + std::sin(a0) * radius},
                         {center.x + std::cos(a1) * radius,
                          center.y + std::sin(a1) * radius},
                         activeColor, thickness);
        }
        const float endAngle = startAngle + tau * progress;
        ren.drawCircle({center.x, center.y - radius}, thickness * 0.5f,
                       activeColor, 20);
        ren.drawCircle({center.x + std::cos(endAngle) * radius,
                        center.y + std::sin(endAngle) * radius},
                       thickness * 0.5f, activeColor, 20);
    }

    const nxui::Color glyph = nxui::Color::white().withAlpha(0.88f * opacity);
    if (deviceIcon && deviceIcon->valid()) {
        const float maxWidth = radius * (console ? 0.86f : 0.48f);
        const float maxHeight = radius * (console ? 0.55f : 0.78f);
        const float aspect = static_cast<float>(deviceIcon->width()) /
            std::max(1.f, static_cast<float>(deviceIcon->height()));
        nxui::Rect iconRect{center.x - maxWidth * 0.5f,
                            center.y - maxHeight * 0.58f,
                            maxWidth, maxHeight};
        if (aspect > maxWidth / maxHeight) {
            iconRect.height = maxWidth / aspect;
            iconRect.y += (maxHeight - iconRect.height) * 0.5f;
        } else {
            iconRect.width = maxHeight * aspect;
            iconRect.x += (maxWidth - iconRect.width) * 0.5f;
        }
        ren.drawTexture(deviceIcon, iconRect,
                        nxui::Color::white().withAlpha(0.95f * opacity));
    } else if (console) {
        const nxui::Rect body{center.x - radius * 0.22f,
                              center.y - radius * 0.30f,
                              radius * 0.44f, radius * 0.56f};
        ren.drawRoundedRectOutline(body, glyph, radius * 0.07f,
                                   std::max(1.4f, radius * 0.045f));
        ren.drawLine({body.x + body.width * 0.35f, body.bottom() - radius * 0.07f},
                     {body.x + body.width * 0.65f, body.bottom() - radius * 0.07f},
                     glyph, std::max(1.f, radius * 0.025f));
    } else {
        ren.drawCircle({center.x - radius * 0.16f, center.y - radius * 0.05f},
                       radius * 0.13f, glyph, 18);
        ren.drawCircle({center.x + radius * 0.16f, center.y - radius * 0.05f},
                       radius * 0.13f, glyph, 18);
        ren.drawRoundedRect({center.x - radius * 0.22f,
                             center.y - radius * 0.09f,
                             radius * 0.44f, radius * 0.18f}, glyph,
                            radius * 0.08f);
    }
    if (charging) {
        const nxui::Rect bolt{center.x + radius * 0.18f,
                              center.y - radius * 0.52f,
                              radius * 0.25f, radius * 0.34f};
        switchu::battery_drawing::drawLightningBolt(
            ren, bolt, nxui::Color(1.f, 0.86f, 0.18f, opacity),
            nxui::Color(1.f, 0.64f, 0.08f, opacity * 0.72f),
            std::max(0.8f, radius * 0.018f));
    }
    if (font) {
        const std::string text = std::to_string(percent) + "%";
        const float scale = std::max(0.34f, radius / 72.f * 0.50f);
        const auto measured = font->measure(text);
        ren.drawText(text,
                     {center.x - measured.x * scale * 0.5f,
                      center.y + radius * 0.28f}, font,
                     nxui::Color::white().withAlpha(0.92f * opacity), scale);
    }
}

} // namespace


GlossyIcon::GlossyIcon() {
    m_animScale.setImmediate(0.f);
    m_appearOpacity.setImmediate(0.f);
    m_focusScale.setImmediate(1.f);
    m_focusGlow.setImmediate(0.f);
    m_jiggleAmount.setImmediate(0.f);
    setCornerRadius(16.f);
    setPadding(8.f);
    setLiquidGlassEnabled(true);
    setBlurEnabled(false);
}

GlossyIcon::~GlossyIcon() {
    if (m_widgetGifDecode)
        m_widgetGifDecode->cancelled.store(true);
}

void GlossyIcon::setWidgetData(switchu::widgets::WidgetType type,
                               int columns, int rows,
                               std::string primary, std::string secondary,
                               const std::string& assetPath,
                               nxui::GpuDevice* gpu, nxui::Renderer* renderer,
                               bool deferAssetLoad) {
    m_widgetType = type;
    m_widgetColumns = std::max(1, columns);
    m_widgetRows = std::max(1, rows);
    m_widgetPrimary = std::move(primary);
    m_widgetSecondary = std::move(secondary);
    if (m_widgetGifDecode)
        m_widgetGifDecode->cancelled.store(true);
    m_widgetGifDecode.reset();
    m_widgetGifDecodeFuture = {};
    m_widgetGifUploadIndex = 0;
    m_widgetAssetPath = assetPath;
    m_widgetAssetLoadAttempted = false;
    m_widgetTexture.reset();
    m_widgetAnimation.clear();
    m_widgetExternalTexture = nullptr;
    if (assetPath.empty() || !gpu || !renderer || deferAssetLoad) return;

    loadWidgetImageAsset(*gpu, *renderer);
}

bool GlossyIcon::isWidgetImageAssetLoaded() const {
    return m_widgetAnimation.hasFrames() ||
           (m_widgetTexture && m_widgetTexture->valid());
}

bool GlossyIcon::loadWidgetImageAsset(nxui::GpuDevice& gpu,
                                      nxui::Renderer& renderer) {
    if (m_widgetAssetPath.empty()) return false;
    if (isWidgetImageAssetLoaded()) return true;

    m_widgetAssetLoadAttempted = true;

    std::string extension = m_widgetAssetPath;
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if ((extension.ends_with(".webp") || extension.ends_with(".gif")) &&
        m_widgetAnimation.load(gpu, renderer, m_widgetAssetPath,
                               kWidgetAnimationMaximumSide,
                               kWidgetAnimationGpuBytes)) {
        if (m_widgetAnimation.frameCount() > 0) return true;
    }

    auto texture = std::make_unique<nxui::Texture>();
    if (texture->loadFromFile(gpu, renderer, m_widgetAssetPath, 512)) {
        m_widgetTexture = std::move(texture);
        return true;
    }
    return false;
}

bool GlossyIcon::startWidgetImageAssetLoad(nxui::ThreadPool& threadPool,
                                           nxui::GpuDevice& gpu,
                                           nxui::Renderer& renderer) {
    if (m_widgetAssetPath.empty() || isWidgetImageAssetLoaded() ||
        isWidgetImageAssetLoading())
        return false;
    if (!hasGifExtension(m_widgetAssetPath))
        return loadWidgetImageAsset(gpu, renderer);

    m_widgetAssetLoadAttempted = true;
    m_widgetGifUploadIndex = 0;
    auto state = std::make_shared<WidgetGifDecodeState>();
    m_widgetGifDecode = state;
    const std::string path = m_widgetAssetPath;
    m_widgetGifDecodeFuture = threadPool.submit(
        [state, path]() { decodeWidgetGif(path, state); });
    return true;
}

bool GlossyIcon::pollWidgetImageAssetLoad(nxui::GpuDevice& gpu,
                                          nxui::Renderer& renderer) {
    if (!m_widgetGifDecode) return false;
    if (m_widgetGifDecodeFuture.valid()) {
        if (m_widgetGifDecodeFuture.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready)
            return false;
        try {
            m_widgetGifDecodeFuture.get();
            DebugLog::log("[widget-gif] decoded frames=%d size=%dx%d in %ldms path=%s",
                          static_cast<int>(m_widgetGifDecode->frames.size()),
                          m_widgetGifDecode->width, m_widgetGifDecode->height,
                          m_widgetGifDecode->decodeMilliseconds,
                          m_widgetAssetPath.c_str());
        } catch (...) {
            m_widgetGifDecode->cancelled.store(true);
        }
    }

    auto state = m_widgetGifDecode;
    if (state->cancelled.load() || state->frames.empty() ||
        state->width <= 0 || state->height <= 0) {
        m_widgetGifDecode.reset();
        m_widgetGifUploadIndex = 0;
        return false;
    }
    if (m_widgetGifUploadIndex >= state->frames.size()) {
        m_widgetGifDecode.reset();
        m_widgetGifUploadIndex = 0;
        return false;
    }

    const std::size_t index = m_widgetGifUploadIndex;
    const bool uploaded = m_widgetAnimation.appendFrame(
        gpu, renderer, state->frames[index].data(),
        state->width, state->height, state->durationsMs[index]);
    if (!uploaded) {
        state->cancelled.store(true);
        m_widgetGifDecode.reset();
        m_widgetGifUploadIndex = 0;
        return false;
    }
    ++m_widgetGifUploadIndex;
    if (m_widgetGifUploadIndex >= state->frames.size()) {
        DebugLog::log("[widget-gif] upload complete frames=%d path=%s",
                      static_cast<int>(state->frames.size()),
                      m_widgetAssetPath.c_str());
        m_widgetGifDecode.reset();
        m_widgetGifUploadIndex = 0;
    }
    return true;
}

bool GlossyIcon::isWidgetImageAssetLoading() const {
    return static_cast<bool>(m_widgetGifDecode);
}

void GlossyIcon::allowWidgetImageAssetRetry() {
    if (!isWidgetImageAssetLoading())
        m_widgetAssetLoadAttempted = false;
}

bool GlossyIcon::unloadWidgetImageAsset() {
    const bool hadGpuAsset = isWidgetImageAssetLoaded();
    if (m_widgetGifDecode)
        m_widgetGifDecode->cancelled.store(true);
    m_widgetGifDecode.reset();
    m_widgetGifDecodeFuture = {};
    m_widgetGifUploadIndex = 0;
    m_widgetAnimation.clear();
    m_widgetTexture.reset();
    m_widgetExternalTexture = nullptr;
    m_widgetAssetLoadAttempted = false;
    return hadGpuAsset;
}

void GlossyIcon::setWidgetGameAssets(std::uint64_t titleId,
                                     const std::string& heroPath,
                                     const std::string& logoPath,
                                     const std::vector<std::uint8_t>& iconData,
                                     nxui::GpuDevice* gpu,
                                     nxui::Renderer* renderer) {
    m_widgetGameTitleId = titleId;
    m_widgetHeroTexture.reset();
    m_widgetLogoTexture.reset();
    m_widgetGameIconTexture.reset();
    m_widgetHero = nullptr;
    m_widgetLogo = nullptr;
    m_widgetGameIcon = nullptr;
    if (titleId == 0 || !gpu || !renderer) return;

    std::error_code error;
    if (!heroPath.empty() && std::filesystem::is_regular_file(heroPath, error)) {
        auto hero = std::make_unique<nxui::Texture>();
        if (hero->loadFromFile(*gpu, *renderer, heroPath, 640)) {
            m_widgetHeroTexture = std::move(hero);
            m_widgetHero = m_widgetHeroTexture.get();
        }
    }
    error.clear();
    if (!logoPath.empty() && std::filesystem::is_regular_file(logoPath, error)) {
        auto logo = std::make_unique<nxui::Texture>();
        if (logo->loadFromFile(*gpu, *renderer, logoPath, 384)) {
            m_widgetLogoTexture = std::move(logo);
            m_widgetLogo = m_widgetLogoTexture.get();
        }
    }
    if (!iconData.empty()) {
        auto gameIcon = std::make_unique<nxui::Texture>();
        if (gameIcon->loadFromMemory(*gpu, *renderer,
                                     iconData.data(), iconData.size(), 192)) {
            m_widgetGameIconTexture = std::move(gameIcon);
            m_widgetGameIcon = m_widgetGameIconTexture.get();
            m_tex = m_widgetGameIcon;
        }
    }
}

void GlossyIcon::setWidgetGameTextures(std::uint64_t titleId,
                                       nxui::Texture* hero,
                                       nxui::Texture* logo,
                                       nxui::Texture* gameIcon) {
    m_widgetGameTitleId = titleId;
    m_widgetHero = hero;
    m_widgetLogo = logo;
    m_widgetGameIcon = gameIcon;
    if (gameIcon) m_tex = gameIcon;
}

void GlossyIcon::copyWidgetPresentationFrom(GlossyIcon& source) {
    m_entryKind = source.m_entryKind;
    m_folderPreviewCount = source.m_folderPreviewCount;
    m_folderVisualSeed = source.m_folderVisualSeed;
    m_folderColorIndex = source.m_folderColorIndex;
    m_folderStyleIndex = source.m_folderStyleIndex;
    m_folderShowCover = source.m_folderShowCover;
    m_folderCover = source.m_folderCover;
    m_folderCoverIsPoster = source.m_folderCoverIsPoster;
    m_folderPeekComposite = source.m_folderPeekComposite;
    m_folderPeeks = source.m_folderPeeks;
    m_folderPeekCount = source.m_folderPeekCount;
    m_folderCoverTitleId = source.m_folderCoverTitleId;
    m_themeMode = source.m_themeMode;
    m_widgetType = source.m_widgetType;
    m_widgetColumns = source.m_widgetColumns;
    m_widgetRows = source.m_widgetRows;
    m_widgetPrimary = source.m_widgetPrimary;
    m_widgetSecondary = source.m_widgetSecondary;
    m_widgetHeader = source.m_widgetHeader;
    m_consoleBatteryPercent = source.m_consoleBatteryPercent;
    m_consoleBatteryCharging = source.m_consoleBatteryCharging;
    m_batteryConsoleIcon = source.m_batteryConsoleIcon;
    m_widgetGameTitleId = source.m_widgetGameTitleId;
    m_widgetHero = source.m_widgetHero;
    m_widgetLogo = source.m_widgetLogo;
    m_widgetGameIcon = source.m_widgetGameIcon;
    m_wideGameHero = source.m_wideGameHero;
    m_wideGameLogo = source.m_wideGameLogo;
    m_widgetExternalTexture = source.m_widgetAnimation.hasFrames()
        ? source.m_widgetAnimation.currentFrame()
        : source.m_widgetTexture.get();
    m_font = source.m_font;
    m_loadingColor = source.m_loadingColor;
    m_tex = source.m_tex;
    setBaseColor(source.baseColor());
}

void GlossyIcon::onFocusGained() {
    m_focused = true;
    m_focusScale.set(1.075f, 0.18f, nxui::Easing::outBack);
    m_focusGlow.set(1.f, 0.16f, nxui::Easing::outCubic);
    if (!m_motionPaused)
        m_shineT = 0.f;
}

void GlossyIcon::onFocusLost() {
    m_focused = false;
    m_focusScale.set(1.f, 0.20f, nxui::Easing::outCubic);
    m_focusGlow.set(0.f, 0.16f, nxui::Easing::outCubic);
    // Drop an in-flight sweep too: scrolling fast can hand focus to a new tile
    // every few frames, and letting each old tile finish would pile many
    // stepped bands (and their vertices) on screen at once.
    m_shineT = -1.f;
}

void GlossyIcon::startAppear(float delay) {
    m_appearDelay = delay;
    m_appearTimer = 0.f;
    m_appearing   = true;
    m_disappearing = false;
    m_animScale.setImmediate(0.f);
    m_appearOpacity.setImmediate(0.f);
}

void GlossyIcon::startDisappear(const nxui::Rect& target, float delay, float dur) {
    m_appearing = false;
    m_disappearing = true;
    m_appearOrigin = target;
    m_hasAppearOrigin = true;
    m_animScale.set(0.f, dur, nxui::Easing::inCubic, delay);
    m_appearOpacity.set(0.f, dur, nxui::Easing::inCubic, delay);
}

void GlossyIcon::startGlideFrom(const nxui::Rect& from, float dur) {
    // Reuse the appear-origin plumbing: while an origin is armed the tile keeps
    // its full scale and simply lerps from that rect to its slot, so forcing the
    // opacity to 1 makes it a pure translation.
    m_appearing = false;
    m_disappearing = false;
    m_appearDelay = 0.f;
    m_appearTimer = 0.f;
    m_appearOrigin = from;
    m_hasAppearOrigin = true;
    m_appearOpacity.setImmediate(1.f);
    m_animScale.setImmediate(0.f);
    m_animScale.set(1.f, dur, nxui::Easing::outCubic);
}

void GlossyIcon::forceVisible() {
    m_appearing = false;
    m_disappearing = false;
    m_hasAppearOrigin = false;
    m_appearDelay = 0.f;
    m_appearTimer = 0.f;
    m_animScale.setImmediate(1.f);
    m_appearOpacity.setImmediate(1.f);
}

void GlossyIcon::setJiggle(bool on, float phaseSeed) {
    if (on && !m_jiggle)
        m_jigglePhase = 0.f;
    m_jiggle = on;
    m_jiggleSeed = phaseSeed;
    m_jiggleAmount.set(on ? 1.f : 0.f, 0.18f, nxui::Easing::outCubic);
}

void GlossyIcon::onContentUpdate(float dt) {
    if (m_appearing) {
        m_appearTimer += dt;
        if (m_appearTimer >= m_appearDelay) {
            m_appearing = false;
            m_animScale.set(1.f, 0.4f, nxui::Easing::outExpo);
            m_appearOpacity.set(1.f, 0.3f, nxui::Easing::outExpo);
        }
    }
    if (!m_motionPaused)
        m_suspendPulse += dt * 2.8f;
    // Skeletons: while a tile has no artwork, run a brief shimmer so a cold
    // start reads as loading instead of as a row of blank cards. It stops on
    // its own, so a title that genuinely has no icon does not shimmer forever.
    if ((!m_tex || !m_tex->valid()) && m_skeletonT < 1.6f)
        m_skeletonT = std::min(1.6f, m_skeletonT + dt);
    if (!m_motionPaused && m_entryKind == GridEntryKind::Widget && m_widgetAnimation.hasFrames())
        m_widgetAnimation.update(dt, true);
    if (!m_motionPaused && (m_jiggle || m_jiggleAmount.value() > 0.001f))
        m_jigglePhase += dt;
    if (m_shineT >= 0.f) {
        // Short and one-shot: the sweep should read as a reflection catching the
        // tile as the cursor lands, not as a looping marquee.
        constexpr float kShineSeconds = 0.55f;
        m_shineT += dt / kShineSeconds;
        if (m_shineT >= 1.f)
            m_shineT = -1.f;
    }
}

void GlossyIcon::onRender(nxui::Renderer& ren) {
    float externalScale = scale();
    float focusS = m_focusScale.value();
    float appear = m_animScale.value();
    float a = m_appearOpacity.value();

    nxui::Rect savedRect = m_rect;
    nxui::Rect base = savedRect;
    float s;
    if (m_hasAppearOrigin) {
        if (appear >= 0.999f && !m_disappearing)
            m_hasAppearOrigin = false;
        base = nxui::Rect::lerp(m_appearOrigin, savedRect, appear);
        s = externalScale * focusS;
    } else {
        s = appear * externalScale * focusS;
    }
    if (s < 0.01f || a < 0.01f) return;

    nxui::Rect drawRect = base;
    if (std::abs(s - 1.f) > 0.001f) {
        float w = base.width * s;
        float h = base.height * s;
        drawRect.x = base.x + (base.width - w) * 0.5f;
        drawRect.y = base.y + (base.height - h) * 0.5f;
        drawRect.width = w;
        drawRect.height = h;
    }

    const float jig = m_jiggleAmount.value();
    if (jig > 0.001f) {
        const float p = m_jigglePhase;
        const float sq = std::sin(p * 10.2f + m_jiggleSeed * 2.3f);
        const float jw = drawRect.width  * (1.f + 0.012f * jig * sq);
        const float jh = drawRect.height * (1.f - 0.012f * jig * sq);
        drawRect.x += (drawRect.width  - jw) * 0.5f
                    + 2.2f * jig * std::sin(p * 9.0f  + m_jiggleSeed);
        drawRect.y += (drawRect.height - jh) * 0.5f
                    + 1.8f * jig * std::sin(p * 11.3f + m_jiggleSeed * 1.7f);
        drawRect.width  = jw;
        drawRect.height = jh;
    }

    if (drawRect.x != savedRect.x || drawRect.y != savedRect.y ||
        drawRect.width != savedRect.width || drawRect.height != savedRect.height) {
        m_rect = drawRect;
    }
    setScale(1.f);
    float savedShade = liquidGlassShade();
    setLiquidGlassShade(m_notLaunchable ? 0.58f : 0.0f);

    float savedOp = m_opacity;
    m_opacity = a * savedOp;

    // Focus lift: the tile already scales up, so also deepen and spread the
    // shadow a touch. Without it the tile inflates but does not rise.
    const float focusLift = std::clamp((m_focusScale.value() - 1.f) / 0.075f, 0.f, 1.f);
    const float shadowStrength = (m_entryKind == GridEntryKind::Empty ? 0.6f : 1.f)
                               * m_shadowAlpha * m_opacity * (1.f + 0.45f * focusLift);
    if (shadowStrength > 0.001f) {
        // Three concentric layers: the tile covers their cores, so all that
        // shows around it is a soft falloff instead of a hard offset copy.
        constexpr float kSpread[3] = {7.f, 3.5f, 0.f};
        constexpr float kWeight[3] = {0.34f, 0.58f, 1.f};
        const nxui::Color shadowColor(0.02f, 0.04f, 0.06f, 1.f);
        const float offsetY = m_shadowOffsetY + 2.f * focusLift;
        for (int layer = 0; layer < 3; ++layer) {
            const float s = kSpread[layer] * (1.f + 0.22f * focusLift);
            ren.drawRoundedRect({drawRect.x - s * 0.55f,
                                 drawRect.y + offsetY - s * 0.30f,
                                 drawRect.width + s * 1.10f,
                                 drawRect.height + s * 1.30f},
                                shadowColor.withAlpha(shadowStrength * kWeight[layer]),
                                cornerRadius() + 1.f + s);
        }
    }
    nxui::GlassWidget::onRender(ren);

    if (m_entryKind != GridEntryKind::Empty
        && m_entryKind != GridEntryKind::WidgetContinuation)
        drawShine(ren, drawRect, cornerRadius());

    // Time played, bottom-right: "12h 30m" / "45m" / "3d". Hidden below a
    // minute so titles that were never played stay clean.
    if (m_entryKind == GridEntryKind::Application && m_titleId != 0 && m_font) {
        const std::uint64_t seconds = switchu::playtime::secondsFor(m_titleId);
        if (seconds >= 60ULL) {
            std::string label;
            if (seconds >= 86400ULL) {
                label = std::to_string(seconds / 86400ULL) + "d";
            } else if (seconds >= 3600ULL) {
                const std::uint64_t hours = seconds / 3600ULL;
                const std::uint64_t mins = (seconds % 3600ULL) / 60ULL;
                label = std::to_string(hours) + "h";
                if (mins != 0)
                    label += " " + std::to_string(mins) + "m";
            } else {
                label = std::to_string(seconds / 60ULL) + "m";
            }
            constexpr float kTextScale = 1.20f; // 3x the old 0.40 badge
            const nxui::Vec2 measured = m_font->measure(label);
            constexpr float padX = 12.f;
            constexpr float padY = 6.f;
            const float pillW = measured.x * kTextScale + padX * 2.f;
            const float pillH = measured.y * kTextScale + padY * 2.f;
            const float inset = 6.f;
            const nxui::Rect pill{drawRect.x + drawRect.width - pillW - inset,
                                  drawRect.y + drawRect.height - pillH - inset,
                                  pillW, pillH};
            ren.drawRoundedRect(pill, nxui::Color(0.f, 0.f, 0.f, 0.46f * m_opacity),
                                pillH * 0.5f);
            ren.drawText(label, {pill.x + padX, pill.y + padY}, m_font,
                         nxui::Color(1.f, 1.f, 1.f, 0.92f * m_opacity), kTextScale);
        }
    }

    if (m_skeletonT < 1.6f && m_entryKind == GridEntryKind::Application
        && (!m_tex || !m_tex->valid())) {
        // Drawn under the artwork, so a tile that streams in simply covers it.
        const nxui::Rect sk = drawRect;
        const float skRad = cornerRadius();
        ren.drawRoundedRect(sk, nxui::Color(1.f, 1.f, 1.f, 0.055f * m_opacity), skRad);
        const float bandW = sk.width * 0.34f;
        const float sweep = std::clamp(std::fmod(m_skeletonT / 1.1f, 1.f), 0.f, 1.f);
        const float bx = sk.x + (sk.width - bandW) * sweep;
        ren.drawRoundedRect({bx, sk.y, bandW, sk.height},
                            nxui::Color(1.f, 1.f, 1.f, 0.065f * m_opacity), skRad);
    }

    m_opacity = savedOp;
    m_rect = savedRect;
    setLiquidGlassShade(savedShade);
    setScale(externalScale);

    nxui::Rect r = drawRect;
    float rad = cornerRadius();

    float focusGlow = m_focusGlow.value();
    if (focusGlow > 0.01f && s > 0.5f) {
        float breathe = 0.5f + 0.5f * std::sin(m_suspendPulse * 1.8f + 0.4f);
        nxui::Color focusColor = nxui::Color(0.65f, 0.90f, 1.f, (0.08f + 0.04f * breathe) * focusGlow * a);
        ren.drawRoundedRectOutline(r.expanded(5.f * focusGlow), focusColor,
                                   rad + 5.f, 2.f);
    }

    if (m_isGameCard && !m_notLaunchable && s > 0.5f) {
        float badgeW = 66.f * s;
        float badgeH = 48.f * s;
        float badgeX = r.x + 1.f * s;
        float badgeY = r.y + 6.f * s;

        if (m_gameCardTex && m_gameCardTex->valid()) {
            float cardInset = 1.f * s;
            float maxW = badgeW - cardInset * 2;
            float maxH = badgeH - cardInset * 2;
            float aspect = (float)m_gameCardTex->width() / (float)m_gameCardTex->height();
            float texW = maxW;
            float texH = maxH;
            if (aspect > maxW / maxH) {
                texH = maxW / aspect;
            } else {
                texW = maxH * aspect;
            }
            float texX = badgeX + (badgeW - texW) * 0.5f;
            float texY = badgeY + (badgeH - texH) * 0.5f;
            ren.drawTextureRounded(m_gameCardTex, {texX, texY, texW, texH}, 2.f * s,
                                   nxui::Color::white().withAlpha(0.98f * a));
        } else {
            float cardInset = 4.f * s;
            ren.drawRoundedRect({badgeX + cardInset, badgeY + cardInset,
                                 badgeW - cardInset*2, badgeH - cardInset*2},
                                nxui::Color(0.95f, 0.75f, 0.2f, 0.9f * a), 2.f * s);
        }
    }

    if (m_suspended && s > 0.5f) {
        // Soft breathe so an open title reads clearly without looking like focus.
        const float pulse = 0.5f + 0.5f * std::sin(m_suspendPulse);
        const float outerAlpha = (0.28f + 0.42f * pulse) * a;
        const float innerAlpha = (0.55f + 0.35f * pulse) * a;
        const nxui::Color glow(0.20f, 0.92f, 0.50f, 1.f);

        ren.drawRoundedRectOutline(r.expanded(5.f * s),
                                   glow.withAlpha(outerAlpha * 0.55f),
                                   rad + 5.f * s, 4.5f * s);
        ren.drawRoundedRectOutline(r.expanded(2.f * s),
                                   glow.withAlpha(innerAlpha),
                                   rad + 2.f * s, 3.2f * s);

        float badgeSize = 28.f * s;
        float badgeX = r.x + 4.f * s;
        float badgeY = r.y + r.height - badgeSize - 4.f * s;

        nxui::Vec2 badgeCenter = { badgeX + badgeSize * 0.5f, badgeY + badgeSize * 0.5f };
        ren.drawCircle(badgeCenter, badgeSize * 0.58f,
                       glow.withAlpha((0.20f + 0.25f * pulse) * a), 20);
        ren.drawCircle(badgeCenter, badgeSize * 0.5f,
                       nxui::Color(0.06f, 0.10f, 0.08f, 0.90f * a), 16);

        float triH = badgeSize * 0.45f;
        float triW = triH * 0.85f;
        nxui::Vec2 p1 = { badgeCenter.x - triW * 0.35f, badgeCenter.y - triH * 0.5f };
        nxui::Vec2 p2 = { badgeCenter.x - triW * 0.35f, badgeCenter.y + triH * 0.5f };
        nxui::Vec2 p3 = { badgeCenter.x + triW * 0.65f, badgeCenter.y };
        ren.drawTriangle(p1, p2, p3, glow.withAlpha(0.98f * a));
    }
}

namespace {

struct BevelSample {
    float x = 0.f, y = 0.f;
    float nx = 0.f, ny = 0.f;
};

// One segment of the bevel band: a quad hanging inward from two outline
// samples, shaded by whether the surface normal faces the fixed top-left light
// or away from it. Walking the real rounded-rect outline (straight runs and
// corner arcs alike) is what makes the highlight run along the top and left and
// the shadow along the bottom and right with no seam at the corners.
void bevelSegment(nxui::Renderer& ren, const BevelSample& a, const BevelSample& b,
                  float thickness, float lightAlpha, float darkAlpha) {
    if (thickness <= 0.05f)
        return;
    const float dx = (a.nx + b.nx) * 0.5f;
    const float dy = (a.ny + b.ny) * 0.5f;
    // Light comes from the top-left, so a normal pointing up-left is lit and a
    // normal pointing down-right is shadowed; the magnitude is the incidence.
    const float d = dx * -0.6f + dy * -0.8f;
    const bool light = d >= 0.f;
    const float alpha = light ? lightAlpha * d : darkAlpha * -d;
    if (alpha <= 0.002f)
        return;
    const nxui::Color c = light ? nxui::Color(1.f, 1.f, 1.f, alpha)
                                : nxui::Color(0.f, 0.f, 0.f, alpha);
    const nxui::Vec2 p0{a.x, a.y};
    const nxui::Vec2 p1{b.x, b.y};
    const nxui::Vec2 p2{b.x - b.nx * thickness, b.y - b.ny * thickness};
    const nxui::Vec2 p3{a.x - a.nx * thickness, a.y - a.ny * thickness};
    ren.drawTriangle(p0, p1, p2, c);
    ren.drawTriangle(p0, p2, p3, c);
}

} // namespace

void GlossyIcon::drawIconOverlays(nxui::Renderer& ren, const nxui::Rect& r,
                                  float radius) {
    if (m_opacity <= 0.01f
        || (m_vignetteStrength <= 0.001f && m_highlightStrength <= 0.001f
            && m_depth <= 0.001f))
        return;
    const float side = std::min(r.width, r.height);
    if (side <= 2.f)
        return;
    const float rad = std::clamp(radius, 0.f, side * 0.5f);

    // 3D bevel: a band that follows the tile's rounded outline, shaded by a
    // fixed top-left light. Lit stretches (top, left, top-left corner) get a
    // white band, the opposite stretches (bottom, right, bottom-right) get a
    // black one, and the two pass through zero on the top-right and bottom-left
    // corners where the surface turns away from the light. Because the band is
    // walked along the outline itself, the highlight and shadow wrap every
    // corner instead of stopping where the curve begins.
    if (m_depth > 0.001f) {
        const float band = std::clamp(side * 0.10f, 5.f, 16.f)
                         * (0.55f + 0.45f * m_depth);
        const float line = std::clamp(band * 0.20f, 1.f, 3.f);
        const float softLight = 0.13f * m_depth * m_opacity;
        const float softDark = 0.11f * m_depth * m_opacity;
        const float lineLight = 0.30f * m_depth * m_opacity;
        const float lineDark = 0.26f * m_depth * m_opacity;
        const float rr = std::max(0.f, rad);
        const float x0 = r.x, y0 = r.y, x1 = r.right(), y1 = r.bottom();

        BevelSample prev;
        bool havePrev = false;
        auto push = [&](float px, float py, float nx, float ny) {
            const BevelSample cur{px, py, nx, ny};
            if (havePrev) {
                bevelSegment(ren, prev, cur, band, softLight, softDark);
                bevelSegment(ren, prev, cur, line, lineLight, lineDark);
            }
            prev = cur;
            havePrev = true;
        };
        auto edge = [&](float ax, float ay, float bx, float by,
                        float nx, float ny) {
            const float len = std::hypot(bx - ax, by - ay);
            const int steps = std::max(1, static_cast<int>(std::ceil(len / 16.f)));
            for (int i = 0; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                push(ax + (bx - ax) * t, ay + (by - ay) * t, nx, ny);
            }
        };
        auto arc = [&](float cx, float cy, float a0, float a1) {
            constexpr int kArcSteps = 8;
            for (int i = 0; i <= kArcSteps; ++i) {
                const float t = static_cast<float>(i) / kArcSteps;
                const float a = a0 + (a1 - a0) * t;
                const float ca = std::cos(a);
                const float sa = std::sin(a);
                push(cx + ca * rr, cy + sa * rr, ca, sa);
            }
        };
        constexpr float kPi = 3.14159265f;
        constexpr float kHalfPi = kPi * 0.5f;

        // Clockwise from the top edge: top, top-right arc, right, bottom-right
        // arc, bottom, bottom-left arc, left, top-left arc.
        edge(x0 + rr, y0, x1 - rr, y0, 0.f, -1.f);
        if (rr > 0.5f) arc(x1 - rr, y0 + rr, -kHalfPi, 0.f);
        edge(x1, y0 + rr, x1, y1 - rr, 1.f, 0.f);
        if (rr > 0.5f) arc(x1 - rr, y1 - rr, 0.f, kHalfPi);
        edge(x1 - rr, y1, x0 + rr, y1, 0.f, 1.f);
        if (rr > 0.5f) arc(x0 + rr, y1 - rr, kHalfPi, kPi);
        edge(x0, y1 - rr, x0, y0 + rr, -1.f, 0.f);
        if (rr > 0.5f) arc(x0 + rr, y0 + rr, kPi, kPi + kHalfPi);
    }

    // Vignette: a rim that darkens the edge and dissolves inward. The band is
    // cut into rings roughly one pixel wide and each ring's alpha is the
    // *average* of the falloff across its own width, so the ramp is the integral
    // of the gradient instead of a point sample of it. Rings are exactly one
    // step wide: the old version overlapped them by ~35%, which made every seam
    // double-cover and read as concentric lines on the artwork. Every ring
    // follows the tile's own radius, so nothing paints past a rounded corner.
    if (m_vignetteStrength > 0.001f) {
        const float band = std::clamp(side * 0.16f, 6.f, 26.f);
        const int steps = std::clamp(static_cast<int>(std::lround(band * 1.25f)),
                                     10, 30);
        const float step = band / static_cast<float>(steps);
        const float peak = 0.34f * m_vignetteStrength * m_opacity;
        for (int i = 0; i < steps; ++i) {
            const float d0 = step * static_cast<float>(i);
            const float d1 = d0 + step;
            if (d1 > band + 0.001f)
                break;
            const float a0 = 1.f - d0 / band;
            const float a1 = 1.f - d1 / band;
            // Mean of the squared falloff over [d0, d1].
            const float avg = (a0 * a0 + a0 * a1 + a1 * a1) / 3.f;
            const float mid = d0 + step * 0.5f;
            const nxui::Rect ring = r.shrunk(mid);
            if (ring.width <= 2.f || ring.height <= 2.f)
                break;
            ren.drawRoundedRectOutline(
                ring, nxui::Color(0.f, 0.f, 0.f, peak * avg),
                std::max(0.f, rad - mid), step + 0.3f);
        }
    }

    // Highlight: a soft rim light that hugs the tile edge plus a specular catch
    // along the top. The rim is stepped the same way as the vignette so it fades
    // smoothly, and the catch light is inset past the corners so it can never
    // paint a square patch onto a rounded tile.
    if (m_highlightStrength > 0.001f) {
        const float band = std::clamp(side * 0.07f, 3.f, 12.f);
        const int steps = std::clamp(static_cast<int>(std::lround(band * 2.f)),
                                     8, 24);
        const float step = band / static_cast<float>(steps);
        const float peakAlpha = 0.30f * m_highlightStrength * m_opacity;
        for (int i = 0; i < steps; ++i) {
            const float d0 = step * static_cast<float>(i);
            const float d1 = d0 + step;
            if (d1 > band + 0.001f)
                break;
            const float a0 = 1.f - d0 / band;
            const float a1 = 1.f - d1 / band;
            const float avg = (a0 * a0 + a0 * a1 + a1 * a1) / 3.f;
            const float mid = d0 + step * 0.5f;
            const nxui::Rect ring = r.shrunk(mid);
            if (ring.width <= 2.f || ring.height <= 2.f)
                break;
            ren.drawRoundedRectOutline(
                ring, nxui::Color(1.f, 1.f, 1.f, peakAlpha * avg),
                std::max(0.f, rad - mid), step + 0.3f);
        }

        // Sheen: a pool of light just under the top rim. It ramps in over a few
        // pixels instead of starting at full strength (that hard top edge is
        // what read as a drawn line), then tails off over a third of the tile.
        // Both ends are tucked under the corner arcs, where the rim light is at
        // its brightest, so their cut-off is not visible.
        const float inset = std::min(r.width * 0.5f, rad);
        const float sheenW = std::max(1.f, r.width - inset * 2.f);
        const float peakH = std::max(3.f, r.height * 0.08f);
        const float tailH = std::max(4.f, r.height * 0.42f);
        const nxui::Color clear = nxui::Color(1.f, 1.f, 1.f, 0.f);
        const nxui::Color peak = nxui::Color(
            1.f, 1.f, 1.f, 0.16f * m_highlightStrength * m_opacity);
        const nxui::Rect ramp{r.x + inset, r.y + 0.5f, sheenW, peakH};
        const nxui::Rect tail{r.x + inset, r.y + 0.5f + peakH, sheenW, tailH};
        ren.drawGradientRect(ramp, clear, peak);
        ren.drawGradientRect(tail, peak, clear);
        // A wider, much fainter veil gives the tile glass body without adding an
        // edge of its own.
        const nxui::Rect veil{r.x + inset, r.y + 0.5f + peakH, sheenW,
                              tailH * 1.6f};
        ren.drawGradientRect(
            veil, nxui::Color(1.f, 1.f, 1.f,
                              0.05f * m_highlightStrength * m_opacity),
            clear);
    }
}

void GlossyIcon::drawShine(nxui::Renderer& ren, const nxui::Rect& r, float radius) {
    if (m_shineT < 0.f || m_opacity <= 0.01f)
        return;
    const float side = std::min(r.width, r.height);
    if (side <= 6.f)
        return;
    const float rad = std::clamp(radius, 0.f, side * 0.5f);

    const float t = std::clamp(m_shineT, 0.f, 1.f);
    // Smoothstep travel plus a fade at both ends so the band never pops in or
    // out mid-tile, which would read as a white seam appearing from nowhere.
    const float travel = t * t * (3.f - 2.f * t);
    const float fade = std::clamp(std::min(t, 1.f - t) * 5.f, 0.f, 1.f);
    const float peak = 0.20f * fade * m_opacity;
    if (peak <= 0.002f)
        return;

    // The band leans to the right at the top (skew), the way a reflection does
    // when the light sits above the screen. The margin keeps the leading edge
    // fully outside the tile before the sweep starts and after it ends.
    const float halfBand = std::max(6.f, side * 0.09f);
    const float skew = r.height * 0.35f;
    const float margin = halfBand + skew * 0.5f;
    const float centerX = r.x - margin + (r.width + margin * 2.f) * travel;

    // Rows are thin enough that the rounded silhouette is followed closely, and
    // the band is cut into a handful of alpha steps across its width so the
    // streak has soft edges instead of a hard-edged bar.
    const int rows = std::max(6, static_cast<int>(std::lround(r.height / 4.f)));
    const float rowH = r.height / static_cast<float>(rows);
    constexpr int kSteps = 8;
    for (int row = 0; row < rows; ++row) {
        const float y0 = r.y + rowH * static_cast<float>(row);
        const float yc = y0 + rowH * 0.5f;
        // Horizontal reach of the rounded rect on this row, so no light paints
        // the transparent pixels outside a corner arc.
        const float dy = std::max(0.f, std::max((r.y + rad) - yc, yc - (r.bottom() - rad)));
        const float inset = rad - std::sqrt(std::max(0.f, rad * rad - dy * dy));
        const float xs0 = r.x + inset;
        const float xs1 = r.right() - inset;
        if (xs1 <= xs0)
            continue;
        const float yFrac = (yc - r.y) / std::max(1.f, r.height);
        const float cx = centerX + skew * (0.5f - yFrac);
        for (int i = 0; i < kSteps; ++i) {
            const float u0 = -1.f + 2.f * static_cast<float>(i) / kSteps;
            const float u1 = -1.f + 2.f * static_cast<float>(i + 1) / kSteps;
            const float mid = (u0 + u1) * 0.5f;
            const float w = 1.f - std::abs(mid);
            const float a = peak * w * w;
            if (a <= 0.002f)
                continue;
            const float x0 = std::max(xs0, cx + u0 * halfBand);
            const float x1 = std::min(xs1, cx + u1 * halfBand);
            if (x1 <= x0)
                continue;
            ren.drawRect({x0, y0, x1 - x0, rowH + 0.5f},
                         nxui::Color(1.f, 1.f, 1.f, a));
        }
    }
}

void GlossyIcon::onContentRender(nxui::Renderer& ren) {
    float s = scale();
    float rad = cornerRadius();

    nxui::Rect r = m_rect;
    if (s < 1.f) {
        float w = r.width  * s;
        float h = r.height * s;
        r.x += (r.width  - w) * 0.5f;
        r.y += (r.height - h) * 0.5f;
        r.width  = w;
        r.height = h;
    }

    if (m_entryKind == GridEntryKind::Empty) {
        ren.drawRoundedRectOutline(r.shrunk(2.f * s),
                                   nxui::Color::white().withAlpha(0.42f * m_opacity),
                                   std::max(8.f, rad - 2.f * s), 1.5f * s);
        return;
    }

    if (m_entryKind == GridEntryKind::WidgetContinuation)
        return;

    if (m_entryKind == GridEntryKind::Widget) {
        const nxui::Rect inner = r.shrunk(std::max(8.f, 10.f * s));
        if (m_widgetType == switchu::widgets::WidgetType::RecentlyPlayed) {
            const nxui::Rect card = inner;
            const float contentRadius = std::max(8.f, rad - 5.f);
            if (m_widgetHero && m_widgetHero->valid()) {
                ren.drawTextureSubRounded(
                    m_widgetHero, centeredCoverSource(*m_widgetHero, card), card,
                    contentRadius, nxui::Color::white().withAlpha(m_opacity));
            } else {
                ren.drawRoundedRect(card, m_loadingColor.withAlpha(0.20f * m_opacity),
                                    contentRadius);
            }
            if (m_font && !m_widgetHeader.empty()) {
                const float headerScale = 0.44f * s;
                ren.drawText(m_widgetHeader,
                    {card.x + 12.f * s, card.y + 9.f * s}, m_font,
                    nxui::Color::white().withAlpha(0.92f * m_opacity),
                    headerScale);
            }
            const nxui::Rect logoArea{card.x + 12.f * s,
                                      card.y + card.height * 0.58f,
                                      card.width * 0.58f,
                                      card.height * 0.32f};
            if (m_widgetLogo && m_widgetLogo->valid()) {
                const float aspect = static_cast<float>(m_widgetLogo->width()) /
                    std::max(1.f, static_cast<float>(m_widgetLogo->height()));
                nxui::Rect logoRect = logoArea;
                if (aspect > logoArea.width / logoArea.height) {
                    logoRect.height = logoArea.width / aspect;
                    logoRect.y += (logoArea.height - logoRect.height) * 0.5f;
                } else {
                    logoRect.width = logoArea.height * aspect;
                    logoRect.y += 0.f;
                }
                ren.drawTexture(m_widgetLogo, logoRect,
                    nxui::Color::white().withAlpha(0.98f * m_opacity));
            } else if (m_widgetGameIcon && m_widgetGameIcon->valid()) {
                const float iconSide = std::min(logoArea.height, 54.f * s);
                ren.drawTextureRounded(m_widgetGameIcon,
                    {logoArea.x, logoArea.y + logoArea.height - iconSide,
                     iconSide, iconSide}, 10.f * s,
                    nxui::Color::white().withAlpha(m_opacity));
            } else if (m_font && !m_widgetPrimary.empty()) {
                const nxui::Vec2 measured = m_font->measure(m_widgetPrimary);
                float scale = 0.70f * s;
                if (measured.x > 0.f)
                    scale = std::min(scale, logoArea.width / measured.x);
                ren.drawText(m_widgetPrimary,
                    {logoArea.x, logoArea.y + (logoArea.height - measured.y * scale) * 0.5f},
                    m_font, nxui::Color::white().withAlpha(0.96f * m_opacity), scale);
            }
            return;
        }

        if (m_widgetType == switchu::widgets::WidgetType::RecentPlaytime) {
            ren.drawRoundedRect(inner, m_loadingColor.withAlpha(0.13f * m_opacity),
                                std::max(9.f, rad - 5.f));
            if (!m_font) return;

            const float headerScale = 0.48f * s;
            ren.drawText(m_widgetHeader, {inner.x + 10.f * s, inner.y + 8.f * s},
                         m_font, nxui::Color::white().withAlpha(0.68f * m_opacity),
                         headerScale);
            const float headerHeight = m_font->measure(m_widgetHeader).y * headerScale;
            const float bodyTop = inner.y + 12.f * s + headerHeight;
            const float bodyHeight = std::max(28.f, inner.bottom() - bodyTop - 8.f * s);
            const float iconSide = std::min(bodyHeight, inner.width * 0.30f);
            const nxui::Rect iconRect{inner.x + 10.f * s,
                                      bodyTop + (bodyHeight - iconSide) * 0.5f,
                                      iconSide, iconSide};
            if (m_widgetGameIcon && m_widgetGameIcon->valid()) {
                ren.drawTextureRounded(m_widgetGameIcon, iconRect,
                    std::max(8.f, 12.f * s),
                    nxui::Color::white().withAlpha(m_opacity));
            } else {
                ren.drawRoundedRect(iconRect,
                    nxui::Color::white().withAlpha(0.10f * m_opacity),
                    std::max(8.f, 12.f * s));
            }

            const float textX = iconRect.right() + 12.f * s;
            const float textWidth = std::max(20.f, inner.right() - textX - 10.f * s);
            const nxui::Vec2 nameMeasure = m_font->measure(m_widgetPrimary);
            float nameScale = 0.62f * s;
            if (nameMeasure.x > 0.f)
                nameScale = std::min(nameScale, textWidth / nameMeasure.x);
            nameScale = std::max(0.38f * s, nameScale);
            const nxui::Vec2 timeMeasure = m_font->measure(m_widgetSecondary);
            float timeScale = 0.52f * s;
            if (timeMeasure.x > 0.f)
                timeScale = std::min(timeScale, textWidth / timeMeasure.x);
            const float nameHeight = nameMeasure.y * nameScale;
            const float timeHeight = timeMeasure.y * timeScale;
            const float textTop = bodyTop +
                (bodyHeight - nameHeight - timeHeight - 4.f * s) * 0.5f;
            ren.drawText(m_widgetPrimary, {textX, textTop}, m_font,
                         nxui::Color::white().withAlpha(0.96f * m_opacity), nameScale);
            ren.drawText(m_widgetSecondary,
                         {textX, textTop + nameHeight + 4.f * s}, m_font,
                         nxui::Color::white().withAlpha(0.72f * m_opacity), timeScale);
            return;
        }

        if (m_widgetType == switchu::widgets::WidgetType::Batteries) {
            ren.drawRoundedRect(inner,
                m_loadingColor.withAlpha(0.13f * m_opacity),
                std::max(12.f, rad - 5.f));
            const float cellW = inner.width;
            const float cellH = inner.height;
            const float radius = std::max(24.f,
                std::min(cellW, cellH) * 0.36f);
            const nxui::Vec2 center{
                inner.x + cellW * 0.5f,
                inner.y + cellH * 0.47f};
            drawBatteryRing(ren, center, radius,
                m_consoleBatteryPercent, m_consoleBatteryCharging,
                true, m_batteryConsoleIcon, m_font, m_opacity);
            return;
        }

        const bool imageWidget = m_widgetType == switchu::widgets::WidgetType::ImagePin
            || m_widgetType == switchu::widgets::WidgetType::RandomScreenshot;
        nxui::Texture* image = m_widgetExternalTexture
            ? m_widgetExternalTexture
            : (m_widgetAnimation.hasFrames()
                ? m_widgetAnimation.currentFrame() : m_widgetTexture.get());
        if (imageWidget && image && image->valid()) {
            const float sourceAspect = static_cast<float>(image->width()) /
                std::max(1.f, static_cast<float>(image->height()));
            const float targetAspect = inner.width / std::max(1.f, inner.height);
            nxui::Rect imageRect = inner;
            if (sourceAspect > targetAspect) {
                imageRect.height = inner.width / sourceAspect;
                imageRect.y += (inner.height - imageRect.height) * 0.5f;
            } else {
                imageRect.width = inner.height * sourceAspect;
                imageRect.x += (inner.width - imageRect.width) * 0.5f;
            }
            ren.drawTextureRounded(image, imageRect,
                                   std::max(8.f, rad - 6.f),
                                   nxui::Color::white().withAlpha(m_opacity));
            if (m_focused)
                ren.drawRoundedRectOutline(inner.expanded(2.f),
                    nxui::Color::white().withAlpha(0.42f * m_opacity),
                    std::max(8.f, rad - 4.f), 2.f);
            return;
        }

        nxui::Color accent = m_loadingColor;
        ren.drawRoundedRect(inner, accent.withAlpha(0.13f * m_opacity),
                            std::max(9.f, rad - 5.f));
        if (!m_font) return;

        std::string primary = m_widgetPrimary;
        std::string secondary = m_widgetSecondary;
        if (m_widgetType == switchu::widgets::WidgetType::Clock) {
            std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_r(&now, &local);
            char timeBuffer[16]{};
            char dateBuffer[48]{};
            std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &local);
            std::strftime(dateBuffer, sizeof(dateBuffer), "%a %d %b", &local);
            primary = timeBuffer;
            // Wide grid clocks and tall single-row posters both get the date.
            if (m_widgetColumns > 1 || m_posterFrame) secondary = dateBuffer;
        }

        const nxui::Vec2 primaryMeasure = m_font->measure(primary);
        float primaryScale = 0.78f;
        if (m_widgetType == switchu::widgets::WidgetType::Clock) {
            if (m_posterFrame)
                primaryScale = 1.92f;
            else if (m_widgetColumns > 1)
                primaryScale = 1.55f;
            else
                primaryScale = 1.18f;
        }
        if (primaryMeasure.x > 0.f)
            primaryScale = std::min(primaryScale,
                (inner.width - 18.f) / primaryMeasure.x);
        primaryScale = std::max(0.48f, primaryScale);
        const float primaryHeight = primaryMeasure.y * primaryScale;
        float totalHeight = primaryHeight;
        float secondaryScale = m_posterFrame ? 0.78f : 0.62f;
        nxui::Vec2 secondaryMeasure{};
        if (!secondary.empty()) {
            secondaryMeasure = m_font->measure(secondary);
            if (secondaryMeasure.x > 0.f)
                secondaryScale = std::min(secondaryScale,
                    (inner.width - 18.f) / secondaryMeasure.x);
            secondaryScale = std::max(0.42f, secondaryScale);
            totalHeight += (m_posterFrame ? 12.f : 7.f)
                + secondaryMeasure.y * secondaryScale;
        }
        float textY = inner.y + (inner.height - totalHeight) * 0.5f;
        ren.drawText(primary,
            {inner.x + (inner.width - primaryMeasure.x * primaryScale) * 0.5f, textY},
            m_font, nxui::Color::white().withAlpha(0.98f * m_opacity), primaryScale);
        if (!secondary.empty()) {
            textY += primaryHeight + (m_posterFrame ? 12.f : 7.f);
            ren.drawText(secondary,
                {inner.x + (inner.width - secondaryMeasure.x * secondaryScale) * 0.5f,
                 textY},
                m_font, nxui::Color::white().withAlpha(0.72f * m_opacity),
                secondaryScale);
        }
        return;
    }

    if (m_entryKind == GridEntryKind::Folder) {
        const int style = m_folderStyleIndex;
        const bool mosaic = style == switchu::folders::kFolderStyleClassic;
        const float inset = (m_posterFrame ? 6.f : (mosaic ? 10.f : 8.f)) * s;
        switchu::folders::FolderStyleDrawArgs args;
        args.renderer = &ren;
        args.bounds = r.shrunk(inset);
        args.radius = std::max(12.f, rad - 2.f);
        args.scale = s;
        args.opacity = m_opacity;
        args.styleIndex = style;
        args.accent = switchu::folders::colorForIndex(m_folderColorIndex);
        args.themeMode = m_themeMode;
        args.font = m_font;
        args.title = &m_title;
        args.cover = m_folderCover;
        args.peekCovers = m_folderPeeks;
        args.peekCoverCount = m_folderPeekCount;
        args.showCover = m_folderShowCover;
        args.focused = m_focused;
        // Grid: name inside the tile. Single-row with peeks/covers: title pill
        // only. Empty poster folders (FT lite suppresses peeks) get the same
        // centered in-tile name as grid so the tile is not a blank plate.
        const bool hasFolderArt =
            (m_folderCover && m_folderCover->valid())
            || m_folderPeekCount > 0
            || m_folderPeekComposite;
        args.drawName = !m_posterFrame || !hasFolderArt;
        args.posterAspect = m_posterFrame;
        args.coverIsFolderPoster = m_folderCoverIsPoster;
        args.coverIsPeekComposite = m_folderPeekComposite;
        switchu::folders::drawFolderStyle(args);
        return;
    }

    // Landscape wide face: 2x1 / 4x2 (same 2:1 aspect). 3x2 removed — aspect misfit.
    const bool landscapeWideTile =
        switchu::widgets::isLandscapeWideGameTile(m_widgetColumns, m_widgetRows);
    // Tall poster face: 2x3 (and legacy 1x2). 2x2 / 1x1 always use the icon path.
    const bool tallPosterTile =
        (m_widgetColumns == 2 && m_widgetRows == 3)
        || (m_widgetColumns == 1 && m_widgetRows == 2);

    if (m_entryKind == GridEntryKind::Application && tallPosterTile) {
        const nxui::Rect card = r.shrunk(std::max(8.f, 10.f * s));
        const float contentRadius = std::max(8.f, rad - 5.f);
        if (m_tex && m_tex->valid()) {
            // Cover-fit so poster/icon aspect is never stretched into the cell.
            ren.drawTextureSubRounded(
                m_tex, centeredCoverSource(*m_tex, card), card,
                contentRadius, nxui::Color::white().withAlpha(m_opacity));
        } else {
            ren.drawRoundedRect(card,
                m_loadingColor.withAlpha(0.18f * m_opacity), contentRadius);
        }
        drawIconOverlays(ren, card, contentRadius);
        return;
    }

    // Single-row posters use m_tex; never draw landscape wide faces there.
    if (m_entryKind == GridEntryKind::Application && landscapeWideTile
        && !m_posterFrame) {
        const nxui::Rect card = r.shrunk(std::max(8.f, 10.f * s));
        const float contentRadius = std::max(8.f, rad - 5.f);
        if (m_wideGameHero && m_wideGameHero->valid()) {
            ren.drawTextureSubRounded(
                m_wideGameHero, centeredCoverSource(*m_wideGameHero, card), card,
                contentRadius, nxui::Color::white().withAlpha(m_opacity));
        } else if (m_tex && m_tex->valid()) {
            ren.drawTextureSubRounded(
                m_tex, centeredCoverSource(*m_tex, card), card,
                contentRadius, nxui::Color::white().withAlpha(m_opacity));
        } else {
            ren.drawRoundedRect(card,
                m_loadingColor.withAlpha(0.18f * m_opacity), contentRadius);
        }
        drawIconOverlays(ren, card, contentRadius);
        const nxui::Rect logoArea{card.x + 12.f * s,
                                  card.y + card.height * 0.58f,
                                  card.width * 0.58f,
                                  card.height * 0.32f};
        if (m_wideGameLogo && m_wideGameLogo->valid()) {
            const float aspect = static_cast<float>(m_wideGameLogo->width()) /
                std::max(1.f, static_cast<float>(m_wideGameLogo->height()));
            nxui::Rect logoRect = logoArea;
            if (aspect > logoArea.width / logoArea.height) {
                logoRect.height = logoArea.width / aspect;
                logoRect.y += (logoArea.height - logoRect.height) * 0.5f;
            } else {
                logoRect.width = logoArea.height * aspect;
            }
            ren.drawTexture(m_wideGameLogo, logoRect,
                            nxui::Color::white().withAlpha(0.98f * m_opacity));
        } else if (m_font && !m_title.empty()
                   && !(m_wideGameHero && m_wideGameHero->valid())
                   && !(m_tex && m_tex->valid())) {
            // Title fallback only on empty wide cards — never overlay hero/icon.
            const auto measured = m_font->measure(m_title);
            float textScale = 0.68f * s;
            if (measured.x > 0.f)
                textScale = std::min(textScale, logoArea.width / measured.x);
            ren.drawText(m_title,
                {logoArea.x, logoArea.y + (logoArea.height - measured.y * textScale) * 0.5f},
                m_font, nxui::Color::white().withAlpha(0.96f * m_opacity),
                textScale);
        }
        return;
    }

    if (!m_tex || !m_tex->valid()) {
        if (m_titleId == 0)
            return;

        const nxui::Vec2 center{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
        const float outerRadius = std::clamp(std::min(r.width, r.height) * 0.105f,
                                             8.f, 16.f) * s;
        const float innerRadius = outerRadius * 0.48f;
        constexpr int kSpokes = 10;
        constexpr float kStep = 6.28318530718f / (float)kSpokes;
        const float angleBase = m_suspendPulse * 2.8f;
        for (int i = 0; i < kSpokes; ++i) {
            const float angle = angleBase + kStep * (float)i;
            const float weight = 1.f - (float)i / (float)kSpokes;
            const nxui::Color color = m_loadingColor.withAlpha(
                (0.14f + 0.72f * weight) * m_opacity);
            ren.drawLine(
                {center.x + std::cos(angle) * innerRadius,
                 center.y + std::sin(angle) * innerRadius},
                {center.x + std::cos(angle) * outerRadius,
                 center.y + std::sin(angle) * outerRadius},
                color,
                std::max(1.f, (2.4f - 0.9f * ((float)i / (float)kSpokes)) * s));
        }
        return;
    }

    float inset = 8.f * s;
    nxui::Rect texRect = r.shrunk(inset);
    // Contain-fit: square icons stay square inside tall Poster tiles instead
    // of stretching to fill the 2:3 cell.
    if (m_tex->width() > 0 && m_tex->height() > 0 && texRect.width > 0.f
        && texRect.height > 0.f) {
        const float texAspect = (float)m_tex->width() / (float)m_tex->height();
        const float boxAspect = texRect.width / texRect.height;
        if (texAspect > boxAspect) {
            const float h = texRect.width / texAspect;
            texRect.y += (texRect.height - h) * 0.5f;
            texRect.height = h;
        } else if (texAspect < boxAspect) {
            const float w = texRect.height * texAspect;
            texRect.x += (texRect.width - w) * 0.5f;
            texRect.width = w;
        }
    }
    nxui::Color iconTint = nxui::Color::white().withAlpha(m_opacity);
    if (m_notLaunchable) {
        iconTint.r = 0.80f;
        iconTint.g = 0.80f;
        iconTint.b = 0.80f;
    }
    ren.drawTextureRounded(m_tex, texRect, rad - 3.f, iconTint);
    drawIconOverlays(ren, texRect, std::max(0.f, rad - 3.f));
}
