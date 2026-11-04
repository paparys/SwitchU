#include "DateTimeWidget.hpp"
#include "services/ClockService.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cstdio>

void DateTimeWidget::setUse12HourClock(bool enabled) {
    if (m_use12HourClock == enabled)
        return;

    m_use12HourClock = enabled;
    m_timeStr.clear();
    m_timer = 1.f;
}

void DateTimeWidget::onContentUpdate(float dt) {
    m_timer += dt;
    if (m_timer < 1.f && !m_timeStr.empty()) return;
    m_timer = 0.f;

    if (!m_clockService)
        return;
    const auto snapshot = m_clockService->refresh();
    if (!snapshot.valid)
        return;
    const auto& calendar = snapshot.calendar;

    char buf[64];
    if (m_use12HourClock) {
        int hour = calendar.hour % 12;
        if (hour == 0)
            hour = 12;
        std::snprintf(buf, sizeof(buf), "%d:%02d %s", hour, calendar.minute,
                      calendar.hour >= 12 ? "PM" : "AM");
    } else {
        std::snprintf(buf, sizeof(buf), "%02u:%02u",
                      static_cast<unsigned>(calendar.hour),
                      static_cast<unsigned>(calendar.minute));
    }
    m_timeStr = buf;
    std::snprintf(buf, sizeof(buf), "%02u/%02u/%04u",
                  static_cast<unsigned>(calendar.day),
                  static_cast<unsigned>(calendar.month),
                  static_cast<unsigned>(calendar.year));
    m_dateStr = buf;
}

void DateTimeWidget::onContentRender(nxui::Renderer& ren) {
    if (!m_font) return;

    nxui::Rect cr = contentRect();
    nxui::Font* sf = m_smallFont ? m_smallFont : m_font;

    const float s = m_scale;
    if (m_cellDividers) {
        // Short rules, not full-height walls: they separate the clock from the
        // profile and battery without cutting the band into boxes.
        const float ruleH = std::min(cr.height, 30.f * s);
        const float ruleY = cr.y + (cr.height - ruleH) * 0.5f;
        const nxui::Color rule = m_secondaryColor.withAlpha(0.22f * m_opacity);
        ren.drawRoundedRect({cr.x - 2.f, ruleY, 1.f, ruleH}, rule, 0.5f);
        ren.drawRoundedRect({cr.right() + 1.f, ruleY, 1.f, ruleH}, rule, 0.5f);
    }
    nxui::Vec2 timeSz = m_font->measure(m_timeStr);
    nxui::Vec2 dateSz = sf->measure(m_dateStr);
    const float dateScale = 0.7f * s;
    float contentH = timeSz.y * s + 2.f * s + dateSz.y * dateScale;
    float tx = cr.x + (cr.width - timeSz.x * s) * 0.5f;
    float ty = cr.y + (cr.height - contentH) * 0.5f;
    ren.drawText(m_timeStr, {tx, ty}, m_font, m_textColor.withAlpha(m_opacity), s);

    float dx = cr.x + (cr.width - dateSz.x * dateScale) * 0.5f;
    float dy = ty + timeSz.y * s + 2.f * s;
    ren.drawText(m_dateStr, {dx, dy}, sf, m_secondaryColor.withAlpha(m_opacity),
                 dateScale);
}

nxui::Vec2 DateTimeWidget::computeContentSize() const {
    if (!m_font) return {130.f, 46.f};
    nxui::Vec2 timeSz = m_font->measure(m_use12HourClock ? "12:00 PM" : "00:00");
    nxui::Font* sf = m_smallFont ? m_smallFont : m_font;
    nxui::Vec2 dateSz = sf->measure("00/00/0000");
    float w = std::max(timeSz.x, dateSz.x * 0.7f);
    float h = timeSz.y + 2.f + dateSz.y * 0.7f;
    return {w * m_scale, h * m_scale};
}
