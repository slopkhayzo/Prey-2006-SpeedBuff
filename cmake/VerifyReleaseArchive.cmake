cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED ARCHIVE OR NOT EXISTS "${ARCHIVE}")
    message(FATAL_ERROR "ARCHIVE must name a generated PreySpeedBuff ZIP package")
endif()
if(NOT DEFINED EXPECTED_VERSION OR EXPECTED_VERSION STREQUAL "")
    message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()
if(NOT DEFINED VERIFY_SCRIPT OR NOT EXISTS "${VERIFY_SCRIPT}")
    message(FATAL_ERROR "VERIFY_SCRIPT must name VerifyRelease.cmake")
endif()

set(_extract_dir "${ARCHIVE}.verify")
file(REMOVE_RECURSE "${_extract_dir}")
file(MAKE_DIRECTORY "${_extract_dir}")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${_extract_dir}")

set(PACKAGE_DIR "${_extract_dir}")
include("${VERIFY_SCRIPT}")

file(SHA256 "${ARCHIVE}" _archive_sha256)
get_filename_component(_archive_name "${ARCHIVE}" NAME)
file(WRITE "${ARCHIVE}.sha256" "${_archive_sha256}  ${_archive_name}\n")
file(REMOVE_RECURSE "${_extract_dir}")

message(STATUS "Verified archive SHA-256: ${_archive_sha256}")
