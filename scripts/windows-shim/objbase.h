#pragma once
#include <cstdint>
#include <random>
struct GUID { std::uint32_t Data1; std::uint16_t Data2, Data3; std::uint8_t Data4[8]; };
inline long CoCreateGuid(GUID* g) {
    static std::mt19937_64 rng(12345);
    const std::uint64_t a = rng(), b = rng();
    g->Data1 = static_cast<std::uint32_t>(a >> 32);
    g->Data2 = static_cast<std::uint16_t>(a >> 16);
    g->Data3 = static_cast<std::uint16_t>(a);
    for (int i = 0; i < 8; ++i) g->Data4[i] = static_cast<std::uint8_t>(b >> (i * 8));
    return 0;
}
