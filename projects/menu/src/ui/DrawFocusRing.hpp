#pragma once

#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>

#include "ChromeTokens.hpp"
#include "FocusHighlight.hpp"

#include <algorithm>

namespace switchu::ui::chrome {

// One focus treatment for every surface: card expanded by the shared amount,
// a two-pixel ring, and the same soft outer glow. Screens pass only their
// shape radius and the solid fallback colour.
inline void drawFocusRing(nxui::Renderer& ren, const nxui::Rect& rect,
                          float radius, float alpha, float time,
                          const nxui::Color& solid) {
    if (alpha <= 0.001f || rect.width <= 1.f || rect.height <= 1.f)
        return;

    const bool gradient = switchu::ui::highlight::gradientEnabled();
    const nxui::Color ring = gradient
        ? switchu::ui::highlight::colorAt(0.f, alpha)
        : solid.withAlpha(alpha);
    const nxui::Rect ringRect = rect.expanded(kFocusExpansion);
    const float ringRadius = std::max(0.f, radius + kFocusExpansion);

    for (int i = 3; i >= 1; --i) {
        const float t = static_cast<float>(i) / 3.f;
        const float spread = kFocusGlowSpread * t;
        ren.drawRoundedRectOutline(
            ringRect.expanded(spread),
            ring.withAlpha(kFocusGlowAlpha * (1.05f - t * 0.55f) * alpha),
            ringRadius + spread, kFocusRingWidth);
    }

    if (gradient) {
        switchu::ui::highlight::drawGradientRing(
            ren, ringRect, ringRadius, kFocusRingAlpha * alpha, time);
    } else {
        ren.drawRoundedRectOutline(ringRect, ring.withAlpha(kFocusRingAlpha * alpha),
                                   ringRadius, kFocusRingWidth);
    }
}

} // namespace switchu::ui::chrome
