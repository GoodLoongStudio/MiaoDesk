set(WEBVIEW2_ROOT "${CMAKE_SOURCE_DIR}/third_party/webview2")

set(MIAODESK_TARGET_ARCH "${CMAKE_VS_PLATFORM_NAME}")
if(NOT MIAODESK_TARGET_ARCH AND DEFINED ENV{VSCMD_ARG_TGT_ARCH})
    set(MIAODESK_TARGET_ARCH "$ENV{VSCMD_ARG_TGT_ARCH}")
endif()
if(NOT MIAODESK_TARGET_ARCH)
    set(MIAODESK_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()

string(TOLOWER "${MIAODESK_TARGET_ARCH}" MIAODESK_TARGET_ARCH_LOWER)
if(MIAODESK_TARGET_ARCH_LOWER MATCHES "^(arm64|aarch64)$")
    set(WEBVIEW2_ARCH "arm64")
elseif(MIAODESK_TARGET_ARCH_LOWER MATCHES "^(x64|amd64|x86_64)$")
    set(WEBVIEW2_ARCH "x64")
else()
    message(FATAL_ERROR "MiaoDesk supports WebView2 builds on x64 and ARM64 only (detected: ${MIAODESK_TARGET_ARCH}).")
endif()

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_ROOT}/include")
set(WEBVIEW2_LOADER_STATIC "${WEBVIEW2_ROOT}/lib/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib")
if (NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h" OR NOT EXISTS "${WEBVIEW2_LOADER_STATIC}")
    message(FATAL_ERROR "WebView2 SDK is incomplete at ${WEBVIEW2_ROOT} for ${WEBVIEW2_ARCH}.")
endif()

set(MIAODESK_CORE_SOURCES
    ai/agent/L3Agent.cpp
    # P0-07: 一次请求的 URL 与它用的凭据是否同源,以及那个槽名。纯逻辑(不 import
    # Windows 头)才让"本地命令换过主机之后,profile 的 Key 会不会被发到别处去"
    # 这条能在本机验 —— L3Agent.cpp 为了 wincred/winhttp 只能 Windows 上编。
    ai/agent/MiaoAgentConfigAuthority.cpp
    # AI-03: 轮次相位不变量。Stop() 是请求不是完成 —— 取消之后那一轮仍然算忙,
    # 否则第二轮会与还没退完的第一轮共用同一个 Node 进程。纯逻辑,本机可验。
    ai/pi/MiaoTurnLifecycle.cpp
    # P0-09: 旧配置升级时要不要把 AI 会话搬到新身份下。纯逻辑,本机可验 ——
    # L3Agent.cpp 为了 winhttp.h 只能 Windows 上编,而"搬家会话"这条判定必须每轮都跑。
    ai/agent/MiaoSessionMigration.cpp
    # P0-03:壁纸 20 次循环的"该不该重建运行时/重试前要不要摘层"裁决。
    # WallpaperEngine.cpp 自己都不是独立 TU(被 WallpaperEngineProduction.cpp include),
    # 所以这批不变量此前本机一行都跑不到。
    desktop/wallpaper/legacy/MiaoWallpaperCyclePolicy.cpp
    # P0-08 后半句:单实例锁的持有权裁决。OpenMutexW 只回答"锁在不在",
    # 不回答"它属于谁、那个进程还活着吗"—— 于是卡住的持有者看不出来。纯逻辑,本机可验。
    # P0-08 后半句:锁持有权记录的编解码。owner 没有地方发身份的话,
    # 上面那个裁决根本没有输入可用。
    # Native tools live in the core library, not in the app target, so the
    # content-skill gate can link them without compiling a second copy. Each
    # implementation file must keep exactly one CMake owner.
    ai/tools/NativeTools.cpp
    content/input/MiaoAudioCapture.cpp
    content/asset/MiaoAssetDatabase.cpp
    content/binding/MiaoContentDataBinding.cpp
    content/binding/MiaoCapabilityCatalog.cpp
    content/model/ContentWidgetInstanceStore.cpp
    content/model/MiaoContentDefinitionLoader.cpp
    content/model/MiaoContentModel.cpp
    content/model/MiaoWidgetContentCatalog.cpp
    content/package/MiaoContentPackage.cpp
    content/package/MiaoContentPackageCatalog.cpp
    content/package/MiaoContentPackageManager.cpp
    # CCA-05:候选摘要。manifest + scene + 参数 + 引用素材的内容身份,
    # 自带 SHA-256(用已发表测试向量验过),因为摘要要跨进程记录和比较。
    content/package/ContentCandidateDigest.cpp
    # CCA-05: candidate ledger. Host side of the submission contract -- it decides
    # whether a submission is the same revision or a new one, so it is the only
    # place that can keep "same path, new content is a new revision" true.
    content/package/ContentCandidateLedger.cpp
    content/package/MiaoContentPackageUninstall.cpp
    # P0-07: 库行为什么被跳过。纯逻辑,本机可验 —— Load 在 Windows 侧。
    content/package/MiaoLibraryRowFilter.cpp
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
    content/runtime/MiaoAnalyticParticleField.cpp
    content/runtime/MiaoSceneFrameScheduler.cpp
    content/runtime/MiaoSceneRuntime.cpp
    content/runtime/MiaoSceneRuntimeModel.cpp
    # WALL-03 的宿主侧时间策略:暂停恢复、平滑参数过渡、动画连续性审查。
    # 与上面三个同为纯逻辑(不 import Windows 头),所以既进产品,也能被
    # run-pure-logic-tests.sh 真实编译并运行 —— 而它管的恰好是三件
    # "只有接了宿主才看得见"的事:被挡住时动画不跳、拖滑块时壁纸跟手、
    # 循环轨道的接缝不断。不进 MiaoDeskCore 的话,产品里就没有它,
    # 而测试会变成在一份没人用的实现上跑。
    content/runtime/MiaoSceneTimelinePolicy.cpp
    # WPRO-01:组件归一化几何在真实屏幕上的像素盒与宽高比裁决。纯算术(不碰盘、
    # 不 import Windows 头),所以既进产品,也能被 run-pure-logic-tests.sh 真实编译
    # 并运行。它回答的是写 manifest 时答不上来的那个问题 —— "这个组件到底有多大"。
    desktop/widgets/MiaoWidgetGeometry.cpp
    # P0-04:Native 组件"同一 preset 同一显示器只一个实例"的去重规则。
    # 纯字符串逻辑,所以本机可跑 —— 原先它是 DesktopWidgetStore.cpp 匿名命名空间里的
    # 文件局部符号,而那个文件为了 UTF-16 配置持久化 import 了 windows.h。
    desktop/widgets/WidgetIdentityRules.cpp
    # WPRO-05:组件下一次该什么时候重画。失败必须退避 —— 宿主的 nextRefreshAt
    # 只在成功绘制时被赋值,失败路径一条都不赋,而 0 的含义是"随时都该画",
    # 于是画不出来的组件每 16ms 重试一次。纯逻辑,本机可验。
    desktop/widgets/WidgetRefreshPolicy.cpp
    content/shader/MiaoGpuParameterBlock.cpp
    content/shader/MiaoShaderContract.cpp
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
    search/MiaoSearchGeneration.cpp
    search/MiaoFileSearchNotice.cpp
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
# P0-08 后半句:锁持有权记录、裁决,以及宿主侧那个合流点。**独立成库,不是塞进
# MIAODESK_CORE_SOURCES** —— 塞错 list 的后果已经真发生过一次:`HarnessHost.cpp`
# 调 `lock_host::ExistingOwnerIsHealthy`,而 `MiaoDeskHarness` 链的是
# `MiaoDeskHarnessCore` 而不是 `MiaoDeskCore`,于是
#   HarnessHost.obj : error LNK2019: unresolved external symbol
#     miaodesk::lock_host::ExistingOwnerIsHealthy(...)  [MiaoDeskHarness.vcxproj]
#   MiaoDeskHarness.exe : fatal error LNK1120: 1 unresolved externals
# `MiaoDeskHarness.exe` 装不出来,`cmake --install` 报 `file INSTALL cannot find`,
# ARM64 Fast Dev 与 x64 Package 两条 CI 连红六次。
#
# 做成库而不是把三个 .cpp 同时写进两个 list:写进两个 list 就是一个 .cpp 两个
# CMake owner,那是 path-layout-contract 违规;而 `MiaoDesk` 同时链 MiaoDeskCore 与
# MiaoDeskHarnessCore,重复定义会直接在链接期炸掉。库是这个契约自己规定的解法
# (见下面 MIAODESK_SCENE2D_SOURCES 的同一段说明)。
set(MIAODESK_LOCK_OWNERSHIP_SOURCES
    ai/agent/MiaoLockOwnership.cpp
    ai/agent/MiaoLockRecord.cpp
    ai/agent/MiaoLockOwnershipHost.cpp
)

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

# Same reasoning as MIAODESK_SCENE2D_SOURCES and MIAODESK_SCENE_D3D11_SOURCES, one
# domain over. MiaoWallpaperAudioTap is the Windows half of the Scene input bus, and
# it is the only translation unit in MIAODESK_WALLPAPER_SOURCES that had never been
# executed by anything: MiaoDeskWallpaper.exe references it, so it compiled and
# linked, and no test could reach it because a test cannot link an executable.
# Compiling it a second time inside a test target is what the path-layout contract's
# "one CMake owner" rule exists to prevent, so a library target is the remedy —
# exactly the one P3-6 used for the D3D11 renderers.
set(MIAODESK_WALLPAPER_AUDIO_SOURCES
    desktop/wallpaper/monitor/MiaoWallpaperAudioTap.cpp
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
add_library(MiaoDeskLockOwnership STATIC ${MIAODESK_LOCK_OWNERSHIP_SOURCES})
add_library(MiaoDeskScene2D STATIC ${MIAODESK_SCENE2D_SOURCES})
add_library(MiaoDeskSceneD3D11 STATIC ${MIAODESK_SCENE_D3D11_SOURCES})
add_library(MiaoDeskWallpaperAudio STATIC ${MIAODESK_WALLPAPER_AUDIO_SOURCES})
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
# The first thing that actually renders a textured sprite through the D3D11 backend and
# reads the pixels back. Every other gate proves that path compiles and links; an HLSL
# shader is compiled at runtime, so "it linked" never meant "it draws".
add_executable(MiaoDeskSceneD3D11TexturedSpriteTest tests/SceneD3D11TexturedSprite.cpp)
# Pure logic: the two render backends must agree about which SpriteRenderers they can
# draw. That rule is platform-independent (material + texture -> a draw path), so the
# parity is checkable on every commit even though only one backend can render here.
add_executable(MiaoDeskSpriteMaterialPolicyTest tests/SpriteMaterialPolicy.cpp)
# Windows-only: runs the real WASAPI loopback tap. It had compile-and-link evidence
# from MiaoDeskWallpaper.exe and nothing else — a capture thread that owns a COM
# apartment, a join at Stop(), and a retry loop for machines with no sound card are
# all things only running it can show. The assertions branch on whether the machine
# has a render endpoint and the branch that ran is printed, so "no frame arrived" can
# never be read as either outcome.
add_executable(MiaoDeskWallpaperAudioTapTest tests/WallpaperAudioTap.cpp)
# CCA-02:创作工作流状态机。纯逻辑,不依赖 Windows,所以它同时被
# run-pure-logic-tests.sh 真实运行 —— 状态转换、预算、取消 epoch、迟到消息、
# 版本和幂等都不必等一台 Windows 机器才验。
add_executable(MiaoDeskCreationWorkflowStateTest tests/CreationWorkflowStateTest.cpp)
# CCA-05:候选摘要。同为纯逻辑:摘要的正确性只取决于内容,不取决于平台。
add_executable(MiaoDeskContentCandidateDigestTest tests/ContentCandidateDigestTest.cpp)
# CCA-04: same reasoning -- a pure-logic policy that is really run.
add_executable(MiaoDeskCreatorWorkspacePolicyTest tests/CreatorWorkspacePolicyTest.cpp)
add_executable(MiaoDeskContentCandidateLedgerTest tests/ContentCandidateLedgerTest.cpp)
add_executable(MiaoDeskCreatorPackageTransactionTest tests/CreatorPackageTransactionTest.cpp)
add_executable(MiaoDeskCreatorToolRegistryTest tests/CreatorToolRegistryTest.cpp)
add_executable(MiaoDeskCreatorToolWorkerTest tests/CreatorToolWorkerTest.cpp)
add_executable(MiaoDeskCreatorRuntimeSchedulerTest tests/CreatorRuntimeSchedulerTest.cpp)
add_executable(MiaoDeskContentPackageValidatorTest tests/ContentPackageValidatorTest.cpp)
add_executable(MiaoDeskJsonStringFieldTest tests/JsonStringFieldTest.cpp)
add_executable(MiaoDeskContentCandidateReceiptTest tests/ContentCandidateReceiptTest.cpp)
add_executable(MiaoDeskCreationRepairPlannerTest tests/CreationRepairPlannerTest.cpp)
add_executable(MiaoDeskRenderEvidenceTest tests/RenderEvidenceTest.cpp)
# D-1:取证样本的摘要绑定。与上面那份测的是两侧:RenderEvidenceTest 测判据怎么拒绝坏
# 样本,这份测产出侧怎么保证样本不会坏。宿主侧曾因一个从未赋值的局部 digest 让每帧
# 带空摘要,而内存端口自己填对了摘要,于是单元测试全绿、真机全败。
add_executable(MiaoDeskRenderEvidenceSampleBindingTest tests/RenderEvidenceSampleBindingTest.cpp)
# CAP-01:能力目录。受控查询的四种答案必须可区分,而"仅声明"绝不能报成"支持" ——
# 把它当成实现是最贵的错:坏包看起来完全合法,作者要到桌面上才发现。
add_executable(MiaoDeskCapabilityCatalogTest tests/CapabilityCatalogTest.cpp)
add_executable(MiaoDeskTodayTaskPresentationTest tests/TodayTaskPresentationTest.cpp)
add_executable(MiaoDeskWidgetDataActionContractTest tests/WidgetDataActionContractTest.cpp)
# CAP-03:随产品发行的包必须通过包级校验。BuiltinWallpaperPackages 走的是 load →
# deserialize → runtime validate 那条链,它**不跑 ContentPackageValidator**,而包级校验
# 正是宣言层(entry 布局、capabilities 是否在目录里、代码产物)。这个门第一次跑就发现
# 校验器的 entry 规则把 8 个发行包全拒了:它只认 scene/scene.json,而发行包用 scene.json。
add_executable(MiaoDeskShippedPackagesValidate tests/ShippedPackagesValidate.cpp)
add_executable(MiaoDeskSearchRankingBaselineTest tests/SearchRankingBaselineTest.cpp)
# SEARCH-02 的另一半:去重。排序已经提成 SearchTextScoring 并在本机有了断言,去重那行
# `Lower(name + L"|" + target)` 此前还钉在 AppSearch.cpp 里 —— 与排序同一个失败类
# (用户看得见),却因为住在一个要 windows.h 的文件里而本机一行都跑不到。
add_executable(MiaoDeskSearchDedupPolicyTest tests/SearchDedupPolicyTest.cpp)
# SEARCH-03「快速输入不展示过期结果」的代号算术。判定散在 GozSearch.cpp 的三行里,
# 而那个文件要 windows.h(命名管道、SendMessageTimeoutW),本机一行都跑不到。
add_executable(MiaoDeskSearchGenerationTest tests/SearchGenerationTest.cpp)
# SEARCH-04「可诊断」那一半:三桶文件搜索状态与三句文案。内联在 SearchWindow.cpp 里
# 时本机一行都跑不到,而那是用户在文件搜索坏掉时唯一能拿到的信息。
add_executable(MiaoDeskFileSearchNoticeTest tests/FileSearchNoticeTest.cpp)
# CAP-02:作者照抄的那两个示例包必须走得通整条链。BuiltinWallpaperPackages 只覆盖三个
# 内置壁纸,ShippedPackagesValidate 只走包级校验 —— examples/content/* 两头都不覆盖,
# 而它坏了比一个用户包坏了更糟(它是教学的入口)。
add_executable(MiaoDeskExamplePackagesLoad tests/ExamplePackagesLoad.cpp)
# WALL-03:宿主侧时间策略(暂停恢复/平滑过渡/循环连续性)与发行内容动画连续性。
# MiaoSceneRuntimeTest 钉"某一刻场景该是什么值",这两个钉"宿主怎么把时间喂进去、
# 以及喂进去的东西看得见地连续吗"—— 后者此前为零。
add_executable(MiaoDeskMiaoSceneTimelinePolicyTest tests/MiaoSceneTimelinePolicyTest.cpp)
add_executable(MiaoDeskShippedAnimationContinuity tests/ShippedAnimationContinuity.cpp)
# WPRO-01:组件归一化几何在真实屏幕上的像素盒,以及随产品发行的组件在 8 块参考
# 屏幕上实际是什么比例。算术是纯的(不 import Windows 头),所以本机真实运行。
add_executable(MiaoDeskWidgetGeometryTest tests/WidgetGeometryTest.cpp)
add_executable(MiaoDeskShippedWidgetGeometry tests/ShippedWidgetGeometry.cpp)
# WPRO-05:失败退避。它修的是一直烧 CPU 的那个失败循环。
add_executable(MiaoDeskWidgetRefreshPolicyTest tests/WidgetRefreshPolicyTest.cpp)
# CAP-03:每个声明的资产都有人引用,同一份字节不打两遍。走真实加载链 +
# MiaoAssetDatabase::Build —— 验收项"资产不重复打包"此前没有任何门。
add_executable(MiaoDeskShippedPackageAssets tests/ShippedPackageAssets.cpp)
# P0-07:一次请求的 URL 和它用的凭据必须同源。修的是"本地命令换过主机之后,
# 原来那个 profile 的 Key 被发到另一台服务上去"。
add_executable(MiaoDeskAgentConfigAuthorityTest tests/AgentConfigAuthorityTest.cpp)
# AI-03:取消/重试幂等。"已请求取消但 worker 还没退"仍然是忙。
add_executable(MiaoDeskTurnLifecycleTest tests/TurnLifecycleTest.cpp)
# P0-09:旧配置升级不丢会话。
add_executable(MiaoDeskSessionMigrationTest tests/SessionMigrationTest.cpp)
# P0-08 后半句:锁持有权裁决。
add_executable(MiaoDeskLockOwnershipTest tests/LockOwnershipTest.cpp)
add_executable(MiaoDeskWallpaperCyclePolicyTest tests/WallpaperCyclePolicyTest.cpp)
add_executable(MiaoDeskLockRecordTest tests/LockRecordTest.cpp)
add_executable(MiaoDeskLibraryRowFilterTest tests/LibraryRowFilterTest.cpp)
add_executable(MiaoDeskCreatorReplyInterpreterTest tests/CreatorReplyInterpreterTest.cpp)
add_executable(MiaoDeskContentApplyRecoveryTest tests/ContentApplyRecoveryTest.cpp)
add_executable(MiaoDeskCreationDraftStoreTest tests/CreationDraftStoreTest.cpp)
add_executable(MiaoDeskVisualReviewGateTest tests/VisualReviewGateTest.cpp)
add_executable(MiaoDeskContentReleaseGateTest tests/ContentReleaseGateTest.cpp)

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
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${MIAODESK_WALLPAPER_AUDIO_SOURCES})
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

foreach(target MiaoDeskCore MiaoDeskHarnessCore MiaoDeskLockOwnership MiaoDeskScene2D MiaoDeskSceneD3D11 MiaoDesk MiaoDeskWallpaper MiaoDeskHarness MiaoDeskContentWebReplacementTest MiaoDeskContentSkillLoadingTest MiaoDeskMediaPackageTest MiaoDeskInputBusCoreTest MiaoDeskInputBusPublisherTest MiaoDeskAudioIngressTest MiaoDeskBindingResponseTest MiaoDeskSceneRuntimeTest MiaoDeskSceneSpatial3DTest MiaoDeskPointerAttributionTest MiaoDeskSpriteTextureContractTest MiaoDeskSceneD2DRendererTest MiaoDeskSceneTextureFixtureTest MiaoDeskBuiltinWallpaperPackagesTest MiaoDeskSpriteMaterialPolicyTest MiaoDeskSceneSerializerSelfTest MiaoDeskContentSelfTests MiaoDeskWebAudioEnvelopeTest MiaoDeskSceneD3D11Test MiaoDeskSceneD3D11TexturedSpriteTest MiaoDeskWallpaperAudioTapTest MiaoDeskCreationWorkflowStateTest MiaoDeskContentCandidateDigestTest MiaoDeskCreatorWorkspacePolicyTest MiaoDeskContentCandidateLedgerTest MiaoDeskCreatorPackageTransactionTest MiaoDeskCreatorToolRegistryTest MiaoDeskCreatorToolWorkerTest MiaoDeskCreatorRuntimeSchedulerTest MiaoDeskContentPackageValidatorTest MiaoDeskJsonStringFieldTest MiaoDeskContentCandidateReceiptTest MiaoDeskCreationRepairPlannerTest MiaoDeskRenderEvidenceTest MiaoDeskRenderEvidenceSampleBindingTest MiaoDeskCapabilityCatalogTest MiaoDeskTodayTaskPresentationTest MiaoDeskWidgetDataActionContractTest MiaoDeskShippedPackagesValidate MiaoDeskSearchRankingBaselineTest MiaoDeskExamplePackagesLoad MiaoDeskMiaoSceneTimelinePolicyTest MiaoDeskShippedAnimationContinuity MiaoDeskWidgetGeometryTest MiaoDeskShippedWidgetGeometry MiaoDeskWidgetRefreshPolicyTest MiaoDeskShippedPackageAssets MiaoDeskAgentConfigAuthorityTest MiaoDeskTurnLifecycleTest MiaoDeskSessionMigrationTest MiaoDeskLockOwnershipTest MiaoDeskLockRecordTest MiaoDeskLibraryRowFilterTest MiaoDeskWallpaperCyclePolicyTest MiaoDeskCreatorReplyInterpreterTest MiaoDeskContentApplyRecoveryTest MiaoDeskCreationDraftStoreTest MiaoDeskVisualReviewGateTest MiaoDeskContentReleaseGateTest MiaoDeskWallpaperAudio MiaoDeskSearchDedupPolicyTest MiaoDeskSearchGenerationTest MiaoDeskFileSearchNoticeTest)
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
    target_compile_options(MiaoDeskLockOwnership PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskScene2D PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSceneD3D11 PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWallpaperAudio PRIVATE /W4 /permissive- /utf-8 /EHsc)
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
    target_compile_options(MiaoDeskSceneD3D11TexturedSpriteTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSpriteMaterialPolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWallpaperAudioTapTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreationWorkflowStateTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentCandidateDigestTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreatorWorkspacePolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentCandidateLedgerTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreatorPackageTransactionTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreatorToolRegistryTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreatorToolWorkerTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreatorRuntimeSchedulerTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentPackageValidatorTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskJsonStringFieldTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentCandidateReceiptTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreationRepairPlannerTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskRenderEvidenceTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskRenderEvidenceSampleBindingTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCapabilityCatalogTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskTodayTaskPresentationTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWidgetDataActionContractTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskShippedPackagesValidate PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSearchRankingBaselineTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSearchDedupPolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSearchGenerationTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskFileSearchNoticeTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskExamplePackagesLoad PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskMiaoSceneTimelinePolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskShippedAnimationContinuity PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWidgetGeometryTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskShippedWidgetGeometry PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWidgetRefreshPolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskShippedPackageAssets PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskAgentConfigAuthorityTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskTurnLifecycleTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskSessionMigrationTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskLockOwnershipTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskWallpaperCyclePolicyTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskLockRecordTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskLibraryRowFilterTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreatorReplyInterpreterTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentApplyRecoveryTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskCreationDraftStoreTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskVisualReviewGateTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(MiaoDeskContentReleaseGateTest PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

# 锁持有权链两面都要:MiaoDeskCore 供 app 与 wallpaper,MiaoDeskHarnessCore 供
# harness EXE —— 后者只链 harness core,这也是上面那个 LNK2019 的来处。
target_link_libraries(MiaoDeskCore PUBLIC MiaoDeskLockOwnership)
target_link_libraries(MiaoDeskHarnessCore PUBLIC MiaoDeskLockOwnership)
target_link_libraries(MiaoDesk PRIVATE
    MiaoDeskCore
    MiaoDeskScene2D
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
    windowscodecs
    "${WEBVIEW2_LOADER_STATIC}"
)

target_link_libraries(MiaoDeskWallpaper PRIVATE
    MiaoDeskCore
    MiaoDeskScene2D
    MiaoDeskSceneD3D11
    MiaoDeskWallpaperAudio
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

# CCA-02 的创作工作流状态机走同一条路:把 desktop/control/CreationWorkflow.cpp
# 编进 MiaoDeskCore,测的是那个真实现,而不是这里再编一份副本。两份拷贝会各自演变,
# 而测试绿的永远是本地那份。
target_link_libraries(MiaoDeskCreationWorkflowStateTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskContentCandidateDigestTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCreatorWorkspacePolicyTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskContentCandidateLedgerTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCreatorPackageTransactionTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCreatorToolRegistryTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCreatorToolWorkerTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCreatorRuntimeSchedulerTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskContentPackageValidatorTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskJsonStringFieldTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskContentCandidateReceiptTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCreationRepairPlannerTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskRenderEvidenceTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskRenderEvidenceSampleBindingTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCapabilityCatalogTest PRIVATE
    MiaoDeskCore
)
# D-2:原生待办卡片的行模型。它钉的是"没有数据就说没有数据"—— 旧实现在 painter 里
# 写死三条待办,单元测试不可能失败,因为被测的是一段常量。
target_link_libraries(MiaoDeskTodayTaskPresentationTest PRIVATE
    MiaoDeskCore
)
# CAP-05:组件数据与动作的公共契约。它钉的是"没有时间戳不算新鲜""空与离线不是一回事"
# "同一个 operationId 只生效一次"这三条 —— 都是"看起来能用"最容易糊过去的判定。
target_link_libraries(MiaoDeskWidgetDataActionContractTest PRIVATE
    MiaoDeskCore
)
# CAP-03:随产品发行的包必须通过包级校验。BuiltinWallpaperPackages 走的是 load →
# deserialize → runtime validate 那条链,它**不跑 ContentPackageValidator**,而包级校验
# 正是宣言层(entry 布局、capabilities 是否在目录里、代码产物)。这个门第一次跑就发现
# 校验器的 entry 规则把 8 个发行包全拒了:它只认 scene/scene.json,而发行包用 scene.json。
target_link_libraries(MiaoDeskShippedPackagesValidate PRIVATE
    MiaoDeskCore
)
# SEARCH-01/02:固定样本上的排序基线与回归。排序规则原先住在 AppSearch.cpp 的匿名
# 命名空间里,而那个文件要 windows.h 才编 —— 用户感知最强的一段反而在本机一行都跑不到。
target_link_libraries(MiaoDeskSearchRankingBaselineTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskSearchDedupPolicyTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskSearchGenerationTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskFileSearchNoticeTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskExamplePackagesLoad PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskMiaoSceneTimelinePolicyTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskShippedAnimationContinuity PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskWidgetGeometryTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskShippedWidgetGeometry PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskWidgetRefreshPolicyTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskShippedPackageAssets PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskAgentConfigAuthorityTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskTurnLifecycleTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskSessionMigrationTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskLockOwnershipTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskWallpaperCyclePolicyTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskLockRecordTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskLibraryRowFilterTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCreatorReplyInterpreterTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskContentApplyRecoveryTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskCreationDraftStoreTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskVisualReviewGateTest PRIVATE
    MiaoDeskCore
)
target_link_libraries(MiaoDeskContentReleaseGateTest PRIVATE
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

# Same links plus ole32/windowscodecs, which it needs for the WIC PNG it writes as the
# sprite's texture. It owns a real window and a real swap chain, so it is the one target
# here that cannot run anywhere but Windows.
target_link_libraries(MiaoDeskSceneD3D11TexturedSpriteTest PRIVATE
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

# Drives the real capture thread: ole32 for CoInitializeEx/CoUninitialize (the
# apartment the thread owns for exactly as long as it runs), user32 and advapi32 for
# the same reason MiaoDeskWallpaper lists them, and the tap itself from the library
# rather than by listing its .cpp here.
target_link_libraries(MiaoDeskWallpaperAudioTapTest PRIVATE
    MiaoDeskCore
    MiaoDeskWallpaperAudio
    user32
    ole32
    advapi32
    shlwapi
)

