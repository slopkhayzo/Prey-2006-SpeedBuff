cmake_minimum_required(VERSION 3.21)

project(PreySpeedBuff VERSION 1.0.0 LANGUAGES CXX)

set(PREYSPEEDBUFF_PACKAGE_VERSION "${PROJECT_VERSION}")

if(NOT WIN32)
    message(FATAL_ERROR "PreySpeedBuff targets the Windows retail build of Prey 1.4")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "PreySpeedBuff must be configured with a 32-bit (x86) toolchain")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The plugin is dropped into a game folder, so it must not need a VC++ runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(PREYSPEEDBUFF_RETAIL_GAME_DLL "none" CACHE FILEPATH
    "Retail base/gamex86.dll for the retail tests, which are skipped without it")

configure_file(
    cmake/preyspeedbuff_version.h.in
    "${CMAKE_BINARY_DIR}/generated/preyspeedbuff_version.h"
    @ONLY
)

add_library(PreySpeedBuffCommon STATIC
    src/common/config.cpp
    src/common/game_layout.cpp
)

target_include_directories(PreySpeedBuffCommon PUBLIC
    src/common
)

add_library(PreySpeedBuff SHARED
    src/hook/dllmain.cpp
)

target_include_directories(PreySpeedBuff PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
)

add_executable(PreySpeedBuffConfigTest
    src/config_test/main.cpp
)

add_executable(PreySpeedBuffRetailTest
    src/retail_test/main.cpp
)

target_link_libraries(PreySpeedBuff PRIVATE PreySpeedBuffCommon)
target_link_libraries(PreySpeedBuffConfigTest PRIVATE PreySpeedBuffCommon)
target_link_libraries(PreySpeedBuffRetailTest PRIVATE PreySpeedBuffCommon)

foreach(_target IN ITEMS PreySpeedBuffCommon PreySpeedBuff
                         PreySpeedBuffConfigTest PreySpeedBuffRetailTest)
    target_compile_definitions(${_target} PRIVATE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
    )
    target_compile_options(${_target} PRIVATE
        /W4
        /permissive-
        /EHsc
    )
endforeach()

# The test maps the retail DLL at its preferred or a relocated base; the fixed
# image base keeps this executable out of its way.
target_link_options(PreySpeedBuffRetailTest PRIVATE /DYNAMICBASE:NO /BASE:0x00400000)

set_target_properties(PreySpeedBuff PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    PREFIX ""
    OUTPUT_NAME "PreySpeedBuff"
    SUFFIX ".asi"
)

set_target_properties(PreySpeedBuffConfigTest PreySpeedBuffRetailTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS PreySpeedBuff
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)

install(FILES
    PreySpeedBuff.ini
    DESTINATION .
)
install(FILES packaging/PreySpeedBuff-README.txt DESTINATION .)
install(FILES LICENSE DESTINATION . RENAME PreySpeedBuff-LICENSE.txt)

configure_file(
    cmake/GenerateReleaseManifest.cmake.in
    "${CMAKE_BINARY_DIR}/GenerateReleaseManifest.cmake"
    @ONLY
)
install(SCRIPT "${CMAKE_BINARY_DIR}/GenerateReleaseManifest.cmake")

set(PREYSPEEDBUFF_STAGE_DIR
    "${CMAKE_BINARY_DIR}/release/PreySpeedBuff-${PREYSPEEDBUFF_PACKAGE_VERSION}-windows-x86")

add_custom_target(PreySpeedBuffRelease
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PREYSPEEDBUFF_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
            --prefix "${PREYSPEEDBUFF_STAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}"
            -DPACKAGE_DIR=${PREYSPEEDBUFF_STAGE_DIR}
            -DEXPECTED_VERSION=${PREYSPEEDBUFF_PACKAGE_VERSION}
            -P "${CMAKE_SOURCE_DIR}/cmake/VerifyRelease.cmake"
    DEPENDS PreySpeedBuff
    COMMENT "Staging and verifying PreySpeedBuff ${PREYSPEEDBUFF_PACKAGE_VERSION}"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME PreySpeedBuffConfigValidation
        COMMAND PreySpeedBuffConfigTest "${CMAKE_SOURCE_DIR}/PreySpeedBuff.ini"
    )
    foreach(_scenario IN ITEMS override relocated late partial disabled allgame
                               invalid altered reload unload)
        add_test(NAME PreySpeedBuffRetail_${_scenario}
            COMMAND PreySpeedBuffRetailTest
                    "$<TARGET_FILE:PreySpeedBuff>"
                    "${PREYSPEEDBUFF_RETAIL_GAME_DLL}"
                    ${_scenario}
        )
        set_tests_properties(PreySpeedBuffRetail_${_scenario} PROPERTIES
            SKIP_RETURN_CODE 77
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "PreySpeedBuff")
set(CPACK_PACKAGE_VENDOR "PreySpeedBuff contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Configurable player movement speed for the Steam release of Prey 1.4")
set(CPACK_PACKAGE_VERSION "${PREYSPEEDBUFF_PACKAGE_VERSION}")
set(PREYSPEEDBUFF_BINARY_PACKAGE_FILE_NAME
    "PreySpeedBuff-${PREYSPEEDBUFF_PACKAGE_VERSION}-windows-x86")
set(CPACK_PACKAGE_FILE_NAME "${PREYSPEEDBUFF_BINARY_PACKAGE_FILE_NAME}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/release")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(PREYSPEEDBUFF_ARCHIVE
    "${CPACK_PACKAGE_DIRECTORY}/${PREYSPEEDBUFF_BINARY_PACKAGE_FILE_NAME}.zip")
include(CPack)

add_custom_target(PreySpeedBuffPackage
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    COMMAND "${CMAKE_COMMAND}"
            -DARCHIVE=${PREYSPEEDBUFF_ARCHIVE}
            -DEXPECTED_VERSION=${PREYSPEEDBUFF_PACKAGE_VERSION}
            -DVERIFY_SCRIPT=${CMAKE_SOURCE_DIR}/cmake/VerifyRelease.cmake
            -P "${CMAKE_SOURCE_DIR}/cmake/VerifyReleaseArchive.cmake"
    DEPENDS PreySpeedBuffRelease
    COMMENT "Creating and verifying ${PREYSPEEDBUFF_BINARY_PACKAGE_FILE_NAME}.zip"
    VERBATIM
)

set(PREYSPEEDBUFF_LOADER_BUNDLE_DIR
    "${CMAKE_BINARY_DIR}/release/PreySpeedBuff-${PREYSPEEDBUFF_PACKAGE_VERSION}-windows-x86-with-loader")
set(PREYSPEEDBUFF_LOADER_BUNDLE_ARCHIVE
    "${PREYSPEEDBUFF_LOADER_BUNDLE_DIR}.zip")
add_custom_target(PreySpeedBuffBundlePackage
    COMMAND "${CMAKE_COMMAND}"
            -DPLUGIN_DIR=${PREYSPEEDBUFF_STAGE_DIR}
            -DBUNDLE_DIR=${PREYSPEEDBUFF_LOADER_BUNDLE_DIR}
            -DARCHIVE=${PREYSPEEDBUFF_LOADER_BUNDLE_ARCHIVE}
            -DLOADER_BINARY=${CMAKE_SOURCE_DIR}/vendor/ultimate-asi-loader/dinput.dll
            -DLOADER_CONFIG=${CMAKE_SOURCE_DIR}/packaging/dinput.ini
            -DLOADER_LICENSE=${CMAKE_SOURCE_DIR}/vendor/ultimate-asi-loader/LICENSE.txt
            -DLOADER_NOTICE=${CMAKE_SOURCE_DIR}/vendor/ultimate-asi-loader/NOTICE.txt
            -DEXPECTED_VERSION=${PREYSPEEDBUFF_PACKAGE_VERSION}
            -P "${CMAKE_SOURCE_DIR}/cmake/CreateLoaderBundle.cmake"
    DEPENDS PreySpeedBuffRelease
    COMMENT "Creating loader-inclusive ${PREYSPEEDBUFF_BINARY_PACKAGE_FILE_NAME} bundle"
    VERBATIM
)
