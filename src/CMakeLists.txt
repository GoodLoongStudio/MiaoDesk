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
    # Native tools live in the core library, not in the app target, so the
    # content-skill gate can link them without compiling a second copy. Each
    # implementation file must keep exactly one CMake owner.
    ai/tools/NativeTools.cpp
    content/input/MiaoAudioCapture.cpp
    content/asset/MiaoAssetDatabase.cpp
    content/binding/MiaoContentDataBinding.cpp
    content/model/ContentWidgetInstanceStore.cpp
    content/model/MiaoContentDefinitionLoader.cpp
    content/model/MiaoContentModel.cpp
    content/model/MiaoWidgetContentCatalog.cpp
    content/package/MiaoContentPackage.cpp
    content/package/MiaoContentPackageCatalog.cpp
    content/package/MiaoContentPackageManager.cpp
    content/package/MiaoContentPackageUninstall.cpp
    content/render/MiaoRenderGraph.cpp
    content/render/MiaoPostProcessCompiler.cpp
    content/render/MiaoPostProcessShaderLibrary.cpp
    content/render/MiaoSpriteMaterialPolicy.cpp
    # Lives under render/d3d11/ but is compiled into MiaoDeskCore, not into
    # MiaoDeskSceneD3D11: it is the platform-free half of the D3D11 render-target and
    # texture-loader *rules* (dimension arithmetic, path policy, and their self-tests).
    # It used to sit in .cpp files that include <d3d11.h>, which is why those pure
    # assertions could only run where a D3D11 toolchain existed. See the file header.
    content/render/d3d11/MiaoD3D11RenderPolicy.cpp
    content/scene/MiaoSceneModel.cpp
    content/serialization/MiaoSceneSerializer.cpp
    content/runtime/MiaoParticleRuntime.cpp
    content/runtime/MiaoSceneFrameScheduler.cpp
    content/runtime/MiaoSceneRuntime.cpp
    content/runtime/MiaoSceneRuntimeModel.cpp
    content/shader/MiaoGpuParameterBlock.cpp
    content/shader/MiaoShaderContract.cpp
    desktop/control/DesktopControlService.cpp
    desktop/control/DesktopContentPackageLifecycle.cpp
    desktop/demo/StoreDemoExperience.cpp
    desktop/preview/GeneratedDesktopPreview.cpp
    desktop/shell/DesktopSurfaceTelemetry.cpp
    desktop/wallpaper/WallpaperService.cpp
    desktop/widgets/WidgetService.cpp
    desktop/widgets/DesktopWidgetStore.cpp
    desktop/widgets/DesktopWidgetController.cpp
    desktop/widgets/NativeWidgetPreset.cpp
    desktop/widgets/TodayTaskStore.cpp
    desktop/widgets/TodayTaskContentProvider.cpp
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
    ai/tools/NativeToolIsolation.cpp
    ai/tools/DesktopWidgetTools.cpp
    ai/agent/L3PersistenceSelfTest.cpp
)

set(MIAODESK_HARNESS_CORE_SOURCES
    harness/HarnessProcessManager.cpp
    harness/HarnessSettingsBridge.cpp
)

# The 2D scene renderer is a library, not part of the wallpaper EXE's source list, so
# that the D2D renderer SelfTest target can link it. Two CMake owners for one .cpp is
# a path-layout-contract violation (scripts/verify-path-layout-contract.ps1), and the
# contract's own prescribed remedy is a library target.
set(MIAODESK_SCENE2D_SOURCES
    content/render/d2d/MiaoD2DTextureLoader.cpp
    content/render/d2d/MiaoSceneD2DRenderer.cpp
)

# Same reasoning as MIAODESK_SCENE2D_SOURCES, one directory over.
# MiaoSceneD3D11Renderer::SelfTest() and the three helpers it drives currently have no
# caller at all. The only way to reach them was to compile these four .cpp a second time
# inside a test target, and each implementation file has exactly one CMake owner — so the
# tests stayed dark rather than the rule being broken. A library target is the
# path-layout contract's own prescribed remedy for this.
#
# The note about MiaoD3D11TextureLoader is carried over verbatim from where these four
# used to sit in MIAODESK_WALLPAPER_SOURCES; it records a real LNK2019 and still holds.
set(MIAODESK_SCENE_D3D11_SOURCES
    content/render/d3d11/MiaoD3D11ParticleRenderer.cpp
    content/render/d3d11/MiaoD3D11RenderTarget.cpp
    # MiaoSceneD3D11Renderer.cpp references MiaoD3D11TextureLoader::LoadImageW and
    # ::SelfTestPathPolicy. Listing the loader is what makes those symbols exist at
    # link time; omitting it produced LNK2019/LNK1120 in every packaging workflow.
    content/render/d3d11/MiaoD3D11TextureLoader.cpp
    content/render/d3d11/MiaoSceneD3D11Renderer.cpp
)

set(MIAODESK_WALLPAPER_SOURCES
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

    desktop/wallpaper/monitor/MiaoWallpaperAudioTap.cpp
    desktop/wallpaper/monitor/WallpaperIndependentLayout.cpp
    desktop/wallpaper/monitor/IndependentWallpaperHost.cpp

    desktop/wallpaper/web/WebWallpaperHost.cpp
    desktop/wallpaper/web/WebDesktopSurfaceChild.cpp
    desktop/wallpaper/web/WallpaperWebRuntimeCoordinator.cpp

    desktop/widgets/NativeWidgetHost.cpp
    desktop/widgets/ContentWidgetHost.cpp
    desktop/widgets/OfficialWidgetContentPainter.cpp

    desktop/wallpaper/render/WallpaperScaling.cpp
    desktop/wallpaper/render/VideoWallpaperPlayer.cpp
    desktop/wallpaper/render/VideoWallpaperSet.cpp

    ui/wallpaper/WallpaperLibraryWindowV2.cpp
    ui/wallpaper/ContentPackageUiBridge.cpp
    ui/wallpaper/ContentPackageManagerDialog.cpp
    ui/wallpaper/ContentWidgetSettingsDialog.cpp
    ui/wallpaper/ContentWidgetPreviewRenderer.cpp
    ui/wallpaper/TodayTaskEditorDialog.cpp
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
add_library(MiaoDeskScene2D STATIC ${MIAODESK_SCENE2D_SOURCES})
add_library(MiaoDeskSceneD3D11 STATIC ${MIAODESK_SCENE_D3D11_SOURCES})
add_executable(MiaoDesk WIN32 ${MIAODESK_APP_SOURCES} ${MIAODESK_WINDOWS_RESOURCES})
add_executable(MiaoDeskWallpaper WIN32 ${MIAODESK_WALLPAPER_SOURCES} ${MIAODESK_WINDOWS_RESOURCES})
add_executable(MiaoDeskHarness WIN32 ${MIAODESK_HARNESS_SOURCES} ${MIAODESK_WINDOWS_RESOURCES})
add_executable(MiaoDeskContentWebReplacementTest tests/ContentWebReplacementContinuity.cpp)
add_executable(MiaoDeskContentSkillLoadingTest tests/ContentSkillLoading.cpp)
add_executable(MiaoDeskMediaPackageTest tests/MediaWallpaperPackage.cpp)
# No Windows dependency: the input bus core is pure logic, so this gate runs on
# every build rather than only where audio hardware exists.
add_executable(MiaoDeskInputBusCoreTest tests/InputBusCore.cpp)
add_executable(MiaoDeskInputBusPublisherTest tests/InputBusPublisher.cpp)
add_executable(MiaoDeskAudioIngressTest tests/AudioIngress.cpp)
add_executable(MiaoDeskBindingResponseTest tests/BindingResponse.cpp)
add_executable(MiaoDeskSceneRuntimeTest tests/MiaoSceneRuntimeTest.cpp)
add_executable(MiaoDeskSceneSpatial3DTest tests/SceneSpatial3D.cpp)
# Pure logic: reproduces the host's per-monitor attribution arithmetic against a fake
# topology, so the multi-monitor decision is testable without real monitors.
add_executable(MiaoDeskPointerAttributionTest tests/PointerAttribution.cpp)
# Pure logic: spriteRenderer texture contract validation, no Windows needed.
add_executable(MiaoDeskSpriteTextureContractTest tests/SpriteTextureContract.cpp)
# Windows-only: drives the real D2D renderer against a real package and reads pixels
# back. Its SelfTest had no caller at all before this target existed.
add_executable(MiaoDeskSceneD2DRendererTest tests/SceneD2DRenderer.cpp)
# Pure logic: pins the textured-sprite scene fixture's schema (including the
# assetReference-as-a-bare-string shape) on every commit, without a render target.
# scripts/verify-scene-fixture-parity.sh keeps it identical to the copy the Windows
# SelfTest renders.
add_executable(MiaoDeskSceneTextureFixtureTest tests/SceneTextureFixture.cpp)
# Pure logic: walks the three shipped .mdwall packages through package load ->
# deserialize -> validate -> initialize -> asset-database build. It is what makes
# "the built-in wallpapers actually resolve their images" a checked fact instead of an
# assumption, and it is where the missing-art blocker on NeonCity/MysticMoon is pinned.
add_executable(MiaoDeskBuiltinWallpaperPackagesTest tests/BuiltinWallpaperPackages.cpp)
# Pure logic: MiaoSceneSerializer::SelfTest had no caller anywhere in the repository.
# 120 lines of assertions, concentrated on particle emitters, and none of them ran.
add_executable(MiaoDeskSceneSerializerSelfTest tests/SceneSerializerSelfTest.cpp)
# Pure logic: runs the content-layer SelfTests that were reachable only through
# MiaoSceneD3D11Renderer::SelfTest, an aggregator with no caller. Seven of them,
# ~290 lines, and none of them ran on any machine.
add_executable(MiaoDeskContentSelfTests tests/ContentSelfTests.cpp)
# Pure logic: pins the host->page audio envelope against the shim's contract — field
# names, array lengths, value ranges, and that the decimal point survives a locale
# whose decimal separator is a comma. The page-side half is covered by
# tests/WebAudioBridge.mjs; the two halves had nothing checking them against each other.
add_executable(MiaoDeskWebAudioEnvelopeTest tests/WebAudioEnvelope.cpp)
# Windows-only: the four self-tests that hung off MiaoSceneD3D11Renderer::SelfTest() with
# no caller. P3-6 step 2, and it comes after step 1 on purpose: the MiaoDeskSceneD3D11
# library had to be proved to compile and link under MSVC before anything was allowed to
# depend on it, otherwise a link failure could not be attributed to either change.
add_executable(MiaoDeskSceneD3D11Test tests/SceneD3D11Renderer.cpp)
# Pure logic: the two render backends must agree about which SpriteRenderers they can
# draw. That rule is platform-independent (material + texture -> a draw path), so the
# parity is checkable on every commit even though only one backend can render here.
add_executable(MiaoDeskSpriteMaterialPolicyTest tests/SpriteMaterialPolicy.cpp)

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
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MIAODESK_SCENE2D_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MIAODESK_SCENE_D3D11_SOURCES})
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

foreach(target MiaoDeskCore MiaoDeskHarnessCore MiaoDeskScene2D MiaoDeskSceneD3D11 MiaoDesk MiaoDeskWallpaper MiaoDeskHarness MiaoDeskContentWebReplacementTest MiaoDeskContentSkillLoadingTest MiaoDeskMediaPackageTest MiaoDeskInputBusCoreTest MiaoDeskInputBusPublisherTest MiaoDeskAudioIngressTest MiaoDeskBindingResponseTest MiaoDeskSceneRuntimeTest MiaoDeskSceneSpatial3DTest MiaoDeskPointerAttributionTest MiaoDeskSpriteTextureContractTest MiaoDeskSceneD2DRendererTest MiaoDeskSceneTextureFixtureTest MiaoDeskBuiltinWallpaperPackagesTest MiaoDeskSpriteMaterialPolicyTest MiaoDeskSceneSerializerSelfTest MiaoDeskContentSelfTests MiaoDeskWebAudioEnvelopeTest MiaoDeskSceneD3D11Test)
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
    target_compile_options(MiaoDeskScene2D PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSceneD3D11 PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDesk PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWallpaper PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskHarness PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentWebReplacementTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentSkillLoadingTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskMediaPackageTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskInputBusCoreTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskInputBusPublisherTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskAudioIngressTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskBindingResponseTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSceneRuntimeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSceneSpatial3DTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskPointerAttributionTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSpriteTextureContractTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSceneD2DRendererTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSceneTextureFixtureTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskBuiltinWallpaperPackagesTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSceneSerializerSelfTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentSelfTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWebAudioEnvelopeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSceneD3D11Test PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSpriteMaterialPolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
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
    MiaoDeskScene2D
    MiaoDeskSceneD3D11
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

target_link_libraries(MiaoDeskContentWebReplacementTest PRIVATE
    MiaoDeskCore
    user32
    gdi32
    shell32
    ole32
    advapi32
    comctl32
    d2d1
    dwrite
    windowscodecs
    shlwapi
    version
    winhttp
)

# content_skill_get reaches into NativeTools.cpp, which links against oleaut32 for
# its PowerPoint path even though this gate only exercises the skill allowlist.
target_link_libraries(MiaoDeskContentSkillLoadingTest PRIVATE
    MiaoDeskCore
    user32
    shell32
    ole32
    oleaut32
    advapi32
    shlwapi
)

target_link_libraries(MiaoDeskMediaPackageTest PRIVATE
    MiaoDeskCore
    user32
    shell32
    ole32
    advapi32
    shlwapi
)

# MiaoDeskInputBusCoreTest intentionally links nothing: MiaoInputBus.h is pure C++
# with no Windows or library dependency, which is what makes it runnable everywhere.
target_link_libraries(MiaoDeskInputBusPublisherTest PRIVATE
    MiaoDeskCore
)

target_link_libraries(MiaoDeskAudioIngressTest PRIVATE
    MiaoDeskCore
)

# Both drive the real scene runtime and serializer, so they must link the same
# translation units those use.
target_link_libraries(MiaoDeskBindingResponseTest PRIVATE
    MiaoDeskCore
)

target_link_libraries(MiaoDeskSceneRuntimeTest PRIVATE
    MiaoDeskCore
)

target_link_libraries(MiaoDeskSceneSpatial3DTest PRIVATE
    MiaoDeskCore
)

# MiaoSceneModel.cpp lives in MiaoDeskCore; without this line the target compiles and
# then fails to link MiaoSceneModel::Validate.
target_link_libraries(MiaoDeskSpriteTextureContractTest PRIVATE
    MiaoDeskCore
)

target_link_libraries(MiaoDeskSceneTextureFixtureTest PRIVATE
    MiaoDeskCore
)

target_link_libraries(MiaoDeskBuiltinWallpaperPackagesTest PRIVATE
    MiaoDeskCore
)

target_link_libraries(MiaoDeskSpriteMaterialPolicyTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskSceneSerializerSelfTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskContentSelfTests PRIVATE
    MiaoDeskCore
)

# Links nothing beyond MiaoDeskCore: the envelope builder is a header-only pure
# function over content::inputbus::AudioSpectrumFrame, which is exactly why it
# can run on every machine.
target_link_libraries(MiaoDeskWebAudioEnvelopeTest PRIVATE
    MiaoDeskCore
)



# This one drives the real D2D renderer: it owns a render target, so it links the same
# graphics and codec libraries MiaoDeskWallpaper does. The renderer comes from the
# MiaoDeskScene2D library rather than from listing its .cpp here, because each
# implementation file has exactly one CMake owner.
target_link_libraries(MiaoDeskSceneD2DRendererTest PRIVATE
    MiaoDeskCore
    MiaoDeskScene2D
    user32
    gdi32
    shell32
    ole32
    advapi32
    d2d1
    dwrite
    windowscodecs
    shlwapi
)

# Same shape, one directory over: it drives the real D3D11 renderer, so it links the
# graphics, compiler and codec libraries MiaoDeskWallpaper does. The renderer comes from
# the MiaoDeskSceneD3D11 library that P3-6 step 1 created — listing its .cpp here instead
# would give one implementation file two CMake owners.
target_link_libraries(MiaoDeskSceneD3D11Test PRIVATE
    MiaoDeskCore
    MiaoDeskSceneD3D11
    user32
    shell32
    ole32
    advapi32
    d3d11
    d3dcompiler
    dxgi
    windowscodecs
    shlwapi
)

