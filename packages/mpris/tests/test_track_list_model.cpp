#include "mradio/mpris/track_list_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using mradio::core::Station;
using mradio::core::StationId;
using mradio::core::StationList;
using mradio::mpris::TrackListModel;

namespace {

// The second name is deliberately not ASCII: an id keeps those bytes, which is
// the whole reason a track id cannot just be the id.
StationList three_stations()
{
    StationList list;
    list.add(Station{StationId{"groove-salad"}, "Groove Salad", "http://example.org/1"}).value();
    list.add(Station{StationId{"радио-джаз"}, "Радио Джаз", "http://example.org/2"}).value();
    list.add(Station{StationId{"rock"}, "Rock FM", "http://example.org/3"}).value();
    return list;
}

}  // namespace

TEST_CASE("the track list is the playlist, in order", "[mpris]")
{
    const StationList stations = three_stations();
    const TrackListModel model{stations};

    const std::vector<std::string> ids = model.track_ids();

    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == "/org/mpris/MediaPlayer2/mradio/station/0");
    CHECK(ids[1] == "/org/mpris/MediaPlayer2/mradio/station/1");
    CHECK(ids[2] == "/org/mpris/MediaPlayer2/mradio/station/2");
}

TEST_CASE("an empty playlist is an empty track list", "[mpris]")
{
    const StationList stations;
    const TrackListModel model{stations};

    CHECK(model.track_ids().empty());
    CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/0").has_value());
}

TEST_CASE("every id leads back to the station it was made from", "[mpris]")
{
    const StationList stations = three_stations();
    const TrackListModel model{stations};

    for (const Station& station : stations) {
        const auto id = model.id_of(station.id);

        REQUIRE(id.has_value());
        CHECK(model.station_of(*id) == station.id);
    }
}

TEST_CASE("a station outside the playlist has no id", "[mpris]")
{
    const StationList stations = three_stations();
    const TrackListModel model{stations};

    CHECK_FALSE(model.id_of(StationId{"not-in-the-file"}).has_value());
}

TEST_CASE("a track id that names no station is turned down", "[mpris]")
{
    const StationList stations = three_stations();
    const TrackListModel model{stations};

    SECTION("the path MPRIS reserves for no track at all")
    {
        CHECK_FALSE(model.station_of(TrackListModel::no_track()).has_value());
    }

    SECTION("an index past the end of the playlist")
    {
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/3").has_value());
    }

    SECTION("another player's track")
    {
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/Track/1").has_value());
    }

    SECTION("the prefix with nothing after it")
    {
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/").has_value());
    }

    SECTION("something that is not a number")
    {
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/one").has_value());
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/-1").has_value());
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/+1").has_value());
    }

    SECTION("trailing rubbish after a number that would otherwise do")
    {
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/1x").has_value());
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/1/2").has_value());
    }

    SECTION("a second spelling of an index that already has one")
    {
        // 01 is not what track_ids() handed out, so it names nothing: two ids
        // for one station would break a client keeping them in a set.
        CHECK_FALSE(model.station_of("/org/mpris/MediaPlayer2/mradio/station/01").has_value());
    }

    SECTION("the empty string")
    {
        CHECK_FALSE(model.station_of("").has_value());
    }
}

TEST_CASE("the reserved no-track path is the one the spec names", "[mpris]")
{
    CHECK(TrackListModel::no_track() == "/org/mpris/MediaPlayer2/TrackList/NoTrack");
}
