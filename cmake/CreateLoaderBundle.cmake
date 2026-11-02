cmake_minimum_required(VERSION 3.21)

foreach(_required IN ITEMS PLUGIN_DIR BUNDLE_DIR ARCHIVE LOADER_BINARY
                           LOADER_CONFIG LOADER_LICENSE LOADER_NOTICE
                           EXPECTED_VERSION)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${PLUGIN_DIR}")
    message(FATAL_ERROR "The verified plugin package is missing: ${PLUGIN_DIR}")
endif()

set(_loader_sha256
    "ec2f4824eca58dd40f425756a4a7cec77b8e381f21d11d7c846ec4b339b617ab")
file(SHA256 "${LOADER_BINARY}" _actual_loader_sha256)
if(NOT _actual_loader_sha256 STREQUAL _loader_sha256)
    message(FATAL_ERROR "The pinned Ultimate ASI Loader binary has changed")
endif()

file(REMOVE_RECURSE "${BUNDLE_DIR}")
file(MAKE_DIRECTORY "${BUNDLE_DIR}")
file(COPY "${PLUGIN_DIR}/" DESTINATION "${BUNDLE_DIR}")
file(COPY "${LOADER_BINARY}" DESTINATION "${BUNDLE_DIR}")
file(COPY "${LOADER_CONFIG}" DESTINATION "${BUNDLE_DIR}")
file(COPY "${LOADER_LICENSE}" DESTINATION "${BUNDLE_DIR}")
file(RENAME "${BUNDLE_DIR}/LICENSE.txt"
            "${BUNDLE_DIR}/Ultimate-ASI-Loader-LICENSE.txt")
file(COPY "${LOADER_NOTICE}" DESTINATION "${BUNDLE_DIR}")
file(RENAME "${BUNDLE_DIR}/NOTICE.txt"
            "${BUNDLE_DIR}/Ultimate-ASI-Loader-NOTICE.txt")
file(WRITE "${BUNDLE_DIR}/Ultimate-ASI-Loader-release.json"
"{\n"
"  \"name\": \"Ultimate ASI Loader\",\n"
"  \"version\": \"9.7.4\",\n"
"  \"upstream\": \"https://github.com/ThirteenAG/Ultimate-ASI-Loader\",\n"
"  \"file\": \"dinput.dll\",\n"
"  \"sha256\": \"${_loader_sha256}\"\n"
"}\n")

set(_expected_files
    "PreySpeedBuff.asi"
    "PreySpeedBuff.ini"
    "PreySpeedBuff-LICENSE.txt"
    "PreySpeedBuff-README.txt"
    "PreySpeedBuff-release.json"
    "Ultimate-ASI-Loader-LICENSE.txt"
    "Ultimate-ASI-Loader-NOTICE.txt"
    "Ultimate-ASI-Loader-release.json"
    "dinput.dll"
    "dinput.ini"
)
file(GLOB_RECURSE _actual_files LIST_DIRECTORIES false
     RELATIVE "${BUNDLE_DIR}" "${BUNDLE_DIR}/*")
list(SORT _expected_files)
list(SORT _actual_files)
if(NOT _actual_files STREQUAL _expected_files)
    message(FATAL_ERROR
        "Loader bundle contents differ from the allowlist.\n"
        "Expected: ${_expected_files}\nActual: ${_actual_files}")
endif()

file(READ "${BUNDLE_DIR}/dinput.dll" _dos_offset_hex OFFSET 60 LIMIT 4 HEX)
string(SUBSTRING "${_dos_offset_hex}" 0 2 _b0)
string(SUBSTRING "${_dos_offset_hex}" 2 2 _b1)
string(SUBSTRING "${_dos_offset_hex}" 4 2 _b2)
string(SUBSTRING "${_dos_offset_hex}" 6 2 _b3)
math(EXPR _pe_offset "0x${_b3}${_b2}${_b1}${_b0}")
file(READ "${BUNDLE_DIR}/dinput.dll" _pe_header
     OFFSET ${_pe_offset} LIMIT 6 HEX)
string(TOLOWER "${_pe_header}" _pe_header)
if(NOT _pe_header STREQUAL "504500004c01")
    message(FATAL_ERROR "The bundled Ultimate ASI Loader is not an x86 PE image")
endif()

get_filename_component(_archive_directory "${ARCHIVE}" DIRECTORY)
file(MAKE_DIRECTORY "${_archive_directory}")
file(REMOVE "${ARCHIVE}" "${ARCHIVE}.sha256")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${ARCHIVE}" --format=zip -- .
    WORKING_DIRECTORY "${BUNDLE_DIR}"
    RESULT_VARIABLE _archive_result
)
if(NOT _archive_result EQUAL 0 OR NOT EXISTS "${ARCHIVE}")
    message(FATAL_ERROR "Could not create the loader-inclusive archive")
endif()
set(_archive_check_dir "${BUNDLE_DIR}-archive-check")
file(REMOVE_RECURSE "${_archive_check_dir}")
file(MAKE_DIRECTORY "${_archive_check_dir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xf "${ARCHIVE}"
    WORKING_DIRECTORY "${_archive_check_dir}"
    RESULT_VARIABLE _extract_result
)
if(NOT _extract_result EQUAL 0)
    message(FATAL_ERROR "Could not extract the loader-inclusive archive")
endif()
file(GLOB_RECURSE _archive_files LIST_DIRECTORIES false
     RELATIVE "${_archive_check_dir}" "${_archive_check_dir}/*")
list(SORT _archive_files)
if(NOT _archive_files STREQUAL _expected_files)
    message(FATAL_ERROR "The extracted loader bundle differs from its allowlist")
endif()
file(SHA256 "${_archive_check_dir}/dinput.dll" _archive_loader_sha256)
if(NOT _archive_loader_sha256 STREQUAL _loader_sha256)
    message(FATAL_ERROR "The archived Ultimate ASI Loader checksum is invalid")
endif()
file(REMOVE_RECURSE "${_archive_check_dir}")
file(SHA256 "${ARCHIVE}" _archive_sha256)
get_filename_component(_archive_name "${ARCHIVE}" NAME)
file(WRITE "${ARCHIVE}.sha256" "${_archive_sha256}  ${_archive_name}\n")
message(STATUS
    "Created PreySpeedBuff ${EXPECTED_VERSION} bundle with pinned Ultimate ASI Loader v9.7.4")
