cmake_minimum_required(VERSION 3.22)
project(chronicletwo NONE)

# PLATFORM picks what is built: PS2 builds and verifies the retail executable
# (ps2/CMakeLists.txt), PC builds the x64 port (port/CMakeLists.txt), iOS
# cross-compiles the same PC port code for iOS arm64.
set(PLATFORM PS2 CACHE STRING "Platform to build: PS2, PC or iOS")
set_property(CACHE PLATFORM PROPERTY STRINGS PS2 PC iOS)
if(PLATFORM STREQUAL "PS2")
    add_subdirectory(ps2)
elseif(PLATFORM STREQUAL "PC")
    # Here rather than in port so ctest finds the tests from the top of the build tree.
    enable_testing()
    add_subdirectory(port)
elseif(PLATFORM STREQUAL "iOS")
    # iOS cross-compiles the PC port for iOS arm64 via CMAKE_SYSTEM_NAME=iOS.
    enable_testing()
    add_subdirectory(port)
else()
    message(FATAL_ERROR "PLATFORM must be PS2, PC or iOS, not ${PLATFORM}")
endif()
