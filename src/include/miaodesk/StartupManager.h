#pragma once

#include <string_view>

namespace miaodesk::startup {

// StartupTask activations do not carry a conventional command-line switch.
// Packaged builds inspect the activation kind while unpackaged builds use the
// explicit --startup argument stored in the current user's Run key.
bool IsStartupLaunch(std::wstring_view commandLine);

// Requests the user's permission once, then registers login startup using the
// Windows-supported mechanism for the current deployment type.
void PromptForConsentIfNeeded();

// Opens the Windows page where the user can review or revoke startup access.
void OpenWindowsStartupSettings();

// Pure checks used by the native executable self-test.
bool SelfTest();

} // namespace miaodesk::startup
