# SPDX-License-Identifier: GPL-3.0-or-later
# Generic target selection: TSUNAMI_PLUGIN_ID names plugins/<id> under TSUNAMI_PLUGIN_SOURCE_ROOT.
# The id must be a lowercase identifier and the folder must provide the plugin contract files.
set(TSUNAMI_PLUGIN_REQUIRED_FILES parameters.json Processor.hpp)
function(tsunami_resolve_plugin root id out_dir)
  if(NOT id MATCHES "^[a-z][a-z0-9_]*$")
    message(FATAL_ERROR "Invalid TSUNAMI_PLUGIN_ID '${id}' (expected [a-z][a-z0-9_]*)")
  endif()
  file(TO_CMAKE_PATH "${root}" root) # native Windows roots (C:\...) -> forward slashes
  set(dir "${root}/plugins/${id}")
  foreach(file IN LISTS TSUNAMI_PLUGIN_REQUIRED_FILES)
    if(NOT EXISTS "${dir}/${file}")
      message(FATAL_ERROR "Plugin '${id}' unavailable: ${dir}/${file} missing; no substitute is built")
    endif()
  endforeach()
  set(${out_dir} "${dir}" PARENT_SCOPE)
endfunction()
if(TSUNAMI_RESOLVE_ONLY)
  tsunami_resolve_plugin("${TSUNAMI_PLUGIN_SOURCE_ROOT}" "${TSUNAMI_PLUGIN_ID}" dir)
  message(STATUS "TSUNAMI_PLUGIN_DIR=${dir}")
endif()
