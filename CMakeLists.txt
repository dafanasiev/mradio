cmake_minimum_required(VERSION 3.28)

project(mradio
    VERSION 0.1.0
    DESCRIPTION "Tray-only internet radio player, controlled over D-Bus"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(MradioPackage)

include(CTest)

# Packages are listed in dependency order. Each one is also a standalone CMake
# project: configuring packages/<name> on its own works and resolves siblings
# through find_package. That keeps a package from silently depending on a
# target that only exists because the aggregator happened to add it earlier.
set(MRADIO_PACKAGES
    m3uparser
    core
    stations
    audio
    ipc
    viewmodel
    mpris
    tray
    app)

foreach(pkg IN LISTS MRADIO_PACKAGES)
    add_subdirectory("packages/${pkg}")
endforeach()

message(STATUS "mradio ${PROJECT_VERSION}: packages = ${MRADIO_PACKAGES}")
