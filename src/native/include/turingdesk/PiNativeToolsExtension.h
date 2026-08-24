#pragma once

#include <string>

namespace turingdesk {

// Materializes the TuringDesk-owned Pi extension into the dedicated Pi agent
// directory and exports the current native host path for its isolated workers.
bool EnsurePiNativeToolsExtension(std::wstring* error = nullptr);

} // namespace turingdesk
