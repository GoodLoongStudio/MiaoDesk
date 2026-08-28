#include "turingdesk/GeneratedDesktopPreview.h"

#include "turingdesk/A2UIParser.h"
#include "turingdesk/DesktopControlService.h"
#include "turingdesk/WallpaperPackage.h"
#include "turingdesk/WidgetIntentComposer.h"

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <WebView2.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace turingdesk::preview {
namespace {

constexpr wchar_t kSearchWindowClass[] = L"TuringDesk.Native.SearchWindow";
constexpr wchar_t kPreviewWindowClass[] = L"TuringDesk.GeneratedDesktopPreview";
constexpr int kApplyButtonId = 7101;
constexpr int kRejectButtonId = 7102;
constexpr std::uintmax_t kMaxImageBytes = 25ull * 1024ull * 1024ull;
constexpr std::uintmax_t kMaxVideoBytes = 250ull * 1024ull * 1024ull;

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}

std::optional<std::string> JsonString(std::string_view json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    auto pos = json.find(token);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + token.size());
    if (pos == std::string_view::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;
    std::string out;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') return out;
        if (ch != '\\') { out.push_back(ch); continue; }
        if (pos >= json.size()) return std::nullopt;
        const char esc = json[pos++];
        switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

bool WriteUtf8(const fs::path& path, std::string_view content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(stream);
}

std::string ReadUtf8(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

fs::path TempPreviewRoot() {
    std::error_code ec;
    auto root = fs::temp_directory_path(ec);
    if (ec) return {};
    root /= L"TuringDesk";
    root /= L"AI_Generated";
    fs::create_directories(root, ec);
    return ec ? fs::path{} : root;
}

fs::path LocalGeneratedWallpaperRoot() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw) return {};
    fs::path root(raw);
    CoTaskMemFree(raw);
    root /= L"TuringDesk";
    root /= L"GeneratedWallpapers";
    std::error_code ec;
    fs::create_directories(root, ec);
    return ec ? fs::path{} : root;
}

std::wstring NewPreviewId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        return L"preview-" + std::to_wstring(ticks);
    }
    wchar_t text[64]{};
    StringFromGUID2(guid, text, static_cast<int>(std::size(text)));
    std::wstring id(text);
    id.erase(std::remove_if(id.begin(), id.end(), [](wchar_t ch) { return ch == L'{' || ch == L'}'; }), id.end());
    return id;
}

bool IsHttps(std::string_view value) {
    return value.size() > 8 && _strnicmp(value.data(), "https://", 8) == 0;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

bool AllowedImageExtension(const fs::path& path) {
    const auto ext = Lower(path.extension().wstring());
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".webp" || ext == L".bmp";
}

bool AllowedVideoExtension(const fs::path& path) {
    const auto ext = Lower(path.extension().wstring());
    return ext == L".mp4" || ext == L".webm" || ext == L".mov" || ext == L".m4v";
}

bool CopyLocalAssetIntoPreview(const fs::path& source, const fs::path& previewDir, bool video, fs::path& copied, std::wstring& error) {
    std::error_code ec;
    auto canonical = fs::weakly_canonical(source, ec);
    if (ec || canonical.empty() || !fs::is_regular_file(canonical, ec)) {
        error = L"壁纸资源不是有效的本地文件。";
        return false;
    }
    if (video ? !AllowedVideoExtension(canonical) : !AllowedImageExtension(canonical)) {
        error = video ? L"视频壁纸只允许 mp4/webm/mov/m4v。" : L"图片壁纸只允许 png/jpg/jpeg/webp/bmp。";
        return false;
    }
    const auto size = fs::file_size(canonical, ec);
    if (ec || size > (video ? kMaxVideoBytes : kMaxImageBytes)) {
        error = video ? L"视频预览资源超过 250 MiB 限制。" : L"图片预览资源超过 25 MiB 限制。";
        return false;
    }
    copied = previewDir / (L"asset" + canonical.extension().wstring());
    fs::copy_file(canonical, copied, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = L"无法把壁纸资源复制到沙盒临时目录。";
        return false;
    }
    return true;
}

bool NotifyMainProcess(const fs::path& previewDir) {
    const HWND target = FindWindowW(kSearchWindowClass, nullptr);
    if (!target) return false;
    const std::wstring value = previewDir.wstring();
    COPYDATASTRUCT data{};
    data.dwData = kCopyDataTag;
    data.cbData = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(value.c_str());
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &result) != 0 && result != 0;
}

std::string EscapeJsonString(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch >= 0x20) out.push_back(static_cast<char>(ch));
            break;
        }
    }
    return out;
}

std::string Base64(std::string_view bytes) {
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < bytes.size()) {
        const unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) |
                               (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                               static_cast<unsigned char>(bytes[i + 2]);
        out.push_back(table[(value >> 18) & 63]);
        out.push_back(table[(value >> 12) & 63]);
        out.push_back(table[(value >> 6) & 63]);
        out.push_back(table[value & 63]);
        i += 3;
    }
    if (i < bytes.size()) {
        unsigned value = static_cast<unsigned char>(bytes[i]) << 16;
        out.push_back(table[(value >> 18) & 63]);
        if (i + 1 < bytes.size()) {
            value |= static_cast<unsigned char>(bytes[i + 1]) << 8;
            out.push_back(table[(value >> 12) & 63]);
            out.push_back(table[(value >> 6) & 63]);
            out.push_back('=');
        } else {
            out.push_back(table[(value >> 12) & 63]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

std::string FileToDataUri(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    auto ext = Lower(path.extension().wstring());
    std::string mime = "application/octet-stream";
    if (ext == L".png") mime = "image/png";
    else if (ext == L".jpg" || ext == L".jpeg") mime = "image/jpeg";
    else if (ext == L".webp") mime = "image/webp";
    else if (ext == L".bmp") mime = "image/bmp";
    return "data:" + mime + ";base64," + Base64(bytes);
}

std::string TrustedWidgetRendererHtml(std::string_view modelJson, bool desktopMode) {
    const std::string encoded = Base64(modelJson);
    std::string html;
    html.reserve(14000 + encoded.size());
    html += R"HTML(<!doctype html><html><head><meta charset="utf-8"><style>
html,body{width:100%;height:100%;margin:0;overflow:hidden;background:transparent;font-family:"Segoe UI Variable Text","Segoe UI",sans-serif;color:#1f2937}
*{box-sizing:border-box}.stage{position:relative;width:100%;height:100%;overflow:hidden}.preview-bg{position:absolute;inset:0;background:radial-gradient(circle at 20% 10%,#b9d8ff 0,#dce9f7 36%,#b7c9df 100%);opacity:.72}
.node{position:absolute;overflow:hidden}.card{box-shadow:0 18px 48px rgba(38,55,80,.18);border:1px solid rgba(255,255,255,.65);backdrop-filter:blur(18px)}
.title{font-size:18px;font-weight:650;margin-bottom:4px}.subtitle{font-size:28px;font-weight:650}.list{margin:0;padding-left:20px;line-height:1.6}.weather-main{font-size:28px;font-weight:650}.muted{opacity:.68}
button.node{border:0;font:inherit}
</style></head><body><div id="stage" class="stage">)HTML";
    if (!desktopMode) html += R"HTML(<div class="preview-bg"></div>)HTML";
    html += R"HTML(</div><script>
const raw=Uint8Array.from(atob(')HTML";
    html += encoded;
    html += R"HTML('),c=>c.charCodeAt(0));const doc=JSON.parse(new TextDecoder().decode(raw));const stage=document.getElementById('stage');
function applyLayout(el,l,root){if(root&&)HTML";
    html += desktopMode ? "true" : "false";
    html += R"HTML(){el.style.left='0';el.style.top='0';el.style.width='100%';el.style.height='100%';return;}el.style.left=(l.x*100)+'%';el.style.top=(l.y*100)+'%';el.style.width=(l.width*100)+'%';el.style.height=(l.height*100)+'%';}
function style(el,p){if(p.background)el.style.background=p.background;if(p.foreground)el.style.color=p.foreground;if(p.opacity!==undefined)el.style.opacity=p.opacity;if(p.cornerRadius!==undefined)el.style.borderRadius=p.cornerRadius+'px';if(p.padding!==undefined)el.style.padding=p.padding+'px';if(p.fontSize!==undefined)el.style.fontSize=p.fontSize+'px';if(p.fontWeight)el.style.fontWeight=({normal:'400',medium:'500',semibold:'600',bold:'700'})[p.fontWeight];}
function render(n,parent,root=false){let el=document.createElement(n.type==='Button'?'button':'div');el.className='node '+n.type.toLowerCase();applyLayout(el,n.layout,root);style(el,n.props||{});if(n.type==='Card'){el.classList.add('card');if(n.props.title){const t=document.createElement('div');t.className='title';t.textContent=n.props.title;el.appendChild(t)}if(n.props.subtitle){const s=document.createElement('div');s.className='subtitle';s.textContent=n.props.subtitle;el.appendChild(s)}for(const c of (n.props.children||[]))render(c,el,false)}else if(n.type==='Text'){el.textContent=n.props.text}else if(n.type==='Button'){el.textContent=n.props.text;el.disabled=true}else if(n.type==='List'){const list=document.createElement(n.props.ordered?'ol':'ul');list.className='list';for(const item of n.props.items){const li=document.createElement('li');li.textContent=item;list.appendChild(li)}el.appendChild(list)}else if(n.type==='Weather'){const main=document.createElement('div');main.className='weather-main';main.textContent='21°';const loc=document.createElement('div');loc.className='muted';loc.textContent=n.props.location+(n.props.showForecast?' · 晴':'');el.append(main,loc)}parent.appendChild(el);}
render(doc,stage,true);
</script></body></html>)HTML";
    return html;
}

std::string TrustedWallpaperRendererHtml(std::string_view mode, std::string_view value) {
    std::string payload = "{\"mode\":\"" + EscapeJsonString(mode) + "\",\"value\":\"" + EscapeJsonString(value) + "\"}";
    const std::string encoded = Base64(payload);
    std::string html;
    html.reserve(10000 + encoded.size());
    html += R"HTML(<!doctype html><html><head><meta charset="utf-8"><style>
html,body,#root{width:100%;height:100%;margin:0;overflow:hidden;background:#07111f}#root{position:relative}img,video{width:100%;height:100%;object-fit:cover}.preset{position:absolute;inset:-12%;filter:saturate(1.12)}
.aurora_flow{background:radial-gradient(circle at 78% 16%,rgba(236,244,255,.95),transparent 12%),radial-gradient(ellipse at 22% 18%,rgba(81,157,255,.75),transparent 34%),radial-gradient(ellipse at 58% 42%,rgba(158,104,255,.55),transparent 38%),linear-gradient(180deg,#081b38 0%,#0d2a3f 72%,#071423 100%);animation:drift 10s ease-in-out infinite alternate}
.neon_flow{background:radial-gradient(circle at 50% 58%,rgba(255,170,48,.95),transparent 18%),radial-gradient(circle at 50% 58%,rgba(255,88,120,.55),transparent 28%),linear-gradient(180deg,#12051f 0%,#2a0a2a 58%,#12051f 100%);animation:drift 8s ease-in-out infinite alternate}
.neon_flow:after{content:"";position:absolute;inset:42% -20% -30%;background:linear-gradient(90deg,transparent,rgba(8,236,255,.35),rgba(255,36,220,.28),transparent),repeating-linear-gradient(90deg,rgba(8,236,255,.22) 0 1px,transparent 1px 56px);transform:perspective(420px) rotateX(68deg);animation:road 5s linear infinite}
.ocean_glass{background:radial-gradient(ellipse at 50% 0%,rgba(178,240,255,.95),transparent 34%),linear-gradient(180deg,#2db7df 0%,#1178b8 35%,#06598f 62%,#07385e 100%);animation:ocean 7s ease-in-out infinite alternate}
.ocean_glass:after{content:"";position:absolute;inset:0;background:repeating-radial-gradient(ellipse at 50% 0,rgba(255,255,255,.18) 0 2px,transparent 3px 34px);mix-blend-mode:screen;transform:perspective(500px) rotateX(60deg) scale(1.5);animation:water 4s linear infinite}
@keyframes drift{to{transform:translate3d(4%,-2%,0) scale(1.08)}}@keyframes road{to{transform:perspective(420px) rotateX(68deg) translateY(56px)}}@keyframes ocean{to{filter:hue-rotate(8deg) saturate(1.2);transform:scale(1.04)}}@keyframes water{to{background-position:80px 40px}}
</style></head><body><div id="root"></div><script>
const raw=Uint8Array.from(atob(')HTML";
    html += encoded;
    html += R"HTML('),c=>c.charCodeAt(0));const p=JSON.parse(new TextDecoder().decode(raw));const root=document.getElementById('root');if(p.mode==='preset'){const el=document.createElement('div');el.className='preset '+p.value;root.appendChild(el)}else if(p.mode==='image'){const img=document.createElement('img');img.src=p.value;root.appendChild(img)}else if(p.mode==='video'){const v=document.createElement('video');v.src=p.value;v.autoplay=true;v.loop=true;v.muted=true;v.playsInline=true;root.appendChild(v)}
</script></body></html>)HTML";
    return html;
}

std::string WallpaperPreviewValue(const fs::path& dir, std::string_view mode, std::string_view source) {
    if (mode == "preset") return std::string(source);
    if (IsHttps(source)) return std::string(source);
    const fs::path local = dir / Utf8ToWide(source);
    if (mode == "image") return FileToDataUri(local);
    // Local video file URLs are passed as data rather than model-produced markup.
    std::wstring absolute = fs::absolute(local).wstring();
    std::replace(absolute.begin(), absolute.end(), L'\\', L'/');
    return "file:///" + WideToUtf8(absolute);
}

NativeToolResult CreateWidgetPreview(std::string_view arguments) {
    auto title = JsonString(arguments, "title").value_or("AI Widget");
    auto a2uiJson = JsonString(arguments, "a2ui_json");
    auto exampleKey = JsonString(arguments, "example_key");
    std::string document;
    if (exampleKey && !exampleKey->empty()) document = a2ui::BuiltInWidgetExample(*exampleKey);
    else if (a2uiJson) document = *a2uiJson;
    if (document.empty()) return {false, L"缺少 A2UI JSON，或示例 key 不存在。"};

    const auto validated = a2ui::ValidateWidgetDocument(document);
    if (!validated.success) return {false, validated.message};

    const auto root = TempPreviewRoot();
    if (root.empty()) return {false, L"无法创建 AI 生成内容临时目录。"};
    const auto id = NewPreviewId();
    const auto dir = root / id;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return {false, L"无法创建小组件沙盒目录。"};

    if (!WriteUtf8(dir / L"kind.txt", "widget") ||
        !WriteUtf8(dir / L"title.txt", title) ||
        !WriteUtf8(dir / L"payload.json", validated.normalizedJson)) {
        fs::remove_all(dir, ec);
        return {false, L"无法写入小组件沙盒描述。"};
    }

    if (!NotifyMainProcess(dir)) {
        fs::remove_all(dir, ec);
        return {false, L"沙盒已生成，但没有找到正在运行的 TuringDesk 主进程来展示预览。"};
    }
    return {true, L"小组件已进入沙盒预览。只有你点击“应用”后才会添加到桌面。preview=" + id};
}

bool WriteWidgetPreviewSandbox(const fs::path& dir, std::wstring_view title, std::string_view document, std::wstring& error) {
    const auto validated = a2ui::ValidateWidgetDocument(document);
    if (!validated.success) {
        error = validated.message.empty() ? L"小组件 JSON 校验失败。" : validated.message;
        return false;
    }

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = L"无法创建小组件沙盒目录。";
        return false;
    }

    const std::string titleUtf8 = WideToUtf8(title.empty() ? L"AI Widget" : std::wstring(title));
    if (!WriteUtf8(dir / L"kind.txt", "widget") ||
        !WriteUtf8(dir / L"title.txt", titleUtf8) ||
        !WriteUtf8(dir / L"payload.json", validated.normalizedJson)) {
        fs::remove_all(dir, ec);
        error = L"无法写入小组件沙盒描述。";
        return false;
    }
    return true;
}

NativeToolResult CreateWallpaperPreview(std::string_view arguments) {
    const std::string title = JsonString(arguments, "title").value_or("AI Wallpaper");
    std::string mode = JsonString(arguments, "mode").value_or("preset");
    std::string source = JsonString(arguments, "source").value_or("");
    const std::string example = JsonString(arguments, "example_key").value_or("");

    if (!example.empty()) {
        mode = "preset";
        if (example == "aurora_flow") source = "aurora_flow";
        else if (example == "neon_flow") source = "neon_flow";
        else if (example == "ocean_glass") source = "ocean_glass";
        else return {false, L"未知内置壁纸示例。"};
    }

    if (mode != "preset" && mode != "image" && mode != "video") return {false, L"壁纸预览 mode 只允许 preset/image/video。"};
    if (mode == "preset" && source != "aurora_flow" && source != "neon_flow" && source != "ocean_glass")
        return {false, L"动态壁纸只允许应用内置 aurora_flow / neon_flow / ocean_glass preset。"};
    if (mode != "preset" && source.empty()) return {false, L"图片/视频壁纸缺少 source。"};

    const auto root = TempPreviewRoot();
    if (root.empty()) return {false, L"无法创建 AI 生成内容临时目录。"};
    const auto id = NewPreviewId();
    const auto dir = root / id;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return {false, L"无法创建壁纸沙盒目录。"};

    std::string storedSource = source;
    if (mode != "preset" && !IsHttps(source)) {
        fs::path copied;
        std::wstring error;
        if (!CopyLocalAssetIntoPreview(fs::path(Utf8ToWide(source)), dir, mode == "video", copied, error)) {
            fs::remove_all(dir, ec);
            return {false, std::move(error)};
        }
        storedSource = WideToUtf8(copied.filename().wstring());
    }

    if (!WriteUtf8(dir / L"kind.txt", "wallpaper") ||
        !WriteUtf8(dir / L"title.txt", title) ||
        !WriteUtf8(dir / L"mode.txt", mode) ||
        !WriteUtf8(dir / L"source.txt", storedSource)) {
        fs::remove_all(dir, ec);
        return {false, L"无法写入壁纸沙盒描述。"};
    }

    if (!NotifyMainProcess(dir)) {
        fs::remove_all(dir, ec);
        return {false, L"沙盒已生成，但没有找到正在运行的 TuringDesk 主进程来展示预览。"};
    }
    return {true, L"壁纸已进入沙盒预览。当前桌面没有被修改；只有你点击“应用”后才会生效。preview=" + id};
}

NativeToolResult ExamplesCatalog() {
    return {true,
        L"内置展示：壁纸 aurora_flow / neon_flow / ocean_glass；小组件 today_tasks / focus_clock / weather_glass / system_pulse。所有示例也必须先预览再应用。"};
}

struct PreviewState {
    HWND hwnd{};
    HWND applyButton{};
    HWND rejectButton{};
    fs::path dir;
    std::string kind;
    std::string title;
    std::string mode;
    std::string source;
    std::string payload;
    bool committed{};
    bool closed{};
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
};

void CleanupPreview(const std::shared_ptr<PreviewState>& state) {
    if (!state) return;
    if (state->controller) {
        state->controller->Close();
        state->controller.Reset();
    }
    state->webview.Reset();
    std::error_code ec;
    fs::remove_all(state->dir, ec);
}

void ResizePreviewWebView(const std::shared_ptr<PreviewState>& state) {
    if (!state || !state->hwnd) return;
    RECT client{};
    GetClientRect(state->hwnd, &client);
    const int width = std::max(1L, client.right - client.left);
    const int height = std::max(1L, client.bottom - client.top);
    const int footer = 58;
    if (state->controller) {
        RECT bounds{12, 12, width - 12, std::max(13, height - footer)};
        state->controller->put_Bounds(bounds);
    }
    if (state->applyButton) MoveWindow(state->applyButton, width - 210, height - 44, 92, 32, TRUE);
    if (state->rejectButton) MoveWindow(state->rejectButton, width - 108, height - 44, 92, 32, TRUE);
}

std::string BuildPreviewHtml(const std::shared_ptr<PreviewState>& state) {
    if (state->kind == "widget") return TrustedWidgetRendererHtml(state->payload, false);
    const std::string value = WallpaperPreviewValue(state->dir, state->mode, state->source);
    return TrustedWallpaperRendererHtml(state->mode, value);
}

void BeginWebView(const std::shared_ptr<PreviewState>& state) {
    if (!state || !state->hwnd) return;
    const auto userData = (TempPreviewRoot().parent_path() / L"WebView2Preview").wstring();
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [state](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(hr) || !environment || state->closed || !IsWindow(state->hwnd)) return S_OK;
                return environment->CreateCoreWebView2Controller(
                    state->hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [state](HRESULT controllerHr, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerHr) || !controller || state->closed || !IsWindow(state->hwnd)) return S_OK;
                            state->controller = controller;
                            if (FAILED(controller->get_CoreWebView2(state->webview.GetAddressOf())) || !state->webview) return S_OK;

                            ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(state->controller.As(&controller2)) && controller2) {
                                COREWEBVIEW2_COLOR transparent{0x00, 0xFF, 0xFF, 0xFF};
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(state->webview->get_Settings(settings.GetAddressOf())) && settings) {
                                settings->put_IsScriptEnabled(TRUE); // trusted application renderer only
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                            }

                            ResizePreviewWebView(state);
                            const auto html = Utf8ToWide(BuildPreviewHtml(state));
                            state->webview->NavigateToString(html.c_str());
                            return S_OK;
                        }).Get());
            }).Get());
}

bool LoadPreviewState(const fs::path& dir, std::shared_ptr<PreviewState>& state, std::wstring& error) {
    std::error_code ec;
    const auto root = fs::weakly_canonical(TempPreviewRoot(), ec);
    const auto candidate = fs::weakly_canonical(dir, ec);
    if (ec || root.empty() || candidate.empty()) { error = L"预览路径无效。"; return false; }
    const auto rootText = Lower(root.wstring());
    const auto candidateText = Lower(candidate.wstring());
    if (candidateText.size() <= rootText.size() || candidateText.compare(0, rootText.size(), rootText) != 0) {
        error = L"拒绝打开沙盒目录之外的预览。";
        return false;
    }

    auto loaded = std::make_shared<PreviewState>();
    loaded->dir = candidate;
    loaded->kind = ReadUtf8(candidate / L"kind.txt");
    loaded->title = ReadUtf8(candidate / L"title.txt");
    if (loaded->kind == "widget") {
        loaded->payload = ReadUtf8(candidate / L"payload.json");
        const auto validated = a2ui::ValidateWidgetDocument(loaded->payload);
        if (!validated.success) { error = validated.message; return false; }
        loaded->payload = validated.normalizedJson;
    } else if (loaded->kind == "wallpaper") {
        loaded->mode = ReadUtf8(candidate / L"mode.txt");
        loaded->source = ReadUtf8(candidate / L"source.txt");
        if (loaded->mode != "preset" && loaded->mode != "image" && loaded->mode != "video") { error = L"壁纸预览模式非法。"; return false; }
    } else {
        error = L"未知沙盒预览类型。";
        return false;
    }
    state = std::move(loaded);
    return true;
}

bool ApplyWidget(const std::shared_ptr<PreviewState>& state, std::wstring& message) {
    const auto validated = a2ui::ValidateWidgetDocument(state->payload); // second validation at commit boundary
    if (!validated.success) { message = validated.message; return false; }

    desktop::WebWidgetCreateRequest request;
    request.title = validated.title.empty()
        ? Utf8ToWide(state->title.empty() ? "AI Widget" : state->title)
        : validated.title;
    request.htmlUtf8 = TrustedWidgetRendererHtml(validated.normalizedJson, true);
    request.x = validated.placement.x;
    request.y = validated.placement.y;
    request.width = validated.placement.width;
    request.height = validated.placement.height;

    desktop::DesktopControlService service;
    wallpaper::DesktopWidget created;
    const auto result = service.CreateWebWidget(request, &created);
    message = result.success ? (L"小组件已应用到桌面：" + created.title) : result.message;
    return result.success;
}

bool ApplyWallpaper(const std::shared_ptr<PreviewState>& state, std::wstring& message) {
    if (state->mode != "preset" && state->mode != "image" && state->mode != "video") {
        message = L"壁纸预览已失效。";
        return false;
    }

    const auto root = LocalGeneratedWallpaperRoot();
    if (root.empty()) { message = L"无法创建 TuringDesk 托管壁纸目录。"; return false; }
    const std::wstring packageName = L"AI-" + NewPreviewId() + L".tdwall";
    const fs::path package = root / packageName;

    std::string trustedSource = state->source;
    fs::path copiedAsset;
    if (state->mode != "preset" && !IsHttps(state->source)) {
        const fs::path source = state->dir / Utf8ToWide(state->source);
        std::error_code ec;
        if (!fs::is_regular_file(source, ec)) { message = L"沙盒壁纸资源已经不存在。"; return false; }
        trustedSource = "asset" + WideToUtf8(source.extension().wstring());
        copiedAsset = source;
    }

    const auto html = TrustedWallpaperRendererHtml(state->mode, trustedSource);
    std::wstring packageError;
    if (!wallpaper::WallpaperPackage::CreateWeb(
            package,
            Utf8ToWide(state->title.empty() ? "AI Wallpaper" : state->title),
            html,
            L"ai-preview-approved",
            L"TuringDesk",
            &packageError)) {
        message = packageError.empty() ? L"无法创建托管壁纸包。" : packageError;
        return false;
    }

    if (!copiedAsset.empty()) {
        std::error_code ec;
        fs::copy_file(copiedAsset, package / Utf8ToWide(trustedSource), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove_all(package, ec);
            message = L"无法把已确认的壁纸资源复制到托管包。";
            return false;
        }
    }

    desktop::DesktopControlService service;
    const auto result = service.ApplyWebPackage(package);
    if (!result.success) {
        std::error_code ec;
        fs::remove_all(package, ec);
        message = result.message;
        return false;
    }
    message = L"壁纸已应用到 TuringDesk 桌面。";
    return true;
}

LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* holder = reinterpret_cast<std::shared_ptr<PreviewState>*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    std::shared_ptr<PreviewState> state = holder ? *holder : nullptr;

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        holder = reinterpret_cast<std::shared_ptr<PreviewState>*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(holder));
        state = holder ? *holder : nullptr;
        if (state) state->hwnd = hwnd;
    }

    switch (message) {
    case WM_CREATE:
        if (state) {
            state->applyButton = CreateWindowExW(0, L"BUTTON", L"应用", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                                 0, 0, 92, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyButtonId)), GetModuleHandleW(nullptr), nullptr);
            state->rejectButton = CreateWindowExW(0, L"BUTTON", L"拒绝", WS_CHILD | WS_VISIBLE,
                                                  0, 0, 92, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRejectButtonId)), GetModuleHandleW(nullptr), nullptr);
            BeginWebView(state);
        }
        return 0;

    case WM_SIZE:
        ResizePreviewWebView(state);
        return 0;

    case WM_COMMAND:
        if (state && LOWORD(wParam) == kApplyButtonId) {
            std::wstring messageText;
            const bool success = state->kind == "widget" ? ApplyWidget(state, messageText) : ApplyWallpaper(state, messageText);
            MessageBoxW(hwnd, messageText.c_str(), success ? L"TuringDesk" : L"TuringDesk · 应用失败", success ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
            if (success) {
                state->committed = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        if (state && LOWORD(wParam) == kRejectButtonId) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (state) {
            state->closed = true;
            CleanupPreview(state);
        }
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete holder;
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool EnsurePreviewWindowClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PreviewWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kPreviewWindowClass;
    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    registered = true;
    return true;
}

bool ShowPreviewWindow(HWND owner, const fs::path& dir) {
    std::shared_ptr<PreviewState> state;
    std::wstring error;
    if (!LoadPreviewState(dir, state, error)) {
        MessageBoxW(owner, error.c_str(), L"TuringDesk · 沙盒预览失败", MB_OK | MB_ICONERROR);
        std::error_code ec;
        fs::remove_all(dir, ec);
        return false;
    }
    if (!EnsurePreviewWindowClass()) return false;

    auto* holder = new std::shared_ptr<PreviewState>(state);
    const std::wstring title = L"TuringDesk 沙盒预览 · " + Utf8ToWide(state->title);
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kPreviewWindowClass,
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 560,
        owner, nullptr, GetModuleHandleW(nullptr), holder);
    if (!hwnd) {
        delete holder;
        std::error_code ec;
        fs::remove_all(dir, ec);
        return false;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    return true;
}

} // namespace

bool IsGeneratedPreviewTool(std::string_view toolName) noexcept {
    return toolName == "desktop_preview_widget" ||
           toolName == "desktop_preview_wallpaper" ||
           toolName == "desktop_preview_examples";
}

NativeToolResult ExecuteGeneratedPreviewTool(std::string_view toolName, std::string_view argumentsJson) {
    if (toolName == "desktop_preview_widget") return CreateWidgetPreview(argumentsJson);
    if (toolName == "desktop_preview_wallpaper") return CreateWallpaperPreview(argumentsJson);
    if (toolName == "desktop_preview_examples") return ExamplesCatalog();
    return {false, L"未知桌面沙盒预览工具。"};
}

bool HandleGeneratedPreviewCopyData(HWND owner, const COPYDATASTRUCT* data) {
    if (!data || data->dwData != kCopyDataTag || !data->lpData || data->cbData < sizeof(wchar_t) || data->cbData > 32768 * sizeof(wchar_t)) return false;
    const auto* text = static_cast<const wchar_t*>(data->lpData);
    const std::size_t count = data->cbData / sizeof(wchar_t);
    if (text[count - 1] != L'\0') return false;
    const fs::path dir{std::wstring(text)};
    return ShowPreviewWindow(owner, dir);
}

WidgetPreviewPromptResult ShowWidgetPreviewForPrompt(HWND owner, std::wstring_view prompt) {
    WidgetPreviewPromptResult result;
    const auto composed = widget_intent::ComposeFromPrompt(prompt);
    if (!composed.success) {
        result.message = composed.message.empty() ? L"无法从这句话生成小组件。" : composed.message;
        return result;
    }

    const auto root = TempPreviewRoot();
    if (root.empty()) {
        result.message = L"无法创建 AI 生成内容临时目录。";
        return result;
    }

    const auto dir = root / NewPreviewId();
    std::wstring error;
    if (!WriteWidgetPreviewSandbox(dir, composed.title, composed.a2uiJson, error)) {
        result.message = error.empty() ? L"小组件预览创建失败。" : error;
        return result;
    }

    if (!ShowPreviewWindow(owner, dir)) {
        result.message = L"小组件沙盒已生成，但预览窗口未能打开。";
        return result;
    }

    result.success = true;
    result.previewId = dir.filename().wstring();
    result.message = L"已根据你的描述打开小组件预览。满意后请点击「应用」；不满意可点「拒绝」。";
    return result;
}

bool OpenPreviewById(HWND owner, std::wstring_view previewId) {
    if (previewId.empty()) return false;
    const auto root = TempPreviewRoot();
    if (root.empty()) return false;
    std::error_code ec;
    const fs::path dir = root / std::wstring(previewId);
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return false;
    return ShowPreviewWindow(owner, dir);
}

} // namespace turingdesk::preview
