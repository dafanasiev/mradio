cmake_minimum_required(VERSION 3.28)

project(mradio_m3uparser
    VERSION 0.1.0
    DESCRIPTION "Lenient extended-M3U playlist parser, no external dependencies"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(MradioPackage)
mradio_package_init()

add_library(mradio_m3uparser STATIC src/parser.cpp)
add_library(mradio::m3uparser ALIAS mradio_m3uparser)

target_sources(mradio_m3uparser
    PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES include/mradio/m3u/parser.hpp)

mradio_configure_target(mradio_m3uparser)
mradio_install_package(mradio_m3uparser)
mradio_add_tests(tests)
