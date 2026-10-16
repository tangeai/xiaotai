# The cross-platform dispatcher reserves one identity for each build attempt.
# Reconfiguring CMake must reuse it instead of consuming another build number.
set(xiaotai_build_counter "${CMAKE_CURRENT_SOURCE_DIR}/BUILD_NUMBER")
file(STRINGS "${xiaotai_build_counter}" xiaotai_build_number LIMIT_COUNT 1)
if(NOT xiaotai_build_number MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "Build number is not allocated; build with tools/build.py")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${xiaotai_build_counter}")
set(PROJECT_VER "${PROJECT_VER}+build.${xiaotai_build_number}")
string(LENGTH "${PROJECT_VER}" xiaotai_version_length)
if(xiaotai_version_length GREATER 31)
    message(FATAL_ERROR "Firmware identity exceeds ESP-IDF app descriptor capacity")
endif()
