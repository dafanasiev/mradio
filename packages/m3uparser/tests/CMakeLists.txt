add_executable(mradio_m3uparser_tests test_parser.cpp)

target_link_libraries(mradio_m3uparser_tests
    PRIVATE mradio::m3uparser Catch2::Catch2WithMain)

mradio_configure_target(mradio_m3uparser_tests)

include(Catch)
catch_discover_tests(mradio_m3uparser_tests)
