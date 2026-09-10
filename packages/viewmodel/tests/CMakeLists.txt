add_executable(mradio_viewmodel_tests test_player_view_model.cpp)

target_link_libraries(mradio_viewmodel_tests
    PRIVATE mradio::viewmodel Catch2::Catch2WithMain)

mradio_configure_target(mradio_viewmodel_tests)

include(Catch)
catch_discover_tests(mradio_viewmodel_tests)
