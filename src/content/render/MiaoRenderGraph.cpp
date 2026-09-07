#include "miaodesk/MiaoRenderGraph.h"

#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool HasPrefix(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

} // namespace

const RenderResourceDefinition* MiaoRenderGraph::FindResource(
    const RenderGraphDefinition& graph, std::wstring_view id) noexcept {
    for (const auto& resource : graph.resources) {
        if (resource.id == id) return &resource;
    }
    return nullptr;
}

bool MiaoRenderGraph::Validate(const RenderGraphDefinition& graph, std::wstring* error) {
    std::unordered_set<std::wstring> resourceIds;
    for (const auto& resource : graph.resources) {
        if (!HasPrefix(resource.id, L"renderres://"))
            return Fail(error, L"Render resource id must use renderres:// stable ids: " + resource.id);
        if (!resourceIds.emplace(resource.id).second)
            return Fail(error, L"Duplicate render resource id: " + resource.id);
    }

    std::unordered_set<std::wstring> passIds;
    std::unordered_map<std::wstring, std::size_t> writers;
    for (const auto& pass : graph.passes) {
        if (!HasPrefix(pass.id, L"renderpass://"))
            return Fail(error, L"Render pass id must use renderpass:// stable ids: " + pass.id);
        if (!passIds.emplace(pass.id).second)
            return Fail(error, L"Duplicate render pass id: " + pass.id);
        if (!pass.enabled) continue;
        for (const auto& id : pass.reads) {
            if (!FindResource(graph, id))
                return Fail(error, L"Render pass reads unknown resource: " + pass.id + L" -> " + id);
        }
        for (const auto& id : pass.writes) {
            if (!FindResource(graph, id))
                return Fail(error, L"Render pass writes unknown resource: " + pass.id + L" -> " + id);
            // external means the host owns the resource lifetime (for example a
            // swap-chain backbuffer). It may still be a graph output.
            if (!writers.emplace(id, 1).second)
                return Fail(error, L"Render resource has multiple writers in v1: " + id);
        }
    }

    if (error) error->clear();
    return true;
}

bool MiaoRenderGraph::Compile(
    const RenderGraphDefinition& graph,
    CompiledRenderGraph* compiled,
    std::wstring* error) {
    if (!compiled) return Fail(error, L"Compiled render graph output is null.");
    compiled->passOrder.clear();
    if (!Validate(graph, error)) return false;

    std::unordered_map<std::wstring, std::size_t> producer;
    for (std::size_t i = 0; i < graph.passes.size(); ++i) {
        if (!graph.passes[i].enabled) continue;
        for (const auto& resource : graph.passes[i].writes) producer.emplace(resource, i);
    }

    std::vector<std::vector<std::size_t>> edges(graph.passes.size());
    std::vector<std::size_t> indegree(graph.passes.size());
    std::vector<bool> enabled(graph.passes.size());
    for (std::size_t i = 0; i < graph.passes.size(); ++i) enabled[i] = graph.passes[i].enabled;

    std::unordered_set<unsigned long long> seenEdges;
    for (std::size_t consumer = 0; consumer < graph.passes.size(); ++consumer) {
        if (!enabled[consumer]) continue;
        for (const auto& read : graph.passes[consumer].reads) {
            const auto it = producer.find(read);
            if (it == producer.end()) continue;
            const std::size_t writer = it->second;
            if (writer == consumer)
                return Fail(error, L"Render pass cannot read its own output in v1: " + graph.passes[consumer].id);
            const auto key = (static_cast<unsigned long long>(writer) << 32u) |
                             static_cast<unsigned long long>(consumer);
            if (!seenEdges.emplace(key).second) continue;
            edges[writer].push_back(consumer);
            ++indegree[consumer];
        }
    }

    std::deque<std::size_t> ready;
    for (std::size_t i = 0; i < graph.passes.size(); ++i) {
        if (enabled[i] && indegree[i] == 0) ready.push_back(i);
    }

    while (!ready.empty()) {
        const auto current = ready.front();
        ready.pop_front();
        compiled->passOrder.push_back(current);
        for (const auto next : edges[current]) {
            if (--indegree[next] == 0) ready.push_back(next);
        }
    }

    std::size_t enabledCount = 0;
    for (bool value : enabled) if (value) ++enabledCount;
    if (compiled->passOrder.size() != enabledCount) {
        compiled->passOrder.clear();
        return Fail(error, L"Render graph contains a dependency cycle.");
    }

    if (error) error->clear();
    return true;
}

bool MiaoRenderGraph::SelfTest() {
    RenderGraphDefinition graph;
    graph.resources = {
        {L"renderres://scene", false, false},
        {L"renderres://post", false, false},
        {L"renderres://backbuffer", true, false},
    };
    graph.passes = {
        {L"renderpass://scene", RenderPassKind::Scene2D, {}, {L"renderres://scene"}, true},
        {L"renderpass://post", RenderPassKind::PostProcess, {L"renderres://scene"}, {L"renderres://post"}, true},
        {L"renderpass://present", RenderPassKind::Present, {L"renderres://post"}, {L"renderres://backbuffer"}, true},
    };

    CompiledRenderGraph compiled;
    std::wstring error;
    if (!Compile(graph, &compiled, &error)) return false;
    if (compiled.passOrder.size() != 3) return false;
    if (compiled.passOrder[0] != 0 || compiled.passOrder[1] != 1 || compiled.passOrder[2] != 2) return false;

    auto invalid = graph;
    invalid.passes[0].reads.push_back(L"renderres://post");
    if (Compile(invalid, &compiled, &error)) return false;
    return true;
}

} // namespace miaodesk::content
