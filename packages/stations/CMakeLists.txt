cmake_minimum_required(VERSION 3.28)

project(mradio_stations
    VERSION 0.1.0
    DESCRIPTION "Loads the user's playlist.m3u into the station list"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(MradioPackage)
mradio_package_init()

# Siblings are resolved through find_package when this package is configured on
# its own, and through the aliases the aggregator already defined otherwise.
if(NOT TARGET mradio::core)
    find_package(mradio_core CONFIG REQUIRED)
endif()
if(NOT TARGET mradio::m3uparser)
    find_package(mradio_m3uparser CONFIG REQUIRED)
endif()

add_library(mradio_stations STATIC src/loader.cpp)
add_library(mradio::stations ALIAS mradio_stations)

target_sources(mradio_stations
    PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES include/mradio/stations/loader.hpp)

# m3uparser is PRIVATE: the M3U types are an implementation detail of loading,
# and nothing above this layer should learn about Entry or Problem.
target_link_libraries(mradio_stations
    PUBLIC  mradio::core
    PRIVATE mradio::m3uparser)

mradio_configure_target(mradio_stations)
mradio_install_package(mradio_stations FIND_DEPENDENCIES mradio_core)
mradio_add_tests(tests)
