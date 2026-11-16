#include "AllAppsScreen.hpp"
#include "GlossyIcon.hpp"
#include "MotionPreset.hpp"
#include "core/DebugLog.hpp"
#include "launcher/AppListLoader.hpp"
#include "core/PlaytimeStore.hpp"
#include "core/UiTokens.hpp"
#include "ui/DrawFocusRing.hpp"
#include "ui/KeycapHints.hpp"
#include "ui/ScreenShell.hpp"
#include "ui/TextFit.hpp"

#include <nxui/core/Font.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/Theme.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace {

using namespace switchu::ui;
constexpr float kScreenW = shell::kScreenW;
constexpr float kScreenH = shell::kScreenH;
constexpr float kSidePad = shell::kPagePad;
constexpr float kHeaderH = shell::kPageHeaderH;
constexpr float kFooterH = shell::kPageFooterH;
constexpr float kMetaH = shell::kPageMetaH;
constexpr float kCellGap = ui::kSpace4;

void drawStar(nxui::Renderer& ren, nxui::Vec2 center, float outer,
              const nxui::Color& color) {
    constexpr int kPoints = 10;
    constexpr float kPi = 3.14159265358979f;
    nxui::Vec2 points[kPoints];
    for (int i = 0; i < kPoints; ++i) {
        const float radius = (i % 2 == 0) ? outer : outer * 0.44f;
        const float angle = -kPi * 0.5f + static_cast<float>(i) * kPi / 5.f;
        points[i] = {center.x + std::cos(angle) * radius,
                     center.y + std::sin(angle) * radius};
    }
    for (int i = 0; i < kPoints; ++i)
        ren.drawTriangle(center, points[i], points[(i + 1) % kPoints], color);
}

std::string formatPlaytime(std::uint64_t seconds) {
    if (seconds >= 86400ULL)
        return std::to_string(seconds / 86400ULL) + "d";
    if (seconds >= 3600ULL) {
        const std::uint64_t hours = seconds / 3600ULL;
        const std::uint64_t mins = (seconds % 3600ULL) / 60ULL;
        return std::to_string(hours) + "h" +
               (mins != 0 ? " " + std::to_string(mins) + "m" : "");
    }
    return std::to_string(std::max<std::uint64_t>(1, seconds / 60ULL)) + "m";
}

std::string sortName(int mode) {
    auto& i18n = nxui::I18n::instance();
    if (mode == 1)
        return i18n.tr("allapps.sort_recent_short", "Recent");
    if (mode == 2)
        return i18n.tr("allapps.sort_favourites", "Favourites");
    return i18n.tr("allapps.sort_az", "A-Z");
}

} // namespace

AllAppsScreen::AllAppsScreen() {
    setRect({0.f, 0.f, kScreenW, kScreenH});
    setVisible(false);
    // Stays out of spatial navigation until it is actually on screen.
    setFocusable(false);
    setTag("allAppsScreen");
}

void AllAppsScreen::setApps(std::vector<AppItem> apps) {
    const bool same = apps.size() == m_sourceApps.size() &&
        std::equal(apps.begin(), apps.end(), m_sourceApps.begin(),
                   [](const AppItem& a, const AppItem& b) {
                       return a.titleId == b.titleId && a.title == b.title;
                   });
    if (same) {
        for (auto& icon : m_icons)
            if (icon) applyIconTheme(*icon);
        return;
    }
    m_sourceApps = std::move(apps);
    for (auto& app : m_sourceApps) {
        app.sortKey = app.title;
        for (char& c : app.sortKey)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    m_index = 0;
    m_page = 0;
    applyOrder(false);
}

void AllAppsScreen::applyIconTheme(GlossyIcon& icon) {
    icon.setCornerRadius(m_iconCornerRadius >= 0.f
        ? m_iconCornerRadius
        : (m_theme ? m_theme->cellCornerRadius : 16.f));
    if (!m_theme)
        return;
    icon.setBaseColor(m_theme->iconDefault);
    icon.setBorderColor(m_theme->panelBorder);
    icon.setHighlightColor(m_theme->panelHighlight);
    icon.setLoadingColor(m_theme->cursorNormal);
    icon.setThemeMode(m_theme->mode);
}

void AllAppsScreen::setGridShape(int columns, int rows) {
    columns = std::clamp(columns, 3, 8);
    rows = std::clamp(rows, 2, 5);
    if (columns == m_cols && rows == m_rows)
        return;
    m_cols = columns;
    m_rows = rows;
    m_pageTextKey = -1;
    if (m_active) {
        const int per = perPage();
        m_page = std::clamp(m_index / per, 0, std::max(0, pageCount() - 1));
        m_index = std::clamp(m_index, 0, std::max(0, (int)m_apps.size() - 1));
        buildPage();
        syncFocusVisuals(true);
    }
}

void AllAppsScreen::setMovement(int motionStyle, int scrollSpeedPct,
                                int shrinkPct, int fadePct) {
    m_motionStyle = motion::clampStyle(motionStyle);
    m_scrollSpeedPct = std::clamp(scrollSpeedPct, 0, 100);
    m_shrinkPct = std::clamp(shrinkPct, 0, 100);
    m_fadePct = std::clamp(fadePct, 0, 100);
}

int AllAppsScreen::perPage() const {
    return std::max(1, m_cols * m_rows);
}

int AllAppsScreen::pageCount() const {
    return std::max(1, ((int)m_apps.size() + perPage() - 1) / perPage());
}

GlossyIcon* AllAppsScreen::focusedIcon() const {
    if (m_index < 0 || m_index >= (int)m_icons.size())
        return nullptr;
    return m_icons[(std::size_t)m_index].get();
}

nxui::Rect AllAppsScreen::gridArea() const {
    const shell::PageFrame frame = shell::pageFrame();
    return {frame.content.x, frame.content.y,
            frame.content.width, frame.content.height};
}

nxui::Rect AllAppsScreen::tileRect(int localIndex, float dx, float* outSize) const {
    const nxui::Rect area = gridArea();
    const float cellW = (area.width - (m_cols - 1) * kCellGap) / m_cols;
    const float cellH = (area.height - (m_rows - 1) * kCellGap) / m_rows;
    const float size = std::max(24.f, std::min(cellW, cellH));
    const int col = localIndex % m_cols;
    const int row = localIndex / m_cols;
    const float x = area.x + col * (cellW + kCellGap) + (cellW - size) * 0.5f + dx;
    const float y = area.y + row * (cellH + kCellGap) + (cellH - size) * 0.5f;
    if (outSize)
        *outSize = size;
    return {x, y, size, size};
}

float AllAppsScreen::slideDuration() const {
    const float speed = 0.5f + (float)m_scrollSpeedPct / 100.f;
    return motion::scaled(chrome::kDurPanel, m_motionStyle) / speed;
}

float AllAppsScreen::slideOffset() const {
    const float t = std::clamp(m_slide.value(), 0.f, 1.f);
    if (t >= 1.f)
        return 0.f;
    return (float)m_slideDir * (1.f - t) * gridArea().width * 0.14f;
}

float AllAppsScreen::focusScale() const {
    return 1.f + 0.08f * (float)m_shrinkPct / 100.f;
}

nxui::Rect AllAppsScreen::focusRingTargetRect() const {
    if (m_apps.empty() || m_pageIcons.empty() ||
        m_index < 0 || m_index >= (int)m_apps.size())
        return {};
    const int local = m_index - m_page * perPage();
    if (local < 0 || local >= (int)m_pageIcons.size())
        return {};
    float size = 0.f;
    nxui::Rect rect = tileRect(local, 0.f, &size);
    const float scale = focusScale();
    return rect.expanded((scale - 1.f) * size * 0.5f);
}

void AllAppsScreen::syncFocusVisuals(bool immediate) {
    if (m_focusWeights.size() != m_icons.size())
        m_focusWeights.resize(m_icons.size());
    const float duration = motion::scaled(chrome::kDurFast, m_motionStyle);
    for (int i = 0; i < (int)m_focusWeights.size(); ++i) {
        nxui::AnimatedFloat& weight = m_focusWeights[(std::size_t)i];
        const float target = i == m_index ? 1.f : 0.f;
        if (immediate || std::abs(weight.value() - target) < 0.001f)
            weight.setImmediate(target);
        else
            weight.set(target, duration, nxui::Easing::outCubic);
    }
    const nxui::Rect target = focusRingTargetRect();
    if (immediate)
        m_focusRingRect.setImmediate(target);
    else
        m_focusRingRect.set(target, duration, nxui::Easing::outCubic);
}

void AllAppsScreen::rebuildIcons() {
    m_icons.clear();
    m_textures.clear();
    m_texState.clear();
    m_loadQueue.clear();
    // Any decode still running belongs to the previous icon list. Dropping the
    // futures waits for them, which happens only on a rebuild (app list or
    // shape change), never on a page change.
    m_pendingDecodes.clear();
    m_icons.reserve(m_apps.size());
    m_textures.resize(m_apps.size());
    m_texState.assign(m_apps.size(), 0);
    for (const auto& app : m_apps) {
        auto icon = std::make_shared<GlossyIcon>();
        icon->setFont(m_font);
        icon->setTitle(app.title);
        icon->setTitleId(app.titleId);
        icon->setFocusable(false);
        icon->setTag("all_apps_icon");
        icon->setAccessibilityLabel(app.title);
        // GlossyIcon starts at appear-scale zero; HOME calls this after its
        // entry animation, and without it every tile draws at 0x0.
        icon->forceVisible();
        // Playtime belongs to the focused-app metadata strip. Keeping the pill
        // off the artwork lets the caption use the full tile width.
        icon->setShowPlaytimeBadge(false);
        icon->setArtworkFullBleed(true);
        icon->setTitleOverlay(true);
        icon->setInnerHairline(true);
        icon->setShadow(8.f, 0.16f);
        // Full-bleed artwork covers the glass body, so the liquid-glass shader
        // would only capture and shade an invisible surface.
        applyIconTheme(*icon);
        m_icons.push_back(std::move(icon));
    }
    if (m_apps.empty()) {
        m_index = 0;
        m_page = 0;
    } else {
        m_index = std::clamp(m_index, 0, (int)m_apps.size() - 1);
        m_page = std::clamp(m_page, 0, pageCount() - 1);
    }
    m_dirty = false;
    buildPage();
    syncFocusVisuals(true);
}

void AllAppsScreen::buildPage() {
    m_pageIcons.clear();
    const int start = m_page * perPage();
    const int end = std::min(start + perPage(), (int)m_apps.size());
    for (int i = start; i < end; ++i)
        m_pageIcons.push_back(i);
    queuePageTextures();
    m_pageTextKey = -1;
}

void AllAppsScreen::queuePageTextures() {
    if (!m_gpu || !m_ren)
        return;
    for (int index : m_pageIcons) {
        if (index < 0 || index >= (int)m_icons.size())
            continue;
        if (m_texState[(std::size_t)index] != 0)
            continue;
        auto& icon = m_icons[(std::size_t)index];
        if (!icon || icon->titleId() == 0)
            continue;
        m_texState[(std::size_t)index] = 1;
        m_loadQueue.push_back(index);
    }
}

void AllAppsScreen::startIconDecodes() {
    constexpr int kMaxConcurrentDecodes = 3;
    while ((int)m_pendingDecodes.size() < kMaxConcurrentDecodes &&
           !m_loadQueue.empty()) {
        const int index = m_loadQueue.front();
        m_loadQueue.pop_front();
        if (index < 0 || index >= (int)m_icons.size())
            continue;
        if (m_texState[(std::size_t)index] != 1)
            continue;
        const auto& icon = m_icons[(std::size_t)index];
        const std::uint64_t titleId = icon ? icon->titleId() : 0;
        if (titleId == 0) {
            m_texState[(std::size_t)index] = 4;
            continue;
        }
        m_texState[(std::size_t)index] = 2;
        PendingDecode pending;
        pending.index = index;
        IconDataLoader loader = m_iconDataLoader;
        pending.future = std::async(std::launch::async, [titleId, loader]() {
            std::vector<std::uint8_t> bytes;
            try {
                bytes = loader ? loader(titleId)
                               : AppListLoader::loadIconData(titleId);
            } catch (...) {
                // The tile stays blank rather than taking the grid down.
            }
            return IconStreamer::decodeIconData(bytes);
        });
        m_pendingDecodes.push_back(std::move(pending));
    }
}

void AllAppsScreen::pollIconDecodes() {
    if (!m_gpu || !m_ren)
        return;
    constexpr int kMaxUploadsPerFrame = 2;
    int uploads = 0;
    for (std::size_t i = 0;
         i < m_pendingDecodes.size() && uploads < kMaxUploadsPerFrame;) {
        auto& pending = m_pendingDecodes[i];
        if (pending.future.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            ++i;
            continue;
        }
        IconStreamer::DecodedIcon decoded;
        try {
            decoded = pending.future.get();
        } catch (...) {
            // Treated as a failed tile below.
        }
        const int index = pending.index;
        m_pendingDecodes.erase(m_pendingDecodes.begin() +
                               static_cast<std::ptrdiff_t>(i));
        if (index < 0 || index >= (int)m_icons.size() ||
            !m_icons[(std::size_t)index])
            continue;
        if (decoded.rgba.empty() || decoded.w <= 0 || decoded.h <= 0) {
            m_texState[(std::size_t)index] = 4;
            DebugLog::log("[allapps] no icon data tid=%016lX",
                          static_cast<unsigned long>(
                              m_icons[(std::size_t)index]->titleId()));
            continue;
        }
        auto tex = std::make_unique<nxui::Texture>();
        if (!tex->loadFromPixels(*m_gpu, *m_ren, decoded.rgba.data(),
                                 decoded.w, decoded.h)) {
            // A broken icon must not take the grid down; its tile keeps the
            // loading face and is not retried until the next rebuild.
            m_texState[(std::size_t)index] = 4;
            continue;
        }
        m_icons[(std::size_t)index]->setTexture(tex.get());
        m_textures[(std::size_t)index] = std::move(tex);
        m_texState[(std::size_t)index] = 3;
        ++uploads;
    }
}

void AllAppsScreen::show() {
    if (m_active && !m_animatingOut)
        return;
    m_active = true;
    m_animatingOut = false;
    m_menuOpen = false;
    if (m_dirty || m_icons.size() != m_apps.size())
        rebuildIcons();
    m_page = std::clamp(m_index / perPage(), 0, pageCount() - 1);
    if (m_lastFocusTitleId != 0) {
        for (int i = 0; i < (int)m_apps.size(); ++i) {
            if (m_apps[(std::size_t)i].titleId == m_lastFocusTitleId) {
                m_index = i;
                m_page = std::clamp(i / perPage(), 0, pageCount() - 1);
                break;
            }
        }
    }
    buildPage();
    syncFocusVisuals(true);
    m_slide.setImmediate(1.f);
    m_alpha.setImmediate(0.f);
    m_alpha.set(1.f, motion::scaled(chrome::kDurPanel, m_motionStyle),
                motion::ease(m_motionStyle));
    setVisible(true);
    setFocusable(true);
    setupActions();
}

void AllAppsScreen::hide() {
    if (!m_active || m_animatingOut)
        return;
    m_lastFocusTitleId = focusedTitleId();
    m_menuOpen = false;
    m_animatingOut = true;
    clearActions();
    if (m_closeSfxCb)
        m_closeSfxCb();
    m_alpha.set(0.f, motion::scaled(chrome::kDurBase, m_motionStyle),
                nxui::Easing::outCubic);
}

void AllAppsScreen::setupActions() {
    clearActions();
    addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        if (m_menuOpen) return;
        moveFocus(-1, 0);
    });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (m_menuOpen) return;
        moveFocus(1, 0);
    });
    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (m_menuOpen) { moveMenu(-1); return; }
        moveFocus(0, -1);
    });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        if (m_menuOpen) { moveMenu(1); return; }
        moveFocus(0, 1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (m_menuOpen) { activateMenuItem(); return; }
        if (!isOpen())
            return;
        GlossyIcon* icon = focusedIcon();
        if (!icon)
            return;
        if (m_activateSfxCb)
            m_activateSfxCb();
        if (m_launchCb)
            m_launchCb(icon);
    });
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (m_menuOpen) { closeMenu(); return; }
        hide();
    });
    addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (m_menuOpen)
            return;
        if (!isOpen())
            return;
        GlossyIcon* icon = focusedIcon();
        if (!icon)
            return;
        if (m_activateSfxCb)
            m_activateSfxCb();
        if (m_optionsCb)
            m_optionsCb(icon);
    });
    addAction(static_cast<uint64_t>(nxui::Button::Y), [this]() {
        if (!isOpen())
            return;
        if (m_menuOpen)
            closeMenu();
        else
            openMenu();
    });
    addAction(static_cast<uint64_t>(nxui::Button::L), [this]() {
        if (!m_menuOpen) stepPage(-1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::ZL), [this]() {
        if (!m_menuOpen) stepPage(-1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::R), [this]() {
        if (!m_menuOpen) stepPage(1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::ZR), [this]() {
        if (!m_menuOpen) stepPage(1);
    });
}

void AllAppsScreen::ensurePage(int page, bool animate, int direction) {
    page = std::clamp(page, 0, pageCount() - 1);
    if (page == m_page && animate) {
        // Same page: just replay the bump so the press still reads.
        m_slideDir = direction;
        m_slide.setImmediate(0.f);
        m_slide.set(1.f, slideDuration(), motion::inOutEase(m_motionStyle));
        return;
    }
    m_page = page;
    buildPage();
    if (animate) {
        m_slideDir = direction;
        m_slide.setImmediate(0.f);
        m_slide.set(1.f, slideDuration(), motion::inOutEase(m_motionStyle));
    } else {
        m_slide.setImmediate(1.f);
    }
}

void AllAppsScreen::stepPage(int dir) {
    if (!isOpen())
        return;
    const int target = std::clamp(m_page + dir, 0, pageCount() - 1);
    if (target == m_page)
        return;
    ensurePage(target, true, dir);
    m_index = std::min(target * perPage(), std::max(0, (int)m_apps.size() - 1));
    syncFocusVisuals(true);
    if (m_navSfxCb)
        m_navSfxCb();
}

void AllAppsScreen::moveFocus(int dx, int dy) {
    if (!isOpen() || m_apps.empty())
        return;
    const int per = perPage();
    const int local = std::clamp(m_index - m_page * per, 0, per - 1);
    int col = local % m_cols;
    int row = local / m_cols;

    int newCol = col + dx;
    int newRow = row + dy;
    if (newCol < 0) {
        if (m_page > 0) {
            ensurePage(m_page - 1, true, -1);
            m_index = std::min(m_page * per + per - 1, (int)m_apps.size() - 1);
            syncFocusVisuals(true);
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }
    if (newCol >= m_cols) {
        if (m_page + 1 < pageCount()) {
            ensurePage(m_page + 1, true, 1);
            m_index = m_page * per;
            syncFocusVisuals(true);
            if (m_navSfxCb) m_navSfxCb();
        }
        return;
    }
    newRow = std::clamp(newRow, 0, m_rows - 1);
    const int target = m_page * per + newRow * m_cols + newCol;
    if (target < (int)m_apps.size() && target != m_index) {
        m_index = target;
        syncFocusVisuals(false);
        if (m_navSfxCb)
            m_navSfxCb();
    }
}

void AllAppsScreen::update(float dt) {
    nxui::Widget::update(dt);
    m_focusRingTime += dt;
    m_alpha.update(dt);
    m_slide.update(dt);
    // The focus weights and ring use the shared AnimationManager, like the
    // rest of the menu's AnimatedProperty visuals.
    startIconDecodes();
    pollIconDecodes();
    for (int index : m_pageIcons) {
        if (index >= 0 && index < (int)m_icons.size() && m_icons[(std::size_t)index])
            m_icons[(std::size_t)index]->update(dt);
    }
    if (m_animatingOut && m_alpha.value() <= 0.01f) {
        m_animatingOut = false;
        m_active = false;
        setVisible(false);
        setFocusable(false);
        clearActions();
        if (m_closedCb)
            m_closedCb();
    }
}

void AllAppsScreen::setSortMode(int mode) {
    mode = std::clamp(mode, 0, 2);
    if (mode == m_sortMode)
        return;
    m_sortMode = mode;
    applyOrder(true);
}

void AllAppsScreen::setFavourites(std::vector<std::uint64_t> ids) {
    if (ids == m_favourites)
        return;
    m_favourites = std::move(ids);
    m_favouriteSet.clear();
    m_favouriteSet.insert(m_favourites.begin(), m_favourites.end());
    if (m_sortMode == 2)
        applyOrder(true);
}

void AllAppsScreen::setRecentOrder(std::vector<std::uint64_t> ids) {
    if (ids == m_recentOrder)
        return;
    m_recentOrder = std::move(ids);
    if (m_sortMode == 1)
        applyOrder(true);
}

bool AllAppsScreen::isFavourite(std::uint64_t titleId) const {
    return m_favouriteSet.find(titleId) != m_favouriteSet.end();
}

std::uint64_t AllAppsScreen::focusedTitleId() const {
    if (m_index < 0 || m_index >= (int)m_apps.size())
        return 0;
    return m_apps[(std::size_t)m_index].titleId;
}

void AllAppsScreen::setIconCornerRadius(float radius) {
    m_iconCornerRadius = std::max(0.f, radius);
    for (auto& icon : m_icons) {
        if (icon)
            icon->setCornerRadius(m_iconCornerRadius);
    }
}

void AllAppsScreen::applyOrder(bool preserveFocus) {
    const std::uint64_t focusId = preserveFocus ? focusedTitleId() : 0;
    m_apps.clear();
    m_apps.reserve(m_sourceApps.size());
    for (const auto& app : m_sourceApps) {
        if (m_sortMode == 2 && !isFavourite(app.titleId))
            continue;
        m_apps.push_back(app);
    }

    if (m_sortMode == 1) {
        std::unordered_map<std::uint64_t, int> rank;
        rank.reserve(m_recentOrder.size());
        for (int i = 0; i < (int)m_recentOrder.size(); ++i)
            rank.emplace(m_recentOrder[(std::size_t)i], i);
        std::stable_sort(m_apps.begin(), m_apps.end(),
                         [&](const AppItem& a, const AppItem& b) {
            const auto ra = rank.find(a.titleId);
            const auto rb = rank.find(b.titleId);
            const int va = ra == rank.end() ? INT_MAX : ra->second;
            const int vb = rb == rank.end() ? INT_MAX : rb->second;
            if (va != vb)
                return va < vb;
            return a.sortKey < b.sortKey;
        });
    } else {
        std::stable_sort(m_apps.begin(), m_apps.end(),
                         [&](const AppItem& a, const AppItem& b) {
            return a.sortKey < b.sortKey;
        });
    }

    if (m_apps.empty()) {
        m_index = 0;
        m_page = 0;
    } else {
        m_index = 0;
        if (focusId != 0) {
            for (int i = 0; i < (int)m_apps.size(); ++i) {
                if (m_apps[(std::size_t)i].titleId == focusId) {
                    m_index = i;
                    break;
                }
            }
        }
        m_page = std::clamp(m_index / perPage(), 0, pageCount() - 1);
    }
    m_dirty = true;
    if (m_active)
        rebuildIcons();
}

void AllAppsScreen::openMenu() {
    if (!isOpen())
        return;
    m_menuOpen = true;
    m_menuIndex = std::clamp(m_sortMode, 0, 3);
    if (m_navSfxCb)
        m_navSfxCb();
}

void AllAppsScreen::closeMenu() {
    if (!m_menuOpen)
        return;
    m_menuOpen = false;
    if (m_navSfxCb)
        m_navSfxCb();
}

void AllAppsScreen::moveMenu(int delta) {
    if (!m_menuOpen)
        return;
    const int next = std::clamp(m_menuIndex + delta, 0, 3);
    if (next != m_menuIndex) {
        m_menuIndex = next;
        if (m_navSfxCb)
            m_navSfxCb();
    }
}

void AllAppsScreen::toggleFocusedFavourite() {
    const std::uint64_t titleId = focusedTitleId();
    if (titleId == 0)
        return;
    const bool nowFavourite = !isFavourite(titleId);
    if (nowFavourite) {
        m_favourites.push_back(titleId);
        m_favouriteSet.insert(titleId);
    } else {
        m_favourites.erase(
            std::remove(m_favourites.begin(), m_favourites.end(), titleId),
            m_favourites.end());
        m_favouriteSet.erase(titleId);
    }
    if (m_activateSfxCb)
        m_activateSfxCb();
    if (m_favouriteCb)
        m_favouriteCb(titleId, nowFavourite);
    if (m_sortMode == 2)
        applyOrder(true);
}

void AllAppsScreen::activateMenuItem() {
    if (!m_menuOpen)
        return;
    if (m_menuIndex >= 3) {
        toggleFocusedFavourite();
        return;
    }
    const int mode = m_menuIndex;
    m_menuOpen = false;
    if (m_activateSfxCb)
        m_activateSfxCb();
    setSortMode(mode);
    if (m_sortModeCb)
        m_sortModeCb(m_sortMode);
}

void AllAppsScreen::drawSortMenu(nxui::Renderer& ren, float alpha) {
    auto& i18n = nxui::I18n::instance();
    constexpr float panelW = 344.f;
    constexpr float rowH = 44.f;
    constexpr float headerH = 52.f;
    constexpr float pad = 12.f;
    const float panelH = headerH + rowH * 4.f + pad;
    const nxui::Rect panel{kScreenW - kSidePad - panelW,
                           kScreenH - kFooterH - kMetaH - panelH - 10.f,
                           panelW, panelH};
    ren.drawRect({0.f, 0.f, kScreenW, kScreenH},
                 nxui::Color{0.f, 0.f, 0.f, chrome::kScrimPage * alpha});
    ren.drawRoundedRect(panel.expanded(8.f),
                        nxui::Color{0.f, 0.f, 0.f, 0.10f * alpha},
                        chrome::kRadiusCard + 2.f);

    const nxui::Color panelColor = m_theme
        ? m_theme->panelBase.withAlpha(chrome::kModalAlpha * alpha)
        : nxui::Color{0.07f, 0.09f, 0.13f, chrome::kModalAlpha * alpha};
    ren.drawRoundedRect(panel, panelColor, chrome::kRadiusCard);
    if (m_theme)
        ren.drawRoundedRectOutline(panel,
                                   m_theme->panelBorder.withAlpha(chrome::kModalRim * alpha),
                                   chrome::kRadiusCard, 1.f);

    const nxui::Color primary = m_theme
        ? m_theme->textPrimary.withAlpha(alpha) : nxui::Color::white().withAlpha(alpha);
    const nxui::Color secondary = m_theme
        ? m_theme->textSecondary.withAlpha(0.82f * alpha)
        : nxui::Color{0.8f, 0.82f, 0.9f, 0.82f * alpha};
    const nxui::Color accent = m_theme
        ? m_theme->cursorNormal.withAlpha(alpha) : nxui::Color::white().withAlpha(alpha);

    if (m_font)
        ren.drawText(i18n.tr("allapps.sort_title", "Sort & filter"),
                     {panel.x + 18.f, panel.y + 15.f}, m_font, primary,
                     chrome::kTypeRow);
    if (!m_smallFont)
        return;

    const std::uint64_t focusId = focusedTitleId();
    const bool favourite = focusId != 0 && isFavourite(focusId);
    const std::string labels[4] = {
        i18n.tr("allapps.sort_az", "A-Z"),
        i18n.tr("allapps.sort_recent", "Recently played"),
        i18n.tr("allapps.sort_favourites", "Favourites"),
        favourite ? i18n.tr("allapps.fav_remove", "Remove from favourites")
                  : i18n.tr("allapps.fav_add", "Add to favourites"),
    };

    for (int i = 0; i < 4; ++i) {
        const float y = panel.y + headerH + (float)i * rowH;
        const nxui::Rect row{panel.x + 10.f, y, panelW - 20.f, rowH - 6.f};
        if (i == 3)
            ren.drawRect({panel.x + 16.f, y - 3.f, panelW - 32.f, 1.f},
                         secondary.withAlpha(0.12f * alpha));
        if (i == m_menuIndex)
            ren.drawRoundedRect(row, accent.withAlpha(0.13f * alpha),
                                chrome::kRadiusControl);
        const float cy = row.y + row.height * 0.5f;
        if (i < 3) {
            ren.drawCircle({row.x + 18.f, cy}, 7.f,
                           secondary.withAlpha(0.32f * alpha), 14);
            if (i == m_sortMode)
                ren.drawCircle({row.x + 18.f, cy}, 3.5f,
                               accent.withAlpha(0.95f * alpha), 12);
        } else {
            drawStar(ren, {row.x + 18.f, cy}, 7.f,
                     (favourite ? accent : secondary)
                         .withAlpha((favourite ? 0.95f : 0.35f) * alpha));
        }
        const auto size = m_smallFont->measure(labels[i]);
        ren.drawText(labels[i],
                     {row.x + 38.f,
                      row.y + (row.height - size.y * chrome::kTypeRow) * 0.5f},
                     m_smallFont, i == m_menuIndex ? primary : secondary,
                     chrome::kTypeRow);
    }
}

void AllAppsScreen::drawBackgroundChrome(nxui::Renderer& ren, float alpha) {
    // Fully opaque at rest: the HOME grid behind must not bleed through.
    const nxui::Color backdrop = (m_theme
        ? m_theme->background
        : nxui::Color{0.04f, 0.05f, 0.08f, 1.f}).withAlpha(alpha);
    ren.drawRect({0.f, 0.f, kScreenW, kScreenH}, backdrop);

    const nxui::Color accent = m_theme
        ? m_theme->backgroundAccent
        : nxui::Color{0.24f, 0.48f, 0.85f, 1.f};
    // Depth: a faint lift behind the wall and a soft floor under it. Both are
    // painted before the tiles, so they never tint the artwork.
    const nxui::Rect wall{kSidePad - 14.f, kHeaderH - 10.f,
                          kScreenW - (kSidePad - 14.f) * 2.f,
                          gridArea().height + 26.f};
    ren.drawRoundedRect(wall, nxui::Color(1.f, 1.f, 1.f, 0.024f * alpha),
                        chrome::kRadiusPanel + 8.f);
    ren.drawRoundedRectOutline(
        wall,
        (m_theme ? m_theme->panelBorder : nxui::Color(1.f, 1.f, 1.f, 0.2f))
            .withAlpha(0.06f * alpha),
        chrome::kRadiusPanel + 8.f, 1.f);
    ren.drawGradientRect({0.f, 0.f, kScreenW, 170.f},
                         nxui::Color(0.f, 0.f, 0.f, 0.26f * alpha),
                         nxui::Color(0.f, 0.f, 0.f, 0.f));
    // A very light brand wash keeps the header from reading as a flat black
    // band without competing with the artwork below it.
    ren.drawGradientRect({0.f, 0.f, kScreenW, 190.f},
                         accent.withAlpha(0.035f * alpha),
                         accent.withAlpha(0.f));
    ren.drawGradientRect({kSidePad, gridArea().y, gridArea().width,
                          gridArea().height},
                         nxui::Color(0.f, 0.f, 0.f, 0.10f * alpha),
                         nxui::Color(0.f, 0.f, 0.f, 0.f));
    ren.drawGradientRect({kSidePad,
                          gridArea().bottom() - gridArea().height * 0.26f,
                          gridArea().width, gridArea().height * 0.26f},
                         nxui::Color(0.f, 0.f, 0.f, 0.f),
                         nxui::Color(0.f, 0.f, 0.f, 0.12f * alpha));
    ren.drawGradientRect({0.f, kScreenH - kFooterH - 10.f, kScreenW,
                          kFooterH + 10.f},
                         nxui::Color(0.f, 0.f, 0.f, 0.f),
                         nxui::Color(0.f, 0.f, 0.f, 0.22f * alpha));
}

void AllAppsScreen::drawForegroundChrome(nxui::Renderer& ren, float alpha) {
    auto& i18n = nxui::I18n::instance();
    const nxui::Color primary = m_theme
        ? m_theme->textPrimary.withAlpha(alpha) : nxui::Color::white().withAlpha(alpha);
    const nxui::Color secondary = m_theme
        ? m_theme->textSecondary.withAlpha(0.82f * alpha)
        : nxui::Color{0.8f, 0.82f, 0.9f, 0.82f * alpha};
    const nxui::Color accent = m_theme
        ? m_theme->cursorNormal.withAlpha(alpha) : nxui::Color::white().withAlpha(alpha);

    constexpr float kTitleScale = chrome::kTypeHeader;
    const float titleX = kSidePad + 16.f;
    float titleW = 0.f;
    if (m_font) {
        const std::string title = i18n.tr("allapps.title", "All Apps");
        ren.drawText(title, {titleX, 21.f}, m_font, primary, kTitleScale);
        titleW = m_font->measure(title).x * kTitleScale;
    }
    // Small vertical accent anchors the title and echoes the focus ring.
    ren.drawRoundedRect({kSidePad, 27.f, 3.5f, 30.f},
                        accent.withAlpha(0.88f * alpha), 1.75f);
    if (m_smallFont) {
        const int pages = pageCount();
        std::string count = std::to_string(m_apps.size()) + " " +
            i18n.tr("allapps.apps", "apps");
        if (pages > 1)
            count += " · " + std::to_string(pages) + " " +
                     i18n.tr("allapps.pages", "pages");
        constexpr float kCountScale = chrome::kTypeSection;
        const nxui::Vec2 countSize = m_smallFont->measure(count);
        const float titleH = m_font ? m_font->measure("Ag").y * kTitleScale
                                    : countSize.y * kCountScale;
        ren.drawText(count,
                     {titleX + titleW + 16.f,
                      21.f + (titleH - countSize.y * kCountScale) * 0.5f},
                     m_smallFont, secondary, kCountScale);

        const std::string chip = i18n.tr("allapps.sorted", "Sorted") + "  " +
                                 sortName(m_sortMode);
        constexpr float kChipScale = chrome::kTypeSection;
        const nxui::Vec2 chipSize = m_smallFont->measure(chip);
        const float chipW = chipSize.x * kChipScale + 42.f;
        const nxui::Rect chipRect{kScreenW - kSidePad - chipW, 22.f, chipW, 28.f};
        ren.drawRoundedRect(chipRect, accent.withAlpha(0.12f * alpha),
                            chrome::kRadiusRow);
        ren.drawRoundedRectOutline(chipRect, accent.withAlpha(0.28f * alpha),
                                   chrome::kRadiusRow, 1.f);
        ren.drawText(chip,
                     {chipRect.x + 14.f,
                      chipRect.y + (chipRect.height - chipSize.y * kChipScale) * 0.5f},
                     m_smallFont, accent.withAlpha(0.92f * alpha), kChipScale);
        const float chevX = chipRect.right() - 13.f;
        const float chevY = chipRect.y + chipRect.height * 0.5f;
        ren.drawTriangle({chevX - 3.5f, chevY - 1.5f},
                         {chevX + 3.5f, chevY - 1.5f},
                         {chevX, chevY + 2.5f},
                         accent.withAlpha(0.85f * alpha));
    }

    // Header rule and a short accent lead-in make the wall read as a deliberate
    // layer instead of tiles floating on the global background.
    shell::drawHeaderRule(ren, shell::pageFrame().header,
                          secondary, accent, alpha);

    if (!m_smallFont)
        return;

    drawMetadata(ren, alpha);

    const float footerY = kScreenH - kFooterH + 12.f;
    ren.drawRect({kSidePad, footerY - 8.f, kScreenW - 2.f * kSidePad, 1.f},
                 secondary.withAlpha(0.06f * alpha));
    std::vector<chrome::Hint> hints;
    if (m_menuOpen) {
        hints = {{i18n.tr("hint.key_a", "A"), i18n.tr("hint.select", "Select")},
                 {i18n.tr("hint.key_b", "B"), i18n.tr("hint.close", "Close")}};
    } else {
        hints = {
            {i18n.tr("hint.key_a", "A"), i18n.tr("hint.launch", "Launch")},
            {i18n.tr("hint.key_x", "X"), i18n.tr("hint.options", "Game options")},
            {i18n.tr("hint.key_y", "Y"), i18n.tr("hint.sort", "Sort")},
            {i18n.tr("hint.key_b", "B"), i18n.tr("hint.back", "Back")},
            {i18n.tr("hint.key_lr", "L/R"), i18n.tr("hint.page", "Page")},
        };
    }
    chrome::drawHints(ren, m_smallFont, hints, kSidePad, footerY, secondary,
                      alpha, kScreenW - kSidePad * 2.f - 120.f);

    const int pages = pageCount();
    if (pages > 1 && pages <= 8) {
        constexpr float slotW = 10.f;
        constexpr float gap = 8.f;
        const float total = pages * slotW + (pages - 1) * gap;
        float dotX = (kScreenW - total) * 0.5f;
        const float dotY = footerY + 10.f;
        for (int i = 0; i < pages; ++i) {
            const bool active = i == m_page;
            if (active) {
                ren.drawRoundedRect({dotX, dotY - 2.f, slotW, 4.f},
                                    accent.withAlpha(0.95f * alpha),
                                    chrome::kRadiusPill);
            } else {
                ren.drawCircle({dotX + slotW * 0.5f, dotY}, 2.2f,
                               secondary.withAlpha(0.32f * alpha), 12);
            }
            dotX += slotW + gap;
        }
    }

    if (m_pageTextKey != m_page) {
        m_pageTextKey = m_page;
        m_pageText = std::to_string(m_page + 1) + " / " + std::to_string(pages);
    }
    constexpr float kPageScale = chrome::kTypeSection;
    const float pageW = m_smallFont->measure(m_pageText).x * kPageScale;
    ren.drawText(m_pageText, {kScreenW - kSidePad - pageW, footerY + 4.f},
                 m_smallFont, secondary, kPageScale);
}

void AllAppsScreen::drawMetadata(nxui::Renderer& ren, float alpha) {
    if (m_apps.empty() || !m_font || !m_smallFont)
        return;
    if (m_index < 0 || m_index >= (int)m_apps.size())
        return;
    const auto& app = m_apps[(std::size_t)m_index];
    const nxui::Color primary = m_theme
        ? m_theme->textPrimary.withAlpha(alpha) : nxui::Color::white().withAlpha(alpha);
    const nxui::Color secondary = m_theme
        ? m_theme->textSecondary.withAlpha(0.82f * alpha)
        : nxui::Color{0.8f, 0.82f, 0.9f, 0.82f * alpha};
    const nxui::Color accent = m_theme
        ? m_theme->cursorNormal.withAlpha(alpha) : nxui::Color::white().withAlpha(alpha);
    const float metaY = kScreenH - kFooterH - kMetaH - 2.f;
    const nxui::Rect metaPanel{kSidePad, gridArea().bottom() + 7.f,
                               kScreenW - kSidePad * 2.f, kMetaH + 6.f};
    ren.drawRoundedRect(metaPanel,
                        (m_theme ? m_theme->panelBase
                                 : nxui::Color{0.07f, 0.09f, 0.13f, 1.f})
                            .withAlpha(0.38f * alpha),
                        chrome::kRadiusRow);
    ren.drawRoundedRectOutline(
        metaPanel,
        (m_theme ? m_theme->panelBorder : nxui::Color(1.f, 1.f, 1.f, 0.2f))
            .withAlpha(0.14f * alpha),
        chrome::kRadiusRow, 1.f);
    ren.drawRoundedRect({kSidePad + 12.f, metaY + 7.f, 3.f, kMetaH - 14.f},
                        accent.withAlpha(0.85f * alpha), 1.5f);

    const std::uint64_t seconds = switchu::playtime::secondsFor(app.titleId);
    const std::string played = seconds >= 60ULL
        ? nxui::I18n::instance().tr("allapps.played", "Played") + " " +
              formatPlaytime(seconds)
        : nxui::I18n::instance().tr("allapps.not_played", "Not played");
    constexpr float kPlayScale = chrome::kTypeSection;
    const nxui::Vec2 playSize = m_smallFont->measure(played);
    const float playW = playSize.x * kPlayScale;
    const float right = kScreenW - kSidePad - 14.f;
    ren.drawText(played,
                 {right - playW,
                  metaY + (kMetaH - playSize.y * kPlayScale) * 0.5f},
                 m_smallFont, secondary, kPlayScale);

    constexpr float kTitleScale = chrome::kTypeSection;
    const float titleMax = right - playW - (kSidePad + 24.f) - 20.f;
    const std::string title = text::ellipsize(m_font, app.title, titleMax,
                                               kTitleScale);
    if (!title.empty()) {
        const nxui::Vec2 titleSize = m_font->measure(title);
        ren.drawText(title,
                     {kSidePad + 24.f,
                      metaY + (kMetaH - titleSize.y * kTitleScale) * 0.5f},
                     m_font, primary, kTitleScale);
    }
}

void AllAppsScreen::drawEmptyState(nxui::Renderer& ren, float alpha) {
    auto& i18n = nxui::I18n::instance();
    const nxui::Rect area = gridArea();
    const float cx = area.x + area.width * 0.5f;
    const float cy = area.y + area.height * 0.40f;
    constexpr float kBox = 104.f;
    const nxui::Rect art{cx - kBox * 0.5f, cy - kBox * 0.5f, kBox, kBox};
    const nxui::Color border = m_theme ? m_theme->panelBorder
                                       : nxui::Color(1.f, 1.f, 1.f, 0.2f);
    const nxui::Color glyph = m_theme ? m_theme->textSecondary
                                      : nxui::Color::white();
    ren.drawRoundedRect(art, nxui::Color(1.f, 1.f, 1.f, 0.022f * alpha),
                        chrome::kRadiusPanel);
    ren.drawRoundedRectOutline(art, border.withAlpha(0.22f * alpha),
                               chrome::kRadiusPanel, 1.5f);
    constexpr float cell = 28.f;
    constexpr float gap = 7.f;
    const float gridW = cell * 2.f + gap;
    const float gx = art.x + (kBox - gridW) * 0.5f;
    const float gy = art.y + (kBox - gridW) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        ren.drawRoundedRect({gx + (i % 2) * (cell + gap),
                             gy + (i / 2) * (cell + gap), cell, cell},
                            glyph.withAlpha(0.13f * alpha),
                            chrome::kRadiusControl);
    }

    const bool favouritesEmpty = m_sortMode == 2 && !m_sourceApps.empty();
    const std::string title = favouritesEmpty
        ? i18n.tr("allapps.favourites_empty_title", "No favourites yet")
        : i18n.tr("allapps.empty_title", "No apps installed");
    const std::string hint = favouritesEmpty
        ? i18n.tr("allapps.favourites_empty_hint",
                  "Press Y, switch to A-Z or Recent, then add apps to favourites.")
        : i18n.tr("allapps.empty_hint",
                  "Installed games and apps will appear here.");
    const nxui::Color primary = m_theme
        ? m_theme->textPrimary.withAlpha(alpha) : nxui::Color::white().withAlpha(alpha);
    const nxui::Color secondary = m_theme
        ? m_theme->textSecondary.withAlpha(0.82f * alpha)
        : nxui::Color{0.8f, 0.82f, 0.9f, 0.82f * alpha};
    if (m_font) {
        constexpr float kTitleScale = chrome::kTypeTitle;
        const nxui::Vec2 size = m_font->measure(title);
        ren.drawText(title, {cx - size.x * kTitleScale * 0.5f, art.bottom() + 18.f},
                     m_font, primary, kTitleScale);
    }
    if (m_smallFont) {
        constexpr float kHintScale = chrome::kTypeRow;
        const nxui::Vec2 size = m_smallFont->measure(hint);
        const float titleH = m_font ? m_font->measure(title).y * chrome::kTypeTitle
                                    : 0.f;
        ren.drawText(hint,
                     {cx - size.x * kHintScale * 0.5f,
                      art.bottom() + 22.f + titleH + 8.f},
                     m_smallFont, secondary, kHintScale);
    }
}

void AllAppsScreen::drawFocusRing(nxui::Renderer& ren, float alpha, float dx) {
    if (m_apps.empty() || m_pageIcons.empty())
        return;
    nxui::Rect rect = m_focusRingRect.value();
    if (rect.width <= 1.f || rect.height <= 1.f)
        return;
    rect.x += dx;
    const nxui::Color ring = m_theme
        ? m_theme->cursorNormal
        : nxui::Color::white();
    const float iconRadius = m_iconCornerRadius >= 0.f
        ? m_iconCornerRadius
        : (m_theme ? m_theme->cellCornerRadius : chrome::kRadiusRow);
    chrome::drawFocusRing(ren, rect, iconRadius, alpha, m_focusRingTime, ring);
}

void AllAppsScreen::render(nxui::Renderer& ren) {
    if (!isVisible() || opacity() <= 0.f)
        return;
    const float alpha = m_alpha.value() * opacity();
    if (alpha <= 0.001f)
        return;
    drawBackgroundChrome(ren, alpha);

    if (m_apps.empty()) {
        drawEmptyState(ren, alpha);
        drawForegroundChrome(ren, alpha);
        if (m_menuOpen)
            drawSortMenu(ren, alpha);
        return;
    }

    const float dx = slideOffset();
    const float slideT = std::clamp(m_slide.value(), 0.f, 1.f);
    const float pageFade = 0.82f + 0.18f * slideT;
    for (int local = 0; local < (int)m_pageIcons.size(); ++local) {
        const int index = m_pageIcons[(std::size_t)local];
        if (index < 0 || index >= (int)m_icons.size() || !m_icons[(std::size_t)index])
            continue;
        float size = 0.f;
        nxui::Rect rect = tileRect(local, dx, &size);
        float focusWeight = 0.f;
        if (index < (int)m_focusWeights.size())
            focusWeight = std::clamp(m_focusWeights[(std::size_t)index].value(),
                                     0.f, 1.f);
        if (focusWeight > 0.001f) {
            const float scale = 1.f + (focusScale() - 1.f) * focusWeight;
            rect = rect.expanded((scale - 1.f) * size * 0.5f);
        }
        auto& icon = m_icons[(std::size_t)index];
        icon->setRect(rect);
        // Every tile stays fully opaque. Dimming the unfocused ones made the
        // translucent glass base tint the artwork on the whole wall.
        icon->setOpacity(alpha * pageFade);
        icon->render(ren);
        if (index < (int)m_apps.size() && isFavourite(m_apps[(std::size_t)index].titleId)) {
            const float starR = std::clamp(size * 0.052f, 5.f, 8.f);
            const nxui::Vec2 starPos{rect.right() - starR - 9.f,
                                     rect.y + starR + 9.f};
            ren.drawCircle(starPos, starR + 3.5f,
                           nxui::Color(0.f, 0.f, 0.f, 0.35f * alpha * pageFade), 16);
            drawStar(ren, starPos, starR,
                     nxui::Color(1.f, 0.82f, 0.38f, 0.95f * alpha * pageFade));
        }
    }
    drawFocusRing(ren, alpha * pageFade, dx);
    drawForegroundChrome(ren, alpha);
    if (m_menuOpen)
        drawSortMenu(ren, alpha);
}