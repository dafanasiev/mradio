add_executable(mradio_core_tests
    test_station.cpp
    test_track_info.cpp
    test_volume.cpp)

target_link_libraries(mradio_core_tests
    PRIVATE mradio::core Catch2::Catch2WithMain)

mradio_configure_target(mradio_core_tests)

include(Catch)
catch_discover_tests(mradio_core_tests)
