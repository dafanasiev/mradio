add_executable(mradio_mpris_tests test_track_list_model.cpp)

target_link_libraries(mradio_mpris_tests
    PRIVATE mradio::mpris Catch2::Catch2WithMain)

mradio_configure_target(mradio_mpris_tests)

include(Catch)
catch_discover_tests(mradio_mpris_tests)
