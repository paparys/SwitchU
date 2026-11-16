#pragma once

#include <cstdint>
#include <string>

enum class AppLayoutMode : std::uint8_t {
    Grid,
    DynamicLine,
};

// User-facing HOME arrangement. Grid presets all use AppLayoutMode::Grid;
// keeping the preset separate avoids adding a third navigation/art mode to
// every Grid/DynamicLine branch in the menu.
enum class HomeLayoutPreset : std::uint8_t {
    Carousel,
    Shelf,
    Showcase,
    PosterWall,
};

struct HomeLayoutGeometry {
    int columns;
    int rows;
};

inline HomeLayoutGeometry homeLayoutGeometry(HomeLayoutPreset preset) {
    switch (preset) {
        case HomeLayoutPreset::Shelf:      return {5, 2};
        case HomeLayoutPreset::Showcase:   return {3, 2};
        case HomeLayoutPreset::PosterWall: return {6, 2};
        case HomeLayoutPreset::Carousel:
        default:                           return {5, 3};
    }
}

inline const char* homeLayoutKey(HomeLayoutPreset preset) {
    switch (preset) {
        case HomeLayoutPreset::Shelf:      return "shelf";
        case HomeLayoutPreset::Showcase:   return "showcase";
        case HomeLayoutPreset::PosterWall: return "poster_wall";
        case HomeLayoutPreset::Carousel:
        default:                           return "carousel";
    }
}

inline HomeLayoutPreset homeLayoutFromKey(const std::string& key) {
    if (key == "shelf") return HomeLayoutPreset::Shelf;
    if (key == "showcase") return HomeLayoutPreset::Showcase;
    if (key == "poster_wall" || key == "posterwall") return HomeLayoutPreset::PosterWall;
    return HomeLayoutPreset::Carousel;
}

inline bool homeLayoutIsGrid(HomeLayoutPreset preset) {
    return preset != HomeLayoutPreset::Carousel;
}
