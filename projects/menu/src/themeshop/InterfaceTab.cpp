#include "ThemeShopTabBuilders.hpp"

#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>

ThemeShopScreen::Tab themeshop::tabs::InterfaceTab::build(ThemeShopScreen& screen) {
    using Tab = ThemeShopScreen::Tab;
    using SettingItem = ThemeShopScreen::SettingItem;
    using ItemType = ThemeShopScreen::ItemType;
    auto& i18n = nxui::I18n::instance();

    Tab t;
    t.name = i18n.tr("themeshop.tabs.interface", "Interface");

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.interface.section_home", "HOME layout");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.sort_large_tiles", "Sort Large Tiles Too");
        if (screen.m_sortLargeTilesAvailable) {
            it.description = i18n.tr("themeshop.options.sort_large_tiles_desc",
                                     "A-Z and Recent also move applications that use a larger tile size.");
            it.type = ItemType::Toggle;
            it.boolVal = screen.m_sortLargeTiles;
            it.anim01 = it.boolVal ? 1.f : 0.f;
            it.onChange = [&screen](SettingItem& self) {
                screen.m_sortLargeTiles = self.boolVal;
                if (screen.m_sortLargeTilesCb) screen.m_sortLargeTilesCb(self.boolVal);
            };
        } else {
            // Carousel and Poster Wall cannot produce a larger tile, so leave a
            // read-only explanation instead of a toggle that silently no-ops.
            it.label = i18n.tr("themeshop.options.sort_large_tiles_unavailable",
                               "Sort Large Tiles Too (unavailable)");
            it.description = i18n.tr("themeshop.options.sort_large_tiles_unavailable_desc",
                "This HOME layout uses equal-size tiles. Switch to Shelf or Showcase "
                "to enable larger tiles and this sorting option.");
            it.type = ItemType::Info;
            it.infoText = i18n.tr("common.unavailable", "Unavailable");
        }
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.interface.section_icons", "Icons");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.icon_contrast", "Adaptive icon contrast");
        it.description = i18n.tr(
            "themeshop.options.icon_contrast_desc",
            "In single-row with a hero, colour HOME icons follow the interface light/dark chrome. Without a hero they keep authored colours (or your custom tint).");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_iconContrastAdapt;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_iconContrastAdapt = self.boolVal;
            if (screen.m_iconContrastAdaptCb)
                screen.m_iconContrastAdaptCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.theme_icon_override",
                           "Allow themes to replace my icons");
        it.description = i18n.tr(
            "themeshop.options.theme_icon_override_desc",
            "Themes with icons can switch your Icons pack.");
        it.type = ItemType::Toggle;
        it.boolVal = screen.m_themeIconPackOverride;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_themeIconPackOverride = self.boolVal;
            if (screen.m_themeIconPackOverrideCb)
                screen.m_themeIconPackOverrideCb(self.boolVal);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.interface.section_controller", "Controller hints");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.action_hints", "Action Hints");
        it.description = i18n.tr("themeshop.options.action_hints_desc",
                                 "Choose how contextual controls appear in the bottom-right corner.");
        it.type = ItemType::Selector;
        it.options = {
            i18n.tr("themeshop.options.hint_panel", "Panel"),
            i18n.tr("themeshop.options.hint_capsules", "Capsules")
        };
        it.intVal = std::clamp(screen.m_actionHintStyle, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_actionHintStyle = std::clamp(self.intVal, 0, 1);
            if (screen.m_actionHintStyleCb)
                screen.m_actionHintStyleCb(screen.m_actionHintStyle);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.interface.section_themes", "Themes");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("themeshop.options.auto_theme", "Automatic Theme");
        it.description = screen.m_autoThemeSummary.empty()
            ? i18n.tr("themeshop.options.auto_theme_desc",
                      "Switch between a day theme and a night theme automatically.")
            : screen.m_autoThemeSummary;
        it.type = ItemType::Action;
        it.onChange = [&screen](SettingItem&) {
            if (screen.m_autoThemeOpenCb)
                screen.m_autoThemeOpenCb();
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.cursor_motion", "Cursor Movement");
        it.type = ItemType::Selector;
        it.description = i18n.tr(
            "settings.display.cursor_motion_desc",
            "Choose whether the selection cursor slides or teleports with a visual pulse.");
        it.options = {
            i18n.tr("settings.display.cursor_translate", "Translate"),
            i18n.tr("settings.display.cursor_teleport", "Teleport")
        };
        it.intVal = std::clamp(screen.m_cursorMotionMode, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_cursorMotionMode = std::clamp(self.intVal, 0, 1);
            if (screen.m_cursorMotionModeCb)
                screen.m_cursorMotionModeCb(screen.m_cursorMotionMode);
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.display.page_transition", "Page Transition");
        it.type = ItemType::Selector;
        it.description = i18n.tr(
            "settings.display.page_transition_desc",
            "Choose whether switching pages slides or swaps instantly.");
        it.options = {
            i18n.tr("settings.display.page_translate", "Translate"),
            i18n.tr("settings.display.page_teleport", "Teleport")
        };
        it.intVal = std::clamp(screen.m_pageTransitionMode, 0, 1);
        it.onChange = [&screen](SettingItem& self) {
            screen.m_pageTransitionMode = std::clamp(self.intVal, 0, 1);
            if (screen.m_pageTransitionModeCb)
                screen.m_pageTransitionModeCb(screen.m_pageTransitionMode);
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
