cmake_minimum_required(VERSION 3.28)

project(mradio_core
    VERSION 0.1.0
    DESCRIPTION "Domain types shared by every mradio package"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(MradioPackage)
mradio_package_init()

# Deliberately dependency-free, including on the other packages. Everything
# else in the tree depends on core, so anything that leaks in here becomes a
# dependency of the whole program.
add_library(mradio_core STATIC
    src/error.cpp
    src/player_state.cpp
    src/station.cpp
    src/track_info.cpp)

add_library(mradio::core ALIAS mradio_core)

target_sources(mradio_core
    PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/mradio/core/audio_engine.hpp
        include/mradio/core/error.hpp
        include/mradio/core/player_state.hpp
        include/mradio/core/station.hpp
        include/mradio/core/track_info.hpp
        include/mradio/core/volume.hpp)

mradio_configure_target(mradio_core)
mradio_install_package(mradio_core)
mradio_add_tests(tests)
