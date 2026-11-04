#pragma once

#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>

#include <cstddef>
#include <string>
#include <vector>

/// Looping wallpaper animation for the HOME background.
///
/// The menu runs on real hardware with a fixed GPU budget, so this never
/// allocates a texture per displayed frame. Every supported file is turned into
/// a small set of evenly sampled frames (<= budget), uploaded once, and cycled
/// on the CPU. That keeps memory flat no matter how long the source clip is.
///
/// Supported containers:
///   * animated WebP (.webp)            - decoded with libwebp demux
///   * animated GIF (.gif)              - decoded with stb_image's GIF loader
///   * Motion-JPEG inside MP4 (.mp4/.mov) - ISO-BMFF sample table + stb_image
///
/// H.264 MP4 cannot be decoded without the console's NVDEC pipeline; those
/// files are reported as unsupported instead of allocating a decoder we cannot
/// feed safely from a sysmodule.
class AnimatedWallpaper {
public:
    AnimatedWallpaper() = default;
    ~AnimatedWallpaper() = default;

    AnimatedWallpaper(const AnimatedWallpaper&) = delete;
    AnimatedWallpaper& operator=(const AnimatedWallpaper&) = delete;

    /// Loads and pre-decodes the animation. targetFps only drives playback
    /// pacing; sample selection always spreads frames across the whole clip.
    bool load(nxui::GpuDevice& gpu, nxui::Renderer& ren,
              const std::string& path, int targetFps);

    void clear();

    /// Advances playback. Cheap: only swaps the active index.
    void update(float dt);

    nxui::Texture* texture();
    const nxui::Texture* texture() const;

    bool valid() const { return !m_frames.empty(); }
    bool animated() const { return m_frames.size() > 1; }
    std::size_t frameCount() const { return m_frames.size(); }
    const std::string& path() const { return m_path; }
    /// Set when a file was recognised but could not be turned into frames.
    const std::string& error() const { return m_error; }

private:
    std::vector<nxui::Texture> m_frames;
    std::vector<float> m_frameSeconds;
    std::size_t m_index = 0;
    float m_elapsed = 0.f;
    std::string m_path;
    std::string m_error;
};
