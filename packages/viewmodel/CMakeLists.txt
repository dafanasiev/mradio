cmake_minimum_required(VERSION 3.28)

project(mradio_viewmodel
    VERSION 0.1.0
    DESCRIPTION "View model: player behaviour and the observable state the views bind to"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(MradioPackage)
mradio_package_init()

if(NOT TARGET mradio::core)
    find_package(mradio_core CONFIG REQUIRED)
endif()

# Depends on core only. It drives an IAudioEngine, never a concrete backend,
# and knows nothing about D-Bus - which is what keeps the program's behaviour
# testable without mpv or a bus.
add_library(mradio_viewmodel STATIC src/player_view_model.cpp)
add_library(mradio::viewmodel ALIAS mradio_viewmodel)

target_sources(mradio_viewmodel
    PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES include/mradio/vm/player_view_model.hpp)

target_link_libraries(mradio_viewmodel PUBLIC mradio::core)

mradio_configure_target(mradio_viewmodel)
mradio_install_package(mradio_viewmodel FIND_DEPENDENCIES mradio_core)
mradio_add_tests(tests)
