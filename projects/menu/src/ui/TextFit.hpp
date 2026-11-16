#pragma once

#include <nxui/core/Font.hpp>

#include <string>

namespace switchu::ui::text {

// One ellipsis rule for every menu surface. Remove whole UTF-8 codepoints so
// accented titles and translated labels never end in a broken byte sequence.
inline void popUtf8Codepoint(std::string& text) {
    if (text.empty())
        return;
    std::size_t index = text.size() - 1;
    while (index > 0 &&
           (static_cast<unsigned char>(text[index]) & 0xC0u) == 0x80u) {
        --index;
    }
    text.erase(index);
}

inline std::string ellipsize(nxui::Font* font, const std::string& text,
                             float maxWidth, float scale) {
    if (!font || text.empty() || maxWidth <= 0.f)
        return text;
    if (font->measure(text).x * scale <= maxWidth)
        return text;

    constexpr const char* kEllipsis = "\xE2\x80\xA6";
    std::string out = text;
    while (!out.empty() &&
           font->measure(out + kEllipsis).x * scale > maxWidth) {
        popUtf8Codepoint(out);
    }
    return out.empty() ? std::string() : out + kEllipsis;
}

} // namespace switchu::ui::text
