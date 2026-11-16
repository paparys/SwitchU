#pragma once

#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Types.hpp>

#include "ChromeTokens.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace switchu::ui::chrome {

struct Hint {
    std::string key;
    std::string label;
};

inline constexpr float kKeycapHeight = 20.f;
inline constexpr float kKeycapPadX = 5.f;
inline constexpr float kKeycapLabelGap = 6.f;
inline constexpr float kHintGap = 16.f;

inline float hintLabelScale() {
    return kTypeSection;
}

inline float keycapWidth(nxui::Font* font, const std::string& key) {
    if (!font)
        return 0.f;
    return std::max(kKeycapHeight, font->measure(key).x * hintLabelScale() +
                                      kKeycapPadX * 2.f);
}

inline float drawKeycap(nxui::Renderer& ren, nxui::Font* font,
                        float x, float y, const std::string& key,
                        const std::string& label, const nxui::Color& color,
                        float alpha, bool drawLabel = true) {
    if (!font)
        return 0.f;

    const float scale = hintLabelScale();
    const float width = keycapWidth(font, key);
    if (!key.empty()) {
        ren.drawRoundedRect({x, y, width, kKeycapHeight},
                            color.withAlpha(0.10f * alpha), kRadiusKeycap);
        ren.drawRoundedRectOutline({x, y, width, kKeycapHeight},
                                   color.withAlpha(0.28f * alpha),
                                   kRadiusKeycap, 1.f);
        const nxui::Vec2 keySize = font->measure(key);
        ren.drawText(key,
                     {x + (width - keySize.x * scale) * 0.5f,
                      y + (kKeycapHeight - keySize.y * scale) * 0.5f},
                     font, color.withAlpha(0.92f * alpha), scale);
    }

    float result = key.empty() ? 0.f : width;
    if (drawLabel && !label.empty()) {
        const float labelX = key.empty() ? x : x + width + kKeycapLabelGap;
        const nxui::Vec2 labelSize = font->measure(label);
        ren.drawText(label,
                     {labelX,
                      y + (kKeycapHeight - labelSize.y * scale) * 0.5f},
                     font, color.withAlpha(0.78f * alpha), scale);
        result = labelX + labelSize.x * scale - x;
    }
    return result;
}

// D-pad and left/right hints already carry their own key name. Keep the parsed
// grouping tolerant of translated spacing so a localization change still
// renders the footer instead of dropping to plain text.
inline const std::vector<Hint>& parseHints(const std::string& text) {
    static thread_local std::string cachedText;
    static thread_local std::vector<Hint> cachedHints;
    if (text == cachedText)
        return cachedHints;

    cachedText = text;
    cachedHints.clear();

    auto trim = [](std::string value) {
        while (!value.empty() &&
               std::isspace(static_cast<unsigned char>(value.front())))
            value.erase(value.begin());
        while (!value.empty() &&
               std::isspace(static_cast<unsigned char>(value.back())))
            value.pop_back();
        return value;
    };

    std::size_t groupStart = 0;
    while (groupStart < text.size()) {
        std::size_t groupEnd = text.find("   ", groupStart);
        if (groupEnd == std::string::npos)
            groupEnd = text.size();
        const std::string group = trim(text.substr(groupStart,
                                                   groupEnd - groupStart));
        if (!group.empty()) {
            std::size_t split = std::string::npos;
            for (std::size_t i = 1; i + 1 < group.size(); ++i) {
                if (group[i] == ' ' && group[i + 1] == ' ') {
                    split = i;
                    break;
                }
            }
            if (split == std::string::npos) {
                cachedHints.push_back({"", group});
            } else {
                cachedHints.push_back({trim(group.substr(0, split)),
                                       trim(group.substr(split))});
            }
        }
        groupStart = groupEnd;
        while (groupStart < text.size() && text[groupStart] == ' ')
            ++groupStart;
    }
    return cachedHints;
}

inline float drawHints(nxui::Renderer& ren, nxui::Font* font,
                       const std::vector<Hint>& hints, float x, float y,
                       const nxui::Color& color, float alpha,
                       float maxWidth = 1e9f) {
    if (!font)
        return 0.f;
    const float startX = x;
    bool first = true;
    for (const Hint& hint : hints) {
        if (!first) {
            const float nextWidth = hint.key.empty()
                ? font->measure(hint.label).x * hintLabelScale()
                : keycapWidth(font, hint.key) + kKeycapLabelGap +
                      font->measure(hint.label).x * hintLabelScale();
            if (x - startX + kHintGap + nextWidth > maxWidth)
                break;
            x += kHintGap;
        }
        const float width = drawKeycap(ren, font, x, y, hint.key, hint.label,
                                       color, alpha);
        if (width <= 0.f)
            break;
        x += width;
        first = false;
    }
    return x - startX;
}

inline float drawHintString(nxui::Renderer& ren, nxui::Font* font,
                            const std::string& text, float x, float y,
                            const nxui::Color& color, float alpha,
                            float maxWidth = 1e9f) {
    const auto& hints = parseHints(text);
    return drawHints(ren, font, hints, x, y, color, alpha, maxWidth);
}

} // namespace switchu::ui::chrome
