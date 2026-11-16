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
    // Current carousel row speed in tiles/second, including drag and spring.
    float lineMotionSpeed() const {
        const float drag = std::abs(m_lineDragVelocity);
        const float spring = std::abs(m_lineScrollVel);
        return drag > spring ? drag : spring;
    }
    // Touch-drag the carousel: finger-follow while held, then snap to a title.
    void beginLineDrag();
    void dragLineToTouchDelta(float touchDeltaX, float dt);
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
        m_lineRenderValid = false;
        m_lineFrameValid = false;
    }
    // Re-apply the current page's tile rects after a layout offset changed;
    // a nudge otherwise waits for the next page change to show up.
    void refreshLayout() { positionPage(m_page, 0.f); }
    // Rebuild tile rects and focus navigation after a tile span changed.
    void reflowGridLayout() {
        if (m_layoutMode == AppLayoutMode::Grid)
            layoutPage();
    }
    // Distance shrink, 0..1: 0 keeps every carousel tile the same size, 0.5
    // reproduces the shipped falloff, 1 shrinks the neighbours hardest.
    void setCarouselShrink(float shrink) {
        if (shrink < 0.f) shrink = 0.f;
        if (shrink > 1.f) shrink = 1.f;
        if (std::abs(m_carouselShrink - shrink) < 0.001f) return;
        m_carouselShrink = shrink;
        m_lineLayoutCacheCount = -1;
        m_lineRenderValid = false;
        m_lineFrameValid = false;
    }
    void setLineNudgeY(float px) {
        if (std::abs(m_lineNudgeY - px) < 0.01f) return;
        m_lineNudgeY = px;
        m_lineLayoutCacheCount = -1;
        m_lineRenderValid = false;
        m_lineFrameValid = false;
    }
    // Horizontal offset of the carousel row (focused tile sits left or right
    // of centre). Independent of the grid nudge.
    void setLineNudgeX(float px) {
        if (std::abs(m_lineNudgeX - px) < 0.01f) return;
        m_lineNudgeX = px;
        m_lineLayoutCacheCount = -1;
        m_lineRenderValid = false;
        m_lineFrameValid = false;
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
        m_lineRenderValid = false;
        m_lineFrameValid = false;
    }
    bool linePosterAspect() const { return m_linePosterAspect; }
    void invalidateLineLayout() {
        m_lineLayoutCacheCount = -1;
        m_lineRenderValid = false;
        m_lineFrameValid = false;
        m_lineWidthCount = -1;
    }

    void setPage(int page);
    int  currentPage()  const { return m_page; }
    int  totalPages()   const { return m_totalPages; }
    int  columns()      const { return m_cols; }
    int  rowsPerPage()  const { return m_rows; }
    int  iconsPerPage() const { return m_cols * m_rows; }

    nxui::FocusManager& focusManager() { return m_focus; }
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons() const { return m_allIcons; }

    std::vector<GlossyIcon*> pageIcons() const;
    // Allocation-free lookup for a page-local index (touch highlight runs every
    // frame while a finger is down).
    GlossyIcon* pageIconAt(int index) const;

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
    // Outgoing page while a grid slide is running; otherwise unused.
    int slidePrevPage() const { return m_slidePrevPage; }

    void bumpEdge(int dir);

    // 0 = Crisp, 1 = Smooth, 2 = Springy: scales every tween this grid owns.
    void setMotionStyle(int style) { m_motionStyle = motion::clampStyle(style); }
    // 0 = neighbours stay visible to the screen edge, 1 = they fade out a
    // couple of tiles from the focus. 0.5 is the shipped falloff.
    void setCarouselFade(float fade) {
        m_carouselFade = fade < 0.f ? 0.f : (fade > 1.f ? 1.f : fade);
        m_lineRenderValid = false;
        m_lineFrameValid = false;
    }
    // 0 = half the stock carousel pace, 50 = stock, 100 = 1.5x. Applies to the
    // default glide duration; explicit drag/trigger durations stay explicit.
    void setCarouselScrollSpeed(int percent) {
        percent = std::clamp(percent, 0, 100);
        m_lineScrollSpeed = 0.5f + static_cast<float>(percent) / 100.f;
        if (m_lineScrollOmega > 0.f)
            m_lineScrollOmega = 5.5f / (kLineScrollDuration / m_lineScrollSpeed);
    }

    void setSlideTransition(bool enabled) { m_slideTransition = enabled; }
    // Page slide duration from the Style controls (150..600 ms).
    void setPageSlideMs(int ms) { m_pageSlideMs = std::clamp(ms, 150, 600); }
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
    void stepLineSpring(float dt);
    void stepLineSpringStep(float dt);
    void clearLineEdgeArrival();
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
    float iconAspectAt(int index) const;
    nxui::Rect fitGridRect(int globalIndex, const nxui::Rect& slot) const;
    float pageStride() const;
    float lineItemSpacing() const;
    float lineScaleAt(float absDist, bool poster) const;
    bool linePosterAt(int index) const;
    // Index distance past the focus where the row alpha reaches zero.
    float lineFadeSpan() const;
    int nearestFocusableAtOffset(float offset) const;
    // Cumulative carousel gap between tiles at the current scroll offset, so a
    // tile's position is a prefix difference instead of a walk from the focus.
    const std::vector<float>& lineGapPrefix() const;
    // Width of one carousel tile at the current offset. Many tiles ask for the
    // same neighbour widths while positioning themselves; cache the exact value
    // once per index instead of recomputing the falloff curve each time.
    float lineWidthAt(int index) const;

    // Per-frame carousel geometry. onUpdate fills this while it sets the
    // persistent tile rects; rendering, hit testing and the focus cursor then
    // reuse the same geometry instead of recomputing every visible tile again.
    struct LineFrameTile {
        int index;
        float absD;
        float s;
        float a;
        nxui::Rect rect;
    };
    void rebuildLineFrame(float offset, int focusedIndex, bool fullLayout) const;
    void ensureLineFrame(float offset, int focusedIndex) const;
    mutable std::vector<LineFrameTile> m_lineFrameScratch;
    mutable float m_lineFrameOffset = 0.f;
    mutable int m_lineFrameCount = -1;
    mutable int m_lineFrameFocus = -2;
    mutable nxui::Rect m_lineFrameRect{};
    mutable bool m_lineFrameValid = false;

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
    // Inputs the ordered render list above was built against. While these are
    // unchanged the clear/fill/sort in renderDynamicLine is skipped entirely.
    int m_lineRenderFocus = -2;
    int m_lineRenderCount = -1;
    float m_lineRenderOffset = 0.f;
    float m_lineRenderReach = -1.f;
    nxui::Rect m_lineRenderRect{};
    bool m_lineRenderValid = false;

    // Inputs the carousel layout was last computed against. Used to skip the
    // full-library rect rebuild while the line is at rest.
    int m_lineLayoutCacheCount = -1;
    float m_lineLayoutCacheOffset = 0.f;
    nxui::Rect m_lineLayoutCacheRect{};

    // Inputs the gap prefix was last built against. The prefix is one entry per
    // tile and is rebuilt once per scroll offset, not once per tile.
    mutable std::vector<float> m_lineGapPrefix;
    mutable float m_lineGapPrefixOffset = 0.f;
    mutable float m_lineGapPrefixScale = 0.f;
    mutable float m_lineGapPrefixShrink = 0.f;
    mutable float m_lineGapPrefixSpacing = 0.f;
    mutable bool  m_lineGapPrefixPosters = false;

    // Exact per-index widths for the current offset/scale/poster inputs. A
    // generation stamp makes invalidation O(1) while allowing the vectors to be
    // reused across scroll frames.
    mutable std::vector<float> m_lineWidthCache;
    mutable std::vector<unsigned> m_lineWidthStamp;
    mutable unsigned m_lineWidthGeneration = 1u;
    mutable int m_lineWidthCount = -1;
    mutable float m_lineWidthOffset = 0.f;
    mutable float m_lineWidthScale = 0.f;
    mutable float m_lineWidthShrink = 0.f;
    mutable float m_lineWidthSpacing = 0.f;
    mutable bool m_lineWidthPosters = false;

    // focusedGlobalIndex() is asked for several times per frame (update, render,
    // morph). It is a linear scan of the whole library, so memoize it on the
    // focus pointer and the list shape.
    mutable nxui::Widget* m_focusIndexWidget = nullptr;
    mutable int m_focusIndexCache = -1;
    mutable int m_focusIndexCount = -1;

    std::vector<std::shared_ptr<GlossyIcon>> m_allIcons;
    nxui::FocusManager m_focus;

    AppLayoutMode m_layoutMode = AppLayoutMode::Grid;
    // May sit slightly outside [0, count - 1] while the edge rubber band is
    // pulled or springing back.
    nxui::AnimatedFloat m_lineScrollOffset{0.f};
    // Carousel glide. A retargeted tween restarts its ease on every D-pad step,
    // which reads as a lunge per press; a damped spring keeps the velocity, so a
    // held direction accelerates once and then glides. m_lineScrollOffset holds
    // the integrated position (setImmediate each frame), m_lineScrollGoal the
    // focused index the spring is heading for.
    float m_lineScrollGoal = 0.f;
    float m_lineScrollVel = 0.f;
    // Velocity the current step is allowed to build towards. Assigning a launch
    // velocity directly made every D-pad tap start with a jolt; ramping into it
    // keeps the row responsive without the abrupt acceleration edge.
    float m_lineLaunchVel = 0.f;
    float m_lineScrollSpeed = 1.f;
    float m_lineScrollOmega = 0.f;   // rad/s; 0 derives from kLineScrollDuration
    bool m_lineDragActive = false;
    float m_lineDragStartOffset = 0.f;
    // Scroll-only detail transitions use hysteresis. Recomputing these directly
    // from lineMotionSpeed made captions and shine pop on alternating frames
    // when the spring hovered around a threshold.
    bool m_lineFastDetail = false;
    bool m_lineLowDetailAll = false;
    // Filtered drag speed in tiles/second, sampled from the offset so the
    // release fling is frame-rate independent and follows the actual row.
    float m_lineDragVelocity = 0.f;
    // A fling aimed at an end arms an impact; stepLineSpring releases it when
    // the row reaches that end, so the edge spring receives the arrival speed.
    bool m_lineEdgeArrivalPending = false;
    float m_lineEdgeArrivalVelocity = 0.f;
    // 0 = grid geometry (usual), 1 = carousel geometry (single row)
    nxui::AnimatedFloat m_layoutMorph{0.f};
    bool m_layoutMorphing = false;
    int  m_layoutMorphPage = 0;
    static constexpr float kLayoutMorphDuration = 0.40f;
    static constexpr float kLineIconBase = 150.f;
    nxui::Widget* m_lineUpTarget = nullptr;
    nxui::Widget* m_lineDownTarget = nullptr;
    bool m_linePosterAspect = false;
    float m_gridNudgeX = 0.f;
    float m_lineNudgeY = 0.f;
    float m_lineNudgeX = 0.f;
    float m_iconScale = 1.f;
    float m_carouselShrink = 0.5f;
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
    static constexpr float kLineScrollDuration = 0.40f;

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
    int m_pageSlideMs = 340;
    float slideDuration() const { return static_cast<float>(m_pageSlideMs) / 1000.f; }
    static constexpr float kSwapGlideDuration = 0.36f;

    nxui::AnimatedFloat m_edgeBump;
    bool  m_bumping = false;
    static constexpr float kEdgeBumpDistance = 26.f;

    std::function<void()> m_onPageSwitched;
    std::function<void(int)> m_onEdgePage;
};
