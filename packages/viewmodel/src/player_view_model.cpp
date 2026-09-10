#include "mradio/vm/player_view_model.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace mradio::vm {

PlayerViewModel::PlayerViewModel(core::StationList stations, core::IAudioEngine& engine)
    : stations_(std::move(stations)), engine_(engine)
{
    playback_ = engine_.status();
    engine_.set_listener(this);
}

PlayerViewModel::~PlayerViewModel()
{
    // Before anything else is torn down: the engine's event thread may be
    // inside on_status_changed right now.
    engine_.set_listener(nullptr);
}

ViewState PlayerViewModel::build_view_state() const
{
    ViewState view;
    view.station = station_;
    view.is_playing = core::is_active(playback_.state);
    view.track = playback_.track;
    view.volume = playback_.volume;
    view.muted = playback_.muted;
    view.error = playback_.error.message;

    if (station_) {
        if (const core::Station* const station = stations_.find(*station_)) {
            view.station_name = station->name;
            view.station_url = station->url;
        }
    }

    return view;
}

ViewState PlayerViewModel::view_state() const
{
    const std::lock_guard lock{mutex_};
    return build_view_state();
}

void PlayerViewModel::publish(const ViewState& state)
{
    // The lock is held across the callbacks so a listener cannot be removed
    // out from under a call in flight. Hence the rule that listeners must not
    // add or remove listeners from inside the callback.
    const std::lock_guard lock{listener_mutex_};
    for (IViewStateListener* const listener : listeners_) {
        listener->on_view_state_changed(state);
    }
}

void PlayerViewModel::add_listener(IViewStateListener* listener)
{
    if (listener == nullptr) {
        return;
    }
    const std::lock_guard lock{listener_mutex_};
    listeners_.push_back(listener);
}

void PlayerViewModel::remove_listener(IViewStateListener* listener)
{
    const std::lock_guard lock{listener_mutex_};
    std::erase(listeners_, listener);
}

core::Status PlayerViewModel::play(const core::StationId& id)
{
    const core::Station* const station = stations_.find(id);
    if (station == nullptr) {
        return core::fail(core::Errc::not_found, "no station with id '" + id.str() + "'");
    }

    // Copied out before the engine call: mutex_ must not be held across it,
    // because play() publishes and would re-enter on_status_changed.
    const std::string url = station->url;

    {
        const std::lock_guard lock{mutex_};
        station_ = id;
    }

    if (core::Status started = engine_.play(url); !started) {
        {
            const std::lock_guard lock{mutex_};
            station_.reset();
        }
        publish(view_state());
        return started;
    }

    // Published explicitly: the chosen station changed even if the engine's
    // own status has not caught up, and the tray glyph must move now.
    publish(view_state());
    return {};
}

void PlayerViewModel::stop()
{
    {
        const std::lock_guard lock{mutex_};
        station_.reset();
    }
    engine_.stop();
    publish(view_state());
}

void PlayerViewModel::toggle_play_stop()
{
    bool active = false;
    {
        const std::lock_guard lock{mutex_};
        active = core::is_active(playback_.state);
    }

    if (active) {
        stop();
        return;
    }

    // Nothing is on. A media key sending PlayPause to a stopped player is
    // expected to start something, and with no memory of what played before,
    // the top of the playlist is the only sensible choice.
    if (!stations_.empty()) {
        static_cast<void>(play(stations_[0].id));
    }
}

void PlayerViewModel::next()
{
    if (stations_.empty()) {
        return;
    }

    std::size_t target = 0;  // nothing on yet: start at the top
    {
        const std::lock_guard lock{mutex_};
        if (station_) {
            if (const auto index = stations_.index_of(*station_)) {
                target = (*index + 1) % stations_.size();
            }
        }
    }
    static_cast<void>(play(stations_[target].id));
}

void PlayerViewModel::previous()
{
    if (stations_.empty()) {
        return;
    }

    std::size_t target = stations_.size() - 1;  // nothing on: wrap to the end
    {
        const std::lock_guard lock{mutex_};
        if (station_) {
            if (const auto index = stations_.index_of(*station_)) {
                target = (*index + stations_.size() - 1) % stations_.size();
            }
        }
    }
    static_cast<void>(play(stations_[target].id));
}

void PlayerViewModel::set_volume(core::Volume volume)
{
    engine_.set_volume(volume);
}

void PlayerViewModel::adjust_volume(double delta)
{
    core::Volume current;
    {
        const std::lock_guard lock{mutex_};
        current = playback_.volume;
    }
    engine_.set_volume(current.adjusted(delta));
}

void PlayerViewModel::set_muted(bool muted)
{
    engine_.set_muted(muted);
}

void PlayerViewModel::on_status_changed(const core::PlaybackStatus& status)
{
    ViewState published;
    {
        const std::lock_guard lock{mutex_};
        playback_ = status;
        published = build_view_state();
    }
    publish(published);
}

}  // namespace mradio::vm
