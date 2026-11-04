#pragma once

// The chrome alphas used to live as literals in three different files, so the
// bar, the hint capsules and the tiles drifted apart whenever one changed.
// One place for the numbers the surface treatments share.
namespace switchu::ui::surface {

// The merged HOME band is the thickest glass on screen.
constexpr float kBandAlphaMin    = 0.46f;
constexpr float kBandRimAlphaMin = 0.55f;

// Hint capsules sit over the wallpaper, so they stay translucent enough that
// the art reads through them.
constexpr float kHintAlphaDark  = 0.30f;
constexpr float kHintAlphaLight = 0.24f;

// Hints dim to this share of their alpha once navigation goes quiet.
constexpr float kHintIdleAlpha = 0.45f;

} // namespace switchu::ui::surface
