#include "options.hpp"

#include <string>

namespace mradio::app {
namespace {

constexpr std::string_view kUsage =
    "Usage: mradio [--with-tray] [--without-mpris] [--sort-by-name]\n"
    "\n"
    "Internet radio player with no window of its own. It has two faces, both\n"
    "of them D-Bus services:\n"
    "\n"
    "  --with-tray      publish the tray icon and its menu (StatusNotifierItem).\n"
    "                   Off unless asked for.\n"
    "  --without-mpris  do not publish MPRIS - no media keys, no panel applets,\n"
    "                   no playerctl. It is published otherwise.\n"
    "  --sort-by-name   sort the stations by name as soon as the playlist is\n"
    "                   read, instead of keeping its order. The tray menu, the\n"
    "                   MPRIS track list and next/previous all follow; the\n"
    "                   menu's own \"Sort by name\" entry is left out.\n"
    "  -h, --help       print this and exit\n"
    "\n"
    "One of the two has to remain: --without-mpris on its own would leave no\n"
    "way to pick a station or to stop the program, and is refused.\n"
    "\n"
    "Stations are read once at startup from $XDG_CONFIG_HOME/mradio/playlist.m3u.\n"
    "MRADIO_AUDIO_OUTPUT overrides mpv's choice of audio output; set it to\n"
    "\"null\" on a machine with no sound card.\n";

}  // namespace

std::string_view usage() noexcept
{
    return kUsage;
}

core::Result<Options> parse_options(int argc, const char* const* argv)
{
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];

        if (argument == "--with-tray") {
            options.tray = true;
        } else if (argument == "--without-mpris") {
            options.mpris = false;
        } else if (argument == "--sort-by-name") {
            options.sort_by_name = true;
        } else if (argument == "-h" || argument == "--help") {
            options.help = true;
        } else {
            return core::fail(core::Errc::invalid_argument,
                              "unknown argument: " + std::string{argument});
        }
    }

    return options;
}

}  // namespace mradio::app
