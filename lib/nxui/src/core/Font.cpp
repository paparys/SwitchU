#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Rasterize at (or above) the requested size in quarter steps. Below 1x the
// base raster is kept and minified, which stays sharper than a tiny bitmap.
float textRasterBucket(float scale) {
    return std::max(1.f, std::round(std::clamp(scale, 0.1f, 4.f) * 4.f) / 4.f);
}

} // namespace

namespace nxui {

Font::~Font() {
    for (auto& entry : m_sizeFonts) {
        if (entry.second && entry.second != m_font)
            TTF_CloseFont(entry.second);
    }
    m_sizeFonts.clear();
    if (m_font) TTF_CloseFont(m_font);
}

bool Font::load(GpuDevice& gpu, Renderer& ren,
                const std::string& path, int ptSize)
{
    TTF_Font* newFont = TTF_OpenFont(path.c_str(), ptSize);
    if (!newFont) {
        std::fprintf(stderr, "[Font] TTF_OpenFont failed: %s\n", TTF_GetError());
        return false;
    }

    for (auto& entry : m_sizeFonts) {
        if (entry.second && entry.second != newFont)
            TTF_CloseFont(entry.second);
    }
    m_sizeFonts.clear();
    m_measureCache.clear();

    if (m_font)
        TTF_CloseFont(m_font);

    m_font = newFont;
    m_path = path;
    m_gpu = &gpu;
    m_ren = &ren;
    m_ptSize = ptSize;
    ++m_revision;
    clearCache();
    return true;
}

Vec2 Font::measure(const std::string& text) const {
    if (!m_font || text.empty()) return {0, 0};
    // Measured with the base face so layout is stable no matter which raster
    // size happened to be cached for drawing; the cache keeps SDL_ttf off the
    // hot path for labels that are measured every frame.
    const auto cached = m_measureCache.find(text);
    if (cached != m_measureCache.end())
        return cached->second;
    int w = 0, h = 0;
    TTF_SizeUTF8(m_font, text.c_str(), &w, &h);
    const Vec2 size{(float)w, (float)h};
    if (m_measureCache.size() > 512)
        m_measureCache.clear();
    m_measureCache[text] = size;
    return size;
}

void Font::clearCache() {
    m_lruList.clear();
    m_lruMap.clear();
    m_cacheBytes = 0;
    m_maintenanceRequested = false;
}

void Font::trimCache(std::size_t maxEntries, std::size_t maxBytes) {
    while (!m_lruList.empty() &&
           (m_lruList.size() > maxEntries || m_cacheBytes > maxBytes)) {
        auto victim = std::prev(m_lruList.end());
        const std::size_t bytes = victim->tex.allocationSize();
        m_lruMap.erase(victim->key);
        m_lruList.erase(victim);
        m_cacheBytes = bytes <= m_cacheBytes ? m_cacheBytes - bytes : 0;
    }
    m_maintenanceRequested = false;
}

Texture* Font::getOrRender(GpuDevice& gpu, Renderer& ren, const std::string& text,
                           float scale) {
    const int pixelSize = std::max(
        1, static_cast<int>(std::lround(m_ptSize * textRasterBucket(scale))));
    const std::string key = text + '\x1f' + std::to_string(pixelSize);

    // Cache hit: promote to the front.
    auto it = m_lruMap.find(key);
    if (it != m_lruMap.end()) {
        // Move to front (most-recently-used)
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
        return &it->second->tex;
    }

    // Render the string via SDL_ttf at the size it will be drawn.
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderUTF8_Blended(fontForPixels(pixelSize),
                                                  text.c_str(), white);
    if (!surface) return nullptr;

    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(surface);
    if (!rgba) return nullptr;

    SDL_LockSurface(rgba);
    const uint8_t* pixels = static_cast<const uint8_t*>(rgba->pixels);
    int w = rgba->w, h = rgba->h, pitch = rgba->pitch;

    constexpr std::size_t kTextureAllocationAlignment = 4096u;
    const std::size_t estimatedBytes =
        (static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u +
         kTextureAllocationAlignment - 1u) &
        ~(kTextureAllocationAlignment - 1u);
    if (m_lruList.size() >= kMaxCacheEntries ||
        m_cacheBytes + estimatedBytes > kMaxCacheBytes ||
        gpu.imageMemoryAvailable() < estimatedBytes + kGpuHeadroom) {
        m_maintenanceRequested = true;
        SDL_UnlockSurface(rgba);
        SDL_FreeSurface(rgba);
        return nullptr;
    }

    m_lruList.emplace_front();
    auto& entry = m_lruList.front();
    entry.key = key;
    entry.w = w;
    entry.h = h;
    if (!entry.tex.loadFromSurface(gpu, ren, pixels, w, h, pitch)) {
        m_lruList.pop_front();
        m_maintenanceRequested = true;
        SDL_UnlockSurface(rgba);
        SDL_FreeSurface(rgba);
        return nullptr;
    }
    m_cacheBytes += entry.tex.allocationSize();

    SDL_UnlockSurface(rgba);
    SDL_FreeSurface(rgba);

    m_lruMap[key] = m_lruList.begin();
    return &m_lruList.front().tex;
}

TTF_Font* Font::fontForPixels(int pixelSize) {
    if (!m_font || pixelSize <= 0 || pixelSize == m_ptSize)
        return m_font;
    const auto cached = m_sizeFonts.find(pixelSize);
    if (cached != m_sizeFonts.end())
        return cached->second;
    if (m_path.empty())
        return m_font;
    TTF_Font* font = TTF_OpenFont(m_path.c_str(), pixelSize);
    if (!font)
        return m_font;
    m_sizeFonts[pixelSize] = font;
    return font;
}

void Font::draw(Renderer& ren, const std::string& text,
                const Vec2& pos, const Color& color, float scale) {
    if (!m_font || !m_gpu || text.empty()) return;

    Texture* tex = getOrRender(*m_gpu, ren, text, scale);
    if (!tex || !tex->valid()) return;

    // The texture was rasterized at a quarter-step bucket at or above the draw
    // size, so this is a near-1:1 sample (usually a slight minify) instead of a
    // magnification of a 24 pt bitmap.
    const float rasterScale = scale / textRasterBucket(scale);
    float w = tex->width()  * rasterScale;
    float h = tex->height() * rasterScale;
    // Whole-pixel origin: a fractional origin is the difference between crisp
    // and soft labels.
    ren.drawTexture(tex, {std::round(pos.x), std::round(pos.y), w, h}, color);
}

} // namespace nxui
