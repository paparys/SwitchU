#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include "GridNavigation.hpp"
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

} // namespace

void IconGrid::setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
                     int cols, int rows,
                     float cellW, float cellH,
                     float padX, float padY)
{
    m_allIcons = std::move(icons);
    reconfigureLayout(cols, rows, cellW, cellH, padX, padY);
}

void IconGrid::setLayoutMode(AppLayoutMode mode, bool animate) {
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
        m_lineScrollOffset.setImmediate(cur >= 0 ? static_cast<float>(cur) : 0.f);
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
    const int cur = focusedGlobalIndex();
    const int perPage = std::max(1, iconsPerPage());
    m_layoutMorphPage = cur >= 0 ? cur / perPage : m_page;
    if (cur >= 0)
        m_lineScrollOffset.setImmediate(static_cast<float>(cur));
    if (m_layoutMode == AppLayoutMode::Grid && cur >= 0) {
        const int wanted = cur / perPage;
        if (wanted != m_page)
            setPage(wanted);
    }
}

bool IconGrid::isDynamicLineScrolling() const {
    return m_layoutMode == AppLayoutMode::DynamicLine
        && (m_lineDragActive
            || std::abs(m_lineScrollOffset.value() - m_lineScrollOffset.target()) > 0.01f);
}

float IconGrid::lineItemSpacing() const {
    // Depth tightens the pitch as the row moves back, so fewer pixels per
    // icon means more icons are visible at once.
    return (kLineIconBase * m_iconScale + std::max(8.f, m_padX * 0.4f))
         * (1.f + 0.11f * m_carouselDepth);
}

void IconGrid::beginLineDrag() {
    if (m_layoutMode != AppLayoutMode::DynamicLine || m_layoutMorphing)
        return;
    m_lineDragActive = true;
    m_lineDragStartOffset = m_lineScrollOffset.value();
    m_lineScrollOffset.setImmediate(m_lineDragStartOffset);
}

void IconGrid::dragLineToTouchDelta(float touchDeltaX) {
    if (!m_lineDragActive || m_layoutMode != AppLayoutMode::DynamicLine)
        return;
    const float spacing = std::max(1.f, lineItemSpacing());
    // Finger right → earlier titles (offset decreases); left → later titles.
    const float maxIdx = std::max(0.f, static_cast<float>(m_allIcons.size()) - 1.f);
    const float offset = std::clamp(m_lineDragStartOffset - touchDeltaX / spacing,
                                    0.f, maxIdx);
    m_lineScrollOffset.setImmediate(offset);
    m_lineLayoutCacheCount = -1;
}

int IconGrid::nearestFocusableToLineOffset() const {
    if (m_allIcons.empty())
        return -1;
    const float offset = m_lineScrollOffset.value();
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
    const int nearest = nearestFocusableToLineOffset();
    if (nearest < 0)
        return -1;
    // Short settle so release doesn't feel like a hard stop.
    constexpr float kDragSettleDuration = 0.22f;
    if (!focusGlobalIndex(nearest, false, kDragSettleDuration))
        return -1;
    return nearest;
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
        icon->setRect({x, y,
                       m_cellW * spanColumns + m_padX * (spanColumns - 1),
                       m_cellH * spanRows + m_padY * (spanRows - 1)});
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
    // still travelling
    if (m_layoutMorphing)
        return morphBlend(globalIndex, slot);
    if (m_layoutMode == AppLayoutMode::DynamicLine)
        return dynamicIconRect(globalIndex);
    return slot;
}

void IconGrid::layoutLine() {
    nxui::Widget* prevFocused = m_focus.current();
    clearChildren();

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

    std::vector<nxui::Widget*> fItems;
    fItems.reserve(m_allIcons.size());
    for (size_t i = 0; i < m_allIcons.size(); ++i) {
        auto& icon = m_allIcons[i];
        clearEdgePagingActions(*icon);
        int left = -1;
        for (int j = (int)i - 1; j >= 0; --j) {
            if (m_allIcons[(size_t)j] && m_allIcons[(size_t)j]->isFocusable()) {
                left = j;
                break;
            }
        }
        int right = -1;
        for (int j = (int)i + 1; j < (int)m_allIcons.size(); ++j) {
            if (m_allIcons[(size_t)j] && m_allIcons[(size_t)j]->isFocusable()) {
                right = j;
                break;
            }
        }
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
        const float t = std::clamp(m_slideT / motion::scaled(kSlideDuration, m_motionStyle),
                                   0.f, 1.f);
        const float trail = (1.f - t) * (1.f - t);
        stagger = trail * 30.f * static_cast<float>(m_slideDir);
    }
    for (int i = start; i < end; ++i) {
        const int local = i - start;
        auto& icon = m_allIcons[i];
        const int spanColumns = std::max(1, icon->gridSpanColumns());
        const int spanRows = std::max(1, icon->gridSpanRows());
        const float columnLag = stagger * static_cast<float>(local % std::max(1, m_cols));
        icon->setRect({m_originX + m_gridNudgeX + (local % m_cols) * (m_cellW + m_padX)
                           + dx + columnLag,
                       m_originY + (local / m_cols) * (m_cellH + m_padY),
                       m_cellW * spanColumns + m_padX * (spanColumns - 1),
                       m_cellH * spanRows + m_padY * (spanRows - 1)});
    }
}

float IconGrid::pageStride() const {
    const float gridW = m_cols * m_cellW + (m_cols - 1) * m_padX;
    return std::max(m_rect.width, (m_originX - m_rect.x) + gridW + m_padX);
}

int IconGrid::focusedGlobalIndex() const {
    auto* cur = m_focus.current();
    if (!cur)
        return -1;
    for (int i = 0; i < (int)m_allIcons.size(); ++i) {
        if (m_allIcons[i].get() == cur)
            return i;
    }
    return -1;
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
    // Halfway between the large (300@1.18) and clock-safe (268@1.06) sizes.
    constexpr float kPosterBaseW = 284.f;
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
        : (kLineIconBase * m_iconScale / std::max(0.2f, m_iconAspect));
    // Scroll pitch stays icon-sized so posters can hero-overlap neighbours.
    const float lineSpacing = lineItemSpacing();

    float d = static_cast<float>(index) - offset;
    const float absD = std::abs(d);
    // Depth shrinks or grows the row: at -1 side tiles are noticeably smaller
    // (more neighbours on screen), at +1 they are larger and fewer fit.
    float focusBlend = 0.f;
    if (absD <= 1.f) {
        // Position drives the visual state: the departing icon shrinks as it
        // leaves centre while the incoming icon travels and grows into it.
        const float centerBlend = 1.f - absD;
        focusBlend = centerBlend * centerBlend * (3.f - 2.f * centerBlend);
    }
    // Row scale at a given distance from the focus. Shared by this tile and by
    // the gap accumulation below, so the width used for layout is always the
    // width that gets drawn.
    auto scaleAt = [&](float absDist, bool isPoster) {
        const float side = (isPoster ? 0.72f : 0.82f) + 0.18f * m_carouselDepth;
        const float focus = (isPoster ? 1.12f : 1.36f) + 0.10f * m_carouselDepth;
        if (absDist <= 1.f) {
            const float cb = 1.f - absDist;
            return side + cb * cb * (3.f - 2.f * cb) * (focus - side);
        }
        const float far = absDist - 1.f;
        const float soften = 1.f - std::exp(-far * 1.1f);
        const float shrinkTo = 0.46f - 0.10f * std::max(0.f, m_carouselDepth);
        return std::max(shrinkTo, side - (side - shrinkTo) * soften);
    };
    auto isPosterAt = [&](int j) {
        if (!m_linePosterAspect || j < 0 || j >= static_cast<int>(m_allIcons.size()))
            return false;
        const auto& ic = m_allIcons[static_cast<std::size_t>(j)];
        return ic && ic->usesPosterFrame();
    };
    auto widthAt = [&](int j) {
        const bool p = isPosterAt(j);
        const float base = (p ? kPosterBaseW : kLineIconBase) * m_iconScale;
        return base * scaleAt(std::abs(static_cast<float>(j) - offset), p);
    };

    const float s = scaleAt(absD, poster);
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
        // the screen edge, heavy keeps only the next tile or two), and pulling
        // the row back stretches it so its extra visible tiles stay readable.
        const float far = absD - 1.f;
        const float span = (1.8f + 5.2f * (1.f - m_carouselFade))
                         * (1.f - 0.22f * m_carouselDepth);
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
    if (index != anchor) {
        const int dir = index > anchor ? 1 : -1;
        for (int j = anchor; j != index; j += dir)
            shift += dir * (0.5f * (widthAt(j) + widthAt(j + 1)) + gap);
    }
    if (frac > 0.0001f)
        shift -= frac * (0.5f * (widthAt(anchor) + widthAt(anchor + 1)) + gap);
    const float x = centerX + shift - w * 0.5f;
    // Shelf alignment: every tile rests on the focused tile's bottom edge, so
    // the smaller neighbours sit down beside the big one instead of floating at
    // their own centres. The shelf is taken from the anchor tile under the
    // focus, which is also what carries a focused poster up into the middle
    // band while its neighbours follow to the same line.
    const bool anchorPoster = isPosterAt(anchor);
    const float anchorBaseH = anchorPoster
        ? kPosterBaseW * 1.5f * m_iconScale
        : kLineIconBase * m_iconScale / std::max(0.2f, m_iconAspect);
    const float anchorScale = (anchorPoster ? 1.12f : 1.36f)
                            + 0.10f * m_carouselDepth;
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

nxui::Rect IconGrid::gridSlotRect(int globalIndex) const {
    if (globalIndex < 0 || globalIndex >= (int)m_allIcons.size())
        return {};
    const auto& icon = m_allIcons[(std::size_t)globalIndex];
    const int spanColumns = icon ? std::max(1, icon->gridSpanColumns()) : 1;
    const int spanRows    = icon ? std::max(1, icon->gridSpanRows())    : 1;
    const int local  = globalIndex % std::max(1, iconsPerPage());
    const int column = local % std::max(1, m_cols);
    const int row    = local / std::max(1, m_cols);
    return {m_originX + m_gridNudgeX + column * (m_cellW + m_padX),
            m_originY + row * (m_cellH + m_padY),
            m_cellW * spanColumns + m_padX * (spanColumns - 1),
            m_cellH * spanRows + m_padY * (spanRows - 1)};
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
        if (focused >= 0)
            return dynamicIconRect(focused);
    }
    if (auto* cur = m_focus.current())
        return cur->focusRect();
    return {};
}

bool IconGrid::focusGlobalIndex(int idx, bool instant, float durationSec) {
    if (idx < 0 || idx >= (int)m_allIcons.size())
        return false;
    if (!m_allIcons[idx] || !m_allIcons[idx]->isFocusable())
        return false;

    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        m_focus.setFocus(m_allIcons[idx].get());
        // Opening or closing a folder rebuilds the model while the carousel
        // still holds the offset of the other tree; animating from there would
        // visibly scroll through the whole row, so snap instead.
        if (instant)
            m_lineScrollOffset.setImmediate(static_cast<float>(idx));
        else {
            const float dur = durationSec > 0.f ? durationSec : kLineScrollDuration;
            m_lineScrollOffset.set(static_cast<float>(idx), motion::scaled(dur, m_motionStyle),
                                   motion::ease(m_motionStyle));
        }
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
        for (int i = 0; i < (int)m_allIcons.size(); ++i) {
            nxui::Rect r = dynamicIconRect(i);
            if (r.contains(screenX, screenY))
                return i;
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

void IconGrid::onUpdate(float dt) {
    if (m_layoutMorphing) {
        // AnimationManager advances the tween; this only clears the flag.
        if (std::abs(m_layoutMorph.value() - m_layoutMorph.target()) < 0.001f) {
            m_layoutMorph.setImmediate(m_layoutMorph.target());
            m_layoutMorphing = false;
        }
    }

    if (m_layoutMode == AppLayoutMode::DynamicLine) {
        m_lineScrollOffset.update(dt);
        int cur = focusedGlobalIndex();
        // While the finger owns the offset, do not re-anchor to focus.
        if (!m_lineDragActive
            && cur >= 0
            && std::abs(m_lineScrollOffset.target() - static_cast<float>(cur)) > 0.001f) {
            m_lineScrollOffset.set(static_cast<float>(cur),
                                   motion::scaled(kLineScrollDuration, m_motionStyle),
                                   motion::ease(m_motionStyle));
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
            for (int i = 0; i < (int)m_allIcons.size(); ++i) {
                m_allIcons[i]->setRect(dynamicIconRect(i));
            }
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
    const float t = std::clamp(m_slideT / motion::scaled(kSlideDuration, m_motionStyle),
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
        icon->setRect({m_originX + m_gridNudgeX + (local % m_cols) * (m_cellW + m_padX) + dx,
                       m_originY + (local / m_cols) * (m_cellH + m_padY),
                       m_cellW * spanColumns + m_padX * (spanColumns - 1),
                       m_cellH * spanRows + m_padY * (spanRows - 1)});
        icon->render(ren);
        icon->setRect(saved);
    }
}

void IconGrid::renderDynamicLine(nxui::Renderer& ren) {
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

    auto& candidates = m_lineRenderScratch;
    candidates.clear();
    candidates.reserve(m_allIcons.size());

    for (int i = 0; i < (int)m_allIcons.size(); ++i) {
        float s = 1.f;
        float a = 1.f;
        float absD = 0.f;
        const nxui::Rect r = dynamicIconRect(i, &s, &a, &absD);
        // Cull on the alpha the curve actually produced instead of a fixed tile
        // distance: the fade now reaches zero on its own, so the last visible
        // tile is the one the curve zeroed and nothing pops at an arbitrary edge.
        if (a <= 0.004f && i != focusedIndex) continue;
        const float d = r.center().x - m_rect.center().x;
        candidates.push_back({i, absD, d, s, a, r});
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.absD > rhs.absD;
    });

    for (const auto& c : candidates) {
        auto& icon = m_allIcons[c.index];
        const nxui::Rect savedRect = icon->rect();
        const float savedOp = icon->opacity();

        icon->setRect(c.rect);
        icon->setOpacity(savedOp * c.a);
        icon->render(ren);

        icon->setRect(savedRect);
        icon->setOpacity(savedOp);
    }

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

        const nxui::Rect savedRect = icon->rect();
        const float savedOp = icon->opacity();
        icon->setRect(nxui::Rect::lerp(gridRect, lineRect, t));
        icon->setOpacity(savedOp * nxui::lerpf(gridAlpha, lineAlpha, t));
        icon->render(ren);
        icon->setRect(savedRect);
        icon->setOpacity(savedOp);
    }
    ren.popClipRect();
}

void IconGrid::render(nxui::Renderer& ren) {
    if (!m_visible || m_opacity <= 0.f) return;

    if (!m_children.empty() && ren.gpu().offscreenReady())
        ren.captureToOffscreen(true);

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
        for (auto& c : m_children) c->render(ren);
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
