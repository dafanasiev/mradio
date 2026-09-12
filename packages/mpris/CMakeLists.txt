cmake_minimum_required(VERSION 3.28)

project(mradio_mpris
    VERSION 0.1.0
    DESCRIPTION "org.mpris.MediaPlayer2 view onto the player"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(MradioPackage)
mradio_package_init()

foreach(dep IN ITEMS core viewmodel ipc)
    if(NOT TARGET mradio::${dep})
        find_package(mradio_${dep} CONFIG REQUIRED)
    endif()
endforeach()

find_package(sdbus-c++ 2 REQUIRED)
mradio_require_xml2cpp()

set(MRADIO_MPRIS_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")

mradio_generate_dbus("${CMAKE_CURRENT_SOURCE_DIR}/xml/mpris.xml"
    OUT_DIR "${MRADIO_MPRIS_GENERATED}"
    ADAPTOR mpris_adaptor.h
    OUT_VAR MRADIO_MPRIS_SOURCES)

add_library(mradio_mpris STATIC
    src/track_list_model.cpp
    src/service.cpp
    ${MRADIO_MPRIS_SOURCES})

add_library(mradio::mpris ALIAS mradio_mpris)

target_sources(mradio_mpris
    PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/mradio/mpris/service.hpp
        include/mradio/mpris/track_list_model.hpp)

# The generated header is PRIVATE: service.hpp deliberately hides it behind an
# opaque handle, so nothing outside this package should be able to include it.
target_include_directories(mradio_mpris SYSTEM PRIVATE "${MRADIO_MPRIS_GENERATED}")

target_link_libraries(mradio_mpris
    PUBLIC mradio::viewmodel mradio::ipc SDBusCpp::sdbus-c++)

mradio_configure_target(mradio_mpris)
mradio_install_package(mradio_mpris
    FIND_DEPENDENCIES mradio_viewmodel mradio_ipc "sdbus-c++ 2")
mradio_add_tests(tests)
