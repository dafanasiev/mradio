add_executable(mradio_tray_tests test_menu_model.cpp)

target_link_libraries(mradio_tray_tests
    PRIVATE mradio::tray Catch2::Catch2WithMain)

mradio_configure_target(mradio_tray_tests)

include(Catch)
catch_discover_tests(mradio_tray_tests)
