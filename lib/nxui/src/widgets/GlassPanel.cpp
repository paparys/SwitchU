#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace nxui {

GlassPanel::GlassPanel() {}

namespace {

void drawPanelMaterialTexture(Renderer& ren, const Rect& r, float radius,
                              float opacity, float intensity) {
    float mat = std::clamp(opacity * intensity, 0.f, 1.f);
    if (mat <= 0.004f)
        return;

    float innerRadius = std::max(0.f, radius - 1.5f);
    Rect inner = r.shrunk(1.5f);
    Rect lower = {
        inner.x + 1.f,
        inner.y + inner.height * 0.48f,
        std::max(0.f, inner.width - 2.f),
        std::max(0.f, inner.height * 0.50f)
    };

    ren.drawRoundedRect(inner, Color::white().withAlpha(0.030f * mat), innerRadius);
    ren.drawRoundedRect(lower, Color::black().withAlpha(0.040f * mat),
                        std::max(0.f, innerRadius * 0.72f));
    ren.drawRoundedRectOutline(inner,
                               Color::white().withAlpha(0.090f * mat),
                               innerRadius,
                               1.f);
    ren.drawRoundedRectOutline(r.shrunk(3.f),
                               Color::black().withAlpha(0.050f * mat),
                               std::max(0.f, radius - 3.f),
                               1.f);

    Rect grain = r.shrunk(5.f);
    if (grain.width <= 4.f || grain.height <= 4.f)
        return;

    float step = std::clamp(grain.height * 0.28f, 9.f, 16.f);
    int lineCount = std::min(22, std::max(3, (int)((grain.width + grain.height) / step)));
    float diag = grain.height * 0.72f;
    for (int i = 0; i < lineCount; ++i) {
        float x0 = grain.x - grain.height * 0.45f + i * step;
        float x1 = x0 + diag;
        if (x1 < grain.x || x0 > grain.right())
            continue;
        Color c = (i % 2 == 0)
            ? Color::white().withAlpha(0.020f * mat)
            : Color::black().withAlpha(0.014f * mat);
        ren.drawLine({std::max(grain.x, x0), grain.bottom() - 2.f},
                     {std::min(grain.right(), x1), grain.y + 2.f},
                     c,
                     1.f);
    }

    int speckles = std::min(9, std::max(2, (int)(grain.width / 34.f)));
    for (int i = 0; i < speckles; ++i) {
        float t = (float)(i + 1) / (float)(speckles + 1);
        float x = grain.x + std::fmod((t * 37.0f + r.x * 0.013f), 1.0f) * grain.width;
        float y = grain.y + std::fmod((t * 23.0f + r.y * 0.017f), 1.0f) * grain.height;
        Color c = (i % 2 == 0)
            ? Color::white().withAlpha(0.026f * mat)
            : Color::black().withAlpha(0.020f * mat);
        ren.drawCircle({x, y}, 0.8f, c, 8);
    }
}

// Rounded-rect perimeter point at t in [0, 1], clockwise from the top-left
// straight edge. Used by the gradient fill so its silhouette matches the flat
// rounded fill exactly.
Vec2 roundedFillPoint(const Rect& r, float radius, float t) {
    const float cr = std::max(0.001f,
        std::clamp(radius, 0.f, std::min(r.width, r.height) * 0.5f));
    const float top = std::max(0.f, r.width - cr * 2.f);
    const float side = std::max(0.f, r.height - cr * 2.f);
    const float arc = cr * 1.57079632679f;
    const float total = top * 2.f + side * 2.f + arc * 4.f;
    if (total <= 0.001f)
        return {r.x + r.width * 0.5f, r.y + r.height * 0.5f};

    float d = std::clamp(t, 0.f, 1.f) * total;
    if (d < top)
        return {r.x + cr + d, r.y};
    d -= top;
    if (d < arc) {
        const float a = -1.57079632679f + d / cr;
        return {r.x + r.width - cr + std::cos(a) * cr,
                r.y + cr + std::sin(a) * cr};
    }
    d -= arc;
    if (d < side)
        return {r.x + r.width, r.y + cr + d};
    d -= side;
    if (d < arc) {
        const float a = d / cr;
        return {r.x + r.width - cr + std::cos(a) * cr,
                r.y + r.height - cr + std::sin(a) * cr};
    }
    d -= arc;
    if (d < top)
        return {r.x + r.width - cr - d, r.y + r.height};
    d -= top;
    if (d < arc) {
        const float a = 1.57079632679f + d / cr;
        return {r.x + cr + std::cos(a) * cr,
                r.y + r.height - cr + std::sin(a) * cr};
    }
    d -= arc;
    if (d < side)
        return {r.x, r.y + r.height - cr - d};
    d -= side;
    const float a = 3.14159265359f + d / cr;
    return {r.x + cr + std::cos(a) * cr,
            r.y + cr + std::sin(a) * cr};
}

void drawAnimatedGradientFill(Renderer& ren, const Rect& r, float radius,
                              const Color& a, const Color& b, float phase,
                              float alpha) {
    if (alpha <= 0.001f || r.width <= 0.5f || r.height <= 0.5f)
        return;
    const float cr = std::max(0.001f,
        std::clamp(radius, 0.f, std::min(r.width, r.height) * 0.5f));
    constexpr int kSegments = 160;
    const auto colorAt = [&](float x) {
        float t = (x - r.x) / std::max(1.f, r.width) + phase;
        t -= std::floor(t);
        const float wave = 0.5f - 0.5f * std::cos(t * 6.28318530718f);
        return Color {
            a.r + (b.r - a.r) * wave,
            a.g + (b.g - a.g) * wave,
            a.b + (b.b - a.b) * wave,
            std::clamp(alpha, 0.f, 1.f)
        };
    };

    const Vec2 center {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const Color centerColor = colorAt(center.x);
    Vec2 prev = roundedFillPoint(r, cr, 0.f);
    Color prevColor = colorAt(prev.x);
    for (int i = 1; i <= kSegments; ++i) {
        const Vec2 cur = roundedFillPoint(
            r, cr, static_cast<float>(i) / static_cast<float>(kSegments));
        const Color curColor = colorAt(cur.x);
        ren.drawGradientTriangle(center, prev, cur, centerColor, prevColor, curColor);
        prev = cur;
        prevColor = curColor;
    }
}

} // namespace

void GlassPanel::onRender(Renderer& ren) {
    if (m_scale < 0.01f) return;
    // GlassWidget draws the content after this call, so returning here leaves
    // the widget's contents visible with no chrome of its own.
    if (!m_backgroundVisible) return;

    Rect r = m_rect;
    if (std::abs(m_scale - 1.f) > 0.001f) {
        float w = r.width  * m_scale;
        float h = r.height * m_scale;
        r.x += (r.width  - w) * 0.5f;
        r.y += (r.height - h) * 0.5f;
        r.width  = w;
        r.height = h;
    }

    float op = m_opacity * m_panelOpacity;
    // Nothing in the plate is visible at zero opacity; skip its geometry. The
    // widget's content is drawn separately by GlassWidget::onRender.
    if (op <= 0.001f)
        return;

    // Large panels can float over artwork. Kept behind the backing so the
    // opaque layer still blocks the veil when one is enabled.
    const int shadowPct = std::clamp(ren.panelShadowPct(), 0, 100);
    if (m_panelShadowEnabled && shadowPct > 0 && op > 0.01f
        && r.width * r.height > 60000.f) {
        const float strength = shadowPct / 100.f;
        for (int layer = 0; layer < 3; ++layer) {
            const float spread = (2.f + static_cast<float>(layer) * 5.f) * strength;
            ren.drawRoundedRect(
                {r.x - spread, r.y + 2.f + spread * 0.45f,
                 r.width + spread * 2.f, r.height + spread * 2.f},
                Color(0.f, 0.f, 0.f, (0.16f - 0.045f * layer) * strength * op),
                m_radius + spread);
        }
    }

    // Opaque backing: fully blocks anything drawn underneath
    if (m_backingEnabled && op > 0.01f) {
        ren.drawRoundedRect(r, m_backingColor.withAlpha(op), m_radius);
    }

    const bool solidOpacity = ren.opacitySolid();
    if (m_blurEnabled && !solidOpacity && op > 0.01f) {
        // Capture and blur only this panel's region plus the kernel spread,
        // instead of the whole offscreen target.
        ren.captureSharedBackdrop(r, m_blurRadius, m_blurPasses);
        ren.drawOffscreenRounded(0, r, m_radius, Color::white().withAlpha(op));
    }

    // The flat fill is the user-facing opacity layer: the opacity colour
    // replaces its tint, and Make opacity solid lifts its alpha to 1.
    Color body = m_base;
    const bool tintEnabled = ren.opacityTintEnabled();
    const Color tint = ren.opacityTint();
    // Mix keeps the theme visible at 0 and hands the layer to the tint at
    // 100, so one slider covers both directions without a second colour.
    const float tintMix = tintEnabled
        ? std::clamp(ren.panelTintMixPct() / 100.f, 0.f, 1.f) : 0.f;
    const auto mixTowardTint = [&](Color c) {
        if (!tintEnabled || tintMix <= 0.001f)
            return c;
        return Color(c.r + (tint.r - c.r) * tintMix,
                     c.g + (tint.g - c.g) * tintMix,
                     c.b + (tint.b - c.b) * tintMix,
                     c.a);
    };
    body = mixTowardTint(body);
    if (solidOpacity)
        body.a = 1.f;
    body.a *= op;
    if (m_gradientEnabled) {
        // The shortcut strip already resolves its stops through the opacity
        // tint in SidebarManager. Mixing them toward the same tint a second
        // time here collapsed both ends to one colour at high panel-tint mixes,
        // turning the moving gradient into a flat plate. Pass the resolved
        // stops through and only carry the panel's final alpha.
        drawAnimatedGradientFill(ren, r, m_radius, m_gradientA, m_gradientB,
                                 m_gradientPhase, body.a);
    } else {
        ren.drawRoundedRect(r, body, m_radius);
    }

    const float grain = std::clamp(ren.panelGrainPct() / 100.f, 0.f, 1.f);
    if (m_materialTextureEnabled)
        drawPanelMaterialTexture(ren, r, m_radius, op, m_materialTextureIntensity);
    if (m_panelGrainEnabled && grain > 0.001f)
        drawPanelMaterialTexture(ren, r, m_radius, op, grain * 0.85f);

    const float borderScale = std::clamp(ren.panelBorderPct() / 100.f, 0.f, 2.f);
    if (m_panelHighlightEnabled && m_highlight.a > 0.01f && op > 0.01f) {
        ren.pushClipRect({r.x, r.y, r.width, 2.f});
        ren.drawRoundedRectOutline(r.shrunk(0.5f),
                                   m_highlight.withAlpha(m_highlight.a * 0.65f * op * borderScale),
                                   m_radius - 0.5f, 1.f);
        ren.popClipRect();
    }

    if (m_borderW > 0.f)
        ren.drawRoundedRectOutline(r, m_border.withAlpha(m_border.a * op * borderScale),
                                   m_radius, m_borderW);
}

} // namespace nxui
