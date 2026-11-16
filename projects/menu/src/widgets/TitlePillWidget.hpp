#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Types.hpp>
#include <string>


class TitlePillWidget : public nxui::GlassWidget {
public:
    TitlePillWidget();

    void setFont(nxui::Font* f)              { m_font = f; }
    void setTextColor(const nxui::Color& c)  { m_textColor = c; }
    void setAccentColor(const nxui::Color& c) { m_accentColor = c; }
    // Horizontal offset of the capsule from screen centre; the pill re-aims
    // immediately instead of waiting for the next title change.
    void setNudgeX(float x);
    // Hide the capsule completely. Unlike hideAnimated this is not a
    // transition: the pill paints nothing until suppression is lifted, no
    // matter which show path runs in the meantime.
    void setSuppressed(bool s);
    bool suppressed() const { return m_suppressed; }

    void setText(const std::string& text, float screenWidth = 1280.f);
    void hideAnimated(float screenWidth = 1280.f);
    // Style lab controls. All three re-fit the current title immediately.
    void setTextScale(int pct);
    void setCornerStyle(int style);
    void setBackgroundStrength(int pct);

protected:
    void onRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;

private:
    void applyCornerStyle();
    nxui::Font*       m_font = nullptr;
    std::string m_sourceText;
    std::string m_text;
    nxui::Color       m_textColor {1.f, 1.f, 1.f, 1.f};
    nxui::Color       m_accentColor {1.f, 1.f, 1.f, 1.f};
    float             m_nudgeX = 0.f;
    float             m_screenWidth = 1280.f;
    nxui::AnimatedFloat m_animX{0.f};
    nxui::AnimatedFloat m_animW{0.f};
    nxui::AnimatedFloat m_textReveal{1.f};
    float m_textScale = 1.f;
    mutable nxui::Font* m_cachedMeasureFont = nullptr;
    mutable std::string m_cachedMeasureText;
    mutable nxui::Vec2 m_cachedTextSize{0.f, 0.f};
    int m_cornerStyle = 0;
    bool m_layoutInitialized = false;
    bool m_hideOnCollapse = false;
    bool m_suppressed = false;
};
