#include "PageIndicator.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

PageIndicator::PageIndicator() {
    setPadding(10.f, 18.f, 10.f, 18.f);
}

void PageIndicator::setPageCount(int total) {
    int clampedTotal = std::max(1, total);
    if (clampedTotal == m_total && m_geometryReady) return;

    m_total = clampedTotal;
    m_current = std::clamp(m_current, 0, m_total - 1);
    updateGeometryFromContent();
}

void PageIndicator::setCurrentPage(int page) {
    int clamped = std::clamp(page, 0, std::max(0, m_total - 1));
    if (clamped == m_current) return;
    m_current = clamped;
    m_popT = 0.f;
    if (!m_geometryReady)
        m_slide.setImmediate(static_cast<float>(clamped));
    else
        m_slide.set(static_cast<float>(clamped), 0.22f, nxui::Easing::outCubic);
}

void PageIndicator::onUpdate(float dt) {
    m_slide.update(dt);
    if (m_popT < kDotPopDuration)
        m_popT = std::min(kDotPopDuration, m_popT + dt);
}

void PageIndicator::updateGeometryFromContent() {
    if (m_rect.width > 0.f)
        m_layoutWidth = std::max(m_layoutWidth, m_rect.width);

    float topY = m_rect.y;
    sizeToFit();
    setCornerRadius(m_rect.height * 0.5f);
    m_rect.x = (m_layoutWidth - m_rect.width) * 0.5f;
    m_rect.y = topY;
    m_geometryReady = true;
}

void PageIndicator::onContentRender(nxui::Renderer& ren) {
    nxui::Rect cr = contentRect();
    if (m_total <= 1) return;

    // One capsule, not a row of dots: a low track with a single bright pill that
    // slides to the active page. Stretching a pill reads as a position on a line
    // rather than as N unrelated pips, and it cannot go out of sync with the
    // page slide because it is driven by the same page index.
    constexpr float kSegW = 18.f;
    constexpr float kGap  = 6.f;
    constexpr float kH    = 6.f;

    const float trackW = m_total * kSegW + (m_total - 1) * kGap;
    const float x0 = cr.x + (cr.width - trackW) * 0.5f;
    const float top = cr.y + cr.height * 0.5f - kH * 0.5f;
    const float op = m_opacity;

    nxui::Color inactive = m_theme ? m_theme->pageIndicator
                                   : nxui::Color(1.f, 1.f, 1.f, 0.35f);
    nxui::Color active   = m_hasActiveColor
        ? m_activeColor
        : (m_theme ? m_theme->pageIndicatorActive
                   : nxui::Color(1.f, 1.f, 1.f, 0.95f));

    ren.drawRoundedRect({x0, top, trackW, kH},
                        inactive.withAlpha(inactive.a * 0.55f * op), kH * 0.5f);

    const float slot = kSegW + kGap;
    const float pos = std::clamp(m_slide.value(), 0.f, static_cast<float>(m_total - 1));
    const float px = x0 + pos * slot;
    ren.drawRoundedRect({px, top, kSegW, kH},
                        active.withAlpha(active.a * op), kH * 0.5f);
}

nxui::Vec2 PageIndicator::computeContentSize() const {
    if (m_total <= 1) return {0.f, 0.f};
    constexpr float kSegW = 18.f;
    constexpr float kGap  = 6.f;
    constexpr float kH    = 6.f;
    return {m_total * kSegW + (m_total - 1) * kGap, kH};
}
