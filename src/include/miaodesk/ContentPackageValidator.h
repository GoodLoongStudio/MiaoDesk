#pragma once

// CCA-05:候选包的结构化校验(纯逻辑)。
//
// 计划原话:"宿主通过真正的包校验服务验证、复制封存、生成 digest/revision"。
// 这一层是"真正的包校验"的那一半 —— 另一半分给 render/runtime,不在这里。
//
// 为什么要做成纯逻辑、而不是直接调 MiaoContentPackage 的加载器:加载器要盘、
// 要 Windows,而校验结论必须能在任何机器上被断言。对"这个包能不能用"的每一条判断
// 都该有可执行测试,而不是只在 Windows 真机上跑一次然后靠记忆。
//
// 它刻意只做**结构**校验:字段在不在、引用的文件在不在、类型对不对、有没有
// 本轮不支持的运行时。更深的问题(某个 layer 的参数越界、动画曲线是否收敛)
// 属于 runtime 层,不在这里假装能判。
#include <string>
#include <vector>

#include "miaodesk/ContentCandidateDigest.h"
#include "miaodesk/ContentCandidateLedger.h"

namespace miaodesk::creator {

// 校验一个候选快照。parts 是宿主对工作区读出来的内容清单 ——
// 校验的是**这些字节**,不是盘上此刻的文件:否则校验通过之后源目录再被改一下,
// 这个结论就还挂在上面,而封存的意义正是让它不随源目录变化。
content::ContentValidationResult ValidateCandidatePackage(std::vector<content::CandidatePart> parts);

// 这份清单里按角色能找到的那个文件。找不到返回 nullptr。
const content::CandidatePart* FindPart(const std::vector<content::CandidatePart>& parts,
                                       content::CandidatePartRole role,
                                       const std::string& relPath = {});

} // namespace miaodesk::creator
