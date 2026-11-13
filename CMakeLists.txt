cmake_minimum_required(VERSION 3.16)
project(pwnagotchi-zeroday VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Portable zero-dependency functional core (header-only)
add_library(zd_core INTERFACE)
target_include_directories(zd_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/core/include")

# Portable immediate-mode painter & RGB565 software surface (header-only)
add_library(zd_render INTERFACE)
target_include_directories(zd_render INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/render/include")
target_link_libraries(zd_render INTERFACE zd_core)

if(MSVC)
    set(ZD_WARN_FLAGS /W4 /WX)
else()
    set(ZD_WARN_FLAGS -Wall -Wextra -Werror)
endif()

# Native desktop / Linux SBC terminal simulator
add_executable(zd_sim platforms/native/main.cpp)
target_link_libraries(zd_sim PRIVATE zd_render)
target_compile_options(zd_sim PRIVATE ${ZD_WARN_FLAGS})

# ----------------------------------------------------------------------------------------
# Linux desktop targets (CLI/TUI + Qt6 GUI) with hybrid WiFi backend.
# Native libpcap frame capture/injection is used when available; otherwise the CLI-tool
# backend (iw / nmcli / airodump-ng / aireplay-ng) is the fallback at runtime.
# ----------------------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBPCAP IMPORTED_TARGET libpcap)
    endif()
    if(NOT LIBPCAP_FOUND)
        find_library(PCAP_LIB pcap)
        find_path(PCAP_INC pcap/pcap.h)
    endif()

    set(ZD_LINUX_INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}/core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/render/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/backend"
        "${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/app")

    # --- CLI / ncurses TUI ---
    set(CURSES_NEED_NCURSES TRUE)
    find_package(Curses)
    if(Curses_FOUND)
        add_executable(zd-linux platforms/linux/cli/main.cpp)
        target_include_directories(zd-linux PRIVATE ${ZD_LINUX_INCLUDES} ${CURSES_INCLUDE_DIRS})
        target_link_libraries(zd-linux PRIVATE ${CURSES_LIBRARIES} pthread)
        target_compile_options(zd-linux PRIVATE ${ZD_WARN_FLAGS})
        if(LIBPCAP_FOUND)
            target_compile_definitions(zd-linux PRIVATE ZD_WITH_LIBPCAP)
            target_link_libraries(zd-linux PRIVATE PkgConfig::LIBPCAP)
        elseif(PCAP_LIB)
            target_compile_definitions(zd-linux PRIVATE ZD_WITH_LIBPCAP)
            target_include_directories(zd-linux PRIVATE ${PCAP_INC})
            target_link_libraries(zd-linux PRIVATE ${PCAP_LIB})
        endif()
    else()
        message(STATUS "ncurses not found: skipping zd-linux CLI target")
    endif()

    # --- Qt6 GUI ---
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        add_executable(zd-gui
            platforms/linux/gui/main.cpp
            platforms/linux/gui/main_window.cpp
            platforms/linux/gui/main_window.hpp
            platforms/linux/gui/pet_widget.hpp)
        # Scoped to this target only: CMAKE_AUTOMOC is a global switch and would
        # otherwise make every later target in this file (including the non-Qt
        # test executables) pay for moc/autogen machinery they never use.
        set_target_properties(zd-gui PROPERTIES AUTOMOC ON)
        target_include_directories(zd-gui PRIVATE ${ZD_LINUX_INCLUDES})
        target_link_libraries(zd-gui PRIVATE Qt6::Widgets pthread)
        target_compile_options(zd-gui PRIVATE ${ZD_WARN_FLAGS})
        if(LIBPCAP_FOUND)
            target_compile_definitions(zd-gui PRIVATE ZD_WITH_LIBPCAP)
            target_link_libraries(zd-gui PRIVATE PkgConfig::LIBPCAP)
        elseif(PCAP_LIB)
            target_compile_definitions(zd-gui PRIVATE ZD_WITH_LIBPCAP)
            target_include_directories(zd-gui PRIVATE ${PCAP_INC})
            target_link_libraries(zd-gui PRIVATE ${PCAP_LIB})
        endif()
    else()
        message(STATUS "Qt6 Widgets not found: skipping zd-gui target")
    endif()
endif()

# Automated unit tests
enable_testing()

add_executable(test_functional_core tests/core/test_functional_core.cpp)
target_link_libraries(test_functional_core PRIVATE zd_core)
target_compile_options(test_functional_core PRIVATE ${ZD_WARN_FLAGS})
add_test(NAME core_functional_tests COMMAND test_functional_core)

add_executable(test_painter tests/render/test_painter.cpp)
target_link_libraries(test_painter PRIVATE zd_render)
target_compile_options(test_painter PRIVATE ${ZD_WARN_FLAGS})
add_test(NAME render_and_hal_tests COMMAND test_painter)

add_executable(test_secops tests/core/test_secops.cpp)
target_link_libraries(test_secops PRIVATE zd_core)
target_compile_options(test_secops PRIVATE ${ZD_WARN_FLAGS})
add_test(NAME security_engine_tests COMMAND test_secops)

add_executable(test_linux_parsers tests/core/test_linux_parsers.cpp)
target_include_directories(test_linux_parsers PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/backend")
target_compile_options(test_linux_parsers PRIVATE ${ZD_WARN_FLAGS})
add_test(NAME linux_backend_parser_tests COMMAND test_linux_parsers)

# The tests rely on assert(). Release builds define NDEBUG, which would compile every
# assertion away (making the tests no-ops and tripping -Werror on assert-only variables),
# so force assertions back on for the test executables regardless of build type.
foreach(zd_test test_functional_core test_painter test_secops test_linux_parsers)
    if(MSVC)
        target_compile_options(${zd_test} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${zd_test} PRIVATE -UNDEBUG)
    endif()
endforeach()

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(
        NAME web_and_parity_tests
        COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/test_web_core.js"
    )
endif()
