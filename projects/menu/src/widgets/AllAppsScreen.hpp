#pragma once

#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "launcher/IconStreamer.hpp"

namespace nxui {
class Font;
class GpuDevice;
class Renderer;
class Theme;
}

class GlossyIcon;

// Full-screen wall of every installed title. The grid is independent from the
// HOME layout (folders, widgets, paging), but it mirrors the carousel's motion
// preset, scroll speed, shrink and fade so both views move the same way.
// B backs out to HOME; X opens the focused title's options sheet.
class AllAppsScreen final : public nxui::Widget {
public:
    struct AppItem {
        std::uint64_t titleId = 0;
        std::string title;
        std::string sortKey;
    };

    using IconCb = std::function<void(GlossyIcon*)>;
    using VoidCb = std::function<void()>;

    AllAppsScreen();
    ~AllAppsScreen() override = default;

    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void configure(nxui::GpuDevice* gpu, nxui::Renderer* ren) {
        m_gpu = gpu;
        m_ren = ren;
    }

    void setApps(std::vector<AppItem> apps);
    void setGridShape(int columns, int rows);
    // Called on a worker thread to fetch a title's compressed icon. The host
    // supplies this so the grid can fall back past the SD control cache.
    using IconDataLoader = std::function<std::vector<std::uint8_t>(std::uint64_t)>;
    void setIconDataLoader(IconDataLoader loader) {
        m_iconDataLoader = std::move(loader);
    }
    // Mirrors Settings > Carousel settings.
    void setMovement(int motionStyle, int scrollSpeedPct, int shrinkPct, int fadePct);

    // Y menu ordering/filter. Recent order is most-recent-first title ids.
    void setSortMode(int mode);
    void setFavourites(std::vector<std::uint64_t> ids);
    void setRecentOrder(std::vector<std::uint64_t> ids);
    using SortModeCb = std::function<void(int)>;
    using FavouriteCb = std::function<void(std::uint64_t, bool)>;
    void onSortModeChange(SortModeCb cb) { m_sortModeCb = std::move(cb); }
    void onFavouriteToggle(FavouriteCb cb) { m_favouriteCb = std::move(cb); }

    void show();
    void hide();
    bool isActive() const { return m_active || m_animatingOut; }
    bool isOpen() const { return m_active && !m_animatingOut; }
    // Fully opaque wall: HOME does not need to be drawn underneath it.
    bool isOpaque() const { return isOpen() && m_alpha.value() >= 0.999f; }

    void onLaunch(IconCb cb) { m_launchCb = std::move(cb); }
    void onOptions(IconCb cb) { m_optionsCb = std::move(cb); }
    void onClosed(VoidCb cb) { m_closedCb = std::move(cb); }
    void onNavigateSfx(VoidCb cb) { m_navSfxCb = std::move(cb); }
    void onActivateSfx(VoidCb cb) { m_activateSfxCb = std::move(cb); }
    void onCloseSfx(VoidCb cb) { m_closeSfxCb = std::move(cb); }

    GlossyIcon* focusedIcon() const;
    std::uint64_t focusedTitleId() const;
    // Same radius HOME applies from Rounded/Square + Corner % + UI radius.
    void setIconCornerRadius(float radius);
    void setRestoreFocus(std::uint64_t titleId) { m_lastFocusTitleId = titleId; }

    void update(float dt) override;
    void render(nxui::Renderer& ren) override;

private:
    int perPage() const;
    int pageCount() const;
    void rebuildIcons();
    void buildPage();
    // Icons decode on worker threads; the render thread uploads a small budget
    // of finished textures per frame, so opening the grid or changing pages
    // never waits on image decoding.
    void queuePageTextures();
    void startIconDecodes();
    void pollIconDecodes();
    void setupActions();
    void moveFocus(int dx, int dy);
    void stepPage(int dir);
    void ensurePage(int page, bool animate, int direction);
    nxui::Rect gridArea() const;
    nxui::Rect tileRect(int localIndex, float dx, float* outSize) const;
    float slideOffset() const;
    void drawBackgroundChrome(nxui::Renderer& ren, float alpha);
    void drawForegroundChrome(nxui::Renderer& ren, float alpha);
    void drawMetadata(nxui::Renderer& ren, float alpha);
    void drawEmptyState(nxui::Renderer& ren, float alpha);
    void drawFocusRing(nxui::Renderer& ren, float alpha, float dx);
    void drawSortMenu(nxui::Renderer& ren, float alpha);
    float focusScale() const;
    nxui::Rect focusRingTargetRect() const;
    void syncFocusVisuals(bool immediate);
    void applyOrder(bool preserveFocus);
    void applyIconTheme(GlossyIcon& icon);
    void openMenu();
    void closeMenu();
    void moveMenu(int delta);
    void activateMenuItem();
    void toggleFocusedFavourite();
    bool isFavourite(std::uint64_t titleId) const;
    float slideDuration() const;

    std::vector<AppItem> m_sourceApps;
    std::vector<AppItem> m_apps;
    std::vector<std::uint64_t> m_favourites;
    std::unordered_set<std::uint64_t> m_favouriteSet;
    std::vector<std::uint64_t> m_recentOrder;
    std::vector<std::shared_ptr<GlossyIcon>> m_icons;
    std::vector<std::unique_ptr<nxui::Texture>> m_textures;
    // 0 = none, 1 = queued, 2 = decoding, 3 = loaded, 4 = failed
    std::vector<std::uint8_t> m_texState;
    std::deque<int> m_loadQueue;
    struct PendingDecode {
        int index = -1;
        std::future<IconStreamer::DecodedIcon> future;
    };
    std::vector<PendingDecode> m_pendingDecodes;
    std::vector<int> m_pageIcons;

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;
    nxui::GpuDevice* m_gpu = nullptr;
    nxui::Renderer* m_ren = nullptr;

    int m_cols = 6;
    int m_rows = 3;
    int m_index = 0;
    int m_page = 0;
    int m_motionStyle = 1;
    int m_scrollSpeedPct = 50;
    int m_shrinkPct = 50;
    int m_fadePct = 50;
    int m_slideDir = 1;
    int m_sortMode = 0;
    int m_menuIndex = 0;
    std::uint64_t m_lastFocusTitleId = 0;
    float m_iconCornerRadius = -1.f;

    bool m_active = false;
    bool m_animatingOut = false;
    bool m_dirty = true;
    bool m_menuOpen = false;
    nxui::AnimatedFloat m_alpha{0.f};
    nxui::AnimatedFloat m_slide{1.f};
    std::vector<nxui::AnimatedFloat> m_focusWeights;
    nxui::AnimatedRect m_focusRingRect;
    float m_focusRingTime = 0.f;

    std::string m_pageText;
    int m_pageTextKey = -1;

    IconCb m_launchCb;
    IconCb m_optionsCb;
    IconDataLoader m_iconDataLoader;
    SortModeCb m_sortModeCb;
    FavouriteCb m_favouriteCb;
    VoidCb m_closedCb;
    VoidCb m_navSfxCb;
    VoidCb m_activateSfxCb;
    VoidCb m_closeSfxCb;
};