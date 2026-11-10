cmake_minimum_required(VERSION 3.16)
project(pwnagotchi-zeroday VERSION 0.2.0 LANGUAGES CXX)

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

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(
        NAME web_and_parity_tests
        COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/test_web_core.js"
    )
endif()
