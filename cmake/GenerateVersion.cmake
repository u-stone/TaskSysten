# Try to get the hash automatically if not provided externally
if(NOT TASK_ENGINE_GIT_HASH OR TASK_ENGINE_GIT_HASH STREQUAL "")
    set(TASK_ENGINE_GIT_HASH "unknown")
    find_package(Git QUIET)
    # Only execute if Git is found and the .git directory exists
    if(GIT_FOUND AND EXISTS "${SOURCE_DIR}/.git")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY "${SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(GIT_HASH)
            set(TASK_ENGINE_GIT_HASH "${GIT_HASH}")
        endif()
    endif()
endif()

# Ensure the output directory exists before generating the file
get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
if(NOT EXISTS "${OUTPUT_DIR}")
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
endif()

# Generate the version header file
configure_file("${INPUT_FILE}" "${OUTPUT_FILE}" @ONLY)