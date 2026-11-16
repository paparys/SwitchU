#pragma once

#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>
#include <nxui/Theme.hpp>

#include "ChromeTokens.hpp"
#include "KeycapHints.hpp"

#include <algorithm>
#include <string>

namespace switchu::ui::shell {

inline constexpr float kScreenW = 1280.f;
inline constexpr float kScreenH = 720.f;

// Full-screen page shell. All Apps and the fullscreen editors share this
// geometry instead of carrying 56/92/50, 30/116/34 and 22/58/64 variants.
inline constexpr float kPagePad = 56.f;
inline constexpr float kPageHeaderH = 92.f;
inline constexpr float kPageFooterH = 50.f;
inline constexpr float kPageMetaH = 30.f;
inline constexpr float kPageBottomGap = 10.f;

// Floating editor/modal shell. The inner padding is shared by Settings and the
// Theme Shop header/footer; Quick Settings uses the same pad around its panel.
inline constexpr float kOverlayPad = 30.f;
inline constexpr float kOverlayFooterH = 34.f;

struct PageFrame {
    nxui::Rect header;
    nxui::Rect content;
    nxui::Rect meta;
    nxui::Rect footer;
};

inline PageFrame pageFrame(float screenW = kScreenW, float screenH = kScreenH) {
    const float contentTop = kPageHeaderH;
    const float footerTop = screenH - kPageFooterH;
    const float metaTop = footerTop - kPageMetaH - kPageBottomGap;
    return {
        {0.f, 0.f, screenW, kPageHeaderH},
        {kPagePad, contentTop, screenW - kPagePad * 2.f,
         metaTop - contentTop},
        {kPagePad, metaTop, screenW - kPagePad * 2.f, kPageMetaH + 6.f},
        {kPagePad, footerTop, screenW - kPagePad * 2.f, kPageFooterH},
    };
}

inline void drawHeaderRule(nxui::Renderer& ren, const nxui::Rect& header,
                           const nxui::Color& line, const nxui::Color& accent,
                           float alpha) {
    const float y = header.bottom() - 8.f;
    ren.drawRect({header.x + kPagePad, y,
                  std::max(0.f, header.width - kPagePad * 2.f), 1.f},
                 line.withAlpha(0.07f * alpha));
    ren.drawRoundedRect({header.x + kPagePad, y - 1.f, 48.f, 3.f},
                        accent.withAlpha(0.55f * alpha), 1.5f);
}

struct OverlayHeaderSpec {
    nxui::Rect panel;
    const nxui::Theme* theme = nullptr;
    nxui::Font* font = nullptr;
    nxui::Font* smallFont = nullptr;
    std::string eyebrow;
    std::string headline;
    std::string description;
    float height = 116.f;
    float opacity = 1.f;
    float pad = kOverlayPad;
};

inline void drawOverlayHeader(nxui::Renderer& ren,
                              const OverlayHeaderSpec& spec) {
    if (!spec.theme || !spec.font || !spec.smallFont)
        return;
    const float x = spec.panel.x + spec.pad;
    const float textX = x + 16.f;
    const nxui::Color accent = spec.theme->cursorNormal;

    ren.drawRoundedRect({x, spec.panel.y + 35.f, 3.5f, 34.f},
                        accent.withAlpha(0.88f * spec.opacity), 1.75f);
    if (!spec.eyebrow.empty()) {
        ren.drawText(spec.eyebrow, {textX, spec.panel.y + 20.f},
                     spec.smallFont,
                     spec.theme->textSecondary.withAlpha(0.72f * spec.opacity),
                     chrome::kTypeSection);
    }
    if (!spec.headline.empty()) {
        ren.drawText(spec.headline, {textX, spec.panel.y + 38.f},
                     spec.font,
                     spec.theme->textPrimary.withAlpha(spec.opacity),
                     chrome::kTypeHeader);
    }

    const nxui::Rect shelf{x, spec.panel.y + 70.f,
                           std::max(0.f, spec.panel.width - spec.pad * 2.f),
                           34.f};
    if (!spec.description.empty()) {
        ren.drawRoundedRect(shelf,
                            spec.theme->panelBase.withAlpha(0.28f * spec.opacity),
                            chrome::kRadiusControl);
        ren.drawRoundedRectOutline(
            shelf, spec.theme->panelBorder.withAlpha(0.12f * spec.opacity),
            chrome::kRadiusControl, 1.f);
        ren.drawRoundedRect({shelf.x + 9.f, shelf.y + 9.f, 3.f, 16.f},
                            accent.withAlpha(0.82f * spec.opacity), 1.5f);
    }

    ren.pushClipRect({shelf.x + 18.f, shelf.y + 2.f,
                      std::max(0.f, shelf.width - 24.f),
                      shelf.height - 4.f});
    if (!spec.description.empty()) {
        ren.drawText(spec.description, {shelf.x + 20.f, shelf.y + 7.f},
                     spec.smallFont,
                     spec.theme->textSecondary.withAlpha(0.88f * spec.opacity),
                     chrome::kTypeRow);
    }
    ren.popClipRect();

    const float dividerY = spec.panel.y + spec.height - 10.f;
    ren.drawRect({x, dividerY,
                  std::max(0.f, spec.panel.width - spec.pad * 2.f), 1.f},
                 spec.theme->panelBorder.withAlpha(0.18f * spec.opacity));
    ren.drawRoundedRect({x, dividerY - 1.f, 48.f, 3.f},
                        accent.withAlpha(0.55f * spec.opacity), 1.5f);
}

struct OverlayFooterSpec {
    nxui::Rect panel;
    const nxui::Theme* theme = nullptr;
    nxui::Font* font = nullptr;
    std::string hints;
    std::string note;
    float height = kOverlayFooterH;
    float opacity = 1.f;
    float pad = kOverlayPad;
};

inline void drawOverlayFooter(nxui::Renderer& ren,
                              const OverlayFooterSpec& spec) {
    if (!spec.theme || !spec.font)
        return;
    const float top = spec.panel.bottom() - spec.height;
    const float x = spec.panel.x + spec.pad;
    const nxui::Color accent = spec.theme->cursorNormal;
    ren.drawRect({x, top, std::max(0.f, spec.panel.width - spec.pad * 2.f), 1.f},
                 spec.theme->panelBorder.withAlpha(0.20f * spec.opacity));
    ren.drawRoundedRect({x, top - 1.f, 32.f, 3.f},
                        accent.withAlpha(0.45f * spec.opacity), 1.5f);

    float noteWidth = 0.f;
    if (!spec.note.empty()) {
        noteWidth = spec.font->measure(spec.note).x * chrome::kTypeSection;
    }
    const float maxHintWidth = std::max(
        0.f, spec.panel.width - spec.pad * 2.f - noteWidth -
                 (noteWidth > 0.f ? 24.f : 0.f));
    chrome::drawHintString(ren, spec.font, spec.hints, x, top + 7.f,
                           spec.theme->textSecondary,
                           0.78f * spec.opacity, maxHintWidth);

    if (!spec.note.empty()) {
        ren.drawText(spec.note,
                     {spec.panel.right() - spec.pad - noteWidth, top + 7.f},
                     spec.font,
                     spec.theme->textSecondary.withAlpha(0.52f * spec.opacity),
                     chrome::kTypeSection);
    }
}

} // namespace switchu::ui::shell
