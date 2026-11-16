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

SettingsScreen::Tab settings::tabs::TreatmentTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.treatment", "Screen treatment");

    SettingItem orderSection;
    orderSection.type = ItemType::Section;
    orderSection.label = i18n.tr("settings.treatment.order_section", "Game order");
    tab.items.push_back(std::move(orderSection));

    SettingItem alpha;
    alpha.type = ItemType::Toggle;
    alpha.label = i18n.tr("settings.treatment.alpha_order", "Sort all games A-Z");
    alpha.description = i18n.tr("settings.treatment.alpha_order_desc",
        "Rearranges every game on the HOME screen by name: the "
        "single-row carousel and the inside of folders. Folders and widgets "
        "keep their place. Turning it off goes back to My order.");
    alpha.boolVal = screen.alphaOrder();
    alpha.anim01 = alpha.boolVal ? 1.f : 0.f;
    alpha.onChange = [&screen](SettingItem& self) {
        screen.setAlphaOrder(self.boolVal);
        screen.notifyAlphaOrder(self.boolVal);
    };
    tab.items.push_back(std::move(alpha));

    SettingItem radiusPct;
    radiusPct.type = ItemType::Slider;
    radiusPct.label = i18n.tr("settings.appearance.ui_radius", "Menu roundness");
    radiusPct.description = i18n.tr("settings.appearance.ui_radius_desc",
        "Scales every rounded corner together: tiles, focus ring, panels, clock cells "
        "and folders. 100% keeps the theme's own values.");
    radiusPct.sliderSteps = 100;
    radiusPct.valueBase = 50;
    radiusPct.valueSpan = 150;
    radiusPct.hasReset = true;
    radiusPct.resetValue = 1.f / 3.f;
    radiusPct.floatVal = (std::clamp(screen.m_uiRadiusPct, 50, 200) - 50) / 150.f;
    radiusPct.anim01 = radiusPct.floatVal;
    radiusPct.onChange = [&screen](SettingItem& self) {
        const int pct = 50 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 150.f));
        screen.m_uiRadiusPct = pct;
        if (screen.m_uiRadiusPctCb)
            screen.m_uiRadiusPctCb(pct);
    };
    tab.items.push_back(std::move(radiusPct));

    SettingItem dim;
    dim.type = ItemType::Slider;
    dim.label = i18n.tr("settings.appearance.dim", "Screen dim");
    dim.description = i18n.tr("settings.appearance.dim_desc",
        "Lays a flat veil over the whole menu so bright wallpapers stop fighting "
        "the tiles. 0% is off.");
    dim.sliderSteps = 100;
    dim.valueSpan = 100;
    dim.hasReset = true;
    dim.resetValue = 0.f;
    dim.floatVal = std::clamp(screen.m_wallpaperDim, 0, 100) / 100.f;
    dim.anim01 = dim.floatVal;
    dim.onChange = [&screen](SettingItem& self) {
        const int pct = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        screen.m_wallpaperDim = pct;
        if (screen.m_wallpaperDimCb)
            screen.m_wallpaperDimCb(pct);
    };
    tab.items.push_back(std::move(dim));

    SettingItem vignette;
    vignette.type = ItemType::Slider;
    vignette.label = i18n.tr("settings.appearance.vignette", "Corner vignette");
    vignette.description = i18n.tr("settings.appearance.vignette_desc",
        "Darkens the edges of the screen, keeping the eye on the middle of the screen.");
    vignette.sliderSteps = 100;
    vignette.valueSpan = 100;
    vignette.hasReset = true;
    vignette.resetValue = 0.f;
    vignette.floatVal = std::clamp(screen.m_vignette, 0, 100) / 100.f;
    vignette.anim01 = vignette.floatVal;
    vignette.onChange = [&screen](SettingItem& self) {
        const int pct = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        screen.m_vignette = pct;
        if (screen.m_vignetteCb)
            screen.m_vignetteCb(pct);
    };
    tab.items.push_back(std::move(vignette));

    SettingItem shadowOff;
    shadowOff.type = ItemType::Slider;
    shadowOff.label = i18n.tr("settings.appearance.shadow_offset", "Tile shadow depth");
    shadowOff.description = i18n.tr("settings.appearance.shadow_offset_desc",
        "How far the drop shadow sits below each tile. 0 removes it.");
    shadowOff.sliderSteps = 20;
    shadowOff.valueSpan = 20;
    shadowOff.valueSuffix = " px";
    shadowOff.hasReset = true;
    shadowOff.resetValue = 0.3f;
    shadowOff.floatVal = std::clamp(screen.m_iconShadowOffset, 0, 20) / 20.f;
    shadowOff.anim01 = shadowOff.floatVal;
    shadowOff.onChange = [&screen](SettingItem& self) {
        const int px = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 20.f));
        screen.m_iconShadowOffset = px;
        if (screen.m_iconShadowCb)
            screen.m_iconShadowCb();
    };
    tab.items.push_back(std::move(shadowOff));

    SettingItem shadowAlpha;
    shadowAlpha.type = ItemType::Slider;
    shadowAlpha.label = i18n.tr("settings.appearance.shadow_alpha", "Tile shadow strength");
    shadowAlpha.description = i18n.tr("settings.appearance.shadow_alpha_desc",
        "Darker shadows make tiles pop off busy wallpapers.");
    shadowAlpha.sliderSteps = 60;
    shadowAlpha.valueSpan = 60;
    shadowAlpha.hasReset = true;
    shadowAlpha.resetValue = 20.f / 60.f;
    shadowAlpha.floatVal = std::clamp(screen.m_iconShadowAlpha, 0, 60) / 60.f;
    shadowAlpha.anim01 = shadowAlpha.floatVal;
    shadowAlpha.onChange = [&screen](SettingItem& self) {
        const int pct = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 60.f));
        screen.m_iconShadowAlpha = pct;
        if (screen.m_iconShadowCb)
            screen.m_iconShadowCb();
    };
    tab.items.push_back(std::move(shadowAlpha));

    SettingItem hudSection;
    hudSection.type = ItemType::Section;
    hudSection.label = i18n.tr("settings.treatment.hud_section", "Top bar and hints");
    tab.items.push_back(std::move(hudSection));

    SettingItem hints;
    hints.type = ItemType::Toggle;
    hints.label = i18n.tr("settings.treatment.show_hints", "Show action hints");
    hints.description = i18n.tr("settings.treatment.show_hints_desc",
        "The button capsules in the bottom-right corner. Turning them off "
        "hides the reminders; every action still works.");
    hints.boolVal = !screen.m_hideActionHints;
    hints.anim01 = hints.boolVal ? 1.f : 0.f;
    hints.onChange = [&screen](SettingItem& self) {
        screen.m_hideActionHints = !self.boolVal;
        if (screen.m_hideActionHintsCb)
            screen.m_hideActionHintsCb(screen.m_hideActionHints);
    };
    tab.items.push_back(std::move(hints));

    SettingItem clockPos;
    clockPos.type = ItemType::Slider;
    clockPos.label = i18n.tr("settings.treatment.clock_nudge",
                             "Move clock left / right");
    clockPos.description = i18n.tr("settings.treatment.clock_nudge_desc",
        "Slides the date and time capsule sideways between the profile tile "
        "and the battery. 0 keeps it centred.");
    clockPos.sliderSteps = 50;
    clockPos.valueBase = -25;
    clockPos.valueSpan = 50;
    clockPos.valueSigned = true;
    clockPos.valueSuffix = " px";
    clockPos.hasReset = true;
    clockPos.resetValue = 0.5f;
    clockPos.floatVal = sliderFromNudge(screen.m_clockNudgeX);
    clockPos.anim01 = clockPos.floatVal;
    clockPos.onChange = [&screen](SettingItem& self) {
        screen.m_clockNudgeX = nudgeFromSlider(self.floatVal);
        if (screen.m_clockNudgeXCb)
            screen.m_clockNudgeXCb(screen.m_clockNudgeX);
    };
    tab.items.push_back(std::move(clockPos));

    SettingItem clockVert;
    clockVert.type = ItemType::Slider;
    clockVert.label = i18n.tr("settings.treatment.clock_nudge_y",
                              "Move clock up / down");
    clockVert.description = i18n.tr("settings.treatment.clock_nudge_y_desc",
        "Slides the clock up or down in 2-pixel steps. Moving it off the bar "
        "row gives it its own capsule again.");
    clockVert.sliderSteps = 14;
    const int clockVertStep = std::clamp(
        (std::clamp(screen.m_clockNudgeY, -14, 14) + 14) / 2, 0, 14);
    clockVert.floatVal = static_cast<float>(clockVertStep) / 14.f;
    clockVert.valueBase = -14;
    clockVert.valueSpan = 28;
    clockVert.valueSigned = true;
    clockVert.valueSuffix = " px";
    clockVert.hasReset = true;
    clockVert.resetValue = 0.5f;
    clockVert.anim01 = clockVert.floatVal;
    clockVert.onChange = [&screen](SettingItem& self) {
        const int step = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 14.f));
        screen.m_clockNudgeY = -14 + step * 2;
        if (screen.m_clockNudgeYCb)
            screen.m_clockNudgeYCb(screen.m_clockNudgeY);
    };
    tab.items.push_back(std::move(clockVert));

    SettingItem nameSection;
    nameSection.type = ItemType::Section;
    nameSection.label = i18n.tr("settings.treatment.name_section", "Game name");
    tab.items.push_back(std::move(nameSection));

    SettingItem showName;
    showName.type = ItemType::Toggle;
    showName.label = i18n.tr("settings.treatment.show_game_name", "Show game name");
    showName.description = i18n.tr("settings.treatment.show_game_name_desc",
        "The capsule under the carousel. Turn it off for a clean shelf with "
        "no titles at all.");
    showName.boolVal = screen.m_showGameTitle;
    showName.anim01 = showName.boolVal ? 1.f : 0.f;
    showName.onChange = [&screen](SettingItem& self) {
        screen.m_showGameTitle = self.boolVal;
        if (screen.m_gameTitleVisibilityCb)
            screen.m_gameTitleVisibilityCb(screen.m_showGameTitle);
    };
    tab.items.push_back(std::move(showName));

    SettingItem titlePos;
    titlePos.type = ItemType::Slider;
    titlePos.label = i18n.tr("settings.appearance.title_nudge",
                             "Move game name left / right");
    titlePos.description = i18n.tr("settings.appearance.title_nudge_desc",
        "Slides the title capsule under the carousel sideways. 0 keeps it "
        "centred; the ends park it near the screen edge.");
    titlePos.sliderSteps = 50;
    titlePos.valueBase = -25;
    titlePos.valueSpan = 50;
    titlePos.valueSigned = true;
    titlePos.valueSuffix = " px";
    titlePos.hasReset = true;
    titlePos.resetValue = 0.5f;
    titlePos.floatVal = sliderFromNudge(screen.m_titlePillNudgeX);
    titlePos.anim01 = titlePos.floatVal;
    titlePos.onChange = [&screen](SettingItem& self) {
        screen.m_titlePillNudgeX = nudgeFromSlider(self.floatVal);
        if (screen.m_titlePillNudgeCb)
            screen.m_titlePillNudgeCb(screen.m_titlePillNudgeX);
    };
    tab.items.push_back(std::move(titlePos));

    SettingItem titlePosY;
    titlePosY.type = ItemType::Slider;
    titlePosY.label = i18n.tr("settings.appearance.title_nudge_y",
                              "Move game name up / down");
    titlePosY.description = i18n.tr("settings.appearance.title_nudge_y_desc",
        "Raises or lowers the title capsule under the carousel, so it can "
        "clear the shortcuts or sit closer to the row.");
    titlePosY.sliderSteps = 50;
    titlePosY.valueBase = -25;
    titlePosY.valueSpan = 50;
    titlePosY.valueSigned = true;
    titlePosY.valueSuffix = " px";
    titlePosY.hasReset = true;
    titlePosY.resetValue = 0.5f;
    titlePosY.floatVal = sliderFromNudge(screen.m_titlePillNudgeY);
    titlePosY.anim01 = titlePosY.floatVal;
    titlePosY.onChange = [&screen](SettingItem& self) {
        screen.m_titlePillNudgeY = nudgeFromSlider(self.floatVal);
        if (screen.m_titlePillNudgeYCb)
            screen.m_titlePillNudgeYCb(screen.m_titlePillNudgeY);
    };
    tab.items.push_back(std::move(titlePosY));

    // The opacity layer is part of the same visual treatment, so keep it in
    // this list rather than making users hunt through the rail for a four-row
    // tab.
    SettingsScreen::Tab opacity = OpacityTab::build(screen);
    for (auto& item : opacity.items)
        tab.items.push_back(std::move(item));

    return tab;
}
