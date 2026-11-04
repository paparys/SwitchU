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
    tab.name = i18n.tr("settings.tabs.appearance", "Icons");

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

    SettingItem aspect;
    aspect.type = ItemType::Selector;
    aspect.label = i18n.tr("settings.appearance.aspect", "Icon aspect ratio");
    aspect.description = i18n.tr("settings.appearance.aspect_desc",
        "Shapes tiles in both the grid and the single-row carousel.");
    aspect.options = {"1:1", "16:9", "4:3"};
    aspect.intVal = std::clamp(screen.m_iconAspect, 0, 2);
    aspect.onChange = [&screen](SettingItem& self) {
        screen.m_iconAspect = std::clamp(self.intVal, 0, 2);
        if (screen.m_iconAspectCb)
            screen.m_iconAspectCb(screen.m_iconAspect);
    };
    tab.items.push_back(std::move(aspect));

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
        "Scales every HOME tile in the grid and the carousel. 100% is the stock size.");
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

    SettingItem depth;
    depth.type = ItemType::Slider;
    depth.label = i18n.tr("settings.appearance.carousel_depth", "Carousel icon depth");
    depth.description = i18n.tr("settings.appearance.carousel_depth_desc",
        "Back makes the tiles smaller so more of them are visible; forward makes "
        "them larger and fewer fit.");
    depth.sliderSteps = 40;
    depth.valueBase = -100;
    depth.valueSpan = 200;
    depth.valueSigned = true;
    depth.hasReset = true;
    depth.resetValue = 0.5f;
    depth.floatVal = (std::clamp(screen.m_carouselDepth, -100, 100) + 100) / 200.f;
    depth.anim01 = depth.floatVal;
    depth.onChange = [&screen](SettingItem& self) {
        const int pct = -100 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 200.f));
        screen.m_carouselDepth = pct;
        if (screen.m_carouselDepthCb)
            screen.m_carouselDepthCb(pct);
    };
    tab.items.push_back(std::move(depth));

    SettingItem fade;
    fade.type = ItemType::Slider;
    fade.label = i18n.tr("settings.appearance.carousel_fade", "Carousel fade-out");
    fade.description = i18n.tr("settings.appearance.carousel_fade_desc",
        "How quickly the tiles beside the focused one fade away. Low keeps the "
        "whole row lit, high leaves just a glow at the edges.");
    fade.sliderSteps = 100;
    fade.valueSpan = 100;
    fade.hasReset = true;
    fade.resetValue = 0.5f;
    fade.floatVal = std::clamp(screen.m_carouselFade, 0, 100) / 100.f;
    fade.anim01 = fade.floatVal;
    fade.onChange = [&screen](SettingItem& self) {
        const int pct = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        screen.m_carouselFade = pct;
        if (screen.m_carouselFadeCb)
            screen.m_carouselFadeCb(pct);
    };
    tab.items.push_back(std::move(fade));

    SettingItem gridNudge;
    gridNudge.type = ItemType::Slider;
    gridNudge.label = i18n.tr("settings.appearance.grid_nudge", "Move grid left / right");
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

    SettingItem lineNudge;
    lineNudge.type = ItemType::Slider;
    lineNudge.label = i18n.tr("settings.appearance.line_nudge", "Move carousel up / down");
    lineNudge.description = i18n.tr("settings.appearance.line_nudge_desc",
        "Raises or lowers the single-row icon strip.");
    lineNudge.sliderSteps = 50;
    lineNudge.valueBase = -25;
    lineNudge.valueSpan = 50;
    lineNudge.valueSigned = true;
    lineNudge.valueSuffix = " px";
    lineNudge.hasReset = true;
    lineNudge.resetValue = 0.5f;
    lineNudge.floatVal = sliderFromNudge(screen.m_lineNudgeY);
    lineNudge.anim01 = lineNudge.floatVal;
    lineNudge.onChange = [&screen](SettingItem& self) {
        screen.m_lineNudgeY = nudgeFromSlider(self.floatVal);
        if (screen.m_lineNudgeCb)
            screen.m_lineNudgeCb(screen.m_lineNudgeY);
    };
    tab.items.push_back(std::move(lineNudge));

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

    return tab;
}
