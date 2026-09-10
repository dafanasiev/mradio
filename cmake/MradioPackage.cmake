# Shared build settings for every package under packages/.
#
# This module is included both by the top-level aggregator and by each package
# when it is configured standalone, so the two builds cannot drift apart.

include_guard(GLOBAL)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MRADIO_CXX_STANDARD 23 CACHE STRING "C++ standard used by all packages")

# No package uses C++20 modules. Leaving the scan on costs a dyndep pass
# per translation unit on every build for no benefit.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(MRADIO_WERROR "Treat compiler warnings as errors" OFF)

# Empty for a normal build, otherwise a -fsanitize= list such as
# "address,undefined" or "thread". A list rather than a flag because address
# and thread cannot be combined, and this program is worth checking under both.
set(MRADIO_SANITIZE "" CACHE STRING "Sanitizers to build with, e.g. address,undefined")

# Links the C++ runtime into the binary instead of depending on the system one.
#
# This is the cheap half of "make it a static binary" and the half that usually
# matters: libstdc++ is the dependency most likely to be too old on another
# machine, while glibc is forward-compatible. The rest of the tree stays
# dynamic - libmpv cannot be linked statically at all, since the distribution
# ships no libmpv.a and mpv dlopens its audio outputs at runtime.
option(MRADIO_STATIC_CXX "Link libstdc++ and libgcc statically" OFF)

# Warnings we hold every package to. -Wconversion and -Wsign-conversion are in
# deliberately: this codebase converts between volume scales (0..1 vs 0..100),
# byte counts and durations often enough that silent narrowing is a realistic
# bug source rather than a theoretical one.
set(MRADIO_WARNINGS
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
    -Wconversion -Wsign-conversion
    -Wold-style-cast -Wcast-align -Wdouble-promotion
    -Wformat=2 -Wimplicit-fallthrough -Wunused)

# Applies the project-wide language level, warnings and optional sanitizers.
# Warnings stay PRIVATE: consumers of an installed package must not inherit our
# strictness, and third-party headers must not be judged by it.
function(mradio_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_${MRADIO_CXX_STANDARD})
    target_compile_options(${target} PRIVATE ${MRADIO_WARNINGS})

    if(MRADIO_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()

    if(MRADIO_STATIC_CXX)
        target_link_options(${target} PUBLIC -static-libstdc++ -static-libgcc)
    endif()

    if(MRADIO_SANITIZE)
        # PUBLIC: sanitizer flags must reach every translation unit and the
        # final link, otherwise the runtime is only half-instrumented.
        target_compile_options(${target} PUBLIC
            "-fsanitize=${MRADIO_SANITIZE}" -fno-omit-frame-pointer -g)
        target_link_options(${target} PUBLIC "-fsanitize=${MRADIO_SANITIZE}")
    endif()
endfunction()

# Bootstrap for a package configured standalone. Adds the shared cmake/ dir to
# the module path and applies the defaults the aggregator would otherwise set.
macro(mradio_package_init)
    if(PROJECT_IS_TOP_LEVEL)
        if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
            set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
        endif()
        include(CTest)
    endif()
endmacro()

# Installs a package target together with its headers and generates the
# <project>Config.cmake that lets sibling packages find it via find_package in
# a standalone build.
#
#   mradio_install_package(<target> [FIND_DEPENDENCIES <pkg>...])
#
# FIND_DEPENDENCIES lists packages that must be re-found by consumers; they are
# emitted as find_dependency() calls in the generated config. CONFIG_PREAMBLE is
# copied into that config verbatim, for dependencies find_dependency cannot
# express, such as a pkg-config module.
function(mradio_install_package target)
    cmake_parse_arguments(ARG "" "CONFIG_PREAMBLE" "FIND_DEPENDENCIES" ${ARGN})

    set(pkg "${PROJECT_NAME}")
    set(config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${pkg}")

    install(TARGETS ${target}
        EXPORT ${pkg}Targets
        ARCHIVE   DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY   DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME   DESTINATION "${CMAKE_INSTALL_BINDIR}"
        FILE_SET  HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    install(EXPORT ${pkg}Targets
        FILE      ${pkg}Targets.cmake
        NAMESPACE mradio::
        DESTINATION "${config_dir}")

    set(deps_block "")
    foreach(dep IN LISTS ARG_FIND_DEPENDENCIES)
        string(APPEND deps_block "find_dependency(${dep})\n")
    endforeach()

    # Verbatim lines for dependencies that find_dependency cannot express -
    # a pkg-config module, for instance.
    if(ARG_CONFIG_PREAMBLE)
        string(APPEND deps_block "${ARG_CONFIG_PREAMBLE}\n")
    endif()

    # The template is written here rather than kept as a file per package:
    # every package's config differs only in its find_dependency() list.
    set(template "${CMAKE_CURRENT_BINARY_DIR}/${pkg}Config.cmake.in")
    file(WRITE "${template}"
"@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
${deps_block}include(\"\${CMAKE_CURRENT_LIST_DIR}/${pkg}Targets.cmake\")
check_required_components(${pkg})
")

    configure_package_config_file("${template}"
        "${CMAKE_CURRENT_BINARY_DIR}/${pkg}Config.cmake"
        INSTALL_DESTINATION "${config_dir}")

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${pkg}ConfigVersion.cmake"
        VERSION       "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${pkg}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${pkg}ConfigVersion.cmake"
        DESTINATION "${config_dir}")
endfunction()

# Adds <dir> as a test subdirectory, but only when testing is enabled and
# Catch2 is actually installed. Catch2 stays optional so that a fresh checkout
# configures cleanly before the development dependencies are in place.
function(mradio_add_tests dir)
    if(NOT BUILD_TESTING)
        return()
    endif()

    find_package(Catch2 3 QUIET GLOBAL)
    if(NOT Catch2_FOUND)
        message(STATUS "${PROJECT_NAME}: Catch2 3 not found - tests disabled")
        return()
    endif()

    # Ubuntu's Catch2 package ships Catch.cmake next to Catch2Config.cmake but
    # does not put that directory on the module path, so catch_discover_tests
    # would be unreachable without this. Harmless where the config does it.
    list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")

    add_subdirectory("${dir}")
endfunction()

# Resolves the on-disk path of an imported executable.
#
# The plain IMPORTED_LOCATION property is often empty: a package built with a
# named configuration records IMPORTED_LOCATION_<CONFIG> instead. Ubuntu builds
# sdbus-c++-tools with the configuration literally called NONE, so reading only
# the unsuffixed property finds nothing.
function(mradio_imported_executable_location target out_var)
    get_target_property(location ${target} IMPORTED_LOCATION)

    if(NOT location)
        get_target_property(configurations ${target} IMPORTED_CONFIGURATIONS)
        foreach(config IN LISTS configurations)
            get_target_property(candidate ${target} IMPORTED_LOCATION_${config})
            if(candidate)
                set(location "${candidate}")
                break()
            endif()
        endforeach()
    endif()

    set(${out_var} "${location}" PARENT_SCOPE)
endfunction()

# Finds the sdbus-c++ code generator and proves it is really installed.
#
# Ubuntu ships the binary in libsdbus-c++-bin but ties it to libsdbus-c++-dev
# through Suggests rather than Depends, so find_package(sdbus-c++-tools) can
# succeed while the executable is absent. Catching that here gives an
# actionable message instead of a confusing failure in the middle of a build.
function(mradio_require_xml2cpp)
    find_package(sdbus-c++-tools 2 REQUIRED GLOBAL)

    mradio_imported_executable_location(SDBusCpp::sdbus-c++-xml2cpp location)
    if(NOT location OR NOT EXISTS "${location}")
        message(FATAL_ERROR
            "sdbus-c++-tools was found but its code generator is not on disk.\n"
            "Install it with:  sudo apt-get install libsdbus-c++-bin")
    endif()

    message(STATUS "${PROJECT_NAME}: using code generator ${location}")
endfunction()

# Generates sdbus-c++ adaptor and/or proxy headers from a D-Bus interface XML.
#
#   mradio_generate_dbus(<xml-path>
#       OUT_DIR <dir>
#       [ADAPTOR <header-name>]
#       [PROXY   <header-name>]
#       OUT_VAR  <variable>)
#
# The generated paths are appended to <variable>. List that variable among a
# target's sources so the build knows the headers must exist before compiling.
#
# Every path is passed as one fully quoted argument: in CMake a quote that
# starts mid-token is a literal character, so --adaptor="${dir}/x.h" would hand
# the quotes to the generator as part of the filename.
function(mradio_generate_dbus xml)
    cmake_parse_arguments(ARG "" "OUT_DIR;ADAPTOR;PROXY;OUT_VAR" "" ${ARGN})

    if(NOT ARG_OUT_DIR OR NOT ARG_OUT_VAR)
        message(FATAL_ERROR "mradio_generate_dbus: OUT_DIR and OUT_VAR are required")
    endif()
    if(NOT ARG_ADAPTOR AND NOT ARG_PROXY)
        message(FATAL_ERROR "mradio_generate_dbus: give at least one of ADAPTOR or PROXY")
    endif()

    set(outputs "")
    set(arguments "")

    if(ARG_ADAPTOR)
        list(APPEND outputs "${ARG_OUT_DIR}/${ARG_ADAPTOR}")
        list(APPEND arguments "--adaptor=${ARG_OUT_DIR}/${ARG_ADAPTOR}")
    endif()
    if(ARG_PROXY)
        list(APPEND outputs "${ARG_OUT_DIR}/${ARG_PROXY}")
        list(APPEND arguments "--proxy=${ARG_OUT_DIR}/${ARG_PROXY}")
    endif()

    get_filename_component(name "${xml}" NAME)

    add_custom_command(
        OUTPUT ${outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_OUT_DIR}"
        COMMAND SDBusCpp::sdbus-c++-xml2cpp ${arguments} "${xml}"
        DEPENDS "${xml}"
        COMMENT "Generating D-Bus bindings from ${name}"
        VERBATIM)

    set(${ARG_OUT_VAR} ${${ARG_OUT_VAR}} ${outputs} PARENT_SCOPE)
endfunction()
