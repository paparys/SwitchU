#pragma once
#include <nxui/widgets/Background.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


class WaraWaraBackground : public nxui::Background {
public:
    enum class Layout {
        Floating,
        Grid,
    };

    enum class ShapeSet {
        Mixed,
        Circle,
        Triangle,
        Square,
        Diamond,
        Hexagon,
    };

    enum class Symmetry {
        None,
        MirrorHorizontal,
        MirrorVertical,
        Quad,
    };

    struct Config {
        Layout layout = Layout::Floating;
        ShapeSet shapeSet = ShapeSet::Mixed;
        Symmetry symmetry = Symmetry::None;
        int shapeCount = 30;
        int gridColumns = 14;
        int gridRows = 8;
        float spacingX = 88.f;
        float spacingY = 88.f;
        float sizeMin = 14.f;
        float sizeMax = 54.f;
        float speedMin = 6.f;
        float speedMax = 28.f;
        float wobble = 16.f;
        float opacity = 1.f;
        float rotationSpeed = 0.5f;
        bool fixedOrientation = false;
        float orientationDegrees = 0.f;
        float cornerRoundness = 0.f;
        float imageOpacity = 0.f;
        bool imageCover = true;
    };

    WaraWaraBackground();
    ~WaraWaraBackground();

    void setConfig(const Config& config);
    const Config& config() const { return m_config; }

    bool loadImage(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path);
    void clearImage();
    // Re-read the theme still image after a wallpaper released its GPU slot.
    bool restoreImage(nxui::GpuDevice& gpu, nxui::Renderer& ren);

    // Animated / video wallpaper. Plays over the theme image and falls back to
    // it when cleared or when the file cannot be decoded.
    bool loadAnimatedWallpaper(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                               const std::string& path, int fps);
    void clearAnimatedWallpaper();
    bool hasAnimatedWallpaper() const;
    /// GPU bytes held by the theme image and every uploaded wallpaper frame.
    std::size_t gpuBytes() const;

    void setMotionPaused(bool paused) { m_motionPaused = paused; }
    bool motionPaused() const { return m_motionPaused; }

    // Slow horizontal pan of the wallpaper/hero art. speedPercent 1..100 maps to
    // one left-right pass every ~75s down to ~12s.
    void setDrift(bool enabled, int speedPercent);
    bool driftEnabled() const { return m_driftEnabled; }

    // User setting: false drops the drifting shapes while keeping the gradient
    // and the wallpaper.
    void setShapesEnabled(bool enabled);
    bool shapesEnabled() const { return m_shapesEnabled; }
    // A full-screen per-game hero is about to cover the wallpaper. Pause the
    // shape animation and skip drawing the shapes; the wallpaper and gradient
    // stay for the 6% of the hero's translucent blend. This is presentation
    // state, not a user preference.
    void setHeroCover(bool covered) { m_heroCover = covered; }
    bool heroCover() const { return m_heroCover; }
    // Carousel/page motion: keep the shape bodies and skip the translucent
    // highlight and edge passes. They are subtle at rest and are pure overdraw
    // while the UI is moving; the full treatment returns when motion stops.
    void setScrollActive(bool active) { m_scrollActive = active; }
    bool scrollActive() const { return m_scrollActive; }
    // Wallpaper grading. Blur is a cheap multi-tap soften, saturation is a
    // neutral veil below 100%, and tint lays a theme colour over the artwork.
    // Sharpness, vibrance and noise are baked into the still image on the CPU
    // (the flat renderer has no colour matrix), so they need no per-frame draw.
    void setTreatment(int blurPx, int saturationPct, int tintPct,
                      int sharpnessPct, int vibrancePct, int noisePct,
                      const nxui::Color& tint);

    /// Shapes (+ optional theme image) without the opaque gradient fill —
    /// for compositing over blurred letterbox regions.
    void renderLetterboxOverlay(nxui::Renderer& ren, float opacityMul = 1.f);

    /// Full wallpaper stack (gradient + image + shapes) shifted by dy —
    /// used as the Lock curtain plane so Theme Shop art slides with unlock.
    /// Optional uniform scale around screen center (Lock Zoom transition).
    void renderShifted(nxui::Renderer& ren, float dy, float opacityMul = 1.f,
                       float scale = 1.f);

    void regenerate(int count = 50) override;

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    enum ShapeType { Circle, Triangle, Square, Diamond, Hexagon, ShapeCount };

    struct Shape {
        ShapeType type;
        nxui::Vec2  pos;
        float size;
        float speed;
        float phase;
        float wobble;
        float rotation;
        float rotSpeed;
        nxui::Color color;
        float glassAlpha;
    };

    ShapeType pickShapeType() const;
    const nxui::Texture* activeWallpaperTexture() const;
    // True when a full-bleed wallpaper/cover image hides the gradient fill.
    bool backgroundCoversGradient() const;
    void drawWallpaper(nxui::Renderer& ren, float opacityScale) const;
    nxui::Rect backgroundImageRect() const;
    void drawShapeWithSymmetry(nxui::Renderer& ren, const Shape& s) const;
    void drawGlassShape(nxui::Renderer& ren, const Shape& s) const;
    // cs/sn are the shape's rotation, hoisted because the body, highlight and
    // edge layers all share it.
    void drawRoundedShape(nxui::Renderer& ren, const Shape& s, const nxui::Color& c,
                          float cs, float sn) const;
    // Rounded-square outline in unit space. The arc centres/radii scale
    // linearly with shape size, so this only needs rebuilding when the theme's
    // corner roundness changes.
    static constexpr int kRoundedUnitCapacity = 16;
    mutable std::array<nxui::Vec2, kRoundedUnitCapacity> m_roundedUnitPoints{};
    mutable int m_roundedUnitCount = 0;
    mutable float m_roundedUnitRoundness = -1.f;

    Config m_config;
    std::vector<Shape> m_shapes;
    // Scratch buffers for renderShifted: the lock transition runs every frame
    // and should not allocate two vectors each time.
    std::vector<nxui::Vec2> m_savedShapePos;
    std::vector<float> m_savedShapeSize;
    nxui::Texture m_backgroundImage;
    // Path kept while a wallpaper owns the slot so the image can be restored.
    std::string m_imagePath;
    std::unique_ptr<class AnimatedWallpaper> m_animWallpaper;
    float m_time = 0.f;
    bool m_driftEnabled = false;
    int  m_driftSpeed = 35;
    // Radians; one full sine cycle is one left-right pass.
    float m_driftPhase = 0.f;
    bool m_motionPaused = false;
    bool m_heroCover = false;
    bool m_scrollActive = false;
    int  m_treatmentBlur = 0;
    int  m_treatmentSaturation = 100;
    int  m_treatmentTint = 0;
    int  m_treatmentSharpness = 0;
    int  m_treatmentVibrance = 0;
    int  m_treatmentNoise = 0;
    nxui::Color m_treatmentTintColor {0.f, 0.f, 0.f, 1.f};

    bool treatmentActive() const {
        return m_treatmentSharpness > 0 || m_treatmentVibrance > 0 ||
               m_treatmentNoise > 0;
    }
    bool loadSourcePixels(const std::string& path);
    bool bakeTreatment();
    // Source pixels are kept only while a bake treatment is active, so the
    // default look costs no extra heap.
    std::vector<std::uint8_t> m_sourceRgba;
    std::vector<std::uint8_t> m_treatedRgba;
    std::vector<std::uint8_t> m_treatScratch;
    int m_sourceW = 0;
    int m_sourceH = 0;
    bool m_treatDirty = false;
    nxui::GpuDevice* m_gpu = nullptr;
    nxui::Renderer* m_ren = nullptr;
    std::chrono::steady_clock::time_point m_lastTreat{};
    bool m_shapesEnabled = true;
};

