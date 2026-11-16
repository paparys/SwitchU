#pragma once

#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>
#include "ChromeTokens.hpp"

#include <string>

// Shared chrome drawing so dialogs, panels and rows cannot drift apart again.
namespace switchu::ui::chrome {

// Soft drop shadow under a panel: three barely-there halos plus a contact
// shadow. Cheaper than a blur pass and reads as one soft edge at 720p.
inline void drawPanelShadow(nxui::Renderer& ren, const nxui::Rect& rect,
                            float radius, float opacity) {
    if (opacity <= 0.01f)
        return;

    for (int i = 3; i >= 1; --i) {
        const float t = static_cast<float>(i) / 3.f;
        nxui::Rect s = rect;
        const float grow = kShadowSpread * t * 0.35f;
        s.x -= grow;
        s.y += kShadowOffset * 0.5f * t;
        s.width += grow * 2.f;
        s.height += grow * 2.f;
        ren.drawRoundedRect(s,
                            nxui::Color(0.f, 0.f, 0.f, kShadowAlpha * 0.28f * opacity * (1.05f - t)),
                            radius + kShadowSpread * t * 0.22f);
    }

    nxui::Rect contact = rect;
    contact.y += 2.f;
    ren.drawRoundedRect(contact,
                        nxui::Color(0.f, 0.f, 0.f, kShadowAlpha * 0.55f * opacity),
                        radius);
}

// Cap-height centring. Centring the text box pushes labels visually low
// because the box carries descender space; rows look aligned once the cap
// height is centred instead.
inline float capCenteredY(const nxui::Font& font, const nxui::Rect& rect,
                          const std::string& text, float scale) {
    const nxui::Vec2 sz = font.measure(text);
    return rect.y + (rect.height - sz.y * scale * 0.72f) * 0.5f;
}

inline void drawTextCapCentered(nxui::Renderer& ren, const std::string& text,
                                const nxui::Rect& rect, nxui::Font* font,
                                const nxui::Color& color, float scale) {
    if (!font || text.empty())
        return;
    ren.drawText(text, {rect.x, capCenteredY(*font, rect, text, scale)},
                 font, color, scale);
}

} // namespace switchu::ui::chrome
