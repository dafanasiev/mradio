add_executable(mradio_app_tests test_options.cpp)

target_link_libraries(mradio_app_tests
    PRIVATE mradio_app_options Catch2::Catch2WithMain)

mradio_configure_target(mradio_app_tests)

include(Catch)
catch_discover_tests(mradio_app_tests)
