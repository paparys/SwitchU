#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Types.hpp>
#include <string>

namespace switchu::services {
class ClockService;
}

class DateTimeWidget : public nxui::GlassWidget {
public:
    DateTimeWidget() = default;
    void setFont(nxui::Font* f) { m_font = f; }
    void setSmallFont(nxui::Font* sf) { m_smallFont = sf; }
    void setTextColor(const nxui::Color& c) { m_textColor = c; }
    void setSecondaryTextColor(const nxui::Color& c) { m_secondaryColor = c; }
    void setClockService(switchu::services::ClockService* service) { m_clockService = service; }
    void setUse12HourClock(bool enabled);
    // Scales the drawn time and date (the pill rect is sized to match).
    void setVisualScale(float s) { m_scale = s; }
    // Draws a hairline rule at each edge, so the clock reads as one cell of the
    // HOME bar rather than text floating on it.
    void setCellDividers(bool on) { m_cellDividers = on; }
    void setShowDate(bool on) { m_showDate = on; }

protected:
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;

private:
    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    float m_timer = 0.f;
    std::string m_timeStr;
    std::string m_dateStr;
    bool m_use12HourClock = false;
    bool m_cellDividers = false;
    bool m_showDate = true;
    float m_scale = 1.f;
    switchu::services::ClockService* m_clockService = nullptr;
    nxui::Font* m_cachedTimeFont = nullptr;
    std::string m_cachedTimeText;
    nxui::Vec2 m_cachedTimeSize{0.f, 0.f};
    nxui::Font* m_cachedDateFont = nullptr;
    std::string m_cachedDateText;
    nxui::Vec2 m_cachedDateSize{0.f, 0.f};
    mutable nxui::Font* m_cachedMeasureFont = nullptr;
    mutable nxui::Font* m_cachedMeasureSmall = nullptr;
    mutable bool m_cachedMeasure12h = false;
    mutable nxui::Vec2 m_cachedTimePlaceholder{0.f, 0.f};
    mutable nxui::Vec2 m_cachedDatePlaceholder{0.f, 0.f};
    nxui::Color m_textColor      {1.f, 1.f, 1.f, 1.f};
    nxui::Color m_secondaryColor {0.7f, 0.7f, 0.8f, 0.8f};
};
