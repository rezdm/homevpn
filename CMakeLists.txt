cmake_minimum_required(VERSION 3.16)
project(HomeVPN VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Core sources
set(HOMEVPN_CORE_SRC
    HomeVPNCore.cpp
    HomeVPNCore.h
)

# TUI build
find_package(Curses REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

add_executable(HomeVPN_TUI
    HomeVPN_TUI.cpp
    ${HOMEVPN_CORE_SRC}
)
target_include_directories(HomeVPN_TUI PRIVATE
    ${CURSES_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
)
target_link_libraries(HomeVPN_TUI
    ${CURSES_LIBRARIES}
    ${CURL_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    Threads::Threads
)
target_compile_options(HomeVPN_TUI PRIVATE ${JSONCPP_CFLAGS_OTHER})

# GUI build
pkg_check_modules(GTK3 gtk+-3.0)
pkg_check_modules(APPINDICATOR ayatana-appindicator3-0.1)
if(NOT APPINDICATOR_FOUND)
    pkg_check_modules(APPINDICATOR appindicator3-0.1)
    message(STATUS "Using legacy appindicator")
else()
    message(STATUS "Using ayatana appindicator")
endif()

if(GTK3_FOUND AND APPINDICATOR_FOUND)
    add_executable(HomeVPN_GUI
        HomeVPN_GUI.cpp
        ${HOMEVPN_CORE_SRC}
    )
    target_include_directories(HomeVPN_GUI PRIVATE
        ${GTK3_INCLUDE_DIRS}
        ${APPINDICATOR_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
    )
    target_link_libraries(HomeVPN_GUI
        ${GTK3_LIBRARIES}
        ${APPINDICATOR_LIBRARIES}
        ${CURL_LIBRARIES}
        pthread
    )
    target_compile_options(HomeVPN_GUI PRIVATE
        ${GTK3_CFLAGS_OTHER}
        ${APPINDICATOR_CFLAGS_OTHER}
    )
    target_link_directories(HomeVPN_GUI PRIVATE
        ${GTK3_LIBRARY_DIRS}
        ${APPINDICATOR_LIBRARY_DIRS}
    )
endif()

# Installation
include(GNUInstallDirs)
install(TARGETS HomeVPN_TUI
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(TARGET HomeVPN_GUI)
    install(TARGETS HomeVPN_GUI
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# Custom targets for convenience
add_custom_target(deps
    COMMAND sudo apt update
    COMMAND sudo apt install -y libncurses5-dev libcurl4-openssl-dev libjsoncpp-dev build-essential cmake libgtk-3-dev libayatana-appindicator3-dev
    COMMENT "Installing build dependencies"
)

add_custom_target(setup-mount
    COMMAND sudo mkdir -p /mnt/homeshare
    COMMAND sudo chmod 755 /mnt/homeshare
    COMMENT "Creating mount directory"
)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")

# Print usage information
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "")
    message(STATUS "HomeVPN Build System")
    message(STATUS "===================")
    message(STATUS "")
    message(STATUS "Quick start:")
    message(STATUS "  cmake -B build")
    message(STATUS "  cmake --build build")
    message(STATUS "  cp config_example ~/.homeVPN")
    message(STATUS "  # Edit ~/.homeVPN with your settings")
    message(STATUS "  cmake --build build --target setup-mount")
    message(STATUS "  ./build/HomeVPN_TUI")
    message(STATUS "")
    message(STATUS "Install system-wide:")
    message(STATUS "  sudo cmake --install build")
    message(STATUS "")
    message(STATUS "Install dependencies:")
    message(STATUS "  cmake --build build --target deps")
    message(STATUS "")
endif()

# Create config example file if it exists
if(EXISTS "${CMAKE_SOURCE_DIR}/config_example")
    configure_file(
        "${CMAKE_SOURCE_DIR}/config_example"
        "${CMAKE_BINARY_DIR}/config_example"
        COPYONLY
    )
endif()

# CPack configuration for creating packages
set(CPACK_PACKAGE_NAME "HomeVPN")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight TUI/GUI VPN and Network Share Manager")
set(CPACK_PACKAGE_DESCRIPTION "A simple terminal and graphical user interface application for managing VPN connections and network shares")
set(CPACK_PACKAGE_CONTACT "user@example.com")
set(CPACK_PACKAGE_VENDOR "HomeVPN Project")

set(CPACK_DEBIAN_PACKAGE_DEPENDS "libncurses5, libcurl4, libjsoncpp1, libgtk-3-0, libayatana-appindicator3-1")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

set(CPACK_RPM_PACKAGE_REQUIRES "ncurses, libcurl, jsoncpp, gtk3, ayatana-appindicator3")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")

include(CPack) 