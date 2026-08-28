#pragma once

#include <string>

namespace turingdesk {

// Materializes the TuringDesk-owned Pi extension into the dedicated Pi agent
// directory and exports the current native host path for its isolated workers.
// When extensionPath is non-null, it receives the absolute path of the installed
// extension entrypoint that Pi must load via --extension.
bool EnsurePiNativeToolsExtension(std::wstring* error = nullptr,
                                  std::wstring* extensionPath = nullptr);

} // namespace turingdesk
