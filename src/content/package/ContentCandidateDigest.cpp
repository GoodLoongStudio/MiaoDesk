#include "miaodesk/ContentCandidateDigest.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace miaodesk::content {
namespace {

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4). Self-contained on purpose: the product must not depend on
// a hash whose output can differ between the MSVC build and the offline test build,
// because the digest is recorded and compared across processes.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline std::uint32_t RotateRight(std::uint32_t value, unsigned bits) noexcept {
    return (value >> bits) | (value << (32u - bits));
}

void Sha256Transform(std::array<std::uint32_t, 8>& state, const unsigned char* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[static_cast<std::size_t>(i)] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                                        (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                                        (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                                        (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = RotateRight(w[static_cast<std::size_t>(i) - 15], 7) ^
                                 RotateRight(w[static_cast<std::size_t>(i) - 15], 18) ^
                                 (w[static_cast<std::size_t>(i) - 15] >> 3);
        const std::uint32_t s1 = RotateRight(w[static_cast<std::size_t>(i) - 2], 17) ^
                                 RotateRight(w[static_cast<std::size_t>(i) - 2], 19) ^
                                 (w[static_cast<std::size_t>(i) - 2] >> 10);
        w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i) - 16] + s0 +
                                         w[static_cast<std::size_t>(i) - 7] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kSha256K[static_cast<std::size_t>(i)] +
                                    w[static_cast<std::size_t>(i)];
        const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

std::string ToHex(const unsigned char* bytes, std::size_t count) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 0x0fu]);
    }
    return out;
}

int RoleRank(CandidatePartRole role) noexcept {
    switch (role) {
    case CandidatePartRole::Manifest: return 0;
    case CandidatePartRole::Scene: return 1;
    case CandidatePartRole::Parameters: return 2;
    case CandidatePartRole::Preview: return 3;
    case CandidatePartRole::Asset: return 4;
    case CandidatePartRole::Other: return 5;
    }
    return 6;
}

void AppendLengthPrefixed(std::string& out, const std::string& value) {
    // Length-prefixed so two different splits of the concatenation cannot collide:
    // ("ab","c") and ("a","bc") must produce different digests.
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%020zu", value.size());
    out.append(buffer, sizeof(buffer) - 1);
    out.push_back(':');
    out.append(value);
}

} // namespace

const char* CandidateRoleName(CandidatePartRole role) noexcept {
    switch (role) {
    case CandidatePartRole::Manifest: return "manifest";
    case CandidatePartRole::Scene: return "scene";
    case CandidatePartRole::Parameters: return "parameters";
    case CandidatePartRole::Preview: return "preview";
    case CandidatePartRole::Asset: return "asset";
    case CandidatePartRole::Other: return "other";
    }
    return "other";
}

bool ParseCandidateRole(std::string_view text, CandidatePartRole* role) noexcept {
    if (!role) return false;
    if (text == "manifest") *role = CandidatePartRole::Manifest;
    else if (text == "scene") *role = CandidatePartRole::Scene;
    else if (text == "parameters") *role = CandidatePartRole::Parameters;
    else if (text == "preview") *role = CandidatePartRole::Preview;
    else if (text == "asset") *role = CandidatePartRole::Asset;
    else if (text == "other") *role = CandidatePartRole::Other;
    else return false;
    return true;
}

std::string Sha256Hex(const std::string& bytes) {
    std::array<std::uint32_t, 8> state = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };

    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t bitLength = bytes.size() * 8u;
    std::size_t offset = 0;
    for (; offset + 64 <= bytes.size(); offset += 64) {
        Sha256Transform(state, data + offset);
    }

    // Final block(s): 0x80, zero pad to 56 mod 64, then the 64-bit big-endian length.
    unsigned char tail[128]{};
    const std::size_t remaining = bytes.size() - offset;
    std::memcpy(tail, data + offset, remaining);
    tail[remaining] = 0x80u;
    const std::size_t tailLength = remaining + 1 <= 56 ? 64 : 128;
    for (int i = 0; i < 8; ++i) {
        tail[tailLength - 1 - static_cast<std::size_t>(i)] =
            static_cast<unsigned char>((bitLength >> (8u * static_cast<unsigned>(i))) & 0xffu);
    }
    Sha256Transform(state, tail);
    if (tailLength == 128) Sha256Transform(state, tail + 64);

    unsigned char out[32]{};
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<unsigned char>((state[static_cast<std::size_t>(i)] >> 24) & 0xffu);
        out[i * 4 + 1] = static_cast<unsigned char>((state[static_cast<std::size_t>(i)] >> 16) & 0xffu);
        out[i * 4 + 2] = static_cast<unsigned char>((state[static_cast<std::size_t>(i)] >> 8) & 0xffu);
        out[i * 4 + 3] = static_cast<unsigned char>((state[static_cast<std::size_t>(i)]) & 0xffu);
    }
    return ToHex(out, sizeof(out));
}

CandidateDigest ComputeCandidateDigest(std::vector<CandidatePart> parts) {
    CandidateDigest digest;

    // Directory walks hand back files in an arbitrary order. Sorting by (role, path)
    // is what makes the same snapshot produce the same digest every time.
    std::stable_sort(parts.begin(), parts.end(), [](const CandidatePart& a, const CandidatePart& b) {
        const int ra = RoleRank(a.role);
        const int rb = RoleRank(b.role);
        if (ra != rb) return ra < rb;
        return a.relPath < b.relPath;
    });

    std::string canonical;
    canonical.reserve(1024);
    std::vector<std::string> seen;
    for (const auto& part : parts) {
        if (part.relPath.empty()) {
            digest.complete = false;
            digest.incompletenessReason = "有一个分块没有包内相对路径";
            return digest;
        }
        // The same file listed twice would silently double-count. Reject rather than
        // dedupe, because a duplicate usually means the caller walked the snapshot
        // twice and the coverage claim is no longer what it says it is.
        const std::string key = std::string(CandidateRoleName(part.role)) + ":" + part.relPath;
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) {
            digest.complete = false;
            digest.incompletenessReason = "同一个文件被列了两次: " + key;
            return digest;
        }
        seen.push_back(key);
        digest.covered.push_back(key);
        digest.bytesCovered += part.bytes.size();

        AppendLengthPrefixed(canonical, CandidateRoleName(part.role));
        AppendLengthPrefixed(canonical, part.relPath);
        AppendLengthPrefixed(canonical, part.bytes);
    }

    if (parts.empty()) {
        digest.complete = false;
        digest.incompletenessReason = "候选快照里没有任何内容文件";
        return digest;
    }
    // Every candidate must at least carry a manifest and a scene entry. A digest over
    // an empty or scene-less package would still look like a valid identity.
    bool hasManifest = false;
    bool hasScene = false;
    for (const auto& part : parts) {
        if (part.role == CandidatePartRole::Manifest) hasManifest = true;
        if (part.role == CandidatePartRole::Scene) hasScene = true;
    }
    if (!hasManifest || !hasScene) {
        digest.complete = false;
        digest.incompletenessReason = !hasManifest ? "快照里没有 manifest.json"
                                                   : "快照里没有 scene 入口文件";
        return digest;
    }

    digest.value = Sha256Hex(canonical);
    digest.complete = true;
    digest.incompletenessReason.clear();
    return digest;
}

std::string ShortDigest(std::string_view digest, std::size_t length) {
    if (digest.size() < length) return std::string(digest);
    return std::string(digest.substr(0, length));
}

} // namespace miaodesk::content
