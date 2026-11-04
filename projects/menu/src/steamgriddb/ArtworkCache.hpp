#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace steamgriddb::artwork {

struct DecodedImage {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
};

// `cancel` is polled while scaling and right after the image loads: a request
// that has been superseded by a newer focus stops early instead of holding the
// worker (and the next hero's decode) for the whole 1280x720 pass.
// `useCache == false` skips both the cache read and the cache write: used for
// the fast-scroll preview, which must not leave a second decoded copy per game
// on the SD card.
DecodedImage decode(const std::string& source, int outputWidth, int outputHeight,
                    bool fill, const std::atomic<bool>* cancel = nullptr,
                    bool useCache = true);
bool prepare(const std::string& source, int outputWidth, int outputHeight, bool fill);
void remove(const std::string& source, int outputWidth, int outputHeight);

// Scale to fit inside maxWidth×maxHeight (no letterbox). Cache key is
// source.fit{maxW}x{maxH}.rgba-cache; payload size is the tight output.
DecodedImage decodeFit(const std::string& source, int maxWidth, int maxHeight);
bool prepareFit(const std::string& source, int maxWidth, int maxHeight);
void removeFit(const std::string& source, int maxWidth, int maxHeight);

} // namespace steamgriddb::artwork
