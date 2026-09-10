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

set(MIAODESK_CORE_SOURCES
    ai/agent/L3Agent.cpp
    content/asset/MiaoAssetDatabase.cpp
    content/binding/MiaoContentDataBinding.cpp
    content/model/MiaoContentDefinitionLoader.cpp
    content/model/MiaoContentModel.cpp
    content/model/MiaoWidgetContentCatalog.cpp
    content/package/MiaoContentPackage.cpp
    content/render/MiaoRenderGraph.cpp
    content/render/MiaoPostProcessCompiler.cpp
    content/render/MiaoPostProcessShaderLibrary.cpp
    content/scene/MiaoSceneModel.cpp
    content/serialization/MiaoSceneSerializer.cpp
    content/runtime/MiaoParticleRuntime.cpp
    content/runtime/MiaoSceneFrameScheduler.cpp
    content/runtime/MiaoSceneRuntime.cpp
    content/runtime/MiaoSceneRuntimeModel.cpp
    content/shader/MiaoGpuParameterBlock.cpp
    content/shader/MiaoShaderContract.cpp
    desktop/control/DesktopControlService.cpp
    desktop/demo/StoreDemoExperience.cpp
    desktop/preview/GeneratedDesktopPreview.cpp
    desktop/shell/DesktopSurfaceTelemetry.cpp
    desktop/wallpaper/WallpaperService.cpp
    desktop/widgets/WidgetService.cpp
    desktop/widgets/DesktopWidgetStore.cpp
    desktop/widgets/DesktopWidgetController.cpp
    desktop/widgets/NativeWidgetPreset.cpp
    desktop/wallpaper/library/WallpaperPackage.cpp
    desktop/wallpaper/library/WallpaperLibrary.cpp
    desktop/wallpaper/library/WallpaperLibraryWeb.cpp
    desktop/wallpaper/library/BuiltinWallpaperCatalog.cpp
    desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp
    desktop/wallpaper/monitor/WallpaperMonitorAssignments.cpp
    search/AppSearch.cpp
    search/GozSearch.cpp
)

set(MIAODESK_APP_SOURCES
    app/main.cpp
    app/StartupManager.cpp
    ui/search/SearchWindow.cpp
    ui/settings/SettingsCenterWindow.cpp
    ui/ai/ConversationPanel.cpp

    ai/pi/PiRuntime.cpp
    ai/pi/PiNativeToolsExtension.cpp
    ai/tools/NativeTools.cpp
    ai/tools/NativeToolIsolation.cpp
    ai/tools/DesktopWidgetTools.cpp
    ai/agent/L3PersistenceSelfTest.cpp
)

set(MIAODESK_HARNESS_CORE_SOURCES
    harness/HarnessProcessManager.cpp
    harness/HarnessSettingsBridge.cpp
)

set(MIAODESK_WALLPAPER_SOURCES
    content/render/d2d/MiaoSceneD2DRenderer.cpp
    content/render/d3d11/MiaoD3D11ParticleRenderer.cpp
    content/render/d3d11/MiaoD3D11RenderTarget.cpp
    content/render/d3d11/MiaoD3D11TextureLoader.cpp
    content/render/d3d11/MiaoSceneD3D11Renderer.cpp
    desktop/wallpaper/runtime/WallpaperEntry.cpp
    desktop/wallpaper/legacy/WallpaperEngineProduction.cpp

    desktop/shell/DesktopShellHost.cpp
    desktop/shell/DesktopShellDiagnostics.cpp
    desktop/shell/DesktopShellSurfaceStack.cpp
    desktop/widgets/NativeWeatherService.cpp

    desktop/automation/AutomationService.cpp
    desktop/automation/WallpaperAutomation.cpp
    desktop/automation/WallpaperApplicationRules.cpp
    ui/automation/AutomationUiAdapter.cpp
    ui/automation/WallpaperAutomationWindowProduction.cpp
    ui/automation/WallpaperApplicationRulesWindow.cpp

    desktop/performance/PerformanceService.cpp
    desktop/performance/WallpaperPerformancePolicy.cpp
    ui/performance/PerformanceUiAdapter.cpp

    desktop/wallpaper/monitor/WallpaperIndependentLayout.cpp
    desktop/wallpaper/monitor/IndependentWallpaperHost.cpp

    desktop/wallpaper/web/WebWallpaperHost.cpp
    desktop/wallpaper/web/WebDesktopSurfaceChild.cpp
    desktop/wallpaper/web/WallpaperWebRuntimeCoordinator.cpp

    desktop/widgets/NativeWidgetHost.cpp

    desktop/wallpaper/render/WallpaperScaling.cpp
    desktop/wallpaper/render/VideoWallpaperPlayer.cpp
    desktop/wallpaper/render/VideoWallpaperSet.cpp

    ui/wallpaper/WallpaperLibraryWindowV2.cpp
    ui/settings/DesktopAiSettingsPage.cpp
)

set(MIAODESK_HARNESS_SOURCES
    harness/HarnessHost.cpp
    harness/HarnessBundledRuntimeBootstrap.cpp
)

set(MIAODESK_WINDOWS_RESOURCES)
if(WIN32)
    enable_language(RC)
    list(APPEND MIAODESK_WINDOWS_RESOURCES
        "${CMAKE_SOURCE_DIR}/assets/app/MiaoDesk.rc"
    )
endif()

add_library(MiaoDeskCore STATIC ${MIAODESK_CORE_SOURCES})
add_library(MiaoDeskHarnessCore STATIC ${MIAODESK_HARNESS_CORE_SOURCES})
add_executable(MiaoDesk WIN32 ${MIAODESK_APP_SOURCES} ${MIAODESK_WINDOWS_RESOURCES})
add_executable(MiaoDeskWallpaper WIN32 ${MIAODESK_WALLPAPER_SOURCES} ${MIAODESK_WINDOWS_RESOURCES})
add_executable(MiaoDeskHarness WIN32 ${MIAODESK_HARNESS_SOURCES} ${MIAODESK_WINDOWS_RESOURCES})

add_custom_command(TARGET MiaoDesk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MiaoDesk>/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/app/MiaoMiao.ico"
            "$<TARGET_FILE_DIR:MiaoDesk>/Assets/MiaoMiao.ico"
    VERBATIM
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MIAODESK_CORE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MIAODESK_HARNESS_CORE_SOURCES})
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

foreach(target MiaoDeskCore MiaoDeskHarnessCore MiaoDesk MiaoDeskWallpaper MiaoDeskHarness)
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
target_include_directories(MiaoDeskCore SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")

if (MSVC)
    target_compile_options(MiaoDeskCore PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskHarnessCore PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDesk PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWallpaper PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskHarness PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

target_link_libraries(MiaoDesk PRIVATE
    MiaoDeskCore
    MiaoDeskHarnessCore
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
    windowsapp
    "${WEBVIEW2_LOADER_STATIC}"
)

target_link_libraries(MiaoDeskWallpaper PRIVATE
    MiaoDeskCore
    user32
    gdi32
    shell32
    ole32
    advapi32
    comctl32
    d2d1
    d3d11
    d3dcompiler
    dxgi
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
    MiaoDeskHarnessCore
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
