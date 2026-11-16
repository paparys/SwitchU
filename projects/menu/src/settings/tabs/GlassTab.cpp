#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>
#include <ui/AccentPalette.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// Opacity layer styling: colour and solid mode. Both are applied by the
// renderer to the flat layer every panel draws, so they affect the whole UI
// (top bar, panels, dialogs, capsules) rather than just the screen that owns
// the setting.
SettingsScreen::Tab settings::tabs::OpacityTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.opacity", "Opacity");

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.opacity.section", "Opacity");
    tab.items.push_back(std::move(section));

    // Names and chips come from the shared palette, so the label a user reads
    // and the colour the app applies are the same entry.
    std::vector<std::string> colourNames;
    std::vector<nxui::Color> colourSwatches;
    colourNames.push_back(i18n.tr("settings.opacity.colour_theme", "Theme"));
    colourSwatches.push_back(nxui::Color(0.62f, 0.65f, 0.72f, 1.f));
    for (const auto& entry : switchu::ui::accent::palette()) {
        colourNames.push_back(i18n.tr(entry.key, entry.fallback));
        colourSwatches.push_back(entry.color);
    }
    colourNames.push_back(i18n.tr("settings.opacity.colour_gradient", "Gradient"));
    colourSwatches.push_back(nxui::Color(0.52f, 0.44f, 0.96f, 1.f));

    std::vector<std::string> gradientNames;
    std::vector<nxui::Color> gradientSwatches;
    for (const auto& entry : switchu::ui::accent::palette()) {
        gradientNames.push_back(i18n.tr(entry.key, entry.fallback));
        gradientSwatches.push_back(entry.color);
    }

    SettingItem tint;
    tint.type = ItemType::Selector;
    tint.label = i18n.tr("settings.opacity.tint", "Opacity color");
    tint.description = i18n.tr("settings.opacity.tint_desc",
        "Colours the opacity layer behind the top bar, panels, dialogs and "
        "capsules. Theme keeps whatever colour the active theme ships with.");
    tint.options = colourNames;
    tint.swatches = colourSwatches;
    tint.intVal = screen.m_opacityTint;
    tint.onChange = [&screen](SettingItem& self) {
        screen.m_opacityTint = std::clamp(self.intVal, 0, 9);
        if (screen.m_opacityTintCb)
            screen.m_opacityTintCb(screen.m_opacityTint);
    };
    tab.items.push_back(std::move(tint));

    SettingItem solid;
    solid.type = ItemType::Toggle;
    solid.label = i18n.tr("settings.opacity.solid", "Make opacity solid");
    solid.description = i18n.tr("settings.opacity.solid_desc",
        "Draws the opacity layer fully opaque instead of see-through. It "
        "takes the opacity color above, so the menu stays flat and readable "
        "over busy wallpapers and costs less to draw.");
    solid.boolVal = screen.m_opacitySolid;
    solid.anim01 = solid.boolVal ? 1.f : 0.f;
    solid.onChange = [&screen](SettingItem& self) {
        screen.m_opacitySolid = self.boolVal;
        if (screen.m_opacitySolidCb)
            screen.m_opacitySolidCb(screen.m_opacitySolid);
    };
    tab.items.push_back(std::move(solid));

    SettingItem highlight;
    highlight.type = ItemType::Selector;
    highlight.label = i18n.tr("settings.opacity.accent", "Accent color");
    highlight.description = i18n.tr("settings.opacity.accent_desc",
        "One accent for the whole UI: the ring around the focused icon, the "
        "caret and text selection, spinners, progress bars and every focused "
        "border in dialogs and these settings tabs. Gradient uses two colors "
        "on the animated focus ring; the rest of the UI uses Gradient color 1.");
    highlight.options = colourNames;
    highlight.swatches = colourSwatches;
    highlight.intVal = screen.m_highlightTint;
    highlight.onChange = [&screen](SettingItem& self) {
        screen.m_highlightTint = std::clamp(self.intVal, 0, 10);
        if (screen.m_highlightTintCb)
            screen.m_highlightTintCb(screen.m_highlightTint);
    };
    tab.items.push_back(std::move(highlight));

    SettingItem gradientA;
    gradientA.type = ItemType::Selector;
    gradientA.label = i18n.tr("settings.opacity.gradient_a", "Gradient color 1");
    gradientA.description = i18n.tr("settings.opacity.gradient_a_desc",
        "First color in the animated highlight. Used when Accent color is Gradient.");
    gradientA.options = gradientNames;
    gradientA.swatches = gradientSwatches;
    gradientA.intVal = std::clamp(screen.m_highlightGradientA, 1, 9) - 1;
    gradientA.onChange = [&screen](SettingItem& self) {
        const int value = std::clamp(self.intVal, 0, 8) + 1;
        screen.m_highlightGradientA = value;
        if (screen.m_highlightGradientACb)
            screen.m_highlightGradientACb(value);
    };
    tab.items.push_back(std::move(gradientA));

    SettingItem gradientB;
    gradientB.type = ItemType::Selector;
    gradientB.label = i18n.tr("settings.opacity.gradient_b", "Gradient color 2");
    gradientB.description = i18n.tr("settings.opacity.gradient_b_desc",
        "Second color in the animated highlight. Pick a different color for the full effect.");
    gradientB.options = gradientNames;
    gradientB.swatches = gradientSwatches;
    gradientB.intVal = std::clamp(screen.m_highlightGradientB, 1, 9) - 1;
    gradientB.onChange = [&screen](SettingItem& self) {
        const int value = std::clamp(self.intVal, 0, 8) + 1;
        screen.m_highlightGradientB = value;
        if (screen.m_highlightGradientBCb)
            screen.m_highlightGradientBCb(value);
    };
    tab.items.push_back(std::move(gradientB));

    SettingItem thickness;
    thickness.type = ItemType::Slider;
    thickness.label = i18n.tr("settings.opacity.highlight_thickness", "Highlight thickness");
    thickness.description = i18n.tr("settings.opacity.highlight_thickness_desc",
        "Thickness of the focus ring around the highlighted icon, for solid and "
        "gradient accents.");
    thickness.sliderSteps = 7;
    thickness.valueBase = 1;
    thickness.valueSpan = 7;
    thickness.valueSuffix = " px";
    thickness.hasReset = true;
    thickness.resetValue = 2.f / 7.f;
    thickness.floatVal = (std::clamp(screen.m_highlightThicknessPx, 1, 8) - 1) / 7.f;
    thickness.anim01 = thickness.floatVal;
    thickness.onChange = [&screen](SettingItem& self) {
        const int px = 1 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 7.f));
        screen.m_highlightThicknessPx = std::clamp(px, 1, 8);
        if (screen.m_highlightThicknessCb)
            screen.m_highlightThicknessCb(screen.m_highlightThicknessPx);
    };
    tab.items.push_back(std::move(thickness));

    return tab;
}
