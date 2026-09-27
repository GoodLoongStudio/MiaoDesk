#include "miaodesk/PiNativeToolsExtension.h"
#include "miaodesk/AppPaths.h"
#include "miaodesk/CreatorToolRegistry.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace miaodesk {
namespace {

constexpr wchar_t kNativeToolHostEnvironment[] = L"MIAODESK_NATIVE_TOOL_HOST";


fs::path ModulePath() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    path.resize(count);
    return fs::path(path);
}

fs::path PiAgentDirectory() {
    return paths::PiAgentRoot();
}

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

constexpr std::string_view kExtensionSourcePart1 = R"PIEXT(import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawn } from "node:child_process";

const HOST = process.env.MIAODESK_NATIVE_TOOL_HOST ?? "";
const DEFAULT_IMAGE_MODEL = "google/gemini-2.5-flash-image";
// The image provider is configurable rather than hardcoded so a local inference
// endpoint can be selected. The default is "openrouter" purely for backward
// compatibility with existing behaviour.
//
// MIAODESK_IMAGE_PROVIDER / BASE_URL / MODEL are exported by the host from the
// active API profile. Generic OpenAI-compatible providers are called directly by
// MiaoDesk so local image generation does not depend on pi-ai recognising a custom
// provider name. Named cloud providers keep using pi-ai compat.
const IMAGE_PROVIDER = (process.env.MIAODESK_IMAGE_PROVIDER ?? "").trim();
const IMAGE_BASE_URL = (process.env.MIAODESK_IMAGE_BASE_URL ?? "").trim();
const IMAGE_MODEL = (process.env.MIAODESK_IMAGE_MODEL ?? "").trim();
// A loopback endpoint is the product's own local inference server, which the
// profile already treats as keyless. Treating it as keyless here too is what makes
// "run everything locally" work without inventing a token.
const LOOPBACK_HOSTS = ["127.0.0.1", "localhost", "[::1]", "::1"];
function isLoopback(baseUrl) { return LOOPBACK_HOSTS.some((h) => baseUrl.includes(h)); }
const TOOL_NAMES = [
  "settings_open",
  "ppt_create",
  "file_create",
  "folder_list",
  "file_open",
  "image_generate",
  "wallpaper_validate_package",
  "wallpaper_state_get",
  "desktop_widget_list",
  "desktop_preview_wallpaper",
  "desktop_preview_examples",
  "content_skill_get",
] as const;

function textResult(text: string) {
  return { content: [{ type: "text" as const, text }], details: {} };
}

async function runNativeTool(tool: string, params: unknown, signal?: AbortSignal): Promise<string> {
  if (!HOST) throw new Error("MiaoDesk native tool host is unavailable.");
  const work = await mkdtemp(join(tmpdir(), "miaodesk-pi-tool-"));
  const input = join(work, "input.json");
  const output = join(work, "output.txt");
  try {
    await writeFile(input, JSON.stringify(params ?? {}), "utf8");
    await new Promise<void>((resolve, reject) => {
      const child = spawn(HOST, ["--native-tool-worker", tool, input, output], {
        windowsHide: true,
        stdio: "ignore",
      });
      let finished = false;
      let timer: ReturnType<typeof setTimeout> | undefined;
      const finish = (error?: Error) => {
        if (finished) return;
        finished = true;
        if (timer) clearTimeout(timer);
        signal?.removeEventListener("abort", abort);
        error ? reject(error) : resolve();
      };
      const abort = () => {
        try { child.kill(); } catch {}
        finish(new Error(`MiaoDesk native tool cancelled: ${tool}`));
      };
      timer = setTimeout(() => {
        try { child.kill(); } catch {}
        finish(new Error(`MiaoDesk native tool timed out: ${tool}`));
      }, 30000);
      if (signal?.aborted) return abort();
      signal?.addEventListener("abort", abort, { once: true });
      child.once("error", error => finish(error));
      child.once("exit", code => code === 0
        ? finish()
        : finish(new Error(`MiaoDesk native tool worker exited with code ${code ?? "unknown"}: ${tool}`)));
    });
    const raw = await readFile(output, "utf8");
    const newline = raw.indexOf("\n");
    if (newline < 1) throw new Error(`MiaoDesk native tool returned an invalid result: ${tool}`);
    const success = raw.slice(0, newline).trim() === "1";
    const message = raw.slice(newline + 1).trim() ||
      (success ? "MiaoDesk native tool completed." : "MiaoDesk native tool failed.");
    if (!success) throw new Error(message);
    return message;
  } finally {
    await rm(work, { recursive: true, force: true }).catch(() => {});
  }
}

function safeFileStem(value: string): string {
  const cleaned = value
    .replace(/[<>:"/\\|?*\x00-\x1f]/g, "_")
    .replace(/[. ]+$/g, "")
    .trim()
    .slice(0, 96);
  return cleaned || `MiaoDesk-Image-${Date.now()}`;
}

function extensionForMime(mimeType: string): string {
  const lower = mimeType.toLowerCase();
  if (lower.includes("jpeg") || lower.includes("jpg")) return ".jpg";
  if (lower.includes("webp")) return ".webp";
  if (lower.includes("gif")) return ".gif";
  return ".png";
}

async function currentMiaoDeskBaseUrl(): Promise<string> {
  const agentDir = process.env.PI_CODING_AGENT_DIR;
  if (!agentDir) return "";
  try {
    const raw = await readFile(join(agentDir, "models.json"), "utf8");
    return String(JSON.parse(raw)?.providers?.miaodesk?.baseUrl ?? "");
  } catch { return ""; }
}

// Resolves the credential for the configured image provider.
//
// The old rule was "reuse the main key only when baseUrl contains openrouter.ai",
// which made image generation impossible the moment baseUrl pointed at a local
// inference server. The new rules:
//   * an explicit provider-specific env var wins;
//   * a loopback endpoint is keyless, matching how the profile itself treats it;
//   * otherwise reuse the main model key, whatever the provider is.
async function resolveImageApiKey(): Promise<string> {
  const explicit = process.env.MIAODESK_IMAGE_API_KEY?.trim();
  if (explicit) return explicit;
  if (!IMAGE_PROVIDER) return "";
  const baseUrl = (IMAGE_BASE_URL || await currentMiaoDeskBaseUrl()).toLowerCase();
  if (isLoopback(baseUrl)) return "local";
  return process.env.MIAODESK_MODEL_API_KEY?.trim() ?? "";
}

function usesOpenAICompatibleImageShim(provider: string): boolean {
  const normalized = provider.trim().toLowerCase();
  return normalized === "openai-compatible" || normalized === "local-openai-compatible";
}

function openAIImageEndpoint(baseUrl: string): string {
  const normalized = baseUrl.trim().replace(/\/+$/, "");
  if (!normalized) return "";
  if (normalized.endsWith("/images/generations")) return normalized;
  return normalized + "/images/generations";
}

async function generateOpenAICompatibleImage(
  prompt: string, imageModel: string, apiKey: string, signal?: AbortSignal
): Promise<{ data: string; mimeType: string }> {
  const baseUrl = IMAGE_BASE_URL || await currentMiaoDeskBaseUrl();
  const endpoint = openAIImageEndpoint(baseUrl);
  if (!endpoint) throw new Error("图片生成能力未配置 imageBaseUrl。");
  const headers: Record<string, string> = { "Content-Type": "application/json" };
  if (apiKey && apiKey !== "local") headers.Authorization = `Bearer ${apiKey}`;
  const response = await fetch(endpoint, {
    method: "POST",
    headers,
    body: JSON.stringify({
      model: imageModel,
      prompt,
      n: 1,
      response_format: "b64_json",
    }),
    signal,
  });
  const raw = await response.text();
  let body: any = {};
  try { body = raw ? JSON.parse(raw) : {}; } catch {}
  if (!response.ok) {
    const detail = String(body?.error?.message ?? body?.message ?? raw ?? "").trim();
    throw new Error(detail || `OpenAI-compatible image endpoint returned HTTP ${response.status}`);
  }
  const data = String(body?.data?.[0]?.b64_json ?? "");
  if (!data) {
    throw new Error("OpenAI-compatible image endpoint did not return data[0].b64_json.");
  }
  return { data, mimeType: "image/png" };
}

async function generateImage(prompt: string, fileName: string, signal?: AbortSignal): Promise<string> {
  if (!IMAGE_PROVIDER) {
    throw new Error("图片生成能力未配置：API Profile 未指定图片 Provider。聊天和其他 Pi 工具仍可正常使用。");
  }
  const imageModel = IMAGE_MODEL || DEFAULT_IMAGE_MODEL;
  const apiKey = await resolveImageApiKey();
  if (!apiKey) throw new Error(`图片生成能力当前未配置：需要 ${IMAGE_PROVIDER} 的 API Key。聊天和其他 Pi 工具仍可正常使用。`);
  console.error(`[MiaoDesk][artifact] image_generate start provider=${IMAGE_PROVIDER} model=${imageModel}`);
  let image: { data: string; mimeType: string } | undefined;
  if (usesOpenAICompatibleImageShim(IMAGE_PROVIDER)) {
    image = await generateOpenAICompatibleImage(prompt, imageModel, apiKey, signal);
  } else {
    const { getImageModel, generateImages } = await import("@earendil-works/pi-ai/compat");
    const model = getImageModel(IMAGE_PROVIDER, imageModel);
    if (!model) throw new Error(`Pi 图片模型不可用：${IMAGE_PROVIDER}/${imageModel}`);
    const result = await generateImages(model, { input: [{ type: "text", text: prompt }] }, { apiKey, signal });
    if (result.stopReason === "error") {
      const providerText = result.output
        .filter((block: any) => block?.type === "text")
        .map((block: any) => String(block.text ?? ""))
        .filter(Boolean)
        .join("\n");
      throw new Error(providerText || "Pi 图片生成 Provider 返回失败。");
    }
    image = result.output.find((block: any) => block?.type === "image") as
      | { type: "image"; data: string; mimeType: string }
      | undefined;
  }
  if (!image?.data) throw new Error("Pi 图片生成完成，但 Provider 没有返回图片数据。");
  const outputDir = join(process.cwd(), "MiaoDesk Images");
  await mkdir(outputDir, { recursive: true });
  const stem = safeFileStem(fileName.replace(/\.[A-Za-z0-9]+$/, ""));
  const output = join(outputDir, stem + extensionForMime(image.mimeType || "image/png"));
  await writeFile(output, Buffer.from(image.data, "base64"));
  console.error(`[MiaoDesk][artifact] image_generate success path=${output}`);
  return output;
}

function activateNativeTools(pi: ExtensionAPI) {
  const active = pi.getActiveTools();
  pi.setActiveTools([...new Set([...active, ...TOOL_NAMES])]);
}
)PIEXT";

constexpr std::string_view kExtensionSourcePart2 = R"PIEXT(export default function turingDeskNativeTools(pi: ExtensionAPI) {
  const native = (name: string, label: string, description: string, parameters: any) => {
    pi.registerTool({
      name, label, description, parameters,
      executionMode: "sequential",
      async execute(_toolCallId, params, signal) {
        return textResult(await runNativeTool(name, params, signal));
      },
    });
  };

  native("settings_open", "Open MiaoDesk Settings",
    "Open the native MiaoDesk Settings Center. Use this for MiaoDesk settings or provider configuration instead of shell commands.",
    Type.Object({}, { additionalProperties: false }));

  native("ppt_create", "Create PowerPoint Presentation",
    "Create a real .pptx using installed Microsoft PowerPoint or WPS Presentation.",
    Type.Object({
      file_name: Type.String(), title: Type.String(),
      subtitle: Type.Optional(Type.String()), slides_markdown: Type.String(),
      open_after_create: Type.Optional(Type.Boolean()),
    }, { additionalProperties: false }));

  native("file_create", "Create User File", "Create a UTF-8 text file in a constrained user folder.",
    Type.Object({
      location: Type.Union([Type.Literal("desktop"), Type.Literal("documents"), Type.Literal("downloads")]),
      file_name: Type.String(), content: Type.String(),
    }, { additionalProperties: false }));
  native("folder_list", "List User Folder", "List Desktop, Documents, or Downloads.",
    Type.Object({ location: Type.Union([Type.Literal("desktop"), Type.Literal("documents"), Type.Literal("downloads")]) }, { additionalProperties: false }));
  native("file_open", "Open User File", "Open an existing file from a constrained user folder.",
    Type.Object({
      location: Type.Union([Type.Literal("desktop"), Type.Literal("documents"), Type.Literal("downloads")]),
      file_name: Type.String(),
    }, { additionalProperties: false }));

  pi.registerTool({
    name: "image_generate",
    label: "Generate Image",
    description: "Generate a standalone image file. For a desktop wallpaper request, prefer desktop_preview_wallpaper so the user sees a safe preview before any desktop change.",
    parameters: Type.Object({
      prompt: Type.String(), file_name: Type.Optional(Type.String()),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      try {
        const output = await generateImage(params.prompt, params.file_name ?? `MiaoDesk-Image-${Date.now()}`, signal);
        return textResult(`图片已生成：${output}`);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        console.error(`[MiaoDesk][artifact] image_generate failed: ${message}`);
        throw new Error(message);
      }
    },
  });

  native("wallpaper_validate_package", "Validate MiaoDesk Wallpaper",
    "Validate an existing local .mdwall package. This does not apply it.",
    Type.Object({ path: Type.String() }, { additionalProperties: false }));
  native("content_skill_get", "Load Content Skill",
    "Load a MiaoDesk content-creation skill before writing any .mdwall/.mdwidget package. Call content-package-basics first, then the matching domain skill, then content-review. Omit name to list available skills. This is read-only.",
    Type.Object({
      name: Type.Optional(Type.String({
        description: "content-package-basics | wallpaper-content | widget-content | content-review",
      })),
    }, { additionalProperties: false }));
  native("wallpaper_state_get", "Read Desktop State",
    "Read current MiaoDesk desktop state. This is read-only.",
    Type.Object({}, { additionalProperties: false }));
  native("desktop_widget_list", "List Desktop Widgets",
    "List current persistent widgets. This is read-only.",
    Type.Object({}, { additionalProperties: false }));

  native("desktop_preview_wallpaper", "Preview Desktop Wallpaper",
    "Create a sandbox preview of a wallpaper without changing the current desktop. Dynamic wallpapers use application-owned safe presets; image/video can reference an existing local file. Only the user's native Apply button can commit it.",
    Type.Object({
      title: Type.String(),
      mode: Type.Optional(Type.Union([Type.Literal("preset"), Type.Literal("image"), Type.Literal("video")])),
      source: Type.Optional(Type.String({ description: "Preset key or existing local image/video path" })),
      example_key: Type.Optional(Type.Union([
        Type.Literal("aurora_flow"), Type.Literal("neon_flow"), Type.Literal("ocean_flow"),
      ])),
    }, { additionalProperties: false }));

  native("desktop_preview_examples", "List Desktop Showcase Examples",
    "List built-in wallpaper examples. Examples use the exact same sandbox and Apply/Reject path as AI-generated content.",
    Type.Object({}, { additionalProperties: false }));

  pi.on("session_start", () => activateNativeTools(pi));
  pi.on("before_agent_start", async (event) => {
    activateNativeTools(pi);
    return {
      systemPrompt: `${event.systemPrompt}\n\n## MiaoDesk Artifact and Desktop Safety\n- For a real PPT file, use ppt_create.\n- For a standalone image, use image_generate.\n- For ANY request to add/change the desktop wallpaper, use desktop_preview_wallpaper. Never use shell/file tricks to mutate the desktop.\n- Desktop generation is PREVIEW-FIRST: you can create a sandbox preview, but you can never Apply/Reject it for the user. The native Apply button is the only commit authority.\n- Your widget tools are read-only: use desktop_widget_list to inspect widgets, and never generate widget HTML/CSS/JavaScript. Do not claim your tools created or changed a widget.\n- Widget creation is a SEPARATE, user-initiated flow, not one of your tools: the user starts it with "AI 制作组件" in the widget library. It asks their requirements first, then builds a previewable .mdwidget content package that the user must confirm before it reaches the desktop. So never say widgets cannot be made — say the user opens that creator and you will help inside it.\n- For a decorative dynamic wallpaper, choose the closest safe application-owned preset. A blue-ocean dynamic wallpaper should prefer ocean_flow.\n- Built-in showcase keys: wallpapers aurora_flow, neon_flow, ocean_flow.\n- Never claim a persistent desktop change happened after a preview tool. Say it is waiting for the user's Apply decision.\n- Never claim an artifact was created unless the corresponding tool reports success.`,
    };
  });
}
)PIEXT";

// ---------------------------------------------------------------------------
// 创作扩展(CCA-04)
//
// 它和上面那份不是"少几个工具的同一份",而是另一套:上面那份能写文件、能生成图片、
// 能读桌面状态;这一份**一个都不能**。八个创作工具全部转发给宿主 worker 执行,
// 扩展本身只有"把参数交给宿主"的能力,没有任何写盘路径。
//
// 为什么刻意做得这么窄:创作会话要交给模型的是"往包里写 JSON"这一件事。一旦它顺手
// 带上 bash/read/write,计划 §5 那条"关闭通用工具后仍可完整制作"就永远无法验证 ——
// 因为没人说得清制作到底依赖了哪些能力,也就说不清关掉它们之后还能不能做。
// 隔离做在工具面上,才让"关掉之后仍然完整"成为一句可证伪的话。
//
// TOOL_NAMES 由 CreatorToolNames() 在 C++ 侧生成,不在这里手写一遍。两份各自的
// 清单总会漂移,而漂移的方向全是静默的:名册里多一个工具、这里漏注册,模型就只是
// 从来没见过它,没有任何东西会报错。tests/creator-tool-surface-contract.mjs
// 反向断言注册体覆盖名册,所以两个方向都堵住了。
constexpr std::string_view kCreatorExtensionSourceHead = R"PIEXT(import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { mkdtemp, readFile, writeFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawn } from "node:child_process";

const HOST = process.env.MIAODESK_NATIVE_TOOL_HOST ?? "";
// 会话与工作区由宿主通过环境变量给出,不由模型填写。扩展把它们补进参数,是为了让
// "模型忘了带"不变成一次失败;而 worker 会用**自己的**环境变量复核,所以模型若把
// 这两个字段改成别的值,在 worker 侧就是一次归属不符的拒绝 —— 参数永远不是事实来源。
const SESSION = process.env.MIAODESK_CREATOR_SESSION ?? "";
const WORKSPACE = process.env.MIAODESK_CREATOR_WORKSPACE ?? "";

function textResult(text: string) {
  return { content: [{ type: "text" as const, text }], details: {} };
}

async function runNativeTool(tool: string, params: unknown, signal?: AbortSignal): Promise<string> {
  if (!HOST) throw new Error("MiaoDesk native tool host is unavailable.");
  const work = await mkdtemp(join(tmpdir(), "miaodesk-pi-creator-"));
  const input = join(work, "input.json");
  const output = join(work, "output.txt");
  try {
    await writeFile(input, JSON.stringify(params ?? {}), "utf8");
    await new Promise<void>((resolve, reject) => {
      const child = spawn(HOST, ["--native-tool-worker", tool, input, output], {
        windowsHide: true,
        stdio: "ignore",
      });
      let finished = false;
      let timer: ReturnType<typeof setTimeout> | undefined;
      const finish = (error?: Error) => {
        if (finished) return;
        finished = true;
        if (timer) clearTimeout(timer);
        signal?.removeEventListener("abort", abort);
        error ? reject(error) : resolve();
      };
      const abort = () => {
        try { child.kill(); } catch {}
        finish(new Error(`MiaoDesk creator tool cancelled: ${tool}`));
      };
      timer = setTimeout(() => {
        try { child.kill(); } catch {}
        finish(new Error(`MiaoDesk creator tool timed out: ${tool}`));
      }, 120000);
      if (signal?.aborted) return abort();
      signal?.addEventListener("abort", abort, { once: true });
      child.once("error", error => finish(error));
      child.once("exit", code => code === 0
        ? finish()
        : finish(new Error(`MiaoDesk creator tool worker exited with code ${code ?? "unknown"}: ${tool}`)));
    });
    const raw = await readFile(output, "utf8");
    const newline = raw.indexOf("\n");
    if (newline < 1) throw new Error(`MiaoDesk creator tool returned an invalid result: ${tool}`);
    const success = raw.slice(0, newline).trim() === "1";
    const message = raw.slice(newline + 1).trim() ||
      (success ? "MiaoDesk creator tool completed." : "MiaoDesk creator tool failed.");
    if (!success) throw new Error(message);
    return message;
  } finally {
    await rm(work, { recursive: true, force: true }).catch(() => {});
  }
}
)PIEXT";

constexpr std::string_view kCreatorExtensionSourceBody = R"PIEXT(function activateNativeTools(pi: ExtensionAPI) {
  const active = pi.getActiveTools();
  pi.setActiveTools([...new Set([...active, ...TOOL_NAMES])]);
}

export default function miaodeskCreatorTools(pi: ExtensionAPI) {
  // 每个创作工具都只是"把参数转给宿主"。参数的形状在这里声明清楚:
  // required 的字段和 CreatorToolRegistry 的必填校验一一对应,所以模型漏带时
  // 先在 typebox 这一层就得到一个明确的错误,而不是走到 worker 才被拒。
  const creator = (name: string, label: string, description: string, parameters: any) => {
    pi.registerTool({
      name, label, description, parameters,
      executionMode: "sequential",
      async execute(_toolCallId, params, signal) {
        return textResult(await runNativeTool(name, { ...params, sessionId: SESSION, workspaceRoot: WORKSPACE }, signal));
      },
    });
  };

  creator("creator_capabilities_get", "Read Creator Capabilities",
    "List what this creator session can do right now: the tools it may call, the current stage, and the workspace it is confined to. Call this first when you are unsure what is permitted. This is read-only.",
    Type.Object({}, { additionalProperties: false }));

  creator("content_skill_get", "Load Content Skill",
    "Load a MiaoDesk content-creation skill before writing any .mdwall/.mdwidget package. Call content-package-basics first, then the matching domain skill, then content-review. Omit name to list available skills. This is read-only.",
    Type.Object({
      name: Type.Optional(Type.String({
        description: "content-package-basics | wallpaper-content | widget-content | content-review",
      })),
    }, { additionalProperties: false }));

  creator("creator_package_read", "Read Package File",
    "Read one file from the current work's content package. Only package-relative paths inside the allowed layout are accepted (manifest.json / parameters.json / scene/*.json / preview.<image> / assets/*). This is read-only.",
    Type.Object({ relativePath: Type.String() }, { additionalProperties: false }));

  creator("creator_package_update", "Write Package File",
    "Write one file of the current work's content package. The write is transactional: if anything about it is rejected, the package is left exactly as it was and no revision is created. Pass expectedDigest (from the last read or receipt) to refuse a write that is based on a stale view of the package.",
    Type.Object({
      relativePath: Type.String(),
      content: Type.String(),
      expectedDigest: Type.Optional(Type.String({ description: "64-hex candidate digest this write assumes; omit if unknown" })),
    }, { additionalProperties: false }));

  creator("creator_asset_import", "Import Asset",
    "Import an asset into the package from a host-managed source only (content:cloud, or a path inside the current workspace). Arbitrary filesystem paths are refused. The asset lands under assets/ with a host-chosen flattened name.",
    Type.Object({
      source: Type.String({ description: "content:cloud, content:cloud/<id>, or a path already inside this workspace" }),
      relativePath: Type.String({ description: "assets/<name> destination inside the package" }),
    }, { additionalProperties: false }));

  creator("creator_image_generate", "Generate Package Image",
    "Generate an image and place it inside the package under assets/. Use this for the work's own artwork; it is not a way to write files anywhere else.",
    Type.Object({
      prompt: Type.String(),
      relativePath: Type.String({ description: "assets/<name> destination inside the package" }),
    }, { additionalProperties: false }));

  creator("creator_candidate_submit", "Submit Candidate",
    "Submit a candidate version of the work for validation and sealing. The digest must be one the host issued; it cannot be guessed. The reply is a structured receipt with the revision the host assigned.",
    Type.Object({
      digest: Type.String({ description: "64-hex candidate digest issued by the host for this work" }),
      summary: Type.Optional(Type.String()),
    }, { additionalProperties: false }));

  creator("creator_preview_evidence", "Collect Render Evidence",
    "Ask the host to render the current candidate and return evidence that it actually renders (dimensions, backend, and a preview path). Use this before submitting so you are not sealing a package you have never seen render.",
    Type.Object({
      backend: Type.String({ description: "d2d | d3d11 | auto" }),
    }, { additionalProperties: false }));

  pi.on("session_start", () => activateNativeTools(pi));
  pi.on("before_agent_start", async (event) => {
    activateNativeTools(pi);
    return {
      systemPrompt: `${event.systemPrompt}\n\n## MiaoDesk Content Creator\n- You are making one declarative content package. You have NO shell, NO general file tools, and NO access outside this work's workspace. That is deliberate: everything you need is one of the creator_* tools.\n- Start with creator_capabilities_get and content_skill_get before writing anything.\n- Read a file with creator_package_read and write it with creator_package_update. Write only manifest.json, parameters.json, scene/*.json, preview.<image>, or assets/*. Never write code, HTML, CSS, JavaScript, or executables: desktop content is JSON only.\n- Every write is checked against the allowed layout, a size limit, and the workspace root. A rejected write leaves the package untouched and tells you why; fix the cause instead of retrying the same call.\n- Use creator_asset_import for existing media and creator_image_generate for artwork you describe. Both land inside assets/.\n- Before submitting, call creator_preview_evidence and read what it reports.\n- creator_candidate_submit returns a structured receipt. Only a candidate you actually received a receipt for can be applied. Never claim the user's desktop changed: only their Apply button commits.\n- Never claim a tool succeeded unless its result says so.`,
    };
  });
}
)PIEXT";

// 创作扩展的 TOOL_NAMES 在 C++ 侧由名册生成。
//
// 为什么不在这里手写一份:名册(CreatorToolNames)是唯一事实来源,Pi 的 --tools 与
// worker 的允许表都由它派生。这里如果另抄八个名字,就成了第三份 —— 而"多一个工具
// 但扩展没注册"的故障是静默的:模型从来没见过它,没有任何东西报错。生成它,这个
// 方向就不可能漂移;tests/creator-tool-surface-contract.mjs 再反向断言注册体覆盖
// 名册,把另一个方向也堵住。
std::string CreatorExtensionSource() {
    std::string source(kCreatorExtensionSourceHead);
    source += "const TOOL_NAMES = [\n";
    for (const auto& name : miaodesk::creator::CreatorToolNames()) {
        source += "  \"";
        source += name;
        source += "\",\n";
    }
    source += "] as const;\n\n";
    source += kCreatorExtensionSourceBody;
    return source;
}

} // namespace

bool EnsurePiNativeToolsExtension(std::wstring* error, std::wstring* extensionPath,
                                 PiNativeToolsVariant variant,
                                 const std::wstring& targetDirectory) {
    const auto module = ModulePath();
    if (module.empty()) {
        if (error) *error = L"Unable to resolve MiaoDesk native tool host.";
        return false;
    }
    if (!SetEnvironmentVariableW(kNativeToolHostEnvironment, module.c_str())) {
        if (error) *error = L"Unable to export MiaoDesk native tool host path.";
        return false;
    }

    // 扩展文件按 variant 分开命名,并且可以由调用方指定目录。
    // 共用同一个文件是错的:后写的那份会替换前一份,于是*另一个模式*下一次启动的
    // 进程加载到的是这一模式的工具集 —— 表现是"聊天突然不能写文件了",
    // 而没有任何一处代码改过聊天的 allowlist。
    fs::path root = targetDirectory.empty() ? fs::path(PiAgentDirectory()) : fs::path(targetDirectory);
    const wchar_t* fileName = variant == PiNativeToolsVariant::Creator
                                  ? L"miaodesk-creator-tools.ts"
                                  : L"miaodesk-native-tools.ts";

    std::error_code ec;
    const auto extensions = root / L"extensions";
    fs::create_directories(extensions, ec);
    if (ec) {
        if (error) *error = L"Unable to create Pi extension directory.";
        return false;
    }

    const auto target = extensions / fileName;
    // 两个 variant 的内容也必须是两份:Creator 那份连通用工具的影子都不该有。
    // 判断"要不要重写"按内容逐字节比,所以切换 variant 或名册变化都会自然重装。
    std::string expected;
    if (variant == PiNativeToolsVariant::Creator) {
        expected = CreatorExtensionSource();
    } else {
        expected.reserve(kExtensionSourcePart1.size() + kExtensionSourcePart2.size());
        expected.append(kExtensionSourcePart1);
        expected.append(kExtensionSourcePart2);
    }
    if (ReadFile(target) != expected) {
        auto temporary = target;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) {
                if (error) *error = L"Unable to write MiaoDesk Pi extension.";
                return false;
            }
            stream.write(expected.data(), static_cast<std::streamsize>(expected.size()));
            if (!stream) {
                if (error) *error = L"Unable to finish writing MiaoDesk Pi extension.";
                return false;
            }
        }

        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            fs::remove(temporary, ec);
            if (error) *error = L"Unable to install MiaoDesk Pi extension.";
            return false;
        }
    }

    if (extensionPath) *extensionPath = target.wstring();
    return true;
}

} // namespace miaodesk
