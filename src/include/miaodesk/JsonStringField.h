#pragma once

// 从一段 JSON 里取出一个字符串字段的值(UTF-8,转义已解)。
//
// 为什么把它从 PiRuntime.cpp 里提出来:创作工具的 worker 也要解析模型交上来的
// 参数,而那份参数和 RPC 事件是同一种 JSON。两处各写一份提取器的后果不是重复代码,
// 是**两处可以不一致** —— 而这里不一致的地方恰好是 creator_package_update 的
// content:一个把 \n 解错、一个不解,写进包里的就是另一份内容,而两边都以为自己对。
//
// 它是刻意"只取一个字符串字段"的,不是一个 JSON 解析器:
//   * 调用方都只需要扁平对象里的几个字符串键;
//   * 一个完整的解析器要处理嵌套、数字、数组、重复键,而这里没有这些需求,
//     多出来的复杂度只会让"它到底接受什么"变得更难说清。
//
// 取不到的键返回空串。**空串同时是"键不存在"和"值是空字符串"的返回值** ——
// 这个歧义是已知的:调用方要区分这两者时必须自己判断键在不在(JsonHasKey)。
#include <cstddef>
#include <string>
#include <string_view>

namespace miaodesk {

// 这个键在这段 JSON 里作为一个顶层字符串字段出现了吗。
bool JsonHasStringKey(std::string_view json, std::string_view key) noexcept;

// 取出 key 的值。找不到、或者不是字符串,返回空串。
std::string ExtractJsonString(std::string_view json, std::string_view key);

} // namespace miaodesk
