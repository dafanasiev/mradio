#pragma once

#include "mradio/core/error.hpp"

#include <string_view>

namespace mradio::app {

// What the command line asked for.
//
// The two views are not symmetrical, and neither are their flags. MPRIS costs
// nothing to publish and is what media keys, panel applets and playerctl talk
// to, so it is on until it is turned off. The tray icon is a visible thing on
// someone's panel and is off until it is asked for - a headless box driven
// entirely by playerctl has no use for one.
struct Options {
    bool tray = false;   // --with-tray
    bool mpris = true;   // --without-mpris turns this off
    bool help = false;   // -h, --help

    // --sort-by-name: sort the station list by name as soon as it is read,
    // rather than keeping the playlist's own order. It is the list itself that
    // is sorted, so every view of it agrees; the tray's own "Sort by name"
    // entry is then left out, having nothing left to do.
    bool sort_by_name = false;

    friend bool operator==(const Options&, const Options&) = default;
};

// Parses the command line. argv[0] is skipped; a flag repeated is not an error.
//
// Nothing is checked beyond the spelling of the arguments: that no view at all
// was asked for is a decision for the caller, which is the only place that can
// say what to do about it.
core::Result<Options> parse_options(int argc, const char* const* argv);

// What --help prints, and what a bad argument is followed by. Ends in a
// newline, so it is printed as-is.
[[nodiscard]] std::string_view usage() noexcept;

}  // namespace mradio::app
