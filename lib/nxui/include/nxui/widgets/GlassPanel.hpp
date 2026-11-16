#pragma once
#include <nxui/widgets/Box.hpp>
#include <nxui/core/Types.hpp>

namespace nxui {

// Flat, tinted panel. The name is historical: there is no refraction, blur
// shader or liquid-glass surface left in this class.
class GlassPanel : public Box {
public:
    GlassPanel();

    void setCornerRadius(float r)     { m_radius = r; }
    void setBaseColor(const Color& c) { m_base = c; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setHighlightColor(const Color& c) { m_highlight = c; }
    // Optional animated two-colour gradient instead of the flat base fill.
    // Phase moves the blend horizontally; alpha still follows the panel's
    // opacity and solid settings.
    void setGradientFill(bool enabled, const Color& a, const Color& b) {
        m_gradientEnabled = enabled;
        m_gradientA = a;
        m_gradientB = b;
    }
    void setGradientPhase(float phase) { m_gradientPhase = phase; }
    bool gradientFillEnabled() const { return m_gradientEnabled; }
    void setBorderWidth(float w)      { m_borderW = w; }
    // Icons draw their rim on the artwork, so the panel must not also outline
    // its (now flat) tile from the outside.
    void setPanelHighlightEnabled(bool enabled) { m_panelHighlightEnabled = enabled; }
    bool panelHighlightEnabled() const { return m_panelHighlightEnabled; }
    void setScale(float s)            { m_scale = s; }
    void setPanelOpacity(float o)     { m_panelOpacity = o; }
    // Draws only the content: no base, rim, highlight or backdrop. Used when a
    // shared bar behind several widgets is meant to be the only chrome.
    void setBackgroundVisible(bool visible) { m_backgroundVisible = visible; }
    bool backgroundVisible() const    { return m_backgroundVisible; }
    // Tiles draw their own shadow; large panels use the shared chrome shadow.
    void setPanelShadowEnabled(bool enabled) { m_panelShadowEnabled = enabled; }
    void setPanelGrainEnabled(bool enabled) { m_panelGrainEnabled = enabled; }
    float scale() const               { return m_scale; }
    float panelOpacity() const        { return m_panelOpacity; }
    float cornerRadius() const        { return m_radius; }
    float borderWidth() const         { return m_borderW; }
    const Color& baseColor() const    { return m_base; }
    const Color& borderColor() const  { return m_border; }
    const Color& highlightColor() const { return m_highlight; }

    // Optional backdrop blur behind the flat fill. Off for every menu surface;
    // kept for screens that opt in.
    void setBlurEnabled(bool b)       { m_blurEnabled = b; }
    void setBlurRadius(float r)       { m_blurRadius = r; }
    void setBlurPasses(int p)         { m_blurPasses = p; }
    bool blurEnabled() const          { return m_blurEnabled; }

    void setMaterialTextureEnabled(bool b) { m_materialTextureEnabled = b; }
    bool materialTextureEnabled() const    { return m_materialTextureEnabled; }
    void setMaterialTextureIntensity(float v) { m_materialTextureIntensity = v; }
    float materialTextureIntensity() const    { return m_materialTextureIntensity; }

    // Opaque backing: draws a fully-opaque rounded rect before the flat layer
    // so that anything underneath (e.g. a screen dimmer) is blocked.
    void setBackingEnabled(bool b)       { m_backingEnabled = b; }
    void setBackingColor(const Color& c) { m_backingColor = c; }
    bool backingEnabled() const          { return m_backingEnabled; }

protected:
    void onRender(Renderer& ren) override;

    float m_radius       = 24.f;
    float m_borderW      = 1.f;
    float m_scale        = 1.f;
    float m_panelOpacity = 1.f;
    Color m_base      {0.14f, 0.14f, 0.22f, 0.94f};
    Color m_border    {0.50f, 0.50f, 0.65f, 0.34f};
    Color m_highlight {1.f, 1.f, 1.f, 0.08f};
    bool  m_gradientEnabled = false;
    Color m_gradientA {0.32f, 0.55f, 0.95f, 1.f};
    Color m_gradientB {0.62f, 0.45f, 0.92f, 1.f};
    float m_gradientPhase = 0.f;
    bool  m_panelHighlightEnabled = true;

    // Optional backdrop blur.
    bool  m_blurEnabled  = false;
    float m_blurRadius   = 2.0f;
    int   m_blurPasses   = 3;

    bool  m_materialTextureEnabled = false;
    float m_materialTextureIntensity = 0.55f;

    // Opaque backing
    bool  m_backgroundVisible = true;
    bool  m_panelShadowEnabled = true;
    bool  m_panelGrainEnabled = true;
    bool  m_backingEnabled = false;
    Color m_backingColor   {0.f, 0.f, 0.f, 1.f};
};

} // namespace nxui
