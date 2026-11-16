#include "TitlePillWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>

namespace {

void removeLastUtf8Codepoint(std::string& text) {
    if (text.empty()) return;
    std::size_t start = text.size() - 1;
    while (start > 0 &&
           (static_cast<unsigned char>(text[start]) & 0xC0u) == 0x80u)
        --start;
    text.erase(start);
}

std::string ellipsize(nxui::Font* font, const std::string& text, float maxWidth) {
    if (!font || font->measure(text).x <= maxWidth)
        return text;
    std::string fitted = text;
    constexpr const char* suffix = "...";
    while (!fitted.empty() && font->measure(fitted + suffix).x > maxWidth)
        removeLastUtf8Codepoint(fitted);
    return fitted.empty() ? suffix : fitted + suffix;
}

}

TitlePillWidget::TitlePillWidget() {
    // Flat, colourable pill: the liquid-glass surface only shaded the capsule.
    // The panel's own rim now sits on the pill itself, while the base alpha and
    // theme colours still carry its opacity and tint.
    setPanelOpacity(0.98f);
    setBorderWidth(0.f);
}

void TitlePillWidget::onRender(nxui::Renderer& ren) {
    if (m_suppressed)
        return;
    if (m_rect.width <= 0.5f || m_rect.height <= 0.5f)
        return;
    // Soft drop shadow so the capsule separates from bright artwork instead of
    // relying on the glass alone.
    const float radius = m_cornerStyle == 2 ? 3.f
        : (m_cornerStyle == 1 ? std::max(4.f, m_rect.height * 0.24f)
                              : m_rect.height * 0.5f);
    ren.drawRoundedRect({m_rect.x, m_rect.y + 3.f, m_rect.width, m_rect.height},
                        nxui::Color(0.f, 0.f, 0.f, 0.16f * m_opacity), radius);
    ren.drawRoundedRect({m_rect.x, m_rect.y + 1.5f, m_rect.width, m_rect.height},
                        nxui::Color(0.f, 0.f, 0.f, 0.10f * m_opacity), radius);
    nxui::GlassWidget::onRender(ren);
}

void TitlePillWidget::setText(const std::string& text, float screenWidth) {
    // Suppressed: nothing on screen, but keep the latest title so turning the
    // name back on does not wait for the next focus change.
    if (m_suppressed) {
        m_sourceText = text;
        m_text.clear();
        setVisible(false);
        return;
    }
    if (m_sourceText == text && m_layoutInitialized)
        return;

    const bool wasVisible = isVisible();
    m_hideOnCollapse = false;
    setVisible(true);
    m_screenWidth = screenWidth;
    const float maxContentWidth = std::max(120.f, std::min(736.f, screenWidth - 420.f));
    m_sourceText = text;
    m_text = ellipsize(m_font, text, maxContentWidth / m_textScale);
    sizeToFit();
    applyCornerStyle();
    float targetW = m_rect.width;
    float targetX = (screenWidth - targetW) * 0.5f + m_nudgeX;

    if (!m_layoutInitialized) {
        m_layoutInitialized = true;
        m_animX.setImmediate(targetX);
        m_animW.setImmediate(targetW);
    } else {
        if (!wasVisible) {
            float seedW = std::min(targetW, 54.f);
            m_animW.setImmediate(seedW);
            m_animX.setImmediate((screenWidth - seedW) * 0.5f + m_nudgeX);
        }
        m_animX.set(targetX, 0.22f, nxui::Easing::outCubic);
        m_animW.set(targetW, 0.22f, nxui::Easing::outCubic);
        m_textReveal.setImmediate(0.32f);
        m_textReveal.set(1.f, 0.18f, nxui::Easing::outCubic);
    }

    m_rect.x = m_animX.value();
    m_rect.width = m_animW.value();
}

void TitlePillWidget::setNudgeX(float x) {
    const float diff = x - m_nudgeX;
    if (diff > -0.01f && diff < 0.01f)
        return;
    m_nudgeX = x;
    if (!m_layoutInitialized)
        return;
    // Settled capsule: glide to the new offset now, otherwise the slider would
    // only take effect the next time the focused title changes.
    const float settledW = m_animW.target();
    m_animX.set((m_screenWidth - settledW) * 0.5f + m_nudgeX, 0.18f,
                nxui::Easing::outCubic);
}

void TitlePillWidget::hideAnimated(float screenWidth) {
    if (m_suppressed)
        return;
    if (!isVisible() && !m_hideOnCollapse)
        return;

    if (!m_layoutInitialized) {
        setVisible(false);
        return;
    }

    m_screenWidth = screenWidth;
    const float seedW = std::max(0.f, m_animW.value());
    m_animW.set(seedW, 0.01f, nxui::Easing::outCubic);
    m_animX.set((screenWidth - seedW) * 0.5f + m_nudgeX, 0.01f, nxui::Easing::outCubic);
    m_animW.set(0.f, 0.18f, nxui::Easing::outCubic);
    m_animX.set(screenWidth * 0.5f + m_nudgeX, 0.18f, nxui::Easing::outCubic);
    m_textReveal.set(0.f, 0.12f, nxui::Easing::outCubic);
    m_hideOnCollapse = true;
    setVisible(true);
}

void TitlePillWidget::onContentUpdate(float dt) {
    (void)dt;

    if (m_layoutInitialized) {
        m_rect.x = m_animX.value();
        m_rect.width = m_animW.value();
        if (m_hideOnCollapse && m_animW.value() <= 0.5f) {
            m_hideOnCollapse = false;
            m_sourceText.clear();
            m_text.clear();
            setVisible(false);
        }
    }
}

void TitlePillWidget::onContentRender(nxui::Renderer& ren) {
    if (!m_font || m_text.empty()) return;

    nxui::Rect cr = contentRect();
    // The same short accent bar used by the All Apps metadata shelf, scaled
    // down to the capsule. It stays at the left edge while the title is
    // centred, so it never changes the text fit.
    if (m_rect.width > 80.f && m_rect.height > 16.f) {
        const float barH = std::min(18.f, m_rect.height - 16.f);
        ren.drawRoundedRect({m_rect.x + 13.f,
                             m_rect.y + (m_rect.height - barH) * 0.5f,
                             3.f, barH},
                            m_accentColor.withAlpha(m_opacity * 0.9f), 1.5f);
    }
    if (m_cachedMeasureFont != m_font || m_cachedMeasureText != m_text) {
        m_cachedMeasureFont = m_font;
        m_cachedMeasureText = m_text;
        m_cachedTextSize = m_font->measure(m_text);
    }
    const nxui::Vec2 textSz = m_cachedTextSize;
    const float drawW = textSz.x * m_textScale;
    const float drawH = textSz.y * m_textScale;
    float tx = cr.x + (cr.width  - drawW) * 0.5f;
    float ty = cr.y + (cr.height - drawH) * 0.5f;
    float reveal = std::clamp(m_textReveal.value(), 0.f, 1.f);
    ren.pushClipRect(cr);
    ren.drawText(m_text, {tx, ty + (1.f - reveal) * 3.f}, m_font,
                 m_textColor.withAlpha(m_opacity * reveal), m_textScale);
    ren.popClipRect();
}

nxui::Vec2 TitlePillWidget::computeContentSize() const {
    if (!m_font || m_text.empty()) return {0.f, 0.f};
    if (m_cachedMeasureFont != m_font || m_cachedMeasureText != m_text) {
        m_cachedMeasureFont = m_font;
        m_cachedMeasureText = m_text;
        m_cachedTextSize = m_font->measure(m_text);
    }
    return {m_cachedTextSize.x * m_textScale, m_cachedTextSize.y * m_textScale};
}

void TitlePillWidget::applyCornerStyle() {
    if (m_cornerStyle == 2)
        setCornerRadius(3.f);
    else if (m_cornerStyle == 1)
        setCornerRadius(std::max(4.f, m_rect.height * 0.24f));
    else
        setCornerRadius(m_rect.height * 0.5f);
}

void TitlePillWidget::setTextScale(int pct) {
    const float next = std::clamp(pct, 80, 120) / 100.f;
    if (std::abs(next - m_textScale) < 0.001f)
        return;
    m_textScale = next;
    if (!m_sourceText.empty()) {
        const std::string source = m_sourceText;
        m_sourceText.clear();
        setText(source, m_screenWidth);
    }
}

void TitlePillWidget::setCornerStyle(int style) {
    style = std::clamp(style, 0, 2);
    if (style == m_cornerStyle)
        return;
    m_cornerStyle = style;
    applyCornerStyle();
}

void TitlePillWidget::setBackgroundStrength(int pct) {
    setPanelOpacity(std::clamp(pct, 0, 100) / 100.f);
}

void TitlePillWidget::setSuppressed(bool s) {
    if (m_suppressed == s)
        return;
    m_suppressed = s;
    if (s) {
        // Collapse without animating: the setting is a preference, not a
        // gesture, so the capsule must not slide away over the shelf.
        m_hideOnCollapse = false;
        m_textReveal.setImmediate(0.f);
        m_animW.setImmediate(0.f);
        m_rect.width = 0.f;
        setVisible(false);
    } else {
        m_textReveal.setImmediate(1.f);
        m_layoutInitialized = false;
    }
}
