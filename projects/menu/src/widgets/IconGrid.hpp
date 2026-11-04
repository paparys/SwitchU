#pragma once
#include "MotionPreset.hpp"
#include <nxui/widgets/Widget.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <nxui/core/Types.hpp>
#include <nxui/core/Animation.hpp>
#include "core/AppLayoutMode.hpp"
#include <cmath>
#include <vector>
#include <memory>
#include <functional>


class GlossyIcon;

struct IconAppearOptions {
    float baseDelay = 0.f;
    float stagger   = 0.40f;
    bool  fromTile  = false;
    nxui::Rect origin{};
};

class IconGrid : public nxui::Widget {
public:
    IconGrid();

    void setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
               int cols, int rows,
               float cellW, float cellH,
               float padX, float padY);
    void reconfigureLayout(int cols, int rows,
                           float cellW, float cellH,
                           float padX, float padY);

    // animate=false snaps immediately (boot / rebuild). animate=true plays the
    // grid ↔ single-row morph used when the user toggles layout.
    void setLayoutMode(AppLayoutMode mode, bool animate = true);
    AppLayoutMode layoutMode() const { return m_layoutMode; }
    bool isDynamicLine() const { return m_layoutMode == AppLayoutMode::DynamicLine; }
    bool isDynamicLineScrolling() const;
    bool isLineDragActive() const { return m_lineDragActive; }
    // Touch-drag the carousel: finger-follow while held, then snap to a title.
    void beginLineDrag();
    void dragLineToTouchDelta(float touchDeltaX);
    // Returns the focused global index after settle, or -1.
    int endLineDrag();
    int nearestFocusableToLineOffset() const;
    bool isLayoutMorphing() const { return m_layoutMorphing; }
    // After a model rebuild remaps global indices (compact line ↔ padded grid),
    // re-anchor morph page + line scroll to the focused title so the cursor and
    // tiles blend into the correct grid cells.
    void rebindLayoutMorphAnchor();
    void setDynamicLineUpTarget(nxui::Widget* target);
    void setDynamicLineDownTarget(nxui::Widget* target);
    // When true, single-row may use per-tile Poster frames (apps with poster.img).
    // Tile aspect for the single-row carousel (1 = square, 16:9, 4:3).
    // Posters keep their 2:3 frame; this only shapes plain icon tiles.
    void setIconAspectRatio(float ratio) {
        if (ratio <= 0.f) return;
        if (std::abs(m_iconAspect - ratio) < 0.001f) return;
        m_iconAspect = ratio;
        m_lineLayoutCacheCount = -1;
    }
    float iconAspectRatio() const { return m_iconAspect; }
    // Horizontal nudge for the grid and vertical nudge for the carousel row.
    // Both are pure layout offsets; hit tests follow the drawn rects.
    void setGridNudgeX(float px) {
        if (std::abs(m_gridNudgeX - px) < 0.01f) return;
        m_gridNudgeX = px;
    }
    // Uniform tile scale for both views (1 = the built-in size). Spacing and
    // hit rects follow, since every rect derives from the same base cell.
    void setIconScale(float scale) {
        if (scale < 0.5f) scale = 0.5f;
        if (scale > 1.6f) scale = 1.6f;
        if (std::abs(m_iconScale - scale) < 0.001f) return;
        m_iconScale = scale;
        m_lineLayoutCacheCount = -1;
    }
    // Re-apply the current page's tile rects after a layout offset changed;
    // a nudge otherwise waits for the next page change to show up.
    void refreshLayout() { positionPage(m_page, 0.f); }
    // Carousel depth: -1 pulls the row back (smaller tiles, tighter pitch, so
    // more of them fit on screen), +1 brings it forward.
    void setCarouselDepth(float depth) {
        if (depth < -1.f) depth = -1.f;
        if (depth > 1.f) depth = 1.f;
        if (std::abs(m_carouselDepth - depth) < 0.001f) return;
        m_carouselDepth = depth;
        m_lineLayoutCacheCount = -1;
    }
    void setLineNudgeY(float px) {
        if (std::abs(m_lineNudgeY - px) < 0.01f) return;
        m_lineNudgeY = px;
        m_lineLayoutCacheCount = -1;
    }
    // Horizontal offset of the carousel row (focused tile sits left or right
    // of centre). Independent of the grid nudge.
    void setLineNudgeX(float px) {
        if (std::abs(m_lineNudgeX - px) < 0.01f) return;
        m_lineNudgeX = px;
        m_lineLayoutCacheCount = -1;
    }

    // Space reserved at the top/bottom of this widget for HOME chrome (the
    // shortcut strip). Tiles and the carousel centre inside what is left, so
    // moving the shortcuts to the bottom never parks them on the tiles.
    void setVerticalInsets(float top, float bottom);
    // Rect the tiles actually occupy, chrome bands excluded. The shortcut
    // stacks read this to know where the grid begins.
    nxui::Rect tileBand() const { return contentRect(); }
    void setLinePosterAspect(bool enabled) {
        if (m_linePosterAspect == enabled) return;
        m_linePosterAspect = enabled;
        m_lineLayoutCacheCount = -1;
    }
    bool linePosterAspect() const { return m_linePosterAspect; }
    void invalidateLineLayout() { m_lineLayoutCacheCount = -1; }

    void setPage(int page);
    int  currentPage()  const { return m_page; }
    int  totalPages()   const { return m_totalPages; }
    int  columns()      const { return m_cols; }
    int  rowsPerPage()  const { return m_rows; }
    int  iconsPerPage() const { return m_cols * m_rows; }

    nxui::FocusManager& focusManager() { return m_focus; }
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons() const { return m_allIcons; }

    std::vector<GlossyIcon*> pageIcons() const;

    int hitTest(float screenX, float screenY) const;
    nxui::Rect focusedDisplayRect() const;
    nxui::Rect gridSpanRect(int globalIndex, int columns, int rows) const;
    // Bounds of the laid-out cell block (not the whole widget rect).
    nxui::Rect contentRect() const;
    void setGridSideTargets(std::vector<nxui::Widget*> left,
                            std::vector<nxui::Widget*> right);

    int focusedGlobalIndex() const;
    // durationSec < 0 uses the default line scroll; ignored when instant.
    bool focusGlobalIndex(int idx, bool instant = false, float durationSec = -1.f);
    bool swapSlots(int a, int b);

    void startAppearAnimation(const IconAppearOptions& opt = IconAppearOptions{});
    // Mirror of a fromTile appear: icons fly back into opt.origin, last in, first out.
    void startDisappearAnimation(const IconAppearOptions& opt, float dur);

    // Move-mode swap effect. Glides the tile that a placement displaced from
    // (displacedPrevious) into the slot it ended up in (displacedCurrent).
    // Tiles that stay on the visible page glide across the gap; one arriving
    // from another page slides in from the side that page lies on. Call after
    // the model rebuild, since the rebuild force-visibles every icon. The tile
    // the player was holding is deliberately left alone: it is already drawn
    // where it was dropped, under the edit ghost.
    void animateDisplacedTile(int displacedPrevious, int displacedCurrent);

    void startPageTransition(int targetPage);
    bool isTransitioning() const { return m_sliding; }

    void bumpEdge(int dir);

    // 0 = Crisp, 1 = Smooth, 2 = Springy: scales every tween this grid owns.
    void setMotionStyle(int style) { m_motionStyle = motion::clampStyle(style); }
    // 0 = neighbours stay visible to the screen edge, 1 = they fade out a
    // couple of tiles from the focus. 0.5 is the shipped falloff.
    void setCarouselFade(float fade) {
        m_carouselFade = fade < 0.f ? 0.f : (fade > 1.f ? 1.f : fade);
    }

    void setSlideTransition(bool enabled) { m_slideTransition = enabled; }
    void setEdgePaging(bool enabled) { m_edgePaging = enabled; }
    void onEdgePage(std::function<void(int dir)> cb) { m_onEdgePage = std::move(cb); }

    void onPageSwitched(std::function<void()> cb) { m_onPageSwitched = std::move(cb); }

    void render(nxui::Renderer& ren) override;

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    void layoutPage();
    void layoutLine();
    void positionPage(int page, float dx);
    void renderPageAt(nxui::Renderer& ren, int page, float dx);
    void renderDynamicLine(nxui::Renderer& ren);
    void renderLayoutMorph(nxui::Renderer& ren);
    nxui::Rect gridSlotRect(int globalIndex) const;
    nxui::Rect morphBlend(int globalIndex, const nxui::Rect& gridRect) const;
    void bindEdgeActions(int start, int end);
    void bindGridNavigation(int start, int end);
    nxui::Rect dynamicIconRect(int index, float* outScale = nullptr,
                               float* outOpacity = nullptr,
                               float* outDistance = nullptr) const;
    float pageStride() const;
    float lineItemSpacing() const;

    // Carousel render scratch. Reused across frames so drawing the line does
    // not allocate every frame, and each survivor keeps the rect that was
    // already computed for it instead of recomputing an identical one.
    struct RenderCandidate {
        int index;
        float absD;
        float d;
        float s;
        float a;
        nxui::Rect rect;
    };
    mutable std::vector<RenderCandidate> m_lineRenderScratch;

    // Inputs the carousel layout was last computed against. Used to skip the
    // full-library rect rebuild while the line is at rest.
    int m_lineLayoutCacheCount = -1;
    float m_lineLayoutCacheOffset = 0.f;
    nxui::Rect m_lineLayoutCacheRect{};

    std::vector<std::shared_ptr<GlossyIcon>> m_allIcons;
    nxui::FocusManager m_focus;

    AppLayoutMode m_layoutMode = AppLayoutMode::Grid;
    nxui::AnimatedFloat m_lineScrollOffset{0.f};
    bool m_lineDragActive = false;
    float m_lineDragStartOffset = 0.f;
    // 0 = grid geometry (usual), 1 = carousel geometry (single row)
    nxui::AnimatedFloat m_layoutMorph{0.f};
    bool m_layoutMorphing = false;
    int  m_layoutMorphPage = 0;
    static constexpr float kLayoutMorphDuration = 0.40f;
    static constexpr float kLineIconBase = 150.f;
    nxui::Widget* m_lineUpTarget = nullptr;
    nxui::Widget* m_lineDownTarget = nullptr;
    bool m_linePosterAspect = false;
    float m_iconAspect = 1.f;
    float m_gridNudgeX = 0.f;
    float m_lineNudgeY = 0.f;
    float m_lineNudgeX = 0.f;
    float m_iconScale = 1.f;
    float m_carouselDepth = 0.f;
    int m_motionStyle = 1;
    float m_carouselFade = 0.5f;
    float m_insetTop = 0.f;
    float m_insetBottom = 0.f;
    std::vector<nxui::Widget*> m_gridLeftTargets;
    std::vector<nxui::Widget*> m_gridRightTargets;

    int m_cols = 5, m_rows = 3;
    int m_page = 0, m_totalPages = 1;
    float m_cellW = 200, m_cellH = 200;
    float m_padX  = 20,  m_padY  = 20;
    float m_originX = 0, m_originY = 0;
    static constexpr float kLineScrollDuration = 0.34f;

    // Slide is what the grid starts with: a build path that forgets to apply
    // the user's Page Transition setting should fall back to the usual slide,
    // never to the teleport cascade.
    bool  m_slideTransition = true;
    bool  m_edgePaging      = false;
    bool  m_sliding         = false;
    int   m_slidePrevPage   = 0;
    int   m_slideDir        = 1;
    float m_slideT          = 0.f;
    float m_slideInDx       = 0.f;
    float m_slideOutDx      = 0.f;
    static constexpr float kSlideDuration = 0.34f;
    static constexpr float kSwapGlideDuration = 0.36f;

    nxui::AnimatedFloat m_edgeBump;
    bool  m_bumping = false;
    static constexpr float kEdgeBumpDistance = 26.f;

    std::function<void()> m_onPageSwitched;
    std::function<void(int)> m_onEdgePage;
};
