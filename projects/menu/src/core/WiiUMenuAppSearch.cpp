#include "WiiUMenuApp.hpp"
#include "widgets/ContextMenu.hpp"
#include "widgets/GameSearchBar.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string trimAscii(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
           && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    std::size_t last = value.size();
    while (last > first
           && std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;
    return value.substr(first, last - first);
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

} // namespace

void WiiUMenuApp::beginGameSearch() {
    if (m_editMode || m_openFolderId != 0 || !m_grid)
        return;
    if (m_navigator.route() != switchu::navigation::Route::Home)
        return;
    if ((m_dialog && m_dialog->isActive())
        || (m_settings && m_settings->isActive())
        || (m_themeShop && m_themeShop->isActive())
        || (m_gameOptions && m_gameOptions->isActive())
        || (m_folderOptions && m_folderOptions->isActive()))
        return;

    auto& i18n = nxui::I18n::instance();
    requestTextEntry(i18n.tr("home.search_title", "Search games"),
                     i18n.tr("home.search_guide", "Type part of a game name"),
                     m_gameSearchQuery, 48, false,
                     [this](const std::string& value) {
                         applyGameSearch(value);
                     });
}

void WiiUMenuApp::applyGameSearch(const std::string& rawQuery) {
    if (!m_grid || m_editMode || m_openFolderId != 0)
        return;
    if (m_navigator.route() != switchu::navigation::Route::Home)
        return;

    auto searchBar = m_sidebar.searchBar();
    if (!searchBar || !m_contextMenu)
        return;

    const std::string query = trimAscii(rawQuery);
    m_gameSearchQuery = query;
    if (query.empty()) {
        m_gameSearchCount = -1;
        if (m_contextMenu->isActive())
            m_contextMenu->hide();
        syncGameSearchBar();
        return;
    }

    GridModel root = buildRootFolderModel();
    const std::string needle = lowerAscii(query);
    std::vector<ContextMenu::Item> items;
    int matches = 0;
    for (int i = 0; i < root.count(); ++i) {
        const AppEntry& entry = root.at(i);
        if (!entry.isApplication())
            continue;
        if (lowerAscii(entry.title).find(needle) == std::string::npos
            && lowerAscii(entry.englishTitle).find(needle) == std::string::npos)
            continue;

        std::string label = entry.title.empty() ? entry.englishTitle : entry.title;
        if (label.empty())
            label = std::to_string(entry.titleId);
        ContextMenu::Item item;
        item.label = std::move(label);
        const std::uint64_t titleId = entry.titleId;
        item.onPress = [this, titleId]() { focusGameFromSearch(titleId); };
        items.push_back(std::move(item));
        ++matches;
    }

    m_gameSearchCount = matches;
    syncGameSearchBar();

    if (items.empty()) {
        // Keep the query visible and leave a readable, non-actionable row
        // instead of opening a separate dialog over HOME.
        ContextMenu::Item none;
        none.label = nxui::I18n::instance().tr(
            "home.search_empty", "No games match that name.");
        none.enabled = false;
        items.push_back(std::move(none));
    }

    m_contextMenuReturnFocus = searchBar.get();
    m_contextMenu->show(
        searchBar->rect(),
        nxui::I18n::instance().tr("home.search_results", "Search results"),
        std::move(items));
    focusManager().setFocus(m_contextMenu.get());
    m_audio.playSfx(Sfx::ModalShow);
}

void WiiUMenuApp::clearGameSearch() {
    if (m_gameSearchQuery.empty())
        return;

    if (m_contextMenu && m_contextMenu->isActive())
        m_contextMenu->hide();
    m_gameSearchQuery.clear();
    m_gameSearchCount = -1;
    syncGameSearchBar();
    m_audio.playSfx(Sfx::Navigate);
}

void WiiUMenuApp::focusGameFromSearch(std::uint64_t titleId) {
    if (titleId == 0)
        return;
    if (m_contextMenu && m_contextMenu->isActive())
        m_contextMenu->hide();

    // Make the result an actual jump-to on HOME. Instant focus skips scrolling
    // through the carousel and leaves the player on the matched tile with the
    // normal launch action still available.
    if (!focusTitle(titleId, true))
        return;

    m_gameSearchQuery.clear();
    m_gameSearchCount = -1;
    syncGameSearchBar();
}

void WiiUMenuApp::syncGameSearchBar() {
    if (auto bar = m_sidebar.searchBar()) {
        bar->setQuery(m_gameSearchQuery,
                      m_gameSearchQuery.empty() ? -1 : m_gameSearchCount);
    }
}
