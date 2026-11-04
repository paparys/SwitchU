
#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>

namespace {

int nudgeFromSlider(float value01) {
    return static_cast<int>(std::lround(std::clamp(value01, 0.f, 1.f) * 50.f)) - 25;
}

float sliderFromNudge(int nudge) {
    return (std::clamp(nudge, -25, 25) + 25) / 50.f;
}

} // namespace

SettingsScreen::Tab settings::tabs::ShortcutsTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.shortcuts", "Shortcuts");

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.shortcuts.section", "Applet shortcuts");
    tab.items.push_back(std::move(section));

    SettingItem size;
    size.type = ItemType::Slider;
    size.label = i18n.tr("settings.shortcuts.size", "Shortcut size");
    size.description = i18n.tr("settings.shortcuts.size_desc",
        "Scales the six shortcuts together with the profile tile, clock and "
        "battery, so the whole top bar grows or shrinks as one system. "
        "Independent of the game tile size.");
    size.sliderSteps = 80;
    size.valueBase = 70;
    size.valueSpan = 80;
    size.hasReset = true;
    size.resetValue = 0.375f;
    size.floatVal = (std::clamp(screen.m_sidebarScale, 70, 150) - 70) / 80.f;
    size.anim01 = size.floatVal;
    size.onChange = [&screen](SettingItem& self) {
        const int pct = 70 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 80.f));
        screen.m_sidebarScale = pct;
        if (screen.m_sidebarScaleCb)
            screen.m_sidebarScaleCb(pct);
    };
    tab.items.push_back(std::move(size));

    SettingItem edge;
    edge.type = ItemType::Selector;
    edge.label = i18n.tr("settings.shortcuts.edge", "Shortcut position");
    edge.description = i18n.tr("settings.shortcuts.edge_desc",
        "Top puts the shortcuts above the games, bottom puts them underneath. "
        "The games shift clear so nothing overlaps.");
    edge.options = {
        i18n.tr("settings.shortcuts.edge_top", "Top"),
        i18n.tr("settings.shortcuts.edge_bottom", "Bottom"),
    };
    edge.intVal = std::clamp(screen.m_shortcutEdge, 0, 1);
    edge.onChange = [&screen](SettingItem& self) {
        screen.m_shortcutEdge = std::clamp(self.intVal, 0, 1);
        if (screen.m_shortcutEdgeCb)
            screen.m_shortcutEdgeCb(screen.m_shortcutEdge);
    };
    tab.items.push_back(std::move(edge));

    SettingItem strip;
    strip.type = ItemType::Toggle;
    strip.label = i18n.tr("settings.shortcuts.strip", "Single strip background");
    strip.description = i18n.tr("settings.shortcuts.strip_desc",
        "Draws one continuous plate behind all six shortcuts instead of a "
        "pill around each one. Only the Top / Bottom strip layout uses it.");
    strip.boolVal = screen.m_shortcutStrip;
    strip.anim01 = strip.boolVal ? 1.f : 0.f;
    strip.onChange = [&screen](SettingItem& self) {
        screen.m_shortcutStrip = self.boolVal;
        if (screen.m_shortcutStripCb)
            screen.m_shortcutStripCb(screen.m_shortcutStrip);
    };
    tab.items.push_back(std::move(strip));

    SettingItem vertical;
    vertical.type = ItemType::Slider;
    vertical.label = i18n.tr("settings.shortcuts.nudge_y",
                             "Move shortcuts up / down");
    vertical.description = i18n.tr("settings.shortcuts.nudge_y_desc",
        "Slides the shortcut row up or down in 2-pixel steps. It works at "
        "either edge and stops before the row leaves the screen.");
    vertical.sliderSteps = 24;
    vertical.valueBase = -24;
    vertical.valueSpan = 48;
    vertical.valueSigned = true;
    vertical.valueSuffix = " px";
    vertical.hasReset = true;
    vertical.resetValue = 0.5f;
    const int nudgeSteps = std::clamp(
        (std::clamp(screen.m_shortcutNudgeY, -24, 24) + 24) / 2, 0, 24);
    vertical.floatVal = static_cast<float>(nudgeSteps) / 24.f;
    vertical.anim01 = vertical.floatVal;
    vertical.onChange = [&screen](SettingItem& self) {
        const int step = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 24.f));
        screen.m_shortcutNudgeY = -24 + step * 2;
        if (screen.m_shortcutNudgeYCb)
            screen.m_shortcutNudgeYCb(screen.m_shortcutNudgeY);
    };
    tab.items.push_back(std::move(vertical));

    SettingItem pinnedSection;
    pinnedSection.type = ItemType::Section;
    pinnedSection.label = i18n.tr("settings.shortcuts.pinned_section", "Pinned games");
    tab.items.push_back(std::move(pinnedSection));

    SettingItem pinSide;
    pinSide.type = ItemType::Selector;
    pinSide.label = i18n.tr("settings.shortcuts.pin_side", "Pinned shortcut side");
    pinSide.description = i18n.tr("settings.shortcuts.pin_side_desc",
        "Keeps the pinned game icons next to the applet shortcuts on the left or "
        "right.");
    pinSide.options = {
        i18n.tr("settings.shortcuts.side_left", "Left"),
        i18n.tr("settings.shortcuts.side_right", "Right"),
    };
    pinSide.intVal = std::clamp(screen.m_pinSide, 0, 1);
    pinSide.onChange = [&screen](SettingItem& self) {
        screen.m_pinSide = std::clamp(self.intVal, 0, 1);
        if (screen.m_pinSideCb)
            screen.m_pinSideCb(screen.m_pinSide);
    };
    tab.items.push_back(std::move(pinSide));

    return tab;
}
