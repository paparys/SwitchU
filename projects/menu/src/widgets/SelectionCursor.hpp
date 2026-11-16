#pragma once
#include "MotionPreset.hpp"
#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Animation.hpp>


class SelectionCursor : public nxui::Widget {
public:
    SelectionCursor();

    void moveTo(const nxui::Rect& target, float duration = 0.18f);
    void moveTo(const nxui::Rect& target, float cornerRadius, float duration);
    nxui::Rect currentRect() const;
    void setColor(const nxui::Color& c) { m_color = c; }
    void setGradient(bool enabled, const nxui::Color& a, const nxui::Color& b) {
        m_gradient = enabled;
        m_gradientA = a;
        m_gradientB = b;
    }
    void setCornerRadius(float r)  { m_cornerRadius.setImmediate(r); }
    void setBorderWidth(float w)   { m_borderWidth = w; }
    void setMotionPaused(bool paused);
    bool motionPaused() const { return m_motionPaused; }
    void setInstantMotion(bool instant);
    bool instantMotion() const { return m_instantMotion; }
    // 0 = Crisp, 1 = Smooth, 2 = Springy.
    void setMotionStyle(int style) { m_motionStyle = motion::clampStyle(style); }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    float computeAdaptiveDuration(const nxui::Rect& target, float targetCornerRadius, float baseDuration) const;

    nxui::AnimatedFloat m_x, m_y, m_w, m_h;
    nxui::AnimatedFloat m_cornerRadius;
    nxui::Color m_color {0.f, 0.75f, 1.f, 1.f};
    nxui::Color m_gradientA {0.f, 0.75f, 1.f, 1.f};
    nxui::Color m_gradientB {0.65f, 0.40f, 1.f, 1.f};
    bool  m_gradient = false;
    float m_borderWidth  = 3.f;
    float m_time = 0.f;
    float m_waveSpeed = 3.5f;
    int   m_motionStyle = 1;
    bool  m_initialized = false;
    bool  m_motionPaused = false;
    bool  m_instantMotion = false;
    float m_teleportPulse = 0.f;
};
