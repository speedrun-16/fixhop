# writes build_info.h from the current git checkout. run at configure
# time and again before every build, so the commit stamp follows the tree
# without needing a reconfigure

set(commit "unknown")
set(date "unknown")

find_package(Git QUIET)
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE commit_result
        OUTPUT_VARIABLE commit_out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(commit_result EQUAL 0 AND NOT commit_out STREQUAL "")
        set(commit "${commit_out}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" show -s --format=%cs HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE date_result
        OUTPUT_VARIABLE date_out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(date_result EQUAL 0 AND NOT date_out STREQUAL "")
        set(date "${date_out}")
    endif()

    # a dirty tree would otherwise claim to be exactly the tagged commit
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE status_out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT status_out STREQUAL "" AND NOT commit STREQUAL "unknown")
        set(commit "${commit}-dirty")
    endif()
endif()

string(CONCAT content
    "#pragma once\n\n"
    "#define FIXHOP_BUILD_VERSION \"" "${BUILD_VERSION}" "\"\n"
    "#define FIXHOP_BUILD_COMMIT \"" "${commit}" "\"\n"
    "#define FIXHOP_BUILD_DATE \"" "${date}" "\"\n"
)

# rewriting an identical header would rebuild everything that includes it
set(write_file TRUE)
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" existing)
    if(existing STREQUAL content)
        set(write_file FALSE)
    endif()
endif()

if(write_file)
    file(WRITE "${OUTPUT_FILE}" "${content}")
endif()
