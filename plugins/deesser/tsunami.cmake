# SPDX-License-Identifier: GPL-3.0-or-later
# Plugin-local registration, included by the root CMakeLists.txt for TSUNAMI_PLUGIN_ID=deesser
# (cmake/TsunamiPlugins.cmake). Identity matches targets.json; plugin code TsDE is unused by
# the other registrations (TsFx fixture, TsS1, TsRV). The core is header-only (Processor.hpp).
tsunami_plugin(TsunamiDeEsser "${TSUNAMI_PLUGIN_DIR}" deesser "Tsunami De-Esser" TsDE "tsunami::deesser::Processor")
add_executable(deesser_adapter_test "${TSUNAMI_PLUGIN_SOURCE_ROOT}/tests/deesser/adapter_test.cpp")
target_link_libraries(deesser_adapter_test PRIVATE TsunamiDeEsser juce::juce_audio_utils)
add_test(NAME deesser.adapter COMMAND deesser_adapter_test)
