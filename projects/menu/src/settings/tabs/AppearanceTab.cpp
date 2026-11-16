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

SettingsScreen::Tab settings::tabs::AppearanceTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.appearance", "Icons & Wallpaper");

    SettingItem librarySection;
    librarySection.type = ItemType::Section;
    librarySection.label = i18n.tr("settings.appearance.section_library", "Library");
    tab.items.push_back(std::move(librarySection));

    SettingItem order;
    order.type = ItemType::Selector;
    order.label = i18n.tr("settings.appearance.game_order", "Game order");
    order.description = i18n.tr("settings.appearance.game_order_desc",
        "My order keeps the hand-made layout. A-Z sorts by name, Recent by the "
        "last time you opened each game.");
    order.options = {
        i18n.tr("settings.appearance.order_custom", "My order"),
        i18n.tr("settings.appearance.order_alpha", "A-Z"),
        i18n.tr("settings.appearance.order_recent", "Recent"),
    };
    order.intVal = std::clamp(screen.m_sortMode, 0, 2);
    order.onChange = [&screen](SettingItem& self) {
        screen.m_sortMode = std::clamp(self.intVal, 0, 2);
        if (screen.m_sortModeCb)
            screen.m_sortModeCb(screen.m_sortMode);
    };
    tab.items.push_back(std::move(order));

    SettingItem iconSection;
    iconSection.type = ItemType::Section;
    iconSection.label = i18n.tr("settings.appearance.icons_section", "Home icons");
    tab.items.push_back(std::move(iconSection));

    SettingItem homeLayout;
    homeLayout.type = ItemType::Selector;
    homeLayout.label = i18n.tr("settings.appearance.home_layout", "Home layout");
    homeLayout.description = i18n.tr("settings.appearance.home_layout_desc",
        "Carousel is the single row. Shelf is a 5x2 grid, Showcase a 3x2 grid of "
        "large square tiles, and Poster Wall a 6x2 grid of portrait posters.");
    homeLayout.options = {
        i18n.tr("settings.appearance.home_layout_carousel", "Carousel"),
        i18n.tr("settings.appearance.home_layout_shelf", "Shelf (5x2)"),
        i18n.tr("settings.appearance.home_layout_showcase", "Showcase (3x2)"),
        i18n.tr("settings.appearance.home_layout_poster", "Poster Wall (6x2)"),
    };
    homeLayout.intVal = std::clamp(static_cast<int>(screen.m_homeLayout), 0, 3);
    homeLayout.onChange = [&screen](SettingItem& self) {
        const auto preset = static_cast<HomeLayoutPreset>(
            std::clamp(self.intVal, 0, 3));
        if (screen.m_homeLayout == preset)
            return;
        screen.m_homeLayout = preset;
        if (screen.m_homeLayoutCb)
            screen.m_homeLayoutCb(preset);
    };
    tab.items.push_back(std::move(homeLayout));

    SettingItem corners;
    corners.type = ItemType::Selector;
    corners.label = i18n.tr("settings.appearance.corners", "Icon corners");
    corners.description = i18n.tr("settings.appearance.corners_desc",
        "Rounded keeps the Wii U glass look; Square draws perfectly square tiles.");
    corners.options = {
        i18n.tr("settings.appearance.corners_rounded", "Rounded"),
        i18n.tr("settings.appearance.corners_square", "Square"),
    };
    corners.intVal = std::clamp(screen.m_iconCorners, 0, 1);
    corners.onChange = [&screen](SettingItem& self) {
        screen.m_iconCorners = std::clamp(self.intVal, 0, 1);
        if (screen.m_iconCornersCb)
            screen.m_iconCornersCb(screen.m_iconCorners);
    };
    tab.items.push_back(std::move(corners));

    SettingItem cornerPct;
    cornerPct.type = ItemType::Slider;
    cornerPct.label = i18n.tr("settings.appearance.corner_radius", "Corner roundness");
    cornerPct.description = i18n.tr("settings.appearance.corner_radius_desc",
        "How round the tile corners are. 100% is the theme's own radius, 0% is square. "
        "Does nothing while Icon corners is set to Square.");
    cornerPct.sliderSteps = 100;
    cornerPct.valueSpan = 100;
    cornerPct.hasReset = true;
    cornerPct.resetValue = 1.f;
    cornerPct.floatVal = std::clamp(screen.m_iconCornerPct, 0, 100) / 100.f;
    cornerPct.anim01 = cornerPct.floatVal;
    cornerPct.onChange = [&screen](SettingItem& self) {
        const int pct = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        screen.m_iconCornerPct = pct;
        if (screen.m_iconCornerPctCb)
            screen.m_iconCornerPctCb(pct);
    };
    tab.items.push_back(std::move(cornerPct));

    SettingItem size;
    size.type = ItemType::Slider;
    size.label = i18n.tr("settings.appearance.icon_scale", "Icon size");
    size.description = i18n.tr("settings.appearance.icon_scale_desc",
        "Scales every HOME tile in the carousel. 100% is the stock size.");
    size.sliderSteps = 60;
    size.valueBase = 70;
    size.valueSpan = 60;
    size.hasReset = true;
    size.resetValue = 0.5f;
    size.floatVal = (std::clamp(screen.m_iconScale, 70, 130) - 70) / 60.f;
    size.anim01 = size.floatVal;
    size.onChange = [&screen](SettingItem& self) {
        const int pct = 70 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 60.f));
        screen.m_iconScale = pct;
        if (screen.m_iconScaleCb)
            screen.m_iconScaleCb(pct);
    };
    tab.items.push_back(std::move(size));

    SettingItem vignette;
    vignette.type = ItemType::Slider;
    vignette.label = i18n.tr("settings.appearance.icon_vignette", "Icon vignette");
    vignette.description = i18n.tr("settings.appearance.icon_vignette_desc",
        "Darkens the rim of every tile so the artwork sits inside the glass. "
        "0% is off.");
    vignette.sliderSteps = 100;
    vignette.valueSpan = 100;
    vignette.hasReset = true;
    vignette.resetValue = 0.f;
    vignette.floatVal = std::clamp(screen.m_iconVignette, 0, 100) / 100.f;
    vignette.anim01 = vignette.floatVal;
    vignette.onChange = [&screen](SettingItem& self) {
        screen.m_iconVignette = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        if (screen.m_iconOverlayCb)
            screen.m_iconOverlayCb();
    };
    tab.items.push_back(std::move(vignette));

    SettingItem highlight;
    highlight.type = ItemType::Slider;
    highlight.label = i18n.tr("settings.appearance.icon_highlight", "Icon highlight");
    highlight.description = i18n.tr("settings.appearance.icon_highlight_desc",
        "Adds a glass sheen across the top of each tile, plus a thin rim light. "
        "0% is off.");
    highlight.sliderSteps = 100;
    highlight.valueSpan = 100;
    highlight.hasReset = true;
    highlight.resetValue = 0.f;
    highlight.floatVal = std::clamp(screen.m_iconHighlight, 0, 100) / 100.f;
    highlight.anim01 = highlight.floatVal;
    highlight.onChange = [&screen](SettingItem& self) {
        screen.m_iconHighlight = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        if (screen.m_iconOverlayCb)
            screen.m_iconOverlayCb();
    };
    tab.items.push_back(std::move(highlight));

    SettingItem spacing;
    spacing.type = ItemType::Slider;
    spacing.label = i18n.tr("settings.appearance.icon_spacing", "Icon spacing");
    spacing.description = i18n.tr("settings.appearance.icon_spacing_desc",
        "Gap between tiles. 0% is the stock spacing; negative tightens it.");
    spacing.sliderSteps = 60;
    spacing.valueBase = -50;
    spacing.valueSpan = 150;
    spacing.valueSigned = true;
    spacing.hasReset = true;
    spacing.resetValue = 1.f / 3.f;
    spacing.floatVal = (std::clamp(screen.m_iconSpacing, -50, 100) + 50) / 150.f;
    spacing.anim01 = spacing.floatVal;
    spacing.onChange = [&screen](SettingItem& self) {
        const int pct = -50 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 150.f));
        screen.m_iconSpacing = pct;
        if (screen.m_iconSpacingCb)
            screen.m_iconSpacingCb(pct);
    };
    tab.items.push_back(std::move(spacing));

    SettingItem gridNudge;
    gridNudge.type = ItemType::Slider;
    gridNudge.label = i18n.tr("settings.appearance.grid_nudge", "Move icons left / right");
    gridNudge.description = i18n.tr("settings.appearance.grid_nudge_desc",
        "Slides every HOME tile sideways. 0 keeps the stock position.");
    gridNudge.sliderSteps = 50;
    gridNudge.valueBase = -25;
    gridNudge.valueSpan = 50;
    gridNudge.valueSigned = true;
    gridNudge.valueSuffix = " px";
    gridNudge.hasReset = true;
    gridNudge.resetValue = 0.5f;
    gridNudge.floatVal = sliderFromNudge(screen.m_gridNudgeX);
    gridNudge.anim01 = gridNudge.floatVal;
    gridNudge.onChange = [&screen](SettingItem& self) {
        screen.m_gridNudgeX = nudgeFromSlider(self.floatVal);
        if (screen.m_gridNudgeCb)
            screen.m_gridNudgeCb(screen.m_gridNudgeX);
    };
    tab.items.push_back(std::move(gridNudge));

    SettingItem lineNudgeX;
    lineNudgeX.type = ItemType::Slider;
    lineNudgeX.label = i18n.tr("settings.appearance.line_nudge_x",
                               "Move focused icon left / right");
    lineNudgeX.description = i18n.tr("settings.appearance.line_nudge_x_desc",
        "Slides the carousel sideways so the focused tile can sit off-centre.");
    lineNudgeX.sliderSteps = 50;
    lineNudgeX.valueBase = -25;
    lineNudgeX.valueSpan = 50;
    lineNudgeX.valueSigned = true;
    lineNudgeX.valueSuffix = " px";
    lineNudgeX.hasReset = true;
    lineNudgeX.resetValue = 0.5f;
    lineNudgeX.floatVal = sliderFromNudge(screen.m_lineNudgeX);
    lineNudgeX.anim01 = lineNudgeX.floatVal;
    lineNudgeX.onChange = [&screen](SettingItem& self) {
        screen.m_lineNudgeX = nudgeFromSlider(self.floatVal);
        if (screen.m_lineNudgeXCb)
            screen.m_lineNudgeXCb(screen.m_lineNudgeX);
    };
    tab.items.push_back(std::move(lineNudgeX));

    // Presets cover every value in this tab, so they sit at the bottom instead
    // of taking a separate rail entry that showed only nine rows.
    SettingsScreen::Tab presets = LayoutTab::build(screen);
    for (auto& item : presets.items)
        tab.items.push_back(std::move(item));

    // The All Apps grid uses the same tiles, so its two layout sliders belong
    // with the HOME icon controls instead of a rail entry of their own.
    SettingsScreen::Tab allApps = AllAppsTab::build(screen);
    for (auto& item : allApps.items)
        tab.items.push_back(std::move(item));

    return tab;
}
