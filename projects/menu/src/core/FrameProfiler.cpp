#include "FrameProfiler.hpp"

#include "DebugLog.hpp"

#include <chrono>
#include <cstdio>

namespace perf {
namespace {

constexpr int kMaxPasses = 6;
constexpr int kSummaryFrames = 900;

struct PassSlot {
    const char* name = nullptr;
    double totalMs = 0.0;
    int count = 0;
};

PassSlot g_passes[kMaxPasses];
int g_passCount = 0;

double g_frameTotalMs = 0.0;
double g_frameMaxMs = 0.0;
int g_frameSpikes = 0;
int g_frames = 0;

std::uint64_t nowNs() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock::now().time_since_epoch()).count());
}

PassSlot* findSlot(const char* name) {
    for (int i = 0; i < g_passCount; ++i) {
        if (g_passes[i].name == name)
            return &g_passes[i];
    }
    if (g_passCount >= kMaxPasses)
        return nullptr;
    PassSlot& slot = g_passes[g_passCount++];
    slot.name = name;
    return &slot;
}

} // namespace

ScopedPass::ScopedPass(const char* passName)
    : name(passName), startNs(nowNs()) {}

ScopedPass::~ScopedPass() {
    if (!name)
        return;
    PassSlot* slot = findSlot(name);
    if (!slot)
        return;
    const std::uint64_t elapsed = nowNs() - startNs;
    slot->totalMs += static_cast<double>(elapsed) / 1000000.0;
    ++slot->count;
}

void tick(float frameSeconds) {
    const double frameMs = static_cast<double>(frameSeconds) * 1000.0;
    g_frameTotalMs += frameMs;
    if (frameMs > g_frameMaxMs)
        g_frameMaxMs = frameMs;
    if (frameMs > 20.0)
        ++g_frameSpikes;
    ++g_frames;
    if (g_frames < kSummaryFrames)
        return;

    char line[320];
    int used = std::snprintf(line, sizeof(line),
                             "[perf] frame avg=%.2fms max=%.2fms spikes=%d",
                             g_frameTotalMs / g_frames, g_frameMaxMs, g_frameSpikes);
    for (int i = 0; i < g_passCount && used > 0 && used < (int)sizeof(line); ++i) {
        const PassSlot& slot = g_passes[i];
        if (slot.count <= 0)
            continue;
        used += std::snprintf(line + used, sizeof(line) - (size_t)used,
                              " %s=%.2fms", slot.name,
                              slot.totalMs / slot.count);
    }
    DebugLog::log("%s", line);

    g_frameTotalMs = 0.0;
    g_frameMaxMs = 0.0;
    g_frameSpikes = 0;
    g_frames = 0;
    for (int i = 0; i < g_passCount; ++i) {
        g_passes[i].totalMs = 0.0;
        g_passes[i].count = 0;
    }
}

} // namespace perf
