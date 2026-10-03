#include "miaodesk/MiaoLockOwnershipHost.h"

#include "miaodesk/AppPaths.h"
#include "miaodesk/MiaoLockOwnership.h"
#include "miaodesk/MiaoLockRecord.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace miaodesk::lock_host {
namespace {

// 锁名 → 文件名安全形。规则固定,路径才可复现。
std::wstring SafeFileName(std::wstring_view mutexName) {
    std::wstring safe(mutexName.empty() ? std::wstring_view(L"<none>") : mutexName);
    for (auto& ch : safe) {
        if (ch == L'\\' || ch == L'/' || ch == L':') ch = L'-';
    }
    return safe;
}

fs::path ResolveRecordPath(std::wstring_view mutexName) {
    const fs::path root = miaodesk::paths::StateRoot();
    // 拿不到状态根就返回空路径:调用方据此判"判不了",而不是当成"没有持有者"。
    return root.empty() ? fs::path{} : root / (SafeFileName(mutexName) + L".owner");
}

} // namespace

std::wstring OwnerRecordPathFor(std::wstring_view mutexName) {
    const fs::path path = ResolveRecordPath(mutexName);
    return path.empty() ? std::wstring{} : path.wstring();
}

bool PublishOwnership(std::wstring_view mutexName, std::uint32_t pid, std::uint64_t nowSeconds) {
    const fs::path path = ResolveRecordPath(mutexName);
    if (path.empty()) return false;

    lock_record::LockOwnership ownership;
    ownership.pid = pid;
    ownership.heartbeatSeconds = nowSeconds;

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;

    const std::string text = lock_record::Encode(ownership);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

void RetireOwnership(std::wstring_view mutexName) {
    const fs::path path = ResolveRecordPath(mutexName);
    if (path.empty()) return;
    std::error_code ec;
    fs::remove(path, ec);
}

bool ExistingOwnerIsHealthy(std::wstring_view mutexName, std::uint64_t nowSeconds,
                            std::uint64_t leaseSeconds) {
    const fs::path path = ResolveRecordPath(mutexName);
    // 拿不到状态根:判不了。**不**当成没有持有者 —— 那会让两个 owner 同时上。
    if (path.empty()) return false;

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        // 锁在,但没有任何身份记录。这**不是**"没有持有者":记录这一侧坏了,
        // 而锁是真实的。按判不了处理,并让调用方明确拒绝。
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    lock_record::LockOwnership ownership;
    if (!lock_record::Decode(text, &ownership)) return false;

    lock_ownership::LockObservation observation;
    observation.recordPresent = ownership.pid != 0;
    observation.record.pid = ownership.pid;
    observation.record.heartbeatSeconds = ownership.heartbeatSeconds;
    // 心跳记录自己说进程在不在不可靠,这里以"记录里有没有 pid"为准:
    // 没有 pid 的记录与没有记录是两件事,而前者必须落进 Unusable。
    observation.ownerProcessAlive = ownership.pid != 0;
    observation.nowSeconds = nowSeconds;
    observation.leaseSeconds = leaseSeconds;

    return lock_ownership::JudgeLockOwnership(observation) == lock_ownership::LockVerdict::Running;
}

} // namespace miaodesk::lock_host
