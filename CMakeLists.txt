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

# End-user staging is deliberately shallow. Runtime discovery in MiaoDesk is
# relative to the executable directory, so the three production executables
# stay at the install root. Do not install repository runtime SDK trees,
# FetchContent sources, vcpkg buildtrees, or compiler intermediates.
install(TARGETS
    MiaoDesk
    MiaoDeskWallpaper
    MiaoDeskHarness
    RUNTIME DESTINATION "."
)

install(FILES
    "${CMAKE_SOURCE_DIR}/packaging/windows-store/assets/MiaoMiao.ico"
    DESTINATION "Assets"
)

# Packaged wallpaper scenes contain only runtime manifests and assets. Their
# maximum product-owned nesting remains shallow and portable.
install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/assets/wallpapers/"
    DESTINATION "Wallpapers"
    PATTERN "README.md" EXCLUDE
)
