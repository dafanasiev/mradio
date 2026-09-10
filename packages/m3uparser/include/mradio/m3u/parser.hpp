#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mradio::m3u {

// One playable line of a playlist: the URI plus whatever #EXTINF said about it.
struct Entry {
    std::string title;               // from #EXTINF; empty when the URI stood alone
    std::string url;                 // never empty in a parsed entry
    double duration_seconds = -1.0;  // -1 for streams and for a missing duration
};

// A line the parser could not make sense of.
//
// Parsing never fails as a whole. This playlist is hand-edited by the user, so
// one malformed line must not cost them every other station in the file;
// problems are collected and the rest of the file is still used.
struct Problem {
    std::size_t line = 0;  // 1-based, for pointing the user at the bad line
    std::string message;
};

struct Playlist {
    std::vector<Entry> entries;
    std::vector<Problem> problems;
};

// Parses extended M3U text.
//
// Deliberately lenient, because a human writes this file by hand:
//   - the #EXTM3U header is optional;
//   - CRLF line endings and a leading UTF-8 BOM are tolerated;
//   - a URI line with no preceding #EXTINF still yields an entry, just an
//     untitled one;
//   - unknown directives (#EXT-X-*, plain comments) are ignored, not rejected;
//   - a title containing commas survives intact.
Playlist parse(std::string_view text);

}  // namespace mradio::m3u
