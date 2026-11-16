#pragma once

#include <algorithm>

// One ladder for every chrome surface in the menu.
//
// The screens used to carry their own numbers (radii 7/9/10/11/12/14/16/18/
// 20/22/24/26/28/30/32, text scales 0.58/0.68/0.74/0.80/0.82/0.84/0.86/0.94/
// 0.95/1.10). That is why two panels on the same screen could curve differently
// and two labels of the same rank could be a step apart.
//
// Rules:
//  * Pick a role, not a number: control / row / card / panel / pill.
//  * A shape nested inside another uses nested(parent, inset) so the curves
//    stay concentric instead of fighting.
//  * Text uses the eight type steps. Hero numerals are the only display case.
namespace switchu::ui::chrome {

// ---- Radius ladder -------------------------------------------------------
inline constexpr float kRadiusControl = 10.f;  // toggles, small buttons, chips
inline constexpr float kRadiusIndicator = 4.f; // tracks, bars, scroll thumbs
inline constexpr float kRadiusRow     = 14.f;  // list rows, tiles, buttons
inline constexpr float kRadiusCard    = 20.f;  // grouped cards, rails
inline constexpr float kRadiusPanel   = 26.f;  // dialogs, full panels
inline constexpr float kRadiusPill    = 999.f; // clamped to half the height
inline constexpr float kRadiusKeycap = 6.f;   // controller hints only

// Concentric nesting: a child inset by N gets parent - N, never the parent's
// radius again (which is what makes a card inside a panel look swollen).
inline float nested(float parent, float inset) {
    return std::max(4.f, parent - inset);
}

// ---- Type ladder (drawText scale multipliers) ----------------------------
inline constexpr float kTypeCaption = 0.38f;  // badges, meta, footnote
inline constexpr float kTypeHint    = 0.44f;  // secondary rows, hints
inline constexpr float kTypeBody    = 0.52f;  // list rows, labels
inline constexpr float kTypeSection = 0.62f;  // section labels, rail captions
inline constexpr float kTypeRow     = 0.78f;  // settings labels, descriptions
inline constexpr float kTypeTitle   = 0.90f;  // screen titles, focused values
inline constexpr float kTypeHeader  = 1.10f;  // overlay headers
inline constexpr float kTypeDisplay = 1.55f;  // clock, hero numerals

// ---- Focus ring ----------------------------------------------------------
// Every focus ring in the menu is this: the card rect expanded by 3px, a 2px
// ring, plus one soft outer glow. The ring radius always follows the shape.
inline constexpr float kFocusExpansion = 3.f;
inline constexpr float kFocusRingWidth = 2.f;
inline constexpr float kFocusRingAlpha = 0.78f;
inline constexpr float kFocusGlowSpread = 8.f;
inline constexpr float kFocusGlowAlpha  = 0.16f;

// Selection is one cue, not a stack of them. The rail and the flat list use
// the same wash and accent bar so a selected row reads the same in both places.
inline constexpr float kSelectionWash = 0.10f;
inline constexpr float kSelectionBar  = 0.92f;
inline constexpr float kRailDivider   = 0.18f;

// Toggle and slider parts share these mixes so a control never falls back to
// a hardcoded grey that ignores the active theme.
inline constexpr float kControlOffMix     = 0.45f;
inline constexpr float kControlTrackMix   = 0.28f;
inline constexpr float kControlTrackAlpha = 0.50f;
inline constexpr float kControlFillAlpha  = 0.90f;

// ---- Glass elevation tiers ----------------------------------------------
// Wallpaper chrome lets the art through, working glass is readable but still
// translucent, modals are nearly opaque. Each tier owns its rim alpha so the
// edge never looks brighter or duller than the body.
inline constexpr float kWallpaperAlpha = 0.28f;
inline constexpr float kWallpaperRim   = 0.34f;
inline constexpr float kWorkingAlpha   = 0.86f;
inline constexpr float kWorkingRim     = 0.58f;
inline constexpr float kModalAlpha     = 0.94f;
inline constexpr float kModalRim       = 0.42f;

// HOME band and wallpaper hint capsules predate the glass ladder and keep
// their own floors so a tinted theme cannot wash them out.
inline constexpr float kBandAlphaMin    = 0.46f;
inline constexpr float kBandRimAlphaMin = 0.55f;
inline constexpr float kHintAlphaDark   = 0.30f;
inline constexpr float kHintAlphaLight  = 0.24f;
inline constexpr float kHintIdleAlpha   = 0.45f;

// Scrims share one strength per use: a page menu sits over the current screen,
// while a modal/flyout dims the content it is floating above.
inline constexpr float kScrimPage      = 0.18f;
inline constexpr float kScrimModal     = 0.24f;

// ---- Hairlines -----------------------------------------------------------
inline constexpr float kHairlineDark  = 0.06f;
inline constexpr float kHairlineLight = 0.10f;

// ---- Motion --------------------------------------------------------------
// Route every duration through MotionPreset::scaled() so Crisp / Smooth /
// Springy apply to the whole system, not just the carousel.
inline constexpr float kDurFast  = 0.12f;
inline constexpr float kDurBase  = 0.18f;
inline constexpr float kDurPanel = 0.24f;

// ---- Elevation shadow ----------------------------------------------------
inline constexpr float kShadowOffset = 4.f;
inline constexpr float kShadowSpread = 18.f;
inline constexpr float kShadowAlpha  = 0.18f;

// ---- Icon weights --------------------------------------------------------
// Two weights only: detailed/filled for content, this stroke for chrome.
inline constexpr float kIconStroke = 1.75f;

} // namespace switchu::ui::chrome
