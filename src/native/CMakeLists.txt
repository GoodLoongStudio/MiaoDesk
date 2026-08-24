include(FetchContent)

set(TURINGDESK_WEBVIEW2_VERSION "1.0.4129.50")

if (CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
    set(WEBVIEW2_SDK_ROOT "${CMAKE_SOURCE_DIR}/runtime/arm64/webview2-sdk")
    set(WEBVIEW2_NATIVE_ARCH "arm64")
else()
    FetchContent_Declare(
        webview2sdk
        URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${TURINGDESK_WEBVIEW2_VERSION}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(webview2sdk)
    set(WEBVIEW2_SDK_ROOT "${webview2sdk_SOURCE_DIR}")
    if (CMAKE_VS_PLATFORM_NAME STREQUAL "Win32")
        set(WEBVIEW2_NATIVE_ARCH "x86")
    else()
        set(WEBVIEW2_NATIVE_ARCH "x64")
    endif()
endif()

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_SDK_ROOT}/build/native/include")
set(WEBVIEW2_LOADER_STATIC "${WEBVIEW2_SDK_ROOT}/build/native/${WEBVIEW2_NATIVE_ARCH}/WebView2LoaderStatic.lib")
if (NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h" OR NOT EXISTS "${WEBVIEW2_LOADER_STATIC}")
    message(FATAL_ERROR
        "WebView2 SDK ${TURINGDESK_WEBVIEW2_VERSION} is incomplete at ${WEBVIEW2_SDK_ROOT} for ${WEBVIEW2_NATIVE_ARCH}.")
endif()

add_executable(TuringDesk WIN32
    src/main.cpp
    src/SearchWindow.cpp
    src/SettingsCenterWindow.cpp
    src/ModelSettingsWindow.cpp
    src/L3CliWindow.cpp
    src/PiRuntime.cpp
    src/PiNativeToolsExtension.cpp
    src/NativeTools.cpp
    src/NativeToolIsolation.cpp
    src/DesktopControlService.cpp
    src/DesktopWidgetStore.cpp
    src/DesktopWidgetTools.cpp
    src/WallpaperPackage.cpp
    src/AppSearch.cpp
    src/GozSearch.cpp
    src/L3Agent.cpp
    src/L3PersistenceSelfTest.cpp
    src/HarnessProcessManager.cpp
    src/HarnessSettingsBridge.cpp
)

set_source_files_properties(src/NativeTools.cpp PROPERTIES
    COMPILE_DEFINITIONS "TURINGDESK_NATIVE_TOOLS_IMPL"
)

add_executable(TuringDeskWallpaper WIN32
    src/WallpaperEntry.cpp
    src/WallpaperEngine.cpp
    src/DesktopShellHost.cpp
    src/DesktopControlService.cpp
    src/WallpaperMonitorLayout.cpp
    src/WallpaperMonitorAssignments.cpp
    src/WallpaperIndependentLayout.cpp
    src/IndependentWallpaperHost.cpp
    src/WebWallpaperHost.cpp
    src/WebDesktopSurfaceChild.cpp
    src/WallpaperWebRuntimeCoordinator.cpp
    src/WallpaperScaling.cpp
    src/WallpaperPerformancePolicy.cpp
    src/WallpaperApplicationRules.cpp
    src/WallpaperApplicationRulesWindow.cpp
    src/WallpaperAutomation.cpp
    src/WallpaperAutomationWindow.cpp
    src/WallpaperLibrary.cpp
    src/WallpaperLibraryWeb.cpp
    src/WallpaperLibraryWindow.cpp
    src/DesktopWidgetStore.cpp
    src/WallpaperPackage.cpp
    src/VideoWallpaperPlayer.cpp
    src/VideoWallpaperSet.cpp
    src/AppSearch.cpp
    src/GozSearch.cpp
    src/L3Agent.cpp
)

set_source_files_properties(src/WallpaperEngine.cpp PROPERTIES
    COMPILE_DEFINITIONS "wWinMain=TuringDeskWallpaperMain"
)

add_executable(TuringDeskHarness WIN32
    src/HarnessHost.cpp
    src/HarnessProcessManager.cpp
    src/HarnessSettingsBridge.cpp
    src/HarnessBundledRuntimeBootstrap.cpp
)

set(TURINGDESK_L3_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-l3-runtime-contract.ps1")
if (NOT EXISTS "${TURINGDESK_L3_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing L3 runtime contract guard: ${TURINGDESK_L3_CONTRACT_SCRIPT}")
endif()
add_custom_target(TuringDeskL3ContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${TURINGDESK_L3_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying Pi-first L3 runtime contract"
    VERBATIM
)
add_dependencies(TuringDesk TuringDeskL3ContractCheck)

set(TURINGDESK_DOMAIN_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-desktop-domain-contract.ps1")
if (NOT EXISTS "${TURINGDESK_DOMAIN_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing desktop domain contract guard: ${TURINGDESK_DOMAIN_CONTRACT_SCRIPT}")
endif()
add_custom_target(TuringDeskDesktopDomainContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${TURINGDESK_DOMAIN_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying desktop domain boundaries"
    VERBATIM
)
add_dependencies(TuringDesk TuringDeskDesktopDomainContractCheck)
add_dependencies(TuringDeskWallpaper TuringDeskDesktopDomainContractCheck)

set(TURINGDESK_RUNTIME_LOG_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-runtime-log-paths.ps1")
if (NOT EXISTS "${TURINGDESK_RUNTIME_LOG_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing runtime log contract guard: ${TURINGDESK_RUNTIME_LOG_CONTRACT_SCRIPT}")
endif()
add_custom_target(TuringDeskRuntimeLogContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${TURINGDESK_RUNTIME_LOG_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying Desktop runtime log routing"
    VERBATIM
)
add_dependencies(TuringDesk TuringDeskRuntimeLogContractCheck)
add_dependencies(TuringDeskWallpaper TuringDeskRuntimeLogContractCheck)
add_dependencies(TuringDeskHarness TuringDeskRuntimeLogContractCheck)

target_include_directories(TuringDesk PRIVATE include)
target_compile_features(TuringDesk PRIVATE cxx_std_23)
target_compile_definitions(TuringDesk PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)

target_include_directories(TuringDeskWallpaper PRIVATE include)
target_include_directories(TuringDeskWallpaper SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_compile_features(TuringDeskWallpaper PRIVATE cxx_std_23)
target_compile_definitions(TuringDeskWallpaper PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)

target_include_directories(TuringDeskHarness PRIVATE include)
target_include_directories(TuringDeskHarness SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_compile_features(TuringDeskHarness PRIVATE cxx_std_23)
target_compile_definitions(TuringDeskHarness PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)

if (MSVC)
    target_compile_options(TuringDesk PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(TuringDeskWallpaper PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(TuringDeskHarness PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

target_link_libraries(TuringDesk PRIVATE
    user32
    gdi32
    shell32
    ole32
    oleaut32
    advapi32
    comctl32
    d2d1
    dwrite
    dwmapi
    winhttp
)

target_link_libraries(TuringDeskWallpaper PRIVATE
    user32
    gdi32
    shell32
    ole32
    advapi32
    comctl32
    d2d1
    windowscodecs
    comdlg32
    wtsapi32
    mf
    mfplat
    mfplay
    mfuuid
    shlwapi
    version
    winhttp
    "${WEBVIEW2_LOADER_STATIC}"
)

target_link_libraries(TuringDeskHarness PRIVATE
    user32
    gdi32
    shell32
    ole32
    advapi32
    winhttp
    shlwapi
    version
    "${WEBVIEW2_LOADER_STATIC}"
)
