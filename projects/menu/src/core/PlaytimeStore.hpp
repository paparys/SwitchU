#pragma once
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

// Per-title playtime, accumulated by WidgetStore whenever SwitchU regains
// control after a launch. Header-only on purpose: it is a tiny self-contained
// record and does not need a translation unit or an xmake entry.
//
// There is no usable system playtime service here (libnx exposes no pdmq
// binding), so these totals are SwitchU's own measurements: a session is the
// wall-clock time between the launch handoff and the next HOME return.
namespace switchu::playtime {

inline constexpr const char* kPath = "sdmc:/config/SwitchU/playtime.json";
inline constexpr std::uint64_t kMaxSessionSeconds = 7ULL * 24ULL * 60ULL * 60ULL;

namespace detail {

inline std::map<std::uint64_t, std::uint64_t>& storage() {
    static std::map<std::uint64_t, std::uint64_t> table;
    return table;
}

inline void ensureLoaded() {
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    std::ifstream input(kPath);
    if (!input.is_open())
        return;
    try {
        nlohmann::json root;
        input >> root;
        if (!root.is_object())
            return;
        for (auto it = root.begin(); it != root.end(); ++it) {
            const std::uint64_t id = std::strtoull(it.key().c_str(), nullptr, 10);
            const std::uint64_t secs = it.value().is_number_unsigned()
                ? it.value().get<std::uint64_t>() : 0ULL;
            if (id != 0 && secs != 0)
                storage()[id] = secs;
        }
    } catch (...) {
        // A corrupt record must never block the menu; badges simply start empty.
    }
}

inline void flush() {
    try {
        nlohmann::json root = nlohmann::json::object();
        for (const auto& entry : storage())
            root[std::to_string(entry.first)] = entry.second;
        std::ofstream out(kPath, std::ios::trunc);
        if (out.is_open())
            out << root.dump();
    } catch (...) {
    }
}

} // namespace detail

inline std::uint64_t secondsFor(std::uint64_t titleId) {
    if (titleId == 0)
        return 0;
    detail::ensureLoaded();
    const auto it = detail::storage().find(titleId);
    return it == detail::storage().end() ? 0ULL : it->second;
}

inline void addSession(std::uint64_t titleId, std::uint64_t seconds) {
    if (titleId == 0 || seconds == 0)
        return;
    detail::ensureLoaded();
    detail::storage()[titleId] += std::min(seconds, kMaxSessionSeconds);
    detail::flush();
}

} // namespace switchu::playtime
