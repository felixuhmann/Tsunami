# SPDX-License-Identifier: GPL-3.0-or-later
# Plugin-local registration, included by the root CMakeLists.txt for TSUNAMI_PLUGIN_ID=deesser
# (cmake/TsunamiPlugins.cmake). Identity matches targets.json; plugin code TsDE is unused by
# the other registrations (TsFx fixture, TsS1, TsRV). The core is header-only (Processor.hpp).
tsunami_plugin(TsunamiDeEsser "${TSUNAMI_PLUGIN_DIR}" deesser "Tsunami De-Esser" TsDE "tsunami::deesser::Processor")
add_executable(deesser_adapter_test "${TSUNAMI_PLUGIN_SOURCE_ROOT}/tests/deesser/adapter_test.cpp")
target_link_libraries(deesser_adapter_test PRIVATE TsunamiDeEsser juce::juce_audio_utils)
add_test(NAME deesser.adapter COMMAND deesser_adapter_test)
# Product CLAP mono/stereo ABI regression: loads the real compiled .clap (not the SDK fixture).
add_executable(deesser_clap_abi_test "${TSUNAMI_PLUGIN_SOURCE_ROOT}/tests/deesser/clap_abi_test.cpp")
target_include_directories(deesser_clap_abi_test PRIVATE "${TSUNAMI_PLUGIN_DIR}")
target_link_libraries(deesser_clap_abi_test PRIVATE clap-core ${CMAKE_DL_LIBS})
add_dependencies(deesser_clap_abi_test TsunamiDeEsser_CLAP)
add_test(NAME deesser.clap_abi COMMAND deesser_clap_abi_test "$<TARGET_FILE:TsunamiDeEsser_CLAP>")
