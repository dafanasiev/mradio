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
#include <string>
#include <string_view>

namespace {

// MPRIS requires this exact shape of name, and claiming it doubles as the
// single-instance check: a second copy of the program cannot take it.
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

int main()
{
    // First, before anything creates a thread: the mask has to be inherited by
    // the mpv and D-Bus threads that come later, or one of them takes the
    // termination signal and the process dies without unwinding.
    mradio::app::block_termination_signals();

    auto bus = mradio::ipc::Bus::connect_session();
    if (!bus) {
        report("cannot reach the session bus: " + bus.error().message);
        return 1;
    }

    if (const auto claimed = (*bus)->request_name(kServiceName); !claimed) {
        report("another instance is already running (" + claimed.error().message + ")");
        return 1;
    }

    mradio::audio::MpvOptions audio_options;

    // The one knob this program has. There is no config file, so an
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

    mradio::vm::PlayerViewModel view_model{load_playlist(), **engine};

    const auto quit = [&bus] { (*bus)->stop(); };

    auto mpris = mradio::mpris::Service::start((*bus)->connection(), view_model, quit);
    if (!mpris) {
        report("cannot publish the MPRIS interface: " + mpris.error().message);
        return 1;
    }

    auto icon = mradio::tray::Icon::start((*bus)->connection(), view_model, quit);
    if (!icon) {
        report("cannot publish the tray icon: " + icon.error().message);
        return 1;
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
