cmake_minimum_required(VERSION 3.28)
project(MiaoDesk LANGUAGES CXX)

# MiaoDesk must remain buildable on stock Windows 10/11 without asking users to
# enable the Win32 long-path policy. Keep generated object paths conservative;
# this is a build-time safeguard, not a runtime dependency on long-path support.
set(CMAKE_OBJECT_PATH_MAX 180)

# Never allow a true in-source build. Developer/CI presets use short external
# build roots (for example C:/b/MiaoDesk/x64) so compiler-generated paths do not
# inherit the repository's full nesting depth.
file(REAL_PATH "${CMAKE_SOURCE_DIR}" MIAODESK_SOURCE_REAL)
file(REAL_PATH "${CMAKE_BINARY_DIR}" MIAODESK_BINARY_REAL)
if(MIAODESK_SOURCE_REAL STREQUAL MIAODESK_BINARY_REAL)
    message(FATAL_ERROR
        "In-source builds are not supported. Use an external short build path, e.g. "
        "cmake -S . -B C:/b/MiaoDesk/x64 -A x64")
endif()

include(GNUInstallDirs)
add_subdirectory(src/native)
