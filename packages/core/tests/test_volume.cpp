#include "mradio/core/volume.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using mradio::core::Volume;

TEST_CASE("volume defaults to full", "[volume]")
{
    CHECK(Volume{}.normalised() == Catch::Approx(1.0));
}

TEST_CASE("volume clamps to the unit range", "[volume]")
{
    CHECK(Volume{1.5}.normalised() == Catch::Approx(1.0));
    CHECK(Volume{-0.5}.normalised() == Catch::Approx(0.0));
}

TEST_CASE("NaN folds to silence rather than propagating", "[volume]")
{
    // mpv would take a NaN volume and never recover from it.
    const Volume v{std::numeric_limits<double>::quiet_NaN()};
    CHECK(v.normalised() == Catch::Approx(0.0));
    CHECK(v.is_silent());
}

TEST_CASE("percent and normalised are two views of one value", "[volume]")
{
    CHECK(Volume::from_percent(75.0).normalised() == Catch::Approx(0.75));
    CHECK(Volume{0.75}.percent() == Catch::Approx(75.0));
    CHECK(Volume::from_percent(140.0).percent() == Catch::Approx(100.0));
}
