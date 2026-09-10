cmake_minimum_required(VERSION 3.28)

project(mradio_tray
    VERSION 0.1.0
    DESCRIPTION "StatusNotifierItem tray view onto the player"
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

set(MRADIO_TRAY_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Three separate interfaces: our icon, our menu, and the watcher we register
# with. The watcher is somebody else's object, hence a proxy rather than an
# adaptor.
mradio_generate_dbus("${CMAKE_CURRENT_SOURCE_DIR}/xml/status_notifier_item.xml"
    OUT_DIR "${MRADIO_TRAY_GENERATED}" ADAPTOR sni_adaptor.h
    OUT_VAR MRADIO_TRAY_SOURCES)

mradio_generate_dbus("${CMAKE_CURRENT_SOURCE_DIR}/xml/dbusmenu.xml"
    OUT_DIR "${MRADIO_TRAY_GENERATED}" ADAPTOR dbusmenu_adaptor.h
    OUT_VAR MRADIO_TRAY_SOURCES)

mradio_generate_dbus("${CMAKE_CURRENT_SOURCE_DIR}/xml/status_notifier_watcher.xml"
    OUT_DIR "${MRADIO_TRAY_GENERATED}" PROXY watcher_proxy.h
    OUT_VAR MRADIO_TRAY_SOURCES)

add_library(mradio_tray STATIC
    src/menu_model.cpp
    src/icon.cpp
    ${MRADIO_TRAY_SOURCES})

add_library(mradio::tray ALIAS mradio_tray)

target_sources(mradio_tray
    PUBLIC FILE_SET HEADERS
    BASE_DIRS include
    FILES
        include/mradio/tray/icon.hpp
        include/mradio/tray/menu_model.hpp)

# SYSTEM and PRIVATE: generated code should neither be judged by our warning
# set nor be reachable from outside this package.
target_include_directories(mradio_tray SYSTEM PRIVATE "${MRADIO_TRAY_GENERATED}")

target_link_libraries(mradio_tray
    PUBLIC mradio::viewmodel mradio::ipc SDBusCpp::sdbus-c++)

mradio_configure_target(mradio_tray)
mradio_install_package(mradio_tray
    FIND_DEPENDENCIES mradio_viewmodel mradio_ipc "sdbus-c++ 2")
mradio_add_tests(tests)
