# SPDX-License-Identifier: GPL-3.0-or-later
# Plugin-local registration, included by the root CMakeLists.txt for TSUNAMI_PLUGIN_ID=rvox
# (cmake/TsunamiPlugins.cmake). Product identity matches targets.json ("Tsunami Voice");
# plugin code TsRV is unique among registrations. The core is header-only (Processor.hpp).
tsunami_plugin(TsunamiRVox "${TSUNAMI_PLUGIN_DIR}" rvox "Tsunami Voice" TsRV "tsunami::rvox::Processor")
foreach(test IN ITEMS adapter editor)
  add_executable(rvox_${test}_test "${TSUNAMI_PLUGIN_SOURCE_ROOT}/tests/rvox/${test}_test.cpp")
  target_link_libraries(rvox_${test}_test PRIVATE TsunamiRVox juce::juce_audio_utils)
  add_test(NAME rvox.${test} COMMAND rvox_${test}_test)
endforeach()
# Portable core checks for the meter tap and unchanged audio, built by the same native toolchain.
foreach(test IN ITEMS meters output_regression)
  add_executable(rvox_test_${test} "${TSUNAMI_PLUGIN_SOURCE_ROOT}/tests/rvox/test_${test}.cpp")
  target_include_directories(rvox_test_${test} PRIVATE "${TSUNAMI_PLUGIN_DIR}" "${TSUNAMI_PLUGIN_SOURCE_ROOT}/tests/rvox")
  target_compile_features(rvox_test_${test} PRIVATE cxx_std_17)
  add_test(NAME rvox.${test} COMMAND rvox_test_${test})
endforeach()
# Actual compiled RVox CLAP module driven through the CLAP ABI: host-selectable mono/stereo
# audio-ports configs, selected ports, audio vs the direct core, parameter readback and state.
add_executable(rvox_clap_abi_test "${TSUNAMI_PLUGIN_SOURCE_ROOT}/tests/rvox/clap_abi_test.cpp")
target_include_directories(rvox_clap_abi_test PRIVATE "${TSUNAMI_PLUGIN_DIR}")
target_compile_features(rvox_clap_abi_test PRIVATE cxx_std_17)
target_link_libraries(rvox_clap_abi_test PRIVATE clap-core ${CMAKE_DL_LIBS})
add_dependencies(rvox_clap_abi_test TsunamiRVox_CLAP)
add_test(NAME rvox.clap_abi COMMAND rvox_clap_abi_test "$<TARGET_FILE:TsunamiRVox_CLAP>")
