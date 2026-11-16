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
constexpr float kMargin = 64.f;
constexpr float kCurtainTravel = 740.f;
constexpr float kInputGuard = 0.35f;
constexpr int kTimeFontPt = 88;
constexpr float kDateSettleDelay = 0.35f;
// Dissolve gets a slow push-in so it is not a flat cross-fade.
constexpr float kDissolveEnterScale = 1.035f;
// Unlock durations — AnimatedFloat is driven by AnimationManager only
// (do not also call .update here or motions run ~2×).
// Faster than the first pass: a lock is a gate, not a title sequence. Enter
// settles out, unlock accelerates away.
constexpr float kCurtainEnterSec = 0.52f;
constexpr float kCurtainUnlockSec = 0.42f;
constexpr float kDissolveEnterSec = 0.55f;
constexpr float kDissolveExitSec = 0.45f;
constexpr float kZoomEnterSec = 0.55f;
constexpr float kZoomExitSec = 0.42f;
constexpr float kWipeEnterSec = 0.52f;
constexpr float kWipeExitSec = 0.42f;
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
    m_heroLum = -1.f;
    if (!m_gpu || !m_ren || m_pendingHero.empty() ||
        m_pendingHero.size() != 1280u * 720u * 4u)
        return;

    // Average luma of the upper third: it decides whether the clock is drawn in
    // white or in the theme's dark ink, instead of assuming every hero is dark.
    {
        std::uint64_t sum = 0;
        std::uint32_t count = 0;
        for (unsigned y = 0; y < 240u; y += 8u) {
            const std::uint8_t* row =
                m_pendingHero.data() + static_cast<std::size_t>(y) * 1280u * 4u;
            for (unsigned x = 0; x < 1280u; x += 8u) {
                const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4u;
                sum += static_cast<std::uint64_t>(54u * p[0] + 183u * p[1] + 19u * p[2]);
                ++count;
            }
        }
        if (count)
            m_heroLum = static_cast<float>(sum) /
                        (static_cast<float>(count) * 65280.f);
    }
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
            m_curtain.set(0.f, enterDur, nxui::Easing::outCubic, kEnterAnimDelay);
        } else if (m_transition == Transition::Zoom) {
            m_fade.setImmediate(0.f);
            m_dateReveal.setImmediate(0.f);
            m_dateRevealStarted = true;
            const float z0 = (m_zoomDir == 1) ? kZoomInEnter : kZoomOutEnter;
            m_zoom.setImmediate(z0);
            m_fade.set(1.f, enterDur, nxui::Easing::outCubic, kEnterAnimDelay);
            m_dateReveal.set(1.f, enterDur, nxui::Easing::outCubic, kEnterAnimDelay);
            m_zoom.set(1.f, enterDur, nxui::Easing::outCubic, kEnterAnimDelay);
        } else { // Dissolve
            m_fade.setImmediate(0.f);
            m_dateReveal.setImmediate(0.f);
            m_dateRevealStarted = true;
            m_zoom.setImmediate(kDissolveEnterScale);
            m_fade.set(1.f, enterDur, nxui::Easing::outCubic, kEnterAnimDelay);
            m_dateReveal.set(1.f, enterDur, nxui::Easing::outCubic, kEnterAnimDelay);
            m_zoom.set(1.f, enterDur, nxui::Easing::outCubic, kEnterAnimDelay);
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
    m_heroLum = -1.f;
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
        m_fade.set(0.f, kDissolveExitSec, nxui::Easing::inCubic, kUnlockAnimDelay);
        m_dateReveal.set(0.f, kDissolveExitSec, nxui::Easing::inCubic, kUnlockAnimDelay);
        m_zoom.set(kDissolveEnterScale, kDissolveExitSec, nxui::Easing::inCubic,
                   kUnlockAnimDelay);
        return;
    }

    if (m_transition == Transition::Zoom) {
        const float z1 = (m_zoomDir == 1) ? kZoomInExit : kZoomOutExit;
        m_fade.set(0.f, kZoomExitSec, nxui::Easing::inCubic, kUnlockAnimDelay);
        m_dateReveal.set(0.f, kZoomExitSec, nxui::Easing::inCubic, kUnlockAnimDelay);
        m_zoom.set(z1, kZoomExitSec, nxui::Easing::inCubic, kUnlockAnimDelay);
        m_curtain.set(1.f, kZoomExitSec, nxui::Easing::linear, kUnlockAnimDelay);
        return;
    }

    if (m_transition == Transition::Wipe) {
        m_fade.setImmediate(1.f);
        const float remaining = std::clamp(1.f - m_curtain.value(), 0.05f, 1.f);
        m_curtain.set(1.f, kWipeExitSec * remaining, nxui::Easing::inCubic, kUnlockAnimDelay);
        return;
    }

    m_fade.setImmediate(1.f);
    const float remaining = std::clamp(1.f - m_curtain.value(), 0.05f, 1.f);
    m_curtain.set(1.f, kCurtainUnlockSec * remaining, nxui::Easing::inCubic, kUnlockAnimDelay);
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
    // Static frame. The old 0.55 rad/s breath was a third independent pulse on a
    // screen that already had a moving sweep, a pulsing colon and a ring pulse.
    const float vig = m_hasHero ? 0.30f : 0.22f;
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

}

void LockScreen::drawClock(nxui::Renderer& ren, float dx, float dy, float zoom, float fade,
                           const nxui::Color& ink, const nxui::Color& inkSoft) const {
    const bool overImage = m_hasHero || static_cast<bool>(m_onDrawBackdrop);
    const float colonPulse = 0.74f + 0.26f * (0.5f + 0.5f * std::sin(m_atmosPhase * 1.9f));

    // Glyph layout, shared by the shadow pass and the lit pass.
    struct Glyph {
        std::string text;
        float x;
        float w;
    };
    std::vector<Glyph> glyphs;
    float totalW = 0.f;
    float glyphH = 0.f;
    // 12 h ("9:41") and 24 h ("23:41") must fill the same optical block, or the
    // composition changes mass with the clock setting. Measure, then fit.
    float timeFit = 1.f;
    if (m_timeFont) {
        const auto colon = m_timeStr.find(':');
        if (colon != std::string::npos) {
            const std::string left = m_timeStr.substr(0, colon);
            const std::string right = m_timeStr.substr(colon + 1);
            const nxui::Vec2 ls = m_timeFont->measure(left);
            const nxui::Vec2 cs = m_timeFont->measure(":");
            const nxui::Vec2 rs = m_timeFont->measure(right);
            const float rawW = ls.x + cs.x + rs.x;
            timeFit = std::clamp(560.f / std::max(1.f, rawW), 0.86f, 1.18f);
            glyphH = ls.y * timeFit;
            totalW = rawW * timeFit;
            const float x0 = (kScreenW - totalW) * 0.5f;
            glyphs.push_back({left, x0, ls.x * timeFit});
            glyphs.push_back({":", x0 + ls.x * timeFit, cs.x * timeFit});
            glyphs.push_back({right, x0 + (ls.x + cs.x) * timeFit, rs.x * timeFit});
        } else {
            const nxui::Vec2 ts = m_timeFont->measure(m_timeStr);
            timeFit = std::clamp(560.f / std::max(1.f, ts.x), 0.86f, 1.18f);
            glyphH = ts.y * timeFit;
            totalW = ts.x * timeFit;
            glyphs.push_back({m_timeStr, (kScreenW - totalW) * 0.5f, totalW});
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
    const float glyphScale = (m_timeFont ? timeFit : 2.35f) * zoom;
    // Fixed optical centre rather than a fixed top edge: the width clamp can then
    // change the size without moving the clock off its line.
    constexpr float kTimeCenterY = 170.f;
    auto glyphOrigin = [&](const Glyph& g, float lift) {
        const nxui::Vec2 c =
            mapLockPt(g.x + g.w * 0.5f, kTimeCenterY + lift, dx, dy, zoom);
        return nxui::Vec2{c.x - g.w * zoom * 0.5f, c.y - glyphH * zoom * 0.5f};
    };

    // Date caption under the clock, in the hierarchy instead of above it.
    nxui::Font* dateFont = m_smallFont ? m_smallFont : m_font;
    const float dateT = std::clamp(m_dateReveal.value(), 0.f, 1.f);
    const float dateDrop = (1.f - dateT) * 14.f;
    const float dateY = kTimeCenterY + glyphH * 0.5f + 18.f + dateDrop;
    const std::string date = asciiUpper(m_dateStr);
    const bool latinDate = isAscii(date);
    // Non-ASCII dates skip tracking, so draw them a touch larger to keep the same
    // optical weight as the Latin caption.
    const float dateScale = latinDate ? 1.f : 1.08f;
    const float dateTracking = latinDate ? 2.4f : 0.f;
    // A still needs contrast protection; a flat theme background does not, and a
    // black shadow there would only dirty a light theme.
    if (overImage) {
        drawCenteredTracked(ren, date, dateY + 1.0f, dateFont,
                            nxui::Color(0.f, 0.f, 0.f, 0.28f * dateT), dateScale,
                            dateTracking, dx, dy, zoom);
        drawCenteredTracked(ren, date, dateY + 2.5f, dateFont,
                            nxui::Color(0.f, 0.f, 0.f, 0.10f * dateT), dateScale,
                            dateTracking, dx, dy, zoom);
    }
    drawCenteredTracked(ren, date, dateY, dateFont,
                        inkSoft.withAlpha(inkSoft.a * dateT * 0.94f), dateScale,
                        dateTracking, dx, dy, zoom);

    if (overImage) {
        // Two passes: a tight contact shadow plus a wider, weaker one. One 2.4 px
        // offset reads as a drop shadow, not as lifted type.
        for (const auto& g : glyphs)
            ren.drawText(g.text, glyphOrigin(g, 1.0f), timeFont,
                         nxui::Color(0.f, 0.f, 0.f, 0.30f * fade), glyphScale);
        for (const auto& g : glyphs)
            ren.drawText(g.text, glyphOrigin(g, 3.0f), timeFont,
                         nxui::Color(0.f, 0.f, 0.f, 0.12f * fade), glyphScale);
    }

    // A slow highlight rather than a travelling spotlight: one 6 s pass every
    // 22 s at 15% white. The old continuous 50% sweep was the loudest ambient
    // motion on the screen and read as a demo.
    const float cycle = std::fmod(m_atmosPhase, 22.f) / 22.f;
    const float travel = cycle < 0.30f ? std::clamp(cycle / 0.30f, 0.f, 1.f) : -1.f;
    const float band = std::max(70.f, totalW * 0.30f);
    for (const auto& g : glyphs) {
        nxui::Color c = (g.text == ":") ? ink.withAlpha(ink.a * colonPulse) : ink;
        if (travel >= 0.f) {
            const float gc = g.x + g.w * 0.5f;
            const float sweep = travel * (totalW + band * 2.f) - band;
            float a = std::clamp(1.f - std::fabs(gc - sweep) / band, 0.f, 1.f);
            a = a * a * (3.f - 2.f * a);
            if (a > 0.001f)
                c = nxui::Color::lerp(c, nxui::Color::white(), 0.15f * a).withAlpha(c.a);
        }
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

    // Paint whenever locked: a plane that is transparent at rest reads as an
    // overlay on HOME, not as a lock screen.
    const bool paintPlane = m_hasHero || curtainMoving || wipeMoving
                         || dissolveZoomPlane || m_active;
    if (paintPlane && fade > 0.01f) {
        if (m_hasHero) {
            const nxui::Rect full = atZoom({0.f, 0.f, kScreenW, kScreenH}, bgZoom);
            ren.drawTexture(&m_heroTex, full, nxui::Color::white().withAlpha(fade));
            // One grade, not four stacked ones: accent + gradient + 40% black +
            // a 55% bottom wash reached ~70% and buried the poster.
            ren.drawRect(full, th.backgroundAccent.withAlpha(0.06f * fade));
            ren.drawGradientRect(full, nxui::Color(0.f, 0.f, 0.f, 0.06f * fade),
                                 nxui::Color(0.f, 0.f, 0.f, 0.45f * fade));
        } else if (m_onDrawBackdrop) {
            m_onDrawBackdrop(ren, dy, fade, bgZoom);
            // The lock gets its own plate; the wallpaper still slides under the
            // curtain, six percent visible.
            ren.drawRect(atZoom({0.f, 0.f, kScreenW, kScreenH}, bgZoom),
                         th.background.withAlpha(0.90f * fade));
        } else {
            ren.drawRect(atZoom({0.f, 0.f, kScreenW, kScreenH}, bgZoom),
                         th.background.withAlpha(0.94f * fade));
        }
    }

    // Soft key light from the top edge, so the plate is not a flat fill.
    ren.drawGradientRect(atZoom({0.f, 0.f, kScreenW, 260.f}, bgZoom),
                         nxui::Color::white().withAlpha(0.045f * fade),
                         nxui::Color::white().withAlpha(0.f));

    // Vignette is part of the BG wash — keep it fullscreen on Zoom In.
    drawAtmosphere(ren, dx, dy, bgZoom, fade);

    // Ink follows the artwork, not its presence: a daylight hero needs dark text.
    const bool lightInk = m_hasHero && (m_heroLum < 0.f || m_heroLum < 0.45f);
    const nxui::Color ink = (lightInk ? nxui::Color(1.f, 1.f, 1.f, 1.f) : th.textPrimary)
                                .withAlpha(fade);
    const nxui::Color inkSoft = (lightInk ? nxui::Color(1.f, 1.f, 1.f, 1.f) : th.textSecondary)
                                    .withAlpha((lightInk ? 0.82f : 0.90f) * fade);
    nxui::Font* small = m_smallFont ? m_smallFont : m_font;

    if (m_battery >= 0) {
        const std::string pct = std::to_string(m_battery) + "%";
        const nxui::Vec2 psz = small->measure(pct);
        const float right = kScreenW - kMargin;
        const float rowY = 48.f;
        const nxui::Vec2 pctC = mapLockPt(right - psz.x * 0.5f, rowY + psz.y * 0.5f,
                                          dx, dy, zoom);
        ren.drawText(pct, {pctC.x - psz.x * zoom * 0.5f, pctC.y - psz.y * zoom * 0.5f},
                     small, inkSoft.withAlpha(fade * 0.86f), zoom);
        // Compact chrome: a hairline shell and a contained bolt read as an icon at
        // this size. The old 40 px shell with a bolt spilling past it was noisy,
        // and the accent-coloured fill made it a sixth accent moment.
        const float shellW = 28.f, shellH = 14.f;
        const nxui::Rect body = at({right - psz.x - 10.f - shellW, rowY - 2.f,
                                    shellW, shellH});
        ren.drawRoundedRectOutline(body, inkSoft.withAlpha(fade * 0.80f),
                                   2.5f * zoom, 1.25f * zoom);
        ren.drawRect({body.right() + 1.5f * zoom, body.y + 4.f * zoom,
                      2.f * zoom, 6.f * zoom},
                     inkSoft.withAlpha(fade * 0.72f));
        const float fill = std::clamp(m_battery / 100.f, 0.f, 1.f);
        ren.drawRoundedRect({body.x + 1.5f * zoom, body.y + 1.5f * zoom,
                             (body.width - 3.f * zoom) * fill,
                             body.height - 3.f * zoom},
                            ink.withAlpha(fade * 0.85f), 1.5f * zoom);
        if (m_charging) {
            const float boltH = body.height * 0.96f;
            const float boltW = boltH * 0.56f;
            const nxui::Rect bolt{
                body.x + (body.width - boltW) * 0.5f,
                body.y + (body.height - boltH) * 0.5f,
                boltW, boltH};
            // Knock the bolt out of the fill instead of painting ink onto ink:
            // the plane colour shows through it and the ink edge keeps the shape
            // readable over both the filled and empty halves of the icon.
            switchu::battery_drawing::drawLightningBolt(
                ren, bolt, th.background.withAlpha(fade * 0.96f), ink,
                std::max(1.f, 1.25f * zoom));
        }
    }

    // Local contrast plate behind the clock: full width and soft-ended, so it
    // has no visible edge and the still keeps its colour.
    if ((m_hasHero || static_cast<bool>(m_onDrawBackdrop)) && fade > 0.01f) {
        ren.drawGradientRect(atZoom({0.f, 0.f, kScreenW, 320.f}, zoom),
                             nxui::Color(0.f, 0.f, 0.f, 0.30f * fade),
                             nxui::Color(0.f, 0.f, 0.f, 0.f));
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
            const bool current = filled && i == entered - 1;
            ren.drawRoundedRect(cell, tick.withAlpha(fade * (filled ? 0.24f : 0.12f)),
                                12.f * zoom);
            ren.drawRoundedRectOutline(cell,
                                       tick.withAlpha(fade * (filled ? 0.95f : 0.28f)),
                                       12.f * zoom, (filled ? 2.f : 1.5f) * zoom);
            if (current) {
                // Only the step just entered shows its direction; earlier cells
                // stay filled dots, so the row reads as progress, not a key map.
                drawDirGlyph(ren, m_padInput[static_cast<std::size_t>(i)],
                             {cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f},
                             15.f * zoom, tick.withAlpha(fade));
            } else if (filled) {
                ren.drawCircle({cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f},
                               3.f * zoom, tick.withAlpha(fade * 0.70f), 12);
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
            : nxui::I18n::instance().tr("lock.code_hint", "Enter code");
        nxui::Font* hintFont = m_smallFont ? m_smallFont : m_font;
        drawCentered(ren, hint, y0 + cellH + 34.f, hintFont,
                     error ? bad.withAlpha(fade) : inkSoft, 0.92f, dx, dy, zoom);
        if (!error && !m_padInput.empty()) {
            drawCentered(ren, nxui::I18n::instance().tr("lock.code_clear", "B clears"),
                         y0 + cellH + 56.f, hintFont,
                         inkSoft.withAlpha(inkSoft.a * 0.55f), 0.86f, dx, dy, zoom);
        }
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
        // Hairline, not a filled pill: the accent belongs to the ring, and a 22%
        // wash was the fifth accent moment on the screen.
        ren.drawRoundedRect(btn, ink.withAlpha(fade * 0.05f), 12.f * zoom);
        ren.drawRoundedRectOutline(btn, th.cursorNormal.withAlpha(fade * 0.55f),
                                   12.f * zoom, 1.5f * zoom);
        const nxui::Vec2 rsz = m_font->measure(m_resumeText);
        ren.drawText(m_resumeText,
                     {btn.x + (btn.width - rsz.x * zoom) * 0.5f,
                      btn.y + (btn.height - rsz.y * zoom) * 0.5f},
                     m_font, ink, zoom);
        drawKeyHintRow(ren, {{'A', m_resumeText}, {'B', m_unlockText}}, 596.f, small,
                       ink, inkSoft, fade, dx, dy, zoom);
    } else if (!m_padCodeEnabled) {
        // Same slot as the Resume state, so the primary action never jumps 244 px
        // depending on whether a game happens to be suspended.
        drawKeyHintRow(ren, {{'A', m_unlockText}}, 596.f, small, ink, inkSoft, fade, dx, dy, zoom);
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
