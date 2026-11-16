#pragma once

#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace switchu::ui::highlight {

// The accent selected in Settings is shared by every focus ring in the UI.
// Keeping it here means new cursors pick up the gradient without each screen
// having to copy the config into its own widget.
struct Style {
    bool enabled = false;
    nxui::Color a {0.f, 0.75f, 1.f, 1.f};
    nxui::Color b {0.65f, 0.40f, 1.f, 1.f};
    float thicknessPx = 3.f;
};

inline Style& style() {
    static Style s;
    return s;
}

inline void configure(bool enabled, const nxui::Color& a, const nxui::Color& b,
                      float thicknessPx) {
    Style& s = style();
    s.enabled = enabled;
    s.a = a;
    s.b = b;
    s.thicknessPx = std::clamp(thicknessPx, 1.f, 8.f);
}

inline bool gradientEnabled() {
    return style().enabled;
}

inline float thicknessPx() {
    return style().thicknessPx;
}

inline nxui::Color colorAt(float t, float alpha) {
    const Style& s = style();
    const float wave = 0.5f - 0.5f * std::cos(t * 6.28318530718f);
    return {
        s.a.r + (s.b.r - s.a.r) * wave,
        s.a.g + (s.b.g - s.a.g) * wave,
        s.a.b + (s.b.b - s.a.b) * wave,
        std::clamp(alpha, 0.f, 1.f)
    };
}

inline nxui::Vec2 roundedRectPoint(const nxui::Rect& r, float radius, float t) {
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

inline nxui::Vec2 roundedRectNormal(const nxui::Rect& r, float radius, float t) {
    constexpr float kEpsilon = 0.0008f;
    float before = t - kEpsilon;
    float after = t + kEpsilon;
    if (before < 0.f)
        before += 1.f;
    if (after >= 1.f)
        after -= 1.f;
    const nxui::Vec2 d = roundedRectPoint(r, radius, after) -
                         roundedRectPoint(r, radius, before);
    const float length = std::sqrt(d.x * d.x + d.y * d.y);
    if (length <= 0.00001f)
        return {0.f, -1.f};
    // The ring travels clockwise, so the outward normal is (dy, -dx).
    return {d.y / length, -d.x / length};
}

inline nxui::Color withAlpha(const nxui::Color& c, float alpha) {
    return {c.r, c.g, c.b, std::clamp(alpha, 0.f, 1.f)};
}

inline void drawGradientTriangle(nxui::Renderer& ren,
                                 const nxui::Vec2& p0, const nxui::Vec2& p1,
                                 const nxui::Vec2& p2, const nxui::Color& c0,
                                 const nxui::Color& c1, const nxui::Color& c2) {
    ren.drawGradientTriangle(p0, p1, p2, c0, c1, c2);
}

inline void drawGradientRing(nxui::Renderer& ren, const nxui::Rect& rect,
                             float radius, float alpha, float thicknessPx,
                             const nxui::Color& a, const nxui::Color& b,
                             float time) {
    if (alpha <= 0.001f || rect.width <= 1.f || rect.height <= 1.f ||
        thicknessPx <= 0.f)
        return;

    constexpr int kSegments = 192;
    const float pulse = 0.94f + 0.06f * std::sin(time * 2.4f);
    const float thickness = std::clamp(
        thicknessPx * pulse, 1.f,
        std::max(1.f, std::min(rect.width, rect.height) * 0.25f));
    const float rad = std::clamp(radius, 0.f, std::min(rect.width, rect.height) * 0.5f);
    const float phase = std::fmod(time * 0.32f, 1.f);

    std::array<nxui::Vec2, kSegments + 1> points {};
    std::array<nxui::Vec2, kSegments + 1> normals {};
    for (int i = 0; i <= kSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        points[(std::size_t)i] = roundedRectPoint(rect, rad, t);
        normals[(std::size_t)i] = roundedRectNormal(rect, rad, t);
    }

    const auto vertex = [&](int i, float offset) {
        return points[(std::size_t)i] + normals[(std::size_t)i] * offset;
    };
    const auto color = [&](int i, float vertexAlpha) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments) + phase;
        const float wave = 0.5f - 0.5f * std::cos(t * 6.28318530718f);
        return nxui::Color {
            a.r + (b.r - a.r) * wave,
            a.g + (b.g - a.g) * wave,
            a.b + (b.b - a.b) * wave,
            std::clamp(vertexAlpha, 0.f, 1.f)
        };
    };

    // Ribbon edges share endpoints, so the ring has no butt-cap gaps around
    // corners. One-pixel alpha feathers on both sides keep the silhouette soft.
    const auto ribbon = [&](float outerOffset, float innerOffset,
                            float outerAlpha, float innerAlpha) {
        for (int i = 0; i < kSegments; ++i) {
            const nxui::Vec2 v00 = vertex(i, outerOffset);
            const nxui::Vec2 v10 = vertex(i + 1, outerOffset);
            const nxui::Vec2 v11 = vertex(i + 1, innerOffset);
            const nxui::Vec2 v01 = vertex(i, innerOffset);
            const nxui::Color c00 = color(i, outerAlpha);
            const nxui::Color c10 = color(i + 1, outerAlpha);
            const nxui::Color c11 = color(i + 1, innerAlpha);
            const nxui::Color c01 = color(i, innerAlpha);
            drawGradientTriangle(ren, v00, v10, v11, c00, c10, c11);
            drawGradientTriangle(ren, v00, v11, v01, c00, c11, c01);
        }
    };

    ribbon(1.f, 0.f, 0.f, alpha);
    ribbon(-thickness, -thickness - 1.f, alpha, 0.f);
    ribbon(0.f, -thickness, alpha, alpha);
}

inline void drawGradientRing(nxui::Renderer& ren, const nxui::Rect& rect,
                             float radius, float alpha, float time) {
    const Style& s = style();
    if (!s.enabled)
        return;
    drawGradientRing(ren, rect, radius, alpha, s.thicknessPx, s.a, s.b, time);
}

} // namespace switchu::ui::highlight
