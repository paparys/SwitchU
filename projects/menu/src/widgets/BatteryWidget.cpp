#include "BatteryWidget.hpp"
#include "BatteryDrawing.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <switch.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

using switchu::battery_drawing::drawLightningBolt;

BatteryWidget::BatteryWidget() {
    setTag("battery");
    setFocusable(true);
    setActivateOnFirstTouch(true);
    refreshAccessibility();
}

void BatteryWidget::setBatteryStatus(uint32_t percentage, bool charging) {
    if (percentage > 100)
        percentage = 100;
    m_level = static_cast<float>(percentage) / 100.f;
    m_charging = charging;
    m_timer = 0.f;
    refreshAccessibility();
}

void BatteryWidget::setVisualScale(float s) {
    m_scale = std::clamp(s, 0.5f, 2.f);
}

void BatteryWidget::refreshAccessibility() {
    auto& i18n = nxui::I18n::instance();
    char buf[32];
    if (m_level < 0.f)
        std::snprintf(buf, sizeof(buf), "%s",
                      i18n.tr("battery.label", "Battery").c_str());
    else
        std::snprintf(buf, sizeof(buf), "%s %d%%",
                      i18n.tr("battery.label", "Battery").c_str(),
                      static_cast<int>(m_level * 100.f + 0.5f));
    setAccessibilityLabel(buf);
    setAccessibilityRole(i18n.tr("accessibility.roles.button", "button"));
    setAccessibilityHint(i18n.tr("battery.hint", "A to open Quick Settings."));
}

void BatteryWidget::onContentUpdate(float dt) {
    if (m_charging)
        m_chargeAnim += dt;
#ifdef SWITCHU_MENU
    // The daemon owns PSM and pushes state changes. Duplicating the query here
    // woke both processes periodically and could race service teardown.
    return;
#else
    m_timer += dt;
    if (m_timer < 1.f && m_level >= 0.f) return;
    m_timer = 0.f;

    u32 charge = 100;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&charge))) {
        if (charge > 100)
            charge = 100;
        m_level = charge / 100.f;
    }

    PsmChargerType ct = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&ct)))
        m_charging = (ct != PsmChargerType_Unconnected);
#endif
}

void BatteryWidget::onContentRender(nxui::Renderer& ren) {
    nxui::Rect cr = contentRect();
    const float s = m_scale;
    float bw = 46.f * s, bh = 22.f * s;
    // The bolt is drawn inside the shell now, so the group is just the body and
    // its terminal; there is no charging slot to reserve on the right.
    float groupW = bw + 1.6f * s + 5.f * s;
    float bx = cr.x + (cr.width - groupW) * 0.5f;
    float textH = m_font ? m_font->measure("100%").y * 0.75f * s : 14.f * s;
    float contentH = bh + 6.f * s + textH;
    float by = cr.y + (cr.height - contentH) * 0.5f;
    float op = m_opacity;

    const bool known = m_level >= 0.f;
    float level = known ? std::clamp(m_level, 0.f, 1.f) : 0.f;

    nxui::Rect body = {bx, by, bw, bh};
    const float radius = bh * 0.46f;
    const float capW = 5.f * s;
    const float capH = bh * 0.40f;

    // Shell: a dark well under a lit rim, so the cells read as sitting inside
    // the battery rather than floating on a flat plate.
    ren.drawRoundedRect(body, nxui::Color(0.f, 0.f, 0.f, 0.24f * op), radius);
    ren.drawRoundedRect(body.shrunk(1.2f * s), m_textColor.withAlpha(0.09f * op),
                        radius * 0.92f);
    ren.drawRoundedRectOutline(body, m_textColor.withAlpha(0.56f * op),
                               radius, 1.5f * s);
    // Glass: a highlight across the top and a shade along the bottom, so the
    // body has a light direction like the tiles do.
    ren.drawRoundedRect({body.x + 2.6f * s, body.y + 2.f * s,
                         body.width - 5.2f * s, body.height * 0.34f},
                        nxui::Color::white().withAlpha(0.12f * op), radius * 0.58f);
    ren.drawRoundedRect({body.x + 2.6f * s,
                         body.bottom() - 2.f * s - body.height * 0.16f,
                         body.width - 5.2f * s, body.height * 0.16f},
                        nxui::Color(0.f, 0.f, 0.f, 0.12f * op), radius * 0.50f);

    // Terminal: a rounded cap, not a square tab.
    nxui::Rect cap = {body.right() + 1.6f * s, body.y + (bh - capH) * 0.5f,
                      capW, capH};
    ren.drawRoundedRect(cap, m_textColor.withAlpha(0.44f * op), capH * 0.44f);
    ren.drawRoundedRect({cap.x + 0.6f * s, cap.y + 1.f * s,
                         cap.width - 1.2f * s, cap.height * 0.34f},
                        nxui::Color::white().withAlpha(0.18f * op), capH * 0.30f);

    const float chargePulse = m_charging
        ? (0.74f + 0.26f * (0.5f + 0.5f * std::sin(m_chargeAnim * 5.2f))) : 1.f;

    // Five cells: structure for the eye, and the leading cell fills by fraction
    // so the readout still tracks single percent steps.
    constexpr int kCells = 5;
    const nxui::Rect track = body.shrunk(3.4f * s);
    const float cellGap = 2.2f * s;
    const float cellW = std::max(1.f,
        (track.width - cellGap * static_cast<float>(kCells - 1)) / kCells);
    const float cellR = std::min(track.height, cellW) * 0.36f;

    nxui::Color levelColor;
    if (m_charging)
        levelColor = nxui::Color(0.44f, 0.95f, 0.78f, op * chargePulse);
    else if (level > 0.50f)
        levelColor = nxui::Color(0.36f, 0.92f, 0.56f, op);
    else if (level > 0.20f)
        levelColor = nxui::Color(0.98f, 0.77f, 0.26f, op);
    else
        levelColor = nxui::Color(0.96f, 0.32f, 0.28f, op * chargePulse);

    const float litCells = known ? std::clamp(m_level, 0.f, 1.f) * kCells : 0.f;
    for (int i = 0; i < kCells; ++i) {
        const nxui::Rect cell = {track.x + static_cast<float>(i) * (cellW + cellGap),
                                 track.y, cellW, track.height};
        ren.drawRoundedRect(cell, nxui::Color(1.f, 1.f, 1.f, 0.10f * op), cellR);
        const float share = std::clamp(litCells - static_cast<float>(i), 0.f, 1.f);
        if (share <= 0.f)
            continue;
        const nxui::Rect filled = {cell.x, cell.y,
                                   std::max(cellW * share, 1.2f * s), cell.height};
        ren.drawRoundedRect(filled, levelColor, cellR);
        ren.drawRoundedRect({filled.x + 0.8f * s, filled.y + 0.9f * s,
                             std::max(0.f, filled.width - 1.6f * s),
                             filled.height * 0.28f},
                            nxui::Color::white().withAlpha(0.22f * op * chargePulse),
                            cellR * 0.70f);
    }

    float groupRight = cap.right();
    if (m_charging) {
        float boltPulse = 0.70f + 0.30f * (0.5f + 0.5f * std::sin(m_chargeAnim * 6.8f));
        // Larger than the shell on purpose: the bolt overlaps the battery and
        // spills past its top and bottom edges, so it reads as charge sitting
        // on the icon rather than a symbol trapped inside it.
        float boltH = bh * (1.24f + 0.06f * boltPulse);
        float boltW = boltH * 0.56f;
        float boltX = body.x + (body.width - boltW) * 0.5f;
        float boltY = body.y + (bh - boltH) * 0.5f;
        nxui::Rect boltRect = {boltX, boltY, boltW, boltH};

        // Dark halo so the bolt keeps its edge over the lit (mint) cells.
        ren.drawRoundedRect(boltRect.expanded(2.2f * s),
                            nxui::Color(0.03f, 0.06f, 0.05f, 0.42f * op),
                            boltH * 0.28f);

        nxui::Color glow = nxui::Color(1.f, 0.74f, 0.12f, 0.18f * op * boltPulse);
        drawLightningBolt(ren, boltRect.expanded(2.8f * s), glow,
                          glow.withAlpha(0.f), 0.f);

        nxui::Color boltColor = nxui::Color(1.f, 0.86f, 0.18f, op * (0.88f + 0.12f * boltPulse));
        nxui::Color boltEdge = nxui::Color(1.f, 0.64f, 0.08f, op * 0.72f);
        drawLightningBolt(ren, boltRect, boltColor, boltEdge, 1.15f * s);
        drawLightningBolt(ren, boltRect.shrunk(2.4f * s),
                          nxui::Color::white().withAlpha(0.12f * op),
                          nxui::Color::white().withAlpha(0.f), 0.f);
    }

    if (m_font) {
        char buf[16];
        if (known)
            std::snprintf(buf, sizeof(buf), "%d%%", (int)(level * 100 + 0.5f));
        else
            std::snprintf(buf, sizeof(buf), "--%%");
        nxui::Vec2 sz = m_font->measure(buf);
        float tx = cr.x + (cr.width - sz.x * 0.75f * s) * 0.5f;
        float ty = by + bh + 6.f * s;
        ren.drawText(buf, {tx, ty}, m_font, m_textColor.withAlpha(op), 0.75f * s);
    }

    // Chevron centered in the free space to the right of the battery group.
    {
        const float freeLeft = groupRight + 2.f;
        const float freeRight = cr.right();
        const float cx = (freeLeft + freeRight) * 0.5f;
        const float cy = cr.y + cr.height * 0.5f;
        const nxui::Color chev = m_textColor.withAlpha(0.42f * op);
        ren.drawLine({cx - 3.5f * s, cy - 5.5f * s}, {cx + 2.5f * s, cy}, chev, 1.7f * s);
        ren.drawLine({cx + 2.5f * s, cy}, {cx - 3.5f * s, cy + 5.5f * s}, chev, 1.7f * s);
    }
}

nxui::Vec2 BatteryWidget::computeContentSize() const {
    const float s = m_scale;
    float bw = 46.f * s, bh = 22.f * s;
    float textH = m_font ? m_font->measure("100%").y * 0.75f * s : 14.f * s;
    return {bw + 6.6f * s, bh + 6.f * s + textH};
}
