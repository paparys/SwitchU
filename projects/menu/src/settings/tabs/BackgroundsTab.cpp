#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
std::string wallpaperLabel(const std::string& path) {
    if (path.empty())
        return "None (theme image)";
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace

SettingsScreen::Tab settings::tabs::BackgroundsTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.backgrounds", "Backgrounds");

    SettingItem wallpaperSection;
    wallpaperSection.type = ItemType::Section;
    wallpaperSection.label = i18n.tr("settings.backgrounds.wallpaper_section",
                                     "Wallpaper");
    tab.items.push_back(std::move(wallpaperSection));

    SettingItem choose;
    choose.type = ItemType::Action;
    choose.label = i18n.tr("settings.backgrounds.wallpaper", "Wallpaper");
    choose.buttonLabel = i18n.tr("settings.backgrounds.wallpaper_choose", "Choose");
    choose.description = i18n.tr("settings.backgrounds.wallpaper_choose_desc",
        "Pick a PNG, JPEG, WebP, GIF or Motion-JPEG MP4 from "
        "sdmc:/config/SwitchU/wallpapers. The picker shows a full preview before "
        "anything is applied. PNG and JPEG files are still wallpapers.");
    choose.infoText = wallpaperLabel(screen.wallpaperPath());
    choose.onChange = [&screen](SettingItem&) {
        screen.notifyWallpaperPicker();
    };
    tab.items.push_back(std::move(choose));

    SettingItem animate;
    animate.type = ItemType::Toggle;
    animate.label = i18n.tr("settings.backgrounds.wallpaper_animate",
                            "Animate wallpapers");
    animate.description = i18n.tr("settings.backgrounds.wallpaper_animate_desc",
        "Play animated WebP, GIF and Motion-JPEG MP4 files. PNG and JPEG stills "
        "always display, whether this is on or off.");
    animate.boolVal = screen.wallpaperAnimated();
    animate.anim01 = animate.boolVal ? 1.f : 0.f;
    animate.onChange = [&screen](SettingItem& self) {
        screen.setWallpaperAnimated(self.boolVal);
        screen.notifyWallpaperAnimated(self.boolVal);
    };
    tab.items.push_back(std::move(animate));

    SettingItem fps;
    fps.type = ItemType::Slider;
    fps.label = i18n.tr("settings.backgrounds.wallpaper_fps",
                        "Animation frame rate");
    fps.description = i18n.tr("settings.backgrounds.wallpaper_fps_desc",
        "Upper bound for animated wallpapers. Lower values use less CPU and "
        "battery; still PNG and JPEG wallpapers ignore this setting.");
    fps.sliderSteps = 25;
    fps.valueBase = 5;
    fps.valueSpan = 25;
    fps.valueSuffix = " fps";
    fps.floatVal = (std::clamp(screen.wallpaperFps(), 5, 30) - 5) / 25.f;
    fps.anim01 = fps.floatVal;
    fps.enabled = screen.wallpaperAnimated();
    fps.onChange = [&screen](SettingItem& self) {
        const int value = 5 + static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 25.f));
        self.floatVal = (value - 5) / 25.f;
        screen.setWallpaperFps(value);
        screen.notifyWallpaperFps(value);
    };
    tab.items.push_back(std::move(fps));

    SettingItem wallpaperHint;
    wallpaperHint.type = ItemType::Info;
    wallpaperHint.label = i18n.tr("settings.backgrounds.wallpaper_hint",
                                  "Wallpaper folder");
    wallpaperHint.infoText = i18n.tr("settings.backgrounds.wallpaper_hint_text",
        "Put files in sdmc:/config/SwitchU/wallpapers. Supported formats: "
        "PNG, JPEG, WebP, GIF, and Motion-JPEG MP4.");
    tab.items.push_back(std::move(wallpaperHint));

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

    SettingItem fadeIn;
    fadeIn.type = ItemType::Slider;
    fadeIn.label = i18n.tr("settings.backgrounds.hero_fade",
                           "Per-game fade-in");
    fadeIn.description = i18n.tr("settings.backgrounds.hero_fade_desc",
        "How long a new per-game background takes to fade in once its artwork "
        "is ready. A longer fade hides the texture upload; 0 shows the art on "
        "the frame it arrives.");
    fadeIn.sliderSteps = 100;  // 0..10000 ms in 100 ms steps
    fadeIn.valueSpan = 100;
    fadeIn.hasReset = true;
    fadeIn.resetValue = 30.f / 100.f;
    fadeIn.floatVal = std::clamp(screen.heroFadeInMs(), 0, 10000) / 10000.f;
    fadeIn.anim01 = fadeIn.floatVal;
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f s", fadeIn.floatVal * 10.f);
        fadeIn.infoText = buf;
    }
    fadeIn.onChange = [&screen](SettingItem& self) {
        const int steps = static_cast<int>(
            std::lround(std::clamp(self.floatVal, 0.f, 1.f) * 100.f));
        const int ms = steps * 100;
        self.floatVal = steps / 100.f;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f s", ms / 1000.f);
        self.infoText = buf;
        screen.setHeroFadeInMs(ms);
        screen.notifyHeroFadeIn(ms);
    };
    tab.items.push_back(std::move(fadeIn));

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

    SettingItem shapes;
    shapes.type = ItemType::Toggle;
    shapes.label = i18n.tr("settings.backgrounds.shapes", "Drifting shapes");
    shapes.description = i18n.tr("settings.backgrounds.shapes_desc",
        "The floating glass shapes drawn over the wallpaper. Turn them off to "
        "keep the gradient and the wallpaper on their own.");
    shapes.boolVal = screen.backgroundShapes();
    shapes.anim01 = shapes.boolVal ? 1.f : 0.f;
    shapes.onChange = [&screen](SettingItem& self) {
        screen.setBackgroundShapes(self.boolVal);
        screen.notifyBackgroundShapes(self.boolVal);
    };
    tab.items.push_back(std::move(shapes));

    // Keep the live wallpaper filename and animation state in sync while the
    // chooser is open on top of this tab. The drift speed is meaningless until
    // drift is on, so grey it out rather than letting it move with nothing to
    // control.
    tab.onUpdate = [](SettingsScreen::Tab& current, TabbedOverlayScreen& base) {
        if (current.items.size() < 14)
            return;
        auto& settings = static_cast<SettingsScreen&>(base);
        const std::string name = wallpaperLabel(settings.wallpaperPath());
        if (current.items[1].infoText != name)
            current.items[1].infoText = name;
        current.items[3].enabled = settings.wallpaperAnimated();
        const bool on = settings.backgroundDrift();
        if (current.items[11].boolVal != on) {
            current.items[11].boolVal = on;
            current.items[11].anim01 = on ? 1.f : 0.f;
        }
        current.items[12].enabled = on;
    };

    return tab;
}
