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
  "wallpaper_create_web_package",
  "wallpaper_validate_package",
  "wallpaper_state_get",
  "wallpaper_apply_web_package",
  "desktop_widget_create_web",
  "desktop_widget_update",
  "desktop_widget_remove",
  "desktop_widget_list",
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
      const abort = () => {
        try { child.kill(); } catch {}
        finish(new Error(`TuringDesk native tool cancelled: ${tool}`));
      };
      const finish = (error?: Error) => {
        if (finished) return;
        finished = true;
        if (timer) clearTimeout(timer);
        signal?.removeEventListener("abort", abort);
        if (error) reject(error);
        else resolve();
      };
      timer = setTimeout(() => {
        try { child.kill(); } catch {}
        finish(new Error(`TuringDesk native tool timed out: ${tool}`));
      }, 30000);

      if (signal?.aborted) {
        abort();
        return;
      }
      signal?.addEventListener("abort", abort, { once: true });
      child.once("error", (error) => finish(error));
      child.once("exit", (code) => {
        if (code === 0) finish();
        else finish(new Error(`TuringDesk native tool worker exited with code ${code ?? "unknown"}: ${tool}`));
      });
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
    const models = JSON.parse(raw);
    return String(models?.providers?.turingdesk?.baseUrl ?? "");
  } catch {
    return "";
  }
}

async function resolveOpenRouterApiKey(): Promise<string> {
  const explicit = process.env.OPENROUTER_API_KEY?.trim();
  if (explicit) return explicit;

  // Only reuse the normal TuringDesk key when the configured provider really is
  // OpenRouter. Never leak an unrelated provider credential to another service.
  const baseUrl = (await currentTuringDeskBaseUrl()).toLowerCase();
  if (baseUrl.includes("openrouter.ai")) {
    return process.env.TURINGDESK_MODEL_API_KEY?.trim() ?? "";
  }
  return "";
}

async function generateImage(
  prompt: string,
  fileName: string,
  signal?: AbortSignal,
): Promise<string> {
  const apiKey = await resolveOpenRouterApiKey();
  if (!apiKey) {
    throw new Error(
      "图片生成能力当前未配置：需要 OpenRouter API Key。聊天和其他 Pi 工具仍可正常使用。",
    );
  }

  console.error(`[TuringDesk][artifact] image_generate start model=${DEFAULT_IMAGE_MODEL}`);

  const { getImageModel, generateImages } = await import("@earendil-works/pi-ai/compat");
  const model = getImageModel("openrouter", DEFAULT_IMAGE_MODEL);
  if (!model) throw new Error(`Pi 图片模型不可用：${DEFAULT_IMAGE_MODEL}`);

  const result = await generateImages(
    model,
    { input: [{ type: "text", text: prompt }] },
    { apiKey, signal },
  );

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

const normalizedGeometry = {
  x: Type.Optional(Type.Number({ minimum: 0, maximum: 1 })),
  y: Type.Optional(Type.Number({ minimum: 0, maximum: 1 })),
  width: Type.Optional(Type.Number({ minimum: 0.05, maximum: 1 })),
  height: Type.Optional(Type.Number({ minimum: 0.05, maximum: 1 })),
};
)PIEXT";

constexpr std::string_view kExtensionSourcePart2 = R"PIEXT(export default function turingDeskNativeTools(pi: ExtensionAPI) {
  pi.registerTool({
    name: "settings_open",
    label: "Open TuringDesk Settings",
    description: "Open the native TuringDesk Settings Center. Use this for TuringDesk settings, wallpaper settings, preferences, provider configuration, or advanced settings instead of shell commands.",
    parameters: Type.Object({}, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("settings_open", params, signal));
    },
  });

  pi.registerTool({
    name: "ppt_create",
    label: "Create PowerPoint Presentation",
    description: "Create a real .pptx presentation on the Windows desktop using installed Microsoft PowerPoint or WPS Presentation. Use this whenever the user asks for a real PPT/presentation file instead of only writing an outline.",
    parameters: Type.Object({
      file_name: Type.String({ description: "Output filename; .pptx is added when missing" }),
      title: Type.String({ description: "Presentation title" }),
      subtitle: Type.Optional(Type.String({ description: "Optional subtitle for the title slide" })),
      slides_markdown: Type.String({ description: "Content slides. Start each slide with '# Slide title'; following lines become bullets." }),
      open_after_create: Type.Optional(Type.Boolean({ description: "Open the generated presentation after saving" })),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("ppt_create", params, signal));
    },
  });

  pi.registerTool({
    name: "file_create",
    label: "Create User File",
    description: "Create a UTF-8 text file in Desktop, Documents, or Downloads. Use this deterministic tool for user-requested text/markdown/html/json artifacts in those folders.",
    parameters: Type.Object({
      location: Type.Union([Type.Literal("desktop"), Type.Literal("documents"), Type.Literal("downloads")]),
      file_name: Type.String(),
      content: Type.String(),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("file_create", params, signal));
    },
  });

  pi.registerTool({
    name: "folder_list",
    label: "List User Folder",
    description: "List files and folders from Desktop, Documents, or Downloads through TuringDesk's constrained native file surface.",
    parameters: Type.Object({
      location: Type.Union([Type.Literal("desktop"), Type.Literal("documents"), Type.Literal("downloads")]),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("folder_list", params, signal));
    },
  });

  pi.registerTool({
    name: "file_open",
    label: "Open User File",
    description: "Open an existing file from Desktop, Documents, or Downloads with its registered Windows application.",
    parameters: Type.Object({
      location: Type.Union([Type.Literal("desktop"), Type.Literal("documents"), Type.Literal("downloads")]),
      file_name: Type.String(),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("file_open", params, signal));
    },
  });

  pi.registerTool({
    name: "image_generate",
    label: "Generate Image",
    description: "Generate a real image file through Pi's image-generation API and save it under the user's desktop. Use this whenever the user asks to create, draw, render, or generate an image. Never claim image creation succeeded unless this tool returns a concrete saved file path.",
    parameters: Type.Object({
      prompt: Type.String({ description: "Detailed image-generation prompt" }),
      file_name: Type.Optional(Type.String({ description: "Desired output filename or stem" })),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      try {
        const output = await generateImage(
          params.prompt,
          params.file_name ?? `TuringDesk-Image-${Date.now()}`,
          signal,
        );
        return textResult(`图片已生成：${output}`);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        console.error(`[TuringDesk][artifact] image_generate failed: ${message}`);
        throw new Error(message);
      }
    },
  });

  pi.registerTool({
    name: "wallpaper_create_web_package",
    label: "Create TuringDesk Wallpaper",
    description: "Create a validated TuringDesk .tdwall Web wallpaper package on the user's desktop from self-contained HTML/CSS/JS.",
    parameters: Type.Object({
      name: Type.String({ description: "Package name; .tdwall is added automatically" }),
      title: Type.String({ description: "User-facing wallpaper title" }),
      html: Type.String({ description: "Complete self-contained HTML/CSS/JS wallpaper" }),
      open_after_create: Type.Optional(Type.Boolean({ description: "Open the generated package after creation" })),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("wallpaper_create_web_package", params, signal));
    },
  });

  pi.registerTool({
    name: "wallpaper_validate_package",
    label: "Validate TuringDesk Wallpaper",
    description: "Validate an existing TuringDesk .tdwall package directory and report its manifest type and entry point.",
    parameters: Type.Object({
      path: Type.String({ description: "Absolute path to the .tdwall package directory" }),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("wallpaper_validate_package", params, signal));
    },
  });

  pi.registerTool({
    name: "wallpaper_state_get",
    label: "Read Desktop State",
    description: "Read the current TuringDesk wallpaper state and desktop widget count before making desktop changes.",
    parameters: Type.Object({}, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("wallpaper_state_get", params, signal));
    },
  });

  pi.registerTool({
    name: "wallpaper_apply_web_package",
    label: "Apply TuringDesk Web Wallpaper",
    description: "Apply a validated local Web .tdwall package to the current TuringDesk desktop. Use after wallpaper_create_web_package when the user asks to actually change the desktop.",
    parameters: Type.Object({
      path: Type.String({ description: "Absolute path to the .tdwall package directory" }),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("wallpaper_apply_web_package", params, signal));
    },
  });

  pi.registerTool({
    name: "desktop_widget_create_web",
    label: "Create Desktop Widget",
    description: "Create a persistent TuringDesk desktop widget from self-contained HTML/CSS/JS. Geometry is normalized to the target monitor. Widgets are click-through in the first runtime so they never block desktop icons.",
    parameters: Type.Object({
      title: Type.String({ description: "Widget title" }),
      html: Type.String({ description: "Complete self-contained HTML/CSS/JS for the widget; design it as a compact desktop card" }),
      monitor_id: Type.Optional(Type.String({ description: "Stable TuringDesk monitor id; omit for primary monitor" })),
      ...normalizedGeometry,
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("desktop_widget_create_web", params, signal));
    },
  });

  pi.registerTool({
    name: "desktop_widget_update",
    label: "Adjust Desktop Widget",
    description: "Move, resize, enable, restyle, or replace the HTML of an existing TuringDesk desktop widget.",
    parameters: Type.Object({
      id: Type.String({ description: "Widget id returned by desktop_widget_create_web or desktop_widget_list" }),
      title: Type.Optional(Type.String()),
      html: Type.Optional(Type.String({ description: "Replacement self-contained HTML/CSS/JS" })),
      monitor_id: Type.Optional(Type.String({ description: "Stable monitor id; empty string means primary monitor" })),
      ...normalizedGeometry,
      z_index: Type.Optional(Type.Integer({ minimum: -1000, maximum: 1000 })),
      enabled: Type.Optional(Type.Boolean()),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("desktop_widget_update", params, signal));
    },
  });

  pi.registerTool({
    name: "desktop_widget_remove",
    label: "Remove Desktop Widget",
    description: "Remove a TuringDesk desktop widget and its managed HTML package.",
    parameters: Type.Object({
      id: Type.String({ description: "Widget id" }),
    }, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("desktop_widget_remove", params, signal));
    },
  });

  pi.registerTool({
    name: "desktop_widget_list",
    label: "List Desktop Widgets",
    description: "List persistent TuringDesk desktop widgets with ids, monitor targets, enabled state, and normalized geometry.",
    parameters: Type.Object({}, { additionalProperties: false }),
    executionMode: "sequential",
    async execute(_toolCallId, params, signal) {
      return textResult(await runNativeTool("desktop_widget_list", params, signal));
    },
  });

  pi.on("session_start", () => activateNativeTools(pi));
  pi.on("before_agent_start", async (event) => {
    activateNativeTools(pi);
    return {
      systemPrompt: `${event.systemPrompt}\n\n## TuringDesk Artifact Capabilities\n- When the user asks for a real PPT/PowerPoint presentation file, use the ppt_create tool.\n- When the user asks to create, draw, render, or generate an image, use the image_generate tool.\n- Never claim an artifact was created unless the corresponding tool reports success and a concrete output path.\n- If an artifact tool reports that its provider/backend is unavailable, explain that limitation clearly instead of improvising a fake success.`,
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

    if (!MoveFileExW(
            temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, ec);
        if (error) *error = L"Unable to install TuringDesk Pi extension.";
        return false;
    }
    return true;
}

} // namespace turingdesk