set(WEBVIEW2_ROOT "${CMAKE_SOURCE_DIR}/third_party/webview2")

set(WHALEDESK_TARGET_ARCH "${CMAKE_VS_PLATFORM_NAME}")
if(NOT WHALEDESK_TARGET_ARCH AND DEFINED ENV{VSCMD_ARG_TGT_ARCH})
    set(WHALEDESK_TARGET_ARCH "$ENV{VSCMD_ARG_TGT_ARCH}")
endif()
if(NOT WHALEDESK_TARGET_ARCH)
    set(WHALEDESK_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

string(TOLOWER "${WHALEDESK_TARGET_ARCH}" WHALEDESK_TARGET_ARCH_LOWER)
if(WHALEDESK_TARGET_ARCH_LOWER MATCHES "^(arm64|aarch64)$")
    set(WEBVIEW2_ARCH "arm64")
elseif(WHALEDESK_TARGET_ARCH_LOWER MATCHES "^(x64|amd64|x86_64)$")
    set(WEBVIEW2_ARCH "x64")
else()
    message(FATAL_ERROR "WhaleDesK supports WebView2 builds on x64 and ARM64 only (detected: ${WHALEDESK_TARGET_ARCH}).")
endif()

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_ROOT}/include")
set(WEBVIEW2_LOADER_STATIC "${WEBVIEW2_ROOT}/lib/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib")
if (NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h" OR NOT EXISTS "${WEBVIEW2_LOADER_STATIC}")
    message(FATAL_ERROR "WebView2 SDK is incomplete at ${WEBVIEW2_ROOT} for ${WEBVIEW2_ARCH}.")
endif()

set(WHALEDESK_CORE_SOURCES
    ai/agent/L3Agent.cpp
    # P0-07: 一次请求的 URL 与它用的凭据是否同源,以及那个槽名。纯逻辑(不 import
    # Windows 头)才让"本地命令换过主机之后,profile 的 Key 会不会被发到别处去"
    # 这条能在本机验 —— L3Agent.cpp 为了 wincred/winhttp 只能 Windows 上编。
    ai/agent/WhaleAgentConfigAuthority.cpp
    # AI-03: 轮次相位不变量。Stop() 是请求不是完成 —— 取消之后那一轮仍然算忙,
    # 否则第二轮会与还没退完的第一轮共用同一个 Node 进程。纯逻辑,本机可验。
    ai/pi/WhaleTurnLifecycle.cpp
    # P0-09: 旧配置升级时要不要把 AI 会话搬到新身份下。纯逻辑,本机可验 ——
    # L3Agent.cpp 为了 winhttp.h 只能 Windows 上编,而"搬家会话"这条判定必须每轮都跑。
    ai/agent/WhaleSessionMigration.cpp
    # P0-03:壁纸 20 次循环的"该不该重建运行时/重试前要不要摘层"裁决。
    # WallpaperEngine.cpp 自己都不是独立 TU(被 WallpaperEngineProduction.cpp include),
    # 所以这批不变量此前本机一行都跑不到。
    desktop/wallpaper/legacy/WhaleWallpaperCyclePolicy.cpp
    # P0-08 后半句:单实例锁的持有权裁决。OpenMutexW 只回答"锁在不在",
    # 不回答"它属于谁、那个进程还活着吗"—— 于是卡住的持有者看不出来。纯逻辑,本机可验。
    # P0-08 后半句:锁持有权记录的编解码。owner 没有地方发身份的话,
    # 上面那个裁决根本没有输入可用。
    # Native tools live in the core library, not in the app target, so the
    # content-skill gate can link them without compiling a second copy. Each
    # implementation file must keep exactly one CMake owner.
    ai/tools/NativeTools.cpp
    content/input/WhaleAudioCapture.cpp
    content/asset/WhaleAssetDatabase.cpp
    content/binding/WhaleContentDataBinding.cpp
    content/binding/WhaleCapabilityCatalog.cpp
    content/model/ContentWidgetInstanceStore.cpp
    content/model/WhaleContentDefinitionLoader.cpp
    content/model/WhaleContentModel.cpp
    content/model/WhaleWidgetContentCatalog.cpp
    content/package/WhaleContentPackage.cpp
    content/package/WhaleContentPackageCatalog.cpp
    content/package/WhaleContentPackageManager.cpp
    # CCA-05:候选摘要。manifest + scene + 参数 + 引用素材的内容身份,
    # 自带 SHA-256(用已发表测试向量验过),因为摘要要跨进程记录和比较。
    content/package/ContentCandidateDigest.cpp
    # CCA-05: candidate ledger. Host side of the submission contract -- it decides
    # whether a submission is the same revision or a new one, so it is the only
    # place that can keep "same path, new content is a new revision" true.
    content/package/ContentCandidateLedger.cpp
    content/package/WhaleContentPackageUninstall.cpp
    # P0-07: 库行为什么被跳过。纯逻辑,本机可验 —— Load 在 Windows 侧。
    content/package/WhaleLibraryRowFilter.cpp
    content/render/WhaleRenderGraph.cpp
    content/render/WhalePostProcessCompiler.cpp
    content/render/WhalePostProcessShaderLibrary.cpp
    content/render/WhaleSpriteMaterialPolicy.cpp
    # Lives under render/d3d11/ but is compiled into WhaleDesKCore, not into
    # WhaleDesKSceneD3D11: it is the platform-free half of the D3D11 render-target and
    # texture-loader *rules* (dimension arithmetic, path policy, and their self-tests).
    # It used to sit in .cpp files that include <d3d11.h>, which is why those pure
    # assertions could only run where a D3D11 toolchain existed. See the file header.
    content/render/d3d11/WhaleD3D11RenderPolicy.cpp
    content/scene/WhaleSceneModel.cpp
    content/serialization/WhaleSceneSerializer.cpp
    content/runtime/WhaleParticleRuntime.cpp
    content/runtime/WhaleAnalyticParticleField.cpp
    content/runtime/WhaleSceneFrameScheduler.cpp
    content/runtime/WhaleSceneRuntime.cpp
    content/runtime/WhaleSceneRuntimeModel.cpp
    # WALL-03 的宿主侧时间策略:暂停恢复、平滑参数过渡、动画连续性审查。
    # 与上面三个同为纯逻辑(不 import Windows 头),所以既进产品,也能被
    # run-pure-logic-tests.sh 真实编译并运行 —— 而它管的恰好是三件
    # "只有接了宿主才看得见"的事:被挡住时动画不跳、拖滑块时壁纸跟手、
    # 循环轨道的接缝不断。不进 WhaleDesKCore 的话,产品里就没有它,
    # 而测试会变成在一份没人用的实现上跑。
    content/runtime/WhaleSceneTimelinePolicy.cpp
    # WPRO-01:组件归一化几何在真实屏幕上的像素盒与宽高比裁决。纯算术(不碰盘、
    # 不 import Windows 头),所以既进产品,也能被 run-pure-logic-tests.sh 真实编译
    # 并运行。它回答的是写 manifest 时答不上来的那个问题 —— "这个组件到底有多大"。
    desktop/widgets/WhaleWidgetGeometry.cpp
    # P0-04:Native 组件"同一 preset 同一显示器只一个实例"的去重规则。
    # 纯字符串逻辑,所以本机可跑 —— 原先它是 DesktopWidgetStore.cpp 匿名命名空间里的
    # 文件局部符号,而那个文件为了 UTF-16 配置持久化 import 了 windows.h。
    desktop/widgets/WidgetIdentityRules.cpp
    # WPRO-05:组件下一次该什么时候重画。失败必须退避 —— 宿主的 nextRefreshAt
    # 只在成功绘制时被赋值,失败路径一条都不赋,而 0 的含义是"随时都该画",
    # 于是画不出来的组件每 16ms 重试一次。纯逻辑,本机可验。
    desktop/widgets/WidgetRefreshPolicy.cpp
    content/shader/WhaleGpuParameterBlock.cpp
    content/shader/WhaleShaderContract.cpp
    desktop/control/DesktopControlService.cpp
    desktop/control/DesktopContentPackageLifecycle.cpp
    desktop/control/ContentCreatorBridge.cpp
    # CCA-02 的创作工作流状态机。它刻意不 include 任何 Windows 头,所以既能在
    # Windows 上编,也能被 run-pure-logic-tests.sh 真实编译并运行 —— 否则
    # "状态机有测试"会退化成"状态机有一份只能在真机走查的代码"。
    desktop/control/CreationWorkflow.cpp
    # CCA-04: creator workspace policy. Pure logic: it only judges, it never
    # touches the filesystem, so 'reject cross-work / out-of-bounds / reparse
    # point / code artifacts' can be really executed on every round.
    desktop/control/CreatorWorkspacePolicy.cpp
    # CCA-04: the write transaction. It is the only place that can keep
    # "an error leaves no half-written file" true, and it is pure logic so that
    # failure path runs everywhere rather than only on Windows.
    desktop/control/CreatorPackageTransaction.cpp
    # CCA-04: the creator tool registry -- one list of names that Pi's --tools
    # and the worker's allow table both derive from, so the two cannot drift
    # into a failure nobody can see.
    desktop/control/CreatorToolRegistry.cpp
    # CCA-03: the on-demand creation runtime's scheduler. Pure logic so the
    # "at most one live creation" invariant is asserted everywhere, not once on
    # a Windows box -- it is exactly what keeps processes from stacking up.
    desktop/control/CreatorRuntimeScheduler.cpp
    # CCA-07: bounded auto-repair. Pure logic so "stop at the limit" and
    # "never repair after cancel" are asserted, not remembered.
    desktop/control/CreationRepairPlanner.cpp
    # CCA-04: the worker-side dispatch. It owns the host-side binding (session and
    # workspace come from the host's own environment and the workspace's state
    # file, never from the model's arguments) and it states explicitly which tools
    # can actually execute -- so an unimplemented one reads as "not available"
    # instead of being mistaken for a rejection.
    desktop/control/CreatorToolWorker.cpp
    # CCA-04: the on-disk session state. The tool worker is a fresh process per
    # call, so stage/epoch/cancel must live in the workspace for it to read.
    desktop/control/CreatorWorkspaceState.cpp
    # Shared JSON string-field reader. Extracted from PiRuntime so the creator
    # worker and the RPC reader cannot disagree about how a package's content
    # field is decoded.
    content/package/ContentJsonStringField.cpp
    # CCA-05: structural validation of a candidate. Pure logic so every rule it
    # applies is asserted on every machine, not once on a Windows box.
    content/package/ContentPackageValidator.cpp
    # CCA-05: parses and verifies the structured candidate receipt, so the
    # dialog stops guessing which package the model meant from its prose.
    content/package/ContentCandidateReceipt.cpp
    # CCA-08: render-evidence records and their integrity rules. Pure logic so
    # "a failure must not be replaced by a cover image" is asserted without a GPU.
    content/package/RenderEvidence.cpp
    # CCA-05: reads what a turn produced and says which credential it used,
    # so a verified receipt outranks a path scraped out of prose.
    desktop/control/CreatorReplyInterpreter.cpp
    desktop/control/ContentApplyRecovery.cpp
    desktop/control/CreationDraftStore.cpp
    content/package/VisualReviewGate.cpp
    content/package/ContentReleaseGate.cpp
    # CCA-03:Pi 的启动配置。Chat 与 Creator 各一份,分开目录/session/cwd/
    # 扩展文件/工具 allowlist。放进 Core 是因为 PiRuntime 在 Core 里。
    desktop/control/PiLaunchProfile.cpp
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
    desktop/widgets/TodayTaskPresentation.cpp
    desktop/widgets/WidgetDataActionContract.cpp
    desktop/wallpaper/library/WallpaperPackage.cpp
    desktop/wallpaper/library/WallpaperLibrary.cpp
    desktop/wallpaper/library/WallpaperLibraryWeb.cpp
    desktop/wallpaper/library/BuiltinWallpaperCatalog.cpp
    desktop/wallpaper/monitor/WallpaperMonitorLayout.cpp
    desktop/wallpaper/monitor/WallpaperMonitorAssignments.cpp
    search/AppSearch.cpp
    search/SearchTextScoring.cpp
    search/SearchDedupPolicy.cpp
    search/WhaleSearchGeneration.cpp
    search/WhaleFileSearchNotice.cpp
    search/WhaleGozRecovery.cpp
    search/WhaleSearchHandoff.cpp
    ai/pi/WhaleTurnReplyScope.cpp
    desktop/shell/WhaleDesKtopBandOrder.cpp
    content/package/WhaleLibraryRestoreMerge.cpp
    desktop/widgets/WhaleWidgetRowAdmission.cpp
    ai/agent/WhaleIniReadLimit.cpp
    desktop/control/WhaleCreatorPathScope.cpp
    ui/ai/WhaleProfileSwitchGuard.cpp
    search/GozSearch.cpp
)

set(WHALEDESK_APP_SOURCES
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

set(WHALEDESK_HARNESS_CORE_SOURCES
    harness/HarnessProcessManager.cpp
    harness/HarnessSettingsBridge.cpp
)

# The 2D scene renderer is a library, not part of the wallpaper EXE's source list, so
# that the D2D renderer SelfTest target can link it. Two CMake owners for one .cpp is
# a path-layout-contract violation (scripts/verify-path-layout-contract.ps1), and the
# contract's own prescribed remedy is a library target.
# P0-08 后半句:锁持有权记录、裁决,以及宿主侧那个合流点。**独立成库,不是塞进
# WHALEDESK_CORE_SOURCES** —— 塞错 list 的后果已经真发生过一次:`HarnessHost.cpp`
# 调 `lock_host::ExistingOwnerIsHealthy`,而 `WhaleDesKHarness` 链的是
# `WhaleDesKHarnessCore` 而不是 `WhaleDesKCore`,于是
#   HarnessHost.obj : error LNK2019: unresolved external symbol
#     whaledesk::lock_host::ExistingOwnerIsHealthy(...)  [WhaleDesKHarness.vcxproj]
#   WhaleDesKHarness.exe : fatal error LNK1120: 1 unresolved externals
# `WhaleDesKHarness.exe` 装不出来,`cmake --install` 报 `file INSTALL cannot find`,
# ARM64 Fast Dev 与 x64 Package 两条 CI 连红六次。
#
# 做成库而不是把三个 .cpp 同时写进两个 list:写进两个 list 就是一个 .cpp 两个
# CMake owner,那是 path-layout-contract 违规;而 `WhaleDesK` 同时链 WhaleDesKCore 与
# WhaleDesKHarnessCore,重复定义会直接在链接期炸掉。库是这个契约自己规定的解法
# (见下面 WHALEDESK_SCENE2D_SOURCES 的同一段说明)。
set(WHALEDESK_LOCK_OWNERSHIP_SOURCES
    ai/agent/WhaleLockOwnership.cpp
    ai/agent/WhaleLockRecord.cpp
    ai/agent/WhaleLockOwnershipHost.cpp
)

set(WHALEDESK_SCENE2D_SOURCES
    content/render/d2d/WhaleD2DTextureLoader.cpp
    content/render/d2d/WhaleSceneD2DRenderer.cpp
)

# Same reasoning as WHALEDESK_SCENE2D_SOURCES, one directory over.
# WhaleSceneD3D11Renderer::SelfTest() and the three helpers it drives currently have no
# caller at all. The only way to reach them was to compile these four .cpp a second time
# inside a test target, and each implementation file has exactly one CMake owner — so the
# tests stayed dark rather than the rule being broken. A library target is the
# path-layout contract's own prescribed remedy for this.
#
# The note about WhaleD3D11TextureLoader is carried over verbatim from where these four
# used to sit in WHALEDESK_WALLPAPER_SOURCES; it records a real LNK2019 and still holds.
set(WHALEDESK_SCENE_D3D11_SOURCES
    content/render/d3d11/WhaleD3D11ParticleRenderer.cpp
    content/render/d3d11/WhaleD3D11RenderTarget.cpp
    # WhaleSceneD3D11Renderer.cpp references WhaleD3D11TextureLoader::LoadImageW and
    # ::SelfTestPathPolicy. Listing the loader is what makes those symbols exist at
    # link time; omitting it produced LNK2019/LNK1120 in every packaging workflow.
    content/render/d3d11/WhaleD3D11TextureLoader.cpp
    content/render/d3d11/WhaleSceneD3D11Renderer.cpp
)

# Same reasoning as WHALEDESK_SCENE2D_SOURCES and WHALEDESK_SCENE_D3D11_SOURCES, one
# domain over. WhaleWallpaperAudioTap is the Windows half of the Scene input bus, and
# it is the only translation unit in WHALEDESK_WALLPAPER_SOURCES that had never been
# executed by anything: WhaleDesKWallpaper.exe references it, so it compiled and
# linked, and no test could reach it because a test cannot link an executable.
# Compiling it a second time inside a test target is what the path-layout contract's
# "one CMake owner" rule exists to prevent, so a library target is the remedy —
# exactly the one P3-6 used for the D3D11 renderers.
set(WHALEDESK_WALLPAPER_AUDIO_SOURCES
    desktop/wallpaper/monitor/WhaleWallpaperAudioTap.cpp
)

set(WHALEDESK_WALLPAPER_SOURCES
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
    desktop/widgets/ContentWidgetHost.cpp
    desktop/widgets/OfficialWidgetContentPainter.cpp

    desktop/wallpaper/render/WallpaperScaling.cpp
    desktop/wallpaper/render/VideoWallpaperPlayer.cpp
    desktop/wallpaper/render/VideoWallpaperSet.cpp

    ui/wallpaper/WallpaperLibraryWindowV2.cpp
    ui/wallpaper/ContentPackageUiBridge.cpp
    ui/wallpaper/ContentPackageManagerDialog.cpp
    ui/wallpaper/ContentSkillBrowserDialog.cpp
    ui/wallpaper/ContentWidgetSettingsDialog.cpp
    ui/wallpaper/ContentWidgetPreviewRenderer.cpp
    ui/wallpaper/TodayTaskEditorDialog.cpp
    ui/settings/DesktopAiSettingsPage.cpp
)

set(WHALEDESK_HARNESS_SOURCES
    harness/HarnessHost.cpp
    harness/HarnessBundledRuntimeBootstrap.cpp
)

set(WHALEDESK_WINDOWS_RESOURCES)
if(WIN32)
    enable_language(RC)
    list(APPEND WHALEDESK_WINDOWS_RESOURCES
        "${CMAKE_SOURCE_DIR}/assets/app/WhaleDesK.rc"
    )
endif()

add_library(WhaleDesKCore STATIC ${WHALEDESK_CORE_SOURCES})
add_library(WhaleDesKHarnessCore STATIC ${WHALEDESK_HARNESS_CORE_SOURCES})
add_library(WhaleDesKLockOwnership STATIC ${WHALEDESK_LOCK_OWNERSHIP_SOURCES})
add_library(WhaleDesKScene2D STATIC ${WHALEDESK_SCENE2D_SOURCES})
add_library(WhaleDesKSceneD3D11 STATIC ${WHALEDESK_SCENE_D3D11_SOURCES})
add_library(WhaleDesKWallpaperAudio STATIC ${WHALEDESK_WALLPAPER_AUDIO_SOURCES})
add_executable(WhaleDesK WIN32 ${WHALEDESK_APP_SOURCES} ${WHALEDESK_WINDOWS_RESOURCES})
add_executable(WhaleDesKWallpaper WIN32 ${WHALEDESK_WALLPAPER_SOURCES} ${WHALEDESK_WINDOWS_RESOURCES})
add_executable(WhaleDesKHarness WIN32 ${WHALEDESK_HARNESS_SOURCES} ${WHALEDESK_WINDOWS_RESOURCES})
add_executable(WhaleDesKContentWebReplacementTest tests/ContentWebReplacementContinuity.cpp)
add_executable(WhaleDesKContentSkillLoadingTest tests/ContentSkillLoading.cpp)
add_executable(WhaleDesKMediaPackageTest tests/MediaWallpaperPackage.cpp)
# No Windows dependency: the input bus core is pure logic, so this gate runs on
# every build rather than only where audio hardware exists.
add_executable(WhaleDesKInputBusCoreTest tests/InputBusCore.cpp)
add_executable(WhaleDesKInputBusPublisherTest tests/InputBusPublisher.cpp)
add_executable(WhaleDesKAudioIngressTest tests/AudioIngress.cpp)
add_executable(WhaleDesKBindingResponseTest tests/BindingResponse.cpp)
add_executable(WhaleDesKSceneRuntimeTest tests/WhaleSceneRuntimeTest.cpp)
add_executable(WhaleDesKSceneSpatial3DTest tests/SceneSpatial3D.cpp)
# Pure logic: reproduces the host's per-monitor attribution arithmetic against a fake
# topology, so the multi-monitor decision is testable without real monitors.
add_executable(WhaleDesKPointerAttributionTest tests/PointerAttribution.cpp)
# Pure logic: spriteRenderer texture contract validation, no Windows needed.
add_executable(WhaleDesKSpriteTextureContractTest tests/SpriteTextureContract.cpp)
# Windows-only: drives the real D2D renderer against a real package and reads pixels
# back. Its SelfTest had no caller at all before this target existed.
add_executable(WhaleDesKSceneD2DRendererTest tests/SceneD2DRenderer.cpp)
# Pure logic: pins the textured-sprite scene fixture's schema (including the
# assetReference-as-a-bare-string shape) on every commit, without a render target.
# scripts/verify-scene-fixture-parity.sh keeps it identical to the copy the Windows
# SelfTest renders.
add_executable(WhaleDesKSceneTextureFixtureTest tests/SceneTextureFixture.cpp)
# Pure logic: walks the three shipped .mdwall packages through package load ->
# deserialize -> validate -> initialize -> asset-database build. It is what makes
# "the built-in wallpapers actually resolve their images" a checked fact instead of an
# assumption, and it is where the missing-art blocker on NeonCity/MysticMoon is pinned.
add_executable(WhaleDesKBuiltinWallpaperPackagesTest tests/BuiltinWallpaperPackages.cpp)
# Pure logic: WhaleSceneSerializer::SelfTest had no caller anywhere in the repository.
# 120 lines of assertions, concentrated on particle emitters, and none of them ran.
add_executable(WhaleDesKSceneSerializerSelfTest tests/SceneSerializerSelfTest.cpp)
# Pure logic: runs the content-layer SelfTests that were reachable only through
# WhaleSceneD3D11Renderer::SelfTest, an aggregator with no caller. Seven of them,
# ~290 lines, and none of them ran on any machine.
add_executable(WhaleDesKContentSelfTests tests/ContentSelfTests.cpp)
# Pure logic: pins the host->page audio envelope against the shim's contract — field
# names, array lengths, value ranges, and that the decimal point survives a locale
# whose decimal separator is a comma. The page-side half is covered by
# tests/WebAudioBridge.mjs; the two halves had nothing checking them against each other.
add_executable(WhaleDesKWebAudioEnvelopeTest tests/WebAudioEnvelope.cpp)
# Windows-only: the four self-tests that hung off WhaleSceneD3D11Renderer::SelfTest() with
# no caller. P3-6 step 2, and it comes after step 1 on purpose: the WhaleDesKSceneD3D11
# library had to be proved to compile and link under MSVC before anything was allowed to
# depend on it, otherwise a link failure could not be attributed to either change.
add_executable(WhaleDesKSceneD3D11Test tests/SceneD3D11Renderer.cpp)
# The first thing that actually renders a textured sprite through the D3D11 backend and
# reads the pixels back. Every other gate proves that path compiles and links; an HLSL
# shader is compiled at runtime, so "it linked" never meant "it draws".
add_executable(WhaleDesKSceneD3D11TexturedSpriteTest tests/SceneD3D11TexturedSprite.cpp)
# Pure logic: the two render backends must agree about which SpriteRenderers they can
# draw. That rule is platform-independent (material + texture -> a draw path), so the
# parity is checkable on every commit even though only one backend can render here.
add_executable(WhaleDesKSpriteMaterialPolicyTest tests/SpriteMaterialPolicy.cpp)
# Windows-only: runs the real WASAPI loopback tap. It had compile-and-link evidence
# from WhaleDesKWallpaper.exe and nothing else — a capture thread that owns a COM
# apartment, a join at Stop(), and a retry loop for machines with no sound card are
# all things only running it can show. The assertions branch on whether the machine
# has a render endpoint and the branch that ran is printed, so "no frame arrived" can
# never be read as either outcome.
add_executable(WhaleDesKWallpaperAudioTapTest tests/WallpaperAudioTap.cpp)
# CCA-02:创作工作流状态机。纯逻辑,不依赖 Windows,所以它同时被
# run-pure-logic-tests.sh 真实运行 —— 状态转换、预算、取消 epoch、迟到消息、
# 版本和幂等都不必等一台 Windows 机器才验。
add_executable(WhaleDesKCreationWorkflowStateTest tests/CreationWorkflowStateTest.cpp)
# CCA-05:候选摘要。同为纯逻辑:摘要的正确性只取决于内容,不取决于平台。
add_executable(WhaleDesKContentCandidateDigestTest tests/ContentCandidateDigestTest.cpp)
# CCA-04: same reasoning -- a pure-logic policy that is really run.
add_executable(WhaleDesKCreatorWorkspacePolicyTest tests/CreatorWorkspacePolicyTest.cpp)
add_executable(WhaleDesKContentCandidateLedgerTest tests/ContentCandidateLedgerTest.cpp)
add_executable(WhaleDesKCreatorPackageTransactionTest tests/CreatorPackageTransactionTest.cpp)
add_executable(WhaleDesKCreatorToolRegistryTest tests/CreatorToolRegistryTest.cpp)
add_executable(WhaleDesKCreatorToolWorkerTest tests/CreatorToolWorkerTest.cpp)
add_executable(WhaleDesKCreatorRuntimeSchedulerTest tests/CreatorRuntimeSchedulerTest.cpp)
add_executable(WhaleDesKContentPackageValidatorTest tests/ContentPackageValidatorTest.cpp)
add_executable(WhaleDesKJsonStringFieldTest tests/JsonStringFieldTest.cpp)
add_executable(WhaleDesKContentCandidateReceiptTest tests/ContentCandidateReceiptTest.cpp)
add_executable(WhaleDesKCreationRepairPlannerTest tests/CreationRepairPlannerTest.cpp)
add_executable(WhaleDesKRenderEvidenceTest tests/RenderEvidenceTest.cpp)
# D-1:取证样本的摘要绑定。与上面那份测的是两侧:RenderEvidenceTest 测判据怎么拒绝坏
# 样本,这份测产出侧怎么保证样本不会坏。宿主侧曾因一个从未赋值的局部 digest 让每帧
# 带空摘要,而内存端口自己填对了摘要,于是单元测试全绿、真机全败。
add_executable(WhaleDesKRenderEvidenceSampleBindingTest tests/RenderEvidenceSampleBindingTest.cpp)
# CAP-01:能力目录。受控查询的四种答案必须可区分,而"仅声明"绝不能报成"支持" ——
# 把它当成实现是最贵的错:坏包看起来完全合法,作者要到桌面上才发现。
add_executable(WhaleDesKCapabilityCatalogTest tests/CapabilityCatalogTest.cpp)
add_executable(WhaleDesKTodayTaskPresentationTest tests/TodayTaskPresentationTest.cpp)
add_executable(WhaleDesKWidgetDataActionContractTest tests/WidgetDataActionContractTest.cpp)
# CAP-03:随产品发行的包必须通过包级校验。BuiltinWallpaperPackages 走的是 load →
# deserialize → runtime validate 那条链,它**不跑 ContentPackageValidator**,而包级校验
# 正是宣言层(entry 布局、capabilities 是否在目录里、代码产物)。这个门第一次跑就发现
# 校验器的 entry 规则把 8 个发行包全拒了:它只认 scene/scene.json,而发行包用 scene.json。
add_executable(WhaleDesKShippedPackagesValidate tests/ShippedPackagesValidate.cpp)
add_executable(WhaleDesKSearchRankingBaselineTest tests/SearchRankingBaselineTest.cpp)
# SEARCH-02 的另一半:去重。排序已经提成 SearchTextScoring 并在本机有了断言,去重那行
# `Lower(name + L"|" + target)` 此前还钉在 AppSearch.cpp 里 —— 与排序同一个失败类
# (用户看得见),却因为住在一个要 windows.h 的文件里而本机一行都跑不到。
add_executable(WhaleDesKSearchDedupPolicyTest tests/SearchDedupPolicyTest.cpp)
# SEARCH-03「快速输入不展示过期结果」的代号算术。判定散在 GozSearch.cpp 的三行里,
# 而那个文件要 windows.h(命名管道、SendMessageTimeoutW),本机一行都跑不到。
add_executable(WhaleDesKSearchGenerationTest tests/SearchGenerationTest.cpp)
# SEARCH-04「可诊断」那一半:三桶文件搜索状态与三句文案。内联在 SearchWindow.cpp 里
# 时本机一行都跑不到,而那是用户在文件搜索坏掉时唯一能拿到的信息。
add_executable(WhaleDesKFileSearchNoticeTest tests/FileSearchNoticeTest.cpp)
# SEARCH-04「可恢复」那一半:恢复尝试实际发生了什么。此前 EnsurePipeAvailable
# 把每一步观测都当场丢掉了、只回一个 bool,于是提示只能猜"可能服务没起来,
# 也可能查询超时"。判定纯逻辑化之后,界面那句话才说得准。
add_executable(WhaleDesKGozRecoveryTest tests/GozRecoveryTest.cpp)
# SEARCH-05「Search → AI 连续上下文」:搜索框那段词怎么进 AI 面板。判定内联在
# ConversationPanelImpl.inc 里,而那个文件要 <windows.h>,本机一行都跑不到。
add_executable(WhaleDesKSearchHandoffTest tests/SearchHandoffTest.cpp)
# AI 面板三轮消息的代号比对。此行此前是三处一字不差的副本,住在
# ConversationPanelImpl.inc(要 <windows.h>)里,本机一行都跑不到。
add_executable(WhaleDesKTurnReplyScopeTest tests/TurnReplyScopeTest.cpp)
# STAB-02/P0-05「Explorer restart 恢复」的层级契约。判定内联在 DesktopShellHost.cpp
# 里时本机一行都跑不到,而它两个方向的失败都用户看得见(没人修 / 每跳都修到壁纸闪)。
add_executable(WhaleDesKDesktopBandOrderTest tests/DesktopBandOrderTest.cpp)
# P0-07「库状态一致恢复」的合并策略。此前在 WallpaperLibrary::Load 里有两份
# 一字不差的副本,加字段时两份都会安静地漏掉它,而 Load 照常返回 true。
add_executable(WhaleDesKLibraryRestoreMergeTest tests/LibraryRestoreMergeTest.cpp)
# P0-09「升级不丢组件布局」的入账判定。此前 DesktopWidgetStore::Load 里三种拒绝
# 加两种去重结局共用一个 continue,而 Load 照常返回 true。
add_executable(WhaleDesKWidgetRowAdmissionTest tests/WidgetRowAdmissionTest.cpp)
# P0-09「升级不丢 API profile」的读取边界。Win32 在缓冲区放不下时不报错,
# 而 ReadIni / ProfileSections 原来都把返回值丢掉了 —— baseUrl 截断意味着
# 请求带着 Key 打到另一台主机,段名清单截断意味着整个 profile 消失。
add_executable(WhaleDesKIniReadLimitTest tests/IniReadLimitTest.cpp)
# CREATE-06「修改正确 workspace,不新建错误作品」。此前 InspectForGeneratedPackage
# 扫到什么就用什么,而 DialogState 里压根没有 workspaceRoot 字段 —— 模型在回复里
# 提到别处的路径时,那个包会成为本轮结果并可应用到桌面。
add_executable(WhaleDesKCreatorPathScopeTest tests/CreatorPathScopeTest.cpp)
# AI-05「当前任务不静默换 Provider」。这段判定原本是两份一字不差的副本
# (ConversationPanelImpl.inc 与 ContentCreatorDialog.cpp 各一份),`&& !busy`
# 那句豁免没有任何地方写过理由,而它恰好让轮次中途换 profile 成为可能。
add_executable(WhaleDesKProfileSwitchGuardTest tests/ProfileSwitchGuardTest.cpp)
# CAP-02:作者照抄的那两个示例包必须走得通整条链。BuiltinWallpaperPackages 只覆盖三个
# 内置壁纸,ShippedPackagesValidate 只走包级校验 —— examples/content/* 两头都不覆盖,
# 而它坏了比一个用户包坏了更糟(它是教学的入口)。
add_executable(WhaleDesKExamplePackagesLoad tests/ExamplePackagesLoad.cpp)
# WALL-03:宿主侧时间策略(暂停恢复/平滑过渡/循环连续性)与发行内容动画连续性。
# WhaleSceneRuntimeTest 钉"某一刻场景该是什么值",这两个钉"宿主怎么把时间喂进去、
# 以及喂进去的东西看得见地连续吗"—— 后者此前为零。
add_executable(WhaleDesKWhaleSceneTimelinePolicyTest tests/WhaleSceneTimelinePolicyTest.cpp)
add_executable(WhaleDesKShippedAnimationContinuity tests/ShippedAnimationContinuity.cpp)
# WPRO-01:组件归一化几何在真实屏幕上的像素盒,以及随产品发行的组件在 8 块参考
# 屏幕上实际是什么比例。算术是纯的(不 import Windows 头),所以本机真实运行。
add_executable(WhaleDesKWidgetGeometryTest tests/WidgetGeometryTest.cpp)
add_executable(WhaleDesKShippedWidgetGeometry tests/ShippedWidgetGeometry.cpp)
# WPRO-05:失败退避。它修的是一直烧 CPU 的那个失败循环。
add_executable(WhaleDesKWidgetRefreshPolicyTest tests/WidgetRefreshPolicyTest.cpp)
# CAP-03:每个声明的资产都有人引用,同一份字节不打两遍。走真实加载链 +
# WhaleAssetDatabase::Build —— 验收项"资产不重复打包"此前没有任何门。
add_executable(WhaleDesKShippedPackageAssets tests/ShippedPackageAssets.cpp)
# P0-07:一次请求的 URL 和它用的凭据必须同源。修的是"本地命令换过主机之后,
# 原来那个 profile 的 Key 被发到另一台服务上去"。
add_executable(WhaleDesKAgentConfigAuthorityTest tests/AgentConfigAuthorityTest.cpp)
# AI-03:取消/重试幂等。"已请求取消但 worker 还没退"仍然是忙。
add_executable(WhaleDesKTurnLifecycleTest tests/TurnLifecycleTest.cpp)
# P0-09:旧配置升级不丢会话。
add_executable(WhaleDesKSessionMigrationTest tests/SessionMigrationTest.cpp)
# P0-08 后半句:锁持有权裁决。
add_executable(WhaleDesKLockOwnershipTest tests/LockOwnershipTest.cpp)
add_executable(WhaleDesKWallpaperCyclePolicyTest tests/WallpaperCyclePolicyTest.cpp)
add_executable(WhaleDesKLockRecordTest tests/LockRecordTest.cpp)
add_executable(WhaleDesKLibraryRowFilterTest tests/LibraryRowFilterTest.cpp)
add_executable(WhaleDesKCreatorReplyInterpreterTest tests/CreatorReplyInterpreterTest.cpp)
add_executable(WhaleDesKContentApplyRecoveryTest tests/ContentApplyRecoveryTest.cpp)
add_executable(WhaleDesKCreationDraftStoreTest tests/CreationDraftStoreTest.cpp)
add_executable(WhaleDesKVisualReviewGateTest tests/VisualReviewGateTest.cpp)
add_executable(WhaleDesKContentReleaseGateTest tests/ContentReleaseGateTest.cpp)

add_custom_command(TARGET WhaleDesK POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WhaleDesK>/Assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/app/WhaleWhale.ico"
            "$<TARGET_FILE_DIR:WhaleDesK>/Assets/WhaleWhale.ico"
    VERBATIM
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${WHALEDESK_CORE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${WHALEDESK_HARNESS_CORE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${WHALEDESK_APP_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${WHALEDESK_WALLPAPER_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${WHALEDESK_SCENE2D_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${WHALEDESK_SCENE_D3D11_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${WHALEDESK_WALLPAPER_AUDIO_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${WHALEDESK_HARNESS_SOURCES})

set_source_files_properties(ai/tools/NativeTools.cpp PROPERTIES
    COMPILE_DEFINITIONS "WHALEDESK_NATIVE_TOOLS_IMPL"
)
set_source_files_properties(ai/agent/L3Agent.cpp PROPERTIES
    COMPILE_DEFINITIONS "WHALEDESK_L3_WINHTTP_TRACE"
)
set_source_files_properties(ai/pi/PiRuntime.cpp PROPERTIES
    COMPILE_DEFINITIONS "WHALEDESK_PI_CREDENTIAL_GUARD"
)
set_source_files_properties(desktop/wallpaper/legacy/WallpaperEngineProduction.cpp PROPERTIES
    COMPILE_DEFINITIONS "wWinMain=WhaleDesKWallpaperMain"
)

foreach(target WhaleDesKCore WhaleDesKHarnessCore WhaleDesKLockOwnership WhaleDesKScene2D WhaleDesKSceneD3D11 WhaleDesK WhaleDesKWallpaper WhaleDesKHarness WhaleDesKContentWebReplacementTest WhaleDesKContentSkillLoadingTest WhaleDesKMediaPackageTest WhaleDesKInputBusCoreTest WhaleDesKInputBusPublisherTest WhaleDesKAudioIngressTest WhaleDesKBindingResponseTest WhaleDesKSceneRuntimeTest WhaleDesKSceneSpatial3DTest WhaleDesKPointerAttributionTest WhaleDesKSpriteTextureContractTest WhaleDesKSceneD2DRendererTest WhaleDesKSceneTextureFixtureTest WhaleDesKBuiltinWallpaperPackagesTest WhaleDesKSpriteMaterialPolicyTest WhaleDesKSceneSerializerSelfTest WhaleDesKContentSelfTests WhaleDesKWebAudioEnvelopeTest WhaleDesKSceneD3D11Test WhaleDesKSceneD3D11TexturedSpriteTest WhaleDesKWallpaperAudioTapTest WhaleDesKCreationWorkflowStateTest WhaleDesKContentCandidateDigestTest WhaleDesKCreatorWorkspacePolicyTest WhaleDesKContentCandidateLedgerTest WhaleDesKCreatorPackageTransactionTest WhaleDesKCreatorToolRegistryTest WhaleDesKCreatorToolWorkerTest WhaleDesKCreatorRuntimeSchedulerTest WhaleDesKContentPackageValidatorTest WhaleDesKJsonStringFieldTest WhaleDesKContentCandidateReceiptTest WhaleDesKCreationRepairPlannerTest WhaleDesKRenderEvidenceTest WhaleDesKRenderEvidenceSampleBindingTest WhaleDesKCapabilityCatalogTest WhaleDesKTodayTaskPresentationTest WhaleDesKWidgetDataActionContractTest WhaleDesKShippedPackagesValidate WhaleDesKSearchRankingBaselineTest WhaleDesKExamplePackagesLoad WhaleDesKWhaleSceneTimelinePolicyTest WhaleDesKShippedAnimationContinuity WhaleDesKWidgetGeometryTest WhaleDesKShippedWidgetGeometry WhaleDesKWidgetRefreshPolicyTest WhaleDesKShippedPackageAssets WhaleDesKAgentConfigAuthorityTest WhaleDesKTurnLifecycleTest WhaleDesKSessionMigrationTest WhaleDesKLockOwnershipTest WhaleDesKLockRecordTest WhaleDesKLibraryRowFilterTest WhaleDesKWallpaperCyclePolicyTest WhaleDesKCreatorReplyInterpreterTest WhaleDesKContentApplyRecoveryTest WhaleDesKCreationDraftStoreTest WhaleDesKVisualReviewGateTest WhaleDesKContentReleaseGateTest WhaleDesKWallpaperAudio WhaleDesKSearchDedupPolicyTest WhaleDesKSearchGenerationTest WhaleDesKFileSearchNoticeTest WhaleDesKGozRecoveryTest WhaleDesKSearchHandoffTest WhaleDesKTurnReplyScopeTest WhaleDesKDesktopBandOrderTest WhaleDesKLibraryRestoreMergeTest WhaleDesKWidgetRowAdmissionTest WhaleDesKIniReadLimitTest WhaleDesKCreatorPathScopeTest WhaleDesKProfileSwitchGuardTest)
    target_include_directories(${target} PRIVATE include)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
endforeach()

target_include_directories(WhaleDesK SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_include_directories(WhaleDesKWallpaper SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_include_directories(WhaleDesKHarness SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_include_directories(WhaleDesKCore SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")

if (MSVC)
    target_compile_options(WhaleDesKCore PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKHarnessCore PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKLockOwnership PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKScene2D PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSceneD3D11 PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWallpaperAudio PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesK PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWallpaper PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKHarness PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentWebReplacementTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentSkillLoadingTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKMediaPackageTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKInputBusCoreTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKInputBusPublisherTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKAudioIngressTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKBindingResponseTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSceneRuntimeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSceneSpatial3DTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKPointerAttributionTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSpriteTextureContractTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSceneD2DRendererTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSceneTextureFixtureTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKBuiltinWallpaperPackagesTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSceneSerializerSelfTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentSelfTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWebAudioEnvelopeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSceneD3D11Test PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSceneD3D11TexturedSpriteTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSpriteMaterialPolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWallpaperAudioTapTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreationWorkflowStateTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentCandidateDigestTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreatorWorkspacePolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentCandidateLedgerTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreatorPackageTransactionTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreatorToolRegistryTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreatorToolWorkerTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreatorRuntimeSchedulerTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentPackageValidatorTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKJsonStringFieldTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentCandidateReceiptTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreationRepairPlannerTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKRenderEvidenceTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKRenderEvidenceSampleBindingTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCapabilityCatalogTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKTodayTaskPresentationTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWidgetDataActionContractTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKShippedPackagesValidate PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSearchRankingBaselineTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSearchDedupPolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSearchGenerationTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKFileSearchNoticeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKGozRecoveryTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSearchHandoffTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKTurnReplyScopeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKDesktopBandOrderTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKLibraryRestoreMergeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWidgetRowAdmissionTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKIniReadLimitTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreatorPathScopeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKProfileSwitchGuardTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKExamplePackagesLoad PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWhaleSceneTimelinePolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKShippedAnimationContinuity PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWidgetGeometryTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKShippedWidgetGeometry PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWidgetRefreshPolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKShippedPackageAssets PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKAgentConfigAuthorityTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKTurnLifecycleTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKSessionMigrationTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKLockOwnershipTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKWallpaperCyclePolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKLockRecordTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKLibraryRowFilterTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreatorReplyInterpreterTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentApplyRecoveryTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKCreationDraftStoreTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKVisualReviewGateTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(WhaleDesKContentReleaseGateTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

# 锁持有权链两面都要:WhaleDesKCore 供 app 与 wallpaper,WhaleDesKHarnessCore 供
# harness EXE —— 后者只链 harness core,这也是上面那个 LNK2019 的来处。
target_link_libraries(WhaleDesKCore PUBLIC WhaleDesKLockOwnership)
target_link_libraries(WhaleDesKHarnessCore PUBLIC WhaleDesKLockOwnership)
target_link_libraries(WhaleDesK PRIVATE
    WhaleDesKCore
    WhaleDesKScene2D
    WhaleDesKHarnessCore
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
    windowscodecs
    "${WEBVIEW2_LOADER_STATIC}"
)

target_link_libraries(WhaleDesKWallpaper PRIVATE
    WhaleDesKCore
    WhaleDesKScene2D
    WhaleDesKSceneD3D11
    WhaleDesKWallpaperAudio
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

target_link_libraries(WhaleDesKHarness PRIVATE
    WhaleDesKHarnessCore
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

target_link_libraries(WhaleDesKContentWebReplacementTest PRIVATE
    WhaleDesKCore
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
target_link_libraries(WhaleDesKContentSkillLoadingTest PRIVATE
    WhaleDesKCore
    user32
    shell32
    ole32
    oleaut32
    advapi32
    shlwapi
)

target_link_libraries(WhaleDesKMediaPackageTest PRIVATE
    WhaleDesKCore
    user32
    shell32
    ole32
    advapi32
    shlwapi
)

# WhaleDesKInputBusCoreTest intentionally links nothing: WhaleInputBus.h is pure C++
# with no Windows or library dependency, which is what makes it runnable everywhere.
target_link_libraries(WhaleDesKInputBusPublisherTest PRIVATE
    WhaleDesKCore
)

target_link_libraries(WhaleDesKAudioIngressTest PRIVATE
    WhaleDesKCore
)

# Both drive the real scene runtime and serializer, so they must link the same
# translation units those use.
target_link_libraries(WhaleDesKBindingResponseTest PRIVATE
    WhaleDesKCore
)

target_link_libraries(WhaleDesKSceneRuntimeTest PRIVATE
    WhaleDesKCore
)

target_link_libraries(WhaleDesKSceneSpatial3DTest PRIVATE
    WhaleDesKCore
)

# WhaleSceneModel.cpp lives in WhaleDesKCore; without this line the target compiles and
# then fails to link WhaleSceneModel::Validate.
target_link_libraries(WhaleDesKSpriteTextureContractTest PRIVATE
    WhaleDesKCore
)

target_link_libraries(WhaleDesKSceneTextureFixtureTest PRIVATE
    WhaleDesKCore
)

target_link_libraries(WhaleDesKBuiltinWallpaperPackagesTest PRIVATE
    WhaleDesKCore
)

target_link_libraries(WhaleDesKSpriteMaterialPolicyTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKSceneSerializerSelfTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKContentSelfTests PRIVATE
    WhaleDesKCore
)

# CCA-02 的创作工作流状态机走同一条路:把 desktop/control/CreationWorkflow.cpp
# 编进 WhaleDesKCore,测的是那个真实现,而不是这里再编一份副本。两份拷贝会各自演变,
# 而测试绿的永远是本地那份。
target_link_libraries(WhaleDesKCreationWorkflowStateTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKContentCandidateDigestTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreatorWorkspacePolicyTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKContentCandidateLedgerTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreatorPackageTransactionTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreatorToolRegistryTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreatorToolWorkerTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreatorRuntimeSchedulerTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKContentPackageValidatorTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKJsonStringFieldTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKContentCandidateReceiptTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreationRepairPlannerTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKRenderEvidenceTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKRenderEvidenceSampleBindingTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCapabilityCatalogTest PRIVATE
    WhaleDesKCore
)
# D-2:原生待办卡片的行模型。它钉的是"没有数据就说没有数据"—— 旧实现在 painter 里
# 写死三条待办,单元测试不可能失败,因为被测的是一段常量。
target_link_libraries(WhaleDesKTodayTaskPresentationTest PRIVATE
    WhaleDesKCore
)
# CAP-05:组件数据与动作的公共契约。它钉的是"没有时间戳不算新鲜""空与离线不是一回事"
# "同一个 operationId 只生效一次"这三条 —— 都是"看起来能用"最容易糊过去的判定。
target_link_libraries(WhaleDesKWidgetDataActionContractTest PRIVATE
    WhaleDesKCore
)
# CAP-03:随产品发行的包必须通过包级校验。BuiltinWallpaperPackages 走的是 load →
# deserialize → runtime validate 那条链,它**不跑 ContentPackageValidator**,而包级校验
# 正是宣言层(entry 布局、capabilities 是否在目录里、代码产物)。这个门第一次跑就发现
# 校验器的 entry 规则把 8 个发行包全拒了:它只认 scene/scene.json,而发行包用 scene.json。
target_link_libraries(WhaleDesKShippedPackagesValidate PRIVATE
    WhaleDesKCore
)
# SEARCH-01/02:固定样本上的排序基线与回归。排序规则原先住在 AppSearch.cpp 的匿名
# 命名空间里,而那个文件要 windows.h 才编 —— 用户感知最强的一段反而在本机一行都跑不到。
target_link_libraries(WhaleDesKSearchRankingBaselineTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKSearchDedupPolicyTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKSearchGenerationTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKFileSearchNoticeTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKGozRecoveryTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKSearchHandoffTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKTurnReplyScopeTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKDesktopBandOrderTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKLibraryRestoreMergeTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKWidgetRowAdmissionTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKIniReadLimitTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreatorPathScopeTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKProfileSwitchGuardTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKExamplePackagesLoad PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKWhaleSceneTimelinePolicyTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKShippedAnimationContinuity PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKWidgetGeometryTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKShippedWidgetGeometry PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKWidgetRefreshPolicyTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKShippedPackageAssets PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKAgentConfigAuthorityTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKTurnLifecycleTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKSessionMigrationTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKLockOwnershipTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKWallpaperCyclePolicyTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKLockRecordTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKLibraryRowFilterTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreatorReplyInterpreterTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKContentApplyRecoveryTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKCreationDraftStoreTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKVisualReviewGateTest PRIVATE
    WhaleDesKCore
)
target_link_libraries(WhaleDesKContentReleaseGateTest PRIVATE
    WhaleDesKCore
)

# Links nothing beyond WhaleDesKCore: the envelope builder is a header-only pure
# function over content::inputbus::AudioSpectrumFrame, which is exactly why it
# can run on every machine.
target_link_libraries(WhaleDesKWebAudioEnvelopeTest PRIVATE
    WhaleDesKCore
)



# This one drives the real D2D renderer: it owns a render target, so it links the same
# graphics and codec libraries WhaleDesKWallpaper does. The renderer comes from the
# WhaleDesKScene2D library rather than from listing its .cpp here, because each
# implementation file has exactly one CMake owner.
target_link_libraries(WhaleDesKSceneD2DRendererTest PRIVATE
    WhaleDesKCore
    WhaleDesKScene2D
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
# graphics, compiler and codec libraries WhaleDesKWallpaper does. The renderer comes from
# the WhaleDesKSceneD3D11 library that P3-6 step 1 created — listing its .cpp here instead
# would give one implementation file two CMake owners.
target_link_libraries(WhaleDesKSceneD3D11Test PRIVATE
    WhaleDesKCore
    WhaleDesKSceneD3D11
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

# Same links plus ole32/windowscodecs, which it needs for the WIC PNG it writes as the
# sprite's texture. It owns a real window and a real swap chain, so it is the one target
# here that cannot run anywhere but Windows.
target_link_libraries(WhaleDesKSceneD3D11TexturedSpriteTest PRIVATE
    WhaleDesKCore
    WhaleDesKSceneD3D11
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

# Drives the real capture thread: ole32 for CoInitializeEx/CoUninitialize (the
# apartment the thread owns for exactly as long as it runs), user32 and advapi32 for
# the same reason WhaleDesKWallpaper lists them, and the tap itself from the library
# rather than by listing its .cpp here.
target_link_libraries(WhaleDesKWallpaperAudioTapTest PRIVATE
    WhaleDesKCore
    WhaleDesKWallpaperAudio
    user32
    ole32
    advapi32
    shlwapi
)

