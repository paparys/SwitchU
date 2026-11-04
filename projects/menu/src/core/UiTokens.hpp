#pragma once

// One place for the numbers the whole menu is laid out against.
//
// Before this header every screen carried its own literals: 107 occurrences of
// 1280/720 and ad-hoc text scales at 163 drawText() call sites (0.38, 0.40,
// 0.44, 0.70, 0.78, 1.55, 1.92 ...). Screens built from those numbers never look
// like parts of the same system, and nothing can be retuned globally.
//
// Rules:
//  * Positions and gaps come from the kSpace* ladder (an 8px rhythm).
//  * Text uses one of the six type steps - no raw scale literals at call sites.
//  * The frame is read from kScreenW / kScreenH, not typed as a literal.
namespace ui {

// Frame. The menu composes at 720p and is upscaled by the compositor.
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

// Type steps, expressed as drawText() scale multipliers so they can be passed
// straight through. Roughly a 1.25 ratio: 0.38 / 0.44 / 0.52 / 0.62 / 0.78.
inline constexpr float kTypeCaption    = 0.38f;  // badges, meta, footnote
inline constexpr float kTypeBodySmall  = 0.44f;  // secondary rows, hints
inline constexpr float kTypeBody       = 0.52f;  // list rows, labels
inline constexpr float kTypeTitleSmall = 0.62f;  // panel headers, section labels
inline constexpr float kTypeTitle      = 0.78f;  // screen titles
inline constexpr float kTypeDisplay    = 1.55f;  // clock, hero numerals

// Shared radii that are not theme-driven (chrome that must not move when the
// user's roundness slider changes, e.g. tiny indicators).
inline constexpr float kRadiusIndicator = 1.5f;
inline constexpr float kRadiusPillFull  = 999.f; // clamped to half the height

// Overlay scroll affordances.
inline constexpr float kScrollFadeHeight = 22.f;
inline constexpr float kScrollBarWidth  = 3.f;
inline constexpr float kScrollBarInset  = 7.f;
inline constexpr float kScrollThumbMin  = 28.f;

} // namespace ui
