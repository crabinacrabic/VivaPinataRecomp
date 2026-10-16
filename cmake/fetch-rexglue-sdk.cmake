# Downloads and extracts the ReXGlue SDK release archive for the current
# platform into rexglue/<platform>. If rexglue/<platform> already holds the
# pinned version (REXGLUE_VERSION_STRING in include/rex/version.h), nothing is
# done. A folder with another version is moved to
# rexglue/<platform>.old-<version> (nothing is deleted), then the pinned
# release is downloaded.
#
# Usage:
#   cmake -DREXSDK_VERSION=<ver> -DREXSDK_PLATFORM=<platform>
#         -DREXSDK_RELEASE_URL=<release download URL without the file name>
#         -P fetch-rexglue-sdk.cmake
#
# REXSDK_PLATFORM must be one of: win-amd64, linux-amd64, linux-arm64,
# mac-amd64, mac-arm64. (No win-arm64 release is published.)

foreach(var REXSDK_VERSION REXSDK_PLATFORM REXSDK_RELEASE_URL)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} must be passed via -D${var}=<value>")
    endif()
endforeach()

set(REXSDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../rexglue")
if(NOT EXISTS "${REXSDK_ROOT}")
    file(MAKE_DIRECTORY "${REXSDK_ROOT}")
endif()

set(REXSDK_DIR "${REXSDK_ROOT}/${REXSDK_PLATFORM}")

if(EXISTS "${REXSDK_DIR}")
    set(found_version "")
    if(EXISTS "${REXSDK_DIR}/include/rex/version.h")
        file(STRINGS "${REXSDK_DIR}/include/rex/version.h" version_line
             REGEX "^#define REXGLUE_VERSION_STRING ")
        string(REGEX REPLACE "^#define REXGLUE_VERSION_STRING \"([^\"]*)\".*" "\\1"
               found_version "${version_line}")
    endif()
    if(found_version STREQUAL REXSDK_VERSION)
        message(STATUS "[fetch-rexglue-sdk] ${REXSDK_VERSION} already present at ${REXSDK_DIR}; skipping")
        return()
    endif()
    if(found_version STREQUAL "")
        set(found_version "unknown")
    endif()
    string(TIMESTAMP now "%Y%m%d-%H%M%S")
    set(old_dir "${REXSDK_DIR}.old-${found_version}-${now}")
    file(RENAME "${REXSDK_DIR}" "${old_dir}")
    message(STATUS "[fetch-rexglue-sdk] ${REXSDK_DIR} holds ${found_version}, the project needs "
                   "${REXSDK_VERSION}: moved it to ${old_dir}")
endif()

set(REXSDK_ZIP_NAME "rexglue-sdk-${REXSDK_VERSION}-${REXSDK_PLATFORM}.zip")
set(REXSDK_ZIP_URL "${REXSDK_RELEASE_URL}/${REXSDK_ZIP_NAME}")
set(REXSDK_ZIP_PATH "${REXSDK_ROOT}/${REXSDK_ZIP_NAME}")

message(STATUS "[fetch-rexglue-sdk] Downloading ${REXSDK_ZIP_URL}")
file(DOWNLOAD "${REXSDK_ZIP_URL}" "${REXSDK_ZIP_PATH}"
    STATUS download_status)
list(GET download_status 0 result)
list(GET download_status 1 error_message)
if(NOT result EQUAL 0)
    file(REMOVE "${REXSDK_ZIP_PATH}")
    message(FATAL_ERROR "[fetch-rexglue-sdk] download failed: ${error_message}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xzf "${REXSDK_ZIP_PATH}"
    WORKING_DIRECTORY "${REXSDK_ROOT}"
    RESULT_VARIABLE extract_result
    ERROR_VARIABLE extract_error
)
file(REMOVE "${REXSDK_ZIP_PATH}")
if(NOT extract_result EQUAL 0)
    message(FATAL_ERROR "[fetch-rexglue-sdk] extraction failed: ${extract_error}")
endif()

message(STATUS "[fetch-rexglue-sdk] Extracted ${REXSDK_PLATFORM} SDK to ${REXSDK_DIR}")
