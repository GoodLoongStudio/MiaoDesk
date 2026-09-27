#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace miaodesk::content {

// What a file in a candidate snapshot is for. The role is part of the digest so that
// moving a file from one role to another changes the identity even if the bytes match.
enum class CandidatePartRole {
    Manifest,
    Scene,
    Parameters,
    Preview,
    Asset,      // 引用素材(贴图、音频、字体…)
    Other,      // 快照里其余文件
};

// One file that went into a candidate's identity.
struct CandidatePart {
    CandidatePartRole role{CandidatePartRole::Other};
    // Package-relative, forward-slash separated, lowercased. NOT the absolute path:
    // the same bytes at a different absolute location must keep the same identity,
    // otherwise the same candidate looks new every time it is revalidated.
    std::string relPath;
    std::string bytes;
};

// A candidate's content identity.
//
// The plan's requirement is explicit: 摘要覆盖 manifest、scene、参数及引用素材,
// 不能只 hash 路径或单个 JSON. Two consequences drive this shape:
//
//   * Hashing only manifest.json would miss every edit to a layer, a parameter
//     default, or a referenced texture -- and a candidate that changed but keeps
//     its digest would let an old validation stand in for a new one.
//   * Hashing the absolute path would make the same bytes a different candidate
//     depending on where the sandbox happened to put them, so revalidation would
//     never hit.
//
// complete is the part that keeps this honest: when a declared part could not be
// read, the digest is marked incomplete and must not be used as an identity, no
// matter how good it looks.
struct CandidateDigest {
    std::string value;                  // lowercase hex, 64 chars
    std::vector<std::string> covered;   // "role:relPath" for every part, in digest order
    std::size_t bytesCovered{0};
    bool complete{false};
    std::string incompletenessReason;

    bool operator==(const CandidateDigest&) const noexcept = default;
    bool UsableAsIdentity() const noexcept { return complete && value.size() == 64; }
};

// Stable hex SHA-256. Exposed because a digest is only trustworthy if the primitive
// behind it has been checked against published test vectors, not against itself.
std::string Sha256Hex(const std::string& bytes);

// Computes a candidate's digest from its snapshot parts. The input order does not
// matter: parts are sorted by (role rank, relPath) first, because walking a directory
// yields an arbitrary order and the same snapshot must always produce the same digest.
CandidateDigest ComputeCandidateDigest(std::vector<CandidatePart> parts);

// Short, human-readable form for logs and UI. Never used for identity decisions.
std::string ShortDigest(std::string_view digest, std::size_t length = 12);

// Role/常见文本, for diagnostics and tests.
const char* CandidateRoleName(CandidatePartRole role) noexcept;
bool ParseCandidateRole(std::string_view text, CandidatePartRole* role) noexcept;

} // namespace miaodesk::content
