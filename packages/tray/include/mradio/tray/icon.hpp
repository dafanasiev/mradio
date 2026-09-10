#pragma once

#include "mradio/core/error.hpp"
#include "mradio/vm/player_view_model.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <functional>
#include <memory>

namespace mradio::tray {

// The tray icon and its menu.
//
// No GUI toolkit is involved. StatusNotifierItem and com.canonical.dbusmenu are
// pure D-Bus protocols: the panel draws the icon and renders the menu itself
// from the properties published here. That is what lets a program with no
// window of its own still have a tray presence.
//
// An opaque handle on purpose - the concrete class derives from headers that
// sdbus-c++-xml2cpp generates at build time.
class Icon {
public:
    // `quit` is invoked from the menu's Quit entry, on the D-Bus loop thread.
    //
    // Registration with the StatusNotifierWatcher is attempted immediately and
    // retried whenever a watcher appears on the bus, so starting before the
    // panel does is fine.
    static core::Result<std::unique_ptr<Icon>> start(sdbus::IConnection& connection,
                                                     vm::PlayerViewModel& view_model,
                                                     std::function<void()> quit);

    virtual ~Icon() = default;

    Icon(const Icon&) = delete;
    Icon& operator=(const Icon&) = delete;

protected:
    Icon() = default;
};

}  // namespace mradio::tray
