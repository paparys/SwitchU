#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

SettingsScreen::Tab settings::tabs::HiddenGamesTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.hidden", "Hidden Games");

    const auto& games = screen.hiddenGames();
    if (games.empty()) {
        SettingItem none;
        none.type = ItemType::Info;
        none.label = i18n.tr("settings.hidden.none", "No hidden games");
        none.infoText = i18n.tr("settings.hidden.none_text",
            "Titles hidden from HOME with Game Options > Hide from HOME appear "
            "here, ready to be added back.");
        tab.items.push_back(std::move(none));
        return tab;
    }

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.hidden.section", "Hidden from HOME");
    tab.items.push_back(std::move(section));

    for (const auto& game : games) {
        SettingItem row;
        row.type = ItemType::Action;
        row.label = game.second;
        row.buttonLabel = i18n.tr("button.add_back", "Add back");
        row.description = i18n.tr("settings.hidden.restore_desc",
            "Puts this title back on the HOME menu.");
        const std::uint64_t id = game.first;
        row.onChange = [&screen, id](SettingItem&) {
            screen.notifyRestoreHidden(id);
        };
        tab.items.push_back(std::move(row));
    }

    return tab;
}
