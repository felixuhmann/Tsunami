# SPDX-License-Identifier: GPL-3.0-or-later
# Synthetic CLAP layout tests: a mono-capable variant of the infrastructure fixture core plus a thin
# ABI harness that loads the REAL compiled .clap modules. Not a campaign target or a screenshot host.
set(fixture_dir "${PROJECT_SOURCE_DIR}/tests/foundation/fixture")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/mono-generated")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_metadata.py"
  "${fixture_dir}/parameters.json" "${generated}/Generated.hpp" --id fixture_mono
  --product "Tsunami Mono Layout Fixture" --core "tsunami::fixture::Processor"
  COMMAND_ERROR_IS_FATAL ANY)
juce_add_plugin(TsunamiMonoFixture COMPANY_NAME "Tsunami" BUNDLE_ID "org.tsunami.fixture_mono"
  PLUGIN_MANUFACTURER_CODE Tsun PLUGIN_CODE TsFm PRODUCT_NAME "Tsunami Mono Layout Fixture"
  FORMATS VST3 IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx)
target_sources(TsunamiMonoFixture PRIVATE MonoLayoutPlugin.cpp)
target_include_directories(TsunamiMonoFixture PUBLIC "${PROJECT_SOURCE_DIR}/shared" "${fixture_dir}" "${generated}")
target_compile_definitions(TsunamiMonoFixture PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TsunamiMonoFixture PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
clap_juce_extensions_plugin(TARGET TsunamiMonoFixture CLAP_ID "org.tsunami.fixture_mono" CLAP_FEATURES audio-effect mono stereo)

add_executable(clap_layout_test clap_layout_test.cpp)
target_link_libraries(clap_layout_test PRIVATE clap-core ${CMAKE_DL_LIBS})
add_dependencies(clap_layout_test TsunamiMonoFixture_CLAP TsunamiFixture_CLAP)
add_test(NAME clap.layouts.mono_stereo
  COMMAND clap_layout_test "$<TARGET_FILE:TsunamiMonoFixture_CLAP>" org.tsunami.fixture_mono mono-stereo)
add_test(NAME clap.layouts.stereo_only
  COMMAND clap_layout_test "$<TARGET_FILE:TsunamiFixture_CLAP>" org.tsunami.fixture stereo-only)
