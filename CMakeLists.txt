# fixhop - client side auto bhop prediction fix for counter-strike 1.6

cmake_minimum_required(VERSION 3.21)

project(fixhop
    VERSION 1.0.0
    DESCRIPTION "client side auto bhop prediction fix for counter-strike 1.6"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "fixhop targets windows only")
endif()

# hl.exe is x86, so the dll must be too, and the injector has to match it to
# assume a shared kernel32 base
if(CMAKE_SIZEOF_VOID_P GREATER 4)
    message(FATAL_ERROR
        "fixhop must be built 32 bit: configure with -A Win32 (visual studio) "
        "or a 32 bit toolchain")
endif()

# version stamp, written before every build so the commit follows the tree
set(FIXHOP_VERSION_HEADER "${CMAKE_BINARY_DIR}/generated/build_info.h")
set(FIXHOP_VERSION_COMMAND
    "${CMAKE_COMMAND}"
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DOUTPUT_FILE=${FIXHOP_VERSION_HEADER}
        -DBUILD_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_version.cmake
)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
execute_process(COMMAND ${FIXHOP_VERSION_COMMAND})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# file properties windows shows on the binary. the commit is captured here at
# configure time, unlike the one in build_info.h, so reconfigure to refresh it
enable_language(RC)

execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE FIXHOP_RC_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT FIXHOP_RC_COMMIT)
    set(FIXHOP_RC_COMMIT "unknown")
endif()

set(FIXHOP_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(FIXHOP_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(FIXHOP_VERSION_PATCH ${PROJECT_VERSION_PATCH})

function(add_fixhop_version target description filetype filename)
    set(FIXHOP_FILE_DESCRIPTION "${description}")
    set(FIXHOP_FILE_TYPE "${filetype}")
    set(FIXHOP_INTERNAL_NAME "${target}")
    set(FIXHOP_ORIGINAL_FILENAME "${filename}")
    set(FIXHOP_COMMENTS "commit ${FIXHOP_RC_COMMIT}")

    set(dir "${CMAKE_CURRENT_BINARY_DIR}/generated/version")
    set(resource "${dir}/${target}_version.rc")

    file(MAKE_DIRECTORY "${dir}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows_version.rc.in" "${resource}" @ONLY)
    target_sources(${target} PRIVATE "${resource}")
endfunction()

# no visual c++ redistributable beside the dll, which is injected into a
# process we do not control
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# keep the artifacts with the source tree, and out of the per configuration
# subdirectories multi config generators would otherwise use
set(FIXHOP_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${FIXHOP_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${FIXHOP_OUTPUT_DIRECTORY}")

foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" config_upper)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${FIXHOP_OUTPUT_DIRECTORY}")
    set("CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper}" "${FIXHOP_OUTPUT_DIRECTORY}")
endforeach()

add_library(fixhop SHARED
    src/fixhop.cpp
    src/engfuncs.cpp
    src/asi.cpp
)
set_target_properties(fixhop PROPERTIES PREFIX "")

add_executable(inject
    src/inject.cpp
)

# the .asi is a byte copy of the dll, so it carries fixhop.dll as its original
# filename. that is the truth about where it came from
add_fixhop_version(fixhop
    "Client side auto bunnyhop prediction fix for Counter-Strike 1.6"
    VFT_DLL "fixhop.dll")

add_fixhop_version(inject
    "fixhop injector"
    VFT_APP "inject.exe")

add_custom_target(fixhop_version ALL
    COMMAND ${FIXHOP_VERSION_COMMAND}
    BYPRODUCTS "${FIXHOP_VERSION_HEADER}"
    COMMENT "Updating fixhop build version"
    VERBATIM
)
add_dependencies(fixhop fixhop_version)
target_include_directories(fixhop PRIVATE "${CMAKE_BINARY_DIR}/generated")

foreach(target fixhop inject)
    target_compile_definitions(${target} PRIVATE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
    if(MSVC)
        # engfuncs validates a scanned pointer table by calling through it, so
        # it needs structured exception handling
        target_compile_options(${target} PRIVATE /W4 /EHa)
    endif()
endforeach()

# a dll's subsystem is not used by the loader, but keep it matching what was
# tested in game rather than taking the linker's console default
target_link_options(fixhop PRIVATE /SUBSYSTEM:WINDOWS)

target_link_libraries(fixhop PRIVATE kernel32 user32)
target_link_libraries(inject PRIVATE kernel32)

# an .asi is just a dll under another extension, so the same binary is copied
# rather than compiled twice. it is what an asi loader picks up out of the game
# directory; the .dll is what inject.exe pushes into a running client
add_custom_command(TARGET fixhop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:fixhop>"
        "${FIXHOP_OUTPUT_DIRECTORY}/fixhop.asi"
    VERBATIM
)
