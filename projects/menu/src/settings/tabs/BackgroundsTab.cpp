#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>

SettingsScreen::Tab settings::tabs::BackgroundsTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.backgrounds", "Backgrounds");

    SettingItem section;
    section.type = ItemType::Section;
    section.label = i18n.tr("settings.backgrounds.section", "Per-game backgrounds");
    tab.items.push_back(std::move(section));

    SettingItem resolution;
    resolution.type = ItemType::Selector;
    resolution.label = i18n.tr("settings.backgrounds.resolution", "Background resolution");
    resolution.description = i18n.tr("settings.backgrounds.resolution_desc",
        "Canvas the SteamGridDB hero is decoded at for the per-game background. "
        "Full is sharpest; the lower sizes use less memory and read less from "
        "the SD card, which makes scrolling through games feel quicker.");
    resolution.options = {
        i18n.tr("settings.backgrounds.res_full", "Full (720p)"),
        i18n.tr("settings.backgrounds.res_balanced", "Balanced (540p)"),
        i18n.tr("settings.backgrounds.res_fast", "Fast (360p)"),
    };
    resolution.intVal = std::clamp(screen.heroBackdropResolution(), 0, 2);
    resolution.onChange = [&screen](SettingItem& self) {
        screen.setHeroBackdropResolutionState(self.intVal);
        screen.notifyHeroBackdropResolution();
    };
    tab.items.push_back(std::move(resolution));

    SettingItem blur;
    blur.type = ItemType::Slider;
    blur.label = i18n.tr("settings.backgrounds.blur", "Background blur");
    blur.description = i18n.tr("settings.backgrounds.blur_desc",
        "Softens the per-game background so the icons and the title stay readable "
        "over busy artwork. 0% keeps the hero exactly as SteamGridDB provided "
        "it; the blur is applied once when the art is decoded.");
    blur.sliderSteps = 100;
    blur.valueSpan = 100;
    blur.hasReset = true;
    blur.resetValue = 0.f;
    blur.floatVal = std::clamp(screen.heroBackdropBlur(), 0, 100) / 100.f;
    blur.anim01 = blur.floatVal;
    blur.infoText = std::to_string(std::clamp(screen.heroBackdropBlur(), 0, 100)) + "%";
    blur.onChange = [&screen](SettingItem& self) {
        const int pct = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        screen.setHeroBackdropBlurState(pct);
        self.infoText = std::to_string(screen.heroBackdropBlur()) + "%";
        screen.notifyHeroBackdropBlur();
    };
    tab.items.push_back(std::move(blur));

    SettingItem hint;
    hint.type = ItemType::Info;
    hint.label = i18n.tr("settings.backgrounds.hint", "Storage");
    hint.infoText = i18n.tr("settings.backgrounds.hint_text",
        "Each size keeps its own decoded cache on the SD card, so switching "
        "does not re-decode art you already have at another size.");
    tab.items.push_back(std::move(hint));

    SettingItem motionSection;
    motionSection.type = ItemType::Section;
    motionSection.label = i18n.tr("settings.backgrounds.motion_section", "Background motion");
    tab.items.push_back(std::move(motionSection));

    SettingItem drift;
    drift.type = ItemType::Toggle;
    drift.label = i18n.tr("settings.backgrounds.drift", "Slow background drift");
    drift.description = i18n.tr("settings.backgrounds.drift_desc",
        "Pans the wallpaper and the per-game backdrop slowly left and right, so "
        "the background never sits completely still. The art is slightly "
        "overscanned so no edge shows while it moves.");
    drift.boolVal = screen.backgroundDrift();
    drift.anim01 = drift.boolVal ? 1.f : 0.f;
    drift.onChange = [&screen](SettingItem& self) {
        screen.setBackgroundDrift(self.boolVal);
        screen.notifyBackgroundDrift(self.boolVal);
    };
    tab.items.push_back(std::move(drift));

    SettingItem driftSpeed;
    driftSpeed.type = ItemType::Slider;
    driftSpeed.label = i18n.tr("settings.backgrounds.drift_speed", "Drift speed");
    driftSpeed.description = i18n.tr("settings.backgrounds.drift_speed_desc",
        "How quickly the background completes one left-right pass. 1% is a slow "
        "crawl, 100% crosses in about twelve seconds.");
    driftSpeed.sliderSteps = 99;
    driftSpeed.valueBase = 1;
    driftSpeed.valueSpan = 99;
    driftSpeed.hasReset = true;
    driftSpeed.resetValue = (35 - 1) / 99.f;
    driftSpeed.floatVal =
        (std::clamp(screen.backgroundDriftSpeed(), 1, 100) - 1) / 99.f;
    driftSpeed.anim01 = driftSpeed.floatVal;
    driftSpeed.enabled = drift.boolVal;
    driftSpeed.onChange = [&screen](SettingItem& self) {
        const int pct = 1 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 99.f));
        screen.setBackgroundDriftSpeed(pct);
        screen.notifyBackgroundDriftSpeed(pct);
    };
    tab.items.push_back(std::move(driftSpeed));

    // The speed row is meaningless until the drift is on, so grey it out rather
    // than letting it move with nothing to control.
    tab.onUpdate = [](SettingsScreen::Tab& current, TabbedOverlayScreen& base) {
        if (current.items.size() < 7)
            return;
        const bool on = static_cast<SettingsScreen&>(base).backgroundDrift();
        if (current.items[5].boolVal != on) {
            current.items[5].boolVal = on;
            current.items[5].anim01 = on ? 1.f : 0.f;
        }
        current.items[6].enabled = on;
    };

    return tab;
}
