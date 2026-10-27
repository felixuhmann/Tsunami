# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(tsunami_rcomp_gain_stage LANGUAGES CXX)

# Partial coefficient-domain DSP library, deliberately not a plugin target.
add_library(tsunami_rcomp_gain_stage INTERFACE)
target_include_directories(tsunami_rcomp_gain_stage INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tsunami_rcomp_gain_stage INTERFACE cxx_std_17)

include(CTest)
if(BUILD_TESTING)
    add_executable(rcomp_gain_stage_tests ../../tests/rcomp/test_gain_stage.cpp)
    target_link_libraries(rcomp_gain_stage_tests PRIVATE tsunami_rcomp_gain_stage)
    if(MSVC)
        target_compile_options(rcomp_gain_stage_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(rcomp_gain_stage_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME rcomp_gain_stage COMMAND rcomp_gain_stage_tests)
endif()
