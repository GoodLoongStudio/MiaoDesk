include(FetchContent)

option(MIAODESK_DEV_FAST "Skip contract-only build dependencies for local developer preview" OFF)

set(MIAODESK_WEBVIEW2_VERSION "1.0.4129.50")

if (CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
    set(WEBVIEW2_SDK_ROOT "${CMAKE_SOURCE_DIR}/runtime/arm64/webview2-sdk")
    set(WEBVIEW2_NATIVE_ARCH "arm64")
else()
    FetchContent_Declare(
        webview2sdk
        URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${MIAODESK_WEBVIEW2_VERSION}"
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
        "WebView2 SDK ${MIAODESK_WEBVIEW2_VERSION} is incomplete at ${WEBVIEW2_SDK_ROOT} for ${WEBVIEW2_NATIVE_ARCH}.")
endif()

# Keep source ownership visible in the build graph. Implementations are grouped
# by product/domain instead of being flattened under src/native/src.
set(MIAODESK_APP_SOURCES
    src/app/main.cpp
    src/ui/search/SearchWindow.cpp
    src/ui/settings/SettingsCenterWindow.cpp
    src/ui/ai/ConversationPanel.cpp

    src/ai/pi/PiRuntime.cpp
    src/ai/pi/PiNativeToolsExtension.cpp
    src/ai/tools/NativeTools.cpp
    src/ai/tools/NativeToolIsolation.cpp
    src/ai/tools/DesktopWidgetTools.cpp
    src/ai/a2ui/A2UIParser.cpp
    src/ai/agent/L3Agent.cpp
    src/ai/agent/L3PersistenceSelfTest.cpp

    src/desktop/control/DesktopControlService.cpp
    src/desktop/demo/StoreDemoExperience.cpp
    src/desktop/preview/GeneratedDesktopPreview.cpp
    src/desktop/preview/WidgetIntentComposer.cpp
    src/desktop/shell/DesktopSurfaceTelemetry.cpp
    src/desktop/wallpaper/WallpaperService.cpp
    src/desktop/widgets/WidgetService.cpp
    src/desktop/widgets/DesktopWidgetStore.cpp
    src/desktop/widgets/DesktopWidgetController.cpp
    src/desktop/widgets/NativeWidgetPreset.cpp
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

set(MIAODESK_WALLPAPER_SOURCES
    src/desktop/wallpaper/runtime/WallpaperEntry.cpp
    src/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp

    src/desktop/shell/DesktopShellHost.cpp
    src/desktop/shell/DesktopShellDiagnostics.cpp
    src/desktop/shell/DesktopShellSurfaceStack.cpp
    src/desktop/shell/DesktopSurfaceTelemetry.cpp

    src/desktop/control/DesktopControlService.cpp
    src/desktop/demo/StoreDemoExperience.cpp
    src/desktop/preview/GeneratedDesktopPreview.cpp
    src/desktop/preview/WidgetIntentComposer.cpp
    src/ai/a2ui/A2UIParser.cpp
    src/desktop/wallpaper/WallpaperService.cpp
    src/desktop/widgets/WidgetService.cpp
    src/desktop/widgets/DesktopWidgetStore.cpp
    src/desktop/widgets/DesktopWidgetController.cpp
    src/desktop/widgets/NativeWidgetPreset.cpp
    src/desktop/widgets/NativeWeatherService.cpp
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
    src/desktop/wallpaper/web/WallpaperWebRuntimeCoordinator.cpp

    src/desktop/widgets/NativeWidgetHost.cpp

    src/desktop/wallpaper/render/WallpaperScaling.cpp
    src/desktop/wallpaper/render/VideoWallpaperPlayer.cpp
    src/desktop/wallpaper/render/VideoWallpaperSet.cpp

    src/desktop/wallpaper/library/WallpaperLibrary.cpp
    src/desktop/wallpaper/library/WallpaperLibraryWeb.cpp
    src/desktop/wallpaper/library/WallpaperPackage.cpp
    src/ui/wallpaper/WallpaperLibraryWindowProduction.cpp
    src/ui/settings/DesktopAiSettingsPage.cpp

    src/search/AppSearch.cpp
    src/search/GozSearch.cpp
    src/ai/agent/L3Agent.cpp
)

set(MIAODESK_HARNESS_SOURCES
    src/harness/HarnessHost.cpp
    src/harness/HarnessProcessManager.cpp
    src/harness/HarnessSettingsBridge.cpp
    src/harness/HarnessBundledRuntimeBootstrap.cpp
)

set(MIAODESK_WIDGET_ACCEPTANCE_SOURCES
    src/desktop/widgets/WidgetRuntimeAcceptanceMain.cpp
    src/desktop/widgets/WidgetRuntimeAcceptance.cpp
    src/desktop/widgets/WidgetAcceptanceConfigContinuity.cpp
    src/desktop/widgets/WidgetService.cpp
    src/desktop/widgets/DesktopWidgetStore.cpp
    src/desktop/widgets/NativeWidgetPreset.cpp
    src/desktop/shell/DesktopSurfaceTelemetry.cpp
    src/desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp
)

add_executable(MiaoDesk WIN32 ${MIAODESK_APP_SOURCES})
add_executable(MiaoDeskWallpaper WIN32 ${MIAODESK_WALLPAPER_SOURCES})
add_executable(MiaoDeskHarness WIN32 ${MIAODESK_HARNESS_SOURCES})
add_executable(MiaoDeskWidgetAcceptance ${MIAODESK_WIDGET_ACCEPTANCE_SOURCES})


# Keep the canonical 妙喵 application icon beside the native host in every
# build, not only inside MSIX packaging. Tray and assistant chat avatar
# intentionally consume this same asset.
add_custom_command(TARGET MiaoDesk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MiaoDesk>/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/packaging/windows-store/assets/MiaoMiao.ico"
            "$<TARGET_FILE_DIR:MiaoDesk>/Assets/MiaoMiao.ico"
    VERBATIM
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${MIAODESK_APP_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${MIAODESK_WALLPAPER_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${MIAODESK_HARNESS_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${MIAODESK_WIDGET_ACCEPTANCE_SOURCES})

set_source_files_properties(src/ai/tools/NativeTools.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAODESK_NATIVE_TOOLS_IMPL"
)
set_source_files_properties(src/ai/agent/L3Agent.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAODESK_L3_WINHTTP_TRACE"
)
set_source_files_properties(src/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp PROPERTIES
    COMPILE_DEFINITIONS "wWinMain=MiaoDeskWallpaperMain"
)

set(MIAODESK_L3_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-l3-runtime-contract.ps1")
if (NOT EXISTS "${MIAODESK_L3_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing L3 runtime contract guard: ${MIAODESK_L3_CONTRACT_SCRIPT}")
endif()
add_custom_target(MiaoDeskL3ContractCheck ALL
    COMMAND pwsh -NoProfile -File "${MIAODESK_L3_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying Pi-first L3 runtime contract"
    VERBATIM
)

set(MIAODESK_RUNTIME_LOG_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-runtime-log-paths.ps1")
if (NOT EXISTS "${MIAODESK_RUNTIME_LOG_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing runtime log contract guard: ${MIAODESK_RUNTIME_LOG_CONTRACT_SCRIPT}")
endif()
add_custom_target(MiaoDeskRuntimeLogContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${MIAODESK_RUNTIME_LOG_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying Desktop runtime log routing"
    VERBATIM
)

set(MIAODESK_DESKTOP_DOMAIN_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-desktop-domain-contract.ps1")
if (NOT EXISTS "${MIAODESK_DESKTOP_DOMAIN_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing Desktop domain contract guard: ${MIAODESK_DESKTOP_DOMAIN_CONTRACT_SCRIPT}")
endif()
add_custom_target(MiaoDeskDesktopDomainContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${MIAODESK_DESKTOP_DOMAIN_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying Desktop domain ownership contract"
    VERBATIM
)

set(MIAODESK_DESKTOP_SHELL_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-desktop-shell-ownership.ps1")
if (NOT EXISTS "${MIAODESK_DESKTOP_SHELL_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing DesktopShellHost ownership guard: ${MIAODESK_DESKTOP_SHELL_CONTRACT_SCRIPT}")
endif()
add_custom_target(MiaoDeskDesktopShellContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${MIAODESK_DESKTOP_SHELL_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying DesktopShellHost ownership contract"
    VERBATIM
)

set(MIAODESK_SOURCE_LAYOUT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-native-source-layout.ps1")
if (NOT EXISTS "${MIAODESK_SOURCE_LAYOUT_SCRIPT}")
    message(FATAL_ERROR "Missing native source layout guard: ${MIAODESK_SOURCE_LAYOUT_SCRIPT}")
endif()
add_custom_target(MiaoDeskSourceLayoutCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${MIAODESK_SOURCE_LAYOUT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying native source module layout"
    VERBATIM
)

set(MIAODESK_WIDGET_ACCEPTANCE_CONTRACT_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-widget-acceptance-contract.ps1")
if (NOT EXISTS "${MIAODESK_WIDGET_ACCEPTANCE_CONTRACT_SCRIPT}")
    message(FATAL_ERROR "Missing M3 Widget acceptance contract guard: ${MIAODESK_WIDGET_ACCEPTANCE_CONTRACT_SCRIPT}")
endif()
add_custom_target(MiaoDeskWidgetAcceptanceContractCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${MIAODESK_WIDGET_ACCEPTANCE_CONTRACT_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying M3 Widget real-Windows acceptance ownership contract"
    VERBATIM
)

set(MIAODESK_DESKTOP_UI_PARITY_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/verify-desktop-ui-parity.ps1")
if (NOT EXISTS "${MIAODESK_DESKTOP_UI_PARITY_SCRIPT}")
    message(FATAL_ERROR "Missing M4 Desktop UI parity guard: ${MIAODESK_DESKTOP_UI_PARITY_SCRIPT}")
endif()
add_custom_target(MiaoDeskDesktopUiParityCheck ALL
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${MIAODESK_DESKTOP_UI_PARITY_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verifying M4 desktop UI capability parity gate"
    VERBATIM
)

# CI/release builds keep the contract graph. Local developer preview explicitly
# opts out so a UI edit only compiles the requested binary target.
if (NOT MIAODESK_DEV_FAST)
    add_dependencies(MiaoDesk MiaoDeskL3ContractCheck)
    add_dependencies(MiaoDesk MiaoDeskRuntimeLogContractCheck)
    add_dependencies(MiaoDesk MiaoDeskDesktopDomainContractCheck)
    add_dependencies(MiaoDesk MiaoDeskSourceLayoutCheck)
    add_dependencies(MiaoDesk MiaoDeskDesktopUiParityCheck)

    add_dependencies(MiaoDeskWallpaper MiaoDeskRuntimeLogContractCheck)
    add_dependencies(MiaoDeskWallpaper MiaoDeskDesktopDomainContractCheck)
    add_dependencies(MiaoDeskWallpaper MiaoDeskDesktopShellContractCheck)
    add_dependencies(MiaoDeskWallpaper MiaoDeskSourceLayoutCheck)
    add_dependencies(MiaoDeskWallpaper MiaoDeskDesktopUiParityCheck)

    add_dependencies(MiaoDeskHarness MiaoDeskRuntimeLogContractCheck)
    add_dependencies(MiaoDeskHarness MiaoDeskSourceLayoutCheck)

    add_dependencies(MiaoDeskWidgetAcceptance MiaoDeskSourceLayoutCheck)
    add_dependencies(MiaoDeskWidgetAcceptance MiaoDeskDesktopDomainContractCheck)
    add_dependencies(MiaoDeskWidgetAcceptance MiaoDeskDesktopShellContractCheck)
    add_dependencies(MiaoDeskWidgetAcceptance MiaoDeskWidgetAcceptanceContractCheck)
endif()

foreach(target MiaoDesk MiaoDeskWallpaper MiaoDeskHarness MiaoDeskWidgetAcceptance)
    target_include_directories(${target} PRIVATE include)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
endforeach()

target_include_directories(MiaoDesk SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_include_directories(MiaoDeskWallpaper SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_include_directories(MiaoDeskHarness SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")

if (MSVC)
    target_compile_options(MiaoDesk PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWallpaper PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskHarness PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWidgetAcceptance PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

target_link_libraries(MiaoDesk PRIVATE
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
    "${WEBVIEW2_LOADER_STATIC}"
)

target_link_libraries(MiaoDeskWallpaper PRIVATE
    user32
    gdi32
    shell32
    ole32
    advapi32
    comctl32
    d2d1
    dwrite
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

target_link_libraries(MiaoDeskHarness PRIVATE
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

target_link_libraries(MiaoDeskWidgetAcceptance PRIVATE
    user32
    shell32
    advapi32
)
