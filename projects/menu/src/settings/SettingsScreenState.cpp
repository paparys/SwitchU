#include "SettingsScreen.hpp"
#include <cctype>
#include <nxui/core/Renderer.hpp>
#include "tabs/TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include "smi_commands.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "ui/ChromeTokens.hpp"
#include "ui/ScreenShell.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <chrono>
#include <cstdio>

namespace chrome = switchu::ui::chrome;
namespace shell = switchu::ui::shell;

namespace {

bool isRetriableClockSyncError(const std::string& error) {
    return error.find("nifmGetInternetConnectionStatus") != std::string::npos
        || error.find("Internet connection is not ready") != std::string::npos
        || error.find("nifmInitialize") != std::string::npos;
}

std::string normalizeLockCode(const std::string& code) {
    std::string clean;
    for (char c : code) {
        if ((c == 'U' || c == 'D' || c == 'L' || c == 'R') && clean.size() < 4)
            clean.push_back(c);
    }
    while (clean.size() < 4)
        clean.push_back('U');
    return clean;
}

} // namespace

void SettingsScreen::setLockProfilesState(std::vector<LockProfileState> profiles) {
    std::string selectedUid;
    if (const auto* current = selectedLockProfile())
        selectedUid = current->uid;

    m_lockProfiles = std::move(profiles);
    for (auto& profile : m_lockProfiles)
        profile.code = normalizeLockCode(profile.code);

    if (m_lockProfiles.empty()) {
        m_lockPadProfileIndex = 0;
        refreshLockProfilesTab();
        return;
    }

    int index = -1;
    if (!selectedUid.empty()) {
        for (int i = 0; i < static_cast<int>(m_lockProfiles.size()); ++i) {
            if (m_lockProfiles[static_cast<std::size_t>(i)].uid == selectedUid) {
                index = i;
                break;
            }
        }
    } else if (!m_lockPadUserChoseProfile && !m_lockPadPreferredUid.empty()) {
        // Until the User row is touched, the editor follows the account that
        // owns this session instead of the first console user.
        for (int i = 0; i < static_cast<int>(m_lockProfiles.size()); ++i) {
            if (m_lockProfiles[static_cast<std::size_t>(i)].uid == m_lockPadPreferredUid) {
                index = i;
                break;
            }
        }
    }
    m_lockPadProfileIndex = index >= 0
        ? index
        : std::clamp(m_lockPadProfileIndex, 0,
                     static_cast<int>(m_lockProfiles.size()) - 1);
    refreshLockProfilesTab();
}

void SettingsScreen::refreshLockProfilesTab() {
    constexpr int kLockTab = 17;
    if (kLockTab >= static_cast<int>(m_tabs.size()))
        return;
    if (kLockTab < static_cast<int>(m_loadingTabs.size()) &&
        m_loadingTabs[kLockTab]) {
        m_lockProfilesTabDirty = true;
        return;
    }

    // If the section is already cached, rebuild its rows so the toggle and
    // four step selectors describe the newly selected profile. A tab that has
    // not been opened yet will read the current state when it is built.
    const bool loaded = kLockTab < static_cast<int>(m_loadedTabs.size()) &&
                        m_loadedTabs[kLockTab];
    if (!loaded && m_tabIndex != kLockTab)
        return;

    Tab refreshed = buildTabNow(kLockTab);
    refreshed.key = m_tabs[kLockTab].key;
    m_tabs[kLockTab] = std::move(refreshed);
    if (kLockTab < static_cast<int>(m_loadedTabs.size()))
        m_loadedTabs[kLockTab] = true;
    if (kLockTab < static_cast<int>(m_cachedTabContentWidgets.size()))
        m_cachedTabContentWidgets[kLockTab].clear();
    if (m_tabIndex == kLockTab) {
        clampContentIdx();
        rebuildContentItems();
    }
}

void SettingsScreen::setLockPadProfileIndex(int index) {
    if (m_lockProfiles.empty()) {
        m_lockPadProfileIndex = 0;
        return;
    }
    m_lockPadProfileIndex = std::clamp(index, 0,
                                       static_cast<int>(m_lockProfiles.size()) - 1);
    m_lockPadUserChoseProfile = true;
}

SettingsScreen::LockProfileState* SettingsScreen::selectedLockProfile() {
    if (m_lockProfiles.empty() || m_lockPadProfileIndex < 0 ||
        m_lockPadProfileIndex >= static_cast<int>(m_lockProfiles.size()))
        return nullptr;
    return &m_lockProfiles[static_cast<std::size_t>(m_lockPadProfileIndex)];
}

const SettingsScreen::LockProfileState* SettingsScreen::selectedLockProfile() const {
    if (m_lockProfiles.empty() || m_lockPadProfileIndex < 0 ||
        m_lockPadProfileIndex >= static_cast<int>(m_lockProfiles.size()))
        return nullptr;
    return &m_lockProfiles[static_cast<std::size_t>(m_lockPadProfileIndex)];
}

void SettingsScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    // A language switch lands here; the shell text and its measurements are
    // rebuilt on the next frame rather than re-measured on every frame.
    m_headerTitleText.clear();
    m_footerHintsText.clear();
    m_footerHintsKey = -1;
    m_footerAutoSaveText.clear();
    // Grouped rather than chronological: system and hardware first, then the
    // HOME look, and the personal screens. Related screens are
    // folded together (presets under Icons & Wallpaper, opacity under Screen
    // treatment, docked rows under Display) so the rail stays short. The stable
    // key survives tab reordering and localisation.
    m_tabs = {
        // --- System ---
        {.name = i18n.tr("settings.tabs.system", "System"),
         .group = i18n.tr("settings.groups.system", "System"), .groupIcon = 0,
         .key = "system"},
        {.name = i18n.tr("settings.tabs.display", "Display"), .key = "display"},
        {.name = i18n.tr("settings.tabs.audio", "Audio"), .key = "audio"},
        {.name = i18n.tr("settings.tabs.internet", "Internet"), .key = "internet"},
        {.name = i18n.tr("settings.tabs.controllers", "Controllers"), .key = "controllers"},
        {.name = i18n.tr("settings.tabs.bluetooth", "Bluetooth"), .key = "bluetooth"},
        {.name = i18n.tr("settings.tabs.sleep", "Sleep"), .key = "sleep"},
        {.name = i18n.tr("settings.tabs.storage", "Storage"), .key = "storage"},
        {.name = i18n.tr("settings.tabs.about", "About"), .key = "about"},
        // --- HOME look ---
        {.name = i18n.tr("settings.tabs.backgrounds", "Backgrounds"),
         .group = i18n.tr("settings.groups.home", "Home Menu"), .groupIcon = 1,
         .key = "backgrounds"},
        {.name = i18n.tr("settings.tabs.appearance", "Icons & Wallpaper"), .key = "appearance"},
        {.name = i18n.tr("settings.tabs.steamgriddb", "SteamGridDB"), .key = "steamgriddb"},
        {.name = i18n.tr("settings.tabs.carousel", "Carousel settings"), .key = "carousel"},
        {.name = i18n.tr("settings.tabs.treatment", "Screen treatment"), .key = "treatment"},
        {.name = i18n.tr("settings.tabs.style", "Style"), .key = "style"},
        {.name = i18n.tr("settings.tabs.shortcuts", "Shortcuts"), .key = "shortcuts"},
        // --- Personal ---
        {.name = i18n.tr("settings.tabs.hidden", "Hidden Games"),
         .group = i18n.tr("settings.groups.personal", "Personal"), .groupIcon = 2,
         .key = "hidden"},
        {.name = i18n.tr("settings.tabs.lockscreen", "Lock Screen"), .key = "lockscreen"},
        {.name = i18n.tr("settings.tabs.accessibility", "Accessibility"), .key = "accessibility"},
    };
    m_loadedTabs.assign(m_tabs.size(), false);
    m_loadingTabs.assign(m_tabs.size(), false);
    m_tabTasks.clear();
    m_tabTasks.resize(m_tabs.size());
    m_nextPrefetchTab = 1;

    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());

    DebugLog::log("[settings] registered %d lazy tabs", (int)m_tabs.size());
}

void SettingsScreen::drawOverlayHeader(nxui::Renderer& ren, const nxui::Rect& panel,
                                       float opacity) {
    if (!m_theme || !m_font || !m_smallFont)
        return;
    auto& i18n = nxui::I18n::instance();
    if (m_headerTitleText.empty())
        m_headerTitleText = i18n.tr("settings.title", "Settings");

    // Product name as a quiet eyebrow; the active section is the headline.
    // The row descriptions live up here, so the list itself stays one line per
    // setting. Focused row first, otherwise the tab's opening description.
    const SettingItem* focused = focusedSettingItem();
    std::string description;
    if (focused && !focused->description.empty()) {
        description = focused->description;
    } else if (m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size()) {
        for (const auto& item : m_tabs[(std::size_t)m_tabIndex].items) {
            if (!item.description.empty()) {
                description = item.description;
                break;
            }
        }
    }

    shell::drawOverlayHeader(ren, {
        .panel = panel,
        .theme = m_theme,
        .font = m_font,
        .smallFont = m_smallFont,
        .eyebrow = m_headerTitleText,
        .headline = (m_tabIndex >= 0 && m_tabIndex < (int)m_tabs.size())
            ? m_tabs[(std::size_t)m_tabIndex].name : std::string(),
        .description = description,
        .height = overlayHeaderHeight(),
        .opacity = opacity,
    });
}

void SettingsScreen::drawOverlayFooter(nxui::Renderer& ren, const nxui::Rect& panel,
                                       float opacity) {
    if (!m_theme || !m_smallFont)
        return;
    auto& i18n = nxui::I18n::instance();

    int hintsKey = 0;
    if (m_dropdownOpen) {
        hintsKey = 1;
    } else if (m_focusArea == FocusArea::Content) {
        const SettingItem* item = focusedSettingItem();
        if (item) {
            hintsKey = 10 + (int)item->type * 4
                + (item->hasSecondaryAction() ? 1 : 0)
                + (item->hasReset ? 2 : 0);
        }
    }
    if (m_footerHintsKey != hintsKey || m_footerHintsText.empty()) {
        m_footerHintsKey = hintsKey;
        switch (hintsKey) {
            case 0:
                m_footerHintsText = i18n.tr("settings.footer_tabs",
                    "A  Open     D-pad  Navigate     B  Close");
                break;
            case 1:
                m_footerHintsText = i18n.tr("settings.footer_list",
                    "A  Choose     B  Close list");
                break;
            default: {
                const SettingItem* item = focusedSettingItem();
                if (!item) {
                    m_footerHintsText = i18n.tr("settings.footer_tabs",
                        "A  Open     D-pad  Navigate     B  Close");
                } else if (item->type == ItemType::Toggle) {
                    m_footerHintsText = i18n.tr("settings.footer_toggle",
                        "A  Toggle     B  Tabs");
                } else if (item->type == ItemType::Slider) {
                    m_footerHintsText = item->hasReset
                        ? i18n.tr("settings.footer_slider_reset",
                            "Left/Right  Adjust     X  Reset     B  Tabs")
                        : i18n.tr("settings.footer_slider",
                            "Left/Right  Adjust     B  Tabs");
                } else if (item->type == ItemType::Selector) {
                    m_footerHintsText = i18n.tr("settings.footer_selector",
                        "A  Choose     Left/Right  Change     B  Tabs");
                } else if (item->type == ItemType::Action) {
                    m_footerHintsText = item->hasSecondaryAction()
                        ? i18n.tr("settings.footer_action_secondary",
                            "A  Select     X  Clear     B  Tabs")
                        : i18n.tr("settings.footer_action",
                            "A  Activate     B  Tabs");
                } else {
                    m_footerHintsText = i18n.tr("settings.footer_info", "B  Tabs");
                }
                break;
            }
        }
    }
    if (m_footerAutoSaveText.empty())
        m_footerAutoSaveText = i18n.tr("settings.footer_autosave",
            "Changes save automatically");

    shell::drawOverlayFooter(ren, {
        .panel = panel,
        .theme = m_theme,
        .font = m_smallFont,
        .hints = m_footerHintsText,
        .note = m_footerAutoSaveText,
        .height = overlayFooterHeight(),
        .opacity = opacity,
    });
}

void SettingsScreen::ensureTabLoaded(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= static_cast<int>(m_tabs.size()))
        return;
    if (m_loadedTabs.size() != m_tabs.size())
        m_loadedTabs.assign(m_tabs.size(), false);
    if (m_loadingTabs.size() != m_tabs.size())
        m_loadingTabs.assign(m_tabs.size(), false);
    if (m_tabTasks.size() != m_tabs.size())
        m_tabTasks.resize(m_tabs.size());
    if (m_loadedTabs[static_cast<std::size_t>(tabIndex)])
        return;
    if (m_loadingTabs[static_cast<std::size_t>(tabIndex)])
        return;

    if (m_tabs[static_cast<std::size_t>(tabIndex)].key == "bluetooth" &&
        !bluetooth::IsAvailable()) {
        bluetooth::Initialize();
        DebugLog::log("[settings] Bluetooth audio manager initialized on demand");
    }

    m_tabs[static_cast<std::size_t>(tabIndex)] = makeLoadingTab(tabIndex);
    startAsyncTabLoad(tabIndex);
}

SettingsScreen::Tab SettingsScreen::buildTabNow(int tabIndex) {
    Tab tab;
    // Dispatch on the stable key instead of the rail position: tabs can be
    // regrouped or folded together without renumbering every case below.
    const std::string key =
        (tabIndex >= 0 && tabIndex < static_cast<int>(m_tabs.size()))
            ? m_tabs[static_cast<std::size_t>(tabIndex)].key
            : std::string();
    if (key == "system") tab = settings::tabs::SystemTab::build(*this);
    else if (key == "display") tab = settings::tabs::DisplayTab::build(*this);
    else if (key == "audio") tab = settings::tabs::AudioTab::build(*this);
    else if (key == "internet") tab = settings::tabs::InternetTab::build(*this);
    else if (key == "controllers") tab = settings::tabs::ControllersTab::build(*this);
    else if (key == "bluetooth") tab = settings::tabs::BluetoothTab::build(*this);
    else if (key == "sleep") tab = settings::tabs::SleepTab::build(*this);
    else if (key == "storage") tab = settings::tabs::StorageTab::build(*this);
    else if (key == "steamgriddb") tab = settings::tabs::SteamGridDbTab::build(*this);
    else if (key == "about") tab = settings::tabs::AboutTab::build(*this);
    else if (key == "backgrounds") tab = settings::tabs::BackgroundsTab::build(*this);
    else if (key == "appearance") tab = settings::tabs::AppearanceTab::build(*this);
    else if (key == "carousel") tab = settings::tabs::CarouselMovementTab::build(*this);
    else if (key == "treatment") tab = settings::tabs::TreatmentTab::build(*this);
    else if (key == "style") tab = settings::tabs::StyleTab::build(*this);
    else if (key == "shortcuts") tab = settings::tabs::ShortcutsTab::build(*this);
    else if (key == "hidden") tab = settings::tabs::HiddenGamesTab::build(*this);
    else if (key == "lockscreen") tab = settings::tabs::LockScreenTab::build(*this);
    else if (key == "accessibility") tab = settings::tabs::AccessibilityTab::build(*this);
    else tab = makeLoadingTab(tabIndex);

    if (tab.key.empty() && tabIndex >= 0 && tabIndex < static_cast<int>(m_tabs.size()))
        tab.key = m_tabs[static_cast<std::size_t>(tabIndex)].key;
    return tab;
}

SettingsScreen::Tab SettingsScreen::makeLoadingTab(int tabIndex) const {
    Tab tab;
    if (tabIndex >= 0 && tabIndex < static_cast<int>(m_tabs.size())) {
        tab.name = m_tabs[static_cast<std::size_t>(tabIndex)].name;
        tab.key = m_tabs[static_cast<std::size_t>(tabIndex)].key;
    } else
        tab.name = nxui::I18n::instance().tr("common.loading", "Loading");

    SettingItem item;
    item.label = nxui::I18n::instance().tr("common.loading", "Loading...");
    item.type = ItemType::Info;
    item.infoText = nxui::I18n::instance().tr("settings.loading_tab", "Loading this section in the background.");
    tab.items.push_back(std::move(item));
    return tab;
}

void SettingsScreen::startAsyncTabLoad(int tabIndex) {
    const std::size_t idx = static_cast<std::size_t>(tabIndex);
    if (idx >= m_tabs.size())
        return;

    // DebugLog::log("[settings] starting async tab load %d", tabIndex);
    m_loadingTabs[idx] = true;
    m_tabTasks[idx] = std::async(std::launch::async, [this, tabIndex]() {
        return buildTabNow(tabIndex);
    });
}

void SettingsScreen::pollTabLoaders() {
    bool rebuiltCurrent = false;
    for (std::size_t i = 0; i < m_tabTasks.size(); ++i) {
        if (i >= m_loadingTabs.size() || !m_loadingTabs[i] || !m_tabTasks[i].valid())
            continue;

        if (m_tabTasks[i].wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            continue;

        Tab loaded = m_tabTasks[i].get();
        // The lock profile list can arrive after this worker starts. Rebuild
        // that lightweight section once with the settled profile state.
        if (m_tabs[i].key == "lockscreen" && m_lockProfilesTabDirty) {
            m_lockProfilesTabDirty = false;
            loaded = buildTabNow(static_cast<int>(i));
        }
        if (loaded.key.empty())
            loaded.key = m_tabs[i].key;
        m_tabs[i] = std::move(loaded);
        if (i < m_loadedTabs.size())
            m_loadedTabs[i] = true;
        m_loadingTabs[i] = false;
        if (i < m_cachedTabContentWidgets.size())
            m_cachedTabContentWidgets[i].clear();

        DebugLog::log("[settings] async tab loaded %d items=%d",
                      (int)i, (int)m_tabs[i].items.size());

        if ((int)i == m_tabIndex && !rebuiltCurrent) {
            clampContentIdx();
            m_contentIdx = 0;
            m_scrollY = 0.f;
            m_scrollTarget = 0.f;
            m_scrollVel = 0.f;
            m_scrollMomentum = 0.f;
            m_touchScrollVel = 0.f;
            rebuildContentItems();
            rebuiltCurrent = true;
        }
    }
}

void SettingsScreen::prefetchOneTab() {
    if (m_tabs.empty() || m_tabIndex < 0 || m_tabIndex >= static_cast<int>(m_tabs.size()))
        return;

    // Never build more than one section at a time: each starts a worker thread
    // and touches storage or network, and overlapping builds are what made the
    // panel stutter while tabs were being visited.
    for (bool loading : m_loadingTabs)
        if (loading) return;

    // Only the neighbours of the current tab are worth warming: that is where
    // a D-pad step lands, and it keeps a twenty-section screen from building
    // itself in the background the moment it opens.
    const int count = static_cast<int>(m_tabs.size());
    const int candidates[2] = { (m_tabIndex + 1) % count,
                                (m_tabIndex + count - 1) % count };
    for (int idx : candidates) {
        if (idx == m_tabIndex || idx < 0 || idx >= count)
            continue;
        if (idx >= static_cast<int>(m_loadedTabs.size())
            || idx >= static_cast<int>(m_loadingTabs.size()))
            continue;
        if (m_loadedTabs[static_cast<std::size_t>(idx)]
            || m_loadingTabs[static_cast<std::size_t>(idx)])
            continue;

        // Bluetooth brings up a hardware client and About keeps the optional
        // btmsys client out of the handoff. Neither is warmed behind the
        // user's back.
        const std::string& key = m_tabs[static_cast<std::size_t>(idx)].key;
        if (key == "bluetooth" || key == "about")
            continue;

        m_tabs[static_cast<std::size_t>(idx)] = makeLoadingTab(idx);
        startAsyncTabLoad(idx);
        return;
    }
}

void SettingsScreen::requestLookPresetRename(int slot) {
    if (slot < 0 || slot > 2)
        return;
    auto& i18n = nxui::I18n::instance();
    requestTextEntry(i18n.tr("settings.style.rename_title", "Rename look"),
                     i18n.tr("settings.style.rename_guide", "Enter a name for this look"),
                     m_lookPresetNames[slot], 24,
                     [this, slot](const std::string& value) {
                         std::string name = value;
                         while (!name.empty() &&
                                std::isspace(static_cast<unsigned char>(name.front())))
                             name.erase(name.begin());
                         while (!name.empty() &&
                                std::isspace(static_cast<unsigned char>(name.back())))
                             name.pop_back();
                         if (name.empty())
                             return;
                         applyLookPresetName(slot, name);
                     });
}

void SettingsScreen::applyLookPresetName(int slot, const std::string& name) {
    if (slot < 0 || slot > 2 || name.empty())
        return;
    m_lookPresetNames[slot] = name;

    // The row widgets read their label from SettingItem every frame, so the
    // rename is visible immediately without rebuilding the tab (which would
    // invalidate the text-entry focus target).
    for (auto& tab : m_tabs) {
        if (tab.key != "style")
            continue;
        const int presetRow = m_lookPresetRow[slot];
        if (presetRow >= 0 && presetRow < static_cast<int>(tab.items.size()))
            tab.items[static_cast<std::size_t>(presetRow)].label = name;
        const int renameRow = m_lookRenameRow[slot];
        if (renameRow >= 0 && renameRow < static_cast<int>(tab.items.size())) {
            tab.items[static_cast<std::size_t>(renameRow)].description =
                nxui::I18n::instance().tr("settings.style.current_name",
                                          "Current name: ") + name;
        }
        break;
    }

    if (m_renameLookPresetCb)
        m_renameLookPresetCb(slot, name);
}

void SettingsScreen::onContentUpdate(float dt) {
    pollTabLoaders();
    if (m_lockProfileRefreshPending) {
        m_lockProfileRefreshPending = false;
        // Selecting another user changes every value below the selector, so
        // rebuild the section instead of only recreating its row widgets.
        refreshLockProfilesTab();
    }
    pollClockSync();
    if (isActive()) {
        // A tab build is a worker thread plus a full section build (storage,
        // profiles, network). Starting one per frame while the user moved
        // through the rail spawned a dozen threads at once and starved the
        // frame the panel was animating. Wait for the switch to settle, then
        // build at most one neighbour at a time.
        if (m_tabIndex != m_prefetchAnchor) {
            m_prefetchAnchor = m_tabIndex;
            m_tabPrefetchDelay = 0.35f;
        }
        if (m_tabPrefetchDelay > 0.f)
            m_tabPrefetchDelay = m_tabPrefetchDelay > dt
                ? m_tabPrefetchDelay - dt : 0.f;
        else
            prefetchOneTab();
    }
    TabbedOverlayScreen::onContentUpdate(dt);
}

void SettingsScreen::emitClockSyncToast(const std::string& msg, float holdSeconds) {
    if (msg.empty())
        return;
    if (isFullyVisible()) {
        requestToast(msg, holdSeconds);
        return;
    }
    if (m_externalToastCb) {
        m_externalToastCb(msg, holdSeconds);
        return;
    }
    requestToast(msg, holdSeconds);
}

void SettingsScreen::startWebClockSync(bool toastFailureOnly) {
    auto& i18n = nxui::I18n::instance();
    if (m_clockSyncFuture.valid()) {
        if (!toastFailureOnly) {
            emitClockSyncToast(i18n.tr(
                "settings.system.web_time_busy",
                "Clock sync is already running."));
        }
        return;
    }

    m_clockSyncToastFailureOnly = toastFailureOnly;
    if (!toastFailureOnly) {
        emitClockSyncToast(i18n.tr(
            "settings.system.web_time_working",
            "Syncing clock from the web..."), 1.8f);
    }

    m_clockSyncFuture = std::async(std::launch::async, []() -> ClockSyncFetch {
        ClockSyncFetch out;
        try {
            out.posixUtc = themeshop::http::fetchUtcUnixTime();
            out.ok = true;
        } catch (const std::exception& ex) {
            out.ok = false;
            out.error = ex.what();
        } catch (...) {
            out.ok = false;
            out.error = "Unknown clock sync error";
        }
        return out;
    });
}

void SettingsScreen::pollClockSync() {
    if (!m_clockSyncFuture.valid())
        return;
    if (m_clockSyncFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    const ClockSyncFetch fetched = m_clockSyncFuture.get();
    const bool failureOnly = m_clockSyncToastFailureOnly;
    m_clockSyncToastFailureOnly = false;
    auto& i18n = nxui::I18n::instance();
    if (!fetched.ok) {
        DebugLog::log("[settings-time] web clock sync fetch failed: %s",
                      fetched.error.c_str());
        if (failureOnly && m_clockSyncBootRetryCb
            && isRetriableClockSyncError(fetched.error)) {
            m_clockSyncBootRetryCb(fetched.error);
            return;
        }
        const std::string prefix = i18n.tr(
            "settings.system.web_time_failed",
            "Could not sync clock from the web.");
        if (fetched.error.empty())
            emitClockSyncToast(prefix);
        else
            emitClockSyncToast(prefix + " " + fetched.error);
        return;
    }

    // Nintendo auto-sync needs Nintendo servers; turn it off so DNS-blocked
    // setups keep the web-synced time.
    bool automatic = false;
    if (R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&automatic))
        && automatic) {
        const Result disableRc = switchu::menu::smi_cmd::setInternetTimeSync(false);
        DebugLog::log("[settings-time] web sync disabled nintendo auto rc=0x%X",
                      disableRc);
    }

    const std::string nintendoLabel = i18n.tr(
        "settings.system.internet_time",
        "Synchronize Clock via Nintendo Servers");
    const std::string publicLabel = i18n.tr(
        "settings.system.web_time",
        "Synchronize Clock via Public Servers");
    for (auto& tab : m_tabs) {
        for (auto& item : tab.items) {
            if (item.type != ItemType::Toggle)
                continue;
            if (item.label == nintendoLabel) {
                item.boolVal = false;
                item.anim01 = 0.f;
            } else if (item.label == publicLabel) {
                item.boolVal = m_webClockSyncEnabled;
                item.anim01 = m_webClockSyncEnabled ? 1.f : 0.f;
            }
        }
    }

    TimeCalendarTime calendar{};
    TimeCalendarAdditionalInfo additional{};
    const Result calRc = timeToCalendarTimeWithMyRule(
        fetched.posixUtc, &calendar, &additional);
    if (R_FAILED(calRc)) {
        DebugLog::log("[settings-time] web sync calendar convert rc=0x%X", calRc);
        emitClockSyncToast(i18n.tr(
            "settings.system.time_change_failed",
            "The date and time setting could not be changed."));
        return;
    }

    TabbedOverlayScreen::DateTimeEditorValue value;
    value.year = calendar.year;
    value.month = calendar.month;
    value.day = calendar.day;
    value.hour = calendar.hour;
    value.minute = calendar.minute;

    switchu::smi::ManualDateTimeArgs daemonArgs{};
    daemonArgs.year = static_cast<uint32_t>(value.year);
    daemonArgs.month = static_cast<uint32_t>(value.month);
    daemonArgs.day = static_cast<uint32_t>(value.day);
    daemonArgs.hour = static_cast<uint32_t>(value.hour);
    daemonArgs.minute = static_cast<uint32_t>(value.minute);
    const Result daemonRc = switchu::menu::smi_cmd::setManualDateTime(daemonArgs);
    DebugLog::log(
        "[settings-time] web sync apply %04u-%02u-%02u %02u:%02u rc=0x%X posix=%llu",
        daemonArgs.year, daemonArgs.month, daemonArgs.day,
        daemonArgs.hour, daemonArgs.minute, daemonRc,
        static_cast<unsigned long long>(fetched.posixUtc));

    if (R_SUCCEEDED(daemonRc)) {
        if (!failureOnly) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                          "%s (%04u-%02u-%02u %02u:%02u)",
                          i18n.tr("settings.system.web_time_saved",
                                  "Clock synced from the web.").c_str(),
                          daemonArgs.year, daemonArgs.month, daemonArgs.day,
                          daemonArgs.hour, daemonArgs.minute);
            emitClockSyncToast(buf, 3.2f);
        } else {
            DebugLog::log("[settings-time] boot web sync ok (quiet)");
        }
    } else {
        char buf[128];
        std::snprintf(
            buf, sizeof(buf), "%s (0x%X)",
            i18n.tr("settings.system.time_change_failed",
                    "The date and time setting could not be changed.").c_str(),
            daemonRc);
        emitClockSyncToast(buf);
    }
}
