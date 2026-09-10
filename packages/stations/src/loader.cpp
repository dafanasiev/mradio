#include "mradio/stations/loader.hpp"

#include "mradio/m3u/parser.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace mradio::stations {
namespace {

using core::Errc;
using core::Station;
using core::StationId;
using core::StationList;

// Reads an environment variable, treating an empty value as unset - which is
// what the XDG spec asks for and what a shell that exports an empty string
// produces.
const char* env(const char* name) noexcept
{
    const char* const value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? value : nullptr;
}

// "https://user@ice1.somafm.com:8000/groovesalad" -> "ice1.somafm.com"
//
// Only good enough to put a recognisable label in the tray menu for an entry
// whose #EXTINF title is missing; it is not a URL parser and never touches the
// URL that is actually handed to the audio engine.
std::string host_of(std::string_view url)
{
    std::string_view rest = url;

    if (const auto scheme = rest.find("://"); scheme != std::string_view::npos) {
        rest.remove_prefix(scheme + 3);
    }
    rest = rest.substr(0, rest.find('/'));

    if (const auto at = rest.rfind('@'); at != std::string_view::npos) {
        rest.remove_prefix(at + 1);
    }
    rest = rest.substr(0, rest.find(':'));

    return rest.empty() ? std::string(url) : std::string(rest);
}

// Derives an id from `seed`, then makes it unique against what is already in
// the list. `ordinal` only feeds the fallback for a seed with no usable
// characters in it at all.
StationId unique_id(const StationList& list, std::string_view seed, std::size_t ordinal)
{
    const std::optional<StationId> derived = StationId::from_name(seed);
    const std::string stem =
        derived ? derived->str() : ("station-" + std::to_string(ordinal));

    StationId candidate{stem};
    for (unsigned suffix = 2; list.contains(candidate); ++suffix) {
        candidate = StationId{stem + "-" + std::to_string(suffix)};
    }
    return candidate;
}

}  // namespace

core::Result<std::filesystem::path> default_playlist_path()
{
    std::filesystem::path dir;

    if (const char* const xdg = env("XDG_CONFIG_HOME")) {
        const std::filesystem::path candidate{xdg};
        if (candidate.is_absolute()) {
            dir = candidate;
        }
    }

    if (dir.empty()) {
        const char* const home = env("HOME");
        if (home == nullptr) {
            return core::fail(Errc::not_found,
                              "neither XDG_CONFIG_HOME nor HOME is set, "
                              "so there is nowhere to look for a playlist");
        }
        dir = std::filesystem::path{home} / ".config";
    }

    return dir / "mradio" / "playlist.m3u";
}

core::Result<Loaded> load_from_text(std::string_view text)
{
    const m3u::Playlist playlist = m3u::parse(text);

    Loaded loaded;
    loaded.warnings.reserve(playlist.problems.size());

    for (const m3u::Problem& problem : playlist.problems) {
        loaded.warnings.push_back("line " + std::to_string(problem.line) + ": "
                                  + problem.message);
    }

    std::size_t ordinal = 0;
    for (const m3u::Entry& entry : playlist.entries) {
        ++ordinal;

        const std::string name = entry.title.empty() ? host_of(entry.url) : entry.title;

        Station station{
            .id = unique_id(loaded.stations, name, ordinal),
            .name = name,
            .url = entry.url,
        };

        if (core::Status added = loaded.stations.add(std::move(station)); !added) {
            // unique_id already ruled out a collision, so reaching here means
            // the entry itself was unusable - an empty URL, say.
            loaded.warnings.push_back("station " + std::to_string(ordinal)
                                      + " skipped: " + added.error().message);
        }
    }

    return loaded;
}

core::Result<Loaded> load(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return core::fail(Errc::not_found, "no playlist at " + path.string());
    }

    // Binary: the parser handles CRLF itself, and text mode would give us
    // nothing while making the behaviour platform-dependent.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return core::fail(Errc::io_error, "cannot open " + path.string());
    }

    const std::string text{std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>()};
    if (file.bad()) {
        return core::fail(Errc::io_error, "error while reading " + path.string());
    }

    return load_from_text(text);
}

}  // namespace mradio::stations
