#include "turingdesk/DirectToolRuntime.h"

namespace turingdesk {

DirectToolRuntime::~DirectToolRuntime() {
    ResetSession();
}

bool DirectToolRuntime::SelfTest() {
    return true;
}

bool DirectToolRuntime::CanHandle(const L3Agent&) const {
    // DirectToolRuntime is intentionally retired from the default product route.
    // Normal L3 routing is Codex CLI first, then lightweight Direct Model Q&A.
    return false;
}

std::wstring DirectToolRuntime::StatusText(const L3Agent&) const {
    return L"已停用；默认路由为 Codex CLI → 轻量 Direct Model 问答";
}

void DirectToolRuntime::AskAsync(const L3Agent&, std::wstring, DeltaCallback, DoneCallback onDone) {
    if (onDone) onDone(L"Direct Tool Runtime 已退出默认路由。请使用 Codex CLI；不可用时将回退到轻量问答模式。");
}

void DirectToolRuntime::Stop() {
    if (worker_.joinable()) worker_.request_stop();
    if (HINTERNET request = activeRequest_.exchange(nullptr)) WinHttpCloseHandle(request);
}

void DirectToolRuntime::ResetSession() {
    Stop();
    if (worker_.joinable()) worker_.join();
    busy_.store(false);
    std::scoped_lock lock(conversationMutex_);
    conversation_.clear();
}

void DirectToolRuntime::RunRequest(const L3Agent&,
                                   std::wstring,
                                   DeltaCallback,
                                   DoneCallback onDone,
                                   std::stop_token) {
    if (onDone) onDone(L"Direct Tool Runtime 已停用。");
}

} // namespace turingdesk
