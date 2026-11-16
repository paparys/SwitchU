#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include "GridNavigation.hpp"
#include "core/FrameProfiler.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Input.hpp>
#include <algorithm>
#include <cmath>
#include <limits>


IconGrid::IconGrid() {}

namespace {

float clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

// Carousel poster width. Halfway between the large (300@1.18) and clock-safe
// (268@1.06) sizes; the icon variant uses IconGrid::kLineIconBase instead.
constexpr float kPosterBaseW = 284.f;

// Visual overtravel at either end of the carousel, in tiles. The resistance
// below approaches this bound asymptotically, so a hard pull exposes a small
// gap instead of tearing the row off the edge.
constexpr float kLineEdgeOverscroll = 0.26f;

// One edge-depth cue for both the carousel rubber band and grid page bumps.
// The shadow is derived from the live offset, so it appears and fades with the
// motion instead of needing a second animation clock.
constexpr float kEdgeShadowWidth = 104.f;
constexpr float kEdgeShadowAlpha = 0.22f;

void drawEdgeShadow(nxui::Renderer& ren, const nxui::Rect& clip,
                    float leftOver, float rightOver) {
    auto amount = [](float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    const nxui::Color edgeShadow(0.012f, 0.022f, 0.050f, 1.f);
    if (leftOver > 0.001f) {
        const float a = kEdgeShadowAlpha * amount(leftOver);
        ren.drawGradientRectH(
            {clip.x, clip.y, kEdgeShadowWidth, clip.height},
            edgeShadow.withAlpha(a), edgeShadow.withAlpha(0.f));
    }
    if (rightOver > 0.001f) {
        const float a = kEdgeShadowAlpha * amount(rightOver);
        ren.drawGradientRectH(
            {clip.right() - kEdgeShadowWidth, clip.y,
             kEdgeShadowWidth, clip.height},
            edgeShadow.withAlpha(0.f), edgeShadow.withAlpha(a));
    }
}

float resistLineEdge(float rawOffset, float maxIdx) {
    if (rawOffset < 0.f) {
        const float over = -rawOffset;
        return -kLineEdgeOverscroll
             * (1.f - std::exp(-over / kLineEdgeOverscroll));
    }
    if (rawOffset > maxIdx) {
        const float over = rawOffset - maxIdx;
        return maxIdx + kLineEdgeOverscroll
             * (1.f - std::exp(-over / kLineEdgeOverscroll));
    }
    return rawOffset;
}

nxui::Rect fitAspectRect(const nxui::Rect& slot, float aspect) {
    if (slot.width <= 0.f || slot.height <= 0.f
        || !(aspect > 0.01f) || std::abs(aspect - 1.f) < 0.001f)
        return slot;
    float w = slot.width;
    float h = w / aspect;
    if (h > slot.height) {
        h = slot.height;
        w = h * aspect;
    }
    return {slot.x + (slot.width - w) * 0.5f,
            slot.y + (slot.height - h) * 0.5f, w, h};
}

} // namespace

float IconGrid::iconAspectAt(int index) const {
    if (index < 0 || index >= static_cast<int>(m_allIcons.size()))
        return 1.f;
    const auto& icon = m_allIcons[static_cast<std::size_t>(index)];
    return icon ? std::clamp(icon->aspectRatio(), 0.2f, 4.f) : 1.f;
}

nxui::Rect IconGrid::fitGridRect(int globalIndex, const nxui::Rect& slot) const {
    if (globalIndex < 0 || globalIndex >= static_cast<int>(m_allIcons.size()))
        return slot;
    const auto& icon = m_allIcons[static_cast<std::size_t>(globalIndex)];
    if (!icon || icon->gridSpanColumns() != 1 || icon->gridSpanRows() != 1)
        return slot;
    return fitAspectRect(slot, iconAspectAt(globalIndex));
}

void IconGrid::setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
                     int cols, int rows,
                     float cellW, float cellH,
                     float padX, float padY)
{
    m_allIcons = std::move(icons);
    reconfigureLayout(cols, rows, cellW, cellH, padX, padY);
}

void IconGrid::setLayoutMode(AppLayoutMode mode, bool animate) {
    clearLineEdgeArrival();
    const bool toLine = mode == AppLayoutMode::DynamicLine;
    if (m_layoutMode == mode) {
        if (!animate) {
            m_layoutMorph.setImmediate(toLine ? 1.f : 0.f);
            m_layoutMorphing = false;
        }
        return;
    }
    int cur = focusedGlobalIndex();
    const int perPage = std::max(1, iconsPerPage());
    m_layoutMorphPage = (mode == AppLayoutMode::DynamicLine)
        ? m_page
        : (cur >= 0 ? cur / perPage : m_page);
    m_layoutMode = mode;
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        m_lineScrollGoal = cur >= 0 ? static_cast<float>(cur) : 0.f;
        m_lineScrollVel = 0.f;
    m_lineLaunchVel = 0.f;
        m_lineScrollOffset.setImmediate(m_lineScrollGoal);
        layoutLine();
    } else {
        setPage(cur >= 0 ? cur / std::max(1, iconsPerPage()) : m_page);
        layoutPage();
    }
    if (cur >= 0 && cur < (int)m_allIcons.size() && m_allIcons[cur]->isFocusable()) {
        m_focus.setFocus(m_allIcons[cur].get());
    }

    if (!animate) {
        m_layoutMorph.setImmediate(toLine ? 1.f : 0.f);
        m_layoutMorphing = false;
        return;
    }

    m_layoutMorphing = true;
    m_layoutMorph.setImmediate(toLine ? 0.f : 1.f);
    m_layoutMorph.set(toLine ? 1.f : 0.f, motion::scaled(kLayoutMorphDuration, m_motionStyle),
                      motion::inOutEase(m_motionStyle));
}

void IconGrid::rebindLayoutMorphAnchor() {
    if (!m_layoutMorphing)
        return;
    clearLineEdgeArrival();
    const int cur = focusedGlobalIndex();
    const int perPage = std::max(1, iconsPerPage());
    m_layoutMorphPage = cur >= 0 ? cur / perPage : m_page;
    if (cur >= 0) {
        m_lineScrollGoal = static_cast<float>(cur);
        m_lineScrollVel = 0.f;
    m_lineLaunchVel = 0.f;
        m_lineScrollOffset.setImmediate(m_lineScrollGoal);
    }
    if (m_layoutMode == AppLayoutMode::Grid && cur >= 0) {
        const int wanted = cur / perPage;
        if (wanted != m_page)
            setPage(wanted);
    }
}

bool IconGrid::isDynamicLineScrolling() const {
    return m_layoutMode == AppLayoutMode::DynamicLine
        && (m_lineDragActive
            || m_lineEdgeArrivalPending
            || std::abs(m_lineScrollVel) > 0.01f
            || std::abs(m_lineScrollGoal - m_lineScrollOffset.value()) > 0.01f);
}

float IconGrid::lineItemSpacing() const {
    // Base pitch only spaces the drag and gap maths; the per-tile widths carry
    // the distance shrink, so every adjacent pair keeps the same gap.
    return kLineIconBase * m_iconScale + std::max(8.f, m_padX * 0.4f);
}

void IconGrid::clearLineEdgeArrival() {
    m_lineEdgeArrivalPending = false;
    m_lineEdgeArrivalVelocity = 0.f;
}

void IconGrid::beginLineDrag() {
    clearLineEdgeArrival();
    if (m_layoutMode != AppLayoutMode::DynamicLine || m_layoutMorphing)
        return;
    m_lineDragActive = true;
    m_lineDragStartOffset = m_lineScrollOffset.value();
    m_lineScrollGoal = m_lineDragStartOffset;
    m_lineScrollVel = 0.f;
    m_lineLaunchVel = 0.f;
    m_lineDragVelocity = 0.f;
    m_lineScrollOffset.setImmediate(m_lineDragStartOffset);
}

void IconGrid::dragLineToTouchDelta(float touchDeltaX, float dt) {
    if (!m_lineDragActive || m_layoutMode != AppLayoutMode::DynamicLine)
        return;
    const float spacing = std::max(1.f, lineItemSpacing());
    // Finger right → earlier titles (offset decreases); left → later titles.
    const float maxIdx = std::max(0.f, static_cast<float>(m_allIcons.size()) - 1.f);
    const float previous = m_lineScrollOffset.value();
    // Past either end the finger keeps moving, but the row only gives a little.
    // The resisted offset is also what velocity is sampled from, so a fling
    // into the stop cannot build up an artificial full-speed release.
    const float rawOffset = m_lineDragStartOffset - touchDeltaX / spacing;
    const float offset = resistLineEdge(rawOffset, maxIdx);

    if (dt > 0.f) {
        // Offset change per second is the carousel's actual speed; sampling the
        // finger's cumulative delta avoids a second, differently scaled clock.
        const float instant = std::clamp((offset - previous) / dt, -20.f, 20.f);
        if (m_lineDragVelocity == 0.f) {
            m_lineDragVelocity = instant;
        } else {
            const float blend = 1.f - std::exp(-dt / 0.055f);
            m_lineDragVelocity += (instant - m_lineDragVelocity) * blend;
        }
    } else {
        m_lineDragVelocity = 0.f;
    }

    m_lineScrollGoal = offset;
    m_lineScrollVel = 0.f;
    m_lineLaunchVel = 0.f;
    m_lineScrollOffset.setImmediate(offset);
    m_lineLayoutCacheCount = -1;
    m_lineFrameValid = false;
}

int IconGrid::nearestFocusableToLineOffset() const {
    return nearestFocusableAtOffset(m_lineScrollOffset.value());
}

int IconGrid::nearestFocusableAtOffset(float offset) const {
    if (m_allIcons.empty())
        return -1;
    const int prefer = std::clamp(static_cast<int>(std::lround(offset)),
                                  0, static_cast<int>(m_allIcons.size()) - 1);
    if (m_allIcons[static_cast<std::size_t>(prefer)]
        && m_allIcons[static_cast<std::size_t>(prefer)]->isFocusable())
        return prefer;
    for (int dist = 1; dist < static_cast<int>(m_allIcons.size()); ++dist) {
        const int right = prefer + dist;
        if (right < static_cast<int>(m_allIcons.size())
            && m_allIcons[static_cast<std::size_t>(right)]
            && m_allIcons[static_cast<std::size_t>(right)]->isFocusable())
            return right;
        const int left = prefer - dist;
        if (left >= 0
            && m_allIcons[static_cast<std::size_t>(left)]
            && m_allIcons[static_cast<std::size_t>(left)]->isFocusable())
            return left;
    }
    return -1;
}

int IconGrid::endLineDrag() {
    if (!m_lineDragActive)
        return focusedGlobalIndex();
    m_lineDragActive = false;

    constexpr float kFlingMinVelocity = 2.0f;    // tiles/s
    constexpr float kFlingMaxVelocity = 10.0f;   // tiles/s
    constexpr float kFlingLookahead = 0.18f;     // seconds
    constexpr float kFlingSettleDuration = 0.30f;
    constexpr float kDragSettleDuration = 0.22f;
    constexpr float kEdgeBounceMinVelocity = 1.2f;
    constexpr float kEdgeBounceMaxVelocity = 6.0f;

    const float releaseVelocity = std::clamp(m_lineDragVelocity,
                                             -kFlingMaxVelocity, kFlingMaxVelocity);
    m_lineDragVelocity = 0.f;
    m_lineScrollVel = 0.f;
    m_lineLaunchVel = 0.f;

    const float offset = m_lineScrollOffset.value();
    const float maxIdx = std::max(0.f, static_cast<float>(m_allIcons.size()) - 1.f);
    const int nearest = nearestFocusableAtOffset(offset);
    if (nearest < 0)
        return -1;

    const bool atLowEdge = offset <= 0.0001f;
    const bool atHighEdge = offset >= maxIdx - 0.0001f;
    const bool pushPastLow = atLowEdge && releaseVelocity < 0.f;
    const bool pushPastHigh = atHighEdge && releaseVelocity > 0.f;
    if (pushPastLow || pushPastHigh) {
        const float bounceSpeed = std::min(std::abs(releaseVelocity),
                                           kEdgeBounceMaxVelocity);
        if (bounceSpeed >= kEdgeBounceMinVelocity) {
            // Keep the edge tile focused, but carry the release a little past
            // the end. Clearing the launch assist afterwards matters: its step
            // kick points back into the row and would cancel the bounce.
            if (!focusGlobalIndex(nearest, false, kFlingSettleDuration))
                return -1;
            m_lineLaunchVel = 0.f;
            m_lineScrollVel = pushPastLow ? -bounceSpeed : bounceSpeed;
            return nearest;
        }
    }

    const bool flung = std::abs(releaseVelocity) >= kFlingMinVelocity;
    int target = nearest;
    if (flung) {
        // Project the release a short way, then snap to the focusable tile
        // nearest that landing point. The cap keeps a hard flick to roughly
        // two tiles instead of throwing the row across the library.
        const float projected = std::clamp(
            offset + releaseVelocity * kFlingLookahead, 0.f, maxIdx);
        const int projectedTarget = nearestFocusableAtOffset(projected);
        if (projectedTarget >= 0)
            target = projectedTarget;
    }

    bool armArrival = false;
    float arrivalVelocity = 0.f;
    if (flung) {
        const bool lowTarget = target == 0;
        const bool highTarget = target == static_cast<int>(maxIdx);
        const bool towardLow = lowTarget && releaseVelocity < 0.f;
        const bool towardHigh = highTarget && releaseVelocity > 0.f;
        if (towardLow || towardHigh) {
            const bool low = towardLow;
            const float edge = low ? 0.f : maxIdx;
            const float distance = std::abs(offset - edge);
            // Preserve a useful part of the release speed even when the spring
            // has to travel a while; a close edge gets the full rebound.
            const float attenuation = std::clamp(1.f - distance * 0.35f,
                                                 0.35f, 1.f);
            const float speed = std::min(std::abs(releaseVelocity),
                                         kEdgeBounceMaxVelocity) * attenuation;
            if (distance > 0.00001f && speed >= kEdgeBounceMinVelocity) {
                armArrival = true;
                arrivalVelocity = low ? -speed : speed;
            }
        }
    }

    const float delta = static_cast<float>(target) - offset;
    float settleVelocity = releaseVelocity;
    if (std::abs(delta) < 0.0001f || settleVelocity * delta <= 0.f)
        settleVelocity = 0.f;

    // A close target gets a proportionally smaller carry, so the snap lands
    // cleanly instead of overshooting the focused tile.
    const float maxTowardVelocity = std::max(1.5f, std::abs(delta) * 12.f);
    settleVelocity = std::clamp(settleVelocity,
                                -maxTowardVelocity, maxTowardVelocity);
    m_lineScrollVel = settleVelocity;

    const float duration = (flung && settleVelocity != 0.f)
        ? kFlingSettleDuration : kDragSettleDuration;
    if (!focusGlobalIndex(target, false, duration))
        return -1;
    if (armArrival) {
        m_lineEdgeArrivalPending = true;
        m_lineEdgeArrivalVelocity = arrivalVelocity;
    }
    return target;
}

void IconGrid::setDynamicLineUpTarget(nxui::Widget* target) {
    m_lineUpTarget = target;
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        for (auto& icon : m_allIcons) {
            if (icon)
                icon->setCustomNavigation(nxui::FocusDirection::UP, m_lineUpTarget);
        }
        return;
    }
    // Grid: top-row UP falls back to the chrome hub (Change View) via layoutPage.
    if (m_layoutMode == AppLayoutMode::Grid && !m_allIcons.empty())
        layoutPage();
}

void IconGrid::setDynamicLineDownTarget(nxui::Widget* target) {
    m_lineDownTarget = target;
    if (m_layoutMode != AppLayoutMode::DynamicLine)
        return;
    for (auto& icon : m_allIcons) {
        if (icon)
            icon->setCustomNavigation(nxui::FocusDirection::DOWN, m_lineDownTarget);
    }
}

void IconGrid::reconfigureLayout(int cols, int rows,
                                 float cellW, float cellH,
                                 float padX, float padY)
{
    m_cols  = cols;  m_rows = rows;
    m_cellW = cellW; m_cellH = cellH;
    m_padX  = padX;  m_padY  = padY;

    int perPage = iconsPerPage();
    m_totalPages = std::max(1, ((int)m_allIcons.size() + perPage - 1) / perPage);

    // A folder rebuild replaces a long HOME row with a much shorter list. The
    // carousel offset and target belong to the old tree; clamp both before
    // layout so every geometry lookup lands on the new list.
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        const float maxOffset = std::max(0.f, static_cast<float>(m_allIcons.size()) - 1.f);
        const float offset = std::clamp(m_lineScrollOffset.value(), 0.f, maxOffset);
        m_lineScrollOffset.setImmediate(offset);
        m_lineScrollGoal = std::clamp(m_lineScrollGoal, 0.f, maxOffset);
        m_lineScrollVel = 0.f;
        m_lineLaunchVel = 0.f;
    }

    float gridW = m_cols * m_cellW + (m_cols - 1) * m_padX;
    float gridH = m_rows * m_cellH + (m_rows - 1) * m_padY;
    m_originX = (m_rect.width  - gridW) * 0.5f + m_rect.x;
    const float availH = std::max(0.f, m_rect.height - m_insetTop - m_insetBottom);
    m_originY = m_rect.y + m_insetTop + (availH - gridH) * 0.5f;

    if (m_layoutMode == AppLayoutMode::DynamicLine)
        layoutLine();
    else
        setPage(m_page);
}

nxui::Rect IconGrid::contentRect() const {
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        nxui::Rect line = m_rect.shrunk(40.f);
        line.y += m_insetTop;
        line.height = std::max(1.f, line.height - m_insetTop - m_insetBottom);
        return line;
    }
    const float gridW = m_cols * m_cellW + (m_cols - 1) * m_padX;
    const float gridH = m_rows * m_cellH + (m_rows - 1) * m_padY;
    return {m_originX + m_gridNudgeX, m_originY, gridW, gridH};
}

void IconGrid::setPage(int page) {
    m_page = std::clamp(page, 0, m_totalPages - 1);
    layoutPage();
}

void IconGrid::layoutPage() {
    nxui::Widget* prevFocused = m_focus.current();

    clearChildren();
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());

    std::vector<nxui::Widget*> fItems;

    for (int i = start; i < end; ++i) {
        auto& icon = m_allIcons[i];
        if (!icon)
            continue;
        icon->setCustomNavigation(nxui::FocusDirection::LEFT, nullptr);
        icon->setCustomNavigation(nxui::FocusDirection::RIGHT, nullptr);
        icon->setCustomNavigation(nxui::FocusDirection::UP, nullptr);
        icon->setCustomNavigation(nxui::FocusDirection::DOWN, m_lineDownTarget);
        int local  = i - start;
        int col    = local % m_cols;
        int row    = local / m_cols;
        float x = m_originX + col * (m_cellW + m_padX);
        float y = m_originY + row * (m_cellH + m_padY);
        const int spanColumns = std::max(1, icon->gridSpanColumns());
        const int spanRows = std::max(1, icon->gridSpanRows());
        const nxui::Rect slot{x, y,
                              m_cellW * spanColumns + m_padX * (spanColumns - 1),
                              m_cellH * spanRows + m_padY * (spanRows - 1)};
        icon->setRect(fitGridRect(i, slot));
        addChild(icon);
        if (icon->isFocusable())
            fItems.push_back(icon.get());
    }

    bindGridNavigation(start, end);
    bindEdgeActions(start, end);

    m_focus.setGrid(fItems, m_cols);
    if (prevFocused) {
        for (auto* item : fItems) {
            if (item == prevFocused) {
                m_focus.setFocus(prevFocused);
                break;
            }
        }
    }
}

void IconGrid::bindGridNavigation(int start, int end) {
    std::vector<GridNavigationItem> items;
    items.reserve(static_cast<std::size_t>(std::max(0, end - start)));
    for (int index = start; index < end; ++index) {
        const auto& icon = m_allIcons[static_cast<std::size_t>(index)];
        if (!icon || !icon->isFocusable() || !icon->isVisible()) continue;
        const int local = index - start;
        items.push_back({index, local % m_cols, local / m_cols,
                         std::max(1, icon->gridSpanColumns()),
                         std::max(1, icon->gridSpanRows())});
    }

    const auto nearestSideTarget = [](const GlossyIcon& source,
                                      const std::vector<nxui::Widget*>& targets) {
        nxui::Widget* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();
        const float sourceY = source.focusRect().y + source.focusRect().height * 0.5f;
        for (auto* target : targets) {
            if (!target || !target->isVisible() || !target->isFocusable()) continue;
            const auto rect = target->focusRect();
            const float distance = std::abs(rect.y + rect.height * 0.5f - sourceY);
            if (distance < bestDistance) {
                best = target;
                bestDistance = distance;
            }
        }
        return best;
    };
    const auto bind = [&](GlossyIcon& source, const GridNavigationItem& item,
                          nxui::FocusDirection focusDirection,
                          GridNavigationDirection gridDirection) {
        const int target = findGridNavigationTarget(items, item.index, gridDirection);
        nxui::Widget* destination = target >= 0
            ? m_allIcons[static_cast<std::size_t>(target)].get() : nullptr;
        if (!destination && gridDirection == GridNavigationDirection::Left &&
            item.column == 0)
            destination = nearestSideTarget(source, m_gridLeftTargets);
        if (!destination && gridDirection == GridNavigationDirection::Right &&
            item.column + std::max(1, item.columns) >= m_cols)
            destination = nearestSideTarget(source, m_gridRightTargets);
        // Top of the page: land on Change View (chrome hub) instead of drifting
        // to the user tile via spatial search.
        if (!destination && gridDirection == GridNavigationDirection::Up)
            destination = m_lineUpTarget;
        source.setCustomNavigation(focusDirection,
                                   destination);
    };
    for (const auto& item : items) {
        auto& source = *m_allIcons[static_cast<std::size_t>(item.index)];
        bind(source, item, nxui::FocusDirection::LEFT,
             GridNavigationDirection::Left);
        bind(source, item, nxui::FocusDirection::RIGHT,
             GridNavigationDirection::Right);
        bind(source, item, nxui::FocusDirection::UP,
             GridNavigationDirection::Up);
        bind(source, item, nxui::FocusDirection::DOWN,
             GridNavigationDirection::Down);
    }
}

void IconGrid::setGridSideTargets(std::vector<nxui::Widget*> left,
                                  std::vector<nxui::Widget*> right) {
    m_gridLeftTargets = std::move(left);
    m_gridRightTargets = std::move(right);
    if (m_layoutMode == AppLayoutMode::Grid)
        layoutPage();
}

nxui::Rect IconGrid::gridSpanRect(int globalIndex, int columns, int rows) const {
    if (globalIndex < 0 || globalIndex >= static_cast<int>(m_allIcons.size()))
        return {};
    // The single-row carousel has its own fixed metrics and animation. Edit
    // ghosts/cursors must follow that displayed rect instead of reconstructing
    // a cell from the configurable grid dimensions.
    const int local = globalIndex % std::max(1, iconsPerPage());
    const int column = local % std::max(1, m_cols);
    const int row = local / std::max(1, m_cols);
    const int spanColumns = std::max(1, columns);
    const int spanRows = std::max(1, rows);
    const nxui::Rect slot{m_originX + m_gridNudgeX + column * (m_cellW + m_padX),
                          m_originY + row * (m_cellH + m_padY),
                          m_cellW * spanColumns + m_padX * (spanColumns - 1),
                          m_cellH * spanRows + m_padY * (spanRows - 1)};
    const nxui::Rect gridRect = (spanColumns == 1 && spanRows == 1)
        ? fitAspectRect(slot, iconAspectAt(globalIndex))
        : slot;
    // still travelling
    if (m_layoutMorphing)
        return morphBlend(globalIndex, gridRect);
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        ensureLineFrame(m_lineScrollOffset.value(), focusedGlobalIndex());
        for (const auto& tile : m_lineFrameScratch)
            if (tile.index == globalIndex)
                return tile.rect;
        return dynamicIconRect(globalIndex);
    }
    return gridRect;
}

void IconGrid::layoutLine() {
    clearLineEdgeArrival();
    nxui::Widget* prevFocused = m_focus.current();
    clearChildren();
    // Rebuilding the child list also changes every fallback tile rect and may
    // switch individual icons between square and poster widths.
    m_lineLayoutCacheCount = -1;
    m_lineRenderValid = false;
    m_lineFrameValid = false;
    m_lineWidthCount = -1;

    // Grid folder edge-paging registers DLeft/DRight actions on column edges.
    // In-place Grid→line morph reuses those icons; if the actions stick around
    // they fire before custom nav and dead-end RIGHT on the old last column
    // (e.g. title 5 in a 5×N folder).
    auto clearEdgePagingActions = [](nxui::Widget& w) {
        w.removeAction(static_cast<uint64_t>(nxui::Button::DLeft));
        w.removeAction(static_cast<uint64_t>(nxui::Button::DRight));
        w.removeAction(static_cast<uint64_t>(nxui::Button::LStickL));
        w.removeAction(static_cast<uint64_t>(nxui::Button::LStickR));
        w.removeAction(static_cast<uint64_t>(nxui::Button::RStickL));
        w.removeAction(static_cast<uint64_t>(nxui::Button::RStickR));
    };

    // Nearest focusable neighbour on each side, computed in two linear passes.
    // The previous per-icon backwards/forwards scan was quadratic in the
    // installed title count on every layout rebuild.
    const int iconCount = (int)m_allIcons.size();
    std::vector<int> prevFocusable((size_t)iconCount, -1);
    std::vector<int> nextFocusable((size_t)iconCount, -1);
    int lastFocusable = -1;
    for (int i = 0; i < iconCount; ++i) {
        prevFocusable[(size_t)i] = lastFocusable;
        if (m_allIcons[(size_t)i] && m_allIcons[(size_t)i]->isFocusable())
            lastFocusable = i;
    }
    lastFocusable = -1;
    for (int i = iconCount - 1; i >= 0; --i) {
        nextFocusable[(size_t)i] = lastFocusable;
        if (m_allIcons[(size_t)i] && m_allIcons[(size_t)i]->isFocusable())
            lastFocusable = i;
    }

    std::vector<nxui::Widget*> fItems;
    fItems.reserve(m_allIcons.size());
    for (size_t i = 0; i < m_allIcons.size(); ++i) {
        auto& icon = m_allIcons[i];
        if (!icon)
            continue;
        clearEdgePagingActions(*icon);
        const int left = prevFocusable[i];
        const int right = nextFocusable[i];
        // Dead-end at the strip ends (self). nullptr would fall through to
        // spatial nav and yank focus into the left/right sidebar chrome.
        icon->setCustomNavigation(nxui::FocusDirection::LEFT,
                                  left >= 0 ? m_allIcons[(size_t)left].get()
                                            : icon.get());
        icon->setCustomNavigation(nxui::FocusDirection::RIGHT,
                                  right >= 0 ? m_allIcons[(size_t)right].get()
                                             : icon.get());
        icon->setCustomNavigation(nxui::FocusDirection::UP, m_lineUpTarget);
        icon->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
        addChild(icon);
        if (icon->isFocusable())
            fItems.push_back(icon.get());
    }

    m_focus.setGrid(fItems, std::max(1, (int)fItems.size()));

    if (prevFocused) {
        for (auto* item : fItems) {
            if (item == prevFocused) {
                m_focus.setFocus(prevFocused);
                break;
            }
        }
    }
}

void IconGrid::bindEdgeActions(int start, int end) {
    if (!m_edgePaging || m_cols <= 0)
        return;
    for (int i = start; i < end; ++i) {
        nxui::Widget* w = m_allIcons[i].get();
        if (!w)
            continue;
        const int col = (i - start) % m_cols;
        if (col == m_cols - 1) {
            auto next = [this]() { if (m_onEdgePage) m_onEdgePage(+1); };
            w->addAction(static_cast<uint64_t>(nxui::Button::DRight), next);
            w->addAction(static_cast<uint64_t>(nxui::Button::LStickR), next);
            w->addAction(static_cast<uint64_t>(nxui::Button::RStickR), next);
        }
        if (col == 0) {
            auto prev = [this]() { if (m_onEdgePage) m_onEdgePage(-1); };
            w->addAction(static_cast<uint64_t>(nxui::Button::DLeft), prev);
            w->addAction(static_cast<uint64_t>(nxui::Button::LStickL), prev);
            w->addAction(static_cast<uint64_t>(nxui::Button::RStickL), prev);
        }
    }
}

void IconGrid::positionPage(int page, float dx) {
    const int start = page * iconsPerPage();
    const int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    // Stagger: trailing columns lag the page by up to 30px per column and catch
    // up as the slide completes. The page still arrives together, but it enters
    // as a cascade instead of one rigid slab. Only during a slide, so the edge
    // bump and static layout are untouched.
    float stagger = 0.f;
    if (m_sliding) {
        const float t = std::clamp(m_slideT / motion::scaled(slideDuration(), m_motionStyle),
                                   0.f, 1.f);
        const float trail = (1.f - t) * (1.f - t);
        stagger = trail * 30.f * static_cast<float>(m_slideDir);
    }
    for (int i = start; i < end; ++i) {
        const int local = i - start;
        auto& icon = m_allIcons[i];
        if (!icon)
            continue;
        const int spanColumns = std::max(1, icon->gridSpanColumns());
        const int spanRows = std::max(1, icon->gridSpanRows());
        const float columnLag = stagger * static_cast<float>(local % std::max(1, m_cols));
        const nxui::Rect slot{
            m_originX + m_gridNudgeX + (local % m_cols) * (m_cellW + m_padX)
                + dx + columnLag,
            m_originY + (local / m_cols) * (m_cellH + m_padY),
            m_cellW * spanColumns + m_padX * (spanColumns - 1),
            m_cellH * spanRows + m_padY * (spanRows - 1)};
        icon->setRect(fitGridRect(i, slot));
    }
}

float IconGrid::pageStride() const {
    const float gridW = m_cols * m_cellW + (m_cols - 1) * m_padX;
    return std::max(m_rect.width, (m_originX - m_rect.x) + gridW + m_padX);
}

int IconGrid::focusedGlobalIndex() const {
    auto* cur = m_focus.current();
    if (!cur) {
        m_focusIndexWidget = nullptr;
        m_focusIndexCache = -1;
        return -1;
    }
    const int count = static_cast<int>(m_allIcons.size());
    // Valid while the focus widget is the same and the cached slot still holds
    // it; a rebuilt or reordered icon list fails the slot check and rescans.
    if (cur == m_focusIndexWidget && count == m_focusIndexCount
        && m_focusIndexCache >= 0 && m_focusIndexCache < count
        && m_allIcons[static_cast<std::size_t>(m_focusIndexCache)].get() == cur) {
        return m_focusIndexCache;
    }
    int found = -1;
    for (int i = 0; i < count; ++i) {
        if (m_allIcons[(std::size_t)i].get() == cur) {
            found = i;
            break;
        }
    }
    m_focusIndexWidget = cur;
    m_focusIndexCount = count;
    m_focusIndexCache = found;
    return found;
}

float IconGrid::lineScaleAt(float absDist, bool poster) const {
    // Shrink runs 0..1 with the shipped row at 0.5. Below that the row flattens
    // out until every tile matches the focused one; above it the neighbours ease
    // away, stopping at a maximum that still leaves them legible.
    const float sideStock = poster ? 0.72f : 0.82f;
    const float focusStock = poster ? 1.12f : 1.36f;
    const float farStock = 0.46f;
    float side, focus, shrinkTo;
    if (m_carouselShrink <= 0.5f) {
        const float t = m_carouselShrink / 0.5f;
        focus    = 1.f + (focusStock - 1.f) * t;
        side     = 1.f + (sideStock - 1.f) * t;
        shrinkTo = 1.f + (farStock - 1.f) * t;
    } else {
        // Ease into the maximum instead of running linear. The first half of
        // the travel stays gentle and the top end tops out while the neighbours
        // are still large enough to read at a glance.
        const float raw = (m_carouselShrink - 0.5f) / 0.5f;
        const float t = raw * raw * (3.f - 2.f * raw);
        focus    = focusStock + 0.06f * t;
        side     = sideStock - 0.16f * t;
        shrinkTo = farStock - 0.08f * t;
    }
    if (absDist <= 1.f) {
        const float cb = 1.f - absDist;
        return side + cb * cb * (3.f - 2.f * cb) * (focus - side);
    }
    const float far = absDist - 1.f;
    const float soften = 1.f - std::exp(-far * 1.1f);
    return std::max(shrinkTo, side - (side - shrinkTo) * soften);
}

bool IconGrid::linePosterAt(int index) const {
    if (!m_linePosterAspect || index < 0
        || index >= static_cast<int>(m_allIcons.size()))
        return false;
    const auto& icon = m_allIcons[static_cast<std::size_t>(index)];
    return icon && icon->usesPosterFrame();
}

float IconGrid::lineFadeSpan() const {
    // Stronger shrink keeps more neighbours readable, so the fade run
    // stretches a little; a flat row fades sooner.
    const float span = (1.8f + 5.2f * (1.f - m_carouselFade))
                     * (0.78f + 0.44f * m_carouselShrink);
    return 1.f + std::max(0.25f, span);
}

const std::vector<float>& IconGrid::lineGapPrefix() const {
    const float offset = m_lineScrollOffset.value();
    const float spacing = lineItemSpacing();
    const std::size_t count = m_allIcons.size();
    const bool reusable = m_lineGapPrefixOffset == offset
        && m_lineGapPrefixScale == m_iconScale
        && m_lineGapPrefixShrink == m_carouselShrink
        && m_lineGapPrefixSpacing == spacing
        && m_lineGapPrefixPosters == m_linePosterAspect
        && m_lineGapPrefix.size() == count;
    if (reusable)
        return m_lineGapPrefix;

    m_lineGapPrefix.assign(count, 0.f);
    if (count > 1) {
        const float gap = std::max(6.f, spacing - kLineIconBase * m_iconScale);
        auto widthAt = [&](int j) { return lineWidthAt(j); };
        float width = widthAt(0);
        for (std::size_t j = 1; j < count; ++j) {
            const float next = widthAt(static_cast<int>(j));
            m_lineGapPrefix[j] = m_lineGapPrefix[j - 1] + 0.5f * (width + next) + gap;
            width = next;
        }
    }
    m_lineGapPrefixOffset = offset;
    m_lineGapPrefixScale = m_iconScale;
    m_lineGapPrefixShrink = m_carouselShrink;
    m_lineGapPrefixSpacing = spacing;
    m_lineGapPrefixPosters = m_linePosterAspect;
    return m_lineGapPrefix;
}

float IconGrid::lineWidthAt(int index) const {
    const int count = static_cast<int>(m_allIcons.size());
    const float offset = m_lineScrollOffset.value();
    const float spacing = lineItemSpacing();
    if (m_lineWidthCount != count || m_lineWidthOffset != offset
        || m_lineWidthScale != m_iconScale
        || m_lineWidthShrink != m_carouselShrink
        || m_lineWidthSpacing != spacing
        || m_lineWidthPosters != m_linePosterAspect) {
        m_lineWidthCount = count;
        m_lineWidthOffset = offset;
        m_lineWidthScale = m_iconScale;
        m_lineWidthShrink = m_carouselShrink;
        m_lineWidthSpacing = spacing;
        m_lineWidthPosters = m_linePosterAspect;
        const std::size_t slots = static_cast<std::size_t>(count);
        if (m_lineWidthCache.size() != slots) {
            m_lineWidthCache.resize(slots);
            m_lineWidthStamp.assign(slots, 0u);
        }
        ++m_lineWidthGeneration;
        if (m_lineWidthGeneration == 0u) {
            m_lineWidthStamp.assign(slots, 0u);
            m_lineWidthGeneration = 1u;
        }
    }

    const bool cachedIndex = index >= 0 && index < count;
    if (cachedIndex) {
        const std::size_t slot = static_cast<std::size_t>(index);
        if (m_lineWidthStamp[slot] == m_lineWidthGeneration)
            return m_lineWidthCache[slot];
    }

    const bool poster = linePosterAt(index);
    const float base = (poster ? kPosterBaseW : kLineIconBase) * m_iconScale;
    const float width = base * lineScaleAt(
        std::abs(static_cast<float>(index) - offset), poster);
    if (cachedIndex) {
        const std::size_t slot = static_cast<std::size_t>(index);
        m_lineWidthCache[slot] = width;
        m_lineWidthStamp[slot] = m_lineWidthGeneration;
    }
    return width;
}

nxui::Rect IconGrid::dynamicIconRect(int index, float* outScale,
                                     float* outOpacity,
                                     float* outDistance) const {
    const float centerX = m_rect.x + m_rect.width * 0.5f + m_lineNudgeX;
    // Unfocused row sits in the lower half; a focused Poster rises to true
    // vertical centre so the title pill has equal air above/below.
    const float lineAvailH = std::max(0.f, m_rect.height - m_insetTop - m_insetBottom);
    const float lineCenterY = m_rect.y + m_insetTop + lineAvailH * 0.66f;
    const float offset = m_lineScrollOffset.value();
    // Carousel sizing is independent of grid density. Square icons stay square;
    // apps with a Poster and folders (when posters are enabled) use tall 2:3.
    bool poster = false;
    if (m_linePosterAspect && index >= 0 && index < (int)m_allIcons.size()) {
        const auto& icon = m_allIcons[(std::size_t)index];
        poster = icon && icon->usesPosterFrame();
    }
    // Icon size applies to the carousel too; spacing scales with it so larger
    // tiles keep a steady gap instead of overlapping.
    const float baseCellW = (poster ? kPosterBaseW : kLineIconBase) * m_iconScale;
    const float baseCellH = poster
        ? (kPosterBaseW * 1.5f * m_iconScale)
        : (kLineIconBase * m_iconScale / iconAspectAt(index));
    // Scroll pitch stays icon-sized so posters can hero-overlap neighbours.
    const float lineSpacing = lineItemSpacing();

    float d = static_cast<float>(index) - offset;
    const float absD = std::abs(d);
    // Distance shrink: the focused tile stays large while every step away from
    // it gets smaller, so the row falls away instead of scrolling flat.
    float focusBlend = 0.f;
    if (absD <= 1.f) {
        // Position drives the visual state: the departing icon shrinks as it
        // leaves centre while the incoming icon travels and grows into it.
        const float centerBlend = 1.f - absD;
        focusBlend = centerBlend * centerBlend * (3.f - 2.f * centerBlend);
    }
    auto widthAt = [&](int j) { return lineWidthAt(j); };

    const float s = lineScaleAt(absD, poster);
    float a = 0.76f;
    if (absD <= 1.f) {
        a = 0.76f + focusBlend * 0.24f;
    } else {
        // Fade to nothing over a finite run. The old curve ramped linearly into
        // a flat floor alpha and then dropped the tile entirely at a fixed 4.5
        // tiles, so the row ended on a dim plateau and the next tile popped off
        // the screen. A smoothstep that reaches zero exactly where the run ends
        // has no plateau and no pop: the strip dissolves instead of being cut.
        // The slider sets the run length (light keeps neighbours lit out towards
        // the screen edge, heavy keeps only the next tile or two), and a stronger
        // shrink stretches it so its extra visible tiles stay readable.
        const float far = absD - 1.f;
        const float span = (1.8f + 5.2f * (1.f - m_carouselFade))
                         * (0.78f + 0.44f * m_carouselShrink);
        const float fadeT = std::clamp(1.f - far / std::max(0.25f, span),
                                       0.f, 1.f);
        const float curve = fadeT * fadeT * (3.f - 2.f * fadeT);
        a = 0.76f * curve;
    }

    const float w = baseCellW * s;
    const float h = baseCellH * s;
    // Equal gaps: walk out from the tile under the focus one step at a time,
    // charging each step the widths of the two tiles it joins plus one shared
    // gap. A constant pitch gave a wide focused tile almost no air beside it
    // while small neighbours sat far apart; here every adjacent pair keeps the
    // same gap no matter how different their scales are. The fractional part of
    // the scroll offset interpolates between the layout anchored on the lower
    // tile and the one anchored on the next, so dragging stays continuous.
    const float anchorF = std::floor(offset);
    const int anchor = static_cast<int>(anchorF);
    const float frac = offset - anchorF;
    const float gap = std::max(6.f, lineSpacing - kLineIconBase * m_iconScale);
    float shift = 0.f;
    const int lineCount = static_cast<int>(m_allIcons.size());
    // The low-edge anchor can be -1. Keep the shelf/vertical metrics on the
    // nearest real tile so a leading poster does not jump vertically when the
    // row is pulled past the start.
    const int baselineAnchor = lineCount > 0
        ? std::clamp(anchor, 0, lineCount - 1) : 0;
    if (index != anchor) {
        if (index >= 0 && index < lineCount && anchor >= 0 && anchor < lineCount) {
            // Visible tiles sit within a few steps of the anchor. Walking that
            // short span is cheaper than rebuilding the whole-library prefix on
            // every scroll frame. Use the walk for every library size; the
            // prefix is only needed for far indices (layout morph, hit testing).
            constexpr int kMaxWalk = 16;
            if (std::abs(index - anchor) <= kMaxWalk) {
                const int dir = index > anchor ? 1 : -1;
                for (int j = anchor; j != index; j += dir)
                    shift += dir * (0.5f * (widthAt(j) + widthAt(j + dir)) + gap);
            } else {
                const auto& prefix = lineGapPrefix();
                shift = prefix[static_cast<std::size_t>(index)]
                      - prefix[static_cast<std::size_t>(anchor)];
            }
        } else {
            // Off the ends of the library the prefix has no entry, so fall back
            // to the walk (only while the row overscrolls past an edge).
            const int dir = index > anchor ? 1 : -1;
            for (int j = anchor; j != index; j += dir)
                shift += dir * (0.5f * (widthAt(j) + widthAt(j + dir)) + gap);
        }
    }
    if (frac > 0.0001f)
        shift -= frac * (0.5f * (widthAt(anchor) + widthAt(anchor + 1)) + gap);
    const float x = centerX + shift - w * 0.5f;
    // Shelf alignment: every tile rests on the focused tile's bottom edge, so
    // the smaller neighbours sit down beside the big one instead of floating at
    // their own centres. The shelf is taken from the anchor tile under the
    // focus, which is also what carries a focused poster up into the middle
    // band while its neighbours follow to the same line.
    const bool anchorPoster = linePosterAt(baselineAnchor);
    const float anchorBaseH = anchorPoster
        ? kPosterBaseW * 1.5f * m_iconScale
        : kLineIconBase * m_iconScale / iconAspectAt(baselineAnchor);
    const float anchorScale = lineScaleAt(0.f, anchorPoster);
    // Clear the clock capsule (~y 70) and leave room for the title pill.
    constexpr float kTopPad = 100.f;
    constexpr float kBottomPad = 128.f;
    const float bandTop = m_rect.y + m_insetTop + kTopPad;
    const float bandBottom = m_rect.y + m_rect.height - m_insetBottom - kBottomPad;
    const float anchorCenterY =
        (anchorPoster ? (bandTop + bandBottom) * 0.5f : lineCenterY) + m_lineNudgeY;
    const float baseline = anchorCenterY + anchorBaseH * anchorScale * 0.5f;
    const float y = baseline - h;

    if (outScale) *outScale = s;
    if (outOpacity) *outOpacity = a;
    if (outDistance) *outDistance = absD;
    return {x, y, w, h};
}

void IconGrid::rebuildLineFrame(float offset, int focusedIndex,
                                bool fullLayout) const {
    m_lineFrameScratch.clear();
    const int count = static_cast<int>(m_allIcons.size());
    if (count <= 0) {
        m_lineFrameOffset = offset;
        m_lineFrameCount = 0;
        m_lineFrameFocus = focusedIndex;
        m_lineFrameRect = m_rect;
        m_lineFrameValid = true;
        return;
    }

    int first = 0;
    int last = count - 1;
    if (!fullLayout) {
        const float window = std::max(lineFadeSpan(), 5.f);
        first = std::max(0, static_cast<int>(std::floor(offset - window)));
        last = std::min(count - 1, static_cast<int>(std::ceil(offset + window)));
    }

    const bool focusedOutside = focusedIndex >= 0 && focusedIndex < count
        && (focusedIndex < first || focusedIndex > last);
    const std::size_t needed =
        static_cast<std::size_t>(last - first + 1) + (focusedOutside ? 1u : 0u);
    if (m_lineFrameScratch.capacity() < needed)
        m_lineFrameScratch.reserve(needed);

    for (int i = first; i <= last; ++i) {
        auto& icon = m_allIcons[static_cast<std::size_t>(i)];
        if (!icon)
            continue;
        float s = 1.f;
        float a = 1.f;
        float absD = 0.f;
        const nxui::Rect r = dynamicIconRect(i, &s, &a, &absD);
        icon->setRect(r);
        m_lineFrameScratch.push_back({i, absD, s, a, r});
    }

    // A programmatic focus jump can leave the focused tile outside the scroll
    // window. It still needs a valid rect for the cursor and metadata strip.
    if (focusedOutside) {
        auto& icon = m_allIcons[static_cast<std::size_t>(focusedIndex)];
        if (icon) {
            float s = 1.f;
            float a = 1.f;
            float absD = 0.f;
            const nxui::Rect r = dynamicIconRect(focusedIndex, &s, &a, &absD);
            icon->setRect(r);
            m_lineFrameScratch.push_back({focusedIndex, absD, s, a, r});
        }
    }

    m_lineFrameOffset = offset;
    m_lineFrameCount = count;
    m_lineFrameFocus = focusedIndex;
    m_lineFrameRect = m_rect;
    m_lineFrameValid = true;
}

void IconGrid::ensureLineFrame(float offset, int focusedIndex) const {
    const bool sameRect =
        std::abs(m_lineFrameRect.x - m_rect.x) <= 0.0001f
        && std::abs(m_lineFrameRect.y - m_rect.y) <= 0.0001f
        && std::abs(m_lineFrameRect.width - m_rect.width) <= 0.0001f
        && std::abs(m_lineFrameRect.height - m_rect.height) <= 0.0001f;
    if (m_lineFrameValid
        && m_lineFrameCount == static_cast<int>(m_allIcons.size())
        && m_lineFrameFocus == focusedIndex
        && std::abs(m_lineFrameOffset - offset) <= 0.0001f
        && sameRect)
        return;
    rebuildLineFrame(offset, focusedIndex, false);
}

nxui::Rect IconGrid::gridSlotRect(int globalIndex) const {
    if (globalIndex < 0 || globalIndex >= (int)m_allIcons.size())
        return {};
    const auto& icon = m_allIcons[(std::size_t)globalIndex];
    const int spanColumns = icon ? std::max(1, icon->gridSpanColumns()) : 1;
    const int spanRows    = icon ? std::max(1, icon->gridSpanRows())    : 1;
    const int local  = globalIndex % std::max(1, iconsPerPage());
    const int column = local % std::max(1, m_cols);
    const int row    = local / std::max(1, m_cols);
    const nxui::Rect slot{m_originX + m_gridNudgeX + column * (m_cellW + m_padX),
                          m_originY + row * (m_cellH + m_padY),
                          m_cellW * spanColumns + m_padX * (spanColumns - 1),
                          m_cellH * spanRows + m_padY * (spanRows - 1)};
    return fitGridRect(globalIndex, slot);
}

nxui::Rect IconGrid::morphBlend(int globalIndex, const nxui::Rect& gridRect) const {
    const int perPage = std::max(1, iconsPerPage());
    const int start = m_layoutMorphPage * perPage;
    const nxui::Rect line = dynamicIconRect(globalIndex);
    const bool inGrid = globalIndex >= start && globalIndex < start + perPage;
    return nxui::Rect::lerp(inGrid ? gridRect : line, line,
                            clamp01(m_layoutMorph.value()));
}

nxui::Rect IconGrid::focusedDisplayRect() const {
    const int focused = focusedGlobalIndex();
    if (m_layoutMorphing && focused >= 0)
        return morphBlend(focused, gridSlotRect(focused));
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        if (focused >= 0) {
            ensureLineFrame(m_lineScrollOffset.value(), focused);
            for (const auto& tile : m_lineFrameScratch)
                if (tile.index == focused)
                    return tile.rect;
            return dynamicIconRect(focused);
        }
    }
    if (auto* cur = m_focus.current())
        return cur->focusRect();
    return {};
}

bool IconGrid::focusGlobalIndex(int idx, bool instant, float durationSec) {
    clearLineEdgeArrival();
    if (idx < 0 || idx >= (int)m_allIcons.size())
        return false;
    if (!m_allIcons[idx] || !m_allIcons[idx]->isFocusable())
        return false;

    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        // Opening or closing a folder rebuilds the model while the carousel
        // still holds the offset of the other tree; animating from there would
        // visibly scroll through the whole row, so snap instead.
        m_lineScrollGoal = static_cast<float>(idx);
        if (instant) {
            m_lineScrollVel = 0.f;
            m_lineLaunchVel = 0.f;
            m_lineScrollOffset.setImmediate(m_lineScrollGoal);
        } else {
            // Called on every D-pad step. Keeping the velocity is the point: the
            // spring in onUpdate carries the row on towards the new goal instead
            // of restarting an ease from rest, which made a held direction lunge
            // on every repeat.
            const float delta = m_lineScrollGoal - m_lineScrollOffset.value();
            float dur = durationSec > 0.f
                ? durationSec
                : kLineScrollDuration / std::max(0.01f, m_lineScrollSpeed);
            // A single tap starts from rest, so its settle is the most visible
            // wait. Tighten only the short-hop spring; held repeats and flings
            // keep the existing travel timing.
            if (durationSec <= 0.f && std::abs(delta) <= 1.25f)
                dur *= 0.80f;
            m_lineScrollOmega = 5.5f / std::max(0.05f, dur);
            // A critically damped spring leaves from rest, so ramping the launch
            // velocity makes the first ~100 ms of a single tap cover almost no
            // distance and read as input lag. A tap from rest has no velocity to
            // preserve, so seed the spring directly on that first step. Held
            // repeats and direction changes still ramp, keeping their glide.
            if (std::abs(delta) > 0.01f) {
                const float dir = delta > 0.f ? 1.f : -1.f;
                const float kick = std::min(std::abs(delta) * 3.f, 2.f);
                if (std::abs(m_lineScrollVel) < 0.05f) {
                    m_lineScrollVel = dir * kick;
                    m_lineLaunchVel = 0.f;
                } else if (m_lineScrollVel * dir < kick) {
                    m_lineLaunchVel = dir * kick;
                } else {
                    m_lineLaunchVel = 0.f;
                }
            } else {
                m_lineLaunchVel = 0.f;
            }
        }
        // Set focus after arming the scroll. The focus callback checks
        // isDynamicLineScrolling() to decide whether to run an immediate icon
        // stream pass; doing this first keeps a single tap on the same path as
        // a held scroll and avoids a duplicate pass before the spring moves.
        m_focus.setFocus(m_allIcons[idx].get());
        return true;
    }

    int perPage = iconsPerPage();
    if (perPage <= 0)
        return false;

    int wantedPage = idx / perPage;
    if (wantedPage != m_page)
        setPage(wantedPage);

    m_focus.setFocus(m_allIcons[idx].get());
    return true;
}

bool IconGrid::swapSlots(int a, int b) {
    if (a < 0 || b < 0 || a >= (int)m_allIcons.size() || b >= (int)m_allIcons.size())
        return false;
    if (a == b)
        return true;

    std::swap(m_allIcons[a], m_allIcons[b]);
    if (m_layoutMode == AppLayoutMode::DynamicLine)
        layoutLine();
    else
        layoutPage();
    return true;
}

GlossyIcon* IconGrid::pageIconAt(int index) const {
    if (index < 0)
        return nullptr;
    const int count = static_cast<int>(m_allIcons.size());
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        const int cur = focusedGlobalIndex();
        const int center = cur >= 0 ? cur : 0;
        const int start = std::max(0, center - 4);
        const int end = std::min(count, center + 5);
        const int i = start + index;
        return i < end ? m_allIcons[static_cast<std::size_t>(i)].get() : nullptr;
    }
    const int start = m_page * iconsPerPage();
    const int end = std::min(start + iconsPerPage(), count);
    const int i = start + index;
    return i < end ? m_allIcons[static_cast<std::size_t>(i)].get() : nullptr;
}

std::vector<GlossyIcon*> IconGrid::pageIcons() const {
    std::vector<GlossyIcon*> out;
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        int cur = focusedGlobalIndex();
        int center = cur >= 0 ? cur : 0;
        int start = std::max(0, center - 4);
        int end = std::min((int)m_allIcons.size(), center + 5);
        for (int i = start; i < end; ++i)
            out.push_back(m_allIcons[i].get());
        return out;
    }
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i) out.push_back(m_allIcons[i].get());
    return out;
}

int IconGrid::hitTest(float screenX, float screenY) const {
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        ensureLineFrame(m_lineScrollOffset.value(), focusedGlobalIndex());
        for (const auto& tile : m_lineFrameScratch) {
            if (tile.rect.contains(screenX, screenY))
                return tile.index;
        }
        return -1;
    }
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i) {
        nxui::Rect r = m_allIcons[i]->focusRect();
        if (r.contains(screenX, screenY))
            return i - start;
    }
    return -1;
}

void IconGrid::startAppearAnimation(const IconAppearOptions& opt) {
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        int cur = focusedGlobalIndex();
        int center = cur >= 0 ? cur : 0;
        for (int i = 0; i < (int)m_allIcons.size(); ++i) {
            if (!m_allIcons[static_cast<std::size_t>(i)])
                continue;
            float dist = static_cast<float>(std::abs(i - center));
            float delay = std::min(0.40f, dist * 0.06f);
            if (opt.fromTile)
                m_allIcons[i]->setAppearOrigin(opt.origin);
            m_allIcons[i]->startAppear(opt.baseDelay + delay);
        }
        return;
    }
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    int maxDist = (m_cols - 1) + (m_rows - 1);
    for (int i = start; i < end; ++i) {
        int local = i - start;
        int col   = local % m_cols;
        int row   = local / m_cols;
        float t   = maxDist > 0 ? (float)(col + row) / maxDist : 0.f;
        if (opt.fromTile)
            m_allIcons[i]->setAppearOrigin(opt.origin);
        m_allIcons[i]->startAppear(opt.baseDelay + t * opt.stagger);
    }
}

void IconGrid::animateDisplacedTile(int displacedPrevious, int displacedCurrent) {
    if (m_layoutMode != AppLayoutMode::Grid || m_sliding || m_bumping)
        return;

    const int count = static_cast<int>(m_allIcons.size());
    const int perPage = std::max(1, iconsPerPage());
    const int page = m_page;
    if (displacedCurrent < 0 || displacedCurrent >= count ||
        displacedCurrent / perPage != page)
        return;
    GlossyIcon* icon = m_allIcons[static_cast<std::size_t>(displacedCurrent)].get();
    if (!icon)
        return;

    const int spanColumns = std::max(1, icon->gridSpanColumns());
    const int spanRows = std::max(1, icon->gridSpanRows());
    if (displacedPrevious >= 0 && displacedPrevious < count &&
        displacedPrevious / perPage == page) {
        icon->startGlideFrom(
            gridSpanRect(displacedPrevious, spanColumns, spanRows),
            kSwapGlideDuration);
        return;
    }
    // Came from another page: enter from the side that page sits on, the same
    // direction a page slide would have travelled.
    nxui::Rect from = gridSpanRect(displacedCurrent, spanColumns, spanRows);
    const bool fromEarlierPage =
        displacedPrevious < 0 ||
        (displacedCurrent / perPage) > (displacedPrevious / perPage);
    from.x += (fromEarlierPage ? -1.f : 1.f) * pageStride();
    icon->setAppearOrigin(from);
    icon->startAppear(0.f);
}

void IconGrid::startDisappearAnimation(const IconAppearOptions& opt, float dur) {
    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        for (auto& icon : m_allIcons)
            icon->startDisappear(opt.origin, opt.baseDelay, dur);
        return;
    }
    int start = m_page * iconsPerPage();
    int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    int maxDist = (m_cols - 1) + (m_rows - 1);
    for (int i = start; i < end; ++i) {
        int local = i - start;
        int col   = local % m_cols;
        int row   = local / m_cols;
        float t   = maxDist > 0 ? (float)(col + row) / maxDist : 0.f;
        m_allIcons[i]->startDisappear(opt.origin, opt.baseDelay + (1.f - t) * opt.stagger, dur);
    }
}

void IconGrid::startPageTransition(int targetPage) {
    if (m_layoutMode == AppLayoutMode::DynamicLine) return;

    targetPage = std::clamp(targetPage, 0, m_totalPages - 1);
    if (targetPage == m_page) return;

    m_bumping = false;
    m_edgeBump.setImmediate(0.f);

    const int fromPage = m_page;
    const int oldGlobalFocus = focusedGlobalIndex();
    const int wantedLocalCell = oldGlobalFocus >= 0
        ? oldGlobalFocus % std::max(1, iconsPerPage()) : 0;
    setPage(targetPage);

    // Preserve the logical cell when paging. A continuation cell belonging to
    // a large widget is not focusable, so choose the closest real anchor
    // instead of accepting FocusManager's unrelated first-item fallback.
    const int pageStart = targetPage * iconsPerPage();
    const int pageEnd = std::min(pageStart + iconsPerPage(),
                                 static_cast<int>(m_allIcons.size()));
    int best = -1;
    int bestDistance = std::numeric_limits<int>::max();
    const int wantedColumn = wantedLocalCell % std::max(1, m_cols);
    const int wantedRow = wantedLocalCell / std::max(1, m_cols);
    const nxui::Vec2 wantedCenter{
        m_originX + wantedColumn * (m_cellW + m_padX) + m_cellW * 0.5f,
        m_originY + wantedRow * (m_cellH + m_padY) + m_cellH * 0.5f};
    for (int index = pageStart; index < pageEnd; ++index) {
        if (!m_allIcons[static_cast<std::size_t>(index)] ||
            !m_allIcons[static_cast<std::size_t>(index)]->isFocusable())
            continue;
        if (m_allIcons[static_cast<std::size_t>(index)]->focusRect().contains(
                wantedCenter.x, wantedCenter.y)) {
            best = index;
            break;
        }
        const int local = index - pageStart;
        const int distance = std::abs(local % std::max(1, m_cols) - wantedColumn) +
                             std::abs(local / std::max(1, m_cols) - wantedRow);
        if (distance < bestDistance) {
            best = index;
            bestDistance = distance;
        }
    }
    if (best >= 0)
        m_focus.setFocus(m_allIcons[static_cast<std::size_t>(best)].get());

    if (!m_slideTransition) {
        m_sliding = false;
        startAppearAnimation();
        if (m_onPageSwitched) m_onPageSwitched();
        return;
    }

    m_slidePrevPage = fromPage;
    m_slideDir = (targetPage > fromPage) ? 1 : -1;
    m_slideT = 0.f;
    m_sliding = true;

    const int start = m_page * iconsPerPage();
    const int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i)
        m_allIcons[i]->forceVisible();
    // The outgoing page slides out at full opacity too: a first page change
    // while the startup cascade is still running used to show through it.
    const int prevStart = m_slidePrevPage * iconsPerPage();
    const int prevEnd = std::min(prevStart + iconsPerPage(), (int)m_allIcons.size());
    for (int i = prevStart; i < prevEnd; ++i)
        m_allIcons[i]->forceVisible();

    const float stride = pageStride();
    m_slideInDx  = stride * (float)m_slideDir;
    m_slideOutDx = 0.f;
    positionPage(m_page, m_slideInDx);

    if (m_onPageSwitched) m_onPageSwitched();
}

void IconGrid::bumpEdge(int dir) {
    if (m_layoutMode == AppLayoutMode::DynamicLine) return;
    if (dir == 0 || m_sliding || m_bumping)
        return;
    m_bumping = true;
    m_edgeBump.set(-dir * kEdgeBumpDistance, motion::scaled(0.10f, m_motionStyle),
                   motion::inOutEase(m_motionStyle));
    m_edgeBump.onComplete([this]() {
        // Springy keeps the elastic snap back; Crisp lands flat, no wobble.
        m_edgeBump.set(0.f, motion::scaled(0.34f, m_motionStyle),
                       m_motionStyle == 0 ? nxui::Easing::outCubic
                                          : nxui::Easing::outElastic);
    });
}

void IconGrid::stepLineSpring(float dt) {
    if (dt <= 0.f)
        return;
    // Integrate the full frame in bounded slices. The old clamp fixed the curve
    // after a long frame but also discarded elapsed time, so the row visibly
    // slowed down after a hitch. Normal frames still take the exact same
    // closed-form step; a dropped frame now catches up instead of lagging.
    constexpr float kMaxStep = 0.05f;
    while (dt > kMaxStep) {
        stepLineSpringStep(kMaxStep);
        dt -= kMaxStep;
    }
    stepLineSpringStep(dt);
}

void IconGrid::stepLineSpringStep(float dt) {
    const float maxIdx = std::max(0.f, static_cast<float>(m_allIcons.size()) - 1.f);
    if (dt <= 0.f)
        return;
    const float x0 = m_lineScrollOffset.value();
    const float v0 = m_lineScrollVel;
    // A close release can arm the arrival when the spring is already on the
    // edge; fire it here so the settled fast path cannot swallow it.
    if (m_lineEdgeArrivalPending) {
        const bool low = m_lineEdgeArrivalVelocity < 0.f;
        const bool goalAtEdge = low ? m_lineScrollGoal <= 0.0001f
                                    : m_lineScrollGoal >= maxIdx - 0.0001f;
        const bool atEdge = low ? x0 <= 0.001f
                                : x0 >= maxIdx - 0.001f;
        if (goalAtEdge && atEdge) {
            m_lineScrollOffset.setImmediate(low ? 0.f : maxIdx);
            m_lineScrollVel = m_lineEdgeArrivalVelocity;
            m_lineLaunchVel = 0.f;
            clearLineEdgeArrival();
            return;
        }
        if (!goalAtEdge)
            clearLineEdgeArrival();
    }
    // At rest the closed-form step would still run two trig calls and an exp
    // every frame. Snap out immediately once the spring is visually settled.
    if (std::abs(m_lineScrollGoal - x0) < 0.0005f
        && std::abs(v0) < 0.002f) {
        m_lineScrollOffset.setImmediate(m_lineScrollGoal);
        m_lineScrollVel = 0.f;
        m_lineLaunchVel = 0.f;
        return;
    }
    float omega = m_lineScrollOmega > 0.f
        ? m_lineScrollOmega
        : 5.5f / (kLineScrollDuration / std::max(0.01f, m_lineScrollSpeed));
    if (m_motionStyle == 0) omega *= 1.5f;        // Crisp
    else if (m_motionStyle == 2) omega *= 0.85f;  // Springy, slower but softer
    // Crisp and Smooth land dead; Springy is deliberately underdamped and is the
    // only style that overshoots, which is what its name promises.
    const float zeta = m_motionStyle == 2 ? 0.72f : 1.f;

    // Build into the launch velocity over a short ramp instead of assigning it
    // in one frame. Only ever accelerates towards the target: once the spring
    // has overtaken it, the assist drops out. A direction change crosses zero
    // through the same ramp, which removes the snap at the reversal.
    if (m_lineLaunchVel != 0.f && dt > 0.f) {
        constexpr float kLaunchTau = 0.05f;
        const float target = m_lineLaunchVel;
        const bool rising = target > 0.f ? m_lineScrollVel < target
                                         : m_lineScrollVel > target;
        if (rising) {
            const float blend = 1.f - std::exp(-dt / kLaunchTau);
            m_lineScrollVel += (target - m_lineScrollVel) * blend;
            if (std::abs(target - m_lineScrollVel) < 0.01f) {
                m_lineScrollVel = target;
                m_lineLaunchVel = 0.f;
            }
        } else {
            m_lineLaunchVel = 0.f;
        }
    }

    // Closed-form integration of the damped oscillator instead of fixed 240 Hz
    // Euler substeps. The old loop integrated the same flick differently at
    // 30 fps and 60 fps (and picked up a little error every step), which showed
    // up as the row landing slightly short on a heavy frame. The analytic step
    // is exact for any dt, so the glide is identical at every frame rate.
    float x = x0;
    float v = v0;
    if (dt > 0.f) {
        const float a = x0 - m_lineScrollGoal;
        if (zeta < 0.999f) {
            const float wd = omega * std::sqrt(std::max(0.f, 1.f - zeta * zeta));
            const float decay = std::exp(-zeta * omega * dt);
            const float c = (v0 + zeta * omega * a) / std::max(0.001f, wd);
            const float cosT = std::cos(wd * dt);
            const float sinT = std::sin(wd * dt);
            x = m_lineScrollGoal + decay * (a * cosT + c * sinT);
            v = decay * ((c * wd - zeta * omega * a) * cosT
                         - (a * wd + zeta * omega * c) * sinT);
        } else {
            const float decay = std::exp(-omega * dt);
            const float b = v0 + omega * a;
            x = m_lineScrollGoal + (a + b * dt) * decay;
            v = (b - omega * (a + b * dt)) * decay;
        }
    }

    // When a fling reaches an end, transfer the armed arrival speed into the
    // rubber band instead of letting the critically damped spring stop there.
    if (m_lineEdgeArrivalPending) {
        const bool low = m_lineEdgeArrivalVelocity < 0.f;
        const bool goalAtEdge = low ? m_lineScrollGoal <= 0.0001f
                                    : m_lineScrollGoal >= maxIdx - 0.0001f;
        if (!goalAtEdge) {
            clearLineEdgeArrival();
        } else {
            const bool reached = low ? x <= 0.001f
                                     : x >= maxIdx - 0.001f;
            if (reached) {
                x = low ? 0.f : maxIdx;
                v = m_lineEdgeArrivalVelocity;
                m_lineLaunchVel = 0.f;
                clearLineEdgeArrival();
            }
        }
    }

    // Let the row carry a little past either end while the same spring pulls
    // it back. The rubber-band bound is a hard stop only for a runaway spring,
    // not for the initial pull or the short release bounce.
    if (x < -kLineEdgeOverscroll) {
        x = -kLineEdgeOverscroll;
        if (v < 0.f) v = 0.f;
    }
    if (x > maxIdx + kLineEdgeOverscroll) {
        x = maxIdx + kLineEdgeOverscroll;
        if (v > 0.f) v = 0.f;
    }
    if (std::abs(m_lineScrollGoal - x) < 0.0005f && std::abs(v) < 0.002f) {
        x = m_lineScrollGoal;
        v = 0.f;
        clearLineEdgeArrival();
    }
    m_lineScrollVel = v;
    m_lineScrollOffset.setImmediate(x);
}

void IconGrid::onUpdate(float dt) {
    if (m_layoutMorphing) {
        // AnimationManager advances the tween; this only clears the flag.
        if (std::abs(m_layoutMorph.value() - m_layoutMorph.target()) < 0.001f) {
            m_layoutMorph.setImmediate(m_layoutMorph.target());
            m_layoutMorphing = false;
        }
    }

    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        // While the finger owns the offset the spring stays out of the way.
        if (!m_lineDragActive) {
            const int cur = focusedGlobalIndex();
            if (cur >= 0)
                m_lineScrollGoal = static_cast<float>(cur);
            stepLineSpring(dt);
        }

        // Every rect on the line is a pure function of the scroll offset and
        // the grid rect. At rest both are constant, so recomputing them each
        // frame produced identical values for the whole installed library.
        // Recompute only when one of those inputs moved.
        const float offsetNow = m_lineScrollOffset.value();
        const bool layoutDirty =
            m_lineLayoutCacheCount != (int)m_allIcons.size()
            || std::abs(m_lineLayoutCacheOffset - offsetNow) > 0.0001f
            || std::abs(m_lineLayoutCacheRect.x - m_rect.x) > 0.0001f
            || std::abs(m_lineLayoutCacheRect.y - m_rect.y) > 0.0001f
            || std::abs(m_lineLayoutCacheRect.width - m_rect.width) > 0.0001f
            || std::abs(m_lineLayoutCacheRect.height - m_rect.height) > 0.0001f;

        if (layoutDirty) {
            // Only tiles inside the fade span can be seen or hit-tested, and
            // pageIcons() never walks further than the focus +-4. A shape change
            // lays out the whole row once, exactly as before; ordinary scrolling
            // rebuilds only this window and keeps the results for render/hit-test.
            const bool shapeChanged = m_lineLayoutCacheCount != (int)m_allIcons.size()
                || std::abs(m_lineLayoutCacheRect.x - m_rect.x) > 0.0001f
                || std::abs(m_lineLayoutCacheRect.y - m_rect.y) > 0.0001f
                || std::abs(m_lineLayoutCacheRect.width - m_rect.width) > 0.0001f
                || std::abs(m_lineLayoutCacheRect.height - m_rect.height) > 0.0001f;
            rebuildLineFrame(offsetNow, focusedGlobalIndex(), shapeChanged);
            m_lineLayoutCacheCount = (int)m_allIcons.size();
            m_lineLayoutCacheOffset = offsetNow;
            m_lineLayoutCacheRect = m_rect;
        }
        return;
    }

    if (m_bumping) {
        positionPage(m_page, m_edgeBump.value());
        if (std::abs(m_edgeBump.value()) < 0.05f && m_edgeBump.target() == 0.f) {
            m_bumping = false;
            positionPage(m_page, 0.f);
        }
    }

    if (!m_sliding)
        return;

    m_slideT += dt;
    const float t = std::clamp(m_slideT / motion::scaled(slideDuration(), m_motionStyle),
                               0.f, 1.f);
    const float eased = motion::inOutEase(m_motionStyle)(t);
    const float stride = pageStride();

    m_slideInDx  = (1.f - eased) * stride * (float)m_slideDir;
    m_slideOutDx = m_slideInDx - stride * (float)m_slideDir;
    positionPage(m_page, m_slideInDx);

    if (t >= 1.f) {
        m_sliding = false;
        m_slideInDx = m_slideOutDx = 0.f;
        positionPage(m_page, 0.f);
    }
}

void IconGrid::renderPageAt(nxui::Renderer& ren, int page, float dx) {
    const int start = page * iconsPerPage();
    const int end   = std::min(start + iconsPerPage(), (int)m_allIcons.size());
    for (int i = start; i < end; ++i) {
        auto& icon = m_allIcons[i];
        const nxui::Rect saved = icon->rect();
        const int local = i - start;
        const int spanColumns = std::max(1, icon->gridSpanColumns());
        const int spanRows = std::max(1, icon->gridSpanRows());
        const nxui::Rect slot = {
            m_originX + m_gridNudgeX + (local % m_cols) * (m_cellW + m_padX) + dx,
            m_originY + (local / m_cols) * (m_cellH + m_padY),
            m_cellW * spanColumns + m_padX * (spanColumns - 1),
            m_cellH * spanRows + m_padY * (spanRows - 1)
        };
        const nxui::Rect r = fitGridRect(i, slot);
        // Skip the whole tile when it is outside the screen: no rect swap and
        // no widget render for a page that is sliding in from far away.
        constexpr float kCullMargin = 32.f;
        if (r.right() < -kCullMargin || r.x > 1280.f + kCullMargin
            || r.bottom() < -kCullMargin || r.y > 720.f + kCullMargin)
            continue;
        icon->setRect(r);
        // Page slides draw two full grids at once. Use the same motion-only tile
        // treatment as the carousel so the incoming page does not pay for the
        // stepped overlays, captions and shadows on every tile in one frame.
        if (m_sliding || m_bumping)
            icon->setScrollLowDetail(true);
        icon->render(ren);
        if (m_sliding || m_bumping)
            icon->setScrollLowDetail(false);
        icon->setRect(saved);
    }
}

void IconGrid::renderDynamicLine(nxui::Renderer& ren) {
    perf::ScopedPass pass("carousel");
    // Focused Posters rise above the grid band for vertical centering — clip to
    // the full screen height so their tops (and rounded corners) are not sheared.
    nxui::Rect clip = m_rect;
    clip.y = 0.f;
    clip.height = 720.f;
    ren.pushClipRect(clip);

    // The focused index is a linear scan over every icon. Reading it inside the
    // loop made the whole pass quadratic in the installed title count for a
    // value that cannot change while the loop runs.
    const int focusedIndex = focusedGlobalIndex();
    // Tiles past the end of the fade are invisible, so skip their rect maths:
    // a large library no longer pays for the whole row on every frame.
    const float lineOffset = m_lineScrollOffset.value();
    const float visibleReach = lineFadeSpan();
    ensureLineFrame(lineOffset, focusedIndex);

    auto& candidates = m_lineRenderScratch;
    // Rebuilding and sorting the visible set only matters while the focus or
    // the scroll position moves. At rest the inputs are identical, so reuse the
    // previous frame's ordered list instead of clearing and re-sorting it.
    const bool cacheValid = m_lineRenderValid
        && m_lineRenderFocus == focusedIndex
        && m_lineRenderCount == (int)m_allIcons.size()
        && std::abs(m_lineRenderOffset - lineOffset) <= 0.0001f
        && std::abs(m_lineRenderReach - visibleReach) <= 0.0001f
        && std::abs(m_lineRenderRect.x - m_rect.x) <= 0.0001f
        && std::abs(m_lineRenderRect.y - m_rect.y) <= 0.0001f
        && std::abs(m_lineRenderRect.width - m_rect.width) <= 0.0001f
        && std::abs(m_lineRenderRect.height - m_rect.height) <= 0.0001f;

    if (!cacheValid) {
    candidates.clear();
    candidates.reserve(m_lineFrameScratch.size());

    for (const auto& tile : m_lineFrameScratch) {
        const int i = tile.index;
        if (i != focusedIndex
            && tile.absD > visibleReach)
            continue;
        // Cull on the alpha the curve actually produced instead of a fixed tile
        // distance: the fade now reaches zero on its own, so the last visible
        // tile is the one the curve zeroed and nothing pops at an arbitrary edge.
        if (tile.a <= 0.004f && i != focusedIndex) continue;
        // Alpha alone still admits far-away tiles when the fade is relaxed;
        // their rects are outside the screen, so they cannot leave a mark.
        constexpr float kCullMargin = 32.f;
        if (tile.rect.right() < -kCullMargin || tile.rect.x > 1280.f + kCullMargin
            || tile.rect.bottom() < -kCullMargin || tile.rect.y > 720.f + kCullMargin)
            continue;
        const float d = tile.rect.center().x - m_rect.center().x;
        candidates.push_back({i, tile.absD, d, tile.s, tile.a, tile.rect});
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.absD > rhs.absD;
    });

    m_lineRenderFocus = focusedIndex;
    m_lineRenderCount = (int)m_allIcons.size();
    m_lineRenderOffset = lineOffset;
    m_lineRenderReach = visibleReach;
    m_lineRenderRect = m_rect;
    m_lineRenderValid = true;
    }

    // A moving row spends most of its GPU budget on per-tile captions, shine
    // and multi-layer shadows that are not readable at speed. The old 8/s
    // threshold only caught flings, so ordinary D-pad glides still paid full
    // detail for every neighbour. Drop detail for the whole sweep, and for the
    // focused tile too once the row is moving quickly. Everything is restored
    // on the first resting frame.
    const float motionSpeed = lineMotionSpeed();
    // Hold each detail tier until the row is clearly below its exit speed.
    // Direct threshold checks flickered when a held D-pad hovered on a boundary.
    constexpr float kFastDetailEnter = 0.35f;
    constexpr float kFastDetailExit = 0.18f;
    constexpr float kLowDetailEnter = 3.5f;
    constexpr float kLowDetailExit = 2.5f;
    // A step armed by input but not stepped yet still counts as motion. Without
    // this the first frame of every single tap drew every neighbour at full
    // detail, then dropped to the scroll tier one frame later: a visible hitch
    // at the start of exactly the one-step case.
    const bool motionPending = std::abs(m_lineScrollVel) > 0.001f
        || std::abs(m_lineScrollGoal - lineOffset) > 0.0001f;
    m_lineFastDetail = motionPending || motionSpeed >=
        (m_lineFastDetail ? kFastDetailExit : kFastDetailEnter);
    m_lineLowDetailAll = motionSpeed >=
        (m_lineLowDetailAll ? kLowDetailExit : kLowDetailEnter);
    const bool fastScroll = m_lineFastDetail;
    const bool lowDetailAll = m_lineLowDetailAll;
    for (const auto& c : candidates) {
        auto& icon = m_allIcons[c.index];
        const nxui::Rect savedRect = icon->rect();
        const float savedOp = icon->opacity();
        const bool scrollLowDetail = fastScroll
            && (lowDetailAll || c.index != focusedIndex);
        if (scrollLowDetail)
            icon->setScrollLowDetail(true);
        // onUpdate/ensureLineFrame already wrote these exact rects. Avoid two
        // redundant widget writes per visible tile on every scroll frame; only
        // move the rect if a caller left this tile on a different layout.
        const bool rectMoved = savedRect.x != c.rect.x
            || savedRect.y != c.rect.y
            || savedRect.width != c.rect.width
            || savedRect.height != c.rect.height;

        if (rectMoved)
            icon->setRect(c.rect);
        icon->setOpacity(savedOp * c.a);
        icon->render(ren);

        if (scrollLowDetail)
            icon->setScrollLowDetail(false);
        if (rectMoved)
            icon->setRect(savedRect);
        icon->setOpacity(savedOp);
    }

    // Edge shadow follows the rubber-band offset, so it appears with the pull
    // and fades as the same spring returns. It is drawn after the tiles to make
    // the exposed margin read as a soft inner shadow rather than a flat scrim.
    const float edgeMaxIdx = std::max(0.f,
        static_cast<float>(m_allIcons.size()) - 1.f);
    const float leftOver = std::clamp(-lineOffset / kLineEdgeOverscroll,
                                      0.f, 1.f);
    const float rightOver = std::clamp((lineOffset - edgeMaxIdx)
                                        / kLineEdgeOverscroll, 0.f, 1.f);
    drawEdgeShadow(ren, clip, leftOver, rightOver);

    ren.popClipRect();
}

void IconGrid::renderLayoutMorph(nxui::Renderer& ren) {
    const float t = clamp01(m_layoutMorph.value());
    const int perPage = std::max(1, iconsPerPage());
    const int gridStart = m_layoutMorphPage * perPage;
    const int gridEnd = std::min(gridStart + perPage, (int)m_allIcons.size());

    struct Candidate { int index; float absD; };
    std::vector<Candidate> candidates;
    candidates.reserve(m_allIcons.size());
    for (int i = 0; i < (int)m_allIcons.size(); ++i) {
        if (!m_allIcons[i]) continue;
        float absD = 0.f;
        float lineAlpha0 = 1.f;
        dynamicIconRect(i, nullptr, &lineAlpha0, &absD);
        const bool inGrid = i >= gridStart && i < gridEnd;
        if (!inGrid && lineAlpha0 <= 0.004f) continue;
        candidates.push_back({i, absD});
    }
    
    // Far icons first so the centred one ends up on top, as in the carousel.
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.absD > rhs.absD; });

    nxui::Rect clip = m_rect;
    clip.y = 0.f;
    clip.height = 720.f;
    ren.pushClipRect(clip);
    for (const auto& c : candidates) {
        auto& icon = m_allIcons[c.index];
        float lineAlpha = 1.f;
        const nxui::Rect lineRect = dynamicIconRect(c.index, nullptr, &lineAlpha);
        const bool inGrid = c.index >= gridStart && c.index < gridEnd;
        const nxui::Rect gridRect = inGrid ? gridSlotRect(c.index) : lineRect;
        const float gridAlpha = inGrid ? 1.f : 0.f;

        const nxui::Rect blended = nxui::Rect::lerp(gridRect, lineRect, t);
        // The morph can start and end off-screen but cross it in between, so
        // cull on the blended frame rather than on either endpoint.
        constexpr float kCullMargin = 32.f;
        if (blended.right() < -kCullMargin || blended.x > 1280.f + kCullMargin
            || blended.bottom() < -kCullMargin || blended.y > 720.f + kCullMargin)
            continue;

        const nxui::Rect savedRect = icon->rect();
        const float savedOp = icon->opacity();
        icon->setRect(blended);
        icon->setOpacity(savedOp * nxui::lerpf(gridAlpha, lineAlpha, t));
        icon->setScrollLowDetail(true);
        icon->render(ren);
        icon->setScrollLowDetail(false);
        icon->setRect(savedRect);
        icon->setOpacity(savedOp);
    }
    ren.popClipRect();
}

void IconGrid::render(nxui::Renderer& ren) {
    if (!m_visible || m_opacity <= 0.f) return;

    // No grid child samples the scene any more: every tile is flat artwork plus
    // its own rim, so the old per-frame full-screen capture only cost a pass.

    if (m_layoutMorphing) {
        renderLayoutMorph(ren);
        return;
    }

    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        renderDynamicLine(ren);
        return;
    }

    if (m_sliding || m_bumping) {
        ren.pushClipRect(m_rect);
        if (m_sliding)
            renderPageAt(ren, m_slidePrevPage, m_slideOutDx);
        const int start = m_page * iconsPerPage();
        const int end = std::min(start + iconsPerPage(),
                                 static_cast<int>(m_allIcons.size()));
        for (int i = start; i < end; ++i) {
            if (m_allIcons[static_cast<std::size_t>(i)])
                m_allIcons[static_cast<std::size_t>(i)]->setScrollLowDetail(true);
        }
        for (auto& c : m_children) c->render(ren);
        for (int i = start; i < end; ++i) {
            if (m_allIcons[static_cast<std::size_t>(i)])
                m_allIcons[static_cast<std::size_t>(i)]->setScrollLowDetail(false);
        }
        if (m_bumping) {
            // Positive bump shifts the page right and exposes the left margin;
            // negative shifts it left and exposes the right margin.
            const float bump = m_edgeBump.value();
            drawEdgeShadow(ren, m_rect,
                           std::clamp(bump / kEdgeBumpDistance, 0.f, 1.f),
                           std::clamp(-bump / kEdgeBumpDistance, 0.f, 1.f));
        }
        ren.popClipRect();
        return;
    }

    for (auto& c : m_children) c->render(ren);
}

void IconGrid::onRender(nxui::Renderer&) {
}

void IconGrid::setVerticalInsets(float top, float bottom) {
    top = std::max(0.f, top);
    bottom = std::max(0.f, bottom);
    if (std::abs(m_insetTop - top) < 0.5f && std::abs(m_insetBottom - bottom) < 0.5f)
        return;
    m_insetTop = top;
    m_insetBottom = bottom;
    m_lineLayoutCacheCount = -1;
    const float gridH = m_rows * m_cellH + (m_rows - 1) * m_padY;
    const float availH = std::max(0.f, m_rect.height - m_insetTop - m_insetBottom);
    m_originY = m_rect.y + m_insetTop + (availH - gridH) * 0.5f;
    if (m_layoutMode == AppLayoutMode::DynamicLine)
        layoutLine();
    else
        positionPage(m_page, 0.f);
}
