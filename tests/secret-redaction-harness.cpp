// Runs the real header. Compiled and executed, because a scanner whose rules are only
// asserted about in prose is not verified.
//
//   clang++ -std=c++23 -O1 -Wall -Wextra \
//       tests/secret-redaction-harness.cpp -o /tmp/secret-redaction-harness
//   /tmp/secret-redaction-harness
#include "miaodesk/SecretRedaction.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using miaodesk::secrets::RedactSecrets;
using miaodesk::secrets::SummarizeRemoteBody;
using miaodesk::secrets::kRedactedMarker;

namespace {

int failures = 0;

void Eq(const std::string& label, const std::wstring& got, const std::wstring& want) {
  if (got == want) {
    std::cout << "  ok   " << label << "\n";
    return;
  }
  ++failures;
  std::cout << "  FAIL " << label << "\n        want: \"";
  for (wchar_t c : want) std::cout << static_cast<char>(c < 128 ? c : '?');
  std::cout << "\"\n        got:  \"";
  for (wchar_t c : got) std::cout << static_cast<char>(c < 128 ? c : '?');
  std::cout << "\"\n";
}

void NoSecret(const std::string& label, const std::wstring& text) {
  if (text.find(L"sk-live-0123456789abcdef") == std::wstring::npos &&
      text.find(L"hunter2-secret-value") == std::wstring::npos) {
    std::cout << "  ok   " << label << "\n";
    return;
  }
  ++failures;
  std::cout << "  FAIL " << label << " leaked a secret\n";
}

}  // namespace

int main() {
  std::cout << "miaodesk::secrets::RedactSecrets\n";

  // --- the shapes a reflected request actually takes --------------------
  // An HTTP header echoed back verbatim.
  Eq("Authorization: Bearer <token>",
     RedactSecrets(L"upstream said: Authorization: Bearer sk-live-0123456789abcdef"),
     L"upstream said: Authorization: Bearer " + std::wstring(kRedactedMarker));
  // The scheme word keeps whatever casing the server sent; only the token is masked.
  Eq("lowercase authorization",
     RedactSecrets(L"authorization: bearer sk-live-0123456789abcdef"),
     L"authorization: bearer " + std::wstring(kRedactedMarker));
  // JSON, which is what most gateways echo.
  Eq("json api_key",
     RedactSecrets(L"{\"api_key\":\"sk-live-0123456789abcdef\"}"),
     L"{\"api_key\":\"" + std::wstring(kRedactedMarker) + L"\"}");
  Eq("json token = form",
     RedactSecrets(L"{\"token\": \"hunter2-secret-value\"}"),
     L"{\"token\": \"" + std::wstring(kRedactedMarker) + L"\"}");
  Eq("quoted Authorization header inside json",
     RedactSecrets(L"{\"Authorization\":\"Bearer sk-live-0123456789abcdef\"}"),
     L"{\"Authorization\":\"Bearer " + std::wstring(kRedactedMarker) + L"\"}");
  // A query string, which is where a key lands when someone pastes it into the URL.
  Eq("query string key",
     RedactSecrets(L"failed GET /v1/models?api_key=sk-live-0123456789abcdef&x=1"),
     L"failed GET /v1/models?api_key=" + std::wstring(kRedactedMarker) + L"&x=1");
  // A bare key with no field name at all.
  Eq("bare sk- token",
     RedactSecrets(L"echo: sk-live-0123456789abcdef done"),
     L"echo: " + std::wstring(kRedactedMarker) + L" done");
  // Underscore and hyphen spellings both name the same field.
  Eq("api-key hyphen", RedactSecrets(L"api-key=sk-live-0123456789abcdef"),
     L"api-key=" + std::wstring(kRedactedMarker));
  Eq("API_KEY upper", RedactSecrets(L"API_KEY: sk-live-0123456789abcdef"),
     L"API_KEY: " + std::wstring(kRedactedMarker));

  // --- what must survive untouched ---------------------------------------
  // Ordinary error text, including the words these fields could shadow.
  Eq("plain text untouched",
     RedactSecrets(L"模型列表请求返回 HTTP 502。Bad Gateway"),
     L"模型列表请求返回 HTTP 502。Bad Gateway");
  Eq("prose with the word key, no separator",
     RedactSecrets(L"the key to fixing this is a longer timeout"),
     L"the key to fixing this is a longer timeout");
  Eq("Content-Type is not a secret field",
     RedactSecrets(L"Content-Type: application/json"),
     L"Content-Type: application/json");
  Eq("a short token-shaped word is not a key",
     RedactSecrets(L"model = gpt-4o-mini"),
     L"model = gpt-4o-mini");
  // A bare key must start at a word boundary. "risk-assessment" contains "sk-" from its own
  // third letter, and without that check the whole word is redacted -- corrupting ordinary
  // error text to no benefit, since the word is not a secret.
  Eq("hyphenated word is not mistaken for a key",
     RedactSecrets(L"risk-assessment timed out after 30s"),
     L"risk-assessment timed out after 30s");
  Eq("nor is a slug that merely contains sk-",
     RedactSecrets(L"job task-sk-0123 failed"),
     L"job task-sk-0123 failed");
  // ...but a bare key at the very start of the text has no preceding character and must
  // still match.
  Eq("bare key at position zero",
     RedactSecrets(L"sk-live-0123456789abcdef is invalid"),
     std::wstring(kRedactedMarker) + L" is invalid");
  Eq("the field name is kept",
     RedactSecrets(L"api_key=sk-live-0123456789abcdef").find(L"api_key") != std::wstring::npos
       ? RedactSecrets(L"api_key=sk-live-0123456789abcdef")
       : L"",
     L"api_key=" + std::wstring(kRedactedMarker));
  // A field name that is a PREFIX of a longer one must not be shadowed by it.
  Eq("apikey is not read as key",
     RedactSecrets(L"apikey: sk-live-0123456789abcdef"),
     L"apikey: " + std::wstring(kRedactedMarker));
  // Two secrets in one line, both masked.
  Eq("two fields", RedactSecrets(L"api_key=sk-live-0123456789abcdef token=hunter2-secret-value"),
     L"api_key=" + std::wstring(kRedactedMarker) + L" token=" + std::wstring(kRedactedMarker));

  // --- SummarizeRemoteBody ----------------------------------------------
  std::cout << "\nmiaodesk::secrets::SummarizeRemoteBody\n";
  Eq("flattens newlines",
     SummarizeRemoteBody(L"line one\r\nline two\nline three"),
     L"line one line two line three");
  Eq("collapses runs of whitespace",
     SummarizeRemoteBody(L"a\t\tb     c"),
     L"a b c");
  const std::wstring summarized =
      SummarizeRemoteBody(L"{\"error\":\"bad\",\"Authorization\":\"Bearer sk-live-0123456789abcdef\"}");
  NoSecret("redacts as it summarizes", summarized);
  Eq("...and keeps the reason the server gave",
     summarized, L"{\"error\":\"bad\",\"Authorization\":\"Bearer " + std::wstring(kRedactedMarker) + L"\"}");
  NoSecret("full body", SummarizeRemoteBody(
      L"{\"message\":\"unauthorized\",\"headers\":{\"Authorization\":\"Bearer sk-live-0123456789abcdef\","
      L"\"x-api-key\":\"hunter2-secret-value\"},\"body\":\"nope\"}"));

  // Truncation must not split a surrogate pair: 🌈 is U+1F308 = D83C DF08.
  {
    const std::wstring body = std::wstring(60, L'x') + L"\U0001F308" + std::wstring(60, L'y');
    const std::wstring cut = SummarizeRemoteBody(body, 63);
    const bool endsWithLeadSurrogate =
        !cut.empty() && cut[cut.size() - 2] >= 0xD800 && cut[cut.size() - 2] <= 0xDBFF;
    Eq("no surrogate split at the cut", endsWithLeadSurrogate ? L"split" : L"intact", L"intact");
    Eq("the cut is marked", cut.back() == L'…' ? L"marked" : L"bare", L"marked");
    Eq("budget respected", cut.size() <= 64 ? L"within" : L"over", L"within");
  }
  Eq("short body is not truncated",
     SummarizeRemoteBody(L"tiny").size() == 4 ? L"as-is" : L"changed", L"as-is");

  // A body that is nothing but a secret must still come back redacted, not dropped.
  NoSecret("body is only a secret", SummarizeRemoteBody(L"sk-live-0123456789abcdef"));

  std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
  return failures == 0 ? 0 : 1;
}
