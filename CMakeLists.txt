cmake_minimum_required(VERSION 3.28)
project(MiaoDesk LANGUAGES CXX)

# Ship the MSVC runtime inside each native executable. Both the standalone
# Store installer and the MSIX payload must start on a clean Windows machine
# without depending on a separately installed Visual C++ Redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MiaoDesk must remain buildable on stock Windows 10/11 without asking users to
# enable the Win32 long-path policy. Keep generated object paths conservative;
# this is a build-time safeguard, not a runtime dependency on long-path support.
set(CMAKE_OBJECT_PATH_MAX 180)

# Reject every build directory located inside the repository, not only a literal
# in-source build. Developer/CI presets use short external roots such as
# C:/b/MiaoDesk/x64 so compiler-generated paths never inherit source nesting.
file(REAL_PATH "${CMAKE_SOURCE_DIR}" MIAODESK_SOURCE_REAL)
file(REAL_PATH "${CMAKE_BINARY_DIR}" MIAODESK_BINARY_REAL)
cmake_path(IS_PREFIX MIAODESK_SOURCE_REAL "${MIAODESK_BINARY_REAL}" NORMALIZE MIAODESK_BINARY_IN_SOURCE)
if(MIAODESK_BINARY_IN_SOURCE)
    message(FATAL_ERROR
        "Build directories inside the MiaoDesk source tree are not supported. "
        "Use an external short build path, e.g. cmake -S . -B C:/b/MiaoDesk/x64 -A x64")
endif()

include(GNUInstallDirs)
add_subdirectory(src)

# Development/quick-build binaries must exercise the same Content package
# discovery contract as installed products. Keep official .mdwidget packages
# beside MiaoDeskWallpaper.exe after every build instead of making Content-only
# widget creation mysteriously work only after `cmake --install`.
add_custom_command(TARGET MiaoDeskWallpaper POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/widgets"
        "$<TARGET_FILE_DIR:MiaoDeskWallpaper>/Widgets"
    COMMENT "Staging built-in MiaoDesk widget packages"
)

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
    "${CMAKE_SOURCE_DIR}/assets/app/MiaoMiao.ico"
    DESTINATION "Assets"
)

install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
    DESTINATION "."
)

install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/config/"
    DESTINATION "Config"
    FILES_MATCHING PATTERN "*.ini"
)

# Packaged wallpaper scenes contain only runtime manifests and assets. Their
# maximum product-owned nesting remains shallow and portable.
install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/assets/wallpapers/"
    DESTINATION "Wallpapers"
    PATTERN "README.md" EXCLUDE
)

# Built-in widgets use the same external Miao Content Package contract as user
# content. Keep them as real .mdwidget directories in the staged product so the
# host can dogfood package discovery instead of embedding a second private
# format in native code.
install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/assets/widgets/"
    DESTINATION "Widgets"
    PATTERN "README.md" EXCLUDE
)
