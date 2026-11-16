#pragma once
#include <nxui/core/Animation.hpp>
#include <cmath>
#include "../ui/ChromeTokens.hpp"

// Crisp / Smooth / Springy. One place that turns the user's motion style into
// a duration multiplier and an easing curve, so the carousel, the page slides
// and the focus ring all speed up or settle together instead of drifting apart.
namespace motion {

inline constexpr int kStyleCount = 3;

// Shared durations. Every screen routes through scaled() so the user's motion
// style applies to the whole system, not just the carousel.
inline constexpr float kDurFast  = switchu::ui::chrome::kDurFast;
inline constexpr float kDurBase  = switchu::ui::chrome::kDurBase;
inline constexpr float kDurPanel = switchu::ui::chrome::kDurPanel;

inline int clampStyle(int style) {
    return style < 0 ? 0 : (style > kStyleCount - 1 ? kStyleCount - 1 : style);
}

// Crisp finishes early, Springy takes a little longer; Smooth is the shipped
// timing. Springy used to run at 1.5x, which left the focus ring visibly
// detached from the tile it was travelling to.
inline float durationScale(int style) {
    switch (clampStyle(style)) {
        case 0:  return 0.60f;
        case 2:  return 1.15f;
        default: return 1.00f;
    }
}

// Damped step response for Springy: one small overshoot and no anticipation.
// outBack pulled backwards first, so a single cursor step hesitated before it
// moved; this leaves from rest, overshoots ~4.6% and is settled by t = 1.
inline float spring(float t) {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    constexpr float kZeta = 0.70f;
    constexpr float kOmega = 9.0f;
    const float wd = kOmega * std::sqrt(1.f - kZeta * kZeta);
    const float decay = std::exp(-kZeta * kOmega * t);
    return 1.f - decay * (std::cos(wd * t)
                          + (kZeta * kOmega / wd) * std::sin(wd * t));
}

inline float (*ease(int style))(float) {
    switch (clampStyle(style)) {
        case 0:  return nxui::Easing::outExpo;      // hard attack, dead stop
        case 2:  return spring;                     // gentle overshoot, settles
        default: return nxui::Easing::inOutCubic;   // leaves and lands at rest
    }
}

// Symmetric curve for moves that both start and end on screen (page slides,
// layout morphs) where an overshoot would smear one state over the other.
inline float (*inOutEase(int style))(float) {
    return clampStyle(style) == 0 ? nxui::Easing::inOutExpo : nxui::Easing::inOutCubic;
}

inline float scaled(float duration, int style) {
    return duration * durationScale(style);
}

} // namespace motion
