#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace miaodesk::secrets {

// Text that came off the network is not text you can show.
//
// L3Agent::ProbeModels splices up to 220 bytes of the server's response body into its
// error message. That message goes two places:
//
//   - the settings page's status line, where the user reads it; and
//   - profile.lastMessage, written into the plaintext profile INI by WriteIni.
//
// The second one is the reason this exists. Some endpoints echo the request back in their
// error body -- misconfigured proxies and debug gateways return the headers they were
// given, Authorization included. A key that reaches that INI is a key in plaintext config,
// which is what TODO API-03 exists to prevent, and it is shown on screen first, which is
// what API-02 exists to prevent. Both criteria are met today only by assuming no server
// ever reflects the request.
//
// Written as a scanner rather than a regex so it compiles and runs standalone, so the rules
// are inspectable in one place, and so the test can execute the real thing instead of
// asserting about it in prose. Redaction keeps the field NAME -- knowing which field leaked
// is what makes the message actionable -- and nothing else about the value.

inline constexpr const wchar_t* kRedactedMarker = L"<已隐藏>";

namespace detail {

constexpr std::array<std::wstring_view, 14> kSecretFieldNames{{
    L"proxy-authorization", L"authorization",   L"refresh_token",  L"refresh-token",
    L"access_token",       L"access-token",    L"credential",     L"password",
    L"api_key",            L"api-key",         L"secret",         L"apikey",
    L"token",              L"key",
}};

// Longest first. At a given position only one name can match, so this only matters for
// names where one is a prefix of another -- but the assert keeps it true as names are
// added, which is when it would stop being obvious.
static_assert([] {
  for (std::size_t i = 1; i < kSecretFieldNames.size(); ++i)
    if (kSecretFieldNames[i - 1].size() < kSecretFieldNames[i].size()) return false;
  return true;
}(), "kSecretFieldNames must stay longest-first, so no name is shadowed by a shorter one");

constexpr bool IsNameChar(wchar_t c) noexcept {
  return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
         (c >= L'0' && c <= L'9') || c == L'_' || c == L'-';
}

constexpr bool IsBoundary(wchar_t c) noexcept { return !IsNameChar(c); }

constexpr bool IsSpace(wchar_t c) noexcept { return c == L' ' || c == L'\t'; }

constexpr bool IsBlank(wchar_t c) noexcept {
  return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\v' || c == L'\f';
}

constexpr bool IsValueBreak(wchar_t c) noexcept {
  return c == L'\0' || c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
         c == L',' || c == L';' || c == L'&' || c == L'\"' || c == L'\'' ||
         c == L'}' || c == L']' || c == L')' || c == L'<' || c == L'>';
}

constexpr wchar_t Lower(wchar_t c) noexcept {
  return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

// Case-insensitive match of `name` at `at`; 0 if it does not match. The character after the
// name must be a boundary, so "apikeyx" does not match "apikey" and eat the trailing x.
constexpr std::size_t MatchName(const std::wstring& text, std::size_t at, std::wstring_view name) {
  if (at + name.size() > text.size()) return 0;
  for (std::size_t i = 0; i < name.size(); ++i)
    if (Lower(text[at + i]) != name[i]) return 0;
  if (at + name.size() < text.size() && !IsBoundary(text[at + name.size()])) return 0;
  return name.size();
}

// Optional quotes and spaces, then a real separator. Requiring ':' or '=' is what keeps
// ordinary prose from being redacted: "the key to this" has no colon.
struct Separator {
  std::size_t length{};
  bool found{};
};
constexpr Separator MatchSeparator(const std::wstring& text, std::size_t at) {
  Separator result;
  std::size_t i = at;
  bool sawColonOrEquals = false;
  while (i < text.size()) {
    const wchar_t c = text[i];
    if (c == L':' || c == L'=') {
      sawColonOrEquals = true;
      ++i;
      continue;
    }
    if (IsSpace(c) || c == L'\"' || c == L'\'') {
      ++i;
      continue;
    }
    break;
  }
  result.length = i - at;
  result.found = sawColonOrEquals;
  return result;
}

// "Bearer" as it appears after a field name, or on its own. Returns the length of the word
// (no trailing space) or 0. Handled separately from the field names because the scheme word
// itself is not the secret -- the token after it is -- and masking the pair together loses
// the word that tells the reader what was there.
constexpr std::size_t MatchBearerWord(const std::wstring& text, std::size_t at) {
  constexpr wchar_t kBearer[] = L"bearer";
  constexpr std::size_t kLen = 6;
  if (at + kLen > text.size()) return 0;
  for (std::size_t i = 0; i < kLen; ++i)
    if (Lower(text[at + i]) != kBearer[i]) return 0;
  if (at + kLen < text.size() && !IsSpace(text[at + kLen])) return 0;
  return kLen;
}

// A bare key of the shape scripts/check-private-files.ps1 greps tracked files for, so a key
// that appears with no field name and no separator is still masked.
//
// Requires a word boundary before it: without that, "risk-assessment" matches "sk-" from
// its own third letter and the whole word gets redacted, which corrupts ordinary text.
constexpr std::size_t MatchBareKey(const std::wstring& text, std::size_t at) {
  if (at > 0 && IsNameChar(text[at - 1])) return 0;
  if (at + 3 > text.size()) return 0;
  if (Lower(text[at]) != L's' || Lower(text[at + 1]) != L'k' || text[at + 2] != L'-') return 0;
  std::size_t run = 0;
  for (std::size_t i = at + 3; i < text.size(); ++i) {
    const wchar_t c = text[i];
    const bool keyChar = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                         (c >= L'0' && c <= L'9') || c == L'_' || c == L'-';
    if (!keyChar) break;
    ++run;
  }
  if (run < 8) return 0;  // short runs are ordinary identifiers
  return 3 + run;
}

inline std::size_t RunOf(const std::wstring& text, std::size_t at, bool (*stop)(wchar_t)) {
  std::size_t i = at;
  while (i < text.size() && !stop(text[i])) ++i;
  return i - at;
}

// Emits the redacted form of the secret that begins at `at`, and returns how many
// characters it consumed. Returns 0 when there is nothing to redact there, in which case
// nothing was appended and the caller must copy the character itself.
//
// Both redaction shapes live here so they cannot drift: `field: <value>` and
// `field: Bearer <token>` share the name and separator, and differ only in whether the
// scheme word is preserved.
inline std::size_t EmitRedactedSpan(const std::wstring& text, std::size_t at, std::wstring& out) {
  for (std::wstring_view name : kSecretFieldNames) {
    const std::size_t matched = MatchName(text, at, name);
    if (matched == 0) continue;
    const Separator separator = MatchSeparator(text, at + matched);
    if (!separator.found) continue;

    const std::size_t valueAt = at + matched + separator.length;
    out.append(text, at, matched);
    out.append(text, at + matched, separator.length);

    if (const std::size_t bearer = MatchBearerWord(text, valueAt); bearer > 0) {
      std::size_t tokenAt = valueAt + bearer;
      while (tokenAt < text.size() && IsSpace(text[tokenAt])) ++tokenAt;
      const std::size_t token = RunOf(text, tokenAt, IsValueBreak);
      out.append(text, valueAt, bearer);
      out.push_back(L' ');
      out.append(token > 0 ? kRedactedMarker : L"");
      return (matched + separator.length) + bearer + (tokenAt - (valueAt + bearer)) + token;
    }

    const std::size_t value = RunOf(text, valueAt, IsValueBreak);
    if (value == 0) {
      // Nothing to hide: an empty string, or the value on the next line. Rewind and let
      // the caller copy the name through untouched rather than inserting noise.
      out.resize(out.size() - (matched + separator.length));
      return 0;
    }
    out.append(kRedactedMarker);
    return matched + separator.length + value;
  }

  if (const std::size_t bearer = MatchBearerWord(text, at); bearer > 0) {
    std::size_t tokenAt = at + bearer;
    while (tokenAt < text.size() && IsSpace(text[tokenAt])) ++tokenAt;
    const std::size_t token = RunOf(text, tokenAt, IsValueBreak);
    out.append(text, at, bearer);
    out.push_back(L' ');
    out.append(token > 0 ? kRedactedMarker : L"");
    return bearer + (tokenAt - (at + bearer)) + token;
  }

  if (const std::size_t key = MatchBareKey(text, at); key > 0) {
    out.append(kRedactedMarker);
    return key;
  }
  return 0;
}

}  // namespace detail

// Replaces secret-bearing spans with kRedactedMarker, keeping each field name.
inline std::wstring RedactSecrets(const std::wstring& text) {
  std::wstring out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const std::size_t consumed = detail::EmitRedactedSpan(text, i, out);
    if (consumed > 0) {
      i += consumed;
      continue;
    }
    out.push_back(text[i]);
    ++i;
  }
  return out;
}

// Flattens to a single line, redacts, and cuts to `budget` characters.
//
// The flattening matters for more than tidiness. lastMessage is written into an INI with
// WritePrivateProfileString, and a value containing newlines can forge keys in the file
// that reads it back -- so a server body is untrusted for file structure as well as for
// secrets.
inline std::wstring SummarizeRemoteBody(std::wstring body, std::size_t budget = 220) {
  std::wstring flat;
  flat.reserve(body.size());
  bool pendingSpace = false;
  for (wchar_t c : body) {
    if (detail::IsBlank(c) || c < 0x20 || c == 0x7F) {
      pendingSpace = true;
      continue;
    }
    if (pendingSpace && !flat.empty()) flat.push_back(L' ');
    pendingSpace = false;
    flat.push_back(c);
  }
  flat = RedactSecrets(flat);
  if (flat.size() <= budget) return flat;
  // Never cut between a surrogate pair: that turns one code point into two replacement
  // characters and, worse, can leave a marker that no longer matches anything.
  std::size_t cut = budget;
  while (cut > 0 && flat[cut] >= 0xD800 && flat[cut] <= 0xDBFF) ++cut;
  flat.resize(cut);
  flat += L'…';
  return flat;
}

}  // namespace miaodesk::secrets
