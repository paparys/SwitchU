#include "SidebarManager.hpp"
#include "ui/ChromeTokens.hpp"
#include "core/DebugLog.hpp"
#include "icons/IconPackManager.hpp"
#include "widgets/AdaptiveIconLoad.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Animation.hpp>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <array>
#include <algorithm>

namespace {

constexpr int kSidebarIconCount = 7;
constexpr float kSidebarButtonSize = 70.f;
constexpr float kSidebarColumnGap = 16.f;
constexpr float kSidebarButtonPadding = 6.f;
constexpr float kSidebarMarginX = 14.f;
constexpr float kDynamicStripButtonSize = 56.f;
constexpr float kDynamicStripGap = 10.f;
constexpr float kDynamicStripY = 17.f;
// The top strip sits a touch lower so the glyphs read centred against the
// profile / clock capsules instead of hugging the screen edge.
constexpr float kDynamicStripDownBias = 4.f;
constexpr float kDynamicLeftGroupX = 190.f;
constexpr float kDynamicRightGroupX = 902.f;
// Breathing room the plate keeps around the shortcut buttons.
constexpr float kStripPlatePad = 9.f;
// Extra chrome height on top of that pad, split evenly above and below the
// row. Buttons keep their size and sit on the bar's centre line.
constexpr float kStripBarGrowY = 17.f;
// Air kept between a shortcut stack and the tile band.
constexpr float kColumnClearGap = 10.f;
// Mirrors IconGrid::kLayoutMorphDuration so chrome and tiles land together.
constexpr float kSidebarGlideDuration = 0.40f;
// Tiny cascade across the buttons: reads as one gesture, not separate snaps.
constexpr float kSidebarGlideStagger = 0.022f;

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty())
        return name;
    if (base.back() == '/')
        return base + name;
    return base + "/" + name;
}

} // namespace


void SidebarManager::build(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                           const std::string& assetsBase,
                           const Actions& actions) {
    const float btnSize = kSidebarButtonSize * m_buttonScale;
    const float gap = kSidebarColumnGap * m_buttonScale;
    const float leftX = kSidebarMarginX;
    const float rightX = 1280.f - kSidebarMarginX - btnSize;
    // Both stacks use the same three-applet rhythm, centred vertically.
    const float leftTotalH = 4.f * btnSize + 3.f * gap;
    const float rightTotalH = 3.f * btnSize + 2.f * gap;
    const float leftStartY = 360.f - leftTotalH * 0.5f;
    const float rightStartY = 360.f - rightTotalH * 0.5f;

    if (!m_stripPlateWidget) {
        m_stripPlateWidget = std::make_shared<nxui::GlassPanel>();
        m_stripPlateWidget->setTag("shortcutStripPlate");
        m_stripPlateWidget->setBlurEnabled(false);
        // The strip is a flat, colour-tinted plate. A liquid-glass surface only
        // shaded the pill and asked for a full-screen backdrop capture every
        // frame; the panel's own rim still hugs the plate, and the themed base
        // alpha keeps the translucent layer.
        m_stripPlateWidget->setVisible(false);
    }
    // build() can run after the settings pass that stores the saved gradient.
    // Re-apply it on the widget so a freshly created plate cannot spend its
    // first frames (or its whole lifetime) on the flat fill.
    m_stripPlateWidget->setGradientFill(
        m_stripGradient, m_stripGradientA, m_stripGradientB);
    m_stripPlateWidget->setGradientPhase(m_stripGradientPhase);
    if (!m_searchBar) {
        m_searchBar = std::make_shared<GameSearchBar>();
        m_searchBar->setVisible(false);
        m_searchBar->setAccessibilityLabel(
            nxui::I18n::instance().tr("home.search_games", "Search games"));
        m_searchBar->setAccessibilityHint(
            nxui::I18n::instance().tr("home.search_hint",
                                      "A to search for a game."));
    }

    m_leftButtons.clear();
    m_rightButtons.clear();
    m_settingsButton = nullptr;
    m_allAppsButton  = nullptr;
    m_themeShopButton = nullptr;
    m_albumButton    = nullptr;
    m_buttonGlides.clear();
    m_layoutArranged = false;
    m_anims.clear();
    // Never clear this: every AppletButton stores &m_icons[i], so clearing
    // destroys the textures they point at and the next load writes new glyphs
    // into storage the buttons no longer reference. Size it once, in place.
    m_icons.reserve(kSidebarIconCount);
    if ((int)m_icons.size() != kSidebarIconCount)
        m_icons.resize(kSidebarIconCount);
    invalidateAssetsCache();

    auto makeBtn = [](nxui::Texture* tex, const std::string& labelKey,
                      const std::string& fallback, std::function<void()> action) {
        auto btn = std::make_shared<AppletButton>();
        btn->setIcon(tex);
        btn->setLabelKey(labelKey, fallback);
        btn->setAccessibilityLabel(fallback);
        btn->setAccessibilityRole(nxui::I18n::instance().tr("accessibility.roles.button", "button"));
        btn->setAccessibilityHint(nxui::I18n::instance().tr("accessibility.hints.open", "A to open."));
        btn->setOnActivate(std::move(action));
        btn->setFocusable(true);
        return btn;
    };

    {
        auto album = makeBtn(&m_icons[0], "sidebar.album", "Album", actions.onAlbum);
        m_albumButton = album.get();
        album->setRect({leftX, leftStartY + 0.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(album));

        auto miiEditor = makeBtn(&m_icons[1], "sidebar.mii_editor", "Mii Editor", actions.onMiiEditor);
        miiEditor->setRect({leftX, leftStartY + 1.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(miiEditor));

        auto allApps = makeBtn(&m_icons[6], "sidebar.all_apps", "All Apps", actions.onAllApps);
        m_allAppsButton = allApps.get();
        allApps->setRect({leftX, leftStartY + 2.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(allApps));

        auto settings = makeBtn(&m_icons[5], "sidebar.settings", "Settings", actions.onSettings);
        m_settingsButton = settings.get();
        settings->setRect({leftX, leftStartY + 3.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(settings));
    }

    {
        auto ctrl = makeBtn(&m_icons[2], "sidebar.controllers", "Controllers", actions.onControllers);
        ctrl->setRect({rightX, rightStartY + 0.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(ctrl));

        // Theme Shop middle / Power (Sleep) bottom — matches left stack rhythm
        // (utility → destination → system action).
        auto themeShop = makeBtn(&m_icons[4], "sidebar.theme_shop", "Theme Shop", actions.onMiiverse);
        m_themeShopButton = themeShop.get();
        themeShop->setRect({rightX, rightStartY + 1.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(themeShop));

        auto sleep = makeBtn(&m_icons[3], "sidebar.sleep", "Sleep", actions.onSleep);
        sleep->setRect({rightX, rightStartY + 2.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(sleep));
    }

    // Remember the stock applets so the pinned sockets below stay independent of
    // them: the applets are fixed chrome and are never repurposed.
    if ((int)m_gameIcons.size() != kPinnedSlotCount)
        m_gameIcons.resize(kPinnedSlotCount);

    // Pinned-game sockets. Created once and hidden until a title occupies them;
    // each points at its own texture in m_gameIcons, so a rebind never moves a
    // pointer the button is still drawing.
    m_pinButtons.clear();
    for (int i = 0; i < kPinnedSlotCount; ++i) {
        auto pin = makeBtn(&m_gameIcons[static_cast<std::size_t>(i)], "", "", nullptr);
        pin->setTag("pinnedShortcut" + std::to_string(i));
        pin->setVisible(false);
        m_pinButtons.push_back(std::move(pin));
    }
    m_visiblePins = 0;

    if (!m_leftButtons.empty()) {
        m_leftButtons.front()->setCustomNavigation(nxui::FocusDirection::UP, m_leftButtons.front().get());
        m_leftButtons.back()->setCustomNavigation(nxui::FocusDirection::DOWN, m_leftButtons.back().get());
    }
    if (!m_rightButtons.empty()) {
        m_rightButtons.front()->setCustomNavigation(nxui::FocusDirection::UP, m_rightButtons.front().get());
        m_rightButtons.back()->setCustomNavigation(nxui::FocusDirection::DOWN, m_rightButtons.back().get());
    }

    applyChromeToButtons();
    applyPaddingToButtons();

    (void)gpu;
    (void)ren;
    (void)assetsBase;
}

void SidebarManager::glideButtonTo(AppletButton* button, const nxui::Rect& target,
                                   float delay) {
    if (!button)
        return;
    const nxui::Rect current = button->rect();
    const auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    if (near(current.x, target.x) && near(current.y, target.y) &&
        near(current.width, target.width) && near(current.height, target.height)) {
        button->setRect(target);
        for (auto& glide : m_buttonGlides)
            if (glide.button == button)
                glide.active = false;
        return;
    }

    ButtonGlide* slot = nullptr;
    for (auto& glide : m_buttonGlides)
        if (glide.button == button)
            slot = &glide;
    if (!slot) {
        m_buttonGlides.push_back(ButtonGlide{});
        slot = &m_buttonGlides.back();
    }
    // Retarget from the in-flight rect so a flip interrupted mid-glide keeps
    // travelling instead of jumping back to the previous layout.
    slot->button  = button;
    slot->from    = current;
    slot->to      = target;
    slot->elapsed = 0.f;
    slot->delay   = delay;
    slot->active  = true;
}

void SidebarManager::updateButtonGlides(float dt) {
    for (auto& glide : m_buttonGlides) {
        if (!glide.active || !glide.button)
            continue;
        glide.elapsed += dt;
        const float t = (glide.elapsed - glide.delay) / kSidebarGlideDuration;
        if (t <= 0.f)
            continue;
        if (t >= 1.f) {
            glide.button->setRect(glide.to);
            glide.active = false;
            continue;
        }
        glide.button->setRect(nxui::Rect::lerp(glide.from, glide.to,
                                               nxui::Easing::inOutCubic(t)));
    }
}

void SidebarManager::setDynamicLineLayout(bool enabled) {
    std::vector<AppletButton*> ordered;
    ordered.reserve(m_leftButtons.size() + m_rightButtons.size());
    for (auto& button : m_leftButtons) ordered.push_back(button.get());
    for (auto& button : m_rightButtons) ordered.push_back(button.get());
    if (ordered.empty()) return;

    for (auto* button : ordered) {
        button->setCustomNavigation(nxui::FocusDirection::LEFT, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::RIGHT, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::UP, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
    }
    // Pinned sockets are not part of the strip's ordered applet list, but they
    // share the same side row in the grid layouts. Clear their old strip/column
    // links too before the new row is wired.
    for (auto& button : m_pinButtons) {
        if (!button) continue;
        button->setCustomNavigation(nxui::FocusDirection::LEFT, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::RIGHT, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::UP, nullptr);
        button->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
    }

    m_dynamicLine = enabled;
    // Grid and strip need opposite button chrome (pills vs. bare glyphs).
    applyChromeToButtons();
    // Boot snaps to the configured layout; later flips glide into place.
    const bool animate = m_layoutArranged;
    m_layoutArranged = true;
    const auto place = [&](AppletButton* button, const nxui::Rect& target, int order) {
        if (!animate) {
            button->setRect(target);
            return;
        }
        glideButtonTo(button, target,
                      static_cast<float>(order) * kSidebarGlideStagger);
    };

    if (enabled) {
        const float stripSize = kDynamicStripButtonSize * m_buttonScale;
        const float stripGap  = kDynamicStripGap * m_buttonScale;
        // Clamped inside the frame so a large nudge cannot push the row off
        // the screen at either edge.
        float stripY = std::clamp(
            (m_stripEdge == 0)
                ? kDynamicStripY + kDynamicStripDownBias + m_stripNudgeY
                : 720.f - stripSize - kDynamicStripY + m_stripNudgeY,
            4.f, 716.f - stripSize);
        // Merged with the capsules, the row lives inside that band: a nudge
        // slides the shortcuts within the container instead of growing it.
        if (m_anchorValid) {
            const float bandTop = std::min(m_anchorProfile.y, m_anchorBattery.y);
            const float bandH = std::max(m_anchorProfile.height,
                                         m_anchorBattery.height);
            stripY = std::clamp(stripY, bandTop,
                                std::max(bandTop, bandTop + bandH - stripSize));
        }
        // The search shortcut is a normal icon in the strip. It owns the first
        // socket of the left group, so the applets shift one place right and the
        // group span / pin clearance include the extra socket.
        const float leftSlotCount = static_cast<float>(m_leftButtons.size())
            + (m_searchBar ? 1.f : 0.f);
        const float leftGroupSpan = leftSlotCount * stripSize
            + (leftSlotCount > 0.f ? leftSlotCount - 1.f : 0.f) * stripGap;
        const float rightGroupSpan = static_cast<float>(m_rightButtons.size()) * stripSize
            + static_cast<float>(m_rightButtons.size() > 0 ? m_rightButtons.size() - 1 : 0) * stripGap;
        const float leftClusterSpan = leftGroupSpan;
        const float rightClusterSpan = rightGroupSpan;
        const float pinStep = stripSize + stripGap;

        // Collect the pins before placing the applets: how many sit on a side
        // decides how far that side's stock cluster has to move out.
        std::vector<AppletButton*> leftPins;
        std::vector<AppletButton*> rightPins;
        for (int slot = 0; slot < kPinnedSlotCount; ++slot) {
            if (slot >= static_cast<int>(m_pinButtons.size()))
                break;
            auto& btn = m_pinButtons[static_cast<std::size_t>(slot)];
            if (!btn || !btn->isVisible())
                continue;
            (gameShortcutSide(slot) == 0 ? leftPins : rightPins).push_back(btn.get());
        }
        m_visiblePins = static_cast<int>(leftPins.size() + rightPins.size());

        auto groupStart = [&](bool rightGroup, float groupSpan) {
            const float baseX = rightGroup ? kDynamicRightGroupX : kDynamicLeftGroupX;
            const float lowShift = 8.f - baseX;
            const float highShift = 1272.f - (baseX + groupSpan);
            const float shift = rightGroup ? m_stripNudgeRight : m_stripNudgeX;
            return baseX + std::clamp(shift,
                                      std::min(lowShift, highShift),
                                      std::max(lowShift, highShift));
        };
        float leftGroupX = groupStart(false, leftGroupSpan);
        float rightGroupX = groupStart(true, rightGroupSpan);
        // While the band is merged the capsules are part of this row. The
        // profile tile and the battery pill are different widths, so clamping
        // each cluster against its own capsule pushed only the battery side
        // inward and left the left icons farther from the centre than the
        // right ones. Mirror the pair instead: both clusters keep the same
        // clearance from their capsule.
        if (m_anchorValid) {
            const float leftInset = std::max(
                leftGroupX, m_anchorProfile.right() + kColumnClearGap);
            const float rightInset = std::max(
                1280.f - (rightGroupX + rightGroupSpan),
                1280.f - (m_anchorBattery.x - kColumnClearGap));
            const float inset = std::max(leftInset, rightInset);
            leftGroupX = inset;
            rightGroupX = 1280.f - inset - rightGroupSpan;
        }
        leftGroupX = std::clamp(leftGroupX, 8.f, 1272.f - leftGroupSpan);
        rightGroupX = std::clamp(rightGroupX, 8.f, 1272.f - rightGroupSpan);

        // Adding a pin must move that side's applets out of the way instead
        // of growing the pin row into the clock. Start with the normal gap and
        // tighten it only when a full three-pin side would otherwise leave the
        // clock no room. The tight fallback still keeps every button outside
        // the clock; it may sit closer to the outer capsule.
        struct SideFit {
            float x = 0.f;
            float gap = kColumnClearGap;
            bool fits = false;
        };
        const auto fitSide = [&](bool rightSide) {
            const auto& pins = rightSide ? rightPins : leftPins;
            const float groupSpan = rightSide ? rightGroupSpan : leftGroupSpan;
            const float clusterSpan = rightSide ? rightClusterSpan : leftClusterSpan;
            const float pinSpan = pins.empty()
                ? 0.f
                : (static_cast<float>(pins.size()) - 1.f) * pinStep + stripSize;
            const float baseX = rightSide ? rightGroupX : leftGroupX;
            SideFit fit;
            const float gaps[] = {kColumnClearGap, 5.f, 2.f};
            for (float gap : gaps) {
                float minX = 8.f;
                float maxX = 1272.f - groupSpan;
                if (m_anchorValid) {
                    if (rightSide)
                        maxX = std::min(maxX,
                            m_anchorBattery.x - gap - groupSpan);
                    else
                        minX = std::max(minX,
                            m_anchorProfile.right() + gap);
                }
                if (m_anchorClockValid) {
                    const float pinGap = pins.empty() ? 0.f : gap;
                    if (rightSide)
                        minX = std::max(minX,
                            m_anchorClock.right() + gap + pinSpan + pinGap);
                    else
                        maxX = std::min(maxX,
                            m_anchorClock.x - gap - pinSpan - pinGap
                            - clusterSpan);
                }
                if (minX <= maxX) {
                    fit.x = std::clamp(baseX, minX, maxX);
                    fit.gap = gap;
                    fit.fits = true;
                    return fit;
                }
            }

            // The frame cannot hold both clusters at this size. Keep the
            // clock clear and let the row crowd its outer capsule; covering
            // the time is the one overlap this layout must avoid.
            float minX = 8.f;
            float maxX = 1272.f - groupSpan;
            if (m_anchorClockValid) {
                const float pinGap = pins.empty() ? 0.f : 2.f;
                if (rightSide)
                    minX = std::max(minX,
                        m_anchorClock.right() + 2.f + pinSpan + pinGap);
                else
                    maxX = std::min(maxX,
                        m_anchorClock.x - 2.f - pinSpan - pinGap
                        - clusterSpan);
            }
            if (minX > maxX) {
                if (m_anchorClockValid) {
                    fit.x = std::clamp(rightSide ? minX : maxX,
                                       8.f, 1272.f - groupSpan);
                    fit.gap = 2.f;
                    return fit;
                }
                minX = maxX = std::clamp(baseX, 8.f, 1272.f - groupSpan);
            }
            fit.x = std::clamp(baseX, minX, maxX);
            fit.gap = 2.f;
            return fit;
        };
        SideFit leftFit = fitSide(false);
        SideFit rightFit = fitSide(true);
        // One side can be asked to hold more pins than its clock clearance
        // allows (the generic Add row keeps using the last chosen side). Spill
        // the excess onto the other side instead of drawing it through the
        // clock. This only moves what does not fit; a normal 3/3 split stays
        // exactly where the player put it.
        for (int guard = 0; guard < kPinnedSlotCount; ++guard) {
            if (leftFit.fits && rightFit.fits)
                break;
            if (!leftFit.fits && !rightFit.fits)
                break;
            if (!rightFit.fits && !rightPins.empty()) {
                leftPins.push_back(rightPins.front());
                rightPins.erase(rightPins.begin());
            } else if (!leftFit.fits && !leftPins.empty()) {
                rightPins.insert(rightPins.begin(), leftPins.back());
                leftPins.pop_back();
            } else {
                break;
            }
            leftFit = fitSide(false);
            rightFit = fitSide(true);
        }
        leftGroupX = leftFit.x;
        rightGroupX = rightFit.x;

        // Place search only after fitSide() has settled the group origin. That
        // pass can move the left cluster to clear the clock and pins; placing
        // search first left it at the old x while the applets moved onto it.
        if (m_searchBar) {
            m_searchBar->setRect({leftGroupX, stripY, stripSize, stripSize});
            m_searchBar->setVisible(true);
        }

        for (std::size_t i = 0; i < ordered.size(); ++i) {
            auto* button = ordered[i];
            const bool rightGroup = i >= m_leftButtons.size();
            const std::size_t groupIndex = rightGroup ? i - m_leftButtons.size() : i;
            const float groupX = rightGroup ? rightGroupX : leftGroupX;
            const float slotIndex = static_cast<float>(groupIndex)
                + (!rightGroup && m_searchBar ? 1.f : 0.f);
            place(button,
                  {groupX + slotIndex * (stripSize + stripGap),
                   stripY,
                   stripSize,
                   stripSize},
                  static_cast<int>(groupIndex));
            // The ends of the strip hop into the HUD instead of self-looping,
            // so LEFT reaches the profile and RIGHT reaches the battery.
            nxui::Widget* leftTarget = (i == 0)
                ? (m_hudLeftTarget ? m_hudLeftTarget
                                   : static_cast<nxui::Widget*>(button))
                : ordered[i - 1];
            nxui::Widget* rightTarget = (i + 1 == ordered.size())
                ? (m_hudRightTarget ? m_hudRightTarget
                                    : static_cast<nxui::Widget*>(button))
                : ordered[i + 1];
            button->setCustomNavigation(nxui::FocusDirection::LEFT, leftTarget);
            button->setCustomNavigation(nxui::FocusDirection::RIGHT, rightTarget);
        }
        // Pinned games continue the applet row on the side they were added to.
        // Left pins grow rightwards from the left group; right pins grow
        // leftwards from the right group. They never take an applet socket.
        const auto placePins = [&](const std::vector<AppletButton*>& group,
                                   bool rightSide, float gap, float clusterSpan) {
            if (group.empty())
                return;
            const float span = (static_cast<float>(group.size()) - 1.f) * pinStep
                             + stripSize;
            float x = rightSide
                ? rightGroupX - gap - span
                : leftGroupX + clusterSpan + gap;
            x = std::clamp(x, 8.f, std::max(8.f, 1272.f - span));
            for (std::size_t i = 0; i < group.size(); ++i) {
                place(group[i],
                      {x + static_cast<float>(i) * pinStep,
                       stripY, stripSize, stripSize},
                      static_cast<int>(i));
            }
        };
        placePins(leftPins, false, leftFit.gap, leftClusterSpan);
        placePins(rightPins, true, rightFit.gap, rightClusterSpan);
        // One horizontal chain across the whole row: search -> left applets ->
        // left pins -> right pins -> right applets. Ends hop into the HUD
        // capsules. Search is a real shortcut slot, not a detached field.
        std::vector<nxui::Widget*> chain;
        chain.reserve((m_searchBar ? 1u : 0u) + m_leftButtons.size()
                      + leftPins.size() + rightPins.size()
                      + m_rightButtons.size());
        if (m_searchBar)
            chain.push_back(m_searchBar.get());
        for (auto& b : m_leftButtons) chain.push_back(b.get());
        for (auto* b : leftPins) chain.push_back(b);
        for (auto* b : rightPins) chain.push_back(b);
        for (auto& b : m_rightButtons) chain.push_back(b.get());
        for (std::size_t i = 0; i < chain.size(); ++i) {
            nxui::Widget* left = (i == 0)
                ? (m_hudLeftTarget ? m_hudLeftTarget : chain[i])
                : chain[i - 1];
            nxui::Widget* right = (i + 1 == chain.size())
                ? (m_hudRightTarget ? m_hudRightTarget : chain[i])
                : chain[i + 1];
            chain[i]->setCustomNavigation(nxui::FocusDirection::LEFT, left);
            chain[i]->setCustomNavigation(nxui::FocusDirection::RIGHT, right);
        }
        syncStripPlateGeometry();
        return;
    }

    if (m_searchBar)
        m_searchBar->setVisible(false);

    const float colSize = kSidebarButtonSize * m_buttonScale;
    const float colGap  = kSidebarColumnGap * m_buttonScale;
    std::vector<AppletButton*> leftPins;
    std::vector<AppletButton*> rightPins;
    for (int slot = 0; slot < kPinnedSlotCount; ++slot) {
        if (slot >= static_cast<int>(m_pinButtons.size()))
            break;
        auto& btn = m_pinButtons[static_cast<std::size_t>(slot)];
        if (!btn || !btn->isVisible())
            continue;
        (gameShortcutSide(slot) == 0 ? leftPins : rightPins).push_back(btn.get());
    }
    m_visiblePins = static_cast<int>(leftPins.size() + rightPins.size());

    // One row per side: the stock applets followed by that side's pinned
    // games. Keeping them in a single column means the nudge slider moves the
    // whole side row, and there is no second column to overlap the first.
    std::vector<AppletButton*> leftRow;
    std::vector<AppletButton*> rightRow;
    leftRow.reserve(m_leftButtons.size() + leftPins.size());
    rightRow.reserve(m_rightButtons.size() + rightPins.size());
    for (auto& button : m_leftButtons)  leftRow.push_back(button.get());
    for (auto* button : leftPins)       leftRow.push_back(button);
    for (auto& button : m_rightButtons) rightRow.push_back(button.get());
    for (auto* button : rightPins)      rightRow.push_back(button);

    // Both sides share one button size so the rows read as a pair. Only when
    // the longest row cannot fit between the HUD and the bottom edge do the
    // gap and then the icons tighten; the row stays fully on-screen.
    constexpr float kSideRowTop = 96.f;
    constexpr float kSideRowBottom = 712.f;
    constexpr float kSideRowMinGap = 4.f;
    const float sideRowAvailH = std::max(1.f, kSideRowBottom - kSideRowTop);
    const std::size_t longestRow = std::max(leftRow.size(), rightRow.size());
    float sideSize = colSize;
    float sideGap = colGap;
    const auto rowHeight = [&](std::size_t count) {
        return static_cast<float>(count) * sideSize
            + static_cast<float>(count > 0 ? count - 1 : 0) * sideGap;
    };
    float longestH = rowHeight(longestRow);
    if (longestRow > 1 && longestH > sideRowAvailH) {
        sideGap = std::max(kSideRowMinGap,
            (sideRowAvailH - static_cast<float>(longestRow) * sideSize)
                / static_cast<float>(longestRow - 1));
        longestH = rowHeight(longestRow);
        if (longestH > sideRowAvailH) {
            sideSize = std::max(32.f,
                (sideRowAvailH - static_cast<float>(longestRow - 1) * kSideRowMinGap)
                    / static_cast<float>(longestRow));
            sideGap = kSideRowMinGap;
            longestH = rowHeight(longestRow);
        }
    }
    const auto rowStartY = [&](std::size_t count) {
        const float totalH = rowHeight(count);
        return std::clamp(360.f - totalH * 0.5f,
                          kSideRowTop,
                          std::max(kSideRowTop, kSideRowBottom - totalH));
    };

    const float rightX = 1280.f - kSidebarMarginX - sideSize;
    const float leftShift = std::clamp(m_stripNudgeX,
        8.f - kSidebarMarginX, 1272.f - sideSize - kSidebarMarginX);
    const float rightShift = std::clamp(m_stripNudgeRight,
        8.f - rightX, 1272.f - sideSize - rightX);
    // The nudge sliders move each whole side row. Inward travel stops before
    // the tile band, so the row remains inside the reserved side gutter.
    const float leftMax = std::max(8.f, m_clearLeft - kColumnClearGap - sideSize);
    const float leftColX = std::clamp(kSidebarMarginX + leftShift, 8.f, leftMax);
    const float rightMin = std::min(1272.f - sideSize, m_clearRight + kColumnClearGap);
    const float rightColX = std::clamp(rightX + rightShift,
                                       std::max(8.f, rightMin), 1272.f - sideSize);

    const auto placeRow = [&](const std::vector<AppletButton*>& row, float x) {
        const float startY = rowStartY(row.size());
        for (std::size_t i = 0; i < row.size(); ++i) {
            place(row[i],
                  {x,
                   startY + static_cast<float>(i) * (sideSize + sideGap),
                   sideSize, sideSize},
                  static_cast<int>(i));
        }
        // One chain down each side. LEFT/RIGHT stay unbound here so the focus
        // engine can step from the row into the grid spatially.
        for (std::size_t i = 0; i < row.size(); ++i) {
            const std::size_t up = (i == 0) ? i : i - 1;
            const std::size_t down = (i + 1 == row.size()) ? i : i + 1;
            row[i]->setCustomNavigation(nxui::FocusDirection::UP,
                static_cast<nxui::Widget*>(row[up]));
            row[i]->setCustomNavigation(nxui::FocusDirection::DOWN,
                static_cast<nxui::Widget*>(row[down]));
        }
    };
    placeRow(leftRow, leftColX);
    placeRow(rightRow, rightColX);
}

float SidebarManager::sideGutterWidth(int buttonScalePercent) {
    const float scale = static_cast<float>(
        std::clamp(buttonScalePercent, 70, 150)) / 100.f;
    const float colSize = kSidebarButtonSize * scale;
    // Outer margin + the single side row + air before the tiles.
    return kSidebarMarginX + colSize + kColumnClearGap;
}

void SidebarManager::setDynamicLineDownAction(std::function<void()> action) {
    auto configure = [&action](nxui::Widget* button) {
        if (!button)
            return;
        button->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
        button->removeAction(static_cast<uint64_t>(nxui::Button::DDown));
        button->removeAction(static_cast<uint64_t>(nxui::Button::LStickD));
        button->removeAction(static_cast<uint64_t>(nxui::Button::RStickD));
        if (action)
            button->addDirectionAction(nxui::FocusDirection::DOWN, action);
    };
    for (auto& button : m_leftButtons) configure(button.get());
    for (auto& button : m_rightButtons) configure(button.get());
    configure(m_searchBar.get());
}

void SidebarManager::setDynamicLineUpTarget(nxui::Widget* target) {
    for (auto& button : m_leftButtons)
        button->setCustomNavigation(nxui::FocusDirection::UP, target);
    for (auto& button : m_rightButtons)
        button->setCustomNavigation(nxui::FocusDirection::UP, target);
    if (m_searchBar)
        m_searchBar->setCustomNavigation(nxui::FocusDirection::UP, target);
}

void SidebarManager::setDynamicLineProfileTarget(nxui::Widget* profile) {
    // Stored, because every re-place (size, edge, paddle) rebuilds the strip's
    // neighbours and used to drop the hop into the profile tile.
    m_hudLeftTarget = profile;
    if (m_leftButtons.empty())
        return;
    // The HOME profile tile sits left of the dynamic-line applet strip.
    m_leftButtons.front()->setCustomNavigation(nxui::FocusDirection::LEFT, profile);
}

void SidebarManager::setDynamicLineBatteryTarget(nxui::Widget* battery) {
    m_hudRightTarget = battery;
    if (m_rightButtons.empty())
        return;
    m_rightButtons.back()->setCustomNavigation(nxui::FocusDirection::RIGHT, battery);
}

void SidebarManager::setHudAnchors(const nxui::Rect& profile,
                                   const nxui::Rect& clock,
                                   const nxui::Rect& battery) {
    const auto same = [](const nxui::Rect& a, const nxui::Rect& b) {
        return std::abs(a.x - b.x) < 0.5f && std::abs(a.y - b.y) < 0.5f
            && std::abs(a.width - b.width) < 0.5f
            && std::abs(a.height - b.height) < 0.5f;
    };
    const bool valid = profile.width > 0.f && battery.width > 0.f;
    const bool clockValid = clock.width > 0.f && clock.height > 0.f;
    if (valid == m_anchorValid && clockValid == m_anchorClockValid
        && same(profile, m_anchorProfile) && same(clock, m_anchorClock)
        && same(battery, m_anchorBattery))
        return;
    m_anchorProfile = profile;
    m_anchorClock = clock;
    m_anchorBattery = battery;
    m_anchorValid = valid;
    m_anchorClockValid = clockValid;
    // The clock can be nudged or resized independently of the strip. Re-place
    // the row when its rect moves so the clearance is refreshed in the same
    // frame instead of waiting for the next unrelated layout event.
    if (m_dynamicLine && (!m_leftButtons.empty() || !m_rightButtons.empty()))
        setDynamicLineLayout(true);
    else
        syncStripPlateGeometry();
}

void SidebarManager::reloadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                  const std::string& assetsBase,
                                  const std::string& customIconsBase,
                                  const std::string& iconPackId) {
    if (m_leftButtons.empty() || m_rightButtons.empty())
        return;

    const std::string packId = iconPackId.empty()
        ? switchu::icons::kDefaultPackId : iconPackId;

    if (m_assetsLoaded
        && m_loadedAssetsBase == assetsBase
        && m_loadedCustomIconsBase == customIconsBase
        && m_loadedIconPackId == packId) {
        DebugLog::log("[sidebar-anim] reload skipped: assets unchanged (custom=%s pack=%s)",
                      customIconsBase.empty() ? "<empty>" : customIconsBase.c_str(),
                      packId.c_str());
        return;
    }

    loadAssets(gpu, ren, assetsBase, customIconsBase, packId);
}

void SidebarManager::invalidateAssetsCache() {
    m_loadedAssetsBase.clear();
    m_loadedCustomIconsBase.clear();
    m_loadedIconPackId.clear();
    m_assetsLoaded = false;
}

void SidebarManager::loadAssets(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                const std::string& assetsBase,
                                const std::string& customIconsBase,
                                const std::string& iconPackId) {
    gpu.waitIdle();

    // Pack glyphs live under icons/<pack>/ with per-file fallback to icons/default.
    // Theme customIconsBase still wins when present.
    const std::string packId = iconPackId.empty()
        ? switchu::icons::kDefaultPackId : iconPackId;
    const bool useCustomStaticIcons = !customIconsBase.empty();
    auto packAssetPath = [&](const char* fileName) {
        return switchu::icons::resolveGlyphPath(assetsBase, packId, fileName);
    };
    auto resolveAsset = [&](const char* fileName) {
        if (!customIconsBase.empty()) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath))
                return customPath;
        }
        return packAssetPath(fileName);
    };
    // Parallel to iconFiles / button order: true when the loaded glyph is mono.
    std::array<bool, kSidebarIconCount> iconAdaptive{};
    auto loadIconTexture = [&](int iconIdx, const char* fileName) {
        auto tryPath = [&](const std::string& path) -> bool {
            bool adaptive = false;
            float meanLuma = 0.5f;
            if (!switchu::icons::loadMaybeAdaptiveIcon(
                    m_icons[iconIdx], gpu, ren, path, &adaptive, 128,
                    &meanLuma, &m_iconContrastMasks[iconIdx],
                    m_hslAdjust.enabled ? &m_hslAdjust : nullptr))
                return false;
            iconAdaptive[static_cast<std::size_t>(iconIdx)] = adaptive;
            m_iconMeanLumas[static_cast<std::size_t>(iconIdx)] = meanLuma;
            return true;
        };

        if (useCustomStaticIcons) {
            std::string customPath = joinPath(customIconsBase, fileName);
            if (pathExists(customPath)) {
                if (tryPath(customPath))
                    return;
                DebugLog::log("[sidebar-assets] custom icon load failed, falling back: %s",
                              customPath.c_str());
            }
        }

        const std::string fallbackPath = packAssetPath(fileName);
        if (!fallbackPath.empty() && tryPath(fallbackPath))
            return;
        iconAdaptive[static_cast<std::size_t>(iconIdx)] = false;
        m_iconMeanLumas[static_cast<std::size_t>(iconIdx)] = 0.5f;
        DebugLog::log("[sidebar-assets] pack icon load failed: pack=%s file=%s path=%s",
                      packId.c_str(),
                      fileName,
                      fallbackPath.empty() ? "<empty>" : fallbackPath.c_str());
    };

    static const char* iconFiles[] = {
        "album.png", "mii_editor.png", "controller.png", "power.png", "themes.png", "settings.png",
        "all_apps.png",
    };
    // Resize in place, never clear: these are handed to the buttons as raw
    // pointers, and reallocating them here is what left the glyphs attached to
    // the wrong shortcuts after a restart.
    m_icons.reserve(kSidebarIconCount);
    if ((int)m_icons.size() != kSidebarIconCount)
        m_icons.resize(kSidebarIconCount);
    m_iconContrastMasks.reserve(kSidebarIconCount);
    if ((int)m_iconContrastMasks.size() != kSidebarIconCount)
        m_iconContrastMasks.resize(kSidebarIconCount);
    for (int i = 0; i < kSidebarIconCount; ++i)
        m_iconContrastMasks[static_cast<std::size_t>(i)] = nxui::Texture{};
    m_iconMeanLumas.assign(kSidebarIconCount, 0.5f);
    for (int i = 0; i < kSidebarIconCount; ++i)
        loadIconTexture(i, iconFiles[i]);

    static const struct { int iconIdx; const char* webpFile; bool useFirstFrame; } animDefs[] = {
        { 0, "album.webp",      false },
        { 1, "mii_editor.webp", false },
        { 2, "controller.webp", true  },
        { 3, "power.webp",      false },
        { 4, "themes.webp",     false },
        { 5, "settings.webp",   false },
    };

    // Hand every button back to its static glyph *before* the animation entries
    // are destroyed. A button showing a WebP frame holds a raw pointer into that
    // entry's frame storage; clearing the entries first freed the texture and
    // left the pointer naming whatever the allocator reused next. That is how a
    // shortcut ended up wearing another shortcut's icon after a restart.
    if (m_leftButtons.size() == 4 && m_rightButtons.size() == 3) {
        AppletButton* byGlyph[kSidebarIconCount] = {
            m_leftButtons[0].get(),   // album
            m_leftButtons[1].get(),   // mii editor
            m_rightButtons[0].get(),  // controllers
            m_rightButtons[2].get(),  // power -> Sleep
            m_rightButtons[1].get(),  // themes -> Theme Shop
            m_leftButtons[3].get(),   // settings
            m_leftButtons[2].get(),   // all apps
        };
        for (int i = 0; i < kSidebarIconCount; ++i) {
            if (!byGlyph[i])
                continue;
            byGlyph[i]->setIcon(&m_icons[static_cast<std::size_t>(i)]);
            byGlyph[i]->setIconUsesColorArtwork(false);
        }
    }
    m_anims.clear();
    if (useCustomStaticIcons) {
        DebugLog::log("[sidebar-anim] custom theme icons use PNG only; skipping WebP animations (%s)",
                      customIconsBase.c_str());
    }

    for (const auto& def : animDefs) {
        AppletButton* btn = nullptr;
        // Map by glyph meaning (not old slot order): Theme Shop is middle,
        // Sleep/Power is bottom on the right stack.
        if (def.iconIdx == 0) btn = m_leftButtons[0].get();
        else if (def.iconIdx == 1) btn = m_leftButtons[1].get();
        else if (def.iconIdx == 2) btn = m_rightButtons[0].get();
        else if (def.iconIdx == 3) btn = m_rightButtons[2].get(); // power → Sleep
        else if (def.iconIdx == 4) btn = m_rightButtons[1].get(); // themes → Theme Shop
        else if (def.iconIdx == 5) btn = m_leftButtons[3].get();
        if (!btn)
            continue;

        nxui::Texture* staticTex = &m_icons[def.iconIdx];
        const std::size_t idx = static_cast<std::size_t>(def.iconIdx);
        btn->setIconAdaptive(iconAdaptive[idx]);
        btn->setIconMeanLuma(m_iconMeanLumas[idx]);
        btn->setIconContrastMask(
            (!iconAdaptive[idx] && m_iconContrastMasks[idx].valid())
                ? &m_iconContrastMasks[idx] : nullptr);
        btn->setColourContrastAdapt(m_colourContrastAdapt);
        btn->setIconCustomTintEnabled(m_hslAdjust.enabled);
        btn->setIconUsesColorArtwork(false);
        if (useCustomStaticIcons) {
            btn->setIcon(staticTex);
            continue;
        }

        if (!def.webpFile) {
            btn->setIcon(staticTex);
            continue;
        }
        nxui::Texture* idleTex = def.useFirstFrame ? nullptr : staticTex;
        tryLoadAnimation(gpu, ren, resolveAsset(def.webpFile), btn, idleTex);
        btn->setIcon(idleTex ? idleTex : staticTex);
    }

    // Boot trace: one line per shortcut naming the glyph it resolved, the
    // button it was bound to and the texture object behind it. If the icons
    // ever cross over, the log shows whether the file resolution or the
    // pointer binding is the one at fault.
    if (m_leftButtons.size() == 4 && m_rightButtons.size() == 3) {
        static const char* kTraceFiles[kSidebarIconCount] = {
            "album.png", "mii_editor.png", "controller.png",
            "power.png", "themes.png", "settings.png", "all_apps.png",
        };
        AppletButton* kTraceBtn[kSidebarIconCount] = {
            m_leftButtons[0].get(), m_leftButtons[1].get(),
            m_rightButtons[0].get(), m_rightButtons[2].get(),
            m_rightButtons[1].get(), m_leftButtons[3].get(),
            m_leftButtons[2].get(),
        };
        for (int i = 0; i < kSidebarIconCount; ++i) {
            AppletButton* btn = kTraceBtn[i];
            const std::string resolved = resolveAsset(kTraceFiles[i]);
            DebugLog::log("[sidebar-bind] glyph=%s path=%s button=%p tex=%p label=%s",
                          kTraceFiles[i],
                          resolved.empty() ? "<none>" : resolved.c_str(),
                          static_cast<void*>(btn),
                          static_cast<void*>(btn ? btn->icon() : nullptr),
                          btn ? btn->label().c_str() : "<none>");
        }
    }

    m_loadedAssetsBase = assetsBase;
    m_loadedCustomIconsBase = customIconsBase;
    m_loadedIconPackId = packId;
    m_assetsLoaded = true;
}

void SidebarManager::tryLoadAnimation(nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                      const std::string& webpPath,
                                      AppletButton* button,
                                      nxui::Texture* staticIcon) {
    AnimEntry entry;
    entry.button    = button;
    entry.staticTex = staticIcon;
    if (entry.anim.load(gpu, ren, webpPath)) {
        m_anims.push_back(std::move(entry));
    }
}

void SidebarManager::update(float dt, nxui::Widget* focusedWidget) {
    updateButtonGlides(dt);
    for (auto& e : m_anims) {
        if (!e.button) continue;
        bool focused = (focusedWidget == e.button);
        e.anim.update(dt, focused);
        nxui::Texture* next = nullptr;
        bool colorArtwork = false;
        if (focused && e.anim.hasFrames()) {
            next = e.anim.currentFrame();
            // Animated WebP frames keep their own colour — no chrome tint.
            colorArtwork = true;
        } else {
            // staticTex == nullptr: use frame 0 of the animation as idle
            next = e.staticTex ? e.staticTex
                               : (e.anim.hasFrames() ? e.anim.currentFrame() : nullptr);
            colorArtwork = e.staticTex == nullptr && e.anim.hasFrames();
        }
        // Most frames show the same static glyph on five unfocused buttons.
        // Only touch widget state when the displayed texture actually changes.
        if (next != e.appliedTex || colorArtwork != e.appliedColorArtwork) {
            e.button->setIcon(next);
            e.button->setIconUsesColorArtwork(colorArtwork);
            e.appliedTex = next;
            e.appliedColorArtwork = colorArtwork;
        }
    }
    if (m_stripGradient && m_stripPlateWidget) {
        // Keep the drift running through layout transitions. Waiting for the
        // plate to become visible froze the phase at boot and made the first
        // frames after HOME appeared look like a static fill.
        m_stripGradientPhase = std::fmod(m_stripGradientPhase + dt * 0.12f, 1.f);
        m_stripPlateWidget->setGradientPhase(m_stripGradientPhase);
    }
    syncStripPlateGeometry();
}

void SidebarManager::applyTheme(const nxui::Theme& theme) {
    // Mono glyphs follow textPrimary (white on dark chrome / black on light,
    // including hero-aware flips). Colour replacements keep identity tint
    // unless colour-contrast adapt forces a black/white silhouette.
    m_btnBase      = theme.iconDefault;
    m_btnBorder    = theme.panelBorder;
    m_btnHighlight = theme.panelHighlight;
    if (m_searchBar) {
        m_searchBar->setPanelColor(theme.panelBase);
        m_searchBar->setBorderColor(theme.panelBorder);
        m_searchBar->setTextColor(theme.textPrimary);
        m_searchBar->setMutedColor(theme.textSecondary);
    }
    const nxui::Color iconTint = theme.textPrimary.withAlpha(0.92f);
    auto apply = [&](std::shared_ptr<AppletButton>& btn) {
        if (!btn) return;
        btn->setIconChromeTint(iconTint);
        btn->setColourContrastAdapt(m_colourContrastAdapt);
        btn->setIconCustomTintEnabled(m_hslAdjust.enabled);
    };
    for (auto& btn : m_leftButtons)  apply(btn);
    for (auto& btn : m_rightButtons) apply(btn);
    for (auto& btn : m_pinButtons)   apply(btn);
    // Chrome colours live in the members now, so the plate and the buttons
    // always pick up the same theme in one pass.
    applyChromeToButtons();
}

void SidebarManager::setColourIconContrast(bool enabled) {
    m_colourContrastAdapt = enabled;
    auto apply = [&](std::shared_ptr<AppletButton>& btn) {
        if (!btn) return;
        btn->setColourContrastAdapt(enabled);
        btn->setIconCustomTintEnabled(m_hslAdjust.enabled);
    };
    for (auto& btn : m_leftButtons)  apply(btn);
    for (auto& btn : m_rightButtons) apply(btn);
    for (auto& btn : m_pinButtons)   apply(btn);
}

void SidebarManager::setIconHslAdjust(const switchu::icons::HslAdjust& adj) {
    m_hslAdjust = adj;
}

void SidebarManager::setButtonScale(int percent) {
    const float scale = static_cast<float>(std::clamp(percent, 70, 150)) / 100.f;
    if (std::abs(scale - m_buttonScale) < 0.001f)
        return;
    m_buttonScale = scale;
    applyPaddingToButtons();
    // Re-run whichever arrangement is live. Later flips glide, so dragging the
    // slider animates instead of snapping between two sizes.
    if (!m_leftButtons.empty() || !m_rightButtons.empty())
        setDynamicLineLayout(m_dynamicLine);
}

void SidebarManager::nudgeButtonScale(int percent, float pixels) {
    // Re-place the row by a couple of pixels and put it straight back. The
    // second pass reuses the same code the size slider drives, which is what
    // settles the shortcut icons on the configured size at startup.
    const int base = std::clamp(percent, 70, 150);
    const float strip = std::max(1.f, kDynamicStripButtonSize);
    const int delta = std::max(1, static_cast<int>(pixels * 100.f / strip + 0.5f));
    setButtonScale(std::clamp(base + delta, 70, 150));
    setButtonScale(base);
}

AppletButton* SidebarManager::gameSlotButton(int slot) const {
    // Pinned sockets only: the applet buttons are never handed out here, which
    // is what stops a pinned game from replacing Album / Mii / Settings / ...
    if (slot < 0 || slot >= static_cast<int>(m_pinButtons.size()))
        return nullptr;
    return m_pinButtons[static_cast<std::size_t>(slot)].get();
}

void SidebarManager::setGameShortcut(int slot, const std::string& label,
                                     const std::vector<std::uint8_t>& iconData,
                                     nxui::GpuDevice& gpu, nxui::Renderer& ren,
                                     std::function<void()> action) {
    AppletButton* btn = gameSlotButton(slot);
    if (!btn)
        return;
    const bool bound = setGameShortcutIcon(slot, iconData, gpu, ren);
    if (!bound)
        btn->setIcon(nullptr);
    DebugLog::log("[shortcuts] slot=%d label=%s icon_bytes=%u bound=%d",
                  slot, label.c_str(), static_cast<unsigned>(iconData.size()),
                  bound ? 1 : 0);
    btn->setLabel(label);
    btn->setAccessibilityLabel(label);
    btn->setAccessibilityHint(nxui::I18n::instance().tr(
        "accessibility.hints.open_shortcut", "A to open this shortcut."));
    btn->setOnActivate(std::move(action));
    btn->setVisible(true);
}

bool SidebarManager::setGameShortcutIcon(int slot,
                                         const std::vector<std::uint8_t>& iconData,
                                         nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    AppletButton* btn = gameSlotButton(slot);
    if (!btn || iconData.empty())
        return false;
    nxui::Texture& tex = m_gameIcons[static_cast<std::size_t>(slot)];
    tex = nxui::Texture();
    if (!tex.loadFromMemory(gpu, ren, iconData.data(), iconData.size(), 128)
        || !tex.valid())
        return false;
    // A pinned game is colour artwork: never let the applet chrome tint or the
    // mono contrast mask touch it, or the tile reads as an empty square.
    btn->setIconAdaptive(false);
    btn->setIconUsesColorArtwork(true);
    btn->setIconContrastMask(nullptr);
    btn->setIcon(&tex);
    return true;
}

void SidebarManager::clearGameShortcut(int slot) {
    AppletButton* btn = gameSlotButton(slot);
    if (!btn)
        return;
    // Drop the pointer before the texture goes away, then hide the socket. The
    // six applets are untouched by this path.
    btn->setIcon(nullptr);
    btn->setOnActivate(std::function<void()>());
    btn->setVisible(false);
    m_gameIcons[static_cast<std::size_t>(slot)] = nxui::Texture();
}

void SidebarManager::setChromeStyle(float cornerRadius, float borderWidth) {
    m_chromeRadius = std::max(0.f, cornerRadius);
    m_chromeBorder = std::clamp(borderWidth, 0.8f, 3.5f);
    applyChromeToButtons();
}

void SidebarManager::applyChromeToButtons() {
    // With the plate on, the buttons must not paint their own pills or the bar
    // turns back into six capsules. Their base alpha goes to zero so only the
    // plate shows; the rim colour goes with it, leaving the plate's rim alone.
    // Only the strip layout has a plate to sit on; the two-column side
    // stacks keep their own pills.
    const bool plate = m_stripPlate && m_dynamicLine;
    auto apply = [&](std::shared_ptr<AppletButton>& btn) {
        if (!btn) return;
        btn->setCornerRadius(m_chromeRadius);
        btn->setBorderWidth(plate ? 0.f : m_chromeBorder);
        btn->setBaseColor(plate ? m_btnBase.withAlpha(0.f) : m_btnBase);
        btn->setBorderColor(plate ? m_btnBorder.withAlpha(0.f) : m_btnBorder);
        btn->setHighlightColor(plate ? m_btnHighlight.withAlpha(0.f) : m_btnHighlight);
        btn->setBlurEnabled(false);
    };
    for (auto& btn : m_leftButtons)  apply(btn);
    for (auto& btn : m_rightButtons) apply(btn);
    for (auto& btn : m_pinButtons)   apply(btn);
    if (m_searchBar)
        m_searchBar->setChromeVisible(!plate);
}

void SidebarManager::applyPaddingToButtons() {
    // Keep the glyph inset proportional to the button so a large shortcut does
    // not look like a framed picture and a small one does not lose its rim.
    const float padding = std::max(2.f, kSidebarButtonPadding * m_buttonScale);
    for (auto& btn : m_leftButtons) {
        if (btn) btn->setPadding(padding);
    }
    for (auto& btn : m_rightButtons) {
        if (btn) btn->setPadding(padding);
    }
    for (auto& btn : m_pinButtons) {
        if (btn) btn->setPadding(padding);
    }
}

void SidebarManager::setGameShortcutSide(int slot, int side) {
    if (slot < 0 || slot >= kPinnedSlotCount)
        return;
    m_pinSides[static_cast<std::size_t>(slot)] = std::clamp(side, 0, 1);
}

int SidebarManager::gameShortcutSide(int slot) const {
    if (slot < 0 || slot >= kPinnedSlotCount)
        return 0;
    return m_pinSides[static_cast<std::size_t>(slot)];
}

void SidebarManager::setStripEdge(int edge) {
    const int clamped = std::clamp(edge, 0, 1);
    if (m_stripEdge == clamped)
        return;
    m_stripEdge = clamped;
    if (m_dynamicLine)
        setDynamicLineLayout(true);
}

void SidebarManager::setStripNudge(float leftPx, float rightPx) {
    if (std::abs(leftPx - m_stripNudgeX) < 0.5f
        && std::abs(rightPx - m_stripNudgeRight) < 0.5f)
        return;
    m_stripNudgeX = leftPx;
    m_stripNudgeRight = rightPx;
    if (!m_leftButtons.empty() || !m_rightButtons.empty())
        setDynamicLineLayout(m_dynamicLine);
}

float SidebarManager::stripReserve() const {
    // The strip itself plus the margin it keeps from the screen edge, which is
    // the same amount the grid has to stay clear of.
    float reserve = kDynamicStripButtonSize * m_buttonScale + 2.f * kDynamicStripY;
    // The plate reaches half the extra bar height past the old band, so the
    // grid has to clear that too or the first tile row lands under the bar.
    if (m_stripPlate && m_dynamicLine)
        reserve += kStripBarGrowY * 0.5f;
    return reserve;
}

float SidebarManager::reservedTop() const {
    return (m_dynamicLine && m_stripEdge == 0) ? stripReserve() : 0.f;
}

float SidebarManager::reservedBottom() const {
    // Pinned sockets ride beside the applet clusters in both layouts now, so
    // the grid only clears a strip that is actually at the bottom edge.
    if (m_dynamicLine && m_stripEdge == 1)
        return stripReserve();
    return 0.f;
}

void SidebarManager::setStripPlate(bool enabled) {
    if (m_stripPlate == enabled)
        return;
    m_stripPlate = enabled;
    // The buttons restyle either way: plate on means their pills disappear.
    applyChromeToButtons();
    syncStripPlateGeometry();
}

void SidebarManager::setStripGradient(bool enabled, const nxui::Color& a,
                                      const nxui::Color& b) {
    m_stripGradient = enabled;
    m_stripGradientA = a;
    m_stripGradientB = b;
    // The cache owns the fully resolved plate style, so gradient changes have
    // to invalidate it or the next layout tick can keep the old flat style.
    m_plateCacheValid = false;
    if (m_stripPlateWidget) {
        m_stripPlateWidget->setGradientFill(enabled, a, b);
        m_stripPlateWidget->setGradientPhase(m_stripGradientPhase);
    }
    syncStripPlateGeometry();
}

void SidebarManager::setStripFlushTop(bool enabled) {
    if (m_stripFlushTop == enabled)
        return;
    m_stripFlushTop = enabled;
    syncStripPlateGeometry();
}

void SidebarManager::setStripPlateSuppressed(bool suppressed) {
    m_plateSuppressed = suppressed;
    if (suppressed && m_stripPlateWidget)
        m_stripPlateWidget->setVisible(false);
    if (m_searchBar)
        m_searchBar->setVisible(!suppressed && m_dynamicLine);
}

void SidebarManager::setOpacityTint(bool enabled, const nxui::Color& tint) {
    m_opacityTintEnabled = enabled;
    m_opacityTint = tint;
    // The plate cache compares the derived base colour, so force one restyle
    // when the tint changes even if the theme itself stayed put.
    m_plateCacheValid = false;
}

void SidebarManager::syncStripPlateGeometry() {
    if (m_searchBar) {
        const bool show = m_dynamicLine && !m_plateSuppressed
            && m_searchBar->rect().width > 0.f;
        m_searchBar->setVisible(show);
    }
    if (!m_stripPlateWidget)
        return;
    // Sync runs from update(), so the plate follows the buttons through a
    // layout morph instead of blinking out and back.
    if (!m_stripPlate || !m_dynamicLine || m_plateSuppressed
        || m_leftButtons.empty() || m_rightButtons.empty()) {
        m_stripPlateWidget->setVisible(false);
        m_plateCacheValid = false;
        return;
    }

    bool any = false;
    nxui::Rect span{};
    auto grow = [&](const nxui::Rect& r) {
        if (!any) {
            span = r;
            any = true;
            return;
        }
        const float right  = std::max(span.right(), r.right());
        const float bottom = std::max(span.bottom(), r.bottom());
        span.x = std::min(span.x, r.x);
        span.y = std::min(span.y, r.y);
        span.width  = right - span.x;
        span.height = bottom - span.y;
    };
    for (const auto& btn : m_leftButtons)  if (btn) grow(btn->rect());
    for (const auto& btn : m_rightButtons) if (btn) grow(btn->rect());
    // The search field is part of the bar: the plate grows around it instead
    // of leaving it as a loose capsule floating under the shortcuts.
    if (m_searchBar && m_searchBar->isVisible())
        grow(m_searchBar->rect());
    if (!any) {
        m_stripPlateWidget->setVisible(false);
        m_plateCacheValid = false;
        return;
    }

    // One bar across both groups: the gap between the two clusters is part of
    // the plate, which is what makes it read as a single strip.
    const float pad = kStripPlatePad * m_buttonScale;
    span.x -= pad;
    span.y -= pad;
    span.width  += pad * 2.f;
    span.height += pad * 2.f;

    // Top strip: run the plate into the profile and battery capsules. It spans
    // centre to centre and renders behind them, so its ends disappear under
    // their glass and the whole top row reads as one curved band.
    // Merged at whichever edge the strip sits on: when the shortcuts move to
    // the bottom the profile and battery travel with them, so the capsules are
    // still the ends of this band.
    const bool merged = m_anchorValid;
    if (merged) {
        // Outer edge to outer edge at one capsule height. The height comes
        // from the capsules, not the shortcut row, so nudging the row can no
        // longer make the bar taller than the profile tile.
        const float left   = std::min(m_anchorProfile.x, m_anchorBattery.x);
        const float right  = std::max(m_anchorProfile.right(),
                                      m_anchorBattery.right());
        const float top = std::min(m_anchorProfile.y, m_anchorBattery.y);
        const float bandH = std::max(m_anchorProfile.height,
                                     m_anchorBattery.height);
        if (right > left + 40.f) {
            // The band has to contain the shortcut row as well as the capsules.
            // Sizing it from the capsules alone left it shorter than the
            // buttons, so the focus ring reached past the bar's bottom edge.
            // span already carries kStripPlatePad, which is exactly the ring's
            // outward reach.
            const float bandTop = std::min(top, span.y);
            const float bandBottom = std::max(top + bandH, span.bottom());
            span.x = left;
            span.y = bandTop;
            span.width = right - left;
            span.height = bandBottom - bandTop;
        }
    }

    // Bar height is chrome, not a button property: grow the plate by equal
    // amounts above and below the row. Because span is centred on the buttons,
    // they stay exactly on the bar's centre line without being scaled or moved.
    span.y -= kStripBarGrowY * 0.5f;
    span.height += kStripBarGrowY;

    nxui::Color base = m_btnBase;
    nxui::Color rim = m_btnBorder;
    nxui::Color gradA = m_stripGradientA;
    nxui::Color gradB = m_stripGradientB;
    // The strip is the most visible shortcut surface, so it always takes the
    // opacity colour. Panels keep using the Style tint mix; here a selected
    // colour must show on every theme, including the gradient fill path that
    // previously bypassed the tint entirely.
    if (m_opacityTintEnabled) {
        const auto mix = [](const nxui::Color& c, const nxui::Color& t, float m) {
            return nxui::Color(c.r + (t.r - c.r) * m,
                               c.g + (t.g - c.g) * m,
                               c.b + (t.b - c.b) * m,
                               c.a);
        };
        base  = mix(base, m_opacityTint, 0.82f);
        rim   = mix(rim, m_opacityTint, 0.55f);
        // The flat body can hand itself almost entirely to the tint. The two
        // gradient stops cannot: at 0.82 both became the same colour and the
        // animation disappeared. Keep over half of each authored stop so the
        // sweep survives on tinted themes while still following the tint.
        gradA = mix(gradA, m_opacityTint, 0.42f);
        gradB = mix(gradB, m_opacityTint, 0.42f);
    }
    base.a = std::max(base.a, switchu::ui::chrome::kBandAlphaMin);
    rim.a = std::max(rim.a, switchu::ui::chrome::kBandRimAlphaMin);

    // Merged ends get the capsules' pill curvature; a free-standing strip keeps
    // the tile radius so it still matches the icons.
    const float plateRadius = std::min(m_chromeRadius, span.height * 0.5f);
    nxui::Rect plateRect = span;
    if (m_stripFlushTop) {
        if (m_stripEdge == 0) {
            // Push the rounded top corners above the screen so the visible top
            // edge is a straight cut at y=0. The bottom keeps its curve.
            plateRect.y = -plateRadius;
            plateRect.height = span.bottom() + plateRadius;
        } else {
            // Bottom strip: push the rounded bottom corners below the screen so
            // the visible bottom edge is a straight cut at y=720. The top keeps
            // its curve.
            plateRect.height = (720.f + plateRadius) - plateRect.y;
        }
    }

    // The strip is static whenever neither the row glides nor the theme changes.
    // Compare the fully derived style so a layout change still repaints, while
    // an idle frame touches no widget state at all.
    const auto sameColor = [](const nxui::Color& lhs, const nxui::Color& rhs) {
        return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
    };
    if (m_plateCacheValid
        && std::abs(plateRect.x - m_plateRectCache.x) < 0.01f
        && std::abs(plateRect.y - m_plateRectCache.y) < 0.01f
        && std::abs(plateRect.width - m_plateRectCache.width) < 0.01f
        && std::abs(plateRect.height - m_plateRectCache.height) < 0.01f
        && std::abs(plateRadius - m_plateRadiusCache) < 0.01f
        && std::abs(m_chromeBorder - m_plateBorderCache) < 0.01f
        && sameColor(base, m_plateBaseCache)
        && sameColor(rim, m_plateRimCache)
        && sameColor(m_btnHighlight, m_plateHighlightCache)
        && m_stripGradient == m_plateGradientCache
        && sameColor(gradA, m_plateGradientACache)
        && sameColor(gradB, m_plateGradientBCache)) {
        return;
    }

    m_stripPlateWidget->setRect(plateRect);
    m_stripPlateWidget->setCornerRadius(plateRadius);
    m_stripPlateWidget->setBorderWidth(m_chromeBorder);
    m_stripPlateWidget->setBaseColor(base);
    m_stripPlateWidget->setGradientFill(m_stripGradient, gradA, gradB);
    m_stripPlateWidget->setBorderColor(rim);
    m_stripPlateWidget->setHighlightColor(m_btnHighlight);
    m_stripPlateWidget->setVisible(true);

    m_plateRectCache = plateRect;
    m_plateBaseCache = base;
    m_plateRimCache = rim;
    m_plateHighlightCache = m_btnHighlight;
    m_plateGradientCache = m_stripGradient;
    m_plateGradientACache = gradA;
    m_plateGradientBCache = gradB;
    m_plateRadiusCache = plateRadius;
    m_plateBorderCache = m_chromeBorder;
    m_plateCacheValid = true;
}

void SidebarManager::setGridClearance(float left, float right) {
    if (std::abs(left - m_clearLeft) < 0.5f && std::abs(right - m_clearRight) < 0.5f)
        return;
    m_clearLeft = left;
    m_clearRight = right;
    // Only the side stacks are constrained; the strip runs across the top or
    // bottom, where the grid clears it vertically.
    if (!m_dynamicLine && (!m_leftButtons.empty() || !m_rightButtons.empty()))
        setDynamicLineLayout(false);
}

nxui::Rect SidebarManager::stripBandTarget() const {
    if (!m_dynamicLine)
        return {};
    const float size = kDynamicStripButtonSize * m_buttonScale;
    const float y = std::clamp(
        (m_stripEdge == 0)
            ? kDynamicStripY + kDynamicStripDownBias + m_stripNudgeY
            : 720.f - size - kDynamicStripY + m_stripNudgeY,
        4.f, 716.f - size);
    return {0.f, y, 1280.f, size};
}

void SidebarManager::setStripNudgeY(float px) {
    if (std::abs(px - m_stripNudgeY) < 0.01f)
        return;
    m_stripNudgeY = px;
    if (!m_leftButtons.empty() || !m_rightButtons.empty())
        setDynamicLineLayout(m_dynamicLine);
}
