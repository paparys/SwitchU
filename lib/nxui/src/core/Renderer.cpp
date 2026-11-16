#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Font.hpp>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <fstream>

namespace nxui {

namespace {
// Clip rects live in the fixed 1280x720 UI space. Convert them to framebuffer
// pixels with the same scale the viewport applies.
DkScissor uiScissor(const Rect& r, int fbWidth, int fbHeight) {
    const float scaleX = static_cast<float>(fbWidth) / GpuDevice::FB_WIDTH;
    const float scaleY = static_cast<float>(fbHeight) / GpuDevice::FB_HEIGHT;
    const int maxX = fbWidth;
    const int maxY = fbHeight;
    int x0 = (int)std::floor(std::max(0.f, r.x) * scaleX);
    int y0 = (int)std::floor(std::max(0.f, r.y) * scaleY);
    int x1 = (int)std::ceil(
        std::min(static_cast<float>(GpuDevice::FB_WIDTH), r.x + r.width) * scaleX);
    int y1 = (int)std::ceil(
        std::min(static_cast<float>(GpuDevice::FB_HEIGHT), r.y + r.height) * scaleY);
    x0 = std::clamp(x0, 0, maxX);
    y0 = std::clamp(y0, 0, maxY);
    x1 = std::clamp(x1, x0, maxX);
    y1 = std::clamp(y1, y0, maxY);
    return DkScissor{(uint32_t)x0, (uint32_t)y0,
                     (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}
} // namespace

static void ortho(float* m, float w, float h) {
    std::memset(m, 0, 16 * sizeof(float));
    m[ 0] =  2.f / w;
    m[ 5] = -2.f / h;
    m[10] = -1.f;
    m[12] = -1.f;
    m[13] =  1.f;
    m[15] =  1.f;
}

Renderer::Renderer(GpuDevice& gpu) : m_gpu(gpu) {
    m_clipStack.reserve(8);
    // Descriptor slots churn every icon swap; reserving keeps the frame path
    // from growing these vectors mid-render.
    m_freeDescSlots.reserve(64);
    m_deferredDescSlots.reserve(64);
}
Renderer::~Renderer() {}
bool Renderer::initialize() {
    std::printf("[Renderer] Loading shaders...\n");
    if (!loadShaders()) {
        std::printf("[Renderer] Shader initialization FAILED\n");
        return false;
    }
    std::printf("[Renderer] Setting up sampler...\n");
    setupSampler();

    std::printf("[Renderer] Creating 1x1 white texture...\n");
    {
        dk::ImageLayout layout;
        dk::ImageLayoutMaker{m_gpu.device()}
            .setFlags(0)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(1, 1)
            .initialize(layout);

        m_whiteMemBlock = m_gpu.allocImageMemory(layout.getSize());
        if (!m_whiteMemBlock) {
            std::printf("[Renderer] FAILED to allocate memory for white texture\n");
            return false;
        }
        m_whiteImage.initialize(layout, m_whiteMemBlock, 0);

        uint32_t white = 0xFFFFFFFF;
        m_gpu.uploadTexture(m_whiteImage, &white, 4, 1, 1);

        dk::ImageView view{m_whiteImage};
        int slot = registerTexture(view);
        std::printf("[Renderer] White texture registered at slot %d\n", slot);
    }

    if (m_gpu.offscreenReady()) {
        for (int i = 0; i < GpuDevice::NUM_OFFSCREEN; ++i) {
            dk::ImageView view{m_gpu.offscreenImage(i)};
            m_offDescSlot[i] = registerTexture(view);
            std::printf("[Renderer] Offscreen %d registered at slot %d\n", i, m_offDescSlot[i]);
        }
    }

    std::printf("[Renderer] Init complete\n");
    return true;
}

bool Renderer::loadShaders() {
    auto loadDksh = [&](dk::Shader& out, const std::string& path) {
        auto tryLoad = [&](const std::string& candidatePath) {
            std::printf("[Renderer] Loading shader: %s\n", candidatePath.c_str());
            std::ifstream f(candidatePath, std::ios::binary | std::ios::ate);
            if (!f.is_open()) {
                std::printf("[Renderer] FAILED to open shader: %s\n", candidatePath.c_str());
                return false;
            }
            const std::streamoff sz = f.tellg();
            f.seekg(0, std::ios::beg);

            if (sz <= 0) {
                std::printf("[Renderer] Invalid shader size for %s\n", candidatePath.c_str());
                return false;
            }

            uint32_t off = m_gpu.codePool().alloc(static_cast<uint32_t>(sz), DK_SHADER_CODE_ALIGNMENT);
            if (off == UINT32_MAX) {
                std::printf("[Renderer] Code pool alloc FAILED for %lld bytes\n", static_cast<long long>(sz));
                return false;
            }

            void* dst = m_gpu.codePool().cpuAddr(off);
            if (!f.read(static_cast<char*>(dst), static_cast<std::streamsize>(sz))) {
                std::printf("[Renderer] Short shader read for %s\n", candidatePath.c_str());
                return false;
            }

            dk::ShaderMaker{m_gpu.codePool().block, off}.initialize(out);
            return true;
        };

        if (tryLoad(path))
            return true;

        static constexpr const char* kRomfsShaderBase = "romfs:/shaders/";
        if (path.rfind(kRomfsShaderBase, 0) == 0)
            return false;

        std::string fileName = path;
        std::size_t slash = fileName.find_last_of('/');
        if (slash != std::string::npos)
            fileName = fileName.substr(slash + 1);

        std::string fallbackPath = std::string(kRomfsShaderBase) + fileName;
        std::printf("[Renderer] Retrying shader from fallback path: %s\n", fallbackPath.c_str());
        return tryLoad(fallbackPath);
    };

    bool ok = true;

    ok = loadDksh(m_vertShaders[(int)ShaderProgram::Basic], s_shaderBasePath + "basic_vsh.dksh") && ok;
    ok = loadDksh(m_fragShaders[(int)ShaderProgram::Basic], s_shaderBasePath + "basic_fsh.dksh") && ok;

    ok = loadDksh(m_vertShaders[(int)ShaderProgram::Backdrop], s_shaderBasePath + "basic_vsh.dksh") && ok;
    ok = loadDksh(m_fragShaders[(int)ShaderProgram::Backdrop], s_shaderBasePath + "backdrop_fsh.dksh") && ok;

    ok = loadDksh(m_vertShaders[(int)ShaderProgram::BlurH], s_shaderBasePath + "blur_pass_vsh.dksh") && ok;
    ok = loadDksh(m_fragShaders[(int)ShaderProgram::BlurH], s_shaderBasePath + "blur_h_fsh.dksh") && ok;

    ok = loadDksh(m_vertShaders[(int)ShaderProgram::BlurV], s_shaderBasePath + "blur_pass_vsh.dksh") && ok;
    ok = loadDksh(m_fragShaders[(int)ShaderProgram::BlurV], s_shaderBasePath + "blur_v_fsh.dksh") && ok;

    ok = loadDksh(m_vertShaders[(int)ShaderProgram::Wave], s_shaderBasePath + "pass_vsh.dksh") && ok;
    ok = loadDksh(m_fragShaders[(int)ShaderProgram::Wave], s_shaderBasePath + "wave_fsh.dksh") && ok;

    ok = loadDksh(m_vertShaders[(int)ShaderProgram::Gradient], s_shaderBasePath + "basic_vsh.dksh") && ok;
    ok = loadDksh(m_fragShaders[(int)ShaderProgram::Gradient], s_shaderBasePath + "gradient_fsh.dksh") && ok;

    return ok;
}

void Renderer::setupSampler() {
    dk::SamplerDescriptor samDesc;
    samDesc.initialize(
        dk::Sampler{}
            .setFilter(DkFilter_Linear, DkFilter_Linear)
            .setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge)
    );
    std::memcpy(m_gpu.samDescCpuAddr(), &samDesc, sizeof(samDesc));
}

int Renderer::registerTexture(const dk::ImageView& view) {
    int slot = -1;
    if (!m_freeDescSlots.empty()) {
        slot = m_freeDescSlots.back();
        m_freeDescSlots.pop_back();
    } else if (m_nextDescSlot < GpuDevice::MAX_TEXTURES) {
        slot = m_nextDescSlot++;
    }
    if (slot < 0) {
        std::fprintf(stderr, "[Renderer] texture descriptor overflow: slot=%d max=%d\n",
                     m_nextDescSlot, GpuDevice::MAX_TEXTURES);
        return -1;
    }
    dk::ImageDescriptor desc;
    desc.initialize(view);
    auto* base = static_cast<dk::ImageDescriptor*>(m_gpu.imgDescCpuAddr());
    base[slot] = desc;
    m_descDirty = true;   // GPU descriptor cache must be invalidated before next draw
    return slot;
}

void Renderer::releaseTextureSlot(int slot) {
    int firstDynamic = 1 + (m_gpu.offscreenReady() ? GpuDevice::NUM_OFFSCREEN : 0);
    if (slot < firstDynamic || slot >= GpuDevice::MAX_TEXTURES)
        return;
    // Descriptors may still be referenced by queued frames. Three swapchain
    // turns plus one guard frame keep reuse away from in-flight command lists.
    m_deferredDescSlots.push_back({slot, m_frameSerial + 4});
}

void Renderer::reclaimReleasedTextureSlotsAfterIdle() {
    for (const auto& released : m_deferredDescSlots)
        m_freeDescSlots.push_back(released.slot);
    m_deferredDescSlots.clear();
}

void Renderer::updateTexture(int slot, const dk::ImageView& view) {
    dk::ImageDescriptor desc;
    desc.initialize(view);
    auto* base = static_cast<dk::ImageDescriptor*>(m_gpu.imgDescCpuAddr());
    base[slot] = desc;
    m_descDirty = true;   // GPU descriptor cache must be invalidated before next draw
}

void Renderer::resetTextureSlots() {
    int firstFree = 1;
    if (m_gpu.offscreenReady())
        firstFree = 1 + GpuDevice::NUM_OFFSCREEN;
    m_nextDescSlot = firstFree;
    m_curTexSlot = -1;
    m_freeDescSlots.clear();
    m_deferredDescSlots.clear();

    if (m_gpu.offscreenReady()) {
        for (int i = 0; i < GpuDevice::NUM_OFFSCREEN; ++i) {
            dk::ImageView view{m_gpu.offscreenImage(i)};
            dk::ImageDescriptor d;
            d.initialize(view);
            auto* base = static_cast<dk::ImageDescriptor*>(m_gpu.imgDescCpuAddr());
            base[m_offDescSlot[i]] = d;
        }
    }
}

void Renderer::bindTexture(int slot) {
    if (slot != m_curTexSlot) {
        bool newTexturing = (slot >= 0);
        if (newTexturing != m_texturing || slot != m_curTexSlot)
            flush();
        m_curTexSlot = slot;
        m_texturing = newTexturing;
    }
}

void Renderer::useShader(ShaderProgram prog) {
    if (prog == m_curShader) return;
    flush();
    m_curShader = prog;
    // Returning to Basic invalidates the cached per-draw fragment block: a
    // custom push may still be bound in the command stream.
    if (prog == ShaderProgram::Basic)
        m_basicFsUboValid = false;
    int idx = (int)prog;
    auto cmd = m_gpu.cmdBuf();
    cmd.bindShaders(DkStageFlag_GraphicsMask, {&m_vertShaders[idx], &m_fragShaders[idx]});
}

void Renderer::pushFsUniforms(const FsUniforms& fs) {
    auto cmd = m_gpu.cmdBuf();

    // Identical uniforms do not change what the queued vertices sample, so the
    // batch stays intact and the existing block is re-bound. Flushing here was
    // costing one draw call per effect pass even when nothing changed.
    if (m_fsUniformsValid && m_lastFsShader == m_curShader &&
        std::memcmp(&fs, &m_lastFsUniforms, sizeof(fs)) == 0) {
        cmd.bindUniformBuffer(DkStage_Fragment, 1, m_lastFsUboAddr,
                              GpuDevice::FS_UBO_SIZE);
        return;
    }

    flush();
    cmd = m_gpu.cmdBuf();
    int slot = m_gpu.slot();
    auto fsUboAddr = m_gpu.nextFsUboGpuAddr(slot);
    cmd.pushConstants(fsUboAddr, GpuDevice::FS_UBO_SIZE, 0, sizeof(fs), &fs);
    cmd.bindUniformBuffer(DkStage_Fragment, 1, fsUboAddr, GpuDevice::FS_UBO_SIZE);

    m_lastFsUniforms = fs;
    m_lastFsShader = m_curShader;
    m_lastFsUboAddr = fsUboAddr;
    m_fsUniformsValid = true;
    // A custom block supersedes the cached Basic one.
    m_basicFsUboValid = false;
}

void Renderer::beginFrame() {
    ++m_frameSerial;
    m_sharedBackdropValid = false;
    for (std::size_t i = 0; i < m_deferredDescSlots.size();) {
        if (m_deferredDescSlots[i].reusableFrame <= m_frameSerial) {
            m_freeDescSlots.push_back(m_deferredDescSlots[i].slot);
            m_deferredDescSlots[i] = m_deferredDescSlots.back();
            m_deferredDescSlots.pop_back();
        } else {
            ++i;
        }
    }
    int slot = m_gpu.slot();
    m_vtxBase  = static_cast<Vertex2D*>(m_gpu.vtxCpuAddr(slot));
    m_vtxCount = 0;
    m_vtxBatchStart = 0;
    m_drawCalls = 0;
    m_curTexSlot = -1;
    m_texturing  = false;
    m_curShader  = ShaderProgram::Basic;
    m_shapeRadius = 0.f;
    m_shapeThickness = 0.f;
    m_fsUniformsValid = false;
    m_basicFsUboValid = false;
    m_lastFsUboAddr = 0;
    m_basicFsUboAddr = 0;
    m_gpu.resetFsUboRing(slot);
    m_clipStack.clear();
    m_reusableOffscreenCaptureValid = false;

    auto cmd = m_gpu.cmdBuf();

    dk::ImageView colorTarget{m_gpu.fbImage(slot)};
    // 2D UI: bind colour only. Depth testing is disabled below, so the old
    // framebuffer-sized S8 attachment was pure memory and per-pass bandwidth.
    cmd.bindRenderTargets(&colorTarget);

    const int fbWidth  = m_gpu.framebufferWidth();
    const int fbHeight = m_gpu.framebufferHeight();
    cmd.setViewports(0, DkViewport{0.f, 0.f,
        (float)fbWidth, (float)fbHeight, 0.f, 1.f});
    cmd.setScissors(0, DkScissor{0, 0,
        (uint32_t)fbWidth, (uint32_t)fbHeight});

    cmd.clearColor(0, DkColorMask_RGBA, 0.05f, 0.08f, 0.15f, 1.f);

    int idx = (int)ShaderProgram::Basic;
    cmd.bindShaders(DkStageFlag_GraphicsMask, {&m_vertShaders[idx], &m_fragShaders[idx]});

    cmd.bindColorState(dk::ColorState{}.setBlendEnable(0, true));
    dk::BlendState blendState;
    blendState.setFactors(DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha,
                          DkBlendFactor_One, DkBlendFactor_InvSrcAlpha);
    DkBlendState rawBlendState = blendState;
    cmd.bindBlendStates(0, rawBlendState);

    cmd.bindDepthStencilState(dk::DepthStencilState{}.setDepthTestEnable(false));
    cmd.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));

    static const std::array<DkVtxAttribState, 6> attribs = {{
        DkVtxAttribState{0, 0, offsetof(Vertex2D, x), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex2D, u), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex2D, r), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex2D, sx), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex2D, hx), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        DkVtxAttribState{0, 0, offsetof(Vertex2D, rad), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
    }};
    static const DkVtxBufferState bufState = {sizeof(Vertex2D), 0};
    cmd.bindVtxAttribState(attribs);
    cmd.bindVtxBufferState(bufState);

    cmd.bindImageDescriptorSet(m_gpu.imgDescGpuAddr(), GpuDevice::MAX_TEXTURES);
    cmd.bindSamplerDescriptorSet(m_gpu.samDescGpuAddr(), GpuDevice::MAX_SAMPLERS);

    // Invalidate the GPU's descriptor cache so it re-reads all descriptors
    // that were updated via CPU memcpy since the last frame.
    cmd.barrier(DkBarrier_None, DkInvalidateFlags_Descriptors);
    m_descDirty = false;

    updateProjection();
}

void Renderer::endFrame() {
    flush();
}

void Renderer::updateProjection() {
    int slot = m_gpu.slot();
    auto* ubo = static_cast<uint8_t*>(m_gpu.vsUboCpuAddr(slot));
    VsUniforms vs;
    ortho(vs.projection, (float)m_gpu.width(), (float)m_gpu.height());
    std::memcpy(ubo, &vs, sizeof(vs));

    auto cmd = m_gpu.cmdBuf();
    cmd.bindUniformBuffer(DkStage_Vertex, 0, m_gpu.vsUboGpuAddr(slot), GpuDevice::VS_UBO_SIZE);
}

void Renderer::flush() {
    uint32_t batchVerts = m_vtxCount - m_vtxBatchStart;
    if (batchVerts == 0) return;

    auto cmd = m_gpu.cmdBuf();
    int slot = m_gpu.slot();

    // If any image descriptor was written via CPU memcpy since the last
    // barrier, invalidate the GPU's descriptor cache NOW, before the
    // draw call that may reference the updated descriptor.
    if (m_descDirty) {
        cmd.barrier(DkBarrier_None, DkInvalidateFlags_Descriptors);
        m_descDirty = false;
    }

    if (m_curShader == ShaderProgram::Basic) {
        // The Basic program only varies useTexture. Reuse one ring slot while
        // that state holds instead of pushing a fresh block per draw.
        if (!m_basicFsUboValid || m_basicFsTexturing != m_texturing) {
            FsUniforms fs = {};
            fs.useTexture = m_texturing ? 1 : 0;
            auto addr = m_gpu.nextFsUboGpuAddr(slot);
            cmd.pushConstants(addr, GpuDevice::FS_UBO_SIZE,
                              0, sizeof(int32_t) * 4, &fs);
            m_basicFsUboAddr = addr;
            m_basicFsUboValid = true;
            m_basicFsTexturing = m_texturing;
        }
        cmd.bindUniformBuffer(DkStage_Fragment, 1, m_basicFsUboAddr,
                              GpuDevice::FS_UBO_SIZE);
        // Basic's block is the one now bound, so a cached custom block is stale.
        m_fsUniformsValid = false;
    }

    int texSlot = (m_texturing && m_curTexSlot >= 0) ? m_curTexSlot : WHITE_TEX_SLOT;
    cmd.bindTextures(DkStage_Fragment, 0, dkMakeTextureHandle(texSlot, 0));

    DkGpuAddr vtxAddr = m_gpu.vtxGpuAddr(slot) + m_vtxBatchStart * sizeof(Vertex2D);
    cmd.bindVtxBuffer(0, vtxAddr, batchVerts * sizeof(Vertex2D));

    cmd.draw(DkPrimitive_Triangles, batchVerts, 1, 0, 0);
    ++m_drawCalls;

    m_vtxBatchStart = m_vtxCount;
}

// Render target switching

void Renderer::bindRenderTarget(int offscreenIdx) {
    flush();
    if (offscreenIdx == 0)
        m_sharedBackdropValid = false;
    auto cmd = m_gpu.cmdBuf();
    dk::ImageView colorTarget{m_gpu.offscreenImage(offscreenIdx)};
    cmd.bindRenderTargets(&colorTarget);

    constexpr uint32_t offW = GpuDevice::OFFSCREEN_WIDTH;
    constexpr uint32_t offH = GpuDevice::OFFSCREEN_HEIGHT;
    cmd.setViewports(0, DkViewport{0.f, 0.f, (float)offW, (float)offH, 0.f, 1.f});
    cmd.setScissors(0, DkScissor{0, 0, offW, offH});

    int slot = m_gpu.slot();
    VsUniforms vs;
    ortho(vs.projection, (float)offW, (float)offH);
    auto* ubo = static_cast<uint8_t*>(m_gpu.vsUboCpuAddr(slot));
    std::memcpy(ubo, &vs, sizeof(vs));
    cmd.bindUniformBuffer(DkStage_Vertex, 0, m_gpu.vsUboGpuAddr(slot), GpuDevice::VS_UBO_SIZE);
}

void Renderer::restoreRenderTarget() {
    flush();
    auto cmd = m_gpu.cmdBuf();
    int slot = m_gpu.slot();
    dk::ImageView colorTarget{m_gpu.fbImage(slot)};
    // 2D UI: bind colour only. Depth testing is disabled below, so the old
    // framebuffer-sized S8 attachment was pure memory and per-pass bandwidth.
    cmd.bindRenderTargets(&colorTarget);

    const int fbWidth  = m_gpu.framebufferWidth();
    const int fbHeight = m_gpu.framebufferHeight();
    cmd.setViewports(0, DkViewport{0.f, 0.f,
        (float)fbWidth, (float)fbHeight, 0.f, 1.f});
    if (m_clipStack.empty()) {
        cmd.setScissors(0, DkScissor{0, 0,
            (uint32_t)fbWidth, (uint32_t)fbHeight});
    } else {
        cmd.setScissors(0, uiScissor(m_clipStack.back(), fbWidth, fbHeight));
    }

    updateProjection();
}

void Renderer::captureToOffscreen(bool reuseIfValid) {
    if (reuseIfValid && m_reusableOffscreenCaptureValid) {
        return;
    }
    // Several glass widgets sample the same scene. Each capture is a flush, a
    // full barrier and a full-screen blit, so a caller that asked for reuse gets
    // the capture already taken this frame instead of a second one.
    if (reuseIfValid) {
        if (m_reuseCaptureFrame == m_frameSerial)
            return;
        m_reuseCaptureFrame = m_frameSerial;
    }

    m_sharedBackdropValid = false;
    flush();
    auto cmd = m_gpu.cmdBuf();
    int slot = m_gpu.slot();

    // Ensure all current framebuffer writes are visible to the 2D blit engine
    // before we capture the scene behind the glass widget.
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);

    dk::ImageView src{m_gpu.fbImage(slot)};
    dk::ImageView dst{m_gpu.offscreenImage(0)};
    DkImageRect srcRect{
        0,
        0,
        0,
        (uint32_t)m_gpu.framebufferWidth(),
        (uint32_t)m_gpu.framebufferHeight(),
        1,
    };
    DkImageRect dstRect{
        0,
        0,
        0,
        (uint32_t)GpuDevice::OFFSCREEN_WIDTH,
        (uint32_t)GpuDevice::OFFSCREEN_HEIGHT,
        1,
    };
    cmd.blitImage(src, srcRect, dst, dstRect, 0);

    // Barrier: ensure the blit completes before subsequent reads of offscreen 0
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);

    m_reusableOffscreenCaptureValid = reuseIfValid;
}

void Renderer::captureSharedBackdrop(const Rect& region, float radius, int passes) {
    if (!m_gpu.offscreenReady()) return;
    if (region.width <= 0.f || region.height <= 0.f) return;
    if (m_gpu.width() <= 0 || m_gpu.height() <= 0) return;

    // Match applyBlur's effective kernel so the captured margin covers every
    // pass: a radius past the cap becomes extra passes instead.
    float effRadius = std::max(0.f, radius);
    int effPasses = std::max(0, passes);
    if (effRadius > 2.5f && effPasses > 0) {
        const float widen = effRadius / 2.5f;
        effPasses = std::min(24, static_cast<int>(std::ceil(effPasses * widen * widen)));
        effRadius = 2.5f;
    }

    constexpr int offW = GpuDevice::OFFSCREEN_WIDTH;
    constexpr int offH = GpuDevice::OFFSCREEN_HEIGHT;
    const float ox = static_cast<float>(offW) / static_cast<float>(m_gpu.width());
    const float oy = static_cast<float>(offH) / static_cast<float>(m_gpu.height());
    // A 9-tap pass spreads 4 * radius texels in each direction.
    const float margin = static_cast<float>(effPasses) * effRadius * 4.f + 2.f;

    int x0 = static_cast<int>(std::floor(region.x * ox - margin));
    int y0 = static_cast<int>(std::floor(region.y * oy - margin));
    int x1 = static_cast<int>(std::ceil((region.x + region.width) * ox + margin));
    int y1 = static_cast<int>(std::ceil((region.y + region.height) * oy + margin));
    x0 = std::clamp(x0, 0, offW);
    y0 = std::clamp(y0, 0, offH);
    x1 = std::clamp(x1, 0, offW);
    y1 = std::clamp(y1, 0, offH);
    if (x1 <= x0 || y1 <= y0) return;

    if (m_sharedBackdropValid && m_sharedBackdropFrame == m_frameSerial
        && m_sharedBackdropPasses == effPasses
        && std::abs(m_sharedBackdropRadius - effRadius) < 0.001f
        && m_sharedBackdropX0 == x0 && m_sharedBackdropY0 == y0
        && m_sharedBackdropX1 == x1 && m_sharedBackdropY1 == y1) {
        return;
    }

    // Capture only the pixels the blur reads. Scaling the offscreen bounds
    // straight to framebuffer texels keeps the region at the same place in
    // offscreen 0 that drawOffscreen samples.
    const int fbW = m_gpu.framebufferWidth();
    const int fbH = m_gpu.framebufferHeight();
    const int srcX0 = static_cast<int>(std::lround(static_cast<double>(x0) * fbW / offW));
    const int srcY0 = static_cast<int>(std::lround(static_cast<double>(y0) * fbH / offH));
    const int srcX1 = static_cast<int>(std::lround(static_cast<double>(x1) * fbW / offW));
    const int srcY1 = static_cast<int>(std::lround(static_cast<double>(y1) * fbH / offH));
    const int srcW = std::max(0, srcX1 - srcX0);
    const int srcH = std::max(0, srcY1 - srcY0);

    m_sharedBackdropValid = false;
    flush();
    auto cmd = m_gpu.cmdBuf();
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);
    dk::ImageView src{m_gpu.fbImage(m_gpu.slot())};
    dk::ImageView dst{m_gpu.offscreenImage(0)};
    if (srcW > 0 && srcH > 0) {
        DkImageRect srcRect{
            static_cast<std::uint32_t>(srcX0),
            static_cast<std::uint32_t>(srcY0),
            0,
            static_cast<std::uint32_t>(srcW),
            static_cast<std::uint32_t>(srcH),
            1,
        };
        DkImageRect dstRect{
            static_cast<std::uint32_t>(x0),
            static_cast<std::uint32_t>(y0),
            0,
            static_cast<std::uint32_t>(x1 - x0),
            static_cast<std::uint32_t>(y1 - y0),
            1,
        };
        cmd.blitImage(src, srcRect, dst, dstRect, 0);
    }
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);

    const DkScissor scissor{
        static_cast<std::uint32_t>(x0),
        static_cast<std::uint32_t>(y0),
        static_cast<std::uint32_t>(x1 - x0),
        static_cast<std::uint32_t>(y1 - y0),
    };
    applyBlurRegion(effRadius, effPasses, &scissor);

    m_sharedBackdropValid = true;
    m_sharedBackdropFrame = m_frameSerial;
    m_sharedBackdropRadius = effRadius;
    m_sharedBackdropPasses = effPasses;
    m_sharedBackdropX0 = x0;
    m_sharedBackdropY0 = y0;
    m_sharedBackdropX1 = x1;
    m_sharedBackdropY1 = y1;
}

bool Renderer::downloadFramebufferRgba(std::vector<uint8_t>& outRgba,
                                       int& outW, int& outH,
                                       bool halfRes) {
    flush();
    return m_gpu.downloadFramebufferRgba(outRgba, outW, outH, halfRes);
}

void Renderer::copyOffscreen(int srcTarget, int dstTarget) {
    if (!m_gpu.offscreenReady()) return;
    if (srcTarget < 0 || srcTarget >= GpuDevice::NUM_OFFSCREEN) return;
    if (dstTarget < 0 || dstTarget >= GpuDevice::NUM_OFFSCREEN) return;
    if (srcTarget == dstTarget) return;
    // A copy into offscreen 0 replaces the cached frosted backdrop.
    if (dstTarget == 0)
        m_sharedBackdropValid = false;

    flush();

    auto cmd = m_gpu.cmdBuf();
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);

    dk::ImageView src{m_gpu.offscreenImage(srcTarget)};
    dk::ImageView dst{m_gpu.offscreenImage(dstTarget)};
    DkImageRect rect{
        0,
        0,
        0,
        (uint32_t)GpuDevice::OFFSCREEN_WIDTH,
        (uint32_t)GpuDevice::OFFSCREEN_HEIGHT,
        1,
    };
    cmd.blitImage(src, rect, dst, rect, 0);
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);
}

void Renderer::drawOffscreen(int target, const Rect& dest, const Color& tint) {
    useShader(ShaderProgram::Backdrop);
    bindTexture(m_offDescSlot[target]);
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(), 0, 0, 1, 1, tint);
    flush();
    useShader(ShaderProgram::Basic);
}

void Renderer::drawOffscreenRounded(int target, const Rect& dest, float radius, const Color& tint) {
    if (target < 0 || target >= GpuDevice::NUM_OFFSCREEN) return;
    useShader(ShaderProgram::Backdrop);
    bindTexture(m_offDescSlot[target]);
    const Rect uv{dest.x / static_cast<float>(m_gpu.width()),
                  dest.y / static_cast<float>(m_gpu.height()),
                  dest.width / static_cast<float>(m_gpu.width()),
                  dest.height / static_cast<float>(m_gpu.height())};
    if (radius > 0.f) {
        drawRoundedMasked(dest,
                          std::min(radius, std::min(dest.width, dest.height) * 0.5f),
                          tint, uv);
    } else {
        addQuad(dest.x, dest.y, dest.right(), dest.bottom(),
                uv.x, uv.y, uv.right(), uv.bottom(), tint);
    }

    flush();
    useShader(ShaderProgram::Basic);
}

void Renderer::applyBlur(float radius, int passes) {
    applyBlurRegion(radius, passes, nullptr);
}

void Renderer::applyBlurRegion(float radius, int passes, const DkScissor* scissor) {
    if (!m_gpu.offscreenReady()) return;

    constexpr float kMaxSpread = 2.5f;
    constexpr int kMaxPasses = 24;
    if (radius > kMaxSpread && passes > 0) {
        const float widen = radius / kMaxSpread;
        passes = std::min(kMaxPasses,
                          static_cast<int>(std::ceil(passes * widen * widen)));
        radius = kMaxSpread;
    }
    radius = std::max(0.f, radius);
    passes = std::max(0, passes);

    m_reusableOffscreenCaptureValid = false;
    m_sharedBackdropValid = false;
    // The blur overwrites part of offscreen 0, so an earlier "reusable" mark
    // must not let a later shader-glass panel sample this buffer as sharp.
    m_reuseCaptureFrame = 0;

    constexpr float offW = (float)GpuDevice::OFFSCREEN_WIDTH;
    constexpr float offH = (float)GpuDevice::OFFSCREEN_HEIGHT;

    for (int p = 0; p < passes; ++p) {
        // H blur: off0 -> off1. The shader writes alpha 1.0 over a full-screen
        // quad, so the target neither needs a clear before the draw nor keeps
        // anything from the previous pass.
        bindRenderTarget(1);
        if (scissor) m_gpu.cmdBuf().setScissors(0, *scissor);
        useShader(ShaderProgram::BlurH);
        FsUniforms fs = {};
        fs.useTexture = 1;
        fs.param1 = radius;
        fs.param2 = 1.f / offW;
        fs.param3 = 1.f / offH;
        pushFsUniforms(fs);
        bindTexture(m_offDescSlot[0]);
        addQuad(-1.f, -1.f, 1.f, 1.f, 0, 0, 1, 1, Color::white());
        flush();
        m_gpu.cmdBuf().barrier(DkBarrier_Full, DkInvalidateFlags_Image);

        // V blur: off1 -> off0
        bindRenderTarget(0);
        if (scissor) m_gpu.cmdBuf().setScissors(0, *scissor);
        useShader(ShaderProgram::BlurV);
        pushFsUniforms(fs);
        bindTexture(m_offDescSlot[1]);
        addQuad(-1.f, -1.f, 1.f, 1.f, 0, 0, 1, 1, Color::white());
        flush();
        m_gpu.cmdBuf().barrier(DkBarrier_Full, DkInvalidateFlags_Image);
    }

    restoreRenderTarget();
    useShader(ShaderProgram::Basic);
}

void Renderer::applyWave(float time, float amplitude, float frequency) {
    if (!m_gpu.offscreenReady()) return;

    useShader(ShaderProgram::Wave);
    FsUniforms fs = {};
    fs.useTexture = 1;
    fs.param1 = time;
    fs.param2 = amplitude;
    fs.param3 = frequency;
    pushFsUniforms(fs);

    Rect dest = {0, 0, (float)m_gpu.width(), (float)m_gpu.height()};
    bindTexture(m_offDescSlot[0]);
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(), 0, 0, 1, 1, Color::white());
    flush();

    useShader(ShaderProgram::Basic);
}


// Geometry emission

void Renderer::addVertex(float x, float y, float u, float v, const Color& c) {
    if (m_vtxCount >= GpuDevice::MAX_VERTICES) {
        std::printf("[Renderer] WARN: vertex buffer full (%u)\n", m_vtxCount);
        return;
    }
    auto& vtx  = m_vtxBase[m_vtxCount++];
    vtx.x = x; vtx.y = y;
    vtx.u = u; vtx.v = v;
    vtx.sx = x - m_shapeCentre.x; vtx.sy = y - m_shapeCentre.y;
    vtx.hx = m_shapeHalf.x; vtx.hy = m_shapeHalf.y;
    vtx.rad = m_shapeRadius; vtx.thick = m_shapeThickness;
    vtx.r = c.r; vtx.g = c.g; vtx.b = c.b; vtx.a = c.a;
}

void Renderer::addQuad(float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1,
                        const Color& c)
{
    // Straight-alpha blending: a zero-alpha quad contributes nothing. Widgets
    // that fade out keep submitting their geometry for several frames, so
    // dropping it here avoids both the fill and the vertex work.
    if (c.a <= 0.0005f) return;
    // Degenerate quads (collapsed progress bars, zero-size spacers) rasterize
    // nothing but still burn vertex slots and can force a batch flush.
    if (x1 <= x0 || y1 <= y0) return;
    if (m_vtxCount + 6 > GpuDevice::MAX_VERTICES) flush();
    addVertex(x0, y0, u0, v0, c);
    addVertex(x1, y0, u1, v0, c);
    addVertex(x1, y1, u1, v1, c);
    addVertex(x0, y0, u0, v0, c);
    addVertex(x1, y1, u1, v1, c);
    addVertex(x0, y1, u0, v1, c);
}

void Renderer::addQuadGrad(float x0, float y0, float x1, float y1,
                            float u0, float v0, float u1, float v1,
                            const Color& cTop, const Color& cBot)
{
    if (m_vtxCount + 6 > GpuDevice::MAX_VERTICES) flush();
    addVertex(x0, y0, u0, v0, cTop);
    addVertex(x1, y0, u1, v0, cTop);
    addVertex(x1, y1, u1, v1, cBot);
    addVertex(x0, y0, u0, v0, cTop);
    addVertex(x1, y1, u1, v1, cBot);
    addVertex(x0, y1, u0, v1, cBot);
}

void Renderer::addQuadGradH(float x0, float y0, float x1, float y1,
                            float u0, float v0, float u1, float v1,
                            const Color& cLeft, const Color& cRight)
{
    if (m_vtxCount + 6 > GpuDevice::MAX_VERTICES) flush();
    addVertex(x0, y0, u0, v0, cLeft);
    addVertex(x1, y0, u1, v0, cRight);
    addVertex(x1, y1, u1, v1, cRight);
    addVertex(x0, y0, u0, v0, cLeft);
    addVertex(x1, y1, u1, v1, cRight);
    addVertex(x0, y1, u0, v1, cLeft);
}

void Renderer::drawRect(const Rect& r, const Color& c) {
    if (fullyClipped(r)) return;
    bindTexture(-1);
    addQuad(r.x, r.y, r.right(), r.bottom(), 0, 0, 1, 1, c);
}

void Renderer::drawRectOutline(const Rect& r, const Color& c, float t) {
    drawRect({r.x, r.y, r.width, t}, c);
    drawRect({r.x, r.bottom() - t, r.width, t}, c);
    drawRect({r.x, r.y + t, t, r.height - 2*t}, c);
    drawRect({r.right()-t, r.y + t, t, r.height - 2*t}, c);
}

void Renderer::drawGradientRect(const Rect& r, const Color& top, const Color& bottom) {
    bindTexture(-1);
    addQuadGrad(r.x, r.y, r.right(), r.bottom(), 0, 0, 1, 1, top, bottom);
}

void Renderer::drawGradientRectH(const Rect& r, const Color& left, const Color& right) {
    bindTexture(-1);
    addQuadGradH(r.x, r.y, r.right(), r.bottom(), 0, 0, 1, 1, left, right);
}

void Renderer::drawRoundedRect(const Rect& r, const Color& c, float radius) {
    if (radius <= 0.f) { drawRect(r, c); return; }
    bindTexture(-1);
    drawRoundedMasked(r, std::min(radius, std::min(r.width, r.height) * 0.5f),
                      c, Rect{0.f, 0.f, 1.f, 1.f});
}

void Renderer::drawRoundedRectOutline(const Rect& r, const Color& c, float radius, float t) {
    if (radius <= 0.f) { drawRectOutline(r, c, t); return; }
    if (t <= 0.f || r.width <= 0.f || r.height <= 0.f) return;
    const float rad = std::min(radius, std::min(r.width, r.height) * 0.5f);
    bindTexture(-1);
    const float band = t + 2.f;
    if (band * 2.f >= std::min(r.width, r.height)) {
        drawRoundedMasked(r, rad, c, Rect{0.f, 0.f, 1.f, 1.f}, t);
        return;
    }

    beginShape(r, rad, t);
    const float x0 = r.x - 1.f, y0 = r.y - 1.f;
    const float x1 = r.right() + 1.f, y1 = r.bottom() + 1.f;
    const float corner = rad + 1.f;
    const float middleWidth = r.width - 2.f * rad;
    const float middleHeight = r.height - 2.f * rad;
    auto quad = [&](float x, float y, float width, float height) {
        if (width > 0.f && height > 0.f)
            addQuad(x, y, x + width, y + height, 0.f, 0.f, 1.f, 1.f, c);
    };
    quad(x0, y0, corner, corner);
    quad(x1 - corner, y0, corner, corner);
    quad(x0, y1 - corner, corner, corner);
    quad(x1 - corner, y1 - corner, corner, corner);
    quad(r.x + rad, y0, middleWidth, band);
    quad(r.x + rad, y1 - band, middleWidth, band);
    quad(x0, r.y + rad, band, middleHeight);
    quad(x1 - band, r.y + rad, band, middleHeight);
    endShape();
}

void Renderer::drawFrostedInset(const Rect& r, const Color& tint,
                                const Color& border, const Color& highlight,
                                float radius, float opacity) {
    const float alpha = std::clamp(opacity, 0.f, 1.f);
    if (alpha <= 0.01f || r.width <= 0.f || r.height <= 0.f)
        return;

    // Layered shadow: the plate covers the core of each pass, leaving a soft
    // falloff around it instead of one hard offset rectangle.
    constexpr float kShadowSpread[3] = {6.f, 3.f, 0.f};
    constexpr float kShadowWeight[3] = {0.35f, 0.65f, 1.f};
    for (int layer = 0; layer < 3; ++layer) {
        const float s = kShadowSpread[layer];
        drawRoundedRect({r.x - s * 0.5f, r.y + 3.f - s * 0.25f,
                         r.width + s, r.height + s * 1.25f},
                        Color::black().withAlpha(0.12f * alpha * kShadowWeight[layer]),
                        radius + s);
    }
    drawRoundedRect(r, tint.withAlpha(tint.a * alpha), radius);
    drawRoundedRectOutline(r, border.withAlpha(border.a * alpha), radius, 1.f);
    drawRoundedRectOutline(r.shrunk(1.5f),
                           highlight.withAlpha(highlight.a * alpha),
                           std::max(0.f, radius - 1.5f), 1.f);
    drawRoundedRectOutline(r.shrunk(3.f),
                           Color::black().withAlpha(0.045f * alpha),
                           std::max(0.f, radius - 3.f), 1.f);
}

void Renderer::drawCircle(const Vec2& center, float radius, const Color& c, int segments) {
    (void)segments;
    if (radius <= 0.f) return;
    bindTexture(-1);
    const Rect box{center.x - radius, center.y - radius, radius * 2.f, radius * 2.f};
    drawRoundedMasked(box, radius, c, Rect{0.f, 0.f, 1.f, 1.f});
}

void Renderer::drawTriangle(const Vec2& p1, const Vec2& p2, const Vec2& p3, const Color& c) {
    bindTexture(-1);
    addVertex(p1.x, p1.y, 0, 0, c);
    addVertex(p2.x, p2.y, 0, 0, c);
    addVertex(p3.x, p3.y, 0, 0, c);
}

void Renderer::drawGradientTriangle(const Vec2& p1, const Vec2& p2, const Vec2& p3,
                                    const Color& c1, const Color& c2, const Color& c3) {
    bindTexture(-1);
    addVertex(p1.x, p1.y, 0, 0, c1);
    addVertex(p2.x, p2.y, 0, 0, c2);
    addVertex(p3.x, p3.y, 0, 0, c3);
}

void Renderer::drawLine(const Vec2& from, const Vec2& to, const Color& c, float thickness) {
    Vec2 d = (to - from).normalized();
    Vec2 n = {-d.y, d.x};
    float ht = thickness * 0.5f;
    Vec2 a = from + n * ht, b = from - n * ht;
    Vec2 cc = to + n * ht,  dd = to - n * ht;
    bindTexture(-1);
    addVertex(a.x, a.y, 0, 0, c);
    addVertex(b.x, b.y, 0, 0, c);
    addVertex(cc.x, cc.y, 0, 0, c);
    addVertex(b.x, b.y, 0, 0, c);
    addVertex(dd.x, dd.y, 0, 0, c);
    addVertex(cc.x, cc.y, 0, 0, c);
}

void Renderer::drawTexture(const Texture* tex, const Rect& dest, const Color& tint) {
    if (!tex) return;
    if (fullyClipped(dest)) return;
    bindTexture(tex->descriptorSlot());
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(), 0, 0, 1, 1, tint);
}

void Renderer::drawTextureSub(const Texture* tex, const Rect& src, const Rect& dest, const Color& tint) {
    if (!tex) return;
    if (fullyClipped(dest)) return;
    float tw = (float)tex->width(), th = (float)tex->height();
    float u0 = src.x / tw, v0 = src.y / th;
    float u1 = src.right() / tw, v1 = src.bottom() / th;
    bindTexture(tex->descriptorSlot());
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(), u0, v0, u1, v1, tint);
}

void Renderer::beginShape(const Rect& dest, float radius, float thickness) {
    m_shapeCentre = {dest.x + dest.width * 0.5f, dest.y + dest.height * 0.5f};
    m_shapeHalf = {dest.width * 0.5f, dest.height * 0.5f};
    m_shapeRadius = radius;
    m_shapeThickness = thickness;
}

void Renderer::endShape() {
    m_shapeRadius = 0.f;
    m_shapeThickness = 0.f;
}

void Renderer::drawRoundedMasked(const Rect& dest, float radius, const Color& color,
                                 const Rect& uv, float thickness) {
    if (fullyClipped(dest)) return;
    beginShape(dest, radius, thickness);
    addQuad(dest.x, dest.y, dest.right(), dest.bottom(),
            uv.x, uv.y, uv.right(), uv.bottom(), color);
    endShape();
}

void Renderer::drawTextureRounded(const Texture* tex, const Rect& dest, float radius, const Color& tint) {
    if (!tex) { return; }
    if (radius <= 0) { drawTexture(tex, dest, tint); return; }
    bindTexture(tex->descriptorSlot());
    drawRoundedMasked(dest,
                      std::min(radius, std::min(dest.width, dest.height) * 0.5f),
                      tint, Rect{0.f, 0.f, 1.f, 1.f});
}

void Renderer::drawTextureSubRounded(const Texture* tex, const Rect& src,
                                     const Rect& dest, float radius,
                                     const Color& tint) {
    if (!tex) return;
    if (radius <= 0.f) {
        drawTextureSub(tex, src, dest, tint);
        return;
    }
    const float tw = static_cast<float>(tex->width());
    const float th = static_cast<float>(tex->height());
    if (tw <= 0.f || th <= 0.f) return;
    bindTexture(tex->descriptorSlot());
    drawRoundedMasked(dest,
                      std::min(radius, std::min(dest.width, dest.height) * 0.5f),
                      tint,
                      Rect{src.x / tw, src.y / th, src.width / tw, src.height / th});
}

void Renderer::drawText(const std::string& text, const Vec2& pos, Font* font,
                         const Color& color, float scale) {
    if (!font || text.empty()) return;
    font->draw(*this, text, pos, color, scale);
}

bool Renderer::fullyClipped(const Rect& r) const {
    if (m_clipStack.empty()) return false;
    const Rect& clip = m_clipStack.back();
    return r.right() <= clip.x || r.x >= clip.right()
        || r.bottom() <= clip.y || r.y >= clip.bottom();
}

void Renderer::pushClipRect(const Rect& r) {
    flush();
    Rect clip = r;
    if (!m_clipStack.empty()) {
        auto& prev = m_clipStack.back();
        float x0 = std::max(clip.x, prev.x);
        float y0 = std::max(clip.y, prev.y);
        float x1 = std::min(clip.right(), prev.right());
        float y1 = std::min(clip.bottom(), prev.bottom());
        clip = {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
    }
    const int fbWidth  = m_gpu.framebufferWidth();
    const int fbHeight = m_gpu.framebufferHeight();
    m_clipStack.push_back(clip);
    m_gpu.cmdBuf().setScissors(0, uiScissor(clip, fbWidth, fbHeight));
}

void Renderer::popClipRect() {
    flush();
    if (!m_clipStack.empty()) m_clipStack.pop_back();
    const int fbWidth  = m_gpu.framebufferWidth();
    const int fbHeight = m_gpu.framebufferHeight();
    if (m_clipStack.empty()) {
        m_gpu.cmdBuf().setScissors(0, DkScissor{
            0, 0, (uint32_t)fbWidth, (uint32_t)fbHeight});
    } else {
        m_gpu.cmdBuf().setScissors(0, uiScissor(m_clipStack.back(), fbWidth, fbHeight));
    }
}

} // namespace nxui
