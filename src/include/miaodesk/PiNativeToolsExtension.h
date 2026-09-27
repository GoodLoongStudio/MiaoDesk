#pragma once

#include <string>

namespace miaodesk {

// Which MiaoDesk-owned Pi extension to materialize. Chat and Creator are two
// different files on purpose: they register different tool sets, and a single
// shared path means whichever profile writes last wins for every *future* process.
enum class PiNativeToolsVariant {
    Chat,
    Creator,
};

// Materializes the MiaoDesk-owned Pi extension into the dedicated Pi agent
// directory and exports the current native host path for its isolated workers.
// When extensionPath is non-null, it receives the absolute path of the installed
// extension entrypoint that Pi must load via --extension.
//
// targetDirectory is where the extension file is written. Chat and Creator must
// pass different directories (see PiLaunchProfile); sharing one means the second
// write silently replaces the first, and the next process of the *other* mode
// loads the wrong tool set.
bool EnsurePiNativeToolsExtension(std::wstring* error = nullptr,
                                  std::wstring* extensionPath = nullptr,
                                  PiNativeToolsVariant variant = PiNativeToolsVariant::Chat,
                                  const std::wstring& targetDirectory = {});

} // namespace miaodesk
