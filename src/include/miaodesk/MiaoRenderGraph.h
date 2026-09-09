#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::content {

enum class RenderPassKind {
    Clear,
    Scene2D,
    Programmable,
    Particle,
    PostProcess,
    Composite,
    Present,
};

enum class RenderResourceFormat {
    Bgra8Unorm,
};

enum class RenderResourceSizePolicy {
    SurfaceRelative,
};

struct RenderResourceDefinition {
    std::wstring id;
    bool external{};
    bool persistent{};
    RenderResourceFormat format{RenderResourceFormat::Bgra8Unorm};
    RenderResourceSizePolicy sizePolicy{RenderResourceSizePolicy::SurfaceRelative};
    float widthScale{1.0f};
    float heightScale{1.0f};
    bool renderTarget{true};
    bool shaderResource{};
};

struct RenderPassDefinition {
    std::wstring id;
    RenderPassKind kind{RenderPassKind::Scene2D};
    std::vector<std::wstring> reads;
    std::vector<std::wstring> writes;
    bool enabled{true};
};

struct RenderGraphDefinition {
    std::vector<RenderResourceDefinition> resources;
    std::vector<RenderPassDefinition> passes;
};

struct CompiledRenderGraph {
    std::vector<std::size_t> passOrder;
};

class MiaoRenderGraph {
public:
    static bool Validate(const RenderGraphDefinition& graph, std::wstring* error = nullptr);
    static bool Compile(const RenderGraphDefinition& graph, CompiledRenderGraph* compiled,
                        std::wstring* error = nullptr);
    static const RenderResourceDefinition* FindResource(
        const RenderGraphDefinition& graph, std::wstring_view id) noexcept;
    static bool SelfTest();
};

} // namespace miaodesk::content
