#pragma once
#include <nxui/core/Animation.hpp>

// Crisp / Smooth / Springy. One place that turns the user's motion style into
// a duration multiplier and an easing curve, so the carousel, the page slides
// and the focus ring all speed up or settle together instead of drifting apart.
namespace motion {

inline constexpr int kStyleCount = 3;

inline int clampStyle(int style) {
    return style < 0 ? 0 : (style > kStyleCount - 1 ? kStyleCount - 1 : style);
}

// Crisp finishes early, Springy takes its time; Smooth is the shipped timing.
inline float durationScale(int style) {
    switch (clampStyle(style)) {
        case 0:  return 0.60f;
        case 2:  return 1.50f;
        default: return 1.00f;
    }
}

inline float (*ease(int style))(float) {
    switch (clampStyle(style)) {
        case 0:  return nxui::Easing::outExpo;   // hard attack, dead stop
        case 2:  return nxui::Easing::outBack;   // small overshoot, then settle
        default: return nxui::Easing::outCubic;
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
