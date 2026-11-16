#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Types.hpp>


class BatteryWidget : public nxui::GlassWidget {
public:
    BatteryWidget();
    void setFont(nxui::Font* f) { m_font = f; }
    void setTextColor(const nxui::Color& c) { m_textColor = c; }
    void setBatteryStatus(uint32_t percentage, bool charging);
    // Uniform scale for the drawn glyph (the pill rect is sized to match).
    void setVisualScale(float s);

protected:
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;

private:
    void refreshAccessibility();

    nxui::Font* m_font = nullptr;
    float m_level   = -1.f;
    bool  m_charging = false;
    float m_timer    = 0.f;
    float m_chargeAnim = 0.f;
    float m_scale    = 1.f;
    nxui::Color m_textColor {1.f, 1.f, 1.f, 1.f};
    mutable nxui::Font* m_cachedHeightFont = nullptr;
    mutable float m_cachedTextHeight = 0.f;
    nxui::Font* m_cachedPctFont = nullptr;
    std::string m_cachedPctText;
    nxui::Vec2 m_cachedPctSize{0.f, 0.f};
};
