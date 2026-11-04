#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>

SettingsScreen::Tab settings::tabs::DockedTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.docked", "Docked mode");

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.docked.section", "While docked");
    tab.items.push_back(std::move(section));

    SettingItem toggle;
    toggle.type = ItemType::Toggle;
    toggle.label = i18n.tr("settings.docked.enable", "Use docked sizes");
    toggle.description = i18n.tr("settings.docked.enable_desc",
        "When the console is docked, HOME switches to the sizes below. "
        "Undocking restores the normal icon and shortcut sizes.");
    toggle.boolVal = screen.dockedSizeOverrides();
    toggle.anim01 = toggle.boolVal ? 1.f : 0.f;
    toggle.onChange = [&screen](SettingItem& self) {
        screen.setDockedSizeOverrides(self.boolVal);
        screen.notifyDockedOverrides(self.boolVal);
    };
    tab.items.push_back(std::move(toggle));

    SettingItem icons;
    icons.type = ItemType::Slider;
    icons.label = i18n.tr("settings.docked.icon_size", "Docked icon size");
    icons.description = i18n.tr("settings.docked.icon_size_desc",
        "Scales every HOME tile and the carousel while docked. 100% is stock.");
    icons.sliderSteps = 60;
    icons.valueBase = 70;
    icons.valueSpan = 60;
    icons.hasReset = true;
    icons.resetValue = 0.5f;
    icons.floatVal = (screen.dockedIconScale() - 70) / 60.f;
    icons.anim01 = icons.floatVal;
    icons.onChange = [&screen](SettingItem& self) {
        // Editing a docked size implies wanting it used. With the master switch
        // off both sliders moved and did nothing, which read as a broken tab.
        if (!screen.dockedSizeOverrides()) {
            screen.setDockedSizeOverrides(true);
            screen.notifyDockedOverrides(true);
        }
        const int pct = 70 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 60.f));
        screen.setDockedIconScale(pct);
        screen.notifyDockedIcon(pct);
    };
    tab.items.push_back(std::move(icons));

    SettingItem shortcuts;
    shortcuts.type = ItemType::Slider;
    shortcuts.label = i18n.tr("settings.docked.shortcut_size", "Docked shortcut size");
    shortcuts.description = i18n.tr("settings.docked.shortcut_size_desc",
        "Scales the shortcut row together with the profile, clock and battery "
        "while docked.");
    shortcuts.sliderSteps = 80;
    shortcuts.valueBase = 70;
    shortcuts.valueSpan = 80;
    shortcuts.hasReset = true;
    shortcuts.resetValue = 0.375f;
    shortcuts.floatVal =
        (screen.dockedShortcutScale() - 70) / 80.f;
    shortcuts.anim01 = shortcuts.floatVal;
    shortcuts.onChange = [&screen](SettingItem& self) {
        if (!screen.dockedSizeOverrides()) {
            screen.setDockedSizeOverrides(true);
            screen.notifyDockedOverrides(true);
        }
        const int pct = 70 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 80.f));
        screen.setDockedShortcutScale(pct);
        screen.notifyDockedShortcut(pct);
    };
    tab.items.push_back(std::move(shortcuts));

    SettingItem hint;
    hint.type = ItemType::Info;
    hint.label = i18n.tr("settings.docked.hint", "Handheld sizes stay put");
    hint.infoText = i18n.tr("settings.docked.hint_text",
        "The Icons and Shortcuts tabs still control the undocked sizes. Only "
        "the two rows above change while the console sits in the dock.");
    tab.items.push_back(std::move(hint));

    // Moving a slider turns the master switch on above, so the toggle row has to
    // follow instead of still reading "off" while the new size is live.
    tab.onUpdate = [](SettingsScreen::Tab& current, TabbedOverlayScreen& base) {
        if (current.items.size() < 5)
            return;
        const bool on = static_cast<SettingsScreen&>(base).dockedSizeOverrides();
        if (current.items[1].boolVal != on) {
            current.items[1].boolVal = on;
            current.items[1].anim01 = on ? 1.f : 0.f;
        }
    };

    return tab;
}
