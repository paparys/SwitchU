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
    clearAtlases();
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
    if (m_measureCache.size() >= 1024) {
        // Drop a slice rather than the whole table: clearing it made every
        // visible label re-measure through SDL_ttf on the very next frame.
        auto it = m_measureCache.begin();
        for (int i = 0; i < 256 && it != m_measureCache.end(); ++i)
            it = m_measureCache.erase(it);
    }
    m_measureCache[text] = size;
    return size;
}

void Font::clearCache() {
    clearAtlases();
    m_lruList.clear();
    m_lruMap.clear();
    m_cacheBytes = 0;
    m_maintenanceRequested = false;
}

void Font::clearAtlases() {
    m_atlases.clear();
    m_atlasBytes = 0;
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
    const int pixelSize = std::max(1, static_cast<int>(std::lround(
        m_ptSize * textRasterBucket(scale))));
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
    // Both display modes render at 1280x720, so the cache budgets stay fixed.
    // Evict least-recently-used strings instead of refusing to draw. This used
    // to return nullptr once the cache reached its cap, which made every label
    // with a not-yet-cached string vanish instead of re-rasterizing.
    while (!m_lruList.empty() &&
           (m_lruList.size() >= kMaxCacheEntries ||
            m_cacheBytes + estimatedBytes > kMaxCacheBytes)) {
        auto victim = std::prev(m_lruList.end());
        const std::size_t freed = victim->tex.allocationSize();
        m_lruMap.erase(victim->key);
        m_lruList.erase(victim);
        m_cacheBytes = freed <= m_cacheBytes ? m_cacheBytes - freed : 0;
    }
    if (m_cacheBytes + estimatedBytes > kMaxCacheBytes ||
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

Font::Atlas* Font::atlasForSize(int pixelSize) {
    auto found = m_atlases.find(pixelSize);
    if (found != m_atlases.end())
        return found->second.get();
    if (!m_gpu || !m_ren || !m_font || pixelSize <= 0)
        return nullptr;

    TTF_Font* face = fontForPixels(pixelSize);
    if (!face || (pixelSize != m_ptSize && face == m_font))
        return nullptr;

    auto atlas = std::make_unique<Atlas>();
    atlas->pixelSize = pixelSize;
    atlas->ascent = TTF_FontAscent(face);

    constexpr int kSize = kAtlasPageSize;
    constexpr int kPad = 1;
    constexpr std::size_t kPageBytes =
        static_cast<std::size_t>(kSize) * static_cast<std::size_t>(kSize) * 4u;
    std::vector<std::vector<std::uint8_t>> pagePixels;
    int cursorX = kPad;
    int cursorY = kPad;
    int rowHeight = 0;

    auto addPage = [&]() -> bool {
        if ((int)atlas->pages.size() >= kAtlasMaxPages)
            return false;
        if (m_atlasBytes + atlas->bytes + kPageBytes + kGpuHeadroom >
            m_gpu->imageMemoryAvailable())
            return false;
        if (m_atlasBytes + atlas->bytes + kPageBytes > kAtlasMaxBytes)
            return false;
        pagePixels.emplace_back(kPageBytes, 0u);
        cursorX = kPad;
        cursorY = kPad;
        rowHeight = 0;
        return true;
    };

    if (!addPage()) {
        m_atlases.emplace(pixelSize, std::move(atlas));
        return nullptr;
    }

    SDL_Color white = {255, 255, 255, 255};
    bool ok = true;
    for (int code = 32; code <= 126 && ok; ++code) {
        AtlasGlyph& glyph = atlas->glyphs[(std::size_t)(code - 32)];
        int minX = 0, maxX = 0, minY = 0, maxY = 0, advance = 0;
        if (TTF_GlyphMetrics(face, (Uint16)code, &minX, &maxX, &minY, &maxY,
                             &advance) != 0) {
            glyph.advance = 0;
            glyph.present = false;
            continue;
        }
        glyph.minX = minX;
        glyph.maxY = maxY;
        glyph.advance = advance;

        SDL_Surface* surface = TTF_RenderGlyph_Blended(face, (Uint16)code, white);
        if (!surface) {
            glyph.present = false;
            continue;
        }
        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(surface);
        if (!rgba) {
            ok = false;
            break;
        }
        const int gw = rgba->w;
        const int gh = rgba->h;
        if (gw <= 0 || gh <= 0 || gw >= kSize || gh >= kSize) {
            SDL_FreeSurface(rgba);
            glyph.present = false;
            continue;
        }

        if (cursorX + gw + kPad > kSize) {
            cursorX = kPad;
            cursorY += rowHeight + kPad;
            rowHeight = 0;
        }
        if (cursorY + gh + kPad > kSize) {
            if (!addPage()) {
                SDL_FreeSurface(rgba);
                ok = false;
                break;
            }
        }

        SDL_LockSurface(rgba);
        auto& pixels = pagePixels.back();
        const int pitch = rgba->pitch;
        const auto* src = static_cast<const std::uint8_t*>(rgba->pixels);
        for (int row = 0; row < gh; ++row) {
            std::memcpy(pixels.data() +
                            (static_cast<std::size_t>(cursorY + row) * kSize +
                             static_cast<std::size_t>(cursorX)) * 4u,
                        src + static_cast<std::size_t>(row) * static_cast<std::size_t>(pitch),
                        static_cast<std::size_t>(gw) * 4u);
        }
        SDL_UnlockSurface(rgba);
        SDL_FreeSurface(rgba);

        glyph.page = static_cast<int>(pagePixels.size()) - 1;
        glyph.sx = cursorX;
        glyph.sy = cursorY;
        glyph.w = gw;
        glyph.h = gh;
        glyph.present = true;
        cursorX += gw + kPad;
        rowHeight = std::max(rowHeight, gh);
    }

    if (ok) {
        for (auto& pixels : pagePixels) {
            auto tex = std::make_unique<Texture>();
            if (!tex->loadFromSurface(*m_gpu, *m_ren, pixels.data(), kSize, kSize,
                                      kSize * 4)) {
                ok = false;
                break;
            }
            atlas->bytes += tex->allocationSize();
            atlas->pages.push_back(std::move(tex));
        }
    }

    if (!ok || atlas->pages.empty()) {
        atlas->pages.clear();
        atlas->bytes = 0;
        atlas->ready = false;
    } else {
        atlas->ready = true;
        m_atlasBytes += atlas->bytes;
        m_maintenanceRequested = true;
    }

    Atlas* raw = atlas.get();
    m_atlases.emplace(pixelSize, std::move(atlas));
    return raw;
}

bool Font::drawWithAtlas(Renderer& ren, const std::string& text,
                         const Vec2& pos, const Color& color, float scale) {
    for (unsigned char c : text) {
        if (c < 32 || c > 126)
            return false;
    }
    if (text.empty() || !m_gpu)
        return false;

    const float displayScale = std::max(1.f, m_gpu->displayScale());
    const int pixelSize = std::max(1, static_cast<int>(std::lround(
        m_ptSize * textRasterBucket(scale * displayScale))));
    Atlas* atlas = atlasForSize(pixelSize);
    if (!atlas || !atlas->ready)
        return false;

    const float bucket = textRasterBucket(scale * displayScale);
    const float glyphScale = scale / bucket;
    const float x = std::round(pos.x * displayScale) / displayScale;
    const float y = std::round(pos.y * displayScale) / displayScale;

    // The whole-string fallback draws the string's ink box at pos, and Label
    // measures that same ink box for layout. Anchor the atlas path to it too:
    // glyphs are placed relative to the string's own ink top/left instead of the
    // font's ascender line. Anchoring to the ascender line pushed every ASCII
    // string down by an amount that depended on its tallest glyph, so text sat
    // off its measured box and strings with different glyph coverage (or the
    // non-ASCII fallback) disagreed vertically.
    int inkTop = 0;
    int inkLeft = 0;
    {
        int top = 0;
        int left = 0;
        bool any = false;
        for (unsigned char c : text) {
            const AtlasGlyph& glyph = atlas->glyphs[(std::size_t)(c - 32)];
            if (!glyph.present || glyph.w <= 0 || glyph.h <= 0)
                continue;
            const int glyphTop = atlas->ascent - glyph.maxY;
            if (!any || glyphTop < top) top = glyphTop;
            if (!any || glyph.minX < left) left = glyph.minX;
            any = true;
        }
        if (any) {
            inkTop = top;
            inkLeft = left;
        }
    }

    float pen = 0.f;
    for (unsigned char c : text) {
        const AtlasGlyph& glyph = atlas->glyphs[(std::size_t)(c - 32)];
        if (glyph.page >= 0 && glyph.page < (int)atlas->pages.size() &&
            glyph.present && glyph.w > 0 && glyph.h > 0) {
            const Texture* tex = atlas->pages[(std::size_t)glyph.page].get();
            if (tex && tex->valid()) {
                const float gx = x + (float)(pen + glyph.minX - inkLeft) * glyphScale;
                const float gy = y +
                    (float)((atlas->ascent - glyph.maxY) - inkTop) * glyphScale;
                ren.drawTextureSub(tex,
                                   {(float)glyph.sx, (float)glyph.sy,
                                    (float)glyph.w, (float)glyph.h},
                                   {gx, gy, (float)glyph.w * glyphScale,
                                    (float)glyph.h * glyphScale},
                                   color);
            }
        }
        pen += (float)glyph.advance;
    }
    return true;
}

void Font::draw(Renderer& ren, const std::string& text,
                const Vec2& pos, const Color& color, float scale) {
    if (!m_font || !m_gpu || text.empty()) return;

    // Per-glyph atlas drawing is off. It places every glyph from its own
    // metrics, and on this renderer that kept surfacing as letters drifting
    // vertically inside words. The whole-string path below is SDL_ttf's own
    // layout of the string, so its pixels are exactly what measure() reports
    // and what Label positions; there is no per-glyph math left to get wrong.
    // The atlas builder and drawWithAtlas stay in the file behind this switch
    // so it can be re-enabled once it can be checked on a console.
    static constexpr bool kUseGlyphAtlas = false;
    if (kUseGlyphAtlas && drawWithAtlas(ren, text, pos, color, scale))
        return;

    // The framebuffer is 1280x720 in both modes, so UI coordinates map 1:1.
    const float renderScale = scale;
    Texture* tex = getOrRender(*m_gpu, ren, text, renderScale);
    if (!tex || !tex->valid()) return;

    // The texture was rasterized at a quarter-step bucket at or above the draw
    // size, so this is a near-1:1 sample (usually a slight minify) instead of a
    // magnification of a 24 pt bitmap.
    const float rasterScale = scale / textRasterBucket(renderScale);
    float w = tex->width()  * rasterScale;
    float h = tex->height() * rasterScale;
    // Snap the origin to a whole framebuffer pixel.
    const float x = std::round(pos.x);
    const float y = std::round(pos.y);
    ren.drawTexture(tex, {x, y, w, h}, color);
}

} // namespace nxui
