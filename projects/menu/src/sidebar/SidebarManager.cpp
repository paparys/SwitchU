#include "SidebarManager.hpp"
#include "ui/SurfaceStyle.hpp"
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

constexpr int kSidebarIconCount = 6;
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
// Breathing room the plate keeps around the six buttons.
constexpr float kStripPlatePad = 9.f;
// Air kept between a shortcut stack and the tile band.
constexpr float kColumnClearGap = 10.f;
// Gap under the pinned-game row when the applets are in side columns.
constexpr float kPinnedRowMargin = 14.f;
// Mirrors IconGrid::kLayoutMorphDuration so chrome and tiles land together.
constexpr float kSidebarGlideDuration = 0.40f;
// Tiny cascade across the six buttons: reads as one gesture, not six snaps.
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
    const float totalH = 3 * btnSize + 2.f * gap;
    float startY = 360.f - totalH * 0.5f;

    if (!m_stripPlateWidget) {
        m_stripPlateWidget = std::make_shared<nxui::GlassPanel>();
        m_stripPlateWidget->setTag("shortcutStripPlate");
        m_stripPlateWidget->setBlurEnabled(false);
        m_stripPlateWidget->setLiquidGlassEnabled(true);
        m_stripPlateWidget->setForceLiquidGlass(true);
        m_stripPlateWidget->setVisible(false);
    }

    m_leftButtons.clear();
    m_rightButtons.clear();
    m_settingsButton = nullptr;
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
        album->setRect({leftX, startY + 0.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(album));

        auto miiEditor = makeBtn(&m_icons[1], "sidebar.mii_editor", "Mii Editor", actions.onMiiEditor);
        miiEditor->setRect({leftX, startY + 1.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(miiEditor));

        auto settings = makeBtn(&m_icons[5], "sidebar.settings", "Settings", actions.onSettings);
        m_settingsButton = settings.get();
        settings->setRect({leftX, startY + 2.f * (btnSize + gap), btnSize, btnSize});
        m_leftButtons.push_back(std::move(settings));
    }

    {
        auto ctrl = makeBtn(&m_icons[2], "sidebar.controllers", "Controllers", actions.onControllers);
        ctrl->setRect({rightX, startY + 0.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(ctrl));

        // Theme Shop middle / Power (Sleep) bottom — matches left stack rhythm
        // (utility → destination → system action).
        auto themeShop = makeBtn(&m_icons[4], "sidebar.theme_shop", "Theme Shop", actions.onMiiverse);
        m_themeShopButton = themeShop.get();
        themeShop->setRect({rightX, startY + 1.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(themeShop));

        auto sleep = makeBtn(&m_icons[3], "sidebar.sleep", "Sleep", actions.onSleep);
        sleep->setRect({rightX, startY + 2.f * (btnSize + gap), btnSize, btnSize});
        m_rightButtons.push_back(std::move(sleep));
    }

    // Remember the stock six so the pinned sockets below stay independent of
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
        float leftGroupX = kDynamicLeftGroupX;
    float rightGroupX = kDynamicRightGroupX;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
            auto* button = ordered[i];
            const bool rightGroup = i >= m_leftButtons.size();
            const std::size_t groupIndex = rightGroup ? i - m_leftButtons.size() : i;
            const float baseX = rightGroup ? kDynamicRightGroupX : kDynamicLeftGroupX;
            // Each group is three buttons wide and travels on its own paddle,
            // clamped so it slides instead of being pushed off screen.
            const float groupSpan = 2.f * (stripSize + stripGap) + stripSize;
            const float lowShift = 8.f - baseX;
            const float highShift = 1272.f - (baseX + groupSpan);
            const float shift = rightGroup ? m_stripNudgeRight : m_stripNudgeX;
            float groupX = baseX + std::clamp(shift,
                                              std::min(lowShift, highShift),
                                              std::max(lowShift, highShift));
            // While the band is merged the capsules are part of this row, so a
            // paddle stops at them instead of sliding a cluster underneath.
            if (m_anchorValid) {
                if (rightGroup)
                    groupX = std::min(groupX,
                                      m_anchorBattery.x - kColumnClearGap - groupSpan);
                else
                    groupX = std::max(groupX,
                                      m_anchorProfile.right() + kColumnClearGap);
            }
            groupX = std::clamp(groupX, 8.f, 1272.f - groupSpan);
            if (rightGroup)
                rightGroupX = groupX;
            else
                leftGroupX = groupX;
            place(button,
                  {groupX + static_cast<float>(groupIndex) *
                                (stripSize + stripGap),
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
        // Pinned games continue the chosen applet cluster's row: left pins grow
        // rightwards from the left group, right pins grow leftwards from the
        // right group. They never take one of the six applets' sockets.
        std::vector<AppletButton*> pins;
        for (auto& btn : m_pinButtons)
            if (btn && btn->isVisible()) pins.push_back(btn.get());
        m_visiblePins = static_cast<int>(pins.size());
        if (!pins.empty()) {
            const float clusterSpan = 3.f * stripSize + 2.f * stripGap;
            const float pinsSpan = (static_cast<float>(pins.size()) - 1.f)
                                 * (stripSize + stripGap) + stripSize;
            const float clear = kColumnClearGap;
            const float lowX = leftGroupX + clusterSpan + clear;
            const float highX = rightGroupX - clear - pinsSpan;
            float pinsX = (m_pinSide == 0) ? lowX : highX;
            pinsX = std::clamp(pinsX, std::min(lowX, highX), std::max(lowX, highX));
            pinsX = std::clamp(pinsX, 8.f, std::max(8.f, 1272.f - pinsSpan));
            for (std::size_t i = 0; i < pins.size(); ++i) {
                place(pins[i],
                      {pinsX + static_cast<float>(i) * (stripSize + stripGap),
                       stripY, stripSize, stripSize},
                      static_cast<int>(i));
            }
            // Keep the row on the one horizontal chain: last left applet ->
            // pins -> first right applet, so it never becomes a dead end.
            nxui::Widget* leftEnd = m_leftButtons.empty()
                ? nullptr : static_cast<nxui::Widget*>(m_leftButtons.back().get());
            nxui::Widget* rightStart = m_rightButtons.empty()
                ? nullptr : static_cast<nxui::Widget*>(m_rightButtons.front().get());
            for (std::size_t i = 0; i < pins.size(); ++i) {
                pins[i]->setCustomNavigation(nxui::FocusDirection::LEFT,
                    i == 0 ? leftEnd : static_cast<nxui::Widget*>(pins[i - 1]));
                pins[i]->setCustomNavigation(nxui::FocusDirection::RIGHT,
                    i + 1 < pins.size() ? static_cast<nxui::Widget*>(pins[i + 1])
                                        : rightStart);
            }
            if (!m_leftButtons.empty())
                m_leftButtons.back()->setCustomNavigation(nxui::FocusDirection::RIGHT,
                    static_cast<nxui::Widget*>(pins.front()));
            if (!m_rightButtons.empty())
                m_rightButtons.front()->setCustomNavigation(nxui::FocusDirection::LEFT,
                    static_cast<nxui::Widget*>(pins.back()));
        }
        syncStripPlateGeometry();
        return;
    }

    const float colSize = kSidebarButtonSize * m_buttonScale;
    const float colGap  = kSidebarColumnGap * m_buttonScale;
    const float totalH = 3.f * colSize + 2.f * colGap;
    const float startY = 360.f - totalH * 0.5f;
    const float rightX = 1280.f - kSidebarMarginX - colSize;
    const float leftShift = std::clamp(m_stripNudgeX,
        8.f - kSidebarMarginX, 1272.f - colSize - kSidebarMarginX);
    const float rightShift = std::clamp(m_stripNudgeRight,
        8.f - rightX, 1272.f - colSize - rightX);
    // The sliders may pull a stack outward as far as they like, but inward
    // travel stops at the tile band: the shortcuts and the grid stay separate
    // instead of one sliding over the other.
    const float leftMax = std::max(8.f, m_clearLeft - kColumnClearGap - colSize);
    const float leftColX = std::clamp(kSidebarMarginX + leftShift, 8.f, leftMax);
    const float rightMin = std::min(1272.f - colSize, m_clearRight + kColumnClearGap);
    const float rightColX = std::clamp(rightX + rightShift,
                                       std::max(8.f, rightMin), 1272.f - colSize);
    for (std::size_t i = 0; i < m_leftButtons.size(); ++i) {
        place(m_leftButtons[i].get(),
              {leftColX,
               startY + static_cast<float>(i) * (colSize + colGap),
               colSize, colSize},
              static_cast<int>(i));
    }
    for (std::size_t i = 0; i < m_rightButtons.size(); ++i) {
        place(m_rightButtons[i].get(),
              {rightColX,
               startY + static_cast<float>(i) * (colSize + colGap),
               colSize, colSize},
              static_cast<int>(i));
    }
    // Column layout: pinned games stack beside the chosen applet column, two
    // sub-columns of three so the block is exactly as tall as the stack.
    std::vector<AppletButton*> pins;
    for (auto& btn : m_pinButtons)
        if (btn && btn->isVisible()) pins.push_back(btn.get());
    m_visiblePins = static_cast<int>(pins.size());
    if (!pins.empty()) {
        constexpr int kPinsPerColumn = 3;
        const float innerX = (m_pinSide == 0)
            ? leftColX + colSize + colGap
            : rightColX - colGap - colSize;
        const float step = colSize + colGap;
        for (std::size_t i = 0; i < pins.size(); ++i) {
            const int col = static_cast<int>(i) / kPinsPerColumn;
            const int row = static_cast<int>(i) % kPinsPerColumn;
            const float x = (m_pinSide == 0)
                ? innerX + static_cast<float>(col) * step
                : innerX - static_cast<float>(col) * step;
            place(pins[i],
                  {std::clamp(x, 8.f, 1272.f - colSize),
                   startY + static_cast<float>(row) * step,
                   colSize, colSize},
                  static_cast<int>(i));
        }
        // Wire the block by row and column: UP/DOWN inside a sub-column,
        // LEFT/RIGHT across to the applet stack it belongs to.
        for (std::size_t i = 0; i < pins.size(); ++i) {
            const int col = static_cast<int>(i) / kPinsPerColumn;
            const int row = static_cast<int>(i) % kPinsPerColumn;
            const std::size_t partner = (col == 0)
                ? i + static_cast<std::size_t>(kPinsPerColumn)
                : i - static_cast<std::size_t>(kPinsPerColumn);
            const bool partnerExists = partner < pins.size();
            const std::size_t up = (row == 0) ? i : i - 1;
            const std::size_t down = (row == kPinsPerColumn - 1) ? i : i + 1;
            pins[i]->setCustomNavigation(nxui::FocusDirection::UP,
                static_cast<nxui::Widget*>(pins[up]));
            pins[i]->setCustomNavigation(nxui::FocusDirection::DOWN,
                static_cast<nxui::Widget*>(pins[down]));
            nxui::Widget* across = nullptr;
            const auto& stack = (m_pinSide == 0) ? m_leftButtons : m_rightButtons;
            if (col == 0 && !stack.empty()) {
                const std::size_t rowIndex = std::min<std::size_t>(
                    static_cast<std::size_t>(row), stack.size() - 1);
                across = static_cast<nxui::Widget*>(stack[rowIndex].get());
            }
            pins[i]->setCustomNavigation(nxui::FocusDirection::LEFT,
                (m_pinSide == 0) ? across
                                 : (partnerExists
                                        ? static_cast<nxui::Widget*>(pins[partner])
                                        : nullptr));
            pins[i]->setCustomNavigation(nxui::FocusDirection::RIGHT,
                (m_pinSide == 0)
                    ? (partnerExists ? static_cast<nxui::Widget*>(pins[partner])
                                     : nullptr)
                    : across);
        }
    }
    if (!m_leftButtons.empty()) {
        m_leftButtons.front()->setCustomNavigation(nxui::FocusDirection::UP,
                                                   m_leftButtons.front().get());
        m_leftButtons.back()->setCustomNavigation(nxui::FocusDirection::DOWN,
                                                  m_leftButtons.back().get());
    }
    if (!m_rightButtons.empty()) {
        m_rightButtons.front()->setCustomNavigation(nxui::FocusDirection::UP,
                                                    m_rightButtons.front().get());
        m_rightButtons.back()->setCustomNavigation(nxui::FocusDirection::DOWN,
                                                   m_rightButtons.back().get());
    }
}

void SidebarManager::setDynamicLineDownAction(std::function<void()> action) {
    auto configure = [&action](std::shared_ptr<AppletButton>& button) {
        button->setCustomNavigation(nxui::FocusDirection::DOWN, nullptr);
        button->removeAction(static_cast<uint64_t>(nxui::Button::DDown));
        button->removeAction(static_cast<uint64_t>(nxui::Button::LStickD));
        button->removeAction(static_cast<uint64_t>(nxui::Button::RStickD));
        if (action)
            button->addDirectionAction(nxui::FocusDirection::DOWN, action);
    };
    for (auto& button : m_leftButtons) configure(button);
    for (auto& button : m_rightButtons) configure(button);
}

void SidebarManager::setDynamicLineUpTarget(nxui::Widget* target) {
    for (auto& button : m_leftButtons)
        button->setCustomNavigation(nxui::FocusDirection::UP, target);
    for (auto& button : m_rightButtons)
        button->setCustomNavigation(nxui::FocusDirection::UP, target);
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

void SidebarManager::setHudAnchors(const nxui::Rect& profile, const nxui::Rect& battery) {
    const auto same = [](const nxui::Rect& a, const nxui::Rect& b) {
        return std::abs(a.x - b.x) < 0.5f && std::abs(a.y - b.y) < 0.5f
            && std::abs(a.width - b.width) < 0.5f
            && std::abs(a.height - b.height) < 0.5f;
    };
    const bool valid = profile.width > 0.f && battery.width > 0.f;
    if (valid == m_anchorValid && same(profile, m_anchorProfile)
        && same(battery, m_anchorBattery))
        return;
    m_anchorProfile = profile;
    m_anchorBattery = battery;
    m_anchorValid = valid;
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
    if (m_leftButtons.size() == 3 && m_rightButtons.size() == 3) {
        AppletButton* byGlyph[kSidebarIconCount] = {
            m_leftButtons[0].get(),   // album
            m_leftButtons[1].get(),   // mii editor
            m_rightButtons[0].get(),  // controllers
            m_rightButtons[2].get(),  // power -> Sleep
            m_rightButtons[1].get(),  // themes -> Theme Shop
            m_leftButtons[2].get(),   // settings
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
        else if (def.iconIdx == 5) btn = m_leftButtons[2].get();
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

        nxui::Texture* idleTex = def.useFirstFrame ? nullptr : staticTex;
        tryLoadAnimation(gpu, ren, resolveAsset(def.webpFile), btn, idleTex);
        btn->setIcon(idleTex ? idleTex : staticTex);
    }

    // Boot trace: one line per shortcut naming the glyph it resolved, the
    // button it was bound to and the texture object behind it. If the icons
    // ever cross over, the log shows whether the file resolution or the
    // pointer binding is the one at fault.
    if (m_leftButtons.size() == 3 && m_rightButtons.size() == 3) {
        static const char* kTraceFiles[kSidebarIconCount] = {
            "album.png", "mii_editor.png", "controller.png",
            "power.png", "themes.png", "settings.png",
        };
        AppletButton* kTraceBtn[kSidebarIconCount] = {
            m_leftButtons[0].get(), m_leftButtons[1].get(),
            m_rightButtons[0].get(), m_rightButtons[2].get(),
            m_rightButtons[1].get(), m_leftButtons[2].get(),
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
        if (focused && e.anim.hasFrames()) {
            e.button->setIcon(e.anim.currentFrame());
            // Animated WebP frames keep their own colour — no chrome tint.
            e.button->setIconUsesColorArtwork(true);
        } else {
            // staticTex == nullptr: use frame 0 of the animation as idle
            nxui::Texture* idle = e.staticTex ? e.staticTex
                                              : (e.anim.hasFrames() ? e.anim.currentFrame() : nullptr);
            e.button->setIcon(idle);
            e.button->setIconUsesColorArtwork(e.staticTex == nullptr && e.anim.hasFrames());
        }
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
    // turns back into six capsules. They cannot merely go transparent: the
    // liquid-glass path clamps the body alpha up to 0.16, so the glass has to
    // be switched off as well.
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
        btn->setLiquidGlassEnabled(!plate);
        btn->setForceLiquidGlass(!plate);
        btn->setBlurEnabled(false);
    };
    for (auto& btn : m_leftButtons)  apply(btn);
    for (auto& btn : m_rightButtons) apply(btn);
    for (auto& btn : m_pinButtons)   apply(btn);
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

void SidebarManager::setPinSide(int side) {
    const int clamped = std::clamp(side, 0, 1);
    if (m_pinSide == clamped)
        return;
    m_pinSide = clamped;
    if (!m_leftButtons.empty() || !m_rightButtons.empty())
        setDynamicLineLayout(m_dynamicLine);
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
    return kDynamicStripButtonSize * m_buttonScale + 2.f * kDynamicStripY;
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

void SidebarManager::setStripPlateSuppressed(bool suppressed) {
    m_plateSuppressed = suppressed;
    if (suppressed && m_stripPlateWidget)
        m_stripPlateWidget->setVisible(false);
}

void SidebarManager::syncStripPlateGeometry() {
    if (!m_stripPlateWidget)
        return;
    // Mid-morph the buttons are still travelling between the two layouts, so
    // the bar would smear across the screen. Hide it until they land.
    const bool gliding = std::any_of(m_buttonGlides.begin(), m_buttonGlides.end(),
                                     [](const ButtonGlide& g) { return g.active; });
    if (!m_stripPlate || !m_dynamicLine || m_plateSuppressed || gliding
        || m_leftButtons.empty() || m_rightButtons.empty()) {
        m_stripPlateWidget->setVisible(false);
        return;
    }

    bool any = false;
    nxui::Rect span{};
    auto grow = [&](const std::shared_ptr<AppletButton>& btn) {
        if (!btn) return;
        const nxui::Rect r = btn->rect();
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
    for (const auto& btn : m_leftButtons)  grow(btn);
    for (const auto& btn : m_rightButtons) grow(btn);
    if (!any) {
        m_stripPlateWidget->setVisible(false);
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
            span.x = left;
            span.y = top;
            span.width = right - left;
            span.height = bandH;
        }
    }

    nxui::Color base = m_btnBase;
    base.a = std::max(base.a, switchu::ui::surface::kBandAlphaMin);
    nxui::Color rim = m_btnBorder;
    rim.a = std::max(rim.a, switchu::ui::surface::kBandRimAlphaMin);

    m_stripPlateWidget->setRect(span);
    // Merged ends get the capsules' pill curvature; a free-standing strip keeps
    // the tile radius so it still matches the icons.
    const float plateRadius = std::min(m_chromeRadius, span.height * 0.5f);
    m_stripPlateWidget->setCornerRadius(plateRadius);
    m_stripPlateWidget->setBorderWidth(m_chromeBorder);
    m_stripPlateWidget->setBaseColor(base);
    m_stripPlateWidget->setBorderColor(rim);
    m_stripPlateWidget->setHighlightColor(m_btnHighlight);
    m_stripPlateWidget->setVisible(true);
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
