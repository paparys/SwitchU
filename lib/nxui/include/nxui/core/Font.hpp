#pragma once
#include <nxui/core/Types.hpp>
#include "Texture.hpp"
#include <SDL2/SDL_ttf.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <list>

namespace nxui {

class Renderer;
class GpuDevice;

class Font {
public:
    Font() = default;
    ~Font();

    bool load(GpuDevice& gpu, Renderer& ren,
              const std::string& path, int ptSize);

    // Render a string to a texture (cached) and draw it
    void draw(Renderer& ren, const std::string& text,
              const Vec2& pos, const Color& color, float scale = 1.f);

    // Measure text dimensions
    Vec2 measure(const std::string& text) const;

    // Clear all cached glyph textures (call before GPU descriptor reset)
    void clearCache();

    bool maintenanceRequested() const { return m_maintenanceRequested; }
    std::size_t cacheBytes() const { return m_cacheBytes; }
    std::size_t cacheEntryCount() const { return m_lruList.size(); }
    void trimCache(std::size_t maxEntries = 72,
                   std::size_t maxBytes = 2u * 1024u * 1024u);

    int ptSize() const { return m_ptSize; }
    std::uint64_t revision() const { return m_revision; }

private:
    // Render full string to texture. Keyed by text + raster pixel size, so a
    // label drawn at 120% is rasterized at 120% instead of magnified.
    Texture* getOrRender(GpuDevice& gpu, Renderer& ren, const std::string& text,
                         float scale);
    TTF_Font* fontForPixels(int pixelSize);

    TTF_Font* m_font = nullptr;
    std::string m_path;
    // Extra faces opened at the raster size a scaled label asks for. A handful
    // of sizes covers every scale the UI uses; the base face stays for measure().
    std::unordered_map<int, TTF_Font*> m_sizeFonts;
    mutable std::unordered_map<std::string, Vec2> m_measureCache;
    int       m_ptSize = 0;
    GpuDevice* m_gpu = nullptr;
    Renderer*  m_ren = nullptr;
    std::uint64_t m_revision = 0;

    static constexpr std::size_t kMaxCacheEntries = 144;
    static constexpr std::size_t kMaxCacheBytes = 4u * 1024u * 1024u;
    static constexpr std::size_t kGpuHeadroom = 768u * 1024u;

    struct CacheEntry {
        std::string key;
        Texture     tex;
        int         w = 0, h = 0;
    };
    using LruList = std::list<CacheEntry>;
    using LruMap  = std::unordered_map<std::string, LruList::iterator>;

    LruList m_lruList;
    LruMap  m_lruMap;
    std::size_t m_cacheBytes = 0;
    bool m_maintenanceRequested = false;
};

} // namespace nxui
