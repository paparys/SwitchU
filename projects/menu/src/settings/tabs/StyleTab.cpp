#include "TabBuilders.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cmath>

SettingsScreen::Tab settings::tabs::StyleTab::build(SettingsScreen& screen) {
    using ItemType = SettingsScreen::ItemType;
    using SettingItem = SettingsScreen::SettingItem;
    auto& i18n = nxui::I18n::instance();

    SettingsScreen::Tab tab;
    tab.name = i18n.tr("settings.tabs.style", "Style");

    // Style lab. Every default matches the shipped look, so the section is a
    // place to discover the controls rather than a change users must undo.
    auto addSection = [&tab](std::string label) {
        SettingItem item;
        item.type = ItemType::Section;
        item.label = std::move(label);
        tab.items.push_back(std::move(item));
    };
    auto addInt = [&tab, &screen](std::string label, int* target, int lo, int hi,
                                  std::string suffix, std::string description,
                                  int reset) {
        SettingItem item;
        item.type = ItemType::Slider;
        item.label = std::move(label);
        item.description = std::move(description);
        item.valueBase = lo;
        item.valueSpan = hi - lo;
        item.valueSuffix = suffix;
        item.sliderSteps = hi - lo;
        item.floatVal = (static_cast<float>(*target) - lo) / static_cast<float>(hi - lo);
        item.anim01 = item.floatVal;
        item.hasReset = true;
        item.resetValue = (static_cast<float>(reset) - lo) / static_cast<float>(hi - lo);
        item.onChange = [&screen, target, lo, hi](SettingItem& self) {
            const int value = lo + static_cast<int>(std::lround(
                std::clamp(self.floatVal, 0.f, 1.f) * static_cast<float>(hi - lo)));
            *target = std::clamp(value, lo, hi);
            screen.notifyStyleChanged();
        };
        tab.items.push_back(std::move(item));
    };
    auto addBool = [&tab, &screen](std::string label, bool* target,
                                   std::string description) {
        SettingItem item;
        item.type = ItemType::Toggle;
        item.label = std::move(label);
        item.description = std::move(description);
        item.boolVal = *target;
        item.anim01 = *target ? 1.f : 0.f;
        item.onChange = [&screen, target](SettingItem& self) {
            *target = self.boolVal;
            screen.notifyStyleChanged();
        };
        tab.items.push_back(std::move(item));
    };

    addSection(i18n.tr("settings.style.chrome", "Chrome"));
    addInt(i18n.tr("settings.style.panel_tint", "Panel tint mix"),
           &screen.m_panelTintMix, 0, 100, "%",
           i18n.tr("settings.style.panel_tint_desc",
                   "Blends the theme's panel colour toward the opacity tint. 0 keeps the theme; 100 uses the tint colour everywhere."), 0);
    addInt(i18n.tr("settings.style.border", "Border and rim"),
           &screen.m_panelBorderPct, 0, 200, "%",
           i18n.tr("settings.style.border_desc",
                   "Scales every panel, capsule and tile rim. 100 matches the theme."), 100);
    addInt(i18n.tr("settings.style.grain", "Panel grain"),
           &screen.m_panelGrainPct, 0, 100, "%",
           i18n.tr("settings.style.grain_desc",
                   "Adds the subtle material grain used on larger glass panels."), 0);
    addInt(i18n.tr("settings.style.shadow", "Panel shadow"),
           &screen.m_panelShadowPct, 0, 100, "%",
           i18n.tr("settings.style.shadow_desc",
                   "Soft drop shadow behind dialogs and large panels. Tiles keep their own shadow."), 0);

    addSection(i18n.tr("settings.style.focus", "Focus"));
    addInt(i18n.tr("settings.style.ring", "Ring thickness"),
           &screen.m_focusRingPx, 1, 6, "px",
           i18n.tr("settings.style.ring_desc",
                   "Thickness of the focus glow around the selected tile."), 3);
    addInt(i18n.tr("settings.style.glow", "Focus glow"),
           &screen.m_focusGlowPct, 0, 100, "%",
           i18n.tr("settings.style.glow_desc",
                   "Strength of the tinted halo around the focused tile."), 100);
    addInt(i18n.tr("settings.style.lift", "Focus lift"),
           &screen.m_focusLiftPct, 0, 100, "%",
           i18n.tr("settings.style.lift_desc",
                   "How much the focused tile grows and rises. 100 is the shipped pop."), 100);
    addInt(i18n.tr("settings.style.overshoot", "Focus overshoot"),
           &screen.m_focusOvershootPct, 0, 100, "%",
           i18n.tr("settings.style.overshoot_desc",
                   "Adds a small spring to the focus pop. 0 is a clean ease-out."), 0);

    addSection(i18n.tr("settings.style.title", "Title capsule"));
    addInt(i18n.tr("settings.style.title_scale", "Text scale"),
           &screen.m_titleScalePct, 80, 120, "%",
           i18n.tr("settings.style.title_scale_desc",
                   "Scales the game name inside the capsule and re-fits the capsule around it."), 100);
    {
        SettingItem corner;
        corner.type = ItemType::Selector;
        corner.label = i18n.tr("settings.style.title_corner", "Corner style");
        corner.description = i18n.tr("settings.style.title_corner_desc",
                                     "Pill keeps the capsule; Rounded and Rect flatten it into a plate.");
        corner.options = {
            i18n.tr("settings.style.corner_pill", "Pill"),
            i18n.tr("settings.style.corner_rounded", "Rounded"),
            i18n.tr("settings.style.corner_rect", "Rect")
        };
        corner.intVal = std::clamp(screen.m_titleCornerStyle, 0, 2);
        corner.onChange = [&screen](SettingItem& self) {
            screen.m_titleCornerStyle = std::clamp(self.intVal, 0, 2);
            screen.notifyStyleChanged();
        };
        tab.items.push_back(std::move(corner));
    }
    addInt(i18n.tr("settings.style.title_bg", "Capsule strength"),
           &screen.m_titleBackgroundPct, 0, 100, "%",
           i18n.tr("settings.style.title_bg_desc",
                   "Opacity of the capsule backing. 0 leaves the game name floating on the artwork."), 100);

    addSection(i18n.tr("settings.style.hud", "Top bar"));
    addInt(i18n.tr("settings.style.hud_gap", "Item gap"),
           &screen.m_hudGapPx, 0, 24, "px",
           i18n.tr("settings.style.hud_gap_desc",
                   "Minimum space between the profile, clock and battery capsules."), 0);
    addBool(i18n.tr("settings.style.hud_clock", "Show clock"),
            &screen.m_showHudClock,
            i18n.tr("settings.style.hud_clock_desc", "Show the time capsule in the top bar."));
    addBool(i18n.tr("settings.style.hud_date", "Show date"),
            &screen.m_showHudDate,
            i18n.tr("settings.style.hud_date_desc", "Show the date under the time."));
    addBool(i18n.tr("settings.style.hud_battery", "Show battery"),
            &screen.m_showHudBattery,
            i18n.tr("settings.style.hud_battery_desc", "Show the controller and console battery capsule."));
    addBool(i18n.tr("settings.style.hud_profile", "Show profile"),
            &screen.m_showHudProfile,
            i18n.tr("settings.style.hud_profile_desc", "Show the profile button in the top bar."));

    addSection(i18n.tr("settings.style.wallpaper", "Wallpaper"));
    addInt(i18n.tr("settings.style.wallpaper_blur", "Soften"),
           &screen.m_wallpaperBlurPx, 0, 16, "px",
           i18n.tr("settings.style.wallpaper_blur_desc",
                   "Softens the wallpaper so foreground tiles read more clearly."), 0);
    addInt(i18n.tr("settings.style.wallpaper_sat", "Saturation"),
           &screen.m_wallpaperSaturationPct, 0, 150, "%",
           i18n.tr("settings.style.wallpaper_sat_desc",
                   "Below 100% drains colour; above 100% adds a light accent boost."), 100);
    addInt(i18n.tr("settings.style.wallpaper_sharpness", "Sharpness"),
           &screen.m_wallpaperSharpnessPct, 0, 100, "%",
           i18n.tr("settings.style.wallpaper_sharpness_desc",
                   "Unsharp detail on the still wallpaper. 0 leaves the art untouched."), 0);
    addInt(i18n.tr("settings.style.wallpaper_vibrance", "Vibrance"),
           &screen.m_wallpaperVibrancePct, 0, 100, "%",
           i18n.tr("settings.style.wallpaper_vibrance_desc",
                   "Lifts muted colours more than already-vivid ones. 0 is neutral."), 0);
    addInt(i18n.tr("settings.style.wallpaper_noise", "Noise"),
           &screen.m_wallpaperNoisePct, 0, 100, "%",
           i18n.tr("settings.style.wallpaper_noise_desc",
                   "Adds fine film grain to the still wallpaper. 0 is clean."), 0);
    addInt(i18n.tr("settings.style.wallpaper_tint", "Backdrop tint"),
           &screen.m_backdropTintPct, 0, 100, "%",
           i18n.tr("settings.style.wallpaper_tint_desc",
                   "Lays the theme's accent colour over the wallpaper."), 0);

    addSection(i18n.tr("settings.style.motion", "Motion"));
    addInt(i18n.tr("settings.style.slide_ms", "Page slide"),
           &screen.m_pageSlideMs, 150, 600, "ms",
           i18n.tr("settings.style.slide_ms_desc",
                   "Duration of the horizontal page slide. Lower feels snappier."), 340);

    addSection(i18n.tr("settings.style.presets", "Look presets"));
    for (int slot = 0; slot < 3; ++slot) {
        SettingItem preset;
        preset.type = ItemType::Action;
        preset.label = screen.m_lookPresetNames[slot];
        preset.buttonLabel = i18n.tr("settings.style.save", "Save");
        preset.secondaryButtonLabel = i18n.tr("settings.style.load", "Load");
        preset.description = screen.m_lookPresetFilled[slot]
            ? i18n.tr("settings.style.preset_saved", "Saved look. Save overwrites it; Load applies every style value.")
            : i18n.tr("settings.style.preset_empty", "Empty slot. Save captures the current chrome, focus, title, HUD, wallpaper and motion style.");
        preset.onChange = [&screen, slot](SettingItem&) {
            if (screen.m_saveLookPresetCb)
                screen.m_saveLookPresetCb(slot);
        };
        preset.onSecondary = [&screen, slot](SettingItem&) {
            if (screen.m_loadLookPresetCb)
                screen.m_loadLookPresetCb(slot);
        };
        screen.m_lookPresetRow[slot] = static_cast<int>(tab.items.size());
        tab.items.push_back(std::move(preset));

        SettingItem rename;
        rename.type = ItemType::Action;
        rename.label = i18n.tr("settings.style.preset_name", "Preset name");
        rename.buttonLabel = i18n.tr("settings.style.rename", "Rename");
        rename.description = i18n.tr("settings.style.current_name", "Current name: ")
            + screen.m_lookPresetNames[slot];
        rename.onChange = [&screen, slot](SettingItem&) {
            screen.requestLookPresetRename(slot);
        };
        screen.m_lookRenameRow[slot] = static_cast<int>(tab.items.size());
        tab.items.push_back(std::move(rename));
    }


    return tab;
}
