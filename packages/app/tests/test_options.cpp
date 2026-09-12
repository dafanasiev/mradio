#include "options.hpp"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using mradio::app::Options;
using mradio::app::parse_options;

namespace {

// argv as the runtime hands it over: the program name first, everything the
// user typed after it.
mradio::core::Result<Options> parse(std::initializer_list<const char*> arguments)
{
    std::vector<const char*> argv{"mradio"};
    argv.insert(argv.end(), arguments);
    return parse_options(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST_CASE("a bare command line is MPRIS and no tray", "[options]")
{
    const auto options = parse({});

    REQUIRE(options);
    CHECK(options->mpris);
    CHECK_FALSE(options->tray);
    CHECK_FALSE(options->help);
}

TEST_CASE("each flag moves its own view and nothing else", "[options]")
{
    SECTION("--with-tray adds the tray, leaving MPRIS alone")
    {
        const auto options = parse({"--with-tray"});

        REQUIRE(options);
        CHECK(options->tray);
        CHECK(options->mpris);
    }

    SECTION("--without-mpris drops MPRIS, leaving the tray alone")
    {
        const auto options = parse({"--without-mpris"});

        REQUIRE(options);
        CHECK_FALSE(options->mpris);
        CHECK_FALSE(options->tray);
    }

    SECTION("both, in either order: tray only")
    {
        const auto first = parse({"--with-tray", "--without-mpris"});
        const auto second = parse({"--without-mpris", "--with-tray"});

        REQUIRE(first);
        REQUIRE(second);
        CHECK(first->tray);
        CHECK_FALSE(first->mpris);
        CHECK(*first == *second);
    }
}

TEST_CASE("a flag given twice is the same as once", "[options]")
{
    const auto tray = parse({"--with-tray", "--with-tray"});
    const auto mpris = parse({"--without-mpris", "--without-mpris"});

    REQUIRE(tray);
    REQUIRE(mpris);
    CHECK(tray->tray);
    CHECK_FALSE(mpris->mpris);
}

TEST_CASE("help is recognised in both spellings", "[options]")
{
    for (const char* const spelling : {"-h", "--help"}) {
        const auto options = parse({spelling});

        REQUIRE(options);
        CHECK(options->help);
    }
}

TEST_CASE("an unknown argument fails and names itself", "[options]")
{
    // A stranger next to a flag that does exist: the good one must not carry
    // the bad one through.
    const auto options = parse({"--with-tray", "--with-video"});

    REQUIRE_FALSE(options);
    CHECK(options.error().code == mradio::core::Errc::invalid_argument);
    CHECK(options.error().message.find("--with-video") != std::string::npos);
}

TEST_CASE("the usage text mentions both flags", "[options]")
{
    const std::string_view text = mradio::app::usage();

    CHECK(text.find("--with-tray") != std::string_view::npos);
    CHECK(text.find("--without-mpris") != std::string_view::npos);
    CHECK(text.ends_with('\n'));
}
