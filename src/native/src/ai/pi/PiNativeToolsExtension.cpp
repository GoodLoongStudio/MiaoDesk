#include "turingdesk/PiNativeToolsExtension.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

constexpr wchar_t kNativeToolHostEnvironment[] = L"TURINGDESK_NATIVE_TOOL_HOST";

fs::path ModulePath() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    path.resize(count);
    return fs::path(path);
}

fs::path PiAgentDirectory() {
    wchar_t localAppData[32768]{};
    const DWORD count = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (count > 0 && count < std::size(localAppData)) {
        return fs::path(std::wstring(localAppData, count)) / L"TuringDesk" / L"PiAgent";
    }
    return fs::temp_directory_path() / L"TuringDesk" / L"PiAgent";
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

const HOST = process.env.TURINGDESK_NATIVE_TOOL_HOST ?? "";
const DEFAULT_IMAGE_MODEL = "google/gemini-2.5-flash-image";
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
  "desktop_preview_widget",
  "desktop_preview_wallpaper",
  "desktop_preview_examples",
] as const;

function textResult(text: string) {
  return { content: [{ type: "text" as const, text }], details: {} };
}

async function runNativeTool(tool: string, params: unknown, signal?: AbortSignal): Promise<string> {
  if (!HOST) throw new Error("TuringDesk native tool host is unavailable.");
  const work = await mkdtemp(join(tmpdir(), "turingdesk-pi-tool-"));
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
        finish(new Error(`TuringDesk native tool cancelled: ${tool}`));
      };
      timer = setTimeout(() => {
        try { child.kill(); } catch {}
        finish(new Error(`TuringDesk native tool timed out: ${tool}`));
      }, 30000);
      if (signal?.aborted) return abort();
      signal?.addEventListener("abort", abort, { once: true });
      child.once("error", error => finish(error));
      child.once("exit", code => code === 0
        ? finish()
        : finish(new Error(`TuringDesk native tool worker exited with code ${code ?? "unknown"}: ${tool}`)));
    });
    const raw = await readFile(output, "utf8");
    const newline = raw.indexOf("\n");
    if (newline < 1) throw new Error(`TuringDesk native tool returned an invalid result: ${tool}`);
    const success = raw.slice(0, newline).trim() === "1";
    const message = raw.slice(newline + 1).trim() ||
      (success ? "TuringDesk native tool completed." : "TuringDesk native tool failed.");
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
  return cleaned || `TuringDesk-Image-${Date.now()}`;
}

function extensionForMime(mimeType: string): string {
  const lower = mimeType.toLowerCase();
  if (lower.includes("jpeg") || lower.includes("jpg")) return ".jpg";
  if (lower.includes("webp")) return ".webp";
  if (lower.includes("gif")) return ".gif";
  return ".png";
}

async function currentTuringDeskBaseUrl(): Promise<string> {
  const agentDir = process.env.PI_CODING_AGENT_DIR;
  if (!agentDir) return "";
  try {
    const raw = await readFile(join(agentDir, "models.json"), "utf8");
    return String(JSON.parse(raw)?.providers?.turingdesk?.baseUrl ?? "");
  } catch { return ""; }
}

async function resolveOpenRouterApiKey(): Promise<string> {
  const explicit = process.env.OPENROUTER_API_KEY?.trim();
  if (explicit) return explicit;
  const baseUrl = (await currentTuringDeskBaseUrl()).toLowerCase();
  return baseUrl.includes("openrouter.ai")
    ? process.env.TURINGDESK_MODEL_API_KEY?.trim() ?? ""
    : "";
}

async function generateImage(prompt: string, fileName: string, signal?: AbortSignal): Promise<string> {
  const apiKey = await resolveOpenRouterApiKey();
  if (!apiKey) throw new Error("图片生成能力当前未配置：需要 OpenRouter API Key。聊天和其他 Pi 工具仍可正常使用。");
  console.error(`[TuringDesk][artifact] image_generate start model=${DEFAULT_IMAGE_MODEL}`);
  const { getImageModel, generateImages } = await import("@earendil-works/pi-ai/compat");
  const model = getImageModel("openrouter", DEFAULT_IMAGE_MODEL);
  if (!model) throw new Error(`Pi 图片模型不可用：${DEFAULT_IMAGE_MODEL}`);
  const result = await generateImages(model, { input: [{ type: "text", text: prompt }] }, { apiKey, signal });
  if (result.stopReason === "error") {
    const providerText = result.output
      .filter((block: any) => block?.type === "text")
      .map((block: any) => String(block.text ?? ""))
      .filter(Boolean)
      .join("\n");
    throw new Error(providerText || "Pi 图片生成 Provider 返回失败。");
  }
  const image = result.output.find((block: any) => block?.type === "image") as
    | { type: "image"; data: string; mimeType: string }
    | undefined;
  if (!image?.data) throw new Error("Pi 图片生成完成，但 Provider 没有返回图片数据。");
  const outputDir = join(process.cwd(), "TuringDesk Images");
  await mkdir(outputDir, { recursive: true });
  const stem = safeFileStem(fileName.replace(/\.[A-Za-z0-9]+$/, ""));
  const output = join(outputDir, stem + extensionForMime(image.mimeType || "image/png"));
  await writeFile(output, Buffer.from(image.data, "base64"));
  console.error(`[TuringDesk][artifact] image_generate success path=${output}`);
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

  native("settings_open", "Open TuringDesk Settings",
    "Open the native TuringDesk Settings Center. Use this for TuringDesk settings or provider configuration instead of shell commands.",
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
        const output = await generateImage(params.prompt, params.file_name ?? `TuringDesk-Image-${Date.now()}`, signal);
        return textResult(`图片已生成：${output}`);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        console.error(`[TuringDesk][artifact] image_generate failed: ${message}`);
        throw new Error(message);
      }
    },
  });

  native("wallpaper_validate_package", "Validate TuringDesk Wallpaper",
    "Validate an existing local .tdwall package. This does not apply it.",
    Type.Object({ path: Type.String() }, { additionalProperties: false }));
  native("wallpaper_state_get", "Read Desktop State",
    "Read current TuringDesk desktop state. This is read-only.",
    Type.Object({}, { additionalProperties: false }));
  native("desktop_widget_list", "List Desktop Widgets",
    "List current persistent widgets. This is read-only.",
    Type.Object({}, { additionalProperties: false }));

  native("desktop_preview_widget", "Preview Desktop Widget",
    "Create a sandbox preview of a proposed desktop widget. The AI must provide declarative A2UI JSON only: Card/Text/Button/Weather/List with props and normalized layout. NEVER output HTML, CSS, JavaScript, C++, PowerShell, shell commands, or executable code for this tool. This tool cannot apply the widget; only the user's native Apply button can commit it.",
    Type.Object({
      title: Type.String({ description: "User-facing preview title" }),
      a2ui_json: Type.Optional(Type.String({ description: "Strict A2UI JSON document" })),
      example_key: Type.Optional(Type.Union([
        Type.Literal("today_tasks"), Type.Literal("focus_clock"),
        Type.Literal("weather_glass"), Type.Literal("system_pulse"),
      ])),
    }, { additionalProperties: false }));

  native("desktop_preview_wallpaper", "Preview Desktop Wallpaper",
    "Create a sandbox preview of a wallpaper without changing the current desktop. Dynamic wallpapers use application-owned safe presets; image/video can reference an existing local file. Only the user's native Apply button can commit it.",
    Type.Object({
      title: Type.String(),
      mode: Type.Optional(Type.Union([Type.Literal("preset"), Type.Literal("image"), Type.Literal("video")])),
      source: Type.Optional(Type.String({ description: "Preset key or existing local image/video path" })),
      example_key: Type.Optional(Type.Union([
        Type.Literal("aurora_flow"), Type.Literal("neon_flow"), Type.Literal("ocean_glass"),
      ])),
    }, { additionalProperties: false }));

  native("desktop_preview_examples", "List Desktop Showcase Examples",
    "List built-in wallpaper and widget examples. Examples use the exact same sandbox and Apply/Reject path as AI-generated content.",
    Type.Object({}, { additionalProperties: false }));

  pi.on("session_start", () => activateNativeTools(pi));
  pi.on("before_agent_start", async (event) => {
    activateNativeTools(pi);
    return {
      systemPrompt: `${event.systemPrompt}\n\n## TuringDesk Artifact and Desktop Safety\n- For a real PPT file, use ppt_create.\n- For a standalone image, use image_generate.\n- For ANY request to add/change a desktop wallpaper or widget, use desktop_preview_wallpaper or desktop_preview_widget. Never use shell/file tricks to mutate the desktop.\n- Desktop generation is PREVIEW-FIRST: you can create a sandbox preview, but you can never Apply/Reject it for the user. The native Apply button is the only commit authority.\n- Widget generation must be declarative A2UI JSON only. Allowed component types: Card, Text, Button, Weather, List. Do not generate HTML/CSS/JavaScript/C++/PowerShell for widgets.\n- For a decorative dynamic wallpaper, choose the closest safe application-owned preset. A blue-ocean dynamic wallpaper should prefer ocean_glass.\n- Built-in showcase keys: wallpapers aurora_flow, neon_flow, ocean_glass; widgets today_tasks, focus_clock, weather_glass, system_pulse.\n- Never claim a persistent desktop change happened after a preview tool. Say it is waiting for the user's Apply decision.\n- Never claim an artifact was created unless the corresponding tool reports success.`,
    };
  });
}
)PIEXT";

} // namespace

bool EnsurePiNativeToolsExtension(std::wstring* error) {
    const auto module = ModulePath();
    if (module.empty()) {
        if (error) *error = L"Unable to resolve TuringDesk native tool host.";
        return false;
    }
    if (!SetEnvironmentVariableW(kNativeToolHostEnvironment, module.c_str())) {
        if (error) *error = L"Unable to export TuringDesk native tool host path.";
        return false;
    }

    std::error_code ec;
    const auto extensions = PiAgentDirectory() / L"extensions";
    fs::create_directories(extensions, ec);
    if (ec) {
        if (error) *error = L"Unable to create Pi extension directory.";
        return false;
    }

    const auto target = extensions / L"turingdesk-native-tools.ts";
    std::string expected;
    expected.reserve(kExtensionSourcePart1.size() + kExtensionSourcePart2.size());
    expected.append(kExtensionSourcePart1);
    expected.append(kExtensionSourcePart2);
    if (ReadFile(target) == expected) return true;

    auto temporary = target;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            if (error) *error = L"Unable to write TuringDesk Pi extension.";
            return false;
        }
        stream.write(expected.data(), static_cast<std::streamsize>(expected.size()));
        if (!stream) {
            if (error) *error = L"Unable to finish writing TuringDesk Pi extension.";
            return false;
        }
    }

    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, ec);
        if (error) *error = L"Unable to install TuringDesk Pi extension.";
        return false;
    }
    return true;
}

} // namespace turingdesk
