#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <string>

SettingsScreen::Tab settings::tabs::LayoutTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.layout", "Layout presets");

    for (int slot = 0; slot < 3; ++slot) {
        SettingItem row;
        row.type = ItemType::Action;
        row.label = "Save preset " + std::to_string(slot + 1);
        row.buttonLabel = i18n.tr("button.save", "Save");
        row.indentLevel = 1;
        row.onChange = [&screen, slot](SettingItem&) {
            if (screen.m_saveLayoutPresetCb)
                screen.m_saveLayoutPresetCb(slot);
        };
        tab.items.push_back(std::move(row));
    }

    for (int slot = 0; slot < 3; ++slot) {
        SettingItem row;
        row.type = ItemType::Action;
        row.label = "Load preset " + std::to_string(slot + 1);
        row.buttonLabel = i18n.tr("button.load", "Load");
        row.indentLevel = 1;
        row.onChange = [&screen, slot](SettingItem&) {
            if (screen.m_loadLayoutPresetCb)
                screen.m_loadLayoutPresetCb(slot);
        };
        tab.items.push_back(std::move(row));
    }

    SettingItem resetLayout;
    resetLayout.type = ItemType::Action;
    resetLayout.label = i18n.tr("settings.appearance.reset_layout", "Reset icon layout");
    resetLayout.description = i18n.tr("settings.appearance.reset_layout_desc",
        "Puts size, spacing, depth, corners and every nudge back to their stock values.");
    resetLayout.buttonLabel = i18n.tr("button.reset", "Reset");
    resetLayout.onChange = [&screen](SettingItem&) {
        if (screen.m_resetIconLayoutCb)
            screen.m_resetIconLayoutCb();
    };
    tab.items.push_back(std::move(resetLayout));

    return tab;
}
