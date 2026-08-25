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

# Keep source ownership visible in the build graph. Implementations are grouped
# by product/domain instead of being flattened under src/native/src.
set(TURINGDESK_APP_SOURCES
    src/app/main.cpp
    src/ui/search/SearchWindow.cpp
    src/ui/settings/SettingsCenterWindow.cpp
    src/ui/settings/ModelSettingsWindow.cpp
    src/ui/ai/L3CliWindow.cpp

    src/ai/pi/PiRuntime.cpp
    src/ai/pi/PiNativeToolsExtension.cpp
    src/ai/tools/NativeTools.cpp
    src/ai/tools/NativeToolIsolation.cpp
    src/ai/tools/DesktopWidgetTools.cpp
    src/ai/agent/L3Agent.cpp
    src/ai/agent/L3PersistenceSelfTest.cpp

    src/desktop/control/DesktopControlService.cpp
    src/desktop/wallpaper/WallpaperService.cpp
    src/desktop/widgets/WidgetService.cpp
    src/desktop/widgets/DesktopWidgetStore.cpp
    src/desktop/wallpaper/library/WallpaperPackage.cpp
    src/desktop/wallpaper/library/WallpaperLibrary.cpp
    src/desktop/wallpaper/library/WallpaperLibraryWeb.cpp
    src/desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp
    src/desktop/wallpaper/monitor/WallpaperMonitorAssignments.cpp

    src/search/AppSearch.cpp
    src/search/GozSearch.cpp
    src/harness/HarnessProcessManager.cpp
    src/harness/HarnessSettingsBridge.cpp
)

set(TURINGDESK_WALLPAPER_SOURCES
    src/desktop/wallpaper/runtime/WallpaperEntry.cpp
    src/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp

    src/desktop/shell/DesktopShellHost.cpp
    src/desktop/shell/DesktopShellDiagnostics.cpp
    src/desktop/shell/DesktopShellSurfaceStack.cpp

    src/desktop/control/DesktopControlService.cpp
    src/desktop/wallpaper/WallpaperService.cpp
    src/desktop/widgets/WidgetService.cpp
    src/desktop/widgets/DesktopWidgetStore.cpp
    src/desktop/widgets/DesktopWidgetController.cpp
    src/ui/widgets/DesktopWidgetUiAdapter.cpp

    src/desktop/automation/AutomationService.cpp
    src/desktop/automation/WallpaperAutomation.cpp
    src/desktop/automation/WallpaperApplicationRules.cpp
    src/ui/automation/AutomationUiAdapter.cpp
    src/ui/automation/WallpaperAutomationWindowProduction.cpp
    src/ui/automation/WallpaperApplicationRulesWindow.cpp

    src/desktop/performance/PerformanceService.cpp
    src/desktop/performance/WallpaperPerformancePolicy.cpp
    src/ui/performance/PerformanceUiAdapter.cpp

    src/desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp
    src/desktop/wallpaper/monitor/WallpaperMonitorAssignments.cpp
    src/desktop/wallpaper/monitor/WallpaperIndependentLayout.cpp
    src/desktop/wallpaper/monitor/IndependentWallpaperHost.cpp

    src/desktop/wallpaper/web/WebWallpaperHost.cpp
    src/desktop/wallpaper/web/WebDesktopSurfaceChild.cpp
    src/desktop/wallpaper/web/WallpaperWebRuntimeCoordinatorProduction.cpp

    src/desktop/wallpaper/render/WallpaperScaling.cpp
    src/desktop/wallpaper/render/VideoWallpaperPlayer.cpp
    src/desktop/wallpaper/render/VideoWallpaperSet.cpp

    src/desktop/wallpaper/library/WallpaperLibrary.cpp
    src/desktop/wallpaper/library/WallpaperLibraryWeb.cpp
    src/desktop/wallpaper/library/WallpaperPackage.cpp
    src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp

    src/search/AppSearch.cpp
    src/search/GozSearch.cpp
    src/ai/agent/L3Agent.cpp
)

set(TURINGDESK_HARNESS_SOURCES
    src/harness/HarnessHost.cpp
    src/harness/HarnessProcessManager.cpp
    src/harness/HarnessSettingsBridge.cpp
    src/harness/HarnessBundledRuntimeBootstrap.cpp
)

add_executable(TuringDesk WIN32 ${TURINGDESK_APP_SOURCES})
add_executable(TuringDeskWallpaper WIN32 ${TURINGDESK_WALLPAPER_SOURCES})
add_executable(TuringDeskHarness WIN32 ${TURINGDESK_HARNESS_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${TURINGDESK_APP_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${TURINGDESK_WALLPAPER_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${TURINGDESK_HARNESS_SOURCES})

set_source_files_properties(src/ai/tools/NativeTools.cpp PROPERTIES
    COMPILE_DEFINITIONS "TURINGDESK_NATIVE_TOOLS_IMPL"
)
set_source_files_properties(src/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp PROPERTIES
    COMPILE_DEFINITIONS "wWinMain=TuringDeskWallpaperMain"
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

set(TURINGDESK_DESKTOP_DOMAIN_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-desktop-domain-contract.ps1")
if (NOT EXISTS "${TURINGDESK_DESKTOP_DOMAIN_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing Desktop domain contract guard: ${TURINGDESK_DESKTOP_DOMAIN_CONTRACT_SCRIPT}")
endif()
add_custom_target(TuringDeskDesktopDomainContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${TURINGDESK_DESKTOP_DOMAIN_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying Desktop domain ownership contract"
    VERBATIM
)
add_dependencies(TuringDesk TuringDeskDesktopDomainContractCheck)
add_dependencies(TuringDeskWallpaper TuringDeskDesktopDomainContractCheck)

set(TURINGDESK_DESKTOP_SHELL_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-desktop-shell-ownership.ps1")
if (NOT EXISTS "${TURINGDESK_DESKTOP_SHELL_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing DesktopShellHost ownership guard: ${TURINGDESK_DESKTOP_SHELL_CONTRACT_SCRIPT}")
endif()
add_custom_target(TuringDeskDesktopShellContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${TURINGDESK_DESKTOP_SHELL_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying DesktopShellHost ownership contract"
    VERBATIM
)
add_dependencies(TuringDeskWallpaper TuringDeskDesktopShellContractCheck)

set(TURINGDESK_SOURCE_LAYOUT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-native-source-layout.ps1")
if (NOT EXISTS "${TURINGDESK_SOURCE_LAYOUT_SCRIPT}")
    message(FATAL_ERROR "Missing native source layout guard: ${TURINGDESK_SOURCE_LAYOUT_SCRIPT}")
endif()
add_custom_target(TuringDeskSourceLayoutCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${TURINGDESK_SOURCE_LAYOUT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying native source module layout"
    VERBATIM
)
add_dependencies(TuringDesk TuringDeskSourceLayoutCheck)
add_dependencies(TuringDeskWallpaper TuringDeskSourceLayoutCheck)
add_dependencies(TuringDeskHarness TuringDeskSourceLayoutCheck)

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