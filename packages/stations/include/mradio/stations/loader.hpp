#pragma once

#include "mradio/core/error.hpp"
#include "mradio/core/station.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mradio::stations {

// The one file this program reads: $XDG_CONFIG_HOME/mradio/playlist.m3u,
// falling back to $HOME/.config/mradio/playlist.m3u.
//
// Per the XDG spec a relative XDG_CONFIG_HOME is treated as unset. Fails only
// when neither variable gives an absolute directory to work from.
core::Result<std::filesystem::path> default_playlist_path();

struct Loaded {
    core::StationList stations;

    // Lines that were skipped or fixed up. Non-fatal by design: one bad line
    // must not cost the user the rest of their stations, so these are meant
    // for the log, not for aborting startup.
    std::vector<std::string> warnings;
};

// Turns playlist text into stations.
//
// Each entry gets an id derived from its title, made unique within the list by
// a numeric suffix. An entry with no #EXTINF title is named after its URL's
// host, so it still reads sensibly in the tray menu.
core::Result<Loaded> load_from_text(std::string_view text);

// Reads `path` and runs it through load_from_text. Returns Errc::not_found
// when the file does not exist - the caller decides whether that is fatal.
core::Result<Loaded> load(const std::filesystem::path& path);

}  // namespace mradio::stations
