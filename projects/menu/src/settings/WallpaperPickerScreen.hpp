#pragma once

#include <nxui/Theme.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <nxui/widgets/GlassWidget.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

/// Full-screen picker for the images in sdmc:/config/SwitchU/wallpapers.
/// Unlike SteamGridDB artwork this is entirely local, so it scans the folder
/// once when opened and decodes the selected file on a worker thread.
class WallpaperPickerScreen final : public nxui::GlassWidget {
public:
    WallpaperPickerScreen(nxui::GpuDevice& gpu, nxui::Renderer& renderer,
                          nxui::ThreadPool& threadPool);

    void setFont(nxui::Font* font) { m_font = font; }
    void setSmallFont(nxui::Font* font) { m_smallFont = font; }
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }

    void show(const std::string& currentPath);
    void hide();
    void wait();
    bool isActive() const { return m_active; }

    void onApply(std::function<void(const std::string&)> cb) {
        m_applyCb = std::move(cb);
    }
    void onClosed(std::function<void()> cb) { m_closedCb = std::move(cb); }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& renderer) override;

private:
    struct Preview {
        std::vector<std::uint8_t> rgba;
        int width = 0;
        int height = 0;
    };
    struct DecodeState {
        Preview image;
        std::atomic<bool> cancelled{false};
    };

    static std::vector<std::string> scanWallpapers();
    static bool decodePreview(const std::string& path, Preview& out);
    static std::string fileName(const std::string& path);

    void moveSelection(int delta);
    void schedulePreview();
    void cancelPreview();
    void applySelection();

    nxui::GpuDevice& m_gpu;
    nxui::Renderer& m_renderer;
    nxui::ThreadPool& m_threadPool;
    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    const nxui::Theme* m_theme = nullptr;

    std::vector<std::string> m_files;
    std::vector<std::future<void>> m_retired;
    nxui::Texture m_texture;
    std::shared_ptr<DecodeState> m_state;
    std::future<void> m_future;
    int m_selected = 0; // 0 is "None"; files start at 1.
    bool m_active = false;
    bool m_loading = false;
    bool m_failed = false;

    std::function<void(const std::string&)> m_applyCb;
    std::function<void()> m_closedCb;
};
