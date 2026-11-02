cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PACKAGE_DIR OR NOT IS_DIRECTORY "${PACKAGE_DIR}")
    message(FATAL_ERROR "PACKAGE_DIR must name an installed PreySpeedBuff package")
endif()
if(NOT DEFINED EXPECTED_VERSION OR EXPECTED_VERSION STREQUAL "")
    message(FATAL_ERROR "EXPECTED_VERSION is required")
endif()

set(_payload_files
    "PreySpeedBuff.asi"
    "PreySpeedBuff.ini"
    "PreySpeedBuff-README.txt"
    "PreySpeedBuff-LICENSE.txt"
)
set(_expected_files ${_payload_files} "PreySpeedBuff-release.json")
list(SORT _expected_files)

file(GLOB_RECURSE _actual_paths
    LIST_DIRECTORIES false
    RELATIVE "${PACKAGE_DIR}"
    "${PACKAGE_DIR}/*")
set(_actual_files ${_actual_paths})
list(SORT _actual_files)
if(NOT _actual_files STREQUAL _expected_files)
    message(FATAL_ERROR
        "Release contents differ from the allowlist.\n"
        "Expected: ${_expected_files}\nActual: ${_actual_files}")
endif()

set(_manifest_path "${PACKAGE_DIR}/PreySpeedBuff-release.json")
file(READ "${_manifest_path}" _manifest)
string(JSON _format GET "${_manifest}" manifest_format)
string(JSON _name GET "${_manifest}" name)
string(JSON _version GET "${_manifest}" version)
string(JSON _target GET "${_manifest}" target)
string(JSON _compatibility_policy GET "${_manifest}" compatibility policy)
string(JSON _dll_hash GET "${_manifest}" compatibility tested_baseline_gamex86.dll_sha256)

if(NOT _format EQUAL 2 OR NOT _name STREQUAL "PreySpeedBuff" OR
   NOT _version STREQUAL EXPECTED_VERSION OR NOT _target STREQUAL "windows-x86")
    message(FATAL_ERROR "Release manifest identity is invalid")
endif()
if(NOT _compatibility_policy STREQUAL "prey-1.4-x86-structural-v1" OR
   NOT _dll_hash STREQUAL
       "74d436d376ba144762a28c940d0243135b4f9db8fdd7ee597b9cb5e4277b43c6")
    message(FATAL_ERROR "Release manifest contains an unexpected retail-build identity")
endif()

string(JSON _manifest_file_count LENGTH "${_manifest}" files)
list(LENGTH _payload_files _payload_file_count)
if(NOT _manifest_file_count EQUAL _payload_file_count)
    message(FATAL_ERROR "Release manifest file count is invalid")
endif()

set(_manifest_files "")
math(EXPR _last_file_index "${_manifest_file_count} - 1")
foreach(_index RANGE 0 ${_last_file_index})
    string(JSON _relative_path GET "${_manifest}" files ${_index} path)
    string(JSON _recorded_hash GET "${_manifest}" files ${_index} sha256)
    list(APPEND _manifest_files "${_relative_path}")
    if(NOT EXISTS "${PACKAGE_DIR}/${_relative_path}")
        message(FATAL_ERROR "Manifest entry is missing: ${_relative_path}")
    endif()
    file(SHA256 "${PACKAGE_DIR}/${_relative_path}" _actual_hash)
    if(NOT _actual_hash STREQUAL _recorded_hash)
        message(FATAL_ERROR "SHA-256 mismatch for ${_relative_path}")
    endif()
endforeach()
list(SORT _manifest_files)
set(_sorted_payload_files ${_payload_files})
list(SORT _sorted_payload_files)
if(NOT _manifest_files STREQUAL _sorted_payload_files)
    message(FATAL_ERROR "Release manifest payload list is invalid")
endif()

function(require_x86_pe relative_path)
    set(_path "${PACKAGE_DIR}/${relative_path}")
    file(READ "${_path}" _dos_offset_hex OFFSET 60 LIMIT 4 HEX)
    string(LENGTH "${_dos_offset_hex}" _dos_offset_length)
    if(NOT _dos_offset_length EQUAL 8 OR
       _dos_offset_hex MATCHES "[^0-9a-fA-F]")
        message(FATAL_ERROR "Invalid DOS header in ${relative_path}")
    endif()
    string(SUBSTRING "${_dos_offset_hex}" 0 2 _b0)
    string(SUBSTRING "${_dos_offset_hex}" 2 2 _b1)
    string(SUBSTRING "${_dos_offset_hex}" 4 2 _b2)
    string(SUBSTRING "${_dos_offset_hex}" 6 2 _b3)
    math(EXPR _pe_offset "0x${_b3}${_b2}${_b1}${_b0}")
    file(READ "${_path}" _pe_header OFFSET ${_pe_offset} LIMIT 6 HEX)
    string(TOLOWER "${_pe_header}" _pe_header)
    if(NOT _pe_header STREQUAL "504500004c01")
        message(FATAL_ERROR "${relative_path} is not an x86 PE image")
    endif()
endfunction()

require_x86_pe("PreySpeedBuff.asi")

message(STATUS
    "Verified PreySpeedBuff ${EXPECTED_VERSION}: exact payload, SHA-256 manifest, "
    "x86 ASI image, and structural compatibility policy")
