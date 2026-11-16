#include "TabBuilders.hpp"

#include <fmt/format.h>
#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>

namespace {

std::string outputModeText(const SettingsScreen::DockedOutputState& state,
                           const nxui::I18n& i18n) {
    if (state.docked)
        return i18n.tr("settings.docked.output_docked", "docked");
    return i18n.tr("settings.docked.output_handheld", "handheld");
}

std::string outputResolutionText(const SettingsScreen::DockedOutputState& state,
                                 const nxui::I18n& i18n) {
    return fmt::format("{} x {} ({})", state.width, state.height,
                       outputModeText(state, i18n));
}

std::string textScaleText(const SettingsScreen::DockedOutputState& state) {
    const int percent = static_cast<int>(std::lround(state.textScale * 100.f));
    return fmt::format("{}% ({:.1f}x)", percent, state.textScale);
}

} // namespace

SettingsScreen::Tab settings::tabs::DockedTab::build(SettingsScreen& screen) {
    SettingsScreen::Tab tab;
    tab.name = nxui::I18n::instance().tr("settings.tabs.docked", "Docked mode");
    appendTo(screen, tab);
    return tab;
}

void settings::tabs::DockedTab::appendTo(SettingsScreen& screen,
                                         SettingsScreen::Tab& tab) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    const std::size_t first = tab.items.size();

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.docked.section", "While docked");
    tab.items.push_back(std::move(section));

    SettingItem toggle;
    toggle.type = ItemType::Toggle;
    toggle.label = i18n.tr("settings.docked.enable", "Use docked layout");
    toggle.description = i18n.tr("settings.docked.enable_desc",
        "Docked HOME uses the layout below. Both docked and handheld render at "
        "720p; undocking restores the normal icon, shortcut and carousel position.");
    toggle.boolVal = screen.dockedSizeOverrides();
    toggle.anim01 = toggle.boolVal ? 1.f : 0.f;
    toggle.onChange = [&screen](SettingItem& self) {
        screen.setDockedSizeOverrides(self.boolVal);
        screen.notifyDockedOverrides(self.boolVal);
    };
    tab.items.push_back(std::move(toggle));

    // Tab builders can run on a worker. Do not touch GPU state from here;
    // onUpdate below fills these read-only rows on the UI thread.
    SettingItem resolution;
    resolution.type = ItemType::Info;
    resolution.label = i18n.tr("settings.docked.output", "Active output");
    resolution.infoText = "--";
    tab.items.push_back(std::move(resolution));

    SettingItem textScale;
    textScale.type = ItemType::Info;
    textScale.label = i18n.tr("settings.docked.text_scale", "Text scale");
    textScale.infoText = "--";
    tab.items.push_back(std::move(textScale));

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

    SettingItem carouselSection;
    carouselSection.type = ItemType::Section;
    carouselSection.label = i18n.tr("settings.docked.carousel_section",
                                    "Carousel position");
    tab.items.push_back(std::move(carouselSection));

    SettingItem carouselX;
    carouselX.type = ItemType::Slider;
    carouselX.label = i18n.tr("settings.docked.carousel_x",
                              "Move focused icon left / right");
    carouselX.description = i18n.tr("settings.docked.carousel_x_desc",
        "Slides the docked carousel sideways so the focused tile sits left or "
        "right of centre. 0 keeps the stock position.");
    carouselX.sliderSteps = 50;
    carouselX.valueBase = -25;
    carouselX.valueSpan = 50;
    carouselX.valueSigned = true;
    carouselX.valueSuffix = " px";
    carouselX.hasReset = true;
    carouselX.resetValue = 0.5f;
    carouselX.floatVal = (screen.dockedCarouselX() + 25) / 50.f;
    carouselX.anim01 = carouselX.floatVal;
    carouselX.onChange = [&screen](SettingItem& self) {
        if (!screen.dockedSizeOverrides()) {
            screen.setDockedSizeOverrides(true);
            screen.notifyDockedOverrides(true);
        }
        const int nudge = -25 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 50.f));
        screen.setDockedCarouselX(nudge);
        screen.notifyDockedCarouselX(nudge);
    };
    tab.items.push_back(std::move(carouselX));

    SettingItem carouselY;
    carouselY.type = ItemType::Slider;
    carouselY.label = i18n.tr("settings.docked.carousel_y",
                              "Move carousel up / down");
    carouselY.description = i18n.tr("settings.docked.carousel_y_desc",
        "Raises or lowers the docked carousel row. 0 keeps the stock height.");
    carouselY.sliderSteps = 50;
    carouselY.valueBase = -25;
    carouselY.valueSpan = 50;
    carouselY.valueSigned = true;
    carouselY.valueSuffix = " px";
    carouselY.hasReset = true;
    carouselY.resetValue = 0.5f;
    carouselY.floatVal = (screen.dockedCarouselY() + 25) / 50.f;
    carouselY.anim01 = carouselY.floatVal;
    carouselY.onChange = [&screen](SettingItem& self) {
        if (!screen.dockedSizeOverrides()) {
            screen.setDockedSizeOverrides(true);
            screen.notifyDockedOverrides(true);
        }
        const int nudge = -25 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 50.f));
        screen.setDockedCarouselY(nudge);
        screen.notifyDockedCarouselY(nudge);
    };
    tab.items.push_back(std::move(carouselY));

    SettingItem hint;
    hint.type = ItemType::Info;
    hint.label = i18n.tr("settings.docked.hint", "Handheld layout stays put");
    hint.infoText = i18n.tr("settings.docked.hint_text",
        "The Icons, Shortcuts and Carousel tabs still control the undocked "
        "layout. Only the rows above change while the console sits in the dock.");
    tab.items.push_back(std::move(hint));

    // Moving a slider turns the master switch on above, so the toggle row has to
    // follow instead of still reading "off" while the new size is live.
    tab.onUpdate = [first](SettingsScreen::Tab& current, TabbedOverlayScreen& base) {
        if (current.items.size() < first + 10)
            return;
        auto& screen = static_cast<SettingsScreen&>(base);
        const bool on = screen.dockedSizeOverrides();
        if (current.items[first + 1].boolVal != on) {
            current.items[first + 1].boolVal = on;
            current.items[first + 1].anim01 = on ? 1.f : 0.f;
        }
        const auto output = screen.dockedOutputState();
        const auto& i18n = nxui::I18n::instance();
        const std::string resolution = outputResolutionText(output, i18n);
        if (current.items[first + 2].infoText != resolution)
            current.items[first + 2].infoText = resolution;
        const std::string scale = textScaleText(output);
        if (current.items[first + 3].infoText != scale)
            current.items[first + 3].infoText = scale;
    };
}
