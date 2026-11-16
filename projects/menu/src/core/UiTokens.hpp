#pragma once

// Frame and spacing geometry for the menu.
//
// Visual roles live in ui/ChromeTokens.hpp: type, radius, focus, glass,
// motion and shadows have exactly one ladder there. This header deliberately
// owns only layout numbers, so a screen title cannot silently pick up a second
// type scale from the layout layer.
//
// Rules:
//  * Positions and gaps come from the kSpace* ladder (an 8px rhythm).
//  * Text uses the role steps in chrome::, never a scale defined here.
//  * The frame is read from kScreenW / kScreenH, not typed as a literal.
namespace ui {

// Frame. The menu lays out and presents in a fixed 1280x720 space in both
// handheld and docked modes.
inline constexpr float kScreenW = 1280.f;
inline constexpr float kScreenH = 720.f;

// 8px spacing ladder. kSpace1 is the half-step for optical nudges only.
inline constexpr float kSpace1 = 4.f;
inline constexpr float kSpace2 = 8.f;
inline constexpr float kSpace3 = 12.f;
inline constexpr float kSpace4 = 16.f;
inline constexpr float kSpace5 = 24.f;
inline constexpr float kSpace6 = 32.f;
inline constexpr float kSpace7 = 48.f;

} // namespace ui
