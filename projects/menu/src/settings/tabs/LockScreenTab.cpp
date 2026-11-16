#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>

namespace {

char stepChar(int index) {
    static const char kSteps[4] = {'U', 'D', 'L', 'R'};
    return kSteps[std::clamp(index, 0, 3)];
}

int stepIndex(char c) {
    switch (c) {
        case 'U': return 0;
        case 'D': return 1;
        case 'L': return 2;
        case 'R': return 3;
        default: return 0;
    }
}

} // namespace

SettingsScreen::Tab settings::tabs::LockScreenTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.lockscreen", "Lock Screen");

    const auto& profiles = screen.lockProfiles();
    if (profiles.empty()) {
        SettingItem empty;
        empty.type = ItemType::Info;
        empty.label = i18n.tr("settings.lockscreen.no_users", "No users found");
        empty.infoText = i18n.tr("settings.lockscreen.no_users_text",
            "SwitchU could not read the console's users. A lock code cannot be "
            "assigned until a user is available.");
        tab.items.push_back(std::move(empty));
        return tab;
    }

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.lockscreen.section", "User lock code");
    tab.items.push_back(std::move(section));

    SettingItem user;
    user.type = ItemType::Selector;
    user.label = i18n.tr("settings.lockscreen.user", "User");
    user.description = i18n.tr("settings.lockscreen.user_desc",
        "Each console user can have their own four-step D-pad code.");
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        user.options.push_back(profiles[i].nickname.empty()
            ? i18n.tr("settings.lockscreen.user_fallback", "User")
                + " " + std::to_string(i + 1)
            : profiles[i].nickname);
    }
    user.intVal = screen.lockPadProfileIndex();
    user.onChange = [&screen](SettingItem& self) {
        if (self.intVal == screen.lockPadProfileIndex())
            return;
        screen.setLockPadProfileIndex(self.intVal);
        screen.requestLockPadProfileRefresh();
    };
    tab.items.push_back(std::move(user));

    SettingItem toggle;
    toggle.type = ItemType::Toggle;
    toggle.label = i18n.tr("settings.lockscreen.code", "Require a D-pad code");
    toggle.description = i18n.tr("settings.lockscreen.code_desc",
        "Replaces swipe and any-button unlock with four D-pad steps for this "
        "user. While it is on, only the exact sequence below opens HOME, so "
        "write it down before you enable it.");
    toggle.boolVal = screen.lockPadCodeEnabled();
    toggle.anim01 = toggle.boolVal ? 1.f : 0.f;
    toggle.onChange = [&screen](SettingItem& self) {
        screen.setLockPadCodeEnabledState(self.boolVal);
        screen.notifyLockPadCodeChange();
    };
    tab.items.push_back(std::move(toggle));

    for (int i = 0; i < 4; ++i) {
        SettingItem step;
        step.type = ItemType::Selector;
        step.label = i18n.tr("settings.lockscreen.step", "Code step")
                     + " " + std::to_string(i + 1);
        step.description = i18n.tr("settings.lockscreen.step_desc",
            "Which direction this step of the code uses. All four steps are "
            "pressed in this order on the lock screen.");
        step.options = {
            i18n.tr("settings.lockscreen.up", "Up"),
            i18n.tr("settings.lockscreen.down", "Down"),
            i18n.tr("settings.lockscreen.left", "Left"),
            i18n.tr("settings.lockscreen.right", "Right"),
        };
        step.intVal = stepIndex(screen.lockPadCode()[static_cast<std::size_t>(i)]);
        step.onChange = [&screen, i](SettingItem& self) {
            screen.setLockPadCodeStep(i, stepChar(self.intVal));
            screen.notifyLockPadCodeChange();
        };
        tab.items.push_back(std::move(step));
    }

    SettingItem hint;
    hint.type = ItemType::Info;
    hint.label = i18n.tr("settings.lockscreen.hint", "Unlocking");
    hint.infoText = i18n.tr("settings.lockscreen.hint_text",
        "At startup, choose the user whose code opens this session, then press "
        "their four directions in order. Sleep, wake and manual Lock skip the "
        "account screen; a wrong sequence clears the entry, and B resets it.");
    tab.items.push_back(std::move(hint));

    return tab;
}
