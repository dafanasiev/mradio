#pragma once

#include "mradio/vm/player_view_model.hpp"
#include "mradio/core/error.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <functional>
#include <memory>

namespace mradio::mpris {

// Publishes the player on org.mpris.MediaPlayer2 at /org/mpris/MediaPlayer2.
//
// This is what makes the media keys, the Plasma media applet and playerctl
// work without any of them knowing about mradio specifically.
//
// An opaque handle on purpose: the concrete class derives from headers that
// sdbus-c++-xml2cpp generates at build time, and those should not leak into
// anything that merely wants to start the service.
class Service {
public:
    // `quit` is invoked from the MPRIS Quit method, on the D-Bus loop thread.
    //
    // The caller must have claimed the well-known name org.mpris.MediaPlayer2.mradio
    // on this connection; the spec requires the name and the object together.
    static core::Result<std::unique_ptr<Service>> start(sdbus::IConnection& connection,
                                                        vm::PlayerViewModel& view_model,
                                                        std::function<void()> quit);

    virtual ~Service() = default;

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

protected:
    Service() = default;
};

}  // namespace mradio::mpris
