add_executable(mradio_stations_tests test_loader.cpp)

target_link_libraries(mradio_stations_tests
    PRIVATE mradio::stations Catch2::Catch2WithMain)

mradio_configure_target(mradio_stations_tests)

include(Catch)
catch_discover_tests(mradio_stations_tests)
