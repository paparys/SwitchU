#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>

namespace {

int sliderValue(const SettingsScreen::SettingItem& self, int base, int span) {
    return base + static_cast<int>(
        std::lround(std::clamp(self.floatVal, 0.f, 1.f) * static_cast<float>(span)));
}

} // namespace

SettingsScreen::Tab settings::tabs::AllAppsTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.allapps", "All Apps");

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.allapps.section", "All Apps grid");
    tab.items.push_back(std::move(section));

    SettingItem columns;
    columns.type = ItemType::Slider;
    columns.label = i18n.tr("settings.allapps.columns", "Columns");
    columns.description = i18n.tr("settings.allapps.columns_desc",
        "How many app tiles the All Apps grid shows across the screen. "
        "The tiles resize to fill the width.");
    columns.sliderSteps = 5;
    columns.valueBase = 3;
    columns.valueSpan = 5;
    // A count of tiles, not a percentage.
    columns.valueSuffix = "";
    columns.hasReset = true;
    columns.resetValue = 0.6f;
    columns.floatVal = static_cast<float>(
        std::clamp(screen.m_allAppsColumns, 3, 8) - 3) / 5.f;
    columns.anim01 = columns.floatVal;
    columns.onChange = [&screen](SettingItem& self) {
        const int value = sliderValue(self, 3, 5);
        screen.m_allAppsColumns = value;
        if (screen.m_allAppsColumnsCb)
            screen.m_allAppsColumnsCb(value);
    };
    tab.items.push_back(std::move(columns));

    SettingItem rows;
    rows.type = ItemType::Slider;
    rows.label = i18n.tr("settings.allapps.rows", "Rows");
    rows.description = i18n.tr("settings.allapps.rows_desc",
        "How many app tiles the All Apps grid shows vertically. "
        "Rows and columns together decide how many apps fit on one page. "
        "6 x 3 gives the largest comfortable tiles.");
    rows.sliderSteps = 3;
    rows.valueBase = 2;
    rows.valueSpan = 3;
    rows.valueSuffix = "";
    rows.hasReset = true;
    rows.resetValue = 1.f / 3.f;
    rows.floatVal = static_cast<float>(
        std::clamp(screen.m_allAppsRows, 2, 5) - 2) / 3.f;
    rows.anim01 = rows.floatVal;
    rows.onChange = [&screen](SettingItem& self) {
        const int value = sliderValue(self, 2, 3);
        screen.m_allAppsRows = value;
        if (screen.m_allAppsRowsCb)
            screen.m_allAppsRowsCb(value);
    };
    tab.items.push_back(std::move(rows));

    SettingItem movement;
    movement.type = ItemType::Info;
    movement.label = i18n.tr("settings.allapps.movement", "Movement");
    movement.description = i18n.tr("settings.allapps.movement_desc",
        "The All Apps grid follows the motion preset, scroll speed, icon "
        "shrink and fade-out chosen in Carousel settings.");
    movement.infoText = i18n.tr("settings.allapps.movement_info",
        "Change these under Settings > Carousel settings; the grid picks them "
        "up the next time it opens.");
    tab.items.push_back(std::move(movement));

    return tab;
}