#include "GameSearchBar.hpp"

#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>

namespace {

nxui::Color mixColor(const nxui::Color& base, const nxui::Color& tint, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return nxui::Color(base.r + (tint.r - base.r) * t,
                       base.g + (tint.g - base.g) * t,
                       base.b + (tint.b - base.b) * t,
                       base.a);
}

} // namespace

GameSearchBar::GameSearchBar() {
    setTag("gameSearchBar");
    setFocusable(true);
    setAccessibilityRole("button");
}

void GameSearchBar::setQuery(const std::string& query, int resultCount) {
    if (m_query == query && m_resultCount == resultCount)
        return;
    m_query = query;
    m_resultCount = resultCount;
}

void GameSearchBar::onRender(nxui::Renderer& ren) {
    if (!isVisible() || m_rect.width <= 1.f || m_rect.height <= 1.f
        || m_opacity <= 0.01f)
        return;

    const float alpha = m_opacity;
    const float radius = std::min(m_rect.height * 0.5f, 14.f);

    // Match the strip's shortcut language: a 56x56 socket with a centred
    // glyph. When the shared strip plate is visible, the icon contributes no
    // pill of its own, exactly like the applet buttons beside it.
    if (m_chromeVisible) {
        nxui::Color panel = m_panel;
        if (ren.opacityTintEnabled()) {
            const float mix = std::max(0.35f,
                std::clamp(ren.panelTintMixPct() / 100.f, 0.f, 1.f));
            panel = mixColor(panel, ren.opacityTint(), mix);
        }
        ren.drawRoundedRect(m_rect, panel.withAlpha(panel.a * alpha), radius);
        ren.drawRoundedRectOutline(m_rect, m_border.withAlpha(m_border.a * alpha),
                                   radius, 1.2f);
    }

    const float size = std::min(m_rect.width, m_rect.height);
    const nxui::Color ink = (m_query.empty() ? m_muted : m_text)
        .withAlpha((m_query.empty() ? m_muted.a : m_text.a) * alpha);
    const nxui::Vec2 centre{m_rect.x + m_rect.width * 0.5f,
                            m_rect.y + m_rect.height * 0.5f};
    // Magnifier: an actual ring, so it reads as line art on the plate instead
    // of a solid dot. Twenty short segments keep it smooth at 56 px.
    const float ringRadius = size * 0.155f;
    const float ringX = centre.x - size * 0.035f;
    const float ringY = centre.y - size * 0.035f;
    constexpr int kSegments = 20;
    nxui::Vec2 previous{ringX + ringRadius, ringY};
    for (int i = 1; i <= kSegments; ++i) {
        const float angle = (static_cast<float>(i) / kSegments)
                          * 6.28318530718f;
        const nxui::Vec2 point{ringX + std::cos(angle) * ringRadius,
                               ringY + std::sin(angle) * ringRadius};
        ren.drawLine(previous, point, ink, 2.f);
        previous = point;
    }
    const float handleStart = 0.7071f * ringRadius;
    ren.drawLine({ringX + handleStart, ringY + handleStart},
                 {ringX + handleStart + size * 0.11f,
                  ringY + handleStart + size * 0.11f}, ink, 2.2f);
    // A small dot is the only state adornment; the results list carries the
    // count. Keeping the socket icon-sized is the point of this layout.
    if (hasQuery()) {
        ren.drawCircle({m_rect.right() - size * 0.18f,
                        m_rect.y + size * 0.18f}, 2.6f, ink, 12);
    }
}
