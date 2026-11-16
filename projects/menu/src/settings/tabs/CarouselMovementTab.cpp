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

SettingsScreen::Tab settings::tabs::CarouselMovementTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.carousel", "Carousel settings");

    SettingItem scrollSection;
    scrollSection.type = ItemType::Section;
    scrollSection.label = i18n.tr("settings.carousel.scroll_section", "Scrolling");
    tab.items.push_back(std::move(scrollSection));

    SettingItem speed;
    speed.type = ItemType::Slider;
    speed.label = i18n.tr("settings.carousel.scroll_speed", "Scroll speed");
    speed.description = i18n.tr("settings.carousel.scroll_speed_desc",
        "How quickly the single-row carousel glides between tiles. 50% is the "
        "stock speed; lower values are slower and higher values are faster.");
    speed.sliderSteps = 100;
    speed.valueSpan = 100;
    speed.valueSuffix = "%";
    speed.hasReset = true;
    speed.resetValue = 0.5f;
    speed.floatVal = std::clamp(screen.m_carouselScrollSpeed, 0, 100) / 100.f;
    speed.anim01 = speed.floatVal;
    speed.onChange = [&screen](SettingItem& self) {
        const int pct = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        screen.m_carouselScrollSpeed = pct;
        if (screen.m_carouselScrollSpeedCb)
            screen.m_carouselScrollSpeedCb(pct);
    };
    tab.items.push_back(std::move(speed));

    SettingItem sizeSection;
    sizeSection.type = ItemType::Section;
    sizeSection.label = i18n.tr("settings.carousel.size_section", "Focus & size");
    tab.items.push_back(std::move(sizeSection));

    SettingItem shrink;
    shrink.type = ItemType::Slider;
    shrink.label = i18n.tr("settings.appearance.carousel_shrink", "Icon shrink");
    shrink.description = i18n.tr("settings.appearance.carousel_shrink_desc",
        "How much the tiles shrink as they move away from the focused one. 0% "
        "keeps every icon the same size; higher values make the neighbours "
        "smaller and more of the row fit on screen.");
    shrink.sliderSteps = 100;
    shrink.valueSpan = 100;
    shrink.valueSuffix = "%";
    shrink.hasReset = true;
    shrink.resetValue = 0.5f;
    shrink.floatVal = std::clamp(screen.m_carouselShrink, 0, 100) / 100.f;
    shrink.anim01 = shrink.floatVal;
    shrink.onChange = [&screen](SettingItem& self) {
        const int pct = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        screen.m_carouselShrink = pct;
        if (screen.m_carouselShrinkCb)
            screen.m_carouselShrinkCb(pct);
    };
    tab.items.push_back(std::move(shrink));

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

    SettingItem positionSection;
    positionSection.type = ItemType::Section;
    positionSection.label = i18n.tr("settings.carousel.position_section", "Position");
    tab.items.push_back(std::move(positionSection));

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

    SettingItem motionSection;
    motionSection.type = ItemType::Section;
    motionSection.label = i18n.tr("settings.carousel.motion_section", "Movement feel");
    tab.items.push_back(std::move(motionSection));

    SettingItem motionRow;
    motionRow.type = ItemType::Selector;
    motionRow.label = i18n.tr("settings.appearance.motion", "Motion preset");
    motionRow.description = i18n.tr("settings.appearance.motion_desc",
        "Crisp snaps and stops, Smooth glides, Springy overshoots then settles. "
        "Drives the carousel, page slides, edge bump and the focus ring.");
    motionRow.options = {
        i18n.tr("settings.appearance.motion_crisp", "Crisp"),
        i18n.tr("settings.appearance.motion_smooth", "Smooth"),
        i18n.tr("settings.appearance.motion_springy", "Springy"),
    };
    motionRow.intVal = std::clamp(screen.m_motionStyle, 0, 2);
    motionRow.onChange = [&screen](SettingItem& self) {
        screen.m_motionStyle = std::clamp(self.intVal, 0, 2);
        if (screen.m_motionStyleCb)
            screen.m_motionStyleCb(screen.m_motionStyle);
    };
    tab.items.push_back(std::move(motionRow));

    return tab;
}
