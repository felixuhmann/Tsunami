# SPDX-License-Identifier: GPL-3.0-or-later
# Host-selectable CLAP mono/stereo configurations (clap.audio-ports-config).
# The pinned clap-juce-extensions wrapper exposes no audio-ports configs and reports default bus
# layouts only. This applies the checked project patch to a BUILD-LOCAL copy of the pristine wrapper
# source and swaps that copy into the upstream clap_juce_sources interface target. The fetched
# dependency tree is never modified (release source-integrity gates stay valid); the pristine input,
# patch and output are hash-pinned (LF-normalized). Upstream MIT notice: LICENSES/ and
# THIRD_PARTY_NOTICES.md. Windows/Linux only: the macOS .mm wrapper includes the pristine file.
set(TSUNAMI_CLAP_WRAPPER_SHA256 82fb7862a2a40c718dcf53560ce69f3af4244b068c25ea7a25bfe5d16c963cc6)
set(TSUNAMI_CLAP_WRAPPER_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/clap-juce-wrapper-audio-ports-config.patch")
set(TSUNAMI_CLAP_WRAPPER_PATCH_SHA256 dfc3dcfd471177b6ea1bf2feb4973e345b3e0283e4bef1e09f5c28a6b528d837)
set(TSUNAMI_CLAP_WRAPPER_PATCHED_SHA256 302fdf4c8573b605b772c9441bd67ea7eb903cb7773eb6d2d09ac3012039f1f9)
function(tsunami_patch_clap_wrapper)
  if(APPLE)
    message(WARNING "CLAP audio-ports-config patch not applied on Apple: upstream stereo-default CLAP layout only")
    return()
  endif()
  get_target_property(clap_dir clap_juce_sources CLAP_JUCE_SOURCE_DIR)
  set(pristine "${clap_dir}/src/wrapper/clap-juce-wrapper.cpp")
  get_target_property(sources clap_juce_sources INTERFACE_SOURCES)
  list(FIND sources "${pristine}" at)
  list(LENGTH sources source_count)
  if(at EQUAL -1 OR NOT source_count EQUAL 1)
    message(FATAL_ERROR "clap_juce_sources does not carry exactly the pinned wrapper source: ${sources}")
  endif()
  set(patched "${CMAKE_BINARY_DIR}/tsunami-clap-wrapper/clap-juce-wrapper.cpp")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/apply_source_patch.py"
    "${pristine}" "${TSUNAMI_CLAP_WRAPPER_PATCH}" "${patched}"
    --source-sha256 ${TSUNAMI_CLAP_WRAPPER_SHA256}
    --patch-sha256 ${TSUNAMI_CLAP_WRAPPER_PATCH_SHA256}
    --output-sha256 ${TSUNAMI_CLAP_WRAPPER_PATCHED_SHA256}
    COMMAND_ERROR_IS_FATAL ANY)
  set_property(TARGET clap_juce_sources PROPERTY INTERFACE_SOURCES "${patched}")
  set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${pristine}" "${TSUNAMI_CLAP_WRAPPER_PATCH}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/apply_source_patch.py")
endfunction()
