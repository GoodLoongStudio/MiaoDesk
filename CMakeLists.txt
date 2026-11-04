cmake_minimum_required(VERSION 3.28)
project(WhaleDesK LANGUAGES CXX)

# Ship the MSVC runtime inside each native executable. Both the standalone
# Store installer and the MSIX payload must start on a clean Windows machine
# without depending on a separately installed Visual C++ Redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# WhaleDesK must remain buildable on stock Windows 10/11 without asking users to
# enable the Win32 long-path policy. Keep generated object paths conservative;
# this is a build-time safeguard, not a runtime dependency on long-path support.
set(CMAKE_OBJECT_PATH_MAX 180)

# Reject every build directory located inside the repository, not only a literal
# in-source build. Developer/CI presets use short external roots such as
# C:/b/WhaleDesK/x64 so compiler-generated paths never inherit source nesting.
file(REAL_PATH "${CMAKE_SOURCE_DIR}" WHALEDESK_SOURCE_REAL)
file(REAL_PATH "${CMAKE_BINARY_DIR}" WHALEDESK_BINARY_REAL)
cmake_path(IS_PREFIX WHALEDESK_SOURCE_REAL "${WHALEDESK_BINARY_REAL}" NORMALIZE WHALEDESK_BINARY_IN_SOURCE)
if(WHALEDESK_BINARY_IN_SOURCE)
    message(FATAL_ERROR
        "Build directories inside the WhaleDesK source tree are not supported. "
        "Use an external short build path, e.g. cmake -S . -B C:/b/WhaleDesK/x64 -A x64")
endif()

include(GNUInstallDirs)
add_subdirectory(src)

# P0-6's creator surface belongs to the main WhaleDesK process because it borrows the
# canonical L3/Pi runtime. Keep the translation unit owned by that target so Repo
# Hygiene can prove it is compiled and so no second AI stack appears in Wallpaper.
target_sources(WhaleDesK PRIVATE
    "${CMAKE_SOURCE_DIR}/src/ui/ai/ContentCreatorDialog.cpp"
)

# End-user staging is deliberately shallow. Runtime discovery in WhaleDesK is
# relative to the executable directory, so the three production executables
# stay at the install root. Do not install repository runtime SDK trees,
# FetchContent sources, vcpkg buildtrees, or compiler intermediates.
install(TARGETS
    WhaleDesK
    WhaleDesKWallpaper
    WhaleDesKHarness
    RUNTIME DESTINATION "."
)

install(FILES
    "${CMAKE_SOURCE_DIR}/assets/app/WhaleWhale.ico"
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

# Built-in widgets use the same external Whale Content Package contract as user
# content. Keep them as real .mdwidget directories in the staged product so the
# host can dogfood package discovery instead of embedding a second private
# format in native code.
install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/assets/widgets/"
    DESTINATION "Widgets"
    PATTERN "README.md" EXCLUDE
)

# Content-creation skills ship next to the executable. The native tool
# content_skill_get resolves them relative to the executable directory
# (see paths::ExecutableDirectory), so the staged layout must stay
# <install>/skills/<name>/SKILL.md. Any rename here silently breaks AI-driven
# content creation, and packaging/windows/stage.ps1 asserts the set.
install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/skills/"
    DESTINATION "skills"
    FILES_MATCHING PATTERN "*.md"
)
