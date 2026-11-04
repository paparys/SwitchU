#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>
#include <ui/AccentPalette.hpp>

#include <algorithm>
#include <vector>

// Liquid glass styling: colour and solid/opaque mode. Both are applied by the
// renderer, so they affect every glass surface (top bar, panels, dialogs,
// capsules) rather than just the screen that owns the setting.
SettingsScreen::Tab settings::tabs::GlassTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.glass", "Liquid Glass");

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.glass.section", "Liquid glass");
    tab.items.push_back(std::move(section));

    // Names and chips come from the shared palette, so the label a user reads
    // and the colour the app applies are the same entry.
    std::vector<std::string> colourNames;
    std::vector<nxui::Color> colourSwatches;
    colourNames.push_back(i18n.tr("settings.glass.colour_theme", "Theme"));
    colourSwatches.push_back(nxui::Color(0.62f, 0.65f, 0.72f, 1.f));
    for (const auto& entry : switchu::ui::accent::palette()) {
        colourNames.push_back(i18n.tr(entry.key, entry.fallback));
        colourSwatches.push_back(entry.color);
    }

    SettingItem tint;
    tint.type = ItemType::Selector;
    tint.label = i18n.tr("settings.glass.tint", "Glass color");
    tint.description = i18n.tr("settings.glass.tint_desc",
        "Tints every glass surface: the top bar, panels, dialogs and capsules. "
        "Theme keeps whatever colour the active theme ships with.");
    tint.options = colourNames;
    tint.swatches = colourSwatches;
    tint.intVal = screen.m_glassTint;
    tint.onChange = [&screen](SettingItem& self) {
        screen.m_glassTint = std::clamp(self.intVal, 0, 9);
        if (screen.m_glassTintCb)
            screen.m_glassTintCb(screen.m_glassTint);
    };
    tab.items.push_back(std::move(tint));

    SettingItem solid;
    solid.type = ItemType::Toggle;
    solid.label = i18n.tr("settings.glass.solid", "Solid glass");
    solid.description = i18n.tr("settings.glass.solid_desc",
        "Draws glass as a fully opaque plate instead of a see-through lens. "
        "The plate takes the glass color above, so the menu stays flat and "
        "readable over busy wallpapers and costs less to draw.");
    solid.boolVal = screen.m_glassSolid;
    solid.anim01 = solid.boolVal ? 1.f : 0.f;
    solid.onChange = [&screen](SettingItem& self) {
        screen.m_glassSolid = self.boolVal;
        if (screen.m_glassSolidCb)
            screen.m_glassSolidCb(screen.m_glassSolid);
    };
    tab.items.push_back(std::move(solid));

    SettingItem highlight;
    highlight.type = ItemType::Selector;
    highlight.label = i18n.tr("settings.glass.accent", "Accent color");
    highlight.description = i18n.tr("settings.glass.accent_desc",
        "One accent for the whole UI: the ring around the focused icon, the "
        "caret and text selection, spinners, progress bars and every focused "
        "border in dialogs and these settings tabs.");
    highlight.options = colourNames;
    highlight.swatches = colourSwatches;
    highlight.intVal = screen.m_highlightTint;
    highlight.onChange = [&screen](SettingItem& self) {
        screen.m_highlightTint = std::clamp(self.intVal, 0, 9);
        if (screen.m_highlightTintCb)
            screen.m_highlightTintCb(screen.m_highlightTint);
    };
    tab.items.push_back(std::move(highlight));

    return tab;
}
