#include "turingdesk/PiArtifactExtension.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace turingdesk {
namespace {

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

constexpr std::string_view kExtensionSource = R"PIART(import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";

const TOOL_NAME = "image_generate";
const DEFAULT_IMAGE_MODEL = "google/gemini-2.5-flash-image";

function textResult(text: string) {
  return { content: [{ type: "text" as const, text }], details: {} };
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

  // Reuse the normal TuringDesk credential only when the configured provider is
  // actually OpenRouter. Never send an unrelated provider key to OpenRouter.
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
      "图片生成能力当前未配置：需要 OpenRouter API Key。聊天模型仍可正常使用；请在后续图片 Provider 设置中配置后重试。",
    );
  }

  console.error(`[TuringDesk][artifact] image_generate start model=${DEFAULT_IMAGE_MODEL}`);

  const { getImageModel, generateImages } = await import("@earendil-works/pi-ai/compat");
  const model = getImageModel("openrouter", DEFAULT_IMAGE_MODEL);
  if (!model) {
    throw new Error(`Pi 图片模型不可用：${DEFAULT_IMAGE_MODEL}`);
  }

  const result = await generateImages(
    model,
    { input: [{ type: "text", text: prompt }] },
    { apiKey, signal },
  );

  if (result.stopReason === "error") {
    const errorText = result.output
      .filter((block: any) => block?.type === "text")
      .map((block: any) => String(block.text ?? ""))
      .filter(Boolean)
      .join("\n");
    throw new Error(errorText || "Pi 图片生成 Provider 返回失败。\n");
  }

  const image = result.output.find((block: any) => block?.type === "image") as
    | { type: "image"; data: string; mimeType: string }
    | undefined;
  if (!image?.data) {
    throw new Error("Pi 图片生成完成，但 Provider 没有返回图片数据。\n");
  }

  const outputDir = join(process.cwd(), "TuringDesk Images");
  await mkdir(outputDir, { recursive: true });
  const stem = safeFileStem(fileName.replace(/\.[A-Za-z0-9]+$/, ""));
  const output = join(outputDir, stem + extensionForMime(image.mimeType || "image/png"));
  await writeFile(output, Buffer.from(image.data, "base64"));

  console.error(`[TuringDesk][artifact] image_generate success path=${output}`);
  return output;
}

function activateArtifactTools(pi: ExtensionAPI) {
  const active = pi.getActiveTools();
  if (!active.includes(TOOL_NAME)) pi.setActiveTools([...active, TOOL_NAME]);
}

export default function turingDeskArtifacts(pi: ExtensionAPI) {
  pi.registerTool({
    name: TOOL_NAME,
    label: "Generate Image",
    description:
      "Generate a real image file with TuringDesk's configured Pi image provider and save it to the desktop. Use this whenever the user asks to create/generate/draw/render an image. Do not pretend the chat model itself created an image; success requires this tool to return a saved file path.",
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

  pi.on("session_start", () => activateArtifactTools(pi));
  pi.on("before_agent_start", async (event) => {
    activateArtifactTools(pi);
    return {
      systemPrompt: `${event.systemPrompt}\n\n## TuringDesk Artifact Capabilities\n- When the user asks for a real PPT/PowerPoint presentation file, use the ppt_create tool.\n- When the user asks to create, draw, render, or generate an image, use the image_generate tool.\n- Never claim an artifact was created unless the corresponding tool reports success and a concrete output path.\n- If an artifact tool reports that its provider/backend is unavailable, explain that limitation clearly instead of improvising a fake success.`,
    };
  });
}
)PIART";

} // namespace

bool EnsurePiArtifactExtension(std::wstring* error) {
    std::error_code ec;
    const auto extensions = PiAgentDirectory() / L"extensions";
    fs::create_directories(extensions, ec);
    if (ec) {
        if (error) *error = L"Unable to create Pi artifact extension directory.";
        return false;
    }

    const auto target = extensions / L"turingdesk-artifacts.ts";
    const std::string expected(kExtensionSource);
    if (ReadFile(target) == expected) return true;

    auto temporary = target;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            if (error) *error = L"Unable to write TuringDesk Pi artifact extension.";
            return false;
        }
        stream.write(expected.data(), static_cast<std::streamsize>(expected.size()));
        if (!stream) {
            if (error) *error = L"Unable to finish writing TuringDesk Pi artifact extension.";
            return false;
        }
    }

    if (!MoveFileExW(
            temporary.c_str(), target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, ec);
        if (error) *error = L"Unable to install TuringDesk Pi artifact extension.";
        return false;
    }
    return true;
}

} // namespace turingdesk
