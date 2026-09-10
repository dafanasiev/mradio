#include "mradio/core/track_info.hpp"

#include <array>

namespace mradio::core {
namespace {

constexpr std::string_view kBlank = " \t\v\f\r\n";

std::string_view trim(std::string_view s) noexcept
{
    const auto first = s.find_first_not_of(kBlank);
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(kBlank) - first + 1);
}

// Separators recognised between artist and title. Both are spelled with
// surrounding spaces on purpose - see parse_icy_title's contract. The second
// is U+2013 EN DASH, which stations use about as often as the ASCII hyphen.
constexpr std::array<std::string_view, 2> kSeparators{" - ", " \xE2\x80\x93 "};

}  // namespace

bool TrackInfo::empty() const noexcept
{
    return artist.empty() && title.empty() && raw.empty();
}

std::string TrackInfo::display() const
{
    if (!artist.empty() && !title.empty()) {
        return artist + " - " + title;
    }
    if (!title.empty()) {
        return title;
    }
    return raw;
}

TrackInfo parse_icy_title(std::string_view raw)
{
    TrackInfo info;

    const std::string_view line = trim(raw);
    info.raw = std::string(line);
    if (line.empty()) {
        return info;
    }

    for (const std::string_view separator : kSeparators) {
        const auto at = line.find(separator);
        if (at == std::string_view::npos) {
            continue;
        }

        const std::string_view artist = trim(line.substr(0, at));
        const std::string_view title = trim(line.substr(at + separator.size()));

        // A line like "- Title" or "Artist -" is not really a pair; leaving it
        // whole reads better than inventing an empty half.
        if (artist.empty() || title.empty()) {
            continue;
        }

        info.artist = std::string(artist);
        info.title = std::string(title);
        return info;
    }

    info.title = std::string(line);
    return info;
}

}  // namespace mradio::core
