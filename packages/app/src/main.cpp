#include "options.hpp"
#include "signal_waiter.hpp"

#include "mradio/audio/mpv_engine.hpp"
#include "mradio/core/station.hpp"
#include "mradio/ipc/bus.hpp"
#include "mradio/mpris/service.hpp"
#include "mradio/stations/loader.hpp"
#include "mradio/tray/icon.hpp"
#include "mradio/vm/player_view_model.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

// MPRIS requires this exact shape of name, and claiming it doubles as the
// single-instance check: a second copy of the program cannot take it. It is
// claimed only when MPRIS is actually published - see main().
constexpr const char* kServiceName = "org.mpris.MediaPlayer2.mradio";

void report(std::string_view message)
{
    std::cerr << "mradio: " << message << '\n';
}

// Loads the playlist, complaining about problems without refusing to start.
//
// A missing or partly broken playlist is deliberately not fatal. This program
// is a tray icon, and one that refuses to appear leaves the user with nothing
// to click and no idea why; it comes up with an empty menu and says what is
// wrong on stderr instead.
mradio::core::StationList load_playlist()
{
    const auto path = mradio::stations::default_playlist_path();
    if (!path) {
        report(path.error().message);
        return {};
    }

    auto loaded = mradio::stations::load(*path);
    if (!loaded) {
        report(loaded.error().message);
        report("create it and put one #EXTINF line and one stream URL per station in it");
        return {};
    }

    for (const std::string& warning : loaded->warnings) {
        report(path->string() + ": " + warning);
    }

    if (loaded->stations.empty()) {
        report(path->string() + " has no stations in it");
    }

    return std::move(loaded->stations);
}

}  // namespace

int main(int argc, char** argv)
{
    // First, before anything creates a thread: the mask has to be inherited by
    // the mpv and D-Bus threads that come later, or one of them takes the
    // termination signal and the process dies without unwinding. Parsing the
    // command line starts no threads, so it can wait its turn behind this.
    mradio::app::block_termination_signals();

    const auto options = mradio::app::parse_options(argc, argv);
    if (!options) {
        report(options.error().message);
        std::cerr << mradio::app::usage();
        return 2;
    }

    if (options->help) {
        std::cout << mradio::app::usage();
        return 0;
    }

    // Only reachable as --without-mpris with no --with-tray: the program would
    // then hold a playlist it cannot show, a player nothing can start and no
    // way of being asked to quit. Saying so beats sitting there silently.
    if (!options->tray && !options->mpris) {
        report("nothing to control the player with: --without-mpris needs --with-tray");
        std::cerr << mradio::app::usage();
        return 2;
    }

    auto bus = mradio::ipc::Bus::connect_session();
    if (!bus) {
        report("cannot reach the session bus: " + bus.error().message);
        return 1;
    }

    // Only MPRIS needs a well-known name; StatusNotifierItem identifies itself
    // to the watcher by its unique connection name. So --without-mpris gives
    // up the single-instance check along with the interface - claiming an
    // MPRIS name without publishing the object behind it would leave playerctl
    // and the panel applets talking to a player that answers nothing.
    if (options->mpris) {
        if (const auto claimed = (*bus)->request_name(kServiceName); !claimed) {
            report("another instance is already running (" + claimed.error().message + ")");
            return 1;
        }
    }

    mradio::audio::MpvOptions audio_options;

    // The other knob this program has. There is no config file, so an
    // environment variable is the only place to override mpv's choice of
    // audio output - needed on a headless machine, and when mpv picks a device
    // that is not the one the user wanted.
    if (const char* const output = std::getenv("MRADIO_AUDIO_OUTPUT");
        output != nullptr && *output != '\0') {
        audio_options.audio_output = output;
    }

    auto engine = mradio::audio::MpvEngine::create(audio_options);
    if (!engine) {
        report("cannot start the audio backend: " + engine.error().message);
        return 1;
    }

    mradio::core::StationList stations = load_playlist();

    // Right here, on the freshly read list and before anything has looked at
    // it: the view model, the tray menu, the MPRIS track list and next() and
    // previous() then all see one order, and none of them has to know that it
    // is not the file's.
    if (options->sort_by_name) {
        stations.sort_by_name();
    }

    mradio::vm::PlayerViewModel view_model{std::move(stations), **engine};

    const auto quit = [&bus] { (*bus)->stop(); };

    // Held as pointers because either one may not have been asked for. They
    // are views on one view model; whichever exist see the same state.
    std::unique_ptr<mradio::mpris::Service> mpris;
    std::unique_ptr<mradio::tray::Icon> icon;

    if (options->mpris) {
        auto started = mradio::mpris::Service::start((*bus)->connection(), view_model, quit);
        if (!started) {
            report("cannot publish the MPRIS interface: " + started.error().message);
            return 1;
        }
        mpris = std::move(*started);
    }

    if (options->tray) {
        // The playlist is already in the order it was asked for, so the menu's
        // own sort entry would be a switch with nothing to switch.
        const mradio::tray::IconOptions tray_options{
            .sort_by_name_visible = !options->sort_by_name,
        };

        auto started =
            mradio::tray::Icon::start((*bus)->connection(), view_model, quit, tray_options);
        if (!started) {
            report("cannot publish the tray icon: " + started.error().message);
            return 1;
        }
        icon = std::move(*started);
    }

    // The signals have been blocked since the top of main; this only starts
    // the thread that waits for them.
    const mradio::app::SignalWaiter signals{quit};

    if (const auto ran = (*bus)->run(); !ran) {
        report("the D-Bus loop stopped: " + ran.error().message);
        return 1;
    }

    return 0;
}
