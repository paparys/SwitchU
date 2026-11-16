#pragma once

#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Types.hpp>

#include <string>

namespace nxui {
class Font;
class Renderer;
} // namespace nxui

// Compact search field that lives in the shortcut strip. Activating it opens
// the on-screen keyboard; the app then shows matching games in a result list,
// leaving HOME's grid and selection untouched.
class GameSearchBar final : public nxui::Widget {
public:
    GameSearchBar();

    void setFont(nxui::Font* font) { m_font = font; }
    void setPlaceholder(const std::string& text) { m_placeholder = text; }
    void setTextColor(const nxui::Color& c) { m_text = c; }
    void setMutedColor(const nxui::Color& c) { m_muted = c; }
    void setPanelColor(const nxui::Color& c) { m_panel = c; }
    void setBorderColor(const nxui::Color& c) { m_border = c; }
    // The strip plate already supplies a surface. Match the applet buttons by
    // dropping this icon's own pill while that plate is visible.
    void setChromeVisible(bool visible) { m_chromeVisible = visible; }

    // resultCount < 0 hides the count chip (no active search).
    void setQuery(const std::string& query, int resultCount);
    const std::string& query() const { return m_query; }
    bool hasQuery() const { return !m_query.empty(); }

protected:
    void onRender(nxui::Renderer& ren) override;

private:
    nxui::Font* m_font = nullptr;
    std::string m_placeholder;
    std::string m_query;
    int m_resultCount = -1;
    nxui::Color m_text {1.f, 1.f, 1.f, 0.96f};
    nxui::Color m_muted {1.f, 1.f, 1.f, 0.68f};
    nxui::Color m_panel {0.10f, 0.11f, 0.16f, 0.72f};
    nxui::Color m_border {0.50f, 0.50f, 0.65f, 0.34f};
    bool m_chromeVisible = true;
};
