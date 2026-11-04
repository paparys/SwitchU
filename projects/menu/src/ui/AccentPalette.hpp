#pragma once

#include <nxui/Theme.hpp>

#include <array>

// One palette for every colour picker in settings (liquid glass, accent) and
// for the accent sweep itself. Names, order and RGB values live here only, so
// the dropdown label in a tab and the colour the renderer receives can never
// drift apart.
namespace switchu::ui::accent {

struct Entry {
    const char* key;       // i18n key for the option label
    const char* fallback;  // English name used when the key is missing
    nxui::Color color;
};

inline const std::array<Entry, 9>& palette() {
    static const std::array<Entry, 9> kPalette = {{
        {"settings.accent.blue",   "Blue",   {0.32f, 0.55f, 0.95f, 1.f}},
        {"settings.accent.teal",   "Teal",   {0.20f, 0.72f, 0.72f, 1.f}},
        {"settings.accent.green",  "Green",  {0.35f, 0.78f, 0.45f, 1.f}},
        {"settings.accent.lime",   "Lime",   {0.65f, 0.85f, 0.30f, 1.f}},
        {"settings.accent.amber",  "Amber",  {0.95f, 0.72f, 0.28f, 1.f}},
        {"settings.accent.orange", "Orange", {0.95f, 0.52f, 0.24f, 1.f}},
        {"settings.accent.red",    "Red",    {0.90f, 0.32f, 0.34f, 1.f}},
        {"settings.accent.pink",   "Pink",   {0.93f, 0.45f, 0.72f, 1.f}},
        {"settings.accent.purple", "Purple", {0.62f, 0.45f, 0.92f, 1.f}},
    }};
    return kPalette;
}

inline int count() {
    return static_cast<int>(palette().size());
}

// 0 means "leave the theme alone"; 1..9 index the palette above.
inline bool valid(int idx) {
    return idx >= 1 && idx <= count();
}

inline nxui::Color colorFor(int idx) {
    return valid(idx) ? palette()[static_cast<std::size_t>(idx - 1)].color
                      : nxui::Color::white();
}

} // namespace switchu::ui::accent
