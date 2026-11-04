#include "LockScreen.hpp"
#include "BatteryDrawing.hpp"
#include "services/ClockService.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float kScreenW = 1280.f;
constexpr float kScreenH = 720.f;
constexpr float kMargin = 56.f;
constexpr float kCurtainTravel = 740.f;
constexpr float kInputGuard = 0.35f;
constexpr int kTimeFontPt = 88;
constexpr float kDateSettleDelay = 0.35f;
// Dissolve gets a slow push-in so it is not a flat cross-fade.
constexpr float kDissolveEnterScale = 1.035f;
// Unlock durations — AnimatedFloat is driven by AnimationManager only
// (do not also call .update here or motions run ~2×).
constexpr float kCurtainEnterSec = 0.70f;
constexpr float kCurtainUnlockSec = 0.78f;
constexpr float kDissolveEnterSec = 1.15f;
constexpr float kDissolveExitSec = 1.15f;
constexpr float kZoomEnterSec = 0.78f;
constexpr float kZoomExitSec = 0.78f;
constexpr float kWipeEnterSec = 0.72f;
constexpr float kWipeExitSec = 0.72f;
constexpr float kEnterAnimDelay = 0.05f;  // hitch after hiding HOME / focusing Lock
constexpr float kUnlockAnimDelay = 0.05f; // hitch after restoring HOME under Lock
// Zoom scale endpoints (Out grows away / In shrinks in).
constexpr float kZoomOutEnter = 1.48f;
constexpr float kZoomInEnter = 0.52f;
constexpr float kZoomOutExit = 1.58f;
constexpr float kZoomInExit = 0.48f;
constexpr float kResumeBtnW = 280.f;
constexpr float kResumeBtnH = 52.f;
constexpr float kResumeBtnY = 520.f;
constexpr float kIconSize = 148.f;
constexpr float kIconRadius = 16.f; // match GlossyIcon grid tiles
// Vertically center the tile between the clock block and Resume.
constexpr float kClockBottom = 230.f;
constexpr float kIconY = (kClockBottom + kResumeBtnY) * 0.5f - kIconSize * 0.5f;
constexpr float kSwipeCommit = 0.28f;
constexpr float kSwipeFlickPx = 140.f;
constexpr float kSwipeDeadzone = 18.f;

std::string replaceAll(std::string text, const std::string& token, const std::string& value) {
    for (std::size_t pos = text.find(token); pos != std::string::npos;
         pos = text.find(token, pos + value.size()))
        text.replace(pos, token.size(), value);
    return text;
}

nxui::Vec2 mapLockPt(float x, float y, float dx, float dy, float zoom) {
    const float cx = kScreenW * 0.5f;
    const float cy = kScreenH * 0.5f;
    return {cx + (x - cx) * zoom + dx, cy + (y - cy) * zoom + dy};
}

void drawCentered(nxui::Renderer& ren, const std::string& text, float y,
                  nxui::Font* font, const nxui::Color& color, float scale,
                  float dx, float dy, float zoom = 1.f) {
    if (!font || text.empty())
        return;
    const nxui::Vec2 sz = font->measure(text);
    const float w = sz.x * scale;
    const float h = sz.y * scale;
    const float x = (kScreenW - w) * 0.5f;
    const nxui::Vec2 c = mapLockPt(x + w * 0.5f, y + h * 0.5f, dx, dy, zoom);
    const float zs = scale * zoom;
    ren.drawText(text, {c.x - sz.x * zs * 0.5f, c.y - sz.y * zs * 0.5f}, font, color, zs);
}

nxui::Rect resumeButtonRect(float dy) {
    return {(kScreenW - kResumeBtnW) * 0.5f, kResumeBtnY + dy, kResumeBtnW, kResumeBtnH};
}

nxui::Rect iconRect(float dy) {
    return {(kScreenW - kIconSize) * 0.5f, kIconY + dy, kIconSize, kIconSize};
}

// ---------------------------------------------------------------------------
// Soft-image / typography helpers
// ---------------------------------------------------------------------------

bool isAscii(const std::string& s) {
    for (unsigned char c : s)
        if (c >= 0x80) return false;
    return true;
}

std::string asciiUpper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

float trackedWidth(nxui::Font* font, const std::string& text, float scale, float tracking) {
    if (!font || text.empty()) return 0.f;
    return font->measure(text).x * scale +
           tracking * static_cast<float>(text.size() - 1) * scale;
}

// Letter-spaced caption. Tracking is ASCII-only so CJK dates keep their own
// natural spacing instead of being split per byte.
void drawCenteredTracked(nxui::Renderer& ren, const std::string& text, float y, nxui::Font* font,
                         const nxui::Color& color, float scale, float tracking,
                         float dx, float dy, float zoom) {
    if (!font || text.empty() || color.a <= 0.002f) return;
    if (!isAscii(text)) {
        drawCentered(ren, text, y, font, color, scale, dx, dy, zoom);
        return;
    }
    const nxui::Vec2 sz = font->measure(text);
    const float h = sz.y * scale;
    const float startX = (kScreenW - trackedWidth(font, text, scale, tracking)) * 0.5f;
    const nxui::Vec2 c0 = mapLockPt(startX, y + h * 0.5f, dx, dy, zoom);
    float cx = c0.x;
    for (char ch : text) {
        const std::string g(1, ch);
        const float gw = font->measure(g).x * scale * zoom;
        ren.drawText(g, {cx, c0.y - h * zoom * 0.5f}, font, color, scale * zoom);
        cx += gw + tracking * scale * zoom;
    }
}

// Direction arrow for the passcode cells, drawn with primitives so it does not
// depend on the font carrying arrow glyphs.
void drawDirGlyph(nxui::Renderer& ren, char dir, const nxui::Vec2& c, float size,
                  const nxui::Color& col) {
    const float h = size * 0.5f;
    switch (dir) {
        case 'U':
            ren.drawTriangle({c.x, c.y - h}, {c.x - h * 0.86f, c.y + h * 0.55f},
                             {c.x + h * 0.86f, c.y + h * 0.55f}, col);
            break;
        case 'D':
            ren.drawTriangle({c.x, c.y + h}, {c.x - h * 0.86f, c.y - h * 0.55f},
                             {c.x + h * 0.86f, c.y - h * 0.55f}, col);
            break;
        case 'L':
            ren.drawTriangle({c.x - h, c.y}, {c.x + h * 0.55f, c.y - h * 0.86f},
                             {c.x + h * 0.55f, c.y + h * 0.86f}, col);
            break;
        default:
            ren.drawTriangle({c.x + h, c.y}, {c.x - h * 0.55f, c.y - h * 0.86f},
                             {c.x - h * 0.55f, c.y + h * 0.86f}, col);
            break;
    }
}

struct KeyHint {
    char key;
    std::string label;
};

// "(A) Resume   (B) Unlock" centred as one row, with the key cap drawn rather
// than embedded in the translated string.
void drawKeyHintRow(nxui::Renderer& ren, const std::vector<KeyHint>& hints, float y,
                    nxui::Font* font, const nxui::Color& ink, const nxui::Color& soft,
                    float fade, float dx, float dy, float zoom) {
    if (!font || hints.empty()) return;
    constexpr float kCapRadius = 11.f;
    constexpr float kCapGap = 9.f;
    constexpr float kItemGap = 26.f;
    float total = 0.f;
    for (std::size_t i = 0; i < hints.size(); ++i) {
        total += kCapRadius * 2.f + kCapGap + font->measure(hints[i].label).x;
        if (i + 1 != hints.size()) total += kItemGap;
    }
    float x = (kScreenW - total) * 0.5f;
    for (const auto& hint : hints) {
        const nxui::Vec2 cap = mapLockPt(x + kCapRadius, y + kCapRadius, dx, dy, zoom);
        ren.drawCircle(cap, kCapRadius * zoom, ink.withAlpha(0.13f * fade), 22);
        const std::string key(1, hint.key);
        const nxui::Vec2 ks = font->measure(key);
        ren.drawText(key, {cap.x - ks.x * zoom * 0.5f, cap.y - ks.y * zoom * 0.5f}, font,
                     soft.withAlpha(0.92f * fade), zoom);
        const float labelX = x + kCapRadius * 2.f + kCapGap;
        const nxui::Vec2 ls = font->measure(hint.label);
        const nxui::Vec2 lc = mapLockPt(labelX + ls.x * 0.5f, y + kCapRadius, dx, dy, zoom);
        ren.drawText(hint.label, {lc.x - ls.x * zoom * 0.5f, lc.y - ls.y * zoom * 0.5f}, font,
                     ink.withAlpha(0.94f * fade), zoom);
        x = labelX + ls.x + kItemGap;
    }
}
}

LockScreen::LockScreen() {
    setRect({0.f, 0.f, kScreenW, kScreenH});
    setVisible(false);
    setFocusable(true);
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setLiquidGlassShaderEnabled(false);
    setBlurEnabled(false);
    setFrameworkTouchEnabled(false);
    wireActions();
}

void LockScreen::wireActions() {
    clearActions();
    if (m_padCodeEnabled) {
        // Only the four directions, Select (submit) and B (clear) do anything.
        // Nothing here can reach unlock() directly.
        addAction(static_cast<std::uint64_t>(nxui::Button::DUp),
                  [this]() { appendPadStep('U'); });
        addAction(static_cast<std::uint64_t>(nxui::Button::DDown),
                  [this]() { appendPadStep('D'); });
        addAction(static_cast<std::uint64_t>(nxui::Button::DLeft),
                  [this]() { appendPadStep('L'); });
        addAction(static_cast<std::uint64_t>(nxui::Button::DRight),
                  [this]() { appendPadStep('R'); });
        addAction(static_cast<std::uint64_t>(nxui::Button::A),
                  [this]() { submitPadCode(); });
        addAction(static_cast<std::uint64_t>(nxui::Button::Plus),
                  [this]() { submitPadCode(); });
        addAction(static_cast<std::uint64_t>(nxui::Button::B),
                  [this]() { clearPadInput(); });
        return;
    }
    auto primary = [this]() {
        if (m_canResume)
            resumeGame();
        else
            unlock();
    };
    for (auto button : {nxui::Button::A, nxui::Button::Plus})
        addAction(static_cast<std::uint64_t>(button), primary);

    auto secondary = [this]() { unlock(); };
    for (auto button : {nxui::Button::B, nxui::Button::X, nxui::Button::Y,
                        nxui::Button::L, nxui::Button::R, nxui::Button::ZL, nxui::Button::ZR,
                        nxui::Button::Minus,
                        nxui::Button::DUp, nxui::Button::DDown,
                        nxui::Button::DLeft, nxui::Button::DRight})
        addAction(static_cast<std::uint64_t>(button), secondary);
}

void LockScreen::warmTimeFont() {
    if (!m_gpu || !m_ren || m_timeFontPath.empty())
        return;
    if (m_timeFont && m_timeFont->ptSize() == kTimeFontPt)
        return;
    auto font = std::make_unique<nxui::Font>();
    if (font->load(*m_gpu, *m_ren, m_timeFontPath, kTimeFontPt))
        m_timeFont = std::move(font);
}

void LockScreen::loadPendingHero() {
    if (m_hasHero) {
        m_heroTex = nxui::Texture();
        m_hasHero = false;
    }
    if (!m_gpu || !m_ren || m_pendingHero.empty() ||
        m_pendingHero.size() != 1280u * 720u * 4u)
        return;
    constexpr std::uint64_t kTextReserve = 6u * 1024u * 1024u;
    constexpr std::uint64_t kFullBytes = 1280ull * 720ull * 4ull;
    if (m_gpu->imageMemoryAvailable() <= kTextReserve + kFullBytes) {
        // Not enough headroom for a 3.5 MiB hero: a half-res box copy beats no
        // artwork at all, but it is only the low-memory fallback.
        std::vector<std::uint8_t> small(640u * 360u * 4u);
        for (unsigned y = 0; y < 360u; ++y) {
            for (unsigned x = 0; x < 640u; ++x) {
                for (unsigned c = 0; c < 4u; ++c) {
                    const std::uint8_t* src =
                        m_pendingHero.data() + ((y * 2u) * 1280u + x * 2u) * 4u + c;
                    small[(y * 640u + x) * 4u + c] = static_cast<std::uint8_t>(
                        (src[0] + src[4] + src[1280u * 4u] + src[1280u * 4u + 4]) / 4u);
                }
            }
        }
        m_hasHero = m_heroTex.loadFromPixels(*m_gpu, *m_ren, small.data(), 640, 360);
        return;
    }
    // Full 1280x720 straight from the SteamGridDB hero. ArtworkCache already
    // did the bilinear scaling; halving it here only threw away half the rows
    // before the GPU stretched it back to 720p.
    m_hasHero = m_heroTex.loadFromPixels(*m_gpu, *m_ren, m_pendingHero.data(), 1280, 720);
}

void LockScreen::loadPendingIcon() {
    if (m_hasIcon) {
        m_iconTex = nxui::Texture();
        m_hasIcon = false;
    }
    if (!m_gpu || !m_ren || m_pendingIcon.empty() || m_pendingIconW <= 0 || m_pendingIconH <= 0)
        return;
    if (m_pendingIcon.size() != static_cast<size_t>(m_pendingIconW) * m_pendingIconH * 4u)
        return;
    constexpr std::uint64_t kTextReserve = 4u * 1024u * 1024u;
    if (m_gpu->imageMemoryAvailable() <= kTextReserve +
        static_cast<std::uint64_t>(m_pendingIconW) * m_pendingIconH * 4u)
        return;
    m_hasIcon = m_iconTex.loadFromPixels(*m_gpu, *m_ren, m_pendingIcon.data(),
                                         m_pendingIconW, m_pendingIconH);
}

void LockScreen::show(bool instant) {
    auto& i18n = nxui::I18n::instance();
    m_padInput.clear();
    m_padErrorT = 0.f;
    if (m_padCodeEnabled) {
        m_unlockText = i18n.tr("lock.enter_code", "Enter the D-pad code");
        m_resumeText.clear();
    } else if (m_canResume) {
        // Short labels: the button chips carry the key, so the text no longer
        // repeats "Press A" inside a pill that already looks pressable.
        m_unlockText = i18n.tr("lock.unlock_short", "Unlock");
        m_resumeText = i18n.tr("lock.resume_short", "Resume");
    } else {
        m_unlockText = i18n.tr("lock.enter_short", "Enter");
        m_resumeText.clear();
    }

    warmTimeFont();
    loadPendingHero();
    loadPendingIcon();
    m_pendingHero.clear();
    m_pendingHero.shrink_to_fit();
    m_pendingIcon.clear();
    m_pendingIcon.shrink_to_fit();
    m_pendingIconW = 0;
    m_pendingIconH = 0;
    m_releaseTimer = 0.f;

    m_active = true;
    m_unlocking = false;
    m_awaitEnterSettle = !instant;
    m_age = 0.f;
    m_clockTimer = 0.f;
    m_atmosPhase = 0.f;
    m_suspendPulse = 0.f;
    m_dateRevealStarted = instant;
    m_lastMinute = -1;
    resetSwipe();
    m_curtain.setImmediate(0.f);
    m_zoom.setImmediate(1.f);
    if (instant) {
        m_fade.setImmediate(1.f);
        m_dateReveal.setImmediate(1.f);
    } else {
        float enterDur = kCurtainEnterSec;
        if (m_transition == Transition::Dissolve) enterDur = kDissolveEnterSec;
        else if (m_transition == Transition::Zoom) enterDur = kZoomEnterSec;
        else if (m_transition == Transition::Wipe) enterDur = kWipeEnterSec;

        // Curtain / Wipe enter mirrors exit: opaque plane from frame one, only
        // the curtain/clip moves. Dissolve / Zoom fade the whole plane together.
        if (m_transition == Transition::Curtain || m_transition == Transition::Wipe) {
            m_fade.setImmediate(1.f);
            m_dateReveal.setImmediate(1.f);
            m_dateRevealStarted = true;
            m_curtain.setImmediate(1.f);
            m_curtain.set(0.f, enterDur, nxui::Easing::inOutCubic, kEnterAnimDelay);
        } else if (m_transition == Transition::Zoom) {
            m_fade.setImmediate(0.f);
            m_dateReveal.setImmediate(0.f);
            m_dateRevealStarted = true;
            const float z0 = (m_zoomDir == 1) ? kZoomInEnter : kZoomOutEnter;
            m_zoom.setImmediate(z0);
            m_fade.set(1.f, enterDur, nxui::Easing::inOutCubic, kEnterAnimDelay);
            m_dateReveal.set(1.f, enterDur, nxui::Easing::inOutCubic, kEnterAnimDelay);
            m_zoom.set(1.f, enterDur, nxui::Easing::inOutCubic, kEnterAnimDelay);
        } else { // Dissolve
            m_fade.setImmediate(0.f);
            m_dateReveal.setImmediate(0.f);
            m_dateRevealStarted = true;
            m_zoom.setImmediate(kDissolveEnterScale);
            m_fade.set(1.f, enterDur, nxui::Easing::inOutCubic, kEnterAnimDelay);
            m_dateReveal.set(1.f, enterDur, nxui::Easing::inOutCubic, kEnterAnimDelay);
            m_zoom.set(1.f, enterDur, nxui::Easing::inOutCubic, kEnterAnimDelay);
        }
    }
    refreshClock();
    setVisible(true);
}

void LockScreen::hide() {
    m_active = false;
    m_unlocking = false;
    m_awaitEnterSettle = false;
    setVisible(false);
    m_releaseTimer = 0.f;
    resetSwipe();
    m_curtain.setImmediate(0.f);
}

void LockScreen::releaseArt() {
    if (m_hasHero)
        m_heroTex = nxui::Texture();
    m_hasHero = false;
    if (m_hasIcon)
        m_iconTex = nxui::Texture();
    m_hasIcon = false;
}

void LockScreen::tickRelease(float dt) {
    if (m_active || (!m_hasHero && !m_hasIcon))
        return;
    m_releaseTimer += dt;
    if (m_releaseTimer >= 1.5f)
        releaseArt();
}

void LockScreen::resetSwipe() {
    m_swipeActive = false;
    m_swipeDragging = false;
    m_curtainHomeShown = false;
    m_swipeStartY = 0.f;
}

void LockScreen::beginCurtainUnlock() {
    if (!m_active || m_unlocking)
        return;
    m_unlocking = true;
    m_swipeActive = false;
    m_swipeDragging = false;

    // Dissolve/Zoom: defer HOME chrome until the Lock plane has mostly faded so
    // the suspended tile under Lock doesn't pop early / stick around.
    // Curtain/Wipe need HOME under the moving plane immediately.
    const bool deferHome = (m_transition == Transition::Dissolve
                            || m_transition == Transition::Zoom);
    if (!deferHome && !m_curtainHomeShown && m_onUnlocking) {
        m_onUnlocking();
        m_curtainHomeShown = true;
    }

    // Brief delay so the HOME restore hitch doesn't eat the first frames.
    if (m_transition == Transition::Dissolve) {
        m_curtain.setImmediate(0.f);
        m_fade.set(0.f, kDissolveExitSec, nxui::Easing::inOutCubic, kUnlockAnimDelay);
        m_dateReveal.set(0.f, kDissolveExitSec, nxui::Easing::inOutCubic, kUnlockAnimDelay);
        m_zoom.set(kDissolveEnterScale, kDissolveExitSec, nxui::Easing::inOutCubic,
                   kUnlockAnimDelay);
        return;
    }

    if (m_transition == Transition::Zoom) {
        const float z1 = (m_zoomDir == 1) ? kZoomInExit : kZoomOutExit;
        m_fade.set(0.f, kZoomExitSec, nxui::Easing::inOutCubic, kUnlockAnimDelay);
        m_dateReveal.set(0.f, kZoomExitSec, nxui::Easing::inOutCubic, kUnlockAnimDelay);
        m_zoom.set(z1, kZoomExitSec, nxui::Easing::inOutCubic, kUnlockAnimDelay);
        m_curtain.set(1.f, kZoomExitSec, nxui::Easing::linear, kUnlockAnimDelay);
        return;
    }

    if (m_transition == Transition::Wipe) {
        m_fade.setImmediate(1.f);
        const float remaining = std::clamp(1.f - m_curtain.value(), 0.05f, 1.f);
        m_curtain.set(1.f, kWipeExitSec * remaining, nxui::Easing::inOutCubic, kUnlockAnimDelay);
        return;
    }

    m_fade.setImmediate(1.f);
    const float remaining = std::clamp(1.f - m_curtain.value(), 0.05f, 1.f);
    m_curtain.set(1.f, kCurtainUnlockSec * remaining, nxui::Easing::inOutCubic, kUnlockAnimDelay);
}

void LockScreen::unlock() {
    if (!m_active || m_unlocking || m_age < kInputGuard)
        return;
    // With a passcode set only the code path may open the view, so swipe,
    // physical HOME and the button map cannot bypass it.
    if (m_padCodeEnabled)
        return;
    beginCurtainUnlock();
}

void LockScreen::setPadCode(bool enabled, const std::string& code) {
    std::string clean;
    for (char c : code) {
        if (c == 'U' || c == 'D' || c == 'L' || c == 'R')
            clean.push_back(c);
        if (clean.size() == 4)
            break;
    }
    while (clean.size() < 4)
        clean.push_back('U');
    m_padCodeEnabled = enabled;
    m_padCode = std::move(clean);
    m_padInput.clear();
    m_padErrorT = 0.f;
    wireActions();
}

void LockScreen::appendPadStep(char step) {
    if (!m_active || m_unlocking || m_age < kInputGuard)
        return;
    if (m_padInput.size() >= 4)
        m_padInput.clear();
    m_padInput.push_back(step);
    m_padErrorT = 0.f;
    if (m_padInput.size() == 4)
        submitPadCode();
}

void LockScreen::submitPadCode() {
    if (!m_active || m_unlocking || m_age < kInputGuard)
        return;
    if (m_padInput.size() == 4 && m_padInput == m_padCode) {
        m_padInput.clear();
        m_padErrorT = 0.f;
        beginCurtainUnlock();
        return;
    }
    m_padInput.clear();
    m_padErrorT = 1.4f;
}

void LockScreen::clearPadInput() {
    m_padInput.clear();
    m_padErrorT = 0.f;
}

void LockScreen::resumeGame() {
    if (!m_active || !m_canResume || m_unlocking || m_age < kInputGuard)
        return;
    // Keep Lock painted until the suspended title takes the foreground —
    // hiding here flashed HOME for a frame.
    if (m_onResume)
        m_onResume();
}

void LockScreen::handleTouch(nxui::Input& input) {
    if (!m_active || m_unlocking)
        return;

    if (input.touchDown()) {
        if (m_age < kInputGuard)
            return;
        m_swipeActive = true;
        m_swipeDragging = false;
        m_swipeStartY = input.touchY();
        return;
    }

    if (!m_swipeActive)
        return;

    if (input.isTouching()) {
        const float dy = m_swipeStartY - input.touchY(); // up = positive
        if (!m_swipeDragging) {
            if (std::fabs(input.touchDeltaX()) > std::fabs(dy) + 12.f &&
                std::fabs(input.touchDeltaX()) > kSwipeDeadzone) {
                return;
            }
            if (dy < kSwipeDeadzone)
                return;
            m_swipeDragging = true;
        }
        const float progress = std::clamp(dy / kCurtainTravel, 0.f, 1.f);
        m_curtain.setImmediate(progress);
        if (m_transition == Transition::Dissolve)
            m_fade.setImmediate(1.f - progress);
        else if (m_transition == Transition::Zoom) {
            m_fade.setImmediate(1.f - progress * 0.9f);
            const float z1 = (m_zoomDir == 1) ? kZoomInExit : kZoomOutExit;
            m_zoom.setImmediate(1.f + (z1 - 1.f) * progress);
        } else {
            m_fade.setImmediate(1.f);
            // Slight compression while pulling gives the drag some weight.
            m_zoom.setImmediate(1.f - 0.012f * progress);
        }
        return;
    }

    if (!input.touchUp())
        return;

    const bool wasDragging = m_swipeDragging;
    const float progress = m_curtain.value();
    const float flick = m_swipeStartY - input.touchY();
    m_swipeActive = false;
    m_swipeDragging = false;

    if (wasDragging) {
        if (progress >= kSwipeCommit || flick >= kSwipeFlickPx) {
            if (m_transition != Transition::Zoom)
                m_zoom.set(1.f, 0.30f, nxui::Easing::outCubic);
            beginCurtainUnlock();
        } else {
            m_curtain.set(0.f, 0.30f, nxui::Easing::outCubic);
            m_fade.set(1.f, 0.30f, nxui::Easing::outCubic);
            // outBack springs the compressed plane back with a small overshoot.
            m_zoom.set(1.f, 0.36f, nxui::Easing::outBack);
        }
        return;
    }

    if (m_canResume) {
        const auto r = resumeButtonRect(0.f);
        const float x = input.touchX();
        const float y = input.touchY();
        if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height) {
            resumeGame();
            return;
        }
        const auto ir = iconRect(0.f);
        if (x >= ir.x && x <= ir.x + ir.width && y >= ir.y && y <= ir.y + ir.height) {
            resumeGame();
            return;
        }
    }
    unlock();
}

void LockScreen::refreshClock() {
    if (!m_clock)
        return;
    const auto snapshot = m_clock->refresh();
    if (!snapshot.valid)
        return;
    const auto& cal = snapshot.calendar;
    if (static_cast<int>(cal.hour) * 60 + cal.minute == m_lastMinute)
        return;
    m_lastMinute = static_cast<int>(cal.hour) * 60 + cal.minute;

    char buf[32];
    if (m_use12Hour) {
        int hour = cal.hour % 12;
        if (hour == 0) hour = 12;
        std::snprintf(buf, sizeof(buf), "%d:%02d", hour, cal.minute);
    } else {
        std::snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(cal.hour),
                      static_cast<unsigned>(cal.minute));
    }
    m_timeStr = buf;

    auto& i18n = nxui::I18n::instance();
    static const char* const kWeekdayFallback[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* const kMonthFallback[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"};
    const unsigned wday = std::min<unsigned>(snapshot.additional.wday, 6u);
    const unsigned month = std::clamp<unsigned>(cal.month, 1u, 12u);
    std::string weekday = i18n.tr("lock.weekday." + std::to_string(wday), kWeekdayFallback[wday]);
    std::string monthName = i18n.tr("lock.month." + std::to_string(month), kMonthFallback[month - 1]);
    std::string out = i18n.tr("lock.date_format", "{weekday}, {month} {day}");
    out = replaceAll(out, "{weekday}", weekday);
    out = replaceAll(out, "{month}", monthName);
    out = replaceAll(out, "{day}", std::to_string(static_cast<unsigned>(cal.day)));
    out = replaceAll(out, "{year}", std::to_string(static_cast<unsigned>(cal.year)));
    m_dateStr = out;
}

void LockScreen::update(float dt) {
    if (m_padErrorT > 0.f)
        m_padErrorT = std::max(0.f, m_padErrorT - dt);
    if (!m_active)
        return;
    m_age += dt;
    m_atmosPhase += dt;
    if (m_canResume)
        m_suspendPulse += dt * 1.25f; // ~0.2 Hz: a resting breath, not a blink
    // Curtain / fade / zoom / dateReveal are driven by AnimationManager —
    // do not double-tick them here (that made unlocks feel truncated).
    m_clockTimer += dt;
    if (m_clockTimer >= 1.f) {
        m_clockTimer = 0.f;
        refreshClock();
    }
    if (!m_dateRevealStarted && !m_unlocking && m_age >= kDateSettleDelay) {
        m_dateRevealStarted = true;
        m_dateReveal.set(1.f, 0.52f, nxui::Easing::outCubic);
    }

    if (m_awaitEnterSettle && !m_unlocking) {
        bool settled = false;
        if (m_transition == Transition::Curtain || m_transition == Transition::Wipe)
            settled = m_curtain.value() <= 0.005f;
        else if (m_transition == Transition::Zoom)
            settled = m_fade.value() >= 0.995f && std::fabs(m_zoom.value() - 1.f) < 0.01f;
        else
            settled = m_fade.value() >= 0.995f;
        if (settled) {
            m_awaitEnterSettle = false;
            if (m_onEnterSettled)
                m_onEnterSettled();
        }
    }

    // Bring HOME under Dissolve/Zoom once the Lock plane is mostly gone.
    if (m_unlocking && !m_curtainHomeShown && keepsHomeUnderlay()
        && m_fade.value() <= 0.42f && m_onUnlocking) {
        m_onUnlocking();
        m_curtainHomeShown = true;
    }

    bool done = false;
    if (m_unlocking) {
        if (m_transition == Transition::Dissolve)
            done = m_fade.value() <= 0.01f;
        else if (m_transition == Transition::Zoom) {
            const float z1 = (m_zoomDir == 1) ? kZoomInExit : kZoomOutExit;
            const bool zoomDone = (m_zoomDir == 1)
                ? (m_zoom.value() <= z1 + 0.02f)
                : (m_zoom.value() >= z1 - 0.02f);
            done = m_fade.value() <= 0.02f && zoomDone;
        } else
            done = m_curtain.value() >= 0.999f;
    }
    if (done) {
        m_active = false;
        m_unlocking = false;
        setVisible(false);
        if (m_onUnlocked)
            m_onUnlocked();
    }
}

void LockScreen::drawAtmosphere(nxui::Renderer& ren, float dx, float dy, float zoom,
                                float fade) const {
    if (fade <= 0.01f)
        return;
    auto at = [&](nxui::Rect r) {
        const nxui::Vec2 tl = mapLockPt(r.x, r.y, dx, dy, zoom);
        const nxui::Vec2 br = mapLockPt(r.x + r.width, r.y + r.height, dx, dy, zoom);
        return nxui::Rect{tl.x, tl.y, br.x - tl.x, br.y - tl.y};
    };
    // Breathe gently: a 0.10 pulse reads as a flicker next to a clock face.
    const float breath = 0.5f + 0.5f * std::sin(m_atmosPhase * 0.55f);
    const float vig = (m_hasHero ? 0.32f : 0.26f) + 0.045f * breath;
    const nxui::Color edge(0.f, 0.f, 0.f, vig * fade);
    const nxui::Color clear(0.f, 0.f, 0.f, 0.f);

    // Four-sided frame: corners darken from both axes, so the still stops
    // reading as a letterboxed photo.
    ren.drawGradientRect(at({0.f, 0.f, kScreenW, 200.f}), edge, clear);
    ren.drawGradientRect(at({0.f, kScreenH - 230.f, kScreenW, 230.f}), clear,
                         nxui::Color(0.f, 0.f, 0.f, std::min(1.f, vig + 0.12f) * fade));
    ren.drawGradientRectH(at({0.f, 0.f, 190.f, kScreenH}),
                          nxui::Color(0.f, 0.f, 0.f, vig * 0.58f * fade), clear);
    ren.drawGradientRectH(at({kScreenW - 190.f, 0.f, 190.f, kScreenH}), clear,
                          nxui::Color(0.f, 0.f, 0.f, vig * 0.58f * fade));

    if (m_theme) {
        // Accent haze at the foot ties the still to the UI accent.
        const nxui::Color accent = m_theme->cursorNormal;
        ren.drawGradientRect(at({0.f, kScreenH - 300.f, kScreenW, 300.f}),
                             accent.withAlpha(0.f), accent.withAlpha(0.055f * fade));
    }

    if (m_hasHero) {
        // Cinematic bottom wash so Resume sits in the poster.
        ren.drawGradientRect(at({0.f, 300.f, kScreenW, 420.f}), clear,
                             nxui::Color(0.f, 0.f, 0.f, 0.55f * fade));
    }
}

void LockScreen::drawClock(nxui::Renderer& ren, float dx, float dy, float zoom, float fade,
                           const nxui::Color& ink, const nxui::Color& inkSoft) const {
    // Date: tracked uppercase caption, small enough to sit under the clock in
    // the hierarchy instead of competing with it.
    nxui::Font* dateFont = m_smallFont ? m_smallFont : m_font;
    const float dateT = std::clamp(m_dateReveal.value(), 0.f, 1.f);
    const float dateDrop = (1.f - dateT) * 14.f;
    const float dateY = 64.f + dateDrop;
    const std::string date = asciiUpper(m_dateStr);
    const bool overImage = m_hasHero || static_cast<bool>(m_onDrawBackdrop);
    // A still behind the clock needs contrast protection; a flat theme
    // background does not, and a black shadow would only dirty a light theme.
    if (overImage) {
        drawCenteredTracked(ren, date, dateY + 2.5f, dateFont,
                            nxui::Color(0.f, 0.f, 0.f, 0.20f * dateT), 1.f, 2.4f, dx, dy, zoom);
    }
    drawCenteredTracked(ren, date, dateY, dateFont, inkSoft.withAlpha(inkSoft.a * dateT * 0.9f),
                        1.f, 2.4f, dx, dy, zoom);

    const float colonPulse = 0.74f + 0.26f * (0.5f + 0.5f * std::sin(m_atmosPhase * 1.9f));
    const float baseY = 118.f;

    // Glyph layout, shared by the shadow pass and the lit pass.
    struct Glyph {
        std::string text;
        float x;
        float w;
    };
    std::vector<Glyph> glyphs;
    float totalW = 0.f;
    float glyphH = 0.f;
    if (m_timeFont) {
        const auto colon = m_timeStr.find(':');
        if (colon != std::string::npos) {
            const std::string left = m_timeStr.substr(0, colon);
            const std::string right = m_timeStr.substr(colon + 1);
            const nxui::Vec2 ls = m_timeFont->measure(left);
            const nxui::Vec2 cs = m_timeFont->measure(":");
            const nxui::Vec2 rs = m_timeFont->measure(right);
            glyphH = ls.y;
            totalW = ls.x + cs.x + rs.x;
            const float x0 = (kScreenW - totalW) * 0.5f;
            glyphs.push_back({left, x0, ls.x});
            glyphs.push_back({":", x0 + ls.x, cs.x});
            glyphs.push_back({right, x0 + ls.x + cs.x, rs.x});
        } else {
            const nxui::Vec2 ts = m_timeFont->measure(m_timeStr);
            glyphH = ts.y;
            totalW = ts.x;
            glyphs.push_back({m_timeStr, (kScreenW - totalW) * 0.5f, ts.x});
        }
    } else if (m_font) {
        const nxui::Vec2 ts = m_font->measure(m_timeStr);
        glyphH = ts.y * 2.35f;
        totalW = ts.x * 2.35f;
        glyphs.push_back({m_timeStr, (kScreenW - totalW) * 0.5f, totalW});
    }

    nxui::Font* timeFont = m_timeFont ? m_timeFont.get() : m_font;
    if (!timeFont || totalW <= 0.f)
        return;
    const float glyphScale = m_timeFont ? zoom : 2.35f * zoom;
    auto glyphOrigin = [&](const Glyph& g, float lift) {
        const nxui::Vec2 c =
            mapLockPt(g.x + g.w * 0.5f, baseY + glyphH * 0.5f + lift, dx, dy, zoom);
        return nxui::Vec2{c.x - g.w * zoom * 0.5f, c.y - glyphH * zoom * 0.5f};
    };

    if (overImage) {
        for (const auto& g : glyphs)
            ren.drawText(g.text, glyphOrigin(g, 2.4f), timeFont,
                         nxui::Color(0.f, 0.f, 0.f, 0.20f * fade), glyphScale);
    }

    // Light travelling across the digits. Every glyph mixes toward white by its
    // own distance from the sweep centre, so there is no clipping edge to see.
    const float band = std::max(52.f, totalW * 0.26f);
    const float sweep = (0.5f + 0.5f * std::sin(m_atmosPhase * 0.75f)) * totalW;
    for (const auto& g : glyphs) {
        nxui::Color c = (g.text == ":") ? ink.withAlpha(ink.a * colonPulse) : ink;
        const float gc = g.x + g.w * 0.5f;
        float a = std::clamp(1.f - std::fabs(gc - sweep) / band, 0.f, 1.f);
        a = a * a * (3.f - 2.f * a);
        if (a > 0.001f)
            c = nxui::Color::lerp(c, nxui::Color::white(), 0.5f * a).withAlpha(c.a);
        ren.drawText(g.text, glyphOrigin(g, 0.f), timeFont, c, glyphScale);
    }
}

void LockScreen::render(nxui::Renderer& ren) {
    if (!m_active || !m_theme || !m_font)
        return;

    const float curtainT = m_curtain.value();
    const float zoom = std::max(0.01f, m_zoom.value());
    // Wallpaper/hero never shrink below fullscreen — only grow on Zoom Out.
    const float bgZoom = std::max(zoom, 1.f);
    // Wipe stays put — clip only. Curtain is the only plane that translates.
    const float dx = 0.f;
    const float dy = (m_transition == Transition::Curtain) ? (-curtainT * kCurtainTravel) : 0.f;
    if (dy <= -kScreenH)
        return;

    // Zoom: scale Lock chrome around screen center (BG uses bgZoom separately).
    auto atZoom = [&](nxui::Rect r, float z) {
        if (std::fabs(z - 1.f) < 0.001f) {
            r.x += dx;
            r.y += dy;
            return r;
        }
        const nxui::Vec2 tl = mapLockPt(r.x, r.y, dx, dy, z);
        const nxui::Vec2 br = mapLockPt(r.x + r.width, r.y + r.height, dx, dy, z);
        return nxui::Rect{tl.x, tl.y, br.x - tl.x, br.y - tl.y};
    };
    auto at = [&](nxui::Rect r) { return atZoom(r, zoom); };

    const nxui::Theme& th = *m_theme;
    const float fade = std::clamp(m_fade.value(), 0.f, 1.f);
    const bool curtainMoving = m_transition == Transition::Curtain
        && (m_unlocking || m_swipeDragging || curtainT > 0.001f || m_awaitEnterSettle);
    const bool wipeMoving = m_transition == Transition::Wipe
        && (m_unlocking || m_swipeDragging || curtainT > 0.001f || m_awaitEnterSettle);
    // Dissolve/Zoom: paint the Lock plane (wallpaper + chrome) as one unit so
    // they fade/zoom together over HOME — gradient now respects opacity.
    const bool dissolveZoomPlane = keepsHomeUnderlay()
        && (m_unlocking || m_awaitEnterSettle || fade < 0.995f
            || std::fabs(zoom - 1.f) > 0.002f);

    bool clipped = false;
    if (m_transition == Transition::Wipe
        && (curtainT > 0.001f || m_unlocking || m_awaitEnterSettle || m_swipeDragging)) {
        const float remain = std::clamp(1.f - curtainT, 0.f, 1.f);
        nxui::Rect clip = {0.f, 0.f, kScreenW, kScreenH};
        switch (m_wipeDir) {
            case 0: // Up — wipe reveals from bottom
                clip = {0.f, kScreenH * (1.f - remain), kScreenW, kScreenH * remain};
                break;
            case 2: // Down — wipe reveals from top
                clip = {0.f, 0.f, kScreenW, kScreenH * remain};
                break;
            case 3: // Left — wipe reveals from right
                clip = {kScreenW * (1.f - remain), 0.f, kScreenW * remain, kScreenH};
                break;
            default: // Right — wipe reveals from left
                clip = {0.f, 0.f, kScreenW * remain, kScreenH};
                break;
        }
        ren.pushClipRect(clip);
        clipped = true;
    }

    const bool paintPlane = m_hasHero || curtainMoving || wipeMoving || dissolveZoomPlane;
    if (paintPlane && fade > 0.01f) {
        if (m_hasHero) {
            const nxui::Rect full = atZoom({0.f, 0.f, kScreenW, kScreenH}, bgZoom);
            ren.drawTexture(&m_heroTex, full, nxui::Color::white().withAlpha(fade));
            // Grade the still into the theme palette before the contrast scrim.
            ren.drawRect(full, th.backgroundAccent.withAlpha(0.10f * fade));
            ren.drawGradientRect(full, nxui::Color::white().withAlpha(0.f),
                                 th.cursorNormal.withAlpha(0.10f * fade));
            ren.drawRect(full, nxui::Color(0.f, 0.f, 0.f, (m_unlocking ? 0.52f : 0.40f) * fade));
        } else if (m_onDrawBackdrop) {
            m_onDrawBackdrop(ren, dy, fade, bgZoom);
        } else {
            ren.drawRect(atZoom({0.f, 0.f, kScreenW, kScreenH}, bgZoom),
                         th.background.withAlpha(fade));
        }
    }

    // Vignette is part of the BG wash — keep it fullscreen on Zoom In.
    drawAtmosphere(ren, dx, dy, bgZoom, fade);

    const bool lightInk = m_hasHero;
    const nxui::Color ink = (lightInk ? nxui::Color(1.f, 1.f, 1.f, 1.f) : th.textPrimary)
                                .withAlpha(fade);
    const nxui::Color inkSoft = (lightInk ? nxui::Color(1.f, 1.f, 1.f, 1.f) : th.textSecondary)
                                    .withAlpha((lightInk ? 0.82f : 0.90f) * fade);
    nxui::Font* small = m_smallFont ? m_smallFont : m_font;

    if (m_battery >= 0) {
        const std::string pct = std::to_string(m_battery) + "%";
        const nxui::Vec2 psz = small->measure(pct);
        const float right = kScreenW - kMargin;
        const nxui::Vec2 pctC = mapLockPt(right - psz.x * 0.5f, 74.f, dx, dy, zoom);
        ren.drawText(pct, {pctC.x - psz.x * zoom * 0.5f, pctC.y - psz.y * zoom * 0.5f},
                     small, inkSoft, zoom);
        // Sits on the date's optical line instead of floating in the corner.
        const nxui::Rect body = at({right - psz.x - 52.f, 64.f, 40.f, 20.f});
        ren.drawRoundedRectOutline(body, inkSoft, 4.f * zoom, 1.6f * zoom);
        ren.drawRect({body.right() + 2.f * zoom, body.y + 5.f * zoom, 3.f * zoom, 10.f * zoom}, inkSoft);
        const float fill = std::clamp(m_battery / 100.f, 0.f, 1.f);
        ren.drawRoundedRect({body.x + 2.5f * zoom, body.y + 2.5f * zoom,
                             (body.width - 5.f * zoom) * fill, body.height - 5.f * zoom},
                            th.cursorNormal.withAlpha(fade * 0.92f), 2.5f * zoom);
        if (m_charging) {
            // Overlaps the shell and spills past its edges, mirroring the HUD.
            const float boltH = body.height * 1.24f;
            const float boltW = boltH * 0.56f;
            switchu::battery_drawing::drawLightningBolt(
                ren, {body.x + (body.width - boltW) * 0.5f,
                      body.y + (body.height - boltH) * 0.5f,
                      boltW, boltH},
                ink, th.background.withAlpha(fade), 1.f);
        }
    }

    drawClock(ren, dx, dy, zoom, fade, ink, inkSoft);

    if (m_padCodeEnabled) {
        const float cellW = 46.f;
        const float cellH = 58.f;
        const float cellGap = 14.f;
        const float totalW = cellW * 4.f + cellGap * 3.f;
        const float x0 = (kScreenW - totalW) * 0.5f;
        const float y0 = 352.f;
        const nxui::Color bad(0.95f, 0.36f, 0.34f, 1.f);
        const bool error = m_padErrorT > 0.f;
        const nxui::Color tick = error ? bad : ink;
        // Short decaying shake on a wrong entry.
        const float shake = error ? std::sin(m_padErrorT * 46.f) * 4.f *
                                        std::min(1.f, m_padErrorT / 1.4f)
                                  : 0.f;
        const int entered = static_cast<int>(m_padInput.size());
        for (int i = 0; i < 4; ++i) {
            const nxui::Rect cell = at({x0 + static_cast<float>(i) * (cellW + cellGap) + shake,
                                        y0, cellW, cellH});
            const bool filled = i < entered;
            ren.drawRoundedRect(cell, tick.withAlpha(fade * (filled ? 0.22f : 0.09f)),
                                12.f * zoom);
            ren.drawRoundedRectOutline(cell, tick.withAlpha(fade * (filled ? 0.95f : 0.42f)),
                                       12.f * zoom, 2.f * zoom);
            if (filled) {
                // Arrow, not a letter: the code is directional.
                drawDirGlyph(ren, m_padInput[static_cast<std::size_t>(i)],
                             {cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f},
                             15.f * zoom, tick.withAlpha(fade));
            }
            // Underline the step being entered.
            if (!error && entered < 4 && i == entered) {
                ren.drawRoundedRect({cell.x + 6.f * zoom, cell.y + cell.height + 7.f * zoom,
                                     cell.width - 12.f * zoom, 3.f * zoom},
                                    th.cursorNormal.withAlpha(fade * 0.85f), 1.5f * zoom);
            }
        }
        const std::string hint = error
            ? nxui::I18n::instance().tr("lock.code_wrong", "Wrong code")
            : nxui::I18n::instance().tr("lock.code_hint",
                                        "Press Up / Down / Left / Right. B clears.");
        drawCentered(ren, hint, y0 + cellH + 34.f, m_smallFont ? m_smallFont : m_font,
                     error ? bad.withAlpha(fade) : inkSoft, 0.92f, dx, dy, zoom);
    }

    if (m_canResume && !m_padCodeEnabled) {
        if (m_hasIcon) {
            const float pulse = 0.5f + 0.5f * std::sin(m_suspendPulse);
            // One ring, a 1% breath and a static badge: calm, not busy.
            const nxui::Rect base = iconRect(0.f);
            const float grow = kIconSize * 0.005f * pulse;
            const nxui::Rect ir = at({base.x - grow, base.y - grow,
                                      base.width + grow * 2.f, base.height + grow * 2.f});
            // Tint with fade so the resume tile dissolves with the Lock plane.
            ren.drawTextureRounded(&m_iconTex, ir, kIconRadius * zoom,
                                   nxui::Color::white().withAlpha(fade));

            const nxui::Color glow = th.cursorNormal;
            ren.drawRoundedRectOutline(ir.expanded(2.f * zoom),
                                       glow.withAlpha((0.50f + 0.28f * pulse) * fade),
                                       kIconRadius * zoom + 2.f * zoom, 3.f);

            const float badgeSize = 28.f * zoom;
            const float badgeX = ir.x + ir.width - badgeSize - 4.f * zoom;
            const float badgeY = ir.y + ir.height - badgeSize - 4.f * zoom;
            const nxui::Vec2 badgeCenter = {badgeX + badgeSize * 0.5f,
                                            badgeY + badgeSize * 0.5f};
            ren.drawCircle(badgeCenter, badgeSize * 0.58f, glow.withAlpha(0.22f * fade), 20);
            ren.drawCircle(badgeCenter, badgeSize * 0.5f,
                           nxui::Color(0.05f, 0.06f, 0.09f, 0.92f * fade), 16);
            const float triH = badgeSize * 0.45f;
            const float triW = triH * 0.85f;
            const nxui::Vec2 p1 = {badgeCenter.x - triW * 0.35f, badgeCenter.y - triH * 0.5f};
            const nxui::Vec2 p2 = {badgeCenter.x - triW * 0.35f, badgeCenter.y + triH * 0.5f};
            const nxui::Vec2 p3 = {badgeCenter.x + triW * 0.65f, badgeCenter.y};
            ren.drawTriangle(p1, p2, p3, glow.withAlpha(0.98f * fade));
        } else if (!m_suspendedTitle.empty()) {
            drawCentered(ren, m_suspendedTitle, 300.f, m_font, inkSoft, 1.0f, dx, dy, zoom);
        }

        const nxui::Rect btn = at(resumeButtonRect(0.f));
        ren.drawRoundedRect(btn, th.cursorNormal.withAlpha(fade * 0.22f), 12.f * zoom);
        ren.drawRoundedRectOutline(btn, th.cursorNormal.withAlpha(fade * 0.62f), 12.f * zoom,
                                   1.5f);
        const nxui::Vec2 rsz = m_font->measure(m_resumeText);
        ren.drawText(m_resumeText,
                     {btn.x + (btn.width - rsz.x * zoom) * 0.5f,
                      btn.y + (btn.height - rsz.y * zoom) * 0.5f},
                     m_font, ink, zoom);
        drawKeyHintRow(ren, {{'A', m_resumeText}, {'B', m_unlockText}}, 596.f, small,
                       ink, inkSoft, fade, dx, dy, zoom);
    } else if (!m_padCodeEnabled) {
        drawKeyHintRow(ren, {{'A', m_unlockText}}, 352.f, small, ink, inkSoft, fade, dx, dy, zoom);
    }

    if (clipped) {
        // Lit leading edge so the wipe reads as glass sliding over HOME rather
        // than a hard rectangular mask.
        const float remain = std::clamp(1.f - curtainT, 0.f, 1.f);
        const nxui::Color lit(1.f, 1.f, 1.f, 0.12f * fade);
        const nxui::Color glow = th.cursorNormal.withAlpha(0.30f * fade);
        constexpr float kEdge = 44.f;
        switch (m_wipeDir) {
            case 0: { // Up: boundary travels up, lit inside the revealed strip.
                const float edgeY = kScreenH * (1.f - remain);
                ren.drawGradientRect({0.f, edgeY, kScreenW, kEdge}, lit,
                                     nxui::Color(1.f, 1.f, 1.f, 0.f));
                ren.drawRect({0.f, edgeY, kScreenW, 1.5f}, glow);
                break;
            }
            case 2: { // Down
                const float edgeY = kScreenH * remain;
                ren.drawGradientRect({0.f, edgeY - kEdge, kScreenW, kEdge},
                                     nxui::Color(1.f, 1.f, 1.f, 0.f), lit);
                ren.drawRect({0.f, edgeY - 1.5f, kScreenW, 1.5f}, glow);
                break;
            }
            case 3: { // Left
                const float edgeX = kScreenW * (1.f - remain);
                ren.drawGradientRectH({edgeX, 0.f, kEdge, kScreenH}, lit,
                                      nxui::Color(1.f, 1.f, 1.f, 0.f));
                ren.drawRect({edgeX, 0.f, 1.5f, kScreenH}, glow);
                break;
            }
            default: { // Right
                const float edgeX = kScreenW * remain;
                ren.drawGradientRectH({edgeX - kEdge, 0.f, kEdge, kScreenH},
                                      nxui::Color(1.f, 1.f, 1.f, 0.f), lit);
                ren.drawRect({edgeX - 1.5f, 0.f, 1.5f, kScreenH}, glow);
                break;
            }
        }
    }
    if (clipped)
        ren.popClipRect();
}
