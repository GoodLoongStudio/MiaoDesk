#include "turingdesk/PiNativeToolsExtension.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (count > 0 && count < std::size(localAppData)) {
        return fs::path(std::wstring(localAppData, count)) / L"TuringDesk" / L"PiAgent";
    }
    return fs::temp_directory_path() / L"TuringDesk" / L"PiAgent";
}

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

constexpr std::string_view kExtensionSource = R"PIEXT(import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawn } from "node:child_process";

const HOST = process.env.TURINGDESK_NATIVE_TOOL_HOST ?? "";
const TOOL_NAMES = [
  "settings_open",
  "wallpaper_create_web_package",
  "wallpaper_validate_package",
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
      const finish = (error?: Error) => {
        if (finished) return;
        finished = true;
        clearTimeout(timer);
        signal?.removeEventListener("abort", abort);
        if (error) reject(error);
        else resolve();
      };
      const abort = () => {
        try { child.kill(); } catch {}
        finish(new Error(`TuringDesk native tool cancelled: ${tool}`));
      };
      const timer = setTimeout(() => {
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
    const message = raw.slice(newline + 1).trim() || (success ? "TuringDesk native tool completed." : "TuringDesk native tool failed.");
    if (!success) throw new Error(message);
    return message;
  } finally {
    await rm(work, { recursive: true, force: true }).catch(() => {});
  }
}

function activateNativeTools(pi: ExtensionAPI) {
  const active = pi.getActiveTools();
  pi.setActiveTools([...new Set([...active, ...TOOL_NAMES])]);
}

export default function turingDeskNativeTools(pi: ExtensionAPI) {
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

  pi.on("session_start", () => activateNativeTools(pi));
  pi.on("before_agent_start", () => activateNativeTools(pi));
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
    const std::string expected(kExtensionSource);
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
