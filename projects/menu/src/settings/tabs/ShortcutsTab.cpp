
#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <ui/AccentPalette.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

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

    std::vector<std::string> gradientNames;
    std::vector<nxui::Color> gradientSwatches;
    for (const auto& entry : switchu::ui::accent::palette()) {
        gradientNames.push_back(i18n.tr(entry.key, entry.fallback));
        gradientSwatches.push_back(entry.color);
    }

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

    SettingItem gradient;
    gradient.type = ItemType::Toggle;
    gradient.label = i18n.tr("settings.shortcuts.gradient", "Moving gradient");
    gradient.description = i18n.tr("settings.shortcuts.gradient_desc",
        "Animates a two-color gradient across the single strip background. "
        "Turn it off for a flat plate.");
    gradient.boolVal = screen.m_shortcutGradient;
    gradient.anim01 = gradient.boolVal ? 1.f : 0.f;
    gradient.onChange = [&screen](SettingItem& self) {
        screen.m_shortcutGradient = self.boolVal;
        if (screen.m_shortcutGradientCb)
            screen.m_shortcutGradientCb(screen.m_shortcutGradient);
    };
    tab.items.push_back(std::move(gradient));

    SettingItem gradientA;
    gradientA.type = ItemType::Selector;
    gradientA.label = i18n.tr("settings.shortcuts.gradient_a", "Gradient color 1");
    gradientA.description = i18n.tr("settings.shortcuts.gradient_a_desc",
        "First color of the moving gradient on the shortcut strip.");
    gradientA.options = gradientNames;
    gradientA.swatches = gradientSwatches;
    gradientA.intVal = std::clamp(screen.m_shortcutGradientA, 1, 9) - 1;
    gradientA.onChange = [&screen](SettingItem& self) {
        const int value = std::clamp(self.intVal, 0, 8) + 1;
        screen.m_shortcutGradientA = value;
        if (screen.m_shortcutGradientACb)
            screen.m_shortcutGradientACb(value);
    };
    tab.items.push_back(std::move(gradientA));

    SettingItem gradientB;
    gradientB.type = ItemType::Selector;
    gradientB.label = i18n.tr("settings.shortcuts.gradient_b", "Gradient color 2");
    gradientB.description = i18n.tr("settings.shortcuts.gradient_b_desc",
        "Second color of the moving gradient. Pick a different color for the "
        "full sweep.");
    gradientB.options = gradientNames;
    gradientB.swatches = gradientSwatches;
    gradientB.intVal = std::clamp(screen.m_shortcutGradientB, 1, 9) - 1;
    gradientB.onChange = [&screen](SettingItem& self) {
        const int value = std::clamp(self.intVal, 0, 8) + 1;
        screen.m_shortcutGradientB = value;
        if (screen.m_shortcutGradientBCb)
            screen.m_shortcutGradientBCb(value);
    };
    tab.items.push_back(std::move(gradientB));

    SettingItem flushEdge;
    flushEdge.type = ItemType::Toggle;
    flushEdge.label = i18n.tr("settings.shortcuts.flush_edge",
                              "Connect to top/bottom edge");
    flushEdge.description = i18n.tr("settings.shortcuts.flush_edge_desc",
        "Stretches the bar to the screen edge it sits on and squares off that "
        "edge, while the opposite edge keeps its rounded corners.");
    flushEdge.boolVal = screen.m_shortcutFlushTop;
    flushEdge.anim01 = flushEdge.boolVal ? 1.f : 0.f;
    flushEdge.onChange = [&screen](SettingItem& self) {
        screen.m_shortcutFlushTop = self.boolVal;
        if (screen.m_shortcutFlushTopCb)
            screen.m_shortcutFlushTopCb(screen.m_shortcutFlushTop);
    };
    tab.items.push_back(std::move(flushEdge));

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

    return tab;
}
