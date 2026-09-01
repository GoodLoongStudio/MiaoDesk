set(WEBVIEW2_ROOT "${CMAKE_SOURCE_DIR}/third_party/webview2")

if (CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
    set(WEBVIEW2_ARCH "arm64")
elseif (CMAKE_VS_PLATFORM_NAME STREQUAL "x64")
    set(WEBVIEW2_ARCH "x64")
else()
    message(FATAL_ERROR "MiaoDesk supports WebView2 builds on x64 and ARM64 only.")
endif()

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_ROOT}/include")
set(WEBVIEW2_LOADER_STATIC "${WEBVIEW2_ROOT}/lib/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib")
if (NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h" OR NOT EXISTS "${WEBVIEW2_LOADER_STATIC}")
    message(FATAL_ERROR "WebView2 SDK is incomplete at ${WEBVIEW2_ROOT} for ${WEBVIEW2_ARCH}.")
endif()

set(MIAODESK_APP_SOURCES
    app/main.cpp
    ui/search/SearchWindow.cpp
    ui/settings/SettingsCenterWindow.cpp
    ui/ai/ConversationPanel.cpp

    ai/pi/PiRuntime.cpp
    ai/pi/PiNativeToolsExtension.cpp
    ai/tools/NativeTools.cpp
    ai/tools/NativeToolIsolation.cpp
    ai/tools/DesktopWidgetTools.cpp
    ai/a2ui/A2UIParser.cpp
    ai/agent/L3Agent.cpp
    ai/agent/L3PersistenceSelfTest.cpp

    desktop/control/DesktopControlService.cpp
    desktop/demo/StoreDemoExperience.cpp
    desktop/preview/GeneratedDesktopPreview.cpp
    desktop/preview/WidgetIntentComposer.cpp
    desktop/shell/DesktopSurfaceTelemetry.cpp
    desktop/wallpaper/WallpaperService.cpp
    desktop/widgets/WidgetService.cpp
    desktop/widgets/DesktopWidgetStore.cpp
    desktop/widgets/DesktopWidgetController.cpp
    desktop/widgets/NativeWidgetPreset.cpp
    desktop/wallpaper/library/WallpaperPackage.cpp
    desktop/wallpaper/library/WallpaperLibrary.cpp
    desktop/wallpaper/library/WallpaperLibraryWeb.cpp
    desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp
    desktop/wallpaper/monitor/WallpaperMonitorAssignments.cpp

    search/AppSearch.cpp
    search/GozSearch.cpp
    harness/HarnessProcessManager.cpp
    harness/HarnessSettingsBridge.cpp
)

set(MIAODESK_WALLPAPER_SOURCES
    desktop/wallpaper/runtime/WallpaperEntry.cpp
    desktop/wallpaper/legacy/WallpaperEngineProduction.cpp

    desktop/shell/DesktopShellHost.cpp
    desktop/shell/DesktopShellDiagnostics.cpp
    desktop/shell/DesktopShellSurfaceStack.cpp
    desktop/shell/DesktopSurfaceTelemetry.cpp

    desktop/control/DesktopControlService.cpp
    desktop/demo/StoreDemoExperience.cpp
    desktop/preview/GeneratedDesktopPreview.cpp
    desktop/preview/WidgetIntentComposer.cpp
    ai/a2ui/A2UIParser.cpp
    desktop/wallpaper/WallpaperService.cpp
    desktop/widgets/WidgetService.cpp
    desktop/widgets/DesktopWidgetStore.cpp
    desktop/widgets/DesktopWidgetController.cpp
    desktop/widgets/NativeWidgetPreset.cpp
    desktop/widgets/NativeWeatherService.cpp
    ui/widgets/DesktopWidgetUiAdapter.cpp

    desktop/automation/AutomationService.cpp
    desktop/automation/WallpaperAutomation.cpp
    desktop/automation/WallpaperApplicationRules.cpp
    ui/automation/AutomationUiAdapter.cpp
    ui/automation/WallpaperAutomationWindowProduction.cpp
    ui/automation/WallpaperApplicationRulesWindow.cpp

    desktop/performance/PerformanceService.cpp
    desktop/performance/WallpaperPerformancePolicy.cpp
    ui/performance/PerformanceUiAdapter.cpp

    desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp
    desktop/wallpaper/monitor/WallpaperMonitorAssignments.cpp
    desktop/wallpaper/monitor/WallpaperIndependentLayout.cpp
    desktop/wallpaper/monitor/IndependentWallpaperHost.cpp

    desktop/wallpaper/web/WebWallpaperHost.cpp
    desktop/wallpaper/web/WebDesktopSurfaceChild.cpp
    desktop/wallpaper/web/WallpaperWebRuntimeCoordinator.cpp

    desktop/widgets/NativeWidgetHost.cpp

    desktop/wallpaper/render/WallpaperScaling.cpp
    desktop/wallpaper/render/VideoWallpaperPlayer.cpp
    desktop/wallpaper/render/VideoWallpaperSet.cpp

    desktop/wallpaper/library/WallpaperLibrary.cpp
    desktop/wallpaper/library/WallpaperLibraryWeb.cpp
    desktop/wallpaper/library/WallpaperPackage.cpp
    ui/wallpaper/WallpaperLibraryWindowV2.cpp
    ui/settings/DesktopAiSettingsPage.cpp

    search/AppSearch.cpp
    search/GozSearch.cpp
    ai/agent/L3Agent.cpp
)

set(MIAODESK_HARNESS_SOURCES
    harness/HarnessHost.cpp
    harness/HarnessProcessManager.cpp
    harness/HarnessSettingsBridge.cpp
    harness/HarnessBundledRuntimeBootstrap.cpp
)

add_executable(MiaoDesk WIN32 ${MIAODESK_APP_SOURCES})
add_executable(MiaoDeskWallpaper WIN32 ${MIAODESK_WALLPAPER_SOURCES})
add_executable(MiaoDeskHarness WIN32 ${MIAODESK_HARNESS_SOURCES})

add_custom_command(TARGET MiaoDesk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MiaoDesk>/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/app/MiaoMiao.ico"
            "$<TARGET_FILE_DIR:MiaoDesk>/Assets/MiaoMiao.ico"
    VERBATIM
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MIAODESK_APP_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MIAODESK_WALLPAPER_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MIAODESK_HARNESS_SOURCES})

set_source_files_properties(ai/tools/NativeTools.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAODESK_NATIVE_TOOLS_IMPL"
)
set_source_files_properties(ai/agent/L3Agent.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAODESK_L3_WINHTTP_TRACE"
)
set_source_files_properties(ai/pi/PiRuntime.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAODESK_PI_CREDENTIAL_GUARD"
)
set_source_files_properties(desktop/wallpaper/legacy/WallpaperEngineProduction.cpp PROPERTIES
    COMPILE_DEFINITIONS "wWinMain=MiaoDeskWallpaperMain"
)

foreach(target MiaoDesk MiaoDeskWallpaper MiaoDeskHarness)
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
