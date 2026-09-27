#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace miaodesk::api_url {

// A Base URL is not a place for a secret.
//
// baseUrl is stored verbatim in api-profiles.ini and copied into the plaintext
// PiAgent\models.json and Harness\DshHome\settings.yaml. TODO API-03's acceptance
// criterion is that a key enters no plaintext config, log or test evidence, so the only
// reliable place to enforce that is where the URL is accepted -- rejecting beats
// rewriting, because the two downstream consumers disagree about the query: the
// direct-model path parses with WinHttpCrackUrl and keeps only UrlPath, while the
// harness path's JoinApiUrl preserves it. Silently editing one of them changes
// behaviour on the wire.
//
// This lived in an anonymous namespace in DesktopAiSettingsPage.cpp, which made it
// impossible to test except by asserting about the text. It is pure string logic with no
// Windows dependency, so it belongs somewhere it can be compiled and executed.

inline constexpr std::array<std::wstring_view, 19> kSecretQueryNames{{
    L"key", L"api_key", L"apikey", L"x-api-key", L"secret_key",
    L"token", L"api_token", L"access_token", L"access-token",
    L"refresh_token", L"refresh-token", L"secret", L"client_secret", L"auth",
    L"authorization", L"password", L"pwd", L"credential", L"credentials",
}};

// The shape scripts/check-private-files.ps1 greps tracked files for, and the one real
// credentials actually have: a short literal prefix and a long opaque run. Narrow on
// purpose -- a heuristic for "opaque and long" would reject ordinary deployment names,
// version strings and webhook paths.
inline bool LooksLikePastedCredential(std::wstring_view segment) {
  if (segment.size() < 12) return false;  // "sk-" plus fewer than 8 run characters
  const auto startsWith = [&](std::wstring_view prefix) {
    if (segment.size() <= prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      wchar_t c = segment[i];
      if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
      if (c != prefix[i]) return false;
    }
    return true;
  };
  if (!startsWith(L"sk-") && !startsWith(L"pk-") && !startsWith(L"rk-")) return false;
  for (std::size_t i = 3; i < segment.size(); ++i) {
    const wchar_t c = segment[i];
    const bool opaque = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                        (c >= L'0' && c <= L'9') || c == L'_' || c == L'-';
    if (!opaque) return false;
  }
  return true;
}

inline bool HasSecretQueryName(const std::wstring& query) {
  std::size_t start = 0;
  while (start <= query.size()) {
    const auto amp = query.find(L'&', start);
    const std::wstring pair = query.substr(
        start, amp == std::wstring::npos ? std::wstring::npos : amp - start);
    const auto eq = pair.find(L'=');
    std::wstring name = eq == std::wstring::npos ? pair : pair.substr(0, eq);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    for (std::wstring_view candidate : kSecretQueryNames)
      if (name == candidate) return true;
    if (amp == std::wstring::npos) break;
    start = amp + 1;
  }
  return false;
}

inline bool UrlCarriesSecret(const std::wstring& url) {
  const auto queryAt = url.find(L'?');
  const std::wstring authority =
      queryAt == std::wstring::npos ? url : url.substr(0, queryAt);

  // user:password@host -- credentials embedded in the authority. The '@' only counts if
  // it sits inside the authority component, i.e. before the first path slash; an '@'
  // further along belongs to the path, query or fragment.
  const auto schemeAt = authority.find(L"://");
  const auto hostStart = schemeAt == std::wstring::npos ? 0 : schemeAt + 3;
  const auto at = authority.find(L'@', hostStart);
  const auto pathStart = authority.find(L'/', hostStart);
  if (at != std::wstring::npos && (pathStart == std::wstring::npos || at < pathStart)) {
    const auto colon = authority.find(L':', hostStart);
    if (colon != std::wstring::npos && colon < at) return true;
  }

  if (queryAt != std::wstring::npos && HasSecretQueryName(url.substr(queryAt + 1))) return true;

  // A credential pasted into the path. The query check above cannot see this, and a key
  // here is stored in two plaintext files and echoed back by name in the chat error
  // message ("Endpoint=<path>"). Checked per segment so a URL like
  // /v1/models?key=... is caught by the query rule while /v1/sk-live-.../chat is caught
  // here, and neither rule can redact part of an ordinary path.
  const std::wstring path = queryAt == std::wstring::npos ? authority : url.substr(0, queryAt);
  std::size_t segmentStart = pathStart == std::wstring::npos ? path.size() : pathStart;
  while (segmentStart < path.size()) {
    auto next = path.find(L'/', segmentStart + 1);
    if (next == std::wstring::npos) next = path.size();
    const std::wstring segment = path.substr(segmentStart + 1, next - segmentStart - 1);
    if (LooksLikePastedCredential(segment)) return true;
    segmentStart = next;
  }
  return false;
}

}  // namespace miaodesk::api_url
