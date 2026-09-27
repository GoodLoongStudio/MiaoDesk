// Compiled and executed, because the alternative is asserting about text.
//
// UrlCarriesSecret used to live in an anonymous namespace inside
// DesktopAiSettingsPage.cpp. The round that introduced it did compile and run the real
// bytes against 17 cases, but left no test behind -- and noted the gap: "回归只由
// tests/settings-url-secret-guard.mjs 做存在性与覆盖断言，它证明不了解析正确."
//
// So the logic was moving again with nothing that would notice. It is now in
// ApiUrlSecretPolicy.h, where it can be run, and this file runs it.
//
//   clang++ -std=c++23 -O1 -Wall -Wextra -I src/include \
//       tests/api-url-secret-policy-harness.cpp -o /tmp/api-url-policy
//   /tmp/api-url-policy
#include "miaodesk/ApiUrlSecretPolicy.h"

#include <iostream>
#include <string>
#include <vector>

using miaodesk::api_url::UrlCarriesSecret;

namespace {

int failures = 0;

void Case(const std::string& label, const std::wstring& url, bool want) {
  const bool got = UrlCarriesSecret(url);
  if (got == want) {
    std::cout << "  ok   " << label << "\n";
    return;
  }
  ++failures;
  std::cout << "  FAIL " << label << " (want " << (want ? "reject" : "accept")
            << ", got " << (got ? "reject" : "accept") << ")\n";
}

}  // namespace

int main() {
  std::cout << "miaodesk::api_url::UrlCarriesSecret\n\n";
  std::cout << "-- normal URLs must be accepted -----------------------------\n";
  Case("plain https", L"https://api.openai.com/v1", false);
  Case("with path", L"https://api.openai.com/v1/chat/completions", false);
  Case("localhost port", L"http://127.0.0.1:11434/v1", false);
  Case("model-ish path segment", L"https://api.example.com/v1/gpt-4o-mini/chat", false);
  Case("query with no secret name", L"https://api.example.com/v1?version=2024-01&x=1", false);
  Case("subdomain with key in the HOST is not a secret param",
       L"https://api-key.example.com/v1", false);
  Case("trailing slash", L"https://api.example.com/v1/", false);
  Case("a path segment named key is fine without a separator",
       L"https://api.example.com/v1/key/rotate", false);
  Case("deployment name with digits", L"https://x.openai.azure.com/openai/deployments/gpt4/chat", false);
  // The false positive that the whole design turns on: these MUST be accepted.
  Case("webhook path with a long opaque id", L"https://hooks.example.com/services/T0001/B0002/abcdefXYZ", false);
  Case("version-like segment", L"https://api.example.com/v1/2024-01-15-preview/models", false);

  std::cout << "\n-- userinfo in the authority --------------------------------\n";
  Case("user:password@host", L"https://user:password@api.example.com/v1", true);
  Case("user:@host still embeds a credential", L"https://user:@api.example.com/v1", true);
  // An '@' after the first path slash belongs to the path, not the authority.
  Case("@ in the path is not userinfo", L"https://api.example.com/v1/@me/models", false);
  Case("email-shaped host still has no colon", L"https://api.example.com/v1", false);

  std::cout << "\n-- secret-named query parameters ----------------------------\n";
  Case("?key=", L"https://api.example.com/v1?key=abc", true);
  Case("?api_key=", L"https://api.example.com/v1?api_key=abc", true);
  Case("?API_TOKEN= is case-insensitive", L"https://api.example.com/v1?API_TOKEN=abc", true);
  Case("?authorization=", L"https://api.example.com/v1?authorization=abc", true);
  Case("second parameter is the secret", L"https://api.example.com/v1?x=1&token=abc", true);
  Case("?client_secret=", L"https://api.example.com/v1?client_secret=abc", true);
  Case("?api_token= is case-insensitive", L"https://api.example.com/v1?api_token=abc", true);
  // `api_token` was missing from the original name table, and the original test never
  // ran the function, so nothing noticed.
  Case("?x-api-key=", L"https://api.example.com/v1?x-api-key=abc", true);
  Case("?secret_key=", L"https://api.example.com/v1?secret_key=abc", true);
  Case("?access-token= hyphen form", L"https://api.example.com/v1?access-token=abc", true);
  Case("?key= with nothing after it", L"https://api.example.com/v1?key=", true);
  Case("a param merely NAMED like a word is not enough",
       L"https://api.example.com/v1?monkeys=3", false);

  std::cout << "\n-- a credential pasted into the PATH ------------------------\n";
  // The query rules cannot see this, and a key here lands in api-profiles.ini,
  // PiAgent/models.json and Harness settings.yaml -- three plaintext files -- and is
  // echoed back in the chat error's "Endpoint=<path>".
  Case("path segment sk-", L"https://api.example.com/v1/sk-live-0123456789abcdef/chat", true);
  Case("path segment is only the key", L"https://api.example.com/sk-live-0123456789abcdef", true);
  Case("path segment pk-", L"https://api.example.com/v1/pk-live-0123456789abcdef/chat", true);
  Case("path segment rk-", L"https://api.example.com/v1/rk-live-0123456789abcdef/chat", true);
  Case("short sk- fragment is not a credential", L"https://api.example.com/v1/sk-abc", false);
  Case("sk- inside a longer word is not a segment",
       L"https://api.example.com/v1/risk-assessment", false);
  Case("sk- with a space cannot occur in a URL segment",
       L"https://api.example.com/v1/sk-live abc", false);

  std::cout << "\n-- degenerate input ----------------------------------------\n";
  Case("empty", L"", false);
  Case("just a scheme", L"https://", false);
  Case("no scheme, path only", L"api.example.com/v1", false);

  std::cout << (failures == 0 ? "\nALL PASS\n" : "\nFAILURES\n");
  return failures == 0 ? 0 : 1;
}
