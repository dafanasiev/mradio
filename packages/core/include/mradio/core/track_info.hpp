#pragma once

#include <string>
#include <string_view>

namespace mradio::core {

// What the stream says it is currently playing.
//
// Everything here is best-effort. It comes from ICY metadata, where the whole
// payload is one free-form line: many stations send "Artist - Title", some
// send only a title, some send station promos, and some send nothing at all.
struct TrackInfo {
    std::string artist;
    std::string title;
    std::string raw;  // the unsplit icy-title, kept as a display fallback

    [[nodiscard]] bool empty() const noexcept;

    // "Artist - Title", or the title alone, or the raw line - whichever is the
    // most informative thing available.
    [[nodiscard]] std::string display() const;

    friend bool operator==(const TrackInfo&, const TrackInfo&) = default;
};

// Splits the conventional "Artist - Title" ICY line.
//
// The rule is deliberately conservative: split on the first " - " that has
// whitespace on both sides, and otherwise treat the entire line as a title.
// Hyphens inside names ("Jean-Michel Jarre", "Lo-Fi Beats") are common enough
// that a bare '-' is not a safe separator.
TrackInfo parse_icy_title(std::string_view raw);

}  // namespace mradio::core
