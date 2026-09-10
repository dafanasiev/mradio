#include "mradio/vm/player_view_model.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using mradio::core::Errc;
using mradio::core::PlaybackStatus;
using mradio::core::PlayerState;
using mradio::core::Station;
using mradio::core::StationId;
using mradio::core::StationList;
using mradio::core::Volume;
using mradio::vm::PlayerViewModel;
using mradio::vm::ViewState;

namespace {

// An IAudioEngine that records what it was asked to do and publishes the
// status changes a real backend would. Lets the behaviour be pinned down
// without mpv, a sound card or real time.
class FakeEngine final : public mradio::core::IAudioEngine {
public:
    std::vector<std::string> played;
    int stop_calls = 0;
    bool refuse_next_play = false;

    mradio::core::Status play(std::string_view url) override
    {
        played.emplace_back(url);
        if (refuse_next_play) {
            refuse_next_play = false;
            return mradio::core::fail(Errc::backend_error, "refused");
        }
        status_.url = std::string{url};
        status_.state = PlayerState::playing;
        notify();
        return {};
    }

    void stop() override
    {
        ++stop_calls;
        status_.url.clear();
        status_.state = PlayerState::idle;
        notify();
    }

    void set_volume(Volume volume) override
    {
        status_.volume = volume;
        notify();
    }

    void set_muted(bool muted) override
    {
        status_.muted = muted;
        notify();
    }

    [[nodiscard]] PlaybackStatus status() const override { return status_; }

    void set_listener(mradio::core::IAudioEngineListener* listener) override
    {
        listener_ = listener;
    }

private:
    void notify()
    {
        if (listener_ != nullptr) {
            listener_->on_status_changed(status_);
        }
    }

    PlaybackStatus status_;
    mradio::core::IAudioEngineListener* listener_ = nullptr;
};

class RecordingListener final : public mradio::vm::IViewStateListener {
public:
    std::vector<ViewState> states;

    void on_view_state_changed(const ViewState& state) override { states.push_back(state); }
};

StationList three_stations()
{
    StationList list;
    list.add(Station{StationId{"one"}, "One", "http://example.org/1"}).value();
    list.add(Station{StationId{"two"}, "Two", "http://example.org/2"}).value();
    list.add(Station{StationId{"three"}, "Three", "http://example.org/3"}).value();
    return list;
}

}  // namespace

TEST_CASE("playing a station hands its URL to the engine", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};

    REQUIRE(model.play(StationId{"two"}).has_value());

    REQUIRE(engine.played.size() == 1);
    CHECK(engine.played[0] == "http://example.org/2");

    const ViewState state = model.view_state();
    REQUIRE(state.station.has_value());
    CHECK(state.station->str() == "two");
    CHECK(state.is_playing);
}

TEST_CASE("the view state resolves the station so views need not look it up", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};

    REQUIRE(model.play(StationId{"three"}).has_value());

    const ViewState state = model.view_state();
    CHECK(state.station_name == "Three");
    CHECK(state.station_url == "http://example.org/3");
}

TEST_CASE("a stopped player exposes no station name", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};
    REQUIRE(model.play(StationId{"one"}).has_value());

    model.stop();

    const ViewState state = model.view_state();
    CHECK_FALSE(state.station.has_value());
    CHECK(state.station_name.empty());
    CHECK(state.station_url.empty());
    CHECK_FALSE(state.is_playing);
}

TEST_CASE("an unknown station id is refused and changes nothing", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};
    REQUIRE(model.play(StationId{"one"}).has_value());

    const auto played = model.play(StationId{"nope"});

    REQUIRE_FALSE(played.has_value());
    CHECK(played.error().code == Errc::not_found);
    CHECK(engine.played.size() == 1);
    CHECK(model.view_state().station->str() == "one");
}

TEST_CASE("an engine that refuses to play leaves nothing selected", "[viewmodel]")
{
    FakeEngine engine;
    engine.refuse_next_play = true;
    PlayerViewModel model{three_stations(), engine};

    const auto played = model.play(StationId{"one"});

    REQUIRE_FALSE(played.has_value());
    CHECK_FALSE(model.view_state().station.has_value());
}

TEST_CASE("stopping clears the selected station", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};
    REQUIRE(model.play(StationId{"two"}).has_value());

    model.stop();

    CHECK(engine.stop_calls == 1);
    CHECK_FALSE(model.view_state().station.has_value());
}

TEST_CASE("next and previous step through the playlist and wrap", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};

    REQUIRE(model.play(StationId{"two"}).has_value());

    model.next();
    CHECK(model.view_state().station->str() == "three");

    model.next();  // wraps past the end
    CHECK(model.view_state().station->str() == "one");

    model.previous();  // wraps back past the start
    CHECK(model.view_state().station->str() == "three");
}

TEST_CASE("stepping with nothing selected starts at an end of the list", "[viewmodel]")
{
    FakeEngine engine;

    {
        PlayerViewModel model{three_stations(), engine};
        model.next();
        CHECK(model.view_state().station->str() == "one");
    }
    {
        PlayerViewModel model{three_stations(), engine};
        model.previous();
        CHECK(model.view_state().station->str() == "three");
    }
}

TEST_CASE("stepping an empty playlist does nothing", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{StationList{}, engine};

    model.next();
    model.previous();

    CHECK(engine.played.empty());
    CHECK_FALSE(model.view_state().station.has_value());
}

TEST_CASE("the play/stop toggle stops whatever is on", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};
    REQUIRE(model.play(StationId{"one"}).has_value());

    model.toggle_play_stop();

    CHECK(engine.stop_calls == 1);
    CHECK_FALSE(model.view_state().station.has_value());
    CHECK_FALSE(model.view_state().is_playing);
}

TEST_CASE("the play/stop toggle starts the first station when stopped", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};

    // Nothing is remembered across a stop, so this holds whether or not
    // something played earlier in the session.
    REQUIRE(model.play(StationId{"two"}).has_value());
    model.stop();

    model.toggle_play_stop();

    CHECK(model.view_state().station->str() == "one");
    CHECK(model.view_state().is_playing);
}

TEST_CASE("the play/stop toggle does nothing with an empty playlist", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{StationList{}, engine};

    model.toggle_play_stop();

    CHECK(engine.played.empty());
    CHECK_FALSE(model.view_state().station.has_value());
}

TEST_CASE("scrolling adjusts the volume from its current value and saturates", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};

    model.set_volume(Volume{0.50});
    model.adjust_volume(0.05);
    CHECK(model.view_state().volume.normalised() == Catch::Approx(0.55));

    model.adjust_volume(-0.60);
    CHECK(model.view_state().volume.normalised() == Catch::Approx(0.0));

    model.adjust_volume(2.0);
    CHECK(model.view_state().volume.normalised() == Catch::Approx(1.0));
}

TEST_CASE("mute goes through to the engine and back into the view state", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};

    model.set_muted(true);
    CHECK(model.view_state().muted);

    model.set_muted(false);
    CHECK_FALSE(model.view_state().muted);
}

TEST_CASE("a bound view sees the station change as soon as it is selected", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};

    RecordingListener listener;
    model.add_listener(&listener);

    REQUIRE(model.play(StationId{"three"}).has_value());

    REQUIRE_FALSE(listener.states.empty());
    const ViewState& last = listener.states.back();
    REQUIRE(last.station.has_value());
    CHECK(last.station->str() == "three");
    CHECK(last.station_name == "Three");
    CHECK(last.is_playing);

    model.remove_listener(&listener);
}

TEST_CASE("two bound views both receive every change", "[viewmodel]")
{
    FakeEngine engine;
    PlayerViewModel model{three_stations(), engine};

    // The tray and MPRIS both bind; neither may starve the other.
    RecordingListener tray;
    RecordingListener mpris;
    model.add_listener(&tray);
    model.add_listener(&mpris);

    REQUIRE(model.play(StationId{"one"}).has_value());

    CHECK_FALSE(tray.states.empty());
    CHECK(tray.states.size() == mpris.states.size());

    model.remove_listener(&tray);
    model.remove_listener(&mpris);
}
