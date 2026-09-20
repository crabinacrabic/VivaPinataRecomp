# vivapinata - ReXGlue Recompiled Project (Viva Pinata, 2006, Title ID 4D5307F2)
#
# SDK boilerplate lives in generated/rexglue.cmake (emitted by `rexglue init`,
# read-only). Codegen is a custom command of that file: the Visual Studio
# build runs `rexglue codegen vivapinata_manifest.toml` whenever the manifest,
# an included config/*.toml or game_files/default.xex changes. Never run
# codegen or the build from a terminal.

cmake_minimum_required(VERSION 3.25)
project(vivapinata LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pin the SDK this project was scaffolded with. cmake/fetch-rexglue-sdk.cmake
# downloads it into rexglue/<platform> only when that folder is missing; the
# checked-out copy under rexglue/win-amd64 is left untouched.
set(REXSDK_VERSION "0.10.0.8-dev.g1406e1b")

if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
    set(REXSDK_PLATFORM "win-amd64")
elseif(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^ARM64$")
    message(FATAL_ERROR "win-arm64 ReXGlue SDK release is not published")
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(REXSDK_PLATFORM "mac-arm64")
elseif(APPLE)
    set(REXSDK_PLATFORM "mac-amd64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(REXSDK_PLATFORM "linux-arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(REXSDK_PLATFORM "linux-amd64")
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND}
            "-DREXSDK_VERSION='${REXSDK_VERSION}'"
            "-DREXSDK_PLATFORM=${REXSDK_PLATFORM}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fetch-rexglue-sdk.cmake")

# generated/rexglue.cmake treats a non-empty REXSDK_VERSION as an EXACT
# find_package() pin; clear it so the installed 0.10.0.x package is accepted.
set(REXSDK_VERSION "")

# Bootstrap for a tree that has never run codegen. rexglue.cmake only creates
# the vivapinata_recomp OBJECT library when generated/default/sources.cmake
# exists at configure time; on a fresh checkout it does not, the first build
# generates it through the codegen custom command, and the link then fails
# with "undefined symbol: PPCImageConfig" until CMake is re-run. Running
# codegen once here (still inside the Visual Studio configure, never from a
# terminal) makes the first build complete. The build-time codegen command
# stays the source of truth: its stamp makes this a no-op afterwards.
set(VIVAPINATA_GENERATED_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
if(NOT EXISTS "${VIVAPINATA_GENERATED_SOURCES}")
    set(VIVAPINATA_REXGLUE_EXE "${CMAKE_CURRENT_SOURCE_DIR}/rexglue/${REXSDK_PLATFORM}/bin/rexglue${CMAKE_EXECUTABLE_SUFFIX}")
    if(EXISTS "${VIVAPINATA_REXGLUE_EXE}")
        message(STATUS "[vivapinata] generated/default/sources.cmake missing - running codegen once at configure time")
        execute_process(
            COMMAND "${VIVAPINATA_REXGLUE_EXE}" codegen "${CMAKE_CURRENT_SOURCE_DIR}/vivapinata_manifest.toml"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE VIVAPINATA_CODEGEN_RESULT)
        if(NOT VIVAPINATA_CODEGEN_RESULT EQUAL 0)
            message(WARNING "[vivapinata] bootstrap codegen failed (${VIVAPINATA_CODEGEN_RESULT}); "
                            "the build-time codegen step will report the errors")
        endif()
    endif()
endif()
# Re-run configure whenever codegen rewrites the source list (unit count or
# file names change), so new recomp units are compiled and linked.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VIVAPINATA_GENERATED_SOURCES}")

include(generated/rexglue.cmake)

# Host sources. game_fixes.h (all guest overrides) is included from main.cpp
# exactly once - it defines strong symbols and must live in a single TU.
set(VIVAPINATA_SOURCES
    src/main.cpp
)

if(WIN32)
    list(APPEND VIVAPINATA_SOURCES src/vivapinata.rc)
    add_executable(vivapinata WIN32 ${VIVAPINATA_SOURCES})
else()
    add_executable(vivapinata ${VIVAPINATA_SOURCES})
endif()

# Links rex::runtime, adds the generated OBJECT library vivapinata_recomp,
# wires the vivapinata_codegen dependency and loads the xenos GPU plugin.
rexglue_setup_target(vivapinata GPU_PLUGINS xenos)

# debug_tools.h uses plain fopen for its log files; silence the UCRT
# "consider fopen_s" deprecation warning on the host TU.
target_compile_definitions(vivapinata PRIVATE _CRT_SECURE_NO_WARNINGS)

# SDL reads this file (name from settings/mapping.toml hid_mappings_file)
# next to the exe to recognise extra gamepads.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/settings/gamecontrollerdb.txt")
    add_custom_command(TARGET vivapinata POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/settings/gamecontrollerdb.txt"
                "$<TARGET_FILE_DIR:vivapinata>/gamecontrollerdb.txt"
        COMMENT "Copying gamecontrollerdb.txt next to vivapinata.exe")
endif()
