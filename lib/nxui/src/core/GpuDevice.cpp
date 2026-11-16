#include <nxui/core/GpuDevice.hpp>
#include <cstdio>
#include <cstring>
#include <array>
#include <cstdarg>

#ifdef NXUI_BACKEND_DEKO3D
extern "C" {
    void __nx_win_exit(void);
    void __nx_win_init(void);
    // Weak in libnx (0): __nx_win_init opens this managed layer id.
    extern u64 __nx_vi_layer_id;
}
#endif

namespace nxui {

void GpuDevice::logGpu(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (s_logSink) s_logSink(buf);
    else std::fprintf(stderr, "%s\n", buf);
}

static void dkDebugCallback(void* userData, const char* context, DkResult result,
                            const char* message) {
    (void)userData;
    GpuDevice::logGpu("[GpuDevice] deko3d ctx=%s result=%d msg=%s",
                      context ? context : "?", (int)result, message ? message : "");
}

bool GpuDevice::initialize() {
    m_dev   = dk::DeviceMaker{}.setCbDebug(dkDebugCallback).create();
    m_queue = dk::QueueMaker{m_dev}.setFlags(DkQueueFlags_Graphics).create();

    for (int i = 0; i < NUM_FB; ++i) {
        m_cmdPool[i].create(m_dev, CMD_BUF_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        m_cmdbuf[i] = dk::CmdBufMaker{m_dev}.create();
        m_cmdbuf[i].addMemory(m_cmdPool[i].block, 0, CMD_BUF_SIZE);
    }

    m_uploadCmdPool.create(m_dev, 64 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    m_uploadCmdbuf = dk::CmdBufMaker{m_dev}.create();
    m_uploadCmdbuf.addMemory(m_uploadCmdPool.block, 0, 64 * 1024);

    m_codePool.create(m_dev, CODE_POOL_SIZE,
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);

    uint32_t dataSize = 0;
    for (int i = 0; i < NUM_FB; ++i) {
        dataSize += VTX_BUF_SIZE + 256;
        dataSize += IDX_BUF_SIZE + 256;
        dataSize += VS_UBO_SIZE + 256;
        dataSize += FS_UBO_SIZE * FS_UBO_RING + 256;
    }
    dataSize += MAX_TEXTURES * sizeof(DkImageDescriptor) + DK_IMAGE_DESCRIPTOR_ALIGNMENT;
    dataSize += MAX_SAMPLERS * sizeof(DkSamplerDescriptor) + DK_SAMPLER_DESCRIPTOR_ALIGNMENT;
    dataSize = (dataSize + kGpuAlign - 1) & ~(kGpuAlign - 1);
    m_dataPool.create(m_dev, dataSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

    for (int i = 0; i < NUM_FB; ++i) {
        m_vtxOff[i]   = m_dataPool.alloc(VTX_BUF_SIZE, 256);
        m_idxOff[i]   = m_dataPool.alloc(IDX_BUF_SIZE, 256);
        m_vsUboOff[i] = m_dataPool.alloc(VS_UBO_SIZE, DK_UNIFORM_BUF_ALIGNMENT);
        m_fsUboOff[i] = m_dataPool.alloc(FS_UBO_SIZE * FS_UBO_RING, DK_UNIFORM_BUF_ALIGNMENT);
    }
    m_imgDescOff = m_dataPool.alloc(MAX_TEXTURES * sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT);
    m_samDescOff = m_dataPool.alloc(MAX_SAMPLERS * sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT);

    m_stagingPool.create(m_dev, 256 * 1024, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);

    if (!createFramebuffers()) {
        logGpu("[GpuDevice] framebuffer allocation failed");
        return false;
    }
    createOffscreenTargets();

    if (!rebuildSwapchain()) {
        logGpu("[GpuDevice] swapchain creation failed");
        return false;
    }
    return true;
}

bool GpuDevice::createFramebuffers() {
    dk::ImageLayout fbLayout;
    dk::ImageLayoutMaker{m_dev}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
                  DkImageFlags_Usage2DEngine | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions((uint32_t)m_fbWidth, (uint32_t)m_fbHeight)
        .initialize(fbLayout);

    uint64_t fbSize  = fbLayout.getSize();
    uint64_t fbAlign = fbLayout.getAlignment();
    uint32_t totalFb = 0;
    for (int i = 0; i < NUM_FB; ++i) {
        totalFb = (totalFb + fbAlign - 1) & ~(fbAlign - 1);
        totalFb += fbSize;
    }
    m_fbPool.create(m_dev, totalFb, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    if (!m_fbPool.block) {
        logGpu("[GpuDevice] framebuffer pool allocation failed (%u bytes)", totalFb);
        return false;
    }

    uint32_t off = 0;
    for (int i = 0; i < NUM_FB; ++i) {
        off = (off + fbAlign - 1) & ~(fbAlign - 1);
        m_fbImages[i].initialize(fbLayout, m_fbPool.block, off);
        off += fbSize;
    }
    return true;
}

void GpuDevice::createOffscreenTargets() {
    // Half-resolution offscreen targets for blur (640x360)
    constexpr uint32_t offW = OFFSCREEN_WIDTH;
    constexpr uint32_t offH = OFFSCREEN_HEIGHT;

    dk::ImageLayout offLayout;
    dk::ImageLayoutMaker{m_dev}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(offW, offH)
        .initialize(offLayout);

    uint64_t offSize  = offLayout.getSize();
    uint64_t offAlign = offLayout.getAlignment();
    uint32_t totalOff = 0;
    for (int i = 0; i < NUM_OFFSCREEN; ++i) {
        totalOff = (totalOff + offAlign - 1) & ~(offAlign - 1);
        totalOff += offSize;
    }
    m_offPool.create(m_dev, totalOff, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);

    uint32_t off = 0;
    for (int i = 0; i < NUM_OFFSCREEN; ++i) {
        off = (off + offAlign - 1) & ~(offAlign - 1);
        m_offImages[i].initialize(offLayout, m_offPool.block, off);
        off += offSize;
    }
    m_offscreenReady = true;
}

bool GpuDevice::rebuildSwapchain() {
    std::array<DkImage const*, NUM_FB> fbArray;
    for (int i = 0; i < NUM_FB; ++i)
        fbArray[i] = &m_fbImages[i];
    // deko3d applies the NWindow dimensions from the images while creating
    // the chain; the old chain has already released its registrations here.
    m_swapchain = dk::SwapchainMaker{m_dev, nwindowGetDefault(), fbArray}.create();
    if (!m_swapchain) {
        logGpu("[GpuDevice] swapchain creation failed for %dx%d", m_fbWidth, m_fbHeight);
        return false;
    }
    u32 winW = 0, winH = 0;
    if (R_SUCCEEDED(nwindowGetDimensions(nwindowGetDefault(), &winW, &winH))) {
        logGpu("[GpuDevice] swapchain %dx%d (window %ux%u)",
               m_fbWidth, m_fbHeight, winW, winH);
    }
    return true;
}

void GpuDevice::retireImageMemory(dk::UniqueMemBlock block, uint32_t size) {
    if (!block || size == 0)
        return;
    // Two full swapchain cycles of margin, so no queued command list and no
    // descriptor still waiting on its delayed slot release can reach it.
    m_retiredImages.emplace_back();
    RetiredImage& retired = m_retiredImages.back();
    retired.block = static_cast<dk::MemBlock&&>(block);
    retired.size = size;
    retired.framesLeft = 6;
}

int GpuDevice::beginFrame() {
    m_slot = m_queue.acquireImage(m_swapchain);

    // Wait for the GPU to finish the PREVIOUS frame that used this slot's
    // command memory / vertex buffer before we overwrite them.
    m_frameFences[m_slot].wait();

    // Release image memory whose texture was destroyed while the GPU could
    // still reference it. This fence has just signalled, so freeing here cannot
    // stall a frame in flight.
    for (std::size_t i = 0; i < m_retiredImages.size();) {
        if (--m_retiredImages[i].framesLeft <= 0) {
            const uint32_t size = m_retiredImages[i].size;
            m_retiredImages[i].block = nullptr;
            freeImageMemory(size);
            if (i + 1 < m_retiredImages.size())
                m_retiredImages[i] = std::move(m_retiredImages.back());
            m_retiredImages.pop_back();
        } else {
            ++i;
        }
    }

    // Reset command buffer and re-feed its memory (clear invalidates memory
    // tracking — following the deko3d sample framework pattern).
    m_cmdbuf[m_slot].clear();
    m_cmdbuf[m_slot].addMemory(m_cmdPool[m_slot].block, 0, CMD_BUF_SIZE);
    return m_slot;
}

void GpuDevice::endFrame() {
    // Signal the fence for this slot so the NEXT time beginFrame()
    // acquires the same slot, it can wait for completion.
    m_cmdbuf[m_slot].signalFence(m_frameFences[m_slot]);

    auto cmdList = m_cmdbuf[m_slot].finishList();
    m_queue.submitCommands(cmdList);
    m_queue.presentImage(m_swapchain, m_slot);
}

bool GpuDevice::downloadFramebufferRgba(std::vector<uint8_t>& outRgba,
                                        int& outW, int& outH,
                                        bool halfRes) {
    if (m_slot < 0 || !m_queue)
        return false;

    waitForTextureUploads();
    waitIdle();

    const int fullW = m_fbWidth;
    const int fullH = m_fbHeight;
    const bool useHalf = halfRes && m_offscreenReady;
    outW = useHalf ? OFFSCREEN_WIDTH : fullW;
    outH = useHalf ? OFFSCREEN_HEIGHT : fullH;
    if (outW <= 0 || outH <= 0)
        return false;

    const uint32_t byteSize = static_cast<uint32_t>(outW) * static_cast<uint32_t>(outH) * 4u;
    const uint32_t stagingSize = (byteSize + kGpuAlign - 1) & ~(kGpuAlign - 1);
    // MemBlockMaker::create() returns a plain handle; only UniqueMemBlock frees it.
    dk::UniqueMemBlock staging = dk::MemBlockMaker{m_dev, stagingSize}
        .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
        .create();
    if (!staging || !staging.getCpuAddr())
        return false;

    // Full-res path needs an uncompressed blit target: swapchain images use
    // HwCompression, and copyImageToBuffer from those is unreliable / soft.
    dk::Image tempImage;
    dk::UniqueMemBlock tempImageMem;
    if (!useHalf) {
        dk::ImageLayout layout;
        dk::ImageLayoutMaker{m_dev}
            .setFlags(DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(static_cast<uint32_t>(fullW), static_cast<uint32_t>(fullH))
            .initialize(layout);
        const uint32_t imgSize =
            (static_cast<uint32_t>(layout.getSize()) + kGpuAlign - 1) & ~(kGpuAlign - 1);
        tempImageMem = dk::MemBlockMaker{m_dev, imgSize}
            .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
            .create();
        if (!tempImageMem)
            return false;
        tempImage.initialize(layout, tempImageMem, 0);
    }

    m_uploadCmdbuf.clear();
    m_uploadCmdbuf.addMemory(m_uploadCmdPool.block, 0, 64 * 1024);

    dk::ImageView srcView{m_fbImages[m_slot]};
    m_uploadCmdbuf.barrier(DkBarrier_Full, DkInvalidateFlags_Image);
    if (useHalf) {
        dk::ImageView dstView{m_offImages[0]};
        const DkImageRect srcRect{0, 0, 0,
                                  static_cast<uint32_t>(fullW),
                                  static_cast<uint32_t>(fullH), 1};
        const DkImageRect dstRect{0, 0, 0,
                                  static_cast<uint32_t>(outW),
                                  static_cast<uint32_t>(outH), 1};
        m_uploadCmdbuf.blitImage(srcView, srcRect, dstView, dstRect, 0);
        m_uploadCmdbuf.barrier(DkBarrier_Full, DkInvalidateFlags_Image);
        srcView = dk::ImageView{m_offImages[0]};
    } else {
        dk::ImageView dstView{tempImage};
        const DkImageRect rect{0, 0, 0,
                               static_cast<uint32_t>(fullW),
                               static_cast<uint32_t>(fullH), 1};
        m_uploadCmdbuf.blitImage(srcView, rect, dstView, rect, 0);
        m_uploadCmdbuf.barrier(DkBarrier_Full, DkInvalidateFlags_Image);
        srcView = dstView;
    }

    const DkImageRect copyRect{0, 0, 0,
                               static_cast<uint32_t>(outW),
                               static_cast<uint32_t>(outH), 1};
    m_uploadCmdbuf.copyImageToBuffer(
        srcView, copyRect,
        DkCopyBuf{staging.getGpuAddr(), 0, 0});

    // Force a 3D-engine sync so the copy finishes before CPU reads.
    // See deko3d notes on copyImageToBuffer + L2 invalidation.
    const uint32_t threedNop = 0x80000040u;
    m_uploadCmdbuf.replayCmds({threedNop});
    m_uploadCmdbuf.barrier(DkBarrier_None, DkInvalidateFlags_L2Cache);

    m_queue.submitCommands(m_uploadCmdbuf.finishList());
    m_queue.waitIdle();

    outRgba.resize(byteSize);
    std::memcpy(outRgba.data(), staging.getCpuAddr(), byteSize);
    return true;
}

void GpuDevice::waitIdle() {
    if (m_queue) m_queue.waitIdle();
}

void GpuDevice::recreateSwapchain() {
#ifdef NXUI_BACKEND_DEKO3D
    if (!m_dev || !m_queue)
        return;

    // The vi layer/swapchain goes stale across a console sleep: after wake,
    // PresentQueue::acquireImage() blocks forever and the menu freezes on the
    // last presented frame. Wait for in-flight commands, drop the old swapchain
    // so vi releases its buffers, then rebuild it against the current default
    // window.
    m_queue.waitIdle();

    m_swapchain = {};
    rebuildSwapchain();

    // Immediately present the pre-sleep framebuffer so the vi layer never
    // flashes black/garbage during the wake transition. The framebuffers still
    // hold the last frame rendered before sleep, so the screen stays seamless.
    if (m_swapchain) {
        const int slot = m_queue.acquireImage(m_swapchain);
        if (slot >= 0)
            m_queue.presentImage(m_swapchain, slot);
    }

    // Force the next beginFrame() to re-acquire a slot from the fresh swapchain.
    m_slot = -1;
    logGpu("[GpuDevice] swapchain re-created (valid=%d)", (bool)m_swapchain);
#endif
}

void GpuDevice::remountDisplayForCaptureMitm() {
#ifdef NXUI_BACKEND_DEKO3D
    if (!m_dev || !m_queue)
        return;

    m_queue.waitIdle();
    m_swapchain = {};

    // Drop the boot-time NWindow (opened before FT's vi mitm existed).
    // Re-opening the *same* managed layer does not produce setPreallocatedBuffer
    // traffic the mitm can see — create a fresh managed layer so binder
    // handshake matches a HOME-return process restart (~60fps fences).
    ::__nx_win_exit();
    u64 newLayer = 0;
    const Result layerRc = appletCreateManagedDisplayLayer(&newLayer);
    if (R_SUCCEEDED(layerRc) && newLayer != 0)
        ::__nx_vi_layer_id = newLayer;
    ::__nx_win_init();

    // The remounted window starts at its default size; rebuild the chain so
    // the NWindow dimensions are applied to the new managed layer.
    rebuildSwapchain();

    if (m_swapchain) {
        const int slot = m_queue.acquireImage(m_swapchain);
        if (slot >= 0)
            m_queue.presentImage(m_swapchain, slot);
    }

    m_slot = -1;
    logGpu("[GpuDevice] display remounted for FT capture mitm (valid=%d newLayer=0x%llx layerRc=0x%x)",
           (bool)m_swapchain, (unsigned long long)newLayer, layerRc);
#else
    recreateSwapchain();
#endif
}

dk::UniqueMemBlock GpuDevice::allocImageMemory(uint32_t size) {
    size = (size + kGpuAlign - 1) & ~(kGpuAlign - 1);
    if (m_imageMemUsed + size > kDefaultImageBudget) {
        GpuDevice::logGpu( "[GpuDevice] image budget exceeded (%llu + %u > %llu), skipping\n",
                     (unsigned long long)m_imageMemUsed, size,
                     (unsigned long long)kDefaultImageBudget);
        return {};  // return empty MemBlock — caller should check validity
    }
    u64 total = 0;
    u64 used = 0;
    if (R_SUCCEEDED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)) &&
        R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)) &&
        total > used) {
        // Keep a process free-memory cushion so concurrent sysmodules
        // (Frame Tap, Ultrahand, etc.) do not push the menu into soft-locks.
        // Default 2 MiB; the Frame Tap headroom handshake raises this temporarily.
        const u64 headroom = m_allocationHeadroom;
        const u64 freeMemory = total - used;
        if (freeMemory < static_cast<u64>(size) + headroom) {
            GpuDevice::logGpu(
                "[GpuDevice] refusing %u image bytes: free=%llu used=%llu total=%llu headroom=%llu\n",
                size,
                static_cast<unsigned long long>(freeMemory),
                static_cast<unsigned long long>(used),
                static_cast<unsigned long long>(total),
                static_cast<unsigned long long>(headroom));
            return {};
        }
    }
    auto blk = dk::MemBlockMaker{m_dev, size}
        .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        .create();
    if (!blk) {
        GpuDevice::logGpu("[GpuDevice] image allocation failed (%u bytes)\n", size);
        return {};
    }
    m_imageMemUsed += size;
    return blk;
}

void GpuDevice::freeImageMemory(uint32_t size) {
    // A Texture owns a standalone dk::MemBlock which is released immediately
    // after this bookkeeping hook returns. Waiting only for the upload queue
    // is insufficient: an already submitted graphics frame may still sample
    // that image, producing a GPU read page fault when edit-mode destroys its
    // temporary ghost texture. Drain both upload work and graphics work before
    // allowing the MemBlock destructor to run.
    waitForTextureUploads();
    waitIdle();
    size = (size + kGpuAlign - 1) & ~(kGpuAlign - 1);
    if (size <= m_imageMemUsed)
        m_imageMemUsed -= size;
    else
        m_imageMemUsed = 0;
}

GpuDevice::ImageAlloc GpuDevice::allocImageFromPool(uint32_t size, uint32_t alignment) {
    if (alignment < kGpuAlign) alignment = kGpuAlign;
    size = (size + kGpuAlign - 1) & ~(kGpuAlign - 1);
    if (m_imageMemUsed + size > kDefaultImageBudget) {
        GpuDevice::logGpu( "[GpuDevice] pool budget exceeded (%llu + %u > %llu)\n",
                     (unsigned long long)m_imageMemUsed, size,
                     (unsigned long long)kDefaultImageBudget);
        return {};
    }

    u64 total = 0, used = 0;
    if (R_SUCCEEDED(svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)) &&
        R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)) &&
        total > used) {
        const u64 headroom = m_allocationHeadroom;
        const u64 freeMemory = total - used;
        if (freeMemory < static_cast<u64>(size) + headroom) {
            GpuDevice::logGpu(
                "[GpuDevice] pool refusing %u bytes: free=%llu used=%llu total=%llu headroom=%llu\n",
                size,
                static_cast<unsigned long long>(freeMemory),
                static_cast<unsigned long long>(used),
                static_cast<unsigned long long>(total),
                static_cast<unsigned long long>(headroom));
            return {};
        }
    }

    // Try to fit in an existing chunk
    for (auto& chunk : m_imageChunks) {
        uint32_t aligned = (chunk.used + alignment - 1) & ~(alignment - 1);
        if (aligned + size <= chunk.size) {
            chunk.used = aligned + size;
            m_imageMemUsed += size;
            m_poolMemUsed  += size;
            return {chunk.block, aligned};
        }
    }
    // Allocate a new chunk
    uint32_t chunkSize = std::max(kImageChunkSize, size);
    chunkSize = (chunkSize + kGpuAlign - 1) & ~(kGpuAlign - 1);
    auto blk = dk::MemBlockMaker{m_dev, chunkSize}
        .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
        .create();
    if (!blk) {
        GpuDevice::logGpu("[GpuDevice] pooled image allocation failed (%u bytes)\n",
                          chunkSize);
        return {};
    }
    m_imageChunks.push_back({std::move(blk), chunkSize, size});
    m_imageMemUsed += size;
    m_poolMemUsed  += size;
    return {m_imageChunks.back().block, 0};
}

void GpuDevice::resetImagePool() {
    m_imageChunks.clear();
    // Only subtract pool memory; individual allocations remain tracked.
    if (m_poolMemUsed <= m_imageMemUsed)
        m_imageMemUsed -= m_poolMemUsed;
    else
        m_imageMemUsed = 0;
    m_poolMemUsed = 0;
}

bool GpuDevice::uploadTexture(dk::Image& dst, const void* pixels, uint32_t size,
                              uint32_t w, uint32_t h)
{
    // deko3d does not report a bad upload, it calls svcBreak: a crash report
    // resolving to dk::detail::RaiseError under ImageLayout::calcLevelOffset is
    // what that looks like, and one arrived from a 1TB card where the menu had
    // uploaded a great many icons. Nothing here checked the arguments first, so
    // a zero-sized or mismatched upload took the process with it instead of
    // failing. These are the cases that can be caught without asking deko3d.
    if (!pixels || size == 0 || w == 0 || h == 0) {
        std::fprintf(stderr,
                     "[GpuDevice] refusing texture upload %ux%u size=%u pixels=%p\n",
                     w, h, size, pixels);
        return false;
    }
    if (size < (uint64_t)w * h * 4) {
        std::fprintf(stderr,
                     "[GpuDevice] refusing texture upload %ux%u: %u bytes is short of %llu\n",
                     w, h, size, (unsigned long long)((uint64_t)w * h * 4));
        return false;
    }

    if (m_uploadBatchActive) {
        const uint32_t stagingOffset = m_stagingPool.alloc(size, 256);
        if (stagingOffset == UINT32_MAX) {
            GpuDevice::logGpu( "[GpuDevice] upload batch staging budget exceeded (%u bytes)\n", size);
            return false;
        }

        std::memcpy(m_stagingPool.cpuAddr(stagingOffset), pixels, size);
        dk::ImageView view{dst};
        m_uploadCmdbuf.copyBufferToImage(
            {m_stagingPool.gpuAddr(stagingOffset), 0, 0},
            view,
            {0, 0, 0, w, h, 1});
        ++m_uploadBatchCount;
        return true;
    }

    waitForTextureUploads();

    const void* srcCpu = nullptr;
    DkGpuAddr srcGpu = 0;
    dk::UniqueMemBlock tempStaging;

    if (size <= m_stagingPool.size) {
        srcCpu = m_stagingPool.cpuBase;
        srcGpu = m_stagingPool.block.getGpuAddr();
    } else {
        uint32_t allocSize = (size + kGpuAlign - 1) & ~(kGpuAlign - 1);
        tempStaging = dk::MemBlockMaker{m_dev, allocSize}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();

        srcCpu = tempStaging.getCpuAddr();
        srcGpu = tempStaging.getGpuAddr();

        if (!srcCpu || !srcGpu) {
            GpuDevice::logGpu( "[GpuDevice] Failed temp staging allocation for texture (%u bytes)\n", size);
            return false;
        }
    }

    std::memcpy(const_cast<void*>(srcCpu), pixels, size);

    // Use dedicated upload command buffer
    m_uploadCmdbuf.clear();
    m_uploadCmdbuf.addMemory(m_uploadCmdPool.block, 0, 64 * 1024);

    dk::ImageView view{dst};
    m_uploadCmdbuf.copyBufferToImage(
        {srcGpu, 0, 0},
        view,
        {0, 0, 0, w, h, 1}
    );

    m_queue.submitCommands(m_uploadCmdbuf.finishList());
    m_queue.waitIdle();
    return true;
}

void GpuDevice::beginTextureUploadBatch() {
    if (m_uploadBatchActive)
        return;
    waitForTextureUploads();

    m_stagingPool.used = 0;
    m_uploadCmdbuf.clear();
    m_uploadCmdbuf.addMemory(m_uploadCmdPool.block, 0, 64 * 1024);
    m_uploadBatchCount = 0;
    m_uploadBatchActive = true;
}

void GpuDevice::waitForTextureUploads() {
    if (!m_uploadInFlight)
        return;
    m_uploadFence.wait();
    m_uploadInFlight = false;
}

void GpuDevice::endTextureUploadBatch() {
    if (!m_uploadBatchActive)
        return;
    m_uploadBatchActive = false;
    if (m_uploadBatchCount == 0)
        return;

    m_uploadCmdbuf.signalFence(m_uploadFence);
    m_queue.submitCommands(m_uploadCmdbuf.finishList());
    m_uploadInFlight = true;
    m_uploadBatchCount = 0;
}

void GpuDevice::shutdown() {
    // Members are released before the device goes away: a UniqueMemBlock must
    // never outlive the deko3d device that allocated it.
    m_retiredImages.clear();
    if (m_uploadBatchActive)
        endTextureUploadBatch();
    if (m_queue) m_queue.waitIdle();
    m_swapchain    = {};
    for (int i = 0; i < NUM_FB; ++i)
        m_cmdbuf[i] = {};
    m_uploadCmdbuf = {};
    m_queue        = {};
    m_imageChunks.clear();
    m_imageMemUsed = 0;
    m_poolMemUsed  = 0;
    m_fbPool      = {};
    m_offPool     = {};
    m_offscreenReady = false;
    for (int i = 0; i < NUM_FB; ++i)
        m_cmdPool[i] = {};
    m_uploadCmdPool = {};
    m_codePool    = {};
    m_dataPool  = {};
    m_imagePool = {};
    m_stagingPool = {};
    m_dev       = {};
}

} // namespace nxui
