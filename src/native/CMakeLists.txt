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

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${MIAODESK_APP_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${MIAODESK_WALLPAPER_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${MIAODESK_HARNESS_SOURCES})

set_source_files_properties(src/ai/tools/NativeTools.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAODESK_NATIVE_TOOLS_IMPL"
)
set_source_files_properties(src/ai/agent/L3Agent.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAODESK_L3_WINHTTP_TRACE"
)
set_source_files_properties(src/ai/pi/PiRuntime.cpp PROPERTIES
    COMPILE_DEFINITIONS "MIAODESK_PI_CREDENTIAL_GUARD"
)
set_source_files_properties(src/desktop/wallpaper/legacy/WallpaperEngineProduction.cpp PROPERTIES
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
