#pragma once

#include <cstdint>

namespace perf {

// Measures one named span on the CPU and accumulates it for the rolling
// summary. Scopes may be nested; each records its own wall-clock time.
struct ScopedPass {
    const char* name;
    std::uint64_t startNs;

    explicit ScopedPass(const char* name);
    ~ScopedPass();
};

// Call once per presented frame with the frame delta. Every 300 frames a
// summary of the frame time and the named passes is written to the debug log.
void tick(float frameSeconds);

} // namespace perf
