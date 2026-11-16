#include "WallpaperPickerScreen.hpp"

#include "../ui/ChromeTokens.hpp"
#include "../ui/DrawChrome.hpp"

#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iterator>

namespace {
constexpr const char* kWallpaperDir = "sdmc:/config/SwitchU/wallpapers";
constexpr int kPreviewCanvasW = 900;
constexpr int kPreviewCanvasH = 506;

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool supportedWallpaper(const std::filesystem::path& path) {
    const std::string ext = lowerExtension(path);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp"
        || ext == ".gif" || ext == ".mp4" || ext == ".mov" || ext == ".m4v";
}

nxui::Rect containRect(float width, float height, const nxui::Rect& area) {
    if (width <= 0.f || height <= 0.f)
        return area;
    const float scale = std::min(area.width / width, area.height / height);
    const float w = width * scale;
    const float h = height * scale;
    return {area.x + (area.width - w) * 0.5f,
            area.y + (area.height - h) * 0.5f, w, h};
}
} // namespace

WallpaperPickerScreen::WallpaperPickerScreen(nxui::GpuDevice& gpu,
                                             nxui::Renderer& renderer,
                                             nxui::ThreadPool& threadPool)
    : m_gpu(gpu), m_renderer(renderer), m_threadPool(threadPool) {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setVisible(false);
    setFocusable(true);
    setFrameworkTouchEnabled(false);

    auto addDirection = [this](nxui::Button button, int delta) {
        addAction(static_cast<std::uint64_t>(button),
                  [this, delta]() { moveSelection(delta); });
    };
    addDirection(nxui::Button::DLeft, -1);
    addDirection(nxui::Button::LStickL, -1);
    addDirection(nxui::Button::DRight, 1);
    addDirection(nxui::Button::LStickR, 1);
    addDirection(nxui::Button::DUp, -1);
    addDirection(nxui::Button::LStickU, -1);
    addDirection(nxui::Button::DDown, 1);
    addDirection(nxui::Button::LStickD, 1);
    addAction(static_cast<std::uint64_t>(nxui::Button::A),
              [this]() { applySelection(); });
    addAction(static_cast<std::uint64_t>(nxui::Button::B),
              [this]() { hide(); });
}

std::vector<std::string> WallpaperPickerScreen::scanWallpapers() {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    std::error_code ec;
    const fs::path dir = kWallpaperDir;
    fs::create_directories(dir, ec);
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc) || !supportedWallpaper(entry.path()))
            continue;
        files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string WallpaperPickerScreen::fileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool WallpaperPickerScreen::decodePreview(const std::string& path, Preview& out) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* raw = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!raw || width <= 0 || height <= 0) {
        if (raw)
            stbi_image_free(raw);
        return false;
    }

    const float fit = std::min(1.f, std::min(
        static_cast<float>(kPreviewCanvasW) / width,
        static_cast<float>(kPreviewCanvasH) / height));
    out.width = std::max(1, static_cast<int>(std::lround(width * fit)));
    out.height = std::max(1, static_cast<int>(std::lround(height * fit)));
    out.rgba.resize(static_cast<std::size_t>(out.width) *
                    static_cast<std::size_t>(out.height) * 4u);

    for (int y = 0; y < out.height; ++y) {
        const int sourceY = y * height / out.height;
        for (int x = 0; x < out.width; ++x) {
            const int sourceX = x * width / out.width;
            std::memcpy(out.rgba.data()
                            + (static_cast<std::size_t>(y) * out.width + x) * 4u,
                        raw + (static_cast<std::size_t>(sourceY) * width + sourceX) * 4u,
                        4u);
        }
    }
    stbi_image_free(raw);
    return true;
}

void WallpaperPickerScreen::show(const std::string& currentPath) {
    cancelPreview();
    m_files = scanWallpapers();
    m_selected = 0;
    if (!currentPath.empty()) {
        const auto found = std::find(m_files.begin(), m_files.end(), currentPath);
        if (found != m_files.end())
            m_selected = static_cast<int>(std::distance(m_files.begin(), found)) + 1;
    }
    m_active = true;
    m_failed = false;
    setVisible(true);
    schedulePreview();
}

void WallpaperPickerScreen::hide() {
    if (!m_active)
        return;
    m_active = false;
    setVisible(false);
    cancelPreview();
    m_files.clear();
    if (m_closedCb)
        m_closedCb();
}

void WallpaperPickerScreen::wait() {
    cancelPreview();
    for (auto& future : m_retired) {
        if (future.valid())
            future.wait();
    }
    m_retired.clear();
}

void WallpaperPickerScreen::cancelPreview() {
    if (m_state)
        m_state->cancelled.store(true);
    if (m_future.valid())
        m_retired.push_back(std::move(m_future));
    m_state.reset();
    m_future = std::future<void>{};
    m_texture = nxui::Texture{};
    m_loading = false;
    m_failed = false;
}

void WallpaperPickerScreen::schedulePreview() {
    if (m_selected <= 0 || m_selected > static_cast<int>(m_files.size()))
        return;
    m_loading = true;
    m_failed = false;
    m_state = std::make_shared<DecodeState>();
    auto state = m_state;
    const std::string path = m_files[static_cast<std::size_t>(m_selected - 1)];
    m_future = m_threadPool.submit([state, path]() {
        if (state->cancelled.load())
            return;
        Preview preview;
        if (!decodePreview(path, preview))
            return;
        if (!state->cancelled.load())
            state->image = std::move(preview);
    });
}

void WallpaperPickerScreen::moveSelection(int delta) {
    const int count = static_cast<int>(m_files.size()) + 1;
    if (count <= 1)
        return;
    m_selected = (m_selected + delta + count) % count;
    cancelPreview();
    schedulePreview();
}

void WallpaperPickerScreen::applySelection() {
    if (m_selected > 0 && m_selected <= static_cast<int>(m_files.size())) {
        const std::string path = m_files[static_cast<std::size_t>(m_selected - 1)];
        if (m_applyCb)
            m_applyCb(path);
    } else if (m_applyCb) {
        m_applyCb(std::string());
    }
    hide();
}

void WallpaperPickerScreen::onUpdate(float) {
    if (!m_active)
        return;

    for (std::size_t i = 0; i < m_retired.size();) {
        if (!m_retired[i].valid() ||
            m_retired[i].wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++i;
            continue;
        }
        try { m_retired[i].get(); } catch (...) {}
        m_retired.erase(m_retired.begin() + static_cast<std::ptrdiff_t>(i));
    }

    if (!m_future.valid() ||
        m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    try { m_future.get(); } catch (...) {}
    m_future = std::future<void>{};
    auto state = m_state;
    m_state.reset();
    m_loading = false;
    if (!state || state->cancelled.load())
        return;

    auto& image = state->image;
    if (!image.rgba.empty()) {
        m_texture = nxui::Texture{};
        if (!m_texture.loadFromPixels(m_gpu, m_renderer, image.rgba.data(),
                                      image.width, image.height)) {
            m_failed = true;
        }
        image.rgba.clear();
    }
    if (!m_texture.valid())
        m_failed = true;
}

void WallpaperPickerScreen::onRender(nxui::Renderer& renderer) {
    if (!m_active || !m_theme)
        return;

    renderer.drawRect(rect(), nxui::Color(0.f, 0.f, 0.f, 0.62f));
    const nxui::Rect panel{70.f, 38.f, 1140.f, 644.f};
    switchu::ui::chrome::drawPanelShadow(renderer, panel,
                                         switchu::ui::chrome::kRadiusPanel, 1.f);
    renderer.drawRoundedRect(panel,
                             m_theme->panelBase.withAlpha(switchu::ui::chrome::kModalAlpha),
                             switchu::ui::chrome::kRadiusPanel);
    renderer.drawRoundedRectOutline(
        panel, m_theme->panelBorder.withAlpha(switchu::ui::chrome::kModalRim),
        switchu::ui::chrome::kRadiusPanel, 1.5f);

    nxui::Font* titleFont = m_font ? m_font : m_smallFont;
    nxui::Font* bodyFont = m_smallFont ? m_smallFont : m_font;

    if (titleFont) {
        renderer.drawText("Wallpaper", {108.f, 62.f}, titleFont,
                          m_theme->textPrimary, 1.f);
        renderer.drawText(std::string("Folder: ") + kWallpaperDir,
                          {108.f, 104.f}, titleFont, m_theme->textSecondary, 0.78f);
    }
    if (bodyFont) {
        renderer.drawText(
            "PNG, JPEG, WebP, GIF and Motion-JPEG MP4 files appear here.",
            {108.f, 136.f}, bodyFont, m_theme->textSecondary, 0.92f);
        const std::string count = m_files.size() == 1
            ? std::string("1 wallpaper")
            : std::to_string(m_files.size()) + " wallpapers";
        const float countWidth = bodyFont->measure(count).x * 0.92f;
        renderer.drawText(count, {panel.x + panel.width - 38.f - countWidth, 104.f},
                          bodyFont, m_theme->textSecondary, 0.92f);
    }

    const nxui::Rect preview{258.f, 168.f, 764.f, 430.f};
    renderer.drawRoundedRect(preview, m_theme->panelBorder.withAlpha(0.22f), 18.f);

    auto drawCentered = [&](const std::string& text, float y, nxui::Font* font,
                            const nxui::Color& color, float scale) {
        if (!font)
            return;
        const float width = font->measure(text).x * scale;
        renderer.drawText(text, {preview.x + (preview.width - width) * 0.5f, y},
                          font, color, scale);
    };

    if (m_selected == 0) {
        drawCentered("None - theme background", preview.y + 188.f, titleFont,
                     m_theme->textPrimary, 0.95f);
        drawCentered("A removes the wallpaper and restores the theme image.",
                     preview.y + 230.f, bodyFont, m_theme->textSecondary, 0.95f);
    } else if (m_loading) {
        drawCentered("Loading preview...", preview.y + 202.f, titleFont,
                     m_theme->textSecondary, 0.95f);
    } else if (m_failed || !m_texture.valid()) {
        drawCentered(fileName(m_files[static_cast<std::size_t>(m_selected - 1)]),
                     preview.y + 188.f, titleFont, m_theme->textPrimary, 0.95f);
        drawCentered("Preview unavailable - press A to use this wallpaper.",
                     preview.y + 230.f, bodyFont, m_theme->textSecondary, 0.95f);
    } else {
        renderer.drawTexture(&m_texture,
                             containRect(static_cast<float>(m_texture.width()),
                                         static_cast<float>(m_texture.height()),
                                         preview.shrunk(3.f)),
                             nxui::Color::white());
    }
    renderer.drawRoundedRectOutline(preview,
                                    m_theme->panelBorder.withAlpha(0.50f), 18.f, 1.5f);

    if (titleFont && m_files.size() > 1) {
        renderer.drawText("<", {198.f, 350.f}, titleFont,
                          m_theme->textSecondary, 1.8f);
        renderer.drawText(">", {1042.f, 350.f}, titleFont,
                          m_theme->textSecondary, 1.8f);
    }

    const std::string label = m_selected == 0
        ? "None (theme image)"
        : fileName(m_files[static_cast<std::size_t>(m_selected - 1)]);
    if (titleFont) {
        renderer.drawText(label, {108.f, 612.f}, titleFont,
                          m_theme->textPrimary, 0.82f);
        const std::string position = std::to_string(m_selected) + " / "
            + std::to_string(m_files.size() + 1);
        const float width = titleFont->measure(position).x * 0.82f;
        renderer.drawText(position, {panel.x + panel.width - 38.f - width, 612.f},
                          titleFont, m_theme->textSecondary, 0.82f);
    }
    if (bodyFont) {
        if (m_files.empty()) {
            renderer.drawText(
                "No wallpapers found. Put a PNG or JPEG in the folder, then reopen this picker.",
                {108.f, 648.f}, bodyFont, m_theme->textSecondary, 0.92f);
        } else {
            renderer.drawText("A Apply    Left/Right Choose    B Close",
                              {108.f, 648.f}, bodyFont,
                              m_theme->textSecondary, 0.92f);
        }
    }
}
