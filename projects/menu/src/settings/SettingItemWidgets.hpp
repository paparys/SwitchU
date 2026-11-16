#pragma once

#include "SettingsScreen.hpp"
#include <nxui/widgets/Box.hpp>
#include <memory>

namespace settings::widgets {

struct SettingWidgetContext {
    nxui::Font** font = nullptr;
    nxui::Font** smallFont = nullptr;
    const nxui::Theme** theme = nullptr;
    // Rows can drop their second line when the screen shows the focused row's
    // description in its header instead.
    bool showDescriptions = true;
};

std::shared_ptr<nxui::Box> createSettingItemWidget(SettingsScreen::SettingItem& item,
                                                    const SettingWidgetContext& ctx);

}
