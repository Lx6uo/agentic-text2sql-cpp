if(NOT DEFINED TEXT2SQL OR NOT EXISTS "${TEXT2SQL}")
  message(FATAL_ERROR "TEXT2SQL executable is required")
endif()
if(NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(WRITE "${TEST_ROOT}/input.txt" "/model\n/verbose on\n/exit\n")

execute_process(
  COMMAND "${TEXT2SQL}" --json
  WORKING_DIRECTORY "${TEST_ROOT}"
  INPUT_FILE "${TEST_ROOT}/input.txt"
  OUTPUT_VARIABLE repl_out
  ERROR_VARIABLE repl_err
  RESULT_VARIABLE repl_status
  TIMEOUT 10)
if(NOT repl_status EQUAL 0)
  message(FATAL_ERROR "REPL failed (${repl_status}): ${repl_err}")
endif()
string(REGEX MATCHALL "API key: not configured|Verbose: on|Goodbye" repl_matches "${repl_out}")
list(LENGTH repl_matches repl_count)
if(NOT repl_count EQUAL 3)
  message(FATAL_ERROR "Unexpected JSON REPL output: ${repl_out}")
endif()

execute_process(
  COMMAND "${TEXT2SQL}" query --json
  WORKING_DIRECTORY "${TEST_ROOT}"
  OUTPUT_VARIABLE invalid_out
  ERROR_VARIABLE invalid_err
  RESULT_VARIABLE invalid_status
  TIMEOUT 10)
if(NOT invalid_status EQUAL 2 OR NOT invalid_out MATCHES "--db is required")
  message(FATAL_ERROR "Invalid-argument contract failed (${invalid_status}): ${invalid_out} ${invalid_err}")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
