#include "GameOptionsScreen.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace {
constexpr float kPanelWidth = 920.f;
constexpr float kPanelHeight = 656.f;
constexpr float kPanelTop = 32.f;
constexpr float kPanelMargin = 32.f;

// Byte offset of every UTF-8 codepoint boundary, plus the end offset. The old
// loop popped one byte at a time, which could also cut a multibyte character in
// half and leave TTF measuring an invalid tail.
std::vector<std::size_t> utf8Boundaries(const std::string& text) {
    std::vector<std::size_t> bounds;
    bounds.reserve(text.size() + 1);
    for (std::size_t i = 0; i < text.size();) {
        bounds.push_back(i);
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t step = 1;
        if ((c & 0xE0u) == 0xC0u) step = 2;
        else if ((c & 0xF0u) == 0xE0u) step = 3;
        else if ((c & 0xF8u) == 0xF0u) step = 4;
        i += std::min(step, text.size() - i);
    }
    bounds.push_back(text.size());
    return bounds;
}

// Shorten to fit using a binary search over codepoint prefixes. Removing one
// character at a time and re-measuring the whole string cost roughly 120 font
// queries for a long description, once per focused row; this needs about seven.
std::string ellipsize(nxui::Font* font, const std::string& text, float maxWidth, float scale) {
    if (!font || font->measure(text).x * scale <= maxWidth)
        return text;

    const std::vector<std::size_t> bounds = utf8Boundaries(text);
    const std::string tail = "...";
    std::size_t lo = 0;
    std::size_t hi = bounds.size() - 1;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo + 1) / 2;
        const std::string candidate = text.substr(0, bounds[mid]) + tail;
        if (font->measure(candidate).x * scale <= maxWidth)
            lo = mid;
        else
            hi = mid - 1;
    }
    return text.substr(0, bounds[lo]) + tail;
}
}

GameOptionsScreen::GameOptionsScreen()
    : TabbedOverlayScreen(ScreenMode::GameOptions) {
    setRect({(1280.f - kPanelWidth) * 0.5f, kPanelTop,
             kPanelWidth, kPanelHeight});
}

void GameOptionsScreen::setIconAnchor(const nxui::Rect& iconRect) {
    float x = (1280.f - kPanelWidth) * 0.5f;
    if (iconRect.width > 0.f) {
        const float iconCenter = iconRect.x + iconRect.width * 0.5f;
        x = (iconCenter < 640.f)
            ? 1280.f - kPanelWidth - kPanelMargin
            : kPanelMargin;
    }
    setRect({x, kPanelTop, kPanelWidth, kPanelHeight});
}

void GameOptionsScreen::setGame(const GameInfo& info) {
    m_game = info;
    m_tabs.clear();
    m_cachedTabContentWidgets.clear();
    // show() always opens on the first tab. Build that tab here instead of
    // whichever tab was left selected by the previous game, then throwing it
    // away when show() resets the index.
    m_tabIndex = 0;
    m_contentIdx = 0;
    m_scrollY = 0.f;
    m_scrollTarget = 0.f;
    m_scrollVel = 0.f;
    m_scrollMomentum = 0.f;
    m_tabScrollY = 0.f;
    m_tabScrollTarget = 0.f;
    m_tabScrollVel = 0.f;
    m_tabScrollMomentum = 0.f;
    m_touchScrollVel = 0.f;
    m_focusArea = FocusArea::Tabs;
    invalidateHeaderCache();
    if (isActive()) {
        // Replacing the game while the sheet is on screen needs its widgets
        // updated immediately.
        rebuildCurrentTab();
    } else {
        // show() builds the first tab. Warming it here as well built the same
        // rail and rows twice before the sheet could open.
        buildTabs();
    }
}

void GameOptionsScreen::setFolders(std::vector<FolderEntry> folders) {
    bool unchanged = folders.size() == m_folders.size();
    if (unchanged) {
        for (std::size_t i = 0; i < folders.size(); ++i) {
            if (folders[i].id != m_folders[i].id
                || folders[i].name != m_folders[i].name
                || folders[i].containsGame != m_folders[i].containsGame) {
                unchanged = false;
                break;
            }
        }
    }
    m_folders = std::move(folders);
    if (unchanged)
        return;
    // setGame() builds the rows whenever the sheet opens, so an inactive sheet
    // already has the new list. Only a live sheet needs the rows refreshed.
    if (isActive()) {
        const int tab = m_tabIndex;
        const int content = m_contentIdx;
        buildTabs();
        m_tabIndex = std::clamp(tab, 0, std::max(0, static_cast<int>(m_tabs.size()) - 1));
        ensureTabLoaded(m_tabIndex);
        rebuildContentItems();
        const int focusables = focusableCount();
        m_contentIdx = focusables > 0 ? std::clamp(content, 0, focusables - 1) : 0;
    }
}

void GameOptionsScreen::invalidateHeaderCache() {
    m_headerCacheValid = false;
    m_headerTextWidth = -1.f;
    m_headerFallbackTab = -2;
    m_headerFallbackCount = -1;
    m_headerFallbackDescription.clear();
    // The description caches independently now, so drop it here; otherwise a
    // game with no focused description would keep the previous game's line.
    m_headerDescription.clear();
    m_headerDescriptionSource.clear();
}

void GameOptionsScreen::refreshArtworkPresence(bool hasHero, bool hasLogo, bool hasIcon,
                                               bool hasPoster, bool hasPosterWide) {
    // Callers fire this on every SteamGridDB apply and clear. Rebuilding all
    // five tabs (with their i18n strings and row widgets) for a row set that
    // did not change only cost a frame.
    if (m_game.hasHeroArt == hasHero && m_game.hasLogoArt == hasLogo
        && m_game.hasIconArt == hasIcon && m_game.hasPosterArt == hasPoster
        && m_game.hasPosterWideArt == hasPosterWide)
        return;
    m_game.hasHeroArt = hasHero;
    m_game.hasLogoArt = hasLogo;
    m_game.hasIconArt = hasIcon;
    m_game.hasPosterArt = hasPoster;
    m_game.hasPosterWideArt = hasPosterWide;
    if (!m_active && !m_animating)
        return;
    const int tab = m_tabIndex;
    const std::string activeTabName =
        (tab >= 0 && tab < (int)m_tabs.size())
            ? m_tabs[static_cast<std::size_t>(tab)].name
            : std::string();
    const int content = m_contentIdx;
    const FocusArea area = m_focusArea;
    buildTabs();
    m_tabIndex = std::clamp(tab, 0, std::max(0, (int)m_tabs.size() - 1));
    if (!activeTabName.empty()) {
        for (int i = 0; i < (int)m_tabs.size(); ++i) {
            if (m_tabs[static_cast<std::size_t>(i)].name == activeTabName) {
                m_tabIndex = i;
                break;
            }
        }
    }
    ensureTabLoaded(m_tabIndex);
    rebuildContentItems();
    const int focusables = focusableCount();
    m_contentIdx = focusables > 0 ? std::clamp(content, 0, focusables - 1) : 0;
    m_focusArea = area;
}

void GameOptionsScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    // buildTabs() is also called when translations are refreshed. Rebuild
    // from a clean snapshot instead of appending to the previous language's
    // tabs, otherwise every language change duplicates the rail.
    m_tabs.clear();
    m_footerHintsText.clear();
    m_footerHintsKey = -1;

    Tab data;
    data.name = i18n.tr("game.tab_options", "Options");

    {
        SettingItem section;
        section.label = i18n.tr("game.section_home", "HOME menu");
        section.type = ItemType::Section;
        data.items.push_back(std::move(section));
    }

    SettingItem shortcut;
    shortcut.label = m_shortcutPinned
        ? i18n.tr("game.shortcut_remove", "Remove from shortcuts")
        : i18n.tr("game.shortcut_add", "Add to shortcuts");
    shortcut.buttonLabel = m_shortcutPinned
        ? i18n.tr("button.remove", "Remove")
        : i18n.tr("button.add", "Add");
    shortcut.description = i18n.tr("game.shortcut_desc",
        "Puts this title in the shortcut row on HOME. There are six slots; "
        "the shortcut opens the game.");
    shortcut.type = ItemType::Action;
    shortcut.onChange = [this](SettingItem&) {
        if (m_shortcutCb) m_shortcutCb();
    };
    data.items.push_back(std::move(shortcut));

    if (!m_shortcutPinned) {
        const auto addSide = [this, &data, &i18n](int side,
                                                  const char* labelKey,
                                                  const char* label,
                                                  const char* descKey,
                                                  const char* desc) {
            SettingItem row;
            row.label = i18n.tr(labelKey, label);
            row.buttonLabel = i18n.tr("button.add", "Add");
            row.description = i18n.tr(descKey, desc);
            row.type = ItemType::Action;
            row.onChange = [this, side](SettingItem&) {
                if (m_addShortcutSideCb)
                    m_addShortcutSideCb(side);
            };
            data.items.push_back(std::move(row));
        };
        addSide(0, "game.shortcut_add_left", "Add to shortcuts on the left",
                "game.shortcut_add_left_desc",
                "Puts this title on the left side of the HOME shortcut row.");
        addSide(1, "game.shortcut_add_right", "Add to shortcuts on the right",
                "game.shortcut_add_right_desc",
                "Puts this title on the right side of the HOME shortcut row.");
    }

    SettingItem rename;
    rename.label = i18n.tr("game.rename", "Rename game");
    rename.buttonLabel = i18n.tr("button.rename", "Rename");
    rename.description = i18n.tr("game.rename_desc",
        "Changes the name shown on HOME and in the carousel. The title itself "
        "is untouched.");
    rename.type = ItemType::Action;
    rename.onChange = [this](SettingItem&) { if (m_renameCb) m_renameCb(); };
    data.items.push_back(std::move(rename));

    SettingItem hide;
    hide.label = i18n.tr("game.hide_from_home", "Hide from HOME");
    hide.buttonLabel = i18n.tr("button.hide", "Hide");
    hide.description = i18n.tr("game.hide_desc",
        "Removes this title from the HOME menu. It stays installed and can be "
        "brought back from Settings > Hidden Games.");
    hide.type = ItemType::Action;
    hide.onChange = [this](SettingItem&) { if (m_hideCb) m_hideCb(); };
    data.items.push_back(std::move(hide));

    if (m_game.canResize) {
        static const char* kTileSizeLabels[] = {"1x1", "2x1", "4x2", "2x2", "2x3"};
        std::vector<int> availableSizes;
        for (int index : m_game.tileSizeIndices) {
            if (index >= 0 && index < 5)
                availableSizes.push_back(index);
        }
        if (availableSizes.empty())
            availableSizes.push_back(0);

        SettingItem size;
        if (m_game.tileSizeFixed) {
            // Poster Wall and other fixed arrangements keep every game at 1x1.
            size.label = i18n.tr("game.tile_size_fixed",
                                 "Tile size (fixed by HOME layout)");
            size.description = i18n.tr("game.tile_size_fixed_desc",
                "This HOME layout uses the same tile size for every game. "
                "Switch to Shelf or Showcase to choose a larger tile.");
            size.type = ItemType::Info;
            size.infoText = "1x1";
        } else {
            size.label = i18n.tr("game.tile_size", "Tile size");
            size.description = i18n.tr(
                "game.tile_size_desc", "Changes the size of this game on the grid.");
            size.type = ItemType::Selector;
            // ASCII "x" avoids mojibake if the source encoding is wrong.
            // 1x2 dropped — cell aspect (~1:2) can't match poster (2:3); use 2x3.
            // 3x2 dropped — SGDB "Poster wide" (460x215) crops badly in a 3x2 cell.
            // 4x2 kept — same 2:1 aspect as 2x1 (larger twin; Hero / Poster wide).
            for (int index : availableSizes)
                size.options.emplace_back(kTileSizeLabels[index]);
            const auto current = std::find(availableSizes.begin(), availableSizes.end(),
                                           m_game.sizeIndex);
            size.intVal = current == availableSizes.end()
                ? 0 : static_cast<int>(current - availableSizes.begin());
            size.onChange = [this, availableSizes](SettingItem& self) {
                const int pos = std::clamp(self.intVal, 0,
                    static_cast<int>(availableSizes.size()) - 1);
                m_game.sizeIndex = availableSizes[static_cast<std::size_t>(pos)];
                if (m_resizeCb) m_resizeCb(m_game.sizeIndex);
            };
        }
        data.items.push_back(std::move(size));
    }

    // The icon aspect lived on a one-row tab; keep it with the other HOME
    // tile controls so the sheet has fewer rail entries to scan.
    {
        SettingItem aspect;
        aspect.label = i18n.tr("game.icon_aspect", "Aspect ratio");
        aspect.description = i18n.tr("game.icon_aspect_desc",
            "Shapes this game's icon on HOME. 3:4 is tall, 16:9 is wide.");
        aspect.type = ItemType::Selector;
        aspect.options = {"3:4", "16:9", "1:1", "4:3"};
        aspect.intVal = std::clamp(m_game.aspectIndex, 0, 3);
        aspect.onChange = [this](SettingItem& self) {
            m_game.aspectIndex = std::clamp(self.intVal, 0, 3);
            if (m_aspectCb) m_aspectCb(m_game.aspectIndex);
        };
        data.items.push_back(std::move(aspect));
    }

    {
        SettingItem section;
        section.label = i18n.tr("game.section_folders", "Folders");
        section.type = ItemType::Section;
        data.items.push_back(std::move(section));
    }

    if (m_folders.empty()) {
        SettingItem none;
        none.label = i18n.tr("game.folder_none", "No folders yet");
        none.description = i18n.tr("game.folder_none_desc",
            "Create a folder to group this title with others on HOME.");
        none.type = ItemType::Info;
        data.items.push_back(std::move(none));
    }

    for (const auto& folder : m_folders) {
        SettingItem row;
        row.label = folder.name;
        row.buttonLabel = folder.containsGame
            ? i18n.tr("button.remove", "Remove")
            : i18n.tr("button.add", "Add");
        row.description = folder.containsGame
            ? i18n.tr("game.folder_remove_desc",
                "This title is in this folder. Remove it to put it back on HOME.")
            : i18n.tr("game.folder_add_desc",
                "Adds this title to the folder. The folder stays on the HOME carousel.");
        row.type = ItemType::Action;
        const std::uint32_t folderId = folder.id;
        row.secondaryButtonLabel = i18n.tr("button.rename", "Rename");
        row.onSecondary = [this, folderId](SettingItem&) {
            if (m_folderRenameCb)
                m_folderRenameCb(folderId);
        };
        row.onChange = [this, folderId](SettingItem&) {
            if (m_folderToggleCb)
                m_folderToggleCb(folderId);
        };
        data.items.push_back(std::move(row));
    }

    {
        SettingItem create;
        create.label = i18n.tr("game.folder_new", "New folder");
        create.buttonLabel = i18n.tr("button.create", "Create");
        create.description = i18n.tr("game.folder_new_desc",
            "Creates a folder, adds this title to it and places the folder on the HOME carousel.");
        create.type = ItemType::Action;
        create.onChange = [this](SettingItem&) {
            if (m_createFolderCb)
                m_createFolderCb();
        };
        data.items.push_back(std::move(create));
    }

    {
        SettingItem section;
        section.label = i18n.tr("game.section_software", "Software");
        section.type = ItemType::Section;
        data.items.push_back(std::move(section));
    }

    if (m_game.canMove) {
        SettingItem move;
        move.label = i18n.tr("game.move_title", "Move software");
        move.buttonLabel = i18n.tr("button.move", "Move");
        move.description = i18n.tr("game.move_desc", "Move this software to another position on the HOME menu.");
        move.type = ItemType::Action;
        move.onChange = [this](SettingItem&) { if (m_moveCb) m_moveCb(); };
        data.items.push_back(std::move(move));
    }

    if (m_game.suspended) {
        SettingItem close;
        close.label = i18n.tr("game.close_title", "Close software");
        close.buttonLabel = i18n.tr("button.close", "Close");
        close.description = i18n.tr("game.close_desc", "Close the running software. Unsaved progress may be lost.");
        close.type = ItemType::Action;
        close.onChange = [this](SettingItem&) { if (m_closeSoftwareCb) m_closeSoftwareCb(); };
        data.items.push_back(std::move(close));
    }

    {
        SettingItem section;
        section.label = i18n.tr("game.section_danger", "Danger zone");
        section.type = ItemType::Section;
        data.items.push_back(std::move(section));
    }

    SettingItem erase;
    erase.label = i18n.tr("game.delete_software", "Delete Software");
    erase.buttonLabel = i18n.tr("button.uninstall", "Uninstall");
    erase.description = m_game.gameCard
        ? i18n.tr("game.delete_gamecard_unavailable", "Game card software cannot be deleted from the console.")
        : i18n.tr("game.delete_software_desc", "Deletes the software from the console. Save data is not deleted.");
    erase.type = m_game.gameCard ? ItemType::Info : ItemType::Action;
    erase.onChange = [this](SettingItem&) { if (m_deleteSoftwareCb) m_deleteSoftwareCb(); };
    data.items.push_back(std::move(erase));
    m_tabs.push_back(std::move(data));

    Tab info;
    info.name = i18n.tr("game.information", "Information");
    {
        SettingItem section;
        section.label = i18n.tr("game.section_details", "Details");
        section.type = ItemType::Section;
        info.items.push_back(std::move(section));
    }
    SettingItem version;
    version.label = i18n.tr("game.version", "Version");
    version.type = ItemType::Info;
    version.infoText = m_game.version;
    info.items.push_back(std::move(version));

    char id[17]{};
    std::snprintf(id, sizeof(id), "%016lX", static_cast<unsigned long>(m_game.titleId));
    SettingItem titleId;
    titleId.label = i18n.tr("game.title_id", "Title ID");
    titleId.type = ItemType::Info;
    titleId.infoText = id;
    info.items.push_back(std::move(titleId));
    m_tabs.push_back(std::move(info));

    Tab artwork;
    artwork.name = "SteamGridDB";
    {
        SettingItem section;
        section.label = i18n.tr("game.section_artwork", "Artwork");
        section.type = ItemType::Section;
        artwork.items.push_back(std::move(section));
    }
    auto addArtworkActions = [this, &artwork, &i18n](const char* labelKey,
                                                     const char* label,
                                                     const char* descriptionKey,
                                                     const char* description,
                                                     ArtworkKind kind,
                                                     bool hasArt) {
        SettingItem row;
        row.label = i18n.tr(labelKey, label);
        row.buttonLabel = i18n.tr("button.select", "Select");
        if (hasArt) {
            row.secondaryButtonLabel = i18n.tr("button.clear_x", "×");
            row.onSecondary = [this, kind](SettingItem&) {
                if (m_clearArtworkCb) m_clearArtworkCb(kind);
            };
        }
        row.description = i18n.tr(descriptionKey, description);
        row.type = ItemType::Action;
        row.onChange = [this, kind](SettingItem&) {
            if (m_selectArtworkCb) m_selectArtworkCb(kind);
        };
        artwork.items.push_back(std::move(row));
    };
    addArtworkActions("game.steamgriddb.hero", "Hero",
                      "game.steamgriddb.hero_desc",
                      "Choose a hero image.",
                      ArtworkKind::Hero, m_game.hasHeroArt);
    addArtworkActions("game.steamgriddb.logo", "Logo",
                      "game.steamgriddb.logo_desc",
                      "Choose a logo.",
                      ArtworkKind::Logo, m_game.hasLogoArt);
    addArtworkActions("game.steamgriddb.poster", "Poster",
                      "game.steamgriddb.poster_desc",
                      "Tall cover for single-row and 2x3 grid tiles.",
                      ArtworkKind::Poster, m_game.hasPosterArt);
    // 2x1 / 4x2 tiles can use a landscape SGDB grid instead of Hero+Logo.
    if (m_game.sizeIndex == 1 || m_game.sizeIndex == 2) {
        addArtworkActions("game.steamgriddb.poster_wide", "Poster wide",
                          "game.steamgriddb.poster_wide_desc",
                          "Landscape grid art for this 2x1 / 4x2 tile.",
                          ArtworkKind::PosterWide, m_game.hasPosterWideArt);
    }
    addArtworkActions("game.steamgriddb.icon", "Replacement icon",
                      "game.steamgriddb.icon_desc",
                      "Choose an icon.",
                      ArtworkKind::Icon, m_game.hasIconArt);
    {
        SettingItem section;
        section.label = i18n.tr("game.section_hero_position", "Hero position");
        section.type = ItemType::Section;
        artwork.items.push_back(std::move(section));
    }

    const auto addHeroOffset = [this, &artwork, &i18n](bool vertical) {
        SettingItem row;
        row.label = vertical
            ? i18n.tr("game.hero_offset_y", "Move hero up / down")
            : i18n.tr("game.hero_offset_x", "Move hero left / right");
        row.description = vertical
            ? i18n.tr("game.hero_offset_y_desc",
                "Slides this game's hero up or down. 0% keeps its usual position.")
            : i18n.tr("game.hero_offset_x_desc",
                "Slides this game's hero left or right. 0% keeps it centred.");
        row.type = ItemType::Slider;
        row.sliderSteps = 40;
        row.valueBase = -100;
        row.valueSpan = 200;
        row.valueSigned = true;
        row.valueSuffix = "%";
        row.hasReset = true;
        row.resetValue = 0.5f;
        const int value = std::clamp(vertical ? m_game.heroOffsetY : m_game.heroOffsetX,
                                     -100, 100);
        row.floatVal = static_cast<float>(value + 100) / 200.f;
        row.anim01 = row.floatVal;
        row.onChange = [this, vertical](SettingItem& self) {
            const int next = -100 + static_cast<int>(std::lround(
                std::clamp(self.floatVal, 0.f, 1.f) * 200.f));
            if (vertical)
                m_game.heroOffsetY = next;
            else
                m_game.heroOffsetX = next;
            if (m_heroOffsetCb)
                m_heroOffsetCb(m_game.heroOffsetX, m_game.heroOffsetY);
        };
        artwork.items.push_back(std::move(row));
    };
    addHeroOffset(false);
    addHeroOffset(true);
    m_tabs.push_back(std::move(artwork));

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());
    invalidateHeaderCache();
}

void GameOptionsScreen::drawOverlayHeader(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || !m_font || !m_smallFont)
        return;

    const float pad = 30.f;
    const float iconSize = 76.f;
    nxui::Rect iconRect{panel.x + pad, panel.y + 28.f, iconSize, iconSize};
    if (m_game.icon && m_game.icon->valid()) {
        ren.drawTextureRounded(m_game.icon, iconRect, 16.f, nxui::Color::white().withAlpha(opacity));
    } else {
        ren.drawRoundedRect(iconRect, m_theme->panelBorder.withAlpha(0.38f * opacity), 16.f);
    }

    const float textX = iconRect.right() + 22.f;
    const float textW = std::max(0.f, panel.right() - pad - textX);

    // The settings shell repeats the focused row's description in the header;
    // keep that behavior here, with the game's own metadata above it.
    const SettingItem* focused = focusedSettingItem();
    const std::string* desc = (focused && !focused->description.empty())
        ? &focused->description : nullptr;
    if (!desc && m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
        const auto& items = m_tabs[(size_t)m_tabIndex].items;
        if (m_headerFallbackTab != m_tabIndex
            || m_headerFallbackCount != (int)items.size()) {
            m_headerFallbackTab = m_tabIndex;
            m_headerFallbackCount = (int)items.size();
            m_headerFallbackDescription.clear();
            for (const auto& item : items) {
                if (!item.description.empty()) {
                    m_headerFallbackDescription = item.description;
                    break;
                }
            }
        }
        if (!m_headerFallbackDescription.empty())
            desc = &m_headerFallbackDescription;
    }

    // Ellipsizing a long description measures every shorter prefix until it
    // fits. Doing that for five strings on every frame is expensive on the
    // console, so rebuild the header only when its inputs actually change.
    const bool descriptionChanged = desc
        ? (m_headerDescriptionSource != *desc)
        : !m_headerDescriptionSource.empty();

    // Only the description moves while the user walks the rows. Rebuilding the
    // whole header there re-ellipsized the game name, version, publisher and
    // eyebrow on every D-pad press; keep them until their own inputs move.
    const bool identityChanged = !m_headerCacheValid
        || m_headerTextWidth != textW
        || m_headerFont != m_font || m_headerSmallFont != m_smallFont
        || m_headerNameSource != m_game.name
        || m_headerVersionSource != m_game.version
        || m_headerPublisherSource != m_game.publisher;
    if (identityChanged) {
        auto& i18n = nxui::I18n::instance();
        m_headerEyebrow = ellipsize(m_smallFont,
            i18n.tr("game.options", "Software Options"), textW, 0.62f);
        m_headerName = ellipsize(m_font, m_game.name, textW, 1.f);
        m_headerVersion = ellipsize(m_smallFont,
            i18n.tr("game.version", "Version") + " " + m_game.version,
            textW, 0.72f);
        m_headerPublisher = ellipsize(m_smallFont, m_game.publisher, textW, 0.72f);
        m_headerNameSource = m_game.name;
        m_headerVersionSource = m_game.version;
        m_headerPublisherSource = m_game.publisher;
        m_headerTextWidth = textW;
        m_headerFont = m_font;
        m_headerSmallFont = m_smallFont;
        m_headerCacheValid = true;
    }
    if (identityChanged || descriptionChanged) {
        m_headerDescription = desc
            ? ellipsize(m_smallFont, *desc, textW, 0.60f)
            : std::string();
        m_headerDescriptionSource = desc ? *desc : std::string();
    }

    ren.drawText(m_headerEyebrow,
                 {textX, panel.y + 18.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(0.72f * opacity), 0.62f);
    ren.drawText(m_headerName,
                 {textX, panel.y + 34.f}, m_font,
                 m_theme->textPrimary.withAlpha(opacity), 1.f);
    ren.drawText(m_headerVersion,
                 {textX, panel.y + 70.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(opacity), 0.72f);
    ren.drawText(m_headerPublisher,
                 {textX, panel.y + 89.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(0.92f * opacity), 0.72f);
    if (!m_headerDescription.empty()) {
        ren.drawText(m_headerDescription,
                     {textX, panel.y + 108.f}, m_smallFont,
                     m_theme->textSecondary.withAlpha(0.88f * opacity), 0.60f);
    }

    float dividerY = panel.y + pad + overlayHeaderHeight() - 14.f;
    ren.drawRoundedRect({panel.x + pad, dividerY, panel.width - pad * 2.f, 1.5f},
                        m_theme->panelBorder.withAlpha(0.25f * opacity), 0.75f);
}

void GameOptionsScreen::drawOverlayFooter(nxui::Renderer& ren, const nxui::Rect& panel,
                                           float opacity) {
    if (!m_theme || !m_smallFont)
        return;

    auto& i18n = nxui::I18n::instance();
    const float pad = 30.f;
    const float top = panel.bottom() - overlayFooterHeight();
    ren.drawRoundedRect({panel.x + pad, top, panel.width - pad * 2.f, 1.f},
                        m_theme->panelBorder.withAlpha(0.20f * opacity), 0.5f);

    int hintsKey = 0;
    if (m_dropdownOpen) {
        hintsKey = 1;
    } else if (m_focusArea == FocusArea::Content) {
        const SettingItem* item = focusedSettingItem();
        if (item) {
            // Rename rows need their own hint: the shared secondary string says
            // "Clear", which is wrong for the Folders section.
            const bool namedSecondary = item->hasSecondaryAction()
                && item->secondaryButtonLabel != i18n.tr("button.clear_x", "×");
            hintsKey = 10 + (int)item->type * 4
                + (item->hasSecondaryAction() ? 1 : 0)
                + (item->hasReset ? 2 : 0)
                + (namedSecondary ? 4 : 0);
        }
    }
    if (m_footerHintsKey != hintsKey || m_footerHintsText.empty()) {
        m_footerHintsKey = hintsKey;
        switch (hintsKey) {
            case 0:
                m_footerHintsText = i18n.tr("settings.footer_tabs",
                    "A  Open     D-pad  Navigate     B  Close");
                break;
            case 1:
                m_footerHintsText = i18n.tr("settings.footer_list",
                    "A  Choose     B  Close list");
                break;
            default: {
                const SettingItem* item = focusedSettingItem();
                if (!item) {
                    m_footerHintsText = i18n.tr("settings.footer_tabs",
                        "A  Open     D-pad  Navigate     B  Close");
                } else if (item->type == ItemType::Toggle) {
                    m_footerHintsText = i18n.tr("settings.footer_toggle",
                        "A  Toggle     B  Tabs");
                } else if (item->type == ItemType::Slider) {
                    m_footerHintsText = item->hasReset
                        ? i18n.tr("settings.footer_slider_reset",
                            "Left/Right  Adjust     X  Reset     B  Tabs")
                        : i18n.tr("settings.footer_slider",
                            "Left/Right  Adjust     B  Tabs");
                } else if (item->type == ItemType::Selector) {
                    m_footerHintsText = i18n.tr("settings.footer_selector",
                        "A  Choose     Left/Right  Change     B  Tabs");
                } else if (item->type == ItemType::Action) {
                    if (item->hasSecondaryAction()) {
                        const bool named = item->secondaryButtonLabel
                            != i18n.tr("button.clear_x", "×");
                        m_footerHintsText = named
                            ? i18n.tr("settings.footer_action_rename",
                                "A  Select     X  Rename     B  Tabs")
                            : i18n.tr("settings.footer_action_secondary",
                                "A  Select     X  Clear     B  Tabs");
                    } else {
                        m_footerHintsText = i18n.tr("settings.footer_action",
                            "A  Activate     B  Tabs");
                    }
                } else {
                    m_footerHintsText = i18n.tr("settings.footer_info", "B  Tabs");
                }
                break;
            }
        }
    }
    ren.drawText(m_footerHintsText, {panel.x + pad, top + 9.f}, m_smallFont,
                 m_theme->textSecondary.withAlpha(0.78f * opacity), 0.62f);
}

void GameOptionsScreen::setShortcutPinned(bool pinned) {
    if (m_shortcutPinned == pinned)
        return;
    m_shortcutPinned = pinned;
    // During open/close animation no input can reach the action row, so a
    // rebuild here only delays the next screen.
    if (isFullyVisible())
        rebuildCurrentTab();
}
