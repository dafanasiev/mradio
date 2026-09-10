add_executable(mradio_ipc_tests test_bus.cpp)

target_link_libraries(mradio_ipc_tests
    PRIVATE mradio::ipc Catch2::Catch2WithMain)

mradio_configure_target(mradio_ipc_tests)

include(Catch)
# Each test starts a private dbus-daemon, so these are slower than unit tests
# and must not run against the developer's real session bus.
catch_discover_tests(mradio_ipc_tests PROPERTIES TIMEOUT 60)
