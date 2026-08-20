function(relaydesk_windows_runtime_minor compiler_version output_variable)
  string(
    REGEX MATCH
    "^[0-9]+\\.([0-9]+)(\\.|$)"
    _relaydesk_msvc_compiler_version
    "${compiler_version}"
  )
  if (NOT _relaydesk_msvc_compiler_version)
    message(FATAL_ERROR "Unable to determine the MSVC runtime minor version from compiler version: ${compiler_version}")
  endif()

  set(${output_variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
