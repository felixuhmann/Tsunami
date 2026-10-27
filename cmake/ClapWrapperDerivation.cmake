# SPDX-License-Identifier: GPL-3.0-or-later
# Host-selectable CLAP mono/stereo audio-ports configs. The pinned clap-juce-extensions wrapper
# (MIT) is copied into the build tree with hash-bound edits from clap-wrapper-audio-ports-config.json;
# the dependency tree itself is never modified. macOS compiles the wrapper through the pristine .mm
# include and therefore keeps upstream behaviour (no configs offered).
function(tsunami_derive_clap_wrapper)
  if(APPLE)
    return()
  endif()
  get_target_property(cjd clap_juce_sources CLAP_JUCE_SOURCE_DIR)
  set(pristine "${cjd}/src/wrapper/clap-juce-wrapper.cpp")
  set(edits "${PROJECT_SOURCE_DIR}/cmake/clap-wrapper-audio-ports-config.json")
  set(derived "${CMAKE_BINARY_DIR}/tsunami-clap-wrapper/clap-juce-wrapper.cpp")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/cmake/derive_source.py"
    --input "${pristine}" --input-sha256 82fb7862a2a40c718dcf53560ce69f3af4244b068c25ea7a25bfe5d16c963cc6
    --edits "${edits}" --output "${derived}"
    --output-sha256 e1193570d026aa258322393c9cf40181522dca4322347119f47103695e91a3a4
    COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${pristine}" "${edits}" "${PROJECT_SOURCE_DIR}/cmake/derive_source.py")
  set(replaced 0)
  foreach(prop SOURCES INTERFACE_SOURCES)
    get_target_property(srcs clap_juce_sources ${prop})
    if(NOT srcs)
      continue()
    endif()
    set(out "")
    foreach(s IN LISTS srcs)
      if(s STREQUAL pristine)
        set(s "${derived}")
        math(EXPR replaced "${replaced} + 1")
      endif()
      list(APPEND out "${s}")
    endforeach()
    set_property(TARGET clap_juce_sources PROPERTY ${prop} "${out}")
  endforeach()
  if(replaced EQUAL 0)
    message(FATAL_ERROR "clap_juce_sources does not list ${pristine}; cannot use the derived wrapper")
  endif()
  message(STATUS "derived clap-juce-wrapper.cpp: source 82fb7862a2a40c718dcf53560ce69f3af4244b068c25ea7a25bfe5d16c963cc6 -> ${derived} (${replaced} source list entries)")
endfunction()
