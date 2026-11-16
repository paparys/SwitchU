#pragma once

// Live visual style. The Settings sliders write these values through
// WiiUMenuApp::applyStyleSettings so widgets that animate outside a render pass
// (focus pop, title capsule, page slide) can read the current choice without a
// second configuration lookup in every class.
namespace switchu::ui::style {

inline int focusRingPx = 3;
inline int focusGlowPct = 100;
inline int focusLiftPct = 100;
inline int focusOvershootPct = 0;

inline int titleScalePct = 100;
inline int titleCornerStyle = 0;   // 0 = pill, 1 = rounded, 2 = rect
inline int titleBackgroundPct = 100;

inline int hudGapPx = 0;
inline bool showClock = true;
inline bool showDate = true;
inline bool showBattery = true;
inline bool showProfile = true;

inline int wallpaperBlurPx = 0;
inline int wallpaperSaturationPct = 100;
inline int wallpaperSharpnessPct = 0;
inline int wallpaperVibrancePct = 0;
inline int wallpaperNoisePct = 0;
inline int backdropTintPct = 0;

inline int pageSlideMs = 340;

} // namespace switchu::ui::style
