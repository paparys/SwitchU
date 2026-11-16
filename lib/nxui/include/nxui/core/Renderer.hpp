#pragma once
#include <nxui/core/Types.hpp>
#include <nxui/core/GpuDevice.hpp>
#ifdef NXUI_BACKEND_DEKO3D
#include <deko3d.hpp>
#endif
#include <cstdint>
#include <vector>
#include <string>

namespace nxui {

class Texture;
class Font;

struct Vertex2D {
    float x, y;         // Position
    float u, v;         // Texture coordinate
    float r, g, b, a;   // Color (premultiplied alpha)
    float sx, sy;       // Position relative to rounded-shape centre
    float hx, hy;       // Rounded-shape half extent
    float rad;          // Corner radius; zero disables the mask
    float thick;        // Stroke width; zero fills the shape
};
static_assert(sizeof(Vertex2D) == 56);

struct VsUniforms {
    float projection[16];   // Ortho matrix (top-left origin)
};

struct FsUniforms {
    int32_t useTexture;
    float   param1;
    float   param2;
    float   param3;
    float   extra[4 * 4 * 3];
};

// Shader program IDs
enum class ShaderProgram {
    Basic,
    Backdrop,
    BlurH,
    BlurV,
    Wave,
    Gradient,
    Count
};

// Renderer
class Renderer {
public:
    explicit Renderer(GpuDevice& gpu);
    ~Renderer();

    /// Set the base directory for compiled shader (.dksh) files.
    /// Must be called BEFORE initialize(). Default: "romfs:/shaders/"
    static void setShaderBasePath(const std::string& path) { s_shaderBasePath = path; }
    static const std::string& shaderBasePath() { return s_shaderBasePath; }

    bool initialize();

    // Frame scope
    void beginFrame();
    void endFrame();

    // 2D drawing
    void drawRect(const Rect& r, const Color& c);
    void drawRectOutline(const Rect& r, const Color& c, float thickness = 1.f);
    void drawRoundedRect(const Rect& r, const Color& c, float radius);
    void drawRoundedRectOutline(const Rect& r, const Color& c, float radius, float thickness = 1.f);
    void drawFrostedInset(const Rect& r, const Color& tint, const Color& border,
                          const Color& highlight, float radius, float opacity = 1.f);
    void drawCircle(const Vec2& center, float radius, const Color& c, int segments = 32);
    void drawTriangle(const Vec2& p1, const Vec2& p2, const Vec2& p3, const Color& c);
    void drawGradientTriangle(const Vec2& p1, const Vec2& p2, const Vec2& p3,
                              const Color& c1, const Color& c2, const Color& c3);
    void drawLine(const Vec2& from, const Vec2& to, const Color& c, float thickness = 1.f);
    void drawGradientRect(const Rect& r, const Color& top, const Color& bottom);
    // Horizontal counterpart: left colour at r.x, right colour at r.right().
    void drawGradientRectH(const Rect& r, const Color& left, const Color& right);
    void drawTexture(const Texture* tex, const Rect& dest, const Color& tint = Color::white());
    void drawTextureSub(const Texture* tex, const Rect& src, const Rect& dest, const Color& tint = Color::white());
    void drawTextureRounded(const Texture* tex, const Rect& dest, float radius, const Color& tint = Color::white());
    void drawTextureSubRounded(const Texture* tex, const Rect& src, const Rect& dest,
                               float radius, const Color& tint = Color::white());
    void drawText(const std::string& text, const Vec2& pos, Font* font, const Color& color, float scale = 1.f);

    /// GPU→CPU readback of the last presented framebuffer. See GpuDevice.
    bool downloadFramebufferRgba(std::vector<uint8_t>& outRgba,
                                 int& outW, int& outH,
                                 bool halfRes = true);

    // Post-processing
    void captureToOffscreen(bool reuseIfValid = false);

    // Frosted backdrop for a glass panel. Only `region` (screen space) plus
    // the blur kernel's spread is captured and blurred; the same region and
    // settings reuse the result within a frame.
    void captureSharedBackdrop(const Rect& region, float radius, int passes);

    // Per-frame submission stats, reset in beginFrame(). The frame-time readout
    // uses them to tell draw-call pressure apart from fill-rate cost.
    std::uint32_t drawCallCount() const { return m_drawCalls; }
    void copyOffscreen(int srcTarget, int dstTarget);
    void drawOffscreen(int target, const Rect& dest, const Color& tint = Color::white());
    void drawOffscreenRounded(int target, const Rect& dest, float radius,
                              const Color& tint = Color::white());
    void applyBlur(float radius = 1.0f, int passes = 2);
    void applyWave(float time, float amplitude, float frequency);

    // Switch shader program (flushes current batch)
    void useShader(ShaderProgram prog);

    // Push custom FS uniforms (flushes current batch)
    void pushFsUniforms(const FsUniforms& fs);

    // Scissor/clip stack
    void pushClipRect(const Rect& r);
    void popClipRect();

    // Screen dimensions
    int width()  const { return m_gpu.width(); }
    int height() const { return m_gpu.height(); }

    // Flush current batch
    void flush();

    // User-facing opacity style shared by every panel's flat layer.
    // tintEnabled replaces each panel's own layer colour; solid makes that
    // layer fully opaque instead of see-through.
    void setOpacityStyle(bool tintEnabled, const Color& tint, bool solid) {
        m_opacityTintEnabled = tintEnabled;
        m_opacityTint = tint;
        m_opacitySolid = solid;
    }
    bool opacityTintEnabled() const { return m_opacityTintEnabled; }
    const Color& opacityTint() const { return m_opacityTint; }
    bool opacitySolid() const { return m_opacitySolid; }

    // Shared chrome treatment. Tint mix blends a panel's own colour toward the
    // opacity tint; border scales every rim; grain adds the panel material;
    // shadow adds a soft falloff behind large panels.
    void setPanelStyle(int tintMixPct, int borderPct, int grainPct, int shadowPct) {
        m_panelTintMixPct = tintMixPct;
        m_panelBorderPct = borderPct;
        m_panelGrainPct = grainPct;
        m_panelShadowPct = shadowPct;
    }
    int panelTintMixPct() const { return m_panelTintMixPct; }
    int panelBorderPct() const { return m_panelBorderPct; }
    int panelGrainPct() const { return m_panelGrainPct; }
    int panelShadowPct() const { return m_panelShadowPct; }

    // Debug
    uint32_t vertexCount() const { return m_vtxCount; }
    void setBoxWireframeEnabled(bool enabled) { m_boxWireframeEnabled = enabled; }
    bool boxWireframeEnabled() const { return m_boxWireframeEnabled; }

    // Texture descriptor management
#ifdef NXUI_BACKEND_DEKO3D
    int registerTexture(const dk::ImageView& view);
    void updateTexture(int slot, const dk::ImageView& view);
    void releaseTextureSlot(int slot);
    void reclaimReleasedTextureSlotsAfterIdle();
#endif
    void bindTexture(int slot);
    void resetTextureSlots();

    GpuDevice& gpu() { return m_gpu; }

    // Offscreen descriptor slots
    int offscreenDescSlot(int target) const { return m_offDescSlot[target]; }

private:
    // Emit geometry helpers
    Vec2  m_shapeCentre {0.f, 0.f};
    Vec2  m_shapeHalf {0.f, 0.f};
    float m_shapeRadius = 0.f;
    float m_shapeThickness = 0.f;

    void beginShape(const Rect& dest, float radius, float thickness = 0.f);
    void endShape();
    void drawRoundedMasked(const Rect& dest, float radius, const Color& color,
                           const Rect& uv, float thickness = 0.f);
    void addVertex(float x, float y, float u, float v, const Color& c);
    void addQuad(float x0, float y0, float x1, float y1,
                 float u0, float v0, float u1, float v1, const Color& c);
    void addQuadGrad(float x0, float y0, float x1, float y1,
                     float u0, float v0, float u1, float v1,
                     const Color& cTop, const Color& cBot);
    void addQuadGradH(float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1,
                      const Color& cLeft, const Color& cRight);

    // True when a screen-space rect lies entirely outside the active clip: the
    // scissor would discard every pixel, so the geometry can be skipped.
    bool fullyClipped(const Rect& r) const;

#ifdef NXUI_BACKEND_DEKO3D
    bool loadShaders();
    void setupSampler();
    void updateProjection();
    void bindRenderTarget(int offscreenIdx);
    void restoreRenderTarget();
    void applyBlurRegion(float radius, int passes, const DkScissor* scissor);
#endif

    GpuDevice& m_gpu;

#ifdef NXUI_BACKEND_DEKO3D
    static constexpr int SHADER_COUNT = (int)ShaderProgram::Count;
    dk::Shader m_vertShaders[SHADER_COUNT];
    dk::Shader m_fragShaders[SHADER_COUNT];
    ShaderProgram m_curShader = ShaderProgram::Basic;
#endif

    // Batching state
    Vertex2D*  m_vtxBase   = nullptr;
    uint32_t   m_vtxCount  = 0;
    uint32_t   m_vtxBatchStart = 0;
    int        m_curTexSlot = -1;
    bool       m_texturing  = false;

    // Clip stack
    std::vector<Rect> m_clipStack;

    // Texture descriptor tracking
    int m_nextDescSlot = 0;
    std::vector<int> m_freeDescSlots;
    struct DeferredDescSlot {
        int slot = -1;
        std::uint64_t reusableFrame = 0;
    };
    std::vector<DeferredDescSlot> m_deferredDescSlots;
    std::uint64_t m_frameSerial = 0;
    std::uint32_t m_drawCalls = 0;
    // Frame whose reusable backdrop capture is already in the offscreen target.
    std::uint64_t m_reuseCaptureFrame = 0;
    static constexpr int WHITE_TEX_SLOT = 0;

    // Offscreen target descriptor slots
    int m_offDescSlot[GpuDevice::NUM_OFFSCREEN] = {};

#ifdef NXUI_BACKEND_DEKO3D
    dk::Image          m_whiteImage;
    dk::UniqueMemBlock m_whiteMemBlock;

    // Track whether any image/sampler descriptor has been written
    // via CPU memcpy since the last GPU-side barrier.  When true,
    // flush() inserts barrier(DkBarrier_None, DkInvalidateFlags_Descriptors)
    // before the next draw call so the GPU re-reads from memory.
    bool m_descDirty = false;

    // Fragment-uniform dedupe: an identical block re-binds its existing ring
    // slot instead of forcing a draw, so steady-state effect passes merge.
    FsUniforms    m_lastFsUniforms {};
    ShaderProgram m_lastFsShader = ShaderProgram::Basic;
    DkGpuAddr     m_lastFsUboAddr = 0;
    bool          m_fsUniformsValid = false;
    // Basic program's per-draw fragment block (useTexture only). Reused while
    // the texturing state is unchanged instead of burning a ring slot.
    DkGpuAddr     m_basicFsUboAddr = 0;
    bool          m_basicFsUboValid = false;
    bool          m_basicFsTexturing = false;
#endif

#ifdef NXUI_BACKEND_SDL2
    // SDL2 backend: textures tracked by slot for binding
    std::vector<SDL_Texture*> m_texSlots;
    SDL_Texture* m_boundTex = nullptr;

    // Vertex buffer (CPU-side for SDL2)
    std::vector<Vertex2D> m_vtxBuf;
#endif

    bool m_boxWireframeEnabled = false;
    bool m_reusableOffscreenCaptureValid = false;
    // Frame-scoped frosted backdrop shared by GlassPanel. Any other write to
    // offscreen 0 (fresh capture, blur, target bind) invalidates it.
    bool          m_sharedBackdropValid = false;
    std::uint64_t m_sharedBackdropFrame = 0;
    float         m_sharedBackdropRadius = 0.f;
    int           m_sharedBackdropPasses = 0;
    // Offscreen-texel bounds of the cached frosted region.
    int           m_sharedBackdropX0 = 0;
    int           m_sharedBackdropY0 = 0;
    int           m_sharedBackdropX1 = 0;
    int           m_sharedBackdropY1 = 0;
    bool  m_opacityTintEnabled = false;
    Color m_opacityTint {1.f, 1.f, 1.f, 1.f};
    bool  m_opacitySolid = false;
    int   m_panelTintMixPct = 0;
    int   m_panelBorderPct = 100;
    int   m_panelGrainPct = 0;
    int   m_panelShadowPct = 0;

    static inline std::string s_shaderBasePath = "romfs:/shaders/";
};

} // namespace nxui
