# SPDX-License-Identifier: GPL-3.0-or-later
# Prevent local source overrides from silently restoring the old framework.
# Version validation is not a claim that arbitrary local edits match the archive.
function(tsunami_verify_juce source)
  if(NOT EXISTS "${source}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE 9.0.4 required: source CMakeLists.txt is missing")
  endif()
  file(READ "${source}/CMakeLists.txt" juce_cmake)
  if(NOT juce_cmake MATCHES "project\\(JUCE VERSION 9\\.0\\.4[ \t\r\n]")
    message(FATAL_ERROR "JUCE 9.0.4 required: local source version does not match the release")
  endif()
endfunction()
