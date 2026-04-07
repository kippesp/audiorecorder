execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${SOURCE_DIR}"
  OUTPUT_VARIABLE _hash
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _result
)

if(NOT _result EQUAL 0)
  set(_hash "")
endif()

if(_hash STREQUAL "")
  set(_display "")
else()
  set(_display " (${_hash})")
endif()

set(_content
"#pragma once
#define RA_GIT_HASH \"${_hash}\"
#define RA_GIT_HASH_DISPLAY \"${_display}\"
")

if(EXISTS "${OUTPUT_FILE}")
  file(READ "${OUTPUT_FILE}" _existing)
else()
  set(_existing "")
endif()

if(NOT _existing STREQUAL _content)
  file(WRITE "${OUTPUT_FILE}" "${_content}")
endif()
