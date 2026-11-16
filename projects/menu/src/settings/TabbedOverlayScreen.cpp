#include "TabbedOverlayScreen.hpp"
#include "core/FrameProfiler.hpp"
#include "core/UiTokens.hpp"
#include "SettingItemWidgets.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/widgets/GlassBox.hpp>
#include <nxui/widgets/Label.hpp>
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include "ui/ChromeTokens.hpp"
#include "ui/DrawChrome.hpp"
#include "ui/TextFit.hpp"

namespace chrome = switchu::ui::chrome;

static constexpr float kSettingsBlurRadius = 6.0f;
static constexpr int kSettingsBlurIter = 1;

namespace {

static constexpr float kTabRailInset = 14.f;
static constexpr float kTabCardGap = 10.f;
static constexpr float kContentCardInsetX = 8.f;
static constexpr float kContentCardInsetY = 8.f;

constexpr size_t kWrapHeightCacheLimit = 256;
std::unordered_map<std::string, float> g_wrapHeightCache;

std::string wrapHeightCacheKey(nxui::Font* font, const std::string& label, float labelWidth) {
    return std::to_string((std::uintptr_t)font)
         + "\n" + std::to_string(font ? font->revision() : 0)
         + "\n" + std::to_string((int)std::lround(labelWidth * 4.f))
         + "\n" + label;
}

class SettingsTabWidget final : public nxui::GlassBox {
public:
    explicit SettingsTabWidget(const std::string& text)
        : nxui::GlassBox(nxui::Axis::ROW) {
        setCornerRadius(chrome::kRadiusRow);
        setBorderWidth(1.f);
        setWireframeEnabled(false);

        m_label = std::make_shared<nxui::Label>(text);
        m_label->setHAlign(nxui::Label::HAlign::Left);
        m_label->setVAlign(nxui::Label::VAlign::Center);
        addChild(m_label);
    }

    void sync(const std::string& text,
              nxui::Font* font,
              const nxui::Theme* theme,
              bool selected,
              bool focused,
              float uiTime,
              float accentWidth) {
        (void)uiTime;
        m_selected = selected;
        m_focused = focused;
        m_accentWidth = accentWidth;
        m_accentColor = theme ? theme->cursorNormal : nxui::Color::white();

        if (font != m_cachedFont) {
            m_cachedFont = font;
            m_cachedWidth = -1.f;
            if (font) {
                m_label->setFont(font);
            }
        }
        if (m_cachedText != text) {
            m_cachedText = text;
            m_cachedDisplay.clear();
            m_cachedWidth = -1.f;
            if (text.empty())
                m_label->setText("");
        }

        // Every tab uses the same two sizes (selected / normal) and long labels
        // ellipsize, instead of shrinking some rows to an unreadable 0.55.
        const float textScale = chrome::kTypeTitle;
        if (std::abs(m_cachedTextScale - textScale) > 0.001f) {
            m_cachedTextScale = textScale;
            m_label->setScale(textScale);
            m_cachedWidth = -1.f;
        }
        if (!m_cachedText.empty()
            && (m_cachedDisplay.empty() || std::abs(m_cachedWidth - rect().width) > 0.25f)) {
            m_cachedWidth = rect().width;
            std::string shown = m_cachedText;
            if (font && rect().width > 52.f) {
                const float maxW = (rect().width - 52.f) / textScale;
                if (font->measure(shown).x > maxW) {
                    while (!shown.empty()
                           && font->measure(shown + "\xE2\x80\xA6").x > maxW)
                        shown.pop_back();
                    shown += "\xE2\x80\xA6";
                }
            }
            m_cachedDisplay = std::move(shown);
            m_label->setText(m_cachedDisplay);
        }
        m_label->setOpacity(opacity());
        m_label->setRect({rect().x + 22.f, rect().y,
                          std::max(0.f, rect().width - 44.f), rect().height});

        setBorderWidth(0.f);
        setScale(1.f);
        // Restyling a plate recomputes theme colours and marks layout dirty.
        // The rail syncs every visible row each frame, so only touch those
        // properties when the selection, focus or theme really changed.
        if (theme && (theme != m_cachedStyleTheme
                      || selected != m_cachedSelectedStyle
                      || focused != m_cachedFocusedStyle)) {
            m_cachedStyleTheme = theme;
            m_cachedSelectedStyle = selected;
            m_cachedFocusedStyle = focused;
            nxui::Color textColor = selected ? theme->textPrimary : theme->textSecondary;
            // The rail is a flat list, not a stack of cards. Only the selected
            // row gets a plate; the focus ring is the sole focus cue.
            nxui::Color baseColor = selected
                ? theme->panelBase.withAlpha(focused ? 0.74f : 0.66f)
                : nxui::Color(0.f, 0.f, 0.f, 0.f);
            nxui::Color borderColor = nxui::Color(0.f, 0.f, 0.f, 0.f);
            nxui::Color hiColor = selected
                ? theme->panelHighlight.withAlpha(chrome::kSelectionWash * 0.6f)
                : nxui::Color(0.f, 0.f, 0.f, 0.f);

            setBaseColor(baseColor);
            setBorderColor(borderColor);
            setHighlightColor(hiColor);

            m_label->setTextColor(textColor);
        }
    }

protected:
    void onRender(nxui::Renderer& ren) override {
        nxui::GlassBox::onRender(ren);

        if (!m_selected || opacity() <= 0.01f)
            return;

        nxui::Rect r = rect();
        float accentH = std::max(14.f, r.height * 0.34f);
        float accentY = r.y + (r.height - accentH) * 0.5f;
        float accentW = std::clamp(m_accentWidth, 2.f, std::max(2.f, r.width * 0.12f));
        nxui::Rect accent = {r.x + 8.f, accentY, accentW, accentH};
        ren.drawRoundedRect(accent,
                            m_accentColor.withAlpha(chrome::kSelectionBar * opacity()), 2.f);
    }

private:
    std::shared_ptr<nxui::Label> m_label;
    bool m_selected = false;
    bool m_focused = false;
    float m_accentWidth = 3.f;
    nxui::Color m_accentColor = nxui::Color::white();
    nxui::Font* m_cachedFont = nullptr;
    std::string m_cachedText;
    float m_cachedTextScale = -1.f;
    float m_cachedWidth = -1.f;
    float m_cachedMeasured = 0.f;
    std::string m_cachedDisplay;
    const nxui::Theme* m_cachedStyleTheme = nullptr;
    bool m_cachedSelectedStyle = false;
    bool m_cachedFocusedStyle = false;
};

class SettingsItemCard final : public nxui::GlassBox {
public:
    SettingsItemCard(TabbedOverlayScreen::SettingItem& item,
                     std::shared_ptr<nxui::Box> content,
                     bool flat)
        : nxui::GlassBox(nxui::Axis::ROW)
        , m_item(item)
        , m_content(std::move(content))
        , m_flat(flat) {
        setAlignItems(nxui::AlignItems::CENTER);
        setJustifyContent(nxui::JustifyContent::FLEX_START);
        setWireframeEnabled(false);
        addChild(m_content);
    }

    void sync(const nxui::Theme* theme, bool selected, float alpha) {
        const bool isSection = (m_item.type == TabbedOverlayScreen::ItemType::Section);
        m_theme = theme;
        m_selected = selected && !isSection;

        // Rows are re-synced every frame while scrolling. The visual state only
        // changes on focus, theme or reveal-animation steps, so skip the colour
        // and radius setters when none of those moved. Content rects still
        // update below because the row itself moves.
        const bool styleChanged = theme != m_cachedStyleTheme
            || m_selected != m_cachedStyleSelected
            || std::abs(alpha - m_cachedStyleAlpha) > 0.001f;
        if (styleChanged) {
            m_cachedStyleTheme = theme;
            m_cachedStyleSelected = m_selected;
            m_cachedStyleAlpha = alpha;

            if (m_flat) {
                // Flat list: rows are separated by a hairline instead of each being
                // boxed. Only the focused row gets a wash and an accent edge.
                setCornerRadius(isSection ? chrome::kRadiusControl : chrome::kRadiusRow);
                setBorderWidth(0.f);
                setScale(1.f);
                setBaseColor(m_selected && theme
                    ? theme->cursorNormal.withAlpha(chrome::kSelectionWash * alpha)
                    : nxui::Color(0.f, 0.f, 0.f, 0.f));
                setBorderColor(nxui::Color(0.f, 0.f, 0.f, 0.f));
                setHighlightColor(nxui::Color(0.f, 0.f, 0.f, 0.f));
            } else {
                const bool isActionLike = m_item.type == TabbedOverlayScreen::ItemType::Action
                    || m_item.type == TabbedOverlayScreen::ItemType::Selector;

                setCornerRadius(isSection ? chrome::kRadiusRow : chrome::kRadiusCard);
                setBorderWidth(isSection ? 0.f : 1.f);
                setScale(selected ? 1.008f : 1.f);

                if (theme) {
                    float baseAlpha = isSection ? 0.0f
                        : selected ? 0.95f
                        : isActionLike ? 0.91f
                        : m_item.focusable() ? 0.89f : 0.86f;
                    float borderAlpha = isSection ? 0.0f
                        : selected ? 0.58f
                        : isActionLike ? 0.30f : 0.24f;
                    float hiAlpha = selected ? 0.12f : (isSection ? 0.0f : 0.06f);

                    setBaseColor(theme->panelBase.withAlpha(baseAlpha));
                    setBorderColor((selected ? theme->cursorNormal : theme->panelBorder).withAlpha(borderAlpha));
                    setHighlightColor(theme->panelHighlight.withAlpha(hiAlpha));
                }
            }
        }

        float insetX = m_flat ? (isSection ? 2.f : 12.f) : (isSection ? 6.f : 14.f);
        float insetY = m_flat ? 0.f : (isSection ? 4.f : 6.f);
        m_content->setRect({
            rect().x + insetX,
            rect().y + insetY,
            std::max(0.f, rect().width - insetX * 2.f),
            std::max(0.f, rect().height - insetY * 2.f)
        });
        m_content->setOpacity(alpha);
    }

protected:
    void onRender(nxui::Renderer& ren) override {
        nxui::GlassBox::onRender(ren);
        if (!m_flat || !m_selected || !m_theme || opacity() <= 0.01f)
            return;

        nxui::Rect r = rect();
        ren.drawRoundedRect({r.x + 4.f, r.y + 9.f, 3.f, std::max(0.f, r.height - 18.f)},
                            m_theme->cursorNormal.withAlpha(chrome::kSelectionBar * opacity()),
                            1.5f);
    }

private:
    TabbedOverlayScreen::SettingItem& m_item;
    std::shared_ptr<nxui::Box> m_content;
    bool m_flat = false;
    bool m_selected = false;
    const nxui::Theme* m_theme = nullptr;
    const nxui::Theme* m_cachedStyleTheme = nullptr;
    bool m_cachedStyleSelected = false;
    float m_cachedStyleAlpha = -1.f;
};

} // namespace


TabbedOverlayScreen::TabbedOverlayScreen(ScreenMode mode, bool fullscreen)
    : m_mode(mode), m_fullscreenPanel(fullscreen) {
    setFrameworkTouchEnabled(false);
    if (m_fullscreenPanel) {
        setRect({0.f, 0.f, 1280.f, 720.f});
    } else {
        setRect({kPanelMargin, kPanelMargin,
                 1280.f - 2.f * kPanelMargin, 720.f - 2.f * kPanelMargin});
    }
    setVisible(false);
    setOpacity(0.001f);
    setScale(0.92f);
    setCornerRadius(m_fullscreenPanel ? 0.f : kPanelRadius);
    setBlurEnabled(false);
    setBlurRadius(kSettingsBlurRadius);
    setBlurPasses(kSettingsBlurIter);
    setPanelOpacity(0.96f);

    m_focusCursor.setBorderWidth(chrome::kFocusRingWidth);
    m_focusCursor.setCornerRadius(chrome::kRadiusRow + chrome::kFocusExpansion);
    m_tabReveal.setImmediate(1.f);
    m_dropdownAnim.setImmediate(0.f);
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_toastQueue.clear();
    m_contentSlideAnim.setImmediate(1.f);
    m_tabAccentW.setImmediate(3.f);

    m_tabBar = std::make_shared<nxui::GlassBox>(nxui::Axis::COLUMN);
    m_tabBar->setTag("tabBar");
    m_tabBar->setWireframeEnabled(false);

    m_tabContent = std::make_shared<nxui::GlassBox>(nxui::Axis::COLUMN);
    m_tabContent->setTag("tabContent");
    m_tabContent->setWireframeEnabled(false);

    rebuildTabBar();
    rebuildContentItems();

    m_i18nListenerId = nxui::I18n::instance().addLanguageChangedListener([this]() {
        m_deferredRefresh = true;
    });
}
TabbedOverlayScreen::~TabbedOverlayScreen() {
    nxui::I18n::instance().removeLanguageChangedListener(m_i18nListenerId);
}

float TabbedOverlayScreen::rowBaseHeight() const {
    return showsRowDescriptions() ? 68.f : kRowHeight;
}

float TabbedOverlayScreen::itemHeight(const SettingItem& item, float contentWidth) const {
    if (item.hidden) return 0.f;
    if (item.type == ItemType::Section) return kSectionHeight;

    float height = rowBaseHeight();
    if (item.wrapLabel) {
        // Content card inset (36), card content inset (28), row inset (40).
        const float labelWidth = std::max(1.f, contentWidth - 104.f);

        std::string key = wrapHeightCacheKey(m_font, item.label, labelWidth);
        auto cached = g_wrapHeightCache.find(key);
        if (cached != g_wrapHeightCache.end()) {
            height = cached->second;
        } else {
            nxui::Label probe(item.label);
            if (m_font) probe.setFont(m_font);
            probe.setScale(0.90f);
            probe.setMultiline(true);
            height = std::max(rowBaseHeight(),
                              probe.measureWrappedText(labelWidth).y
                                  + (showsRowDescriptions() ? 34.f : 20.f));

            if (g_wrapHeightCache.size() >= kWrapHeightCacheLimit)
                g_wrapHeightCache.clear();
            g_wrapHeightCache.emplace(std::move(key), height);
        }
    }
    return height + std::max(0.f, item.extraHeight);
}

void TabbedOverlayScreen::setTheme(const nxui::Theme* t) {
    m_theme = t;
    if (!m_theme)
        return;

    setBaseColor(m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f));
    setBorderColor(m_theme->panelBorder.withAlpha(0.42f));
    setHighlightColor(m_theme->panelHighlight.withAlpha(0.10f));
    if (!m_active) invalidateBackdropCache();

    // Row widgets cache the colours they took from the theme (label/description
    // text, values), and they key that cache on the theme pointer. The menu
    // recolours one and the same Theme object in place, so those colours would
    // otherwise survive a theme switch: Theme Shop -> Sounds / Interface kept
    // black text over a black theme (or white on white). Drop the cache for
    // every tab, including the ones a custom layout renders itself, so the
    // settings-style tabs rebuild on next use.
    m_cachedTabContentWidgets.clear();
    m_cachedTabContentWidgets.resize(m_tabs.size());

    if (!usesCustomContentLayout() && isActive())
        rebuildContentItems();
}

void TabbedOverlayScreen::show() {
    if (m_active) return;
    DebugLog::log("[settings] show()");
    if (m_tabs.empty())
        warmup();
    m_active    = true;
    m_animating = true;
    m_showing   = true;
    m_animT     = 0.f;
    m_focusArea  = FocusArea::Tabs;
    m_tabIndex   = std::clamp(initialTabIndex(), 0,
                              std::max(0, static_cast<int>(m_tabs.size()) - 1));
    m_contentIdx = 0;
    m_scrollY    = 0.f;
    m_scrollTarget = 0.f;
    m_scrollVel = 0.f;
    m_scrollMomentum = 0.f;
    m_tabScrollY = 0.f;
    m_tabScrollTarget = 0.f;
    m_tabScrollVel = 0.f;
    m_tabScrollMomentum = 0.f;
    m_touchScrollVel = 0.f;
    m_tabReveal.setImmediate(0.f);
    m_tabReveal.set(1.f, chrome::kDurPanel, nxui::Easing::outCubic);
    m_dropdownOpen = false;
    m_dropdownClosing = false;
    m_dropdownRawIdx = -1;
    m_dropdownHover = 0;
    m_dropdownAnim.setImmediate(0.f);
    m_touchDirectControl = false;
    m_ignoreInitialTouchRelease = true;
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_toastQueue.clear();
    m_contentSlideAnim.setImmediate(1.f);
    // Opening the panel already fades the content in via m_tabReveal; the
    // cascade is reserved for tab switches.
    m_contentStaggerT = kContentStaggerDone;
    m_tabAccentW.setImmediate(3.f);
    if (m_tabBar) rebuildTabBar();
    if (m_tabContent) rebuildContentItems();
    invalidateBackdropCache();

    setVisible(true);
    syncPanelState(0.f);
    setFocusable(true);
    setupActions();
    announceCurrentFocus();
}

void TabbedOverlayScreen::hide() {
    if (!m_active) return;
    DebugLog::log("[settings] hide()");
    if (m_closeSfxCb) m_closeSfxCb();
    closeDropdown(false);
    m_trackToastAnim.setImmediate(0.f);
    m_trackToastHold = 0.f;
    m_trackToastFading = false;
    m_toastQueue.clear();
    m_animating = true;
    m_showing   = false;
    m_animT     = 0.f;

    setFocusable(false);
    clearActions();
}

void TabbedOverlayScreen::openDropdown(int rawIdx) {
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
        return;

    auto& items = m_tabs[m_tabIndex].items;
    if (rawIdx < 0 || rawIdx >= (int)items.size())
        return;

    auto& item = items[rawIdx];
    if (item.type != ItemType::Selector || item.options.empty())
        return;

    m_dropdownOpen = true;
    m_dropdownClosing = false;
    m_dropdownRawIdx = rawIdx;
    m_dropdownHover = std::clamp(item.intVal, 0, std::max(0, (int)item.options.size() - 1));
    int visible = std::min((int)item.options.size(), 6);
    m_dropdownVisualStart = (item.options.size() > (size_t)visible)
        ? (float)std::clamp(m_dropdownHover - visible / 2, 0, (int)item.options.size() - visible)
        : 0.f;
    m_dropdownAnim.set(1.f, chrome::kDurBase, nxui::Easing::outCubic);
}

void TabbedOverlayScreen::closeDropdown(bool animated) {
    m_dropdownOpen = false;
    m_dropdownClosing = animated && m_dropdownRawIdx >= 0;
    if (animated && m_dropdownRawIdx >= 0) {
        m_dropdownAnim.set(0.f, chrome::kDurBase, nxui::Easing::outCubic);
    } else {
        m_dropdownClosing = false;
        m_dropdownRawIdx = -1;
        m_dropdownAnim.setImmediate(0.f);
    }
}


float TabbedOverlayScreen::tabRailContentHeight() const {
    float height = kTabRailInset * 2.f + kTabRowHeight * (float)m_tabs.size();
    for (int i = 0; i < (int)m_tabs.size(); ++i)
        height += tabBranchExtraHeight(i) + tabGroupExtra(i);
    return height;
}

float TabbedOverlayScreen::tabGroupExtra(int tabIndex) const {
    if (tabIndex < 0 || tabIndex >= (int)m_tabs.size())
        return 0.f;
    return m_tabs[(size_t)tabIndex].group.empty() ? 0.f : kTabGroupHeight;
}

void TabbedOverlayScreen::drawTabGroupHeader(nxui::Renderer& ren, const nxui::Rect& tr,
                                             int iconKind, const std::string& label,
                                             float y, float opacity) {
    if (!m_theme || !m_smallFont)
        return;
    const float x = tr.x + kTabRailInset + 2.f;
    const float cy = y + 8.f;
    const nxui::Color ink = m_theme->textSecondary.withAlpha(0.62f * opacity);
    const nxui::Color glyph = m_theme->cursorNormal.withAlpha(0.62f * opacity);
    switch (iconKind) {
        case 1: // home
            ren.drawRoundedRect({x, cy - 1.f, 12.f, 8.f}, glyph, 1.5f);
            ren.drawLine({x, cy - 1.f}, {x + 6.f, cy - 7.f}, glyph, chrome::kIconStroke);
            ren.drawLine({x + 6.f, cy - 7.f}, {x + 12.f, cy - 1.f}, glyph, chrome::kIconStroke);
            break;
        case 2: // person
            ren.drawCircle({x + 6.f, cy - 5.f}, 2.8f, glyph, 14);
            ren.drawRoundedRect({x + 2.f, cy, 8.f, 6.f}, glyph, 2.4f);
            break;
        default: // 2x2 grid
            ren.drawRoundedRect({x, cy - 7.f, 4.f, 4.f}, glyph, 1.f);
            ren.drawRoundedRect({x + 7.f, cy - 7.f, 4.f, 4.f}, glyph, 1.f);
            ren.drawRoundedRect({x, cy - 1.f, 4.f, 4.f}, glyph, 1.f);
            ren.drawRoundedRect({x + 7.f, cy - 1.f, 4.f, 4.f}, glyph, 1.f);
            break;
    }
    ren.drawText(label, {x + 20.f, y}, m_smallFont, ink, chrome::kTypeSection);
}

const TabbedOverlayScreen::SettingItem* TabbedOverlayScreen::focusedSettingItem() const {
    if (m_focusArea != FocusArea::Content)
        return nullptr;
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size())
        return nullptr;
    const int raw = rawIndexFromFocusable(m_contentIdx);
    const auto& items = m_tabs[(size_t)m_tabIndex].items;
    if (raw < 0 || raw >= (int)items.size())
        return nullptr;
    return &items[(size_t)raw];
}

void TabbedOverlayScreen::rebuildTabBar() {
    m_tabBar->clearChildren();
    nxui::Rect tr = tabsRect();
    m_tabBar->setRect(tr);

    float tabX = tr.x + kTabRailInset;
    float tabY = tr.y + kTabRailInset - m_tabScrollY;
    float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);
    float tabH = kTabRowHeight - kTabCardGap;

    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        tabY += tabGroupExtra(i);
        auto tabBox = std::make_shared<SettingsTabWidget>(m_tabs[i].name);
        tabBox->setTag(m_tabs[i].name);
        tabBox->setRect({tabX, tabY, tabW, tabH});
        m_tabBar->addChild(tabBox);
        tabY += tabH + kTabCardGap + tabBranchExtraHeight(i);
    }
}

void TabbedOverlayScreen::rebuildContentItems() {
    m_tabContent->clearChildren();
    nxui::Rect cr = contentRect();
    m_tabContent->setRect(cr);

    if (usesCustomContentLayout())
        return;

    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    // Screens that build tabs after setTheme() can leave this cache at size 0
    // while m_tabs is already populated (Theme Shop Sounds/Interface path).
    if (m_cachedTabContentWidgets.size() != m_tabs.size())
        m_cachedTabContentWidgets.resize(m_tabs.size());
    ensureTabLoaded(m_tabIndex);
    auto& items = m_tabs[m_tabIndex].items;
    auto& cache = m_cachedTabContentWidgets[(size_t)m_tabIndex];
    if (m_cachedItemHeights.size() != m_tabs.size())
        m_cachedItemHeights.resize(m_tabs.size());
    auto& heights = m_cachedItemHeights[(size_t)m_tabIndex];
    // DebugLog::log("[settings] rebuildContent tab=%d items=%d cache=%s",
    //               m_tabIndex, (int)items.size(), cache.empty() ? "miss" : "hit");

    if (cache.empty()) {
        cache.reserve(items.size());
        heights.clear();
        heights.reserve(items.size());

        float y = 0.f;
        for (int i = 0; i < (int)items.size(); ++i) {
            float h = itemHeight(items[i], cr.width);
            heights.push_back(h);
            auto itemBox = makeItemWidget(items[i]);
            itemBox->setTag(items[i].label);
            itemBox->setRect({cr.x + kContentCardInsetX, cr.y + y,
                              std::max(0.f, cr.width - kContentCardInsetX * 2.f), h});
            cache.push_back(itemBox);
            y += h;
        }
    }

    for (auto& itemBox : cache) {
        m_tabContent->addChild(itemBox);
    }
}

std::shared_ptr<nxui::Box> TabbedOverlayScreen::makeItemWidget(SettingItem& item) {
    settings::widgets::SettingWidgetContext ctx;
    ctx.font = &m_font;
    ctx.smallFont = &m_smallFont;
    ctx.theme = &m_theme;
    ctx.showDescriptions = showsRowDescriptions();
    auto content = settings::widgets::createSettingItemWidget(item, ctx);
    return std::make_shared<SettingsItemCard>(item, content, !showsRowDescriptions());
}

void TabbedOverlayScreen::onRender(nxui::Renderer& ren) {
    perf::ScopedPass pass("settings");
    if (!m_active && !m_animating)
        return;

    // Capture HOME once, at the point in the tree where it has finished drawing
    // and this overlay has not painted its panel yet. Offscreen target 2 stays
    // reserved for this snapshot; target 0/1 are the live glass pipeline.
    if (m_fullscreenPanel && !m_backdropCacheValid && m_showing &&
        ren.gpu().offscreenReady()) {
        ren.captureToOffscreen(false);
        ren.copyOffscreen(0, 2);
        m_backdropCacheValid = true;
    }
    if (m_backdropCacheValid)
        ren.drawOffscreen(2, {0.f, 0.f, 1280.f, 720.f});

    float opacity = visibilityProgress();
    nxui::Rect p = panelRect(scale());

    if (m_theme)
        m_focusCursor.setColor(m_theme->cursorNormal);
    m_focusCursor.setOpacity(opacity);

    if (m_tabBar) m_tabBar->setRect(tabsRect(p));
    if (m_tabContent) m_tabContent->setRect(contentRect(p));

    drawBackground(ren, p, opacity * 0.72f);

    if (opacity > 0.01f) {
        const nxui::Color panel = m_theme ? m_theme->panelBase : m_base;
        const nxui::Color border = m_theme
            ? m_theme->panelBorder
            : nxui::Color::white().withAlpha(0.18f);
        const nxui::Color highlight = m_theme
            ? m_theme->panelHighlight
            : nxui::Color::white().withAlpha(0.08f);
        const float panelRadius = m_fullscreenPanel ? 0.f : kPanelRadius;
        if (!m_fullscreenPanel)
            chrome::drawPanelShadow(ren, p, panelRadius, opacity);
        // Settings and the Theme Shop are fullscreen, but they keep the working
        // glass tier so the wallpaper and shortcut chrome stay faintly visible
        // behind them. Floating dialogs use the lighter modal tier.
        const float panelAlpha = m_fullscreenPanel
            ? chrome::kWorkingAlpha * opacity
            : chrome::kModalAlpha * opacity;
        ren.drawRoundedRect(p, panel.withAlpha(panelAlpha), panelRadius);
        if (!m_fullscreenPanel) {
            ren.drawRoundedRectOutline(p.shrunk(1.f),
                                       highlight.withAlpha(0.28f * opacity),
                                       std::max(0.f, panelRadius - 1.f), 1.f);
            ren.drawRoundedRectOutline(p, border.withAlpha(chrome::kModalRim * opacity),
                                       panelRadius, 1.5f);
        }
    }

    onContentRender(ren);

    if (ren.boxWireframeEnabled()) {
        syncDebugWireframeRects(p);
        if (m_tabBar) m_tabBar->render(ren);
        if (m_tabContent) m_tabContent->render(ren);
    }

    m_focusCursor.render(ren);
}

void TabbedOverlayScreen::onContentRender(nxui::Renderer& ren) {
    float opacity = visibilityProgress();
    nxui::Rect p = panelRect(scale());
    float textOp = m_showing ? opacity : opacity * opacity;

    ren.pushClipRect(p);
    drawOverlayHeader(ren, p, textOp);
    drawTabs(ren, p, textOp);
    drawContent(ren, p, textOp);
    drawOverlayFooter(ren, p, textOp);
    drawDropdown(ren, p, textOp);
    drawTrackChangedToast(ren, p, textOp);
    ren.popClipRect();
}

void TabbedOverlayScreen::syncDebugWireframeRects(const nxui::Rect& panel) {
    if (!m_tabBar || !m_tabContent) return;

    nxui::Rect tr = tabsRect(panel);
    nxui::Rect cr = contentRect(panel);

    m_tabBar->setRect(tr);
    auto& tabChildren = m_tabBar->children();
    float tabY = tr.y + kTabRailInset - m_tabScrollY;
    float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);
    float tabH = kTabRowHeight - kTabCardGap;
    for (int i = 0; i < (int)tabChildren.size(); ++i) {
        tabY += tabGroupExtra(i);
        tabChildren[i]->setRect({tr.x + kTabRailInset, tabY, tabW, tabH});
        tabY += tabH + kTabCardGap + tabBranchExtraHeight(i);
    }

    m_tabContent->setRect(cr);
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    auto& items = m_tabs[m_tabIndex].items;
    auto& itemChildren = m_tabContent->children();

    float slideT = std::clamp(m_contentSlideAnim.value(), 0.f, 1.f);
    float slideOffset = (1.f - slideT) * 24.f * (float)m_tabSwitchDir;

    float y = cr.y - m_scrollY + slideOffset;
    float x = cr.x;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = itemHeight(items[i], cr.width);
        itemChildren[i]->setRect({x + kContentCardInsetX, y,
                                  std::max(0.f, cr.width - kContentCardInsetX * 2.f), h});
        y += h;
    }
}

void TabbedOverlayScreen::drawBackground(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || opacity <= 0.01f)
        return;

    nxui::Rect screen = {0.f, 0.f, (float)ren.width(), (float)ren.height()};
    nxui::Color scrim = nxui::Color::lerp(m_theme->background, nxui::Color::black(),
                                          m_theme->mode == nxui::ThemeMode::Dark ? 0.72f : 0.28f)
        .withAlpha((m_theme->mode == nxui::ThemeMode::Dark ? 0.14f : 0.10f) * opacity);

    ren.drawRect(screen, scrim);
}

void TabbedOverlayScreen::drawTabs(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_theme || !m_tabBar) return;
    nxui::Rect tr = tabsRect(panel);
    auto* tabPanel = static_cast<nxui::GlassBox*>(m_tabBar.get());

    tabPanel->setRect(tr);
    tabPanel->setOpacity(opacity);
    tabPanel->setCornerRadius(chrome::kRadiusCard);
    // Open rail: no nested glass card behind the tab list. A single hairline
    // separates it from the content and the selected row supplies the surface.
    tabPanel->setBorderWidth(0.f);
    tabPanel->setBaseColor(nxui::Color(0.f, 0.f, 0.f, 0.f));
    tabPanel->setBorderColor(nxui::Color(0.f, 0.f, 0.f, 0.f));
    tabPanel->setHighlightColor(nxui::Color(0.f, 0.f, 0.f, 0.f));
    tabPanel->setPanelOpacity(1.f);

    auto& tabChildren = m_tabBar->children();
    float reveal = std::clamp(m_tabReveal.value(), 0.f, 1.f);
    float tabY = tr.y + kTabRailInset - m_tabScrollY;
    float tabW = std::max(0.f, tr.width - kTabRailInset * 2.f);
    float tabH = kTabRowHeight - kTabCardGap;
    float rowOpacity = opacity * reveal;
    float rowYOffset = (1.f - reveal) * 6.f;
    struct BranchDraw {
        int index = -1;
        nxui::Rect rect;
    };
    BranchDraw branchDraw;
    int visibleTabRank = 0;
    int activeGroupStart = 0;
    if (!m_tabs.empty()) {
        activeGroupStart = std::clamp(m_tabIndex, 0, (int)m_tabs.size() - 1);
        while (activeGroupStart > 0 && m_tabs[(size_t)activeGroupStart - 1].group.empty())
            --activeGroupStart;
    }

    for (int i = 0; i < (int)tabChildren.size() && i < (int)m_tabs.size(); ++i) {
        tabY += tabGroupExtra(i);
        auto* tab = static_cast<SettingsTabWidget*>(tabChildren[i].get());
        tab->setRect({tr.x + kTabRailInset, tabY + rowYOffset, tabW, tabH});
        // The rail holds more entries than fit and every tab draws a glass
        // plate, border, label and accent. Skip the ones outside
        // the clip instead of syncing and submitting all of them each frame.
        const bool onScreen = tab->rect().bottom() >= tr.y - 4.f
                           && tab->rect().y <= tr.bottom() + 4.f;
        tab->setVisible(onScreen);
        if (onScreen) {
            // Stagger only the visible rows. Keying this on the absolute tab
            // index left every row past the fold dimmed, and the last tab
            // never became opaque even after the rail scrolled to it.
            const float stagger = (float)std::min(visibleTabRank, 6) * 0.05f;
            tab->setOpacity(opacity * std::clamp(
                (reveal - stagger) / 0.55f, 0.f, 1.f));
            ++visibleTabRank;
            const bool branchFocus = (i == m_tabIndex) && tabBranchConsumesFocus();
            tab->sync(m_tabs[i].name,
                      m_font,
                      m_theme,
                      i == m_tabIndex,
                      m_focusArea == FocusArea::Tabs && i == m_tabIndex && !branchFocus,
                      m_uiTime,
                      m_tabAccentW.value());
        }
        tabY += tabH + kTabCardGap;
        const float extra = tabBranchExtraHeight(i);
        if (extra > 0.5f) {
            // Same card height as parent tabs (pitch minus gap), just indented.
            if (onScreen) {
                const float branchH = std::max(0.f, std::min(tabH, extra - kTabCardGap));
                branchDraw = {
                    i,
                    {
                        tr.x + kTabRailInset + 20.f,
                        tabY + rowYOffset,
                        std::max(0.f, tabW - 20.f),
                        branchH
                    }
                };
            }
            tabY += extra;
        }
    }

    if (m_focusArea == FocusArea::Tabs && m_tabIndex >= 0 && m_tabIndex < (int)tabChildren.size()) {
        nxui::Rect focusRect = tabChildren[m_tabIndex]->rect().expanded(1.f);
        (void)tabFocusCursorRect(focusRect);
        m_focusCursor.moveTo(focusRect, chrome::kRadiusRow + chrome::kFocusExpansion, chrome::kDurFast);
    }

    ren.drawRoundedRect({tr.right() + 12.f, tr.y + 6.f, 1.f,
                         std::max(0.f, tr.height - 12.f)},
                        m_theme->panelBorder.withAlpha(chrome::kRailDivider * opacity), 0.5f);
    ren.pushClipRect(tr);
    m_tabBar->render(ren);
    for (int i = 0; i < (int)tabChildren.size() && i < (int)m_tabs.size(); ++i) {
        if (m_tabs[i].group.empty())
            continue;
        const nxui::Rect& tabRect = tabChildren[i]->rect();
        float capY = tabRect.y - kTabGroupHeight + 7.f;
        const bool activeGroup = (i == activeGroupStart);
        if (capY < tr.y + 8.f) {
            if (!activeGroup)
                continue;
            // Keep the active group heading available while its rows scroll
            // under it, instead of letting the label disappear above the rail.
            capY = tr.y + 8.f;
            ren.drawRoundedRect({tr.x + 4.f, tr.y + 2.f, std::max(0.f, tr.width - 8.f), 24.f},
                                m_theme->panelBase.withAlpha(0.94f * opacity),
                                chrome::kRadiusControl);
            ren.drawRoundedRect({tr.x + 12.f, tr.y + 25.f,
                                 std::max(0.f, tr.width - 24.f), 1.f},
                                m_theme->panelBorder.withAlpha(0.16f * opacity), 0.5f);
        }
        if (capY > tr.bottom() + 4.f)
            continue;
        drawTabGroupHeader(ren, tr, m_tabs[i].groupIcon, m_tabs[i].group,
                           capY, rowOpacity);
    }
    if (branchDraw.index >= 0)
        drawTabBranch(ren, branchDraw.index, branchDraw.rect, rowOpacity);
    ren.popClipRect();

    const float totalHeight = tabRailContentHeight();
    const float maxScroll = std::max(0.f, totalHeight - tr.height);
    if (maxScroll > 0.f) {
        const float trackH = std::max(34.f, tr.height * tr.height / totalHeight);
        const float travel = std::max(0.f, tr.height - trackH - 16.f);
        const float thumbY = tr.y + 8.f + travel * std::clamp(m_tabScrollY / maxScroll, 0.f, 1.f);
        ren.drawRoundedRect({tr.right() - 7.f, thumbY, 3.f, trackH},
                            m_theme->cursorNormal.withAlpha(0.42f * opacity), 1.5f);
    }
}

void TabbedOverlayScreen::drawContent(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_font || !m_smallFont || !m_theme) return;
    if (m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;
    if (!m_tabContent) return;

    nxui::Rect cr = contentRect(panel);
    auto* contentPanel = static_cast<nxui::GlassBox*>(m_tabContent.get());
    contentPanel->setRect(cr);
    contentPanel->setOpacity(opacity);
    contentPanel->setCornerRadius(chrome::kRadiusCard);
    contentPanel->setBorderWidth(1.f);
    contentPanel->setBaseColor(m_theme->panelBase.withAlpha(chrome::kWorkingAlpha));
    contentPanel->setBorderColor(m_theme->panelBorder.withAlpha(0.30f));
    contentPanel->setHighlightColor(m_theme->panelHighlight.withAlpha(0.08f));
    contentPanel->setPanelOpacity(1.f);

    if (usesCustomContentLayout()) {
        contentPanel->render(ren);
        ren.pushClipRect(cr);
        drawCustomContent(ren, panel, cr, opacity);
        ren.popClipRect();
        return;
    }

    auto& items = m_tabs[m_tabIndex].items;
    auto& itemChildren = m_tabContent->children();
    // Measurements are cached when the rows are built; re-measuring all of them
    // (and building a string cache key per row) every frame made scrolling and
    // tab switches fight the frame budget.
    const std::vector<float>* heights = nullptr;
    if (m_tabIndex < (int)m_cachedItemHeights.size()
        && m_cachedItemHeights[(size_t)m_tabIndex].size() == items.size())
        heights = &m_cachedItemHeights[(size_t)m_tabIndex];
    auto rowHeight = [&](int i) {
        return heights ? (*heights)[(size_t)i]
                       : itemHeight(items[(size_t)i], cr.width);
    };
    int focusedRawIdx = (m_focusArea == FocusArea::Content && focusableCount() > 0)
        ? rawIndexFromFocusable(m_contentIdx) : -1;

    const float reveal = opacity * std::clamp(m_tabReveal.value(), 0.f, 1.f);

    float y = cr.y + ui::kSpace4 - m_scrollY;
    float x = cr.x;
    int visibleRank = 0;
    int n = std::min((int)itemChildren.size(), (int)items.size());
    for (int i = 0; i < n; ++i) {
        float h = rowHeight(i);
        if (items[i].hidden || h <= 0.f) {
            items[i].contentFocused = false;
            itemChildren[i]->setVisible(false);
            continue;
        }
        nxui::Rect itemRect = {
            x + kContentCardInsetX,
            y,
            std::max(0.f, cr.width - kContentCardInsetX * 2.f),
            h
        };
        bool visible = itemRect.bottom() >= cr.y - 8.f && itemRect.y <= cr.bottom() + 8.f;
        const bool selected = (i == focusedRawIdx);
        items[i].contentFocused = selected;

        itemChildren[i]->setVisible(visible);
        if (!visible) {
            y += h;
            continue;
        }

        // Rows cascade in one after another, top to bottom. Ranking by visible
        // position rather than item index keeps the first row on screen leading
        // the cascade even when the tab was left scrolled down.
        const float rowT = std::clamp(
            (m_contentStaggerT - (float)std::min(visibleRank, 6) * kRowStaggerDelay)
                / kRowRevealDur,
            0.f, 1.f);
        const float rowEase = nxui::Easing::outCubic(rowT);
        ++visibleRank;

        itemRect.y += (1.f - rowEase) * 18.f * (float)m_tabSwitchDir;
        const float rowOpacity = reveal * rowEase;

        itemChildren[i]->setRect(itemRect);
        itemChildren[i]->setOpacity(rowOpacity);

        auto* card = static_cast<SettingsItemCard*>(itemChildren[i].get());
        // Dual-button action rows (Select + ×): suppress the full-row card
        // highlight so L/R focus on the buttons themselves stays readable.
        const bool multiButton = selected
            && items[i].type == ItemType::Action
            && items[i].hasSecondaryAction();
        card->sync(m_theme, multiButton ? false : selected, rowOpacity);
        // Hairline between rows keeps the flat list legible without boxing
        // every entry; section headers stay open above their group.
        if (items[i].type != ItemType::Section) {
            ren.drawRoundedRect({itemRect.x + 12.f, itemRect.bottom() - 0.5f,
                                 std::max(0.f, itemRect.width - 24.f), 1.f},
                                m_theme->panelBorder.withAlpha(
                                    (m_theme->mode == nxui::ThemeMode::Dark
                                         ? chrome::kHairlineDark : chrome::kHairlineLight)
                                    * rowOpacity), 0.5f);
        }

        if (selected) {
            if (multiButton) {
                const nxui::Rect& hit = items[i].actionButtonFocus == 1
                    ? items[i].secondaryHit : items[i].primaryHit;
                if (hit.width > 1.f && hit.height > 1.f)
                    m_focusCursor.moveTo(hit.expanded(chrome::kFocusExpansion),
                                         chrome::kRadiusControl + chrome::kFocusExpansion,
                                         chrome::kDurFast);
                else
                    m_focusCursor.moveTo(itemChildren[i]->rect().expanded(chrome::kFocusExpansion),
                                         chrome::kRadiusRow + chrome::kFocusExpansion,
                                         chrome::kDurFast);
            } else {
                m_focusCursor.moveTo(itemChildren[i]->rect().expanded(chrome::kFocusExpansion),
                                     (items[i].type == ItemType::Section
                                          ? chrome::kRadiusControl : chrome::kRadiusRow)
                                         + chrome::kFocusExpansion,
                                     chrome::kDurFast);
            }
        }
        y += h;
    }

    ren.pushClipRect(cr);
    m_tabContent->render(ren);
    ren.popClipRect();

    float totalH = contentTotalHeight() + 16.f;
    if (totalH > cr.height + 1.f) {
        float maxScroll = std::max(1.f, totalH - cr.height + 20.f);
        float trackH = std::max(42.f, cr.height * std::clamp(cr.height / totalH, 0.12f, 1.f));
        float trackX = cr.right() - 8.f;
        float trackY = cr.y + 16.f;
        float trackAreaH = std::max(1.f, cr.height - 32.f);
        float thumbY = trackY + (trackAreaH - trackH) * std::clamp(m_scrollY / maxScroll, 0.f, 1.f);
        nxui::Rect rail = {trackX, trackY, 3.f, trackAreaH};
        nxui::Rect thumb = {trackX - 0.5f, thumbY, 4.f, trackH};
        ren.drawRoundedRect(rail, m_theme->panelBorder.withAlpha(0.16f * opacity), 1.5f);
        ren.drawRoundedRect(thumb, m_theme->cursorNormal.withAlpha(0.46f * opacity), 2.f);
    }

}

void TabbedOverlayScreen::drawDropdown(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_theme || !m_smallFont || m_tabIndex < 0 || m_tabIndex >= (int)m_tabs.size()) return;

    float open = m_dropdownAnim.value();
    if (open <= 0.01f) return;

    auto& items = m_tabs[m_tabIndex].items;
    if (m_dropdownRawIdx < 0 || m_dropdownRawIdx >= (int)items.size()) return;
    auto& item = items[m_dropdownRawIdx];
    if (item.type != ItemType::Selector || item.options.empty()) return;

    nxui::Rect cr = contentRect(panel);

    float y = cr.y + ui::kSpace4 - m_scrollY;
    for (int i = 0; i < m_dropdownRawIdx; ++i)
        y += itemHeight(items[i], cr.width);
    float rowH = itemHeight(item, cr.width);

    float ctrlX = cr.x + cr.width * 0.40f;
    float ctrlW = cr.width * 0.60f;

    int total = (int)item.options.size();
    int visible = std::min(total, 6);
    float optH = 46.f;
    float listH = visible * optH + 16.f;

    float visualStart = 0.f;
    if (total > visible)
        visualStart = std::clamp(m_dropdownVisualStart, 0.f, (float)(total - visible));
    int start = std::clamp((int)std::floor(visualStart), 0, std::max(0, total - visible));
    float rowOffset = (float)start - visualStart;

    float dy = y + rowH + 6.f;
    if (dy + listH > cr.bottom() - 4.f)
        dy = y - listH - 6.f;

    float scale = 0.965f + 0.035f * open;
    float w = ctrlW * scale;
    float h = listH * scale;
    float dx = ctrlX + (ctrlW - w) * 0.5f;
    float fy = dy + (listH - h) * 0.5f + (1.f - open) * 8.f;

    nxui::Rect pop = { dx, fy, w, h };

    float a = opacity * open;
    nxui::Color bg = m_theme->mode == nxui::ThemeMode::Dark
        ? nxui::Color::lerp(m_theme->panelBase, nxui::Color(0.055f, 0.060f, 0.075f, 1.f), 0.36f).withAlpha(chrome::kModalAlpha * a)
        : nxui::Color::lerp(m_theme->panelBase, nxui::Color(0.98f, 0.985f, 1.f, 1.f), 0.30f).withAlpha(chrome::kModalAlpha * a);
    const float radius = chrome::kRadiusCard;

    nxui::Rect contact = pop;
    contact.y += 6.f;
    ren.drawRoundedRect(contact.expanded(3.f), nxui::Color::black().withAlpha(0.12f * a), radius + 3.f);
    ren.drawRoundedRect(pop.expanded(1.f), m_theme->panelHighlight.withAlpha(0.08f * a), radius + 1.f);
    ren.drawRoundedRect(pop, bg, radius);
    ren.drawRoundedRectOutline(pop,
                               m_theme->panelBorder.withAlpha(chrome::kModalRim * a),
                               radius, 1.4f);
    ren.drawRoundedRectOutline(pop.shrunk(2.f),
                               m_theme->panelHighlight.withAlpha(0.10f * a),
                               std::max(0.f, radius - 2.f), 1.f);

    nxui::Rect listClip = pop.shrunk(6.f);
    ren.pushClipRect(listClip);

    for (int i = 0; i < visible + 1; ++i) {
        int idx = start + i;
        if (idx < 0 || idx >= total)
            continue;
        float rowReveal = std::clamp((open - i * 0.025f) / 0.25f, 0.f, 1.f);
        float ry = listClip.y + 3.f + (rowOffset + (float)i) * optH + (1.f - rowReveal) * 5.f;
        nxui::Rect rr = { listClip.x + 3.f, ry, listClip.width - 6.f, optH - 2.f };
        if (rr.bottom() < listClip.y || rr.y > listClip.bottom())
            continue;

        bool hovered = idx == m_dropdownHover;
        bool active = idx == item.intVal;
        float rowAlpha = a * rowReveal;
        if (active) {
            ren.drawRoundedRect(rr,
                                m_theme->panelHighlight.withAlpha(0.065f * rowAlpha),
                                chrome::kRadiusControl);
            ren.drawRoundedRectOutline(rr.shrunk(1.f),
                                       m_theme->panelHighlight.withAlpha(0.075f * rowAlpha),
                                       chrome::nested(chrome::kRadiusControl, 1.f),
                                       1.f);
        }
        if (hovered) {
            float pulse = 0.9f + 0.1f * (std::sin(m_uiTime * 5.2f) * 0.5f + 0.5f);
            nxui::Color hi = m_theme->cursorNormal.withAlpha(0.20f * pulse * rowAlpha);
            ren.drawRoundedRect(rr, hi, chrome::kRadiusControl);
            ren.drawRoundedRectOutline(rr,
                                       m_theme->cursorNormal.withAlpha(0.42f * rowAlpha),
                                       chrome::kRadiusControl,
                                       1.2f);
            if (m_dropdownOpen)
                m_focusCursor.moveTo(rr.shrunk(0.5f), chrome::kRadiusControl,
                                     chrome::kDurFast);
        }

        nxui::Color tc = active ? m_theme->textPrimary : m_theme->textSecondary;
        std::string displayText = item.options[idx];
        float maxWidth = std::max(0.f, rr.width - 16.f);
        if (!displayText.empty()) {
            nxui::Vec2 tsz = m_smallFont->measure(displayText);
            if (tsz.x > maxWidth) {
                displayText = switchu::ui::text::ellipsize(
                    m_smallFont, displayText, maxWidth, chrome::kTypeRow);
            }
        }
        float tx = rr.x + 14.f;
        float ty = chrome::capCenteredY(*m_smallFont, rr, displayText, chrome::kTypeRow);
        ren.drawText(displayText, {tx, ty}, m_smallFont, tc.withAlpha(rowAlpha), chrome::kTypeRow);
    }

    ren.popClipRect();

    if (total > visible) {
        float railH = std::max(1.f, pop.height - 24.f);
        float thumbH = std::max(24.f, railH * ((float)visible / (float)total));
        float maxStart = (float)std::max(1, total - visible);
        float thumbY = pop.y + 12.f + (railH - thumbH) * (visualStart / maxStart);
        nxui::Rect rail = {pop.right() - 10.f, pop.y + 12.f, 3.f, railH};
        nxui::Rect thumb = {pop.right() - 10.5f, thumbY, 4.f, thumbH};
        ren.drawRoundedRect(rail, m_theme->panelBorder.withAlpha(0.22f * a), 1.5f);
        ren.drawRoundedRect(thumb, m_theme->cursorNormal.withAlpha(0.56f * a), 2.f);
    }
}

void TabbedOverlayScreen::drawTrackChangedToast(nxui::Renderer& ren, const nxui::Rect& panel, float opacity) {
    if (!m_smallFont || !m_theme) return;
    float t = m_trackToastAnim.value();
    if (t <= 0.01f || m_toastText.empty()) return;

    // Wrap to at most two lines instead of truncating with "...". Status
    // messages here carry real information ("4:3 only - reloading..."), and a
    // silently cut sentence is worse than a slightly taller toast.
    constexpr float kToastScale = chrome::kTypeRow;
    const float maxTextWidth = 420.f - ui::kSpace5;
    std::string lines[2];
    {
        std::string rest = m_toastText;
        for (int line = 0; line < 2 && !rest.empty(); ++line) {
            std::size_t cut = rest.size();
            if (m_smallFont->measure(rest).x * kToastScale > maxTextWidth) {
                cut = 0;
                std::size_t search = 0;
                while (true) {
                    const std::size_t space = rest.find(' ', search);
                    if (space == std::string::npos)
                        break;
                    if (m_smallFont->measure(rest.substr(0, space)).x * kToastScale
                        > maxTextWidth)
                        break;
                    cut = space;
                    search = space + 1;
                }
                if (cut == 0)
                    cut = rest.size();
            }
            lines[line] = rest.substr(0, cut);
            rest = (cut < rest.size()) ? rest.substr(std::min(rest.size(), cut + 1))
                                       : std::string();
        }
        if (!rest.empty()) {
            lines[1] = switchu::ui::text::ellipsize(
                m_smallFont, lines[1], maxTextWidth, kToastScale);
        }
    }
    const bool twoLines = !lines[1].empty();
    nxui::Vec2 tsz = m_smallFont->measure(twoLines ? lines[0] : m_toastText);
    const float lineH = tsz.y * 0.72f;
    float textWidth = 0.f;
    for (int line = 0; line < (twoLines ? 2 : 1); ++line)
        textWidth = std::max(textWidth,
            m_smallFont->measure(lines[line]).x * kToastScale);
    textWidth = std::min(maxTextWidth, textWidth);
    float scale = 0.96f + 0.04f * t;
    float w = std::clamp(textWidth + ui::kSpace6, 220.f, 420.f) * scale;
    float h = (twoLines ? 58.f : 40.f) * scale;
    float x = panel.right() - 24.f - w;
    float y = panel.y + 18.f;
    nxui::Rect r = {x, y, w, h};

    nxui::Color bg = (m_theme->mode == nxui::ThemeMode::Dark)
        ? nxui::Color(0.10f, 0.14f, 0.20f, 0.92f * t * opacity)
        : nxui::Color(0.90f, 0.95f, 1.00f, 0.94f * t * opacity);
    nxui::Color bd = m_theme->cursorNormal.withAlpha(0.65f * t * opacity);

    ren.drawRoundedRect(r, bg, chrome::kRadiusControl);
    ren.drawRoundedRectOutline(r, bd, chrome::kRadiusControl, 1.5f);

    const int lineCount = twoLines ? 2 : 1;
    const float blockH = lineH * static_cast<float>(lineCount);
    const float firstY = r.y + (r.height - blockH) * 0.5f;
    for (int line = 0; line < lineCount; ++line) {
        const std::string& text = twoLines ? lines[line] : m_toastText;
        const float tx = r.x + ui::kSpace3;
        const float ty = firstY + lineH * static_cast<float>(line);
        ren.drawText(text, {tx, ty}, m_smallFont,
                     m_theme->textPrimary.withAlpha(t * opacity), kToastScale);
    }
}
