# Applies PATCH to the git checkout in the working directory, unless it is applied already. A checkout with
# another version of the patch, or a part of it, is reset to its revision first.
# usage: cmake -DGIT_EXECUTABLE=<git> -DPATCH=<file> -P ApplyPatch.cmake

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH}"
    RESULT_VARIABLE result
    OUTPUT_QUIET ERROR_QUIET
)
if (result EQUAL 0)
    return()
endif()

execute_process(COMMAND "${GIT_EXECUTABLE}" reset --quiet --hard RESULT_VARIABLE result)
if (result EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${PATCH}" RESULT_VARIABLE result)
endif()
if (NOT result EQUAL 0)
    message(FATAL_ERROR "${PATCH} does not apply to ${CMAKE_CURRENT_SOURCE_DIR}")
endif()
