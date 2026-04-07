function(add_git_version_tracking target)
  set(_output_file "${CMAKE_CURRENT_BINARY_DIR}/git_version.h")
  set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WriteGitHash.cmake")

  add_custom_target(git_version_update ALL
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DOUTPUT_FILE=${_output_file}
      -P ${_script}
    BYPRODUCTS ${_output_file}
    COMMENT "Updating git_version.h"
  )

  add_dependencies(${target} git_version_update)
endfunction()
