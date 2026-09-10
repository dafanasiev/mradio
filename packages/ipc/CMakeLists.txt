cmake_minimum_required(VERSION 3.28)

project(mradio_ipc
    VERSION 0.1.0
    DESCRIPTION "Session-bus connection and I/O loop shared by the D-Bus layers"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(MradioPackage)
mradio_package_init()

if(NOT TARGET mradio::core)
    find_package(mradio_core CONFIG REQUIRED)
endif()

find_package(sdbus-c++ 2 REQUIRED)

add_library(mradio_ipc STATIC src/bus.cpp)
add_library(mradio::ipc ALIAS mradio_ipc)

target_sources(mradio_ipc
    PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES include/mradio/ipc/bus.hpp)

# sdbus-c++ is PUBLIC: bus.hpp hands out sdbus::IConnection&, and the mpris and
# tray packages build their generated adaptors directly on those types.
target_link_libraries(mradio_ipc
    PUBLIC mradio::core SDBusCpp::sdbus-c++)

mradio_configure_target(mradio_ipc)
mradio_install_package(mradio_ipc
    FIND_DEPENDENCIES mradio_core "sdbus-c++ 2")
mradio_add_tests(tests)
