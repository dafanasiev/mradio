cmake_minimum_required(VERSION 3.28)

project(mradio_app
    VERSION 0.1.0
    DESCRIPTION "The mradio daemon: composition root wiring the model, view model and views"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(MradioPackage)
mradio_package_init()

foreach(dep IN ITEMS audio core ipc mpris stations tray viewmodel)
    if(NOT TARGET mradio::${dep})
        find_package(mradio_${dep} CONFIG REQUIRED)
    endif()
endforeach()

find_package(Threads REQUIRED)

# Command-line parsing, kept in a library of its own so the tests can link it.
# It is internal to this package and deliberately not installed: nothing above
# the composition root has any business reading argv.
add_library(mradio_app_options STATIC src/options.cpp)

target_include_directories(mradio_app_options PUBLIC src)
target_link_libraries(mradio_app_options PUBLIC mradio::core)

mradio_configure_target(mradio_app_options)

# The only executable in the tree. It owns no behaviour of its own: it loads
# the playlist, builds the engine, the view model and whichever views the
# command line asked for, and then hands control to the D-Bus loop.
add_executable(mradio
    src/main.cpp
    src/signal_waiter.cpp)

target_link_libraries(mradio
    PRIVATE
        mradio_app_options
        mradio::audio
        mradio::ipc
        mradio::mpris
        mradio::stations
        mradio::tray
        mradio::viewmodel
        Threads::Threads)

mradio_configure_target(mradio)

install(TARGETS mradio RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES share/mradio.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

mradio_add_tests(tests)
