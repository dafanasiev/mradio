cmake_minimum_required(VERSION 3.28)

project(mradio_audio
    VERSION 0.1.0
    DESCRIPTION "libmpv-backed implementation of IAudioEngine"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(MradioPackage)
mradio_package_init()

if(NOT TARGET mradio::core)
    find_package(mradio_core CONFIG REQUIRED)
endif()

# mpv ships no CMake config, only a .pc file.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

add_library(mradio_audio STATIC src/mpv_engine.cpp)
add_library(mradio::audio ALIAS mradio_audio)

target_sources(mradio_audio
    PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES include/mradio/audio/mpv_engine.hpp)

# PkgConfig::MPV stays PRIVATE: <mpv/client.h> is an implementation detail and
# must not reach anything that includes mpv_engine.hpp. CMake still records the
# link requirement for consumers of this static library.
target_link_libraries(mradio_audio
    PUBLIC  mradio::core
    PRIVATE PkgConfig::MPV)

mradio_configure_target(mradio_audio)
mradio_install_package(mradio_audio
    FIND_DEPENDENCIES mradio_core
    CONFIG_PREAMBLE "find_dependency(PkgConfig)\npkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)")
mradio_add_tests(tests)
