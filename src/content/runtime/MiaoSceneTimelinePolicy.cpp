#include "miaodesk/MiaoSceneTimelinePolicy.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace miaodesk::content {

double ApplyAnimationEasing(AnimationEasing easing, double normalized) noexcept {
    double t = normalized;
    if (!std::isfinite(t)) return 0.0;
    t = std::clamp(t, 0.0, 1.0);
    switch (easing) {
    case AnimationEasing::Linear:
        return t;
    case AnimationEasing::EaseIn:
        return t * t;
    case AnimationEasing::EaseOut: {
        const double inverse = 1.0 - t;
        return 1.0 - inverse * inverse;
    }
    case AnimationEasing::EaseInOut:
        if (t < 0.5) return 2.0 * t * t;
        return 1.0 - ((-2.0 * t + 2.0) * (-2.0 * t + 2.0)) / 2.0;
    }
    return t;
}

// ---------------------------------------------------------------------------

void SceneClock::Start(double sceneTimeSeconds) noexcept {
    sceneTime_ = sceneTimeSeconds;
    skippedWall_ = 0.0;
    running_ = true;
}

void SceneClock::Pause() noexcept {
    // 没有"已在暂停就直接返回"这道守卫,因为 running_ = false 本身就是幂等的:
    // 宿主每个隐藏事件都可能调一次,重复调用不得把同一段墙钟记两遍。加一道守卫
    // 只会得到一段测不动的死代码 —— 断言只能看见"重复调用后时钟仍是对的",
    // 而那件事由这行赋值本身保证。
    running_ = false;
}

void SceneClock::Resume() noexcept {
    if (running_) return;
    running_ = true;
}

double SceneClock::Advance(double wallDeltaSeconds) noexcept {
    if (!std::isfinite(wallDeltaSeconds)) return sceneTime_;
    if (wallDeltaSeconds <= 0.0) return sceneTime_;
    if (!running_) {
        skippedWall_ += wallDeltaSeconds;
        return sceneTime_;
    }
    sceneTime_ += wallDeltaSeconds;
    return sceneTime_;
}

void SceneClock::Seek(double sceneTimeSeconds) noexcept {
    if (!running_) return;  // 暂停期禁止改时间,否则暂停可以被绕过
    if (!std::isfinite(sceneTimeSeconds)) return;
    sceneTime_ = sceneTimeSeconds;
}

void SceneClock::Reset() noexcept {
    sceneTime_ = 0.0;
    skippedWall_ = 0.0;
    running_ = false;
}

// ---------------------------------------------------------------------------

ParameterSlewSample SampleParameterSlew(const ParameterSlewRequest& request, double elapsedSeconds) noexcept {
    ParameterSlewSample sample;
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) elapsedSeconds = 0.0;
    if (!std::isfinite(request.durationSeconds) || request.durationSeconds <= 0.0) {
        // 明确要求不要过渡:停在终值。这里**不**返回 from —— 宿主已经写进去了,
        // 再写一次 from 会把刚设的值改回去。
        sample.value = request.to;
        sample.settled = true;
        return sample;
    }
    if (elapsedSeconds >= request.durationSeconds) {
        sample.value = request.to;
        sample.settled = true;
        return sample;
    }
    const double normalized = elapsedSeconds / request.durationSeconds;
    const double eased = ApplyAnimationEasing(request.easing, normalized);
    sample.value = request.from + (request.to - request.from) * eased;
    sample.settled = false;
    if (!std::isfinite(sample.value)) sample.value = request.to;
    return sample;
}

double ParameterSlewSet::Begin(const std::wstring& parameterId, const ParameterSlewRequest& request) noexcept {
    Entry entry;
    entry.request = request;
    entry.elapsed = 0.0;
    entry.settled = false;
    const double value = SampleParameterSlew(request, 0.0).value;
    entries_[parameterId] = std::move(entry);
    return value;
}

bool ParameterSlewSet::Advance(
    double sceneDeltaSeconds,
    std::vector<std::pair<std::wstring, double>>* out) {
    if (!out) return false;
    out->clear();
    if (!std::isfinite(sceneDeltaSeconds) || sceneDeltaSeconds < 0.0) sceneDeltaSeconds = 0.0;
    bool active = false;
    for (auto& [id, entry] : entries_) {
        if (entry.settled) continue;
        entry.elapsed += sceneDeltaSeconds;
        const auto sample = SampleParameterSlew(entry.request, entry.elapsed);
        if (sample.settled) {
            // 终值也报出去:宿主要把这一帧的最终值写进场景,
            // 而"从此不再报"会让停在旧值上的那一帧留在屏幕上。
            out->emplace_back(id, sample.value);
            entry.settled = true;
            continue;
        }
        active = true;
        out->emplace_back(id, sample.value);
    }
    return active;
}

std::size_t ParameterSlewSet::ActiveCount() const noexcept {
    std::size_t count = 0;
    for (const auto& [id, entry] : entries_)
        if (!entry.settled) ++count;
    return count;
}

bool ParameterSlewSet::Active(const std::wstring& parameterId) const noexcept {
    const auto it = entries_.find(parameterId);
    return it != entries_.end() && !it->second.settled;
}

double ParameterSlewSet::ValueOf(const std::wstring& parameterId) const noexcept {
    const auto it = entries_.find(parameterId);
    return it == entries_.end() ? 0.0 : SampleParameterSlew(it->second.request, it->second.elapsed).value;
}

void ParameterSlewSet::Reset() noexcept { entries_.clear(); }

// ---------------------------------------------------------------------------

double AnimationValueDistance(const PropertyValue& left, const PropertyValue& right) noexcept {
    if (left.index() != right.index()) return 0.0;
    auto delta = [](double a, double b) { return std::fabs(a - b); };
    if (const auto* a = std::get_if<double>(&left)) {
        const auto b = std::get<double>(right);
        return delta(*a, b);
    }
    if (const auto* a = std::get_if<std::int64_t>(&left)) {
        const auto b = std::get<std::int64_t>(right);
        return static_cast<double>(b > *a ? b - *a : *a - b);
    }
    if (const auto* a = std::get_if<Vec2>(&left)) {
        const auto& b = std::get<Vec2>(right);
        return std::max(delta(a->x, b.x), delta(a->y, b.y));
    }
    if (const auto* a = std::get_if<Vec3>(&left)) {
        const auto& b = std::get<Vec3>(right);
        return std::max({delta(a->x, b.x), delta(a->y, b.y), delta(a->z, b.z)});
    }
    if (const auto* a = std::get_if<Vec4>(&left)) {
        const auto& b = std::get<Vec4>(right);
        return std::max({delta(a->x, b.x), delta(a->y, b.y), delta(a->z, b.z), delta(a->w, b.w)});
    }
    if (const auto* a = std::get_if<Color4>(&left)) {
        const auto& b = std::get<Color4>(right);
        return std::max({delta(a->r, b.r), delta(a->g, b.g), delta(a->b, b.b), delta(a->a, b.a)});
    }
    return 0.0;
}

// 一条轨道自身值行程的大小。
//
// 用的"距离"是分量差取最大(切比雪夫式,见 AnimationValueDistance)。对这种度量,
// 全体关键帧两两之间的最大距离**等于**每个分量的极差再取最大:
// 第 k 个分量上能取到的最大 |a_k - b_k| 就是 (max_k, min_k),由取到这两个值的关键帧
// 达成;而"分量差取最大"不会超过任何一个分量的极差,所以两边相等。
// 于是这里是 O(n·d) 而不是 O(n²) —— Validate 允许一条轨道 4096 个关键帧,
// O(n²) 是 840 万次 variant 比较,而这道审查在加载期就跑了。
//
// MiaoSceneTimelinePolicyTest 里有一条拿乱数和暴力法对照的断言,钉住这个等式。
double AnimationTrackValueRange(const AnimationTrackDefinition& track) noexcept {
    if (track.keyframes.empty()) return 0.0;
    // 每个分量的极差。只对五个可插值的数值族有意义,别的类型(Validate 已经拒了)
    // 不进这张表,于是它们贡献 0。
    struct Span {
        double low{};
        double high{};
        bool seen{};
    };
    std::vector<Span> spans;
    auto add = [&spans](std::size_t component, double value) {
        if (spans.size() <= component) spans.resize(component + 1);
        auto& span = spans[component];
        if (!span.seen) {
            span.low = value;
            span.high = value;
            span.seen = true;
            return;
        }
        span.low = std::min(span.low, value);
        span.high = std::max(span.high, value);
    };

    for (const auto& keyframe : track.keyframes) {
        const auto& value = keyframe.value;
        if (const auto* v = std::get_if<double>(&value)) {
            add(0, *v);
        } else if (const auto* v = std::get_if<std::int64_t>(&value)) {
            add(0, static_cast<double>(*v));
        } else if (const auto* v = std::get_if<Vec2>(&value)) {
            add(0, v->x);
            add(1, v->y);
        } else if (const auto* v = std::get_if<Vec3>(&value)) {
            add(0, v->x);
            add(1, v->y);
            add(2, v->z);
        } else if (const auto* v = std::get_if<Vec4>(&value)) {
            add(0, v->x);
            add(1, v->y);
            add(2, v->z);
            add(3, v->w);
        } else if (const auto* v = std::get_if<Color4>(&value)) {
            add(0, v->r);
            add(1, v->g);
            add(2, v->b);
            add(3, v->a);
        }
    }
    double range = 0.0;
    for (const auto& span : spans)
        if (span.seen) range = std::max(range, span.high - span.low);
    return range;
}

double AnimationLoopSeamFraction(const AnimationTrackDefinition& track) noexcept {
    if (track.loopMode != AnimationLoopMode::Loop) return 0.0;
    if (track.keyframes.size() < 2) return 0.0;
    const double range = AnimationTrackValueRange(track);
    if (range <= 0.0) return 0.0;
    const auto& first = track.keyframes.front();
    const auto& last = track.keyframes.back();
    return AnimationValueDistance(first.value, last.value) / range;
}

double AnimationFrameStepFraction(const AnimationTrackDefinition& track, double fps) noexcept {
    if (track.keyframes.size() < 2) return 0.0;
    if (!std::isfinite(fps) || fps <= 0.0) fps = 1.0;
    const double frameTime = 1.0 / fps;
    const double range = AnimationTrackValueRange(track);
    if (range <= 0.0) return 0.0;

    double worst = 0.0;
    const auto consider = [&](double distance, double seconds) {
        if (distance <= 0.0 || seconds <= 0.0) return;
        // 一段短于一帧时,插值一次都采不到样,整段位移会在同一帧里发生。
        const double covered = distance / range;
        const double frames = seconds / frameTime;
        worst = std::max(worst, covered / std::max(frames, 1.0));
    };

    for (std::size_t i = 0; i + 1 < track.keyframes.size(); ++i) {
        const auto& from = track.keyframes[i];
        const auto& to = track.keyframes[i + 1];
        consider(AnimationValueDistance(from.value, to.value), to.timeSeconds - from.timeSeconds);
    }
    // Loop 的接缝是从最后一个关键帧跳回第一个关键帧。那一步发生在一帧之内
    // (采样点落在 duration 上,下一个采样点已经 wrap 回 0),所以它自己的
    // frame-step 就是整个接缝位移量 —— 不是除以圈数。大多数硬跳都在这里。
    if (track.loopMode == AnimationLoopMode::Loop) {
        worst = std::max(worst, AnimationLoopSeamFraction(track));
    }
    return worst;
}

std::uint32_t AuditAnimationContinuity(
    const AnimationTrackDefinition& track,
    const std::vector<PropertyBindingDefinition>* bindings,
    double fps) noexcept {
    std::uint32_t issues = kAnimationContinuous;

    if (AnimationLoopSeamFraction(track) > kAnimationLoopSeamThreshold)
        issues |= kAnimationLoopSeamMismatch;

    if (AnimationFrameStepFraction(track, fps) > kAnimationFrameJumpThreshold)
        issues |= kAnimationFrameJump;

    if (track.loopMode == AnimationLoopMode::Loop && track.keyframes.size() >= 2) {
        const double last = track.keyframes.back().timeSeconds;
        const double first = track.keyframes.front().timeSeconds;
        if (first > kAnimationLoopEdgeToleranceSeconds ||
            std::fabs(last - track.durationSeconds) > kAnimationLoopEdgeToleranceSeconds) {
            issues |= kAnimationLoopHoldAtEdge;
        }
    }

    if (bindings) {
        for (const auto& binding : *bindings) {
            if (binding.target.componentId == track.target.componentId &&
                binding.target.propertyName == track.target.propertyName) {
                issues |= kAnimationPropertyContested;
                break;
            }
        }
    }
    return issues;
}

std::vector<std::wstring> ContestedAnimationTargets(
    const std::vector<AnimationTrackDefinition>& animations,
    const std::vector<PropertyBindingDefinition>& bindings) noexcept {
    std::vector<std::wstring> contested;
    for (std::size_t i = 0; i < animations.size(); ++i) {
        const auto& track = animations[i];
        bool laterWriter = false;
        for (std::size_t j = i + 1; j < animations.size(); ++j) {
            if (animations[j].target.componentId == track.target.componentId &&
                animations[j].target.propertyName == track.target.propertyName) {
                laterWriter = true;
                break;
            }
        }
        if (!laterWriter) continue;
        contested.push_back(track.id);
    }
    // 动画总在 binding 之后跑,所以 binding 一侧的点名方式不同:它不是"被后面的盖住",
    // 而是"每帧都被盖住"。两种都报出去,由调用方按信息组织语言。
    for (const auto& binding : bindings) {
        for (const auto& track : animations) {
            if (track.target.componentId == binding.target.componentId &&
                track.target.propertyName == binding.target.propertyName) {
                contested.push_back(binding.id);
                break;
            }
        }
    }
    return contested;
}

std::wstring DescribeAnimationContinuity(std::uint32_t issues) noexcept {
    if (issues == kAnimationContinuous) return L"连续";
    std::wstring text;
    auto add = [&text](const wchar_t* part) {
        if (!text.empty()) text += L";";
        text += part;
    };
    if (issues & kAnimationLoopSeamMismatch) add(L"循环接缝不连续(每圈回到起点时硬跳)");
    if (issues & kAnimationFrameJump) add(L"一帧内跨过大半行程(看起来是瞬移)");
    if (issues & kAnimationLoopHoldAtEdge) add(L"循环端点不在 0/duration(整圈里有段时间停在端点)");
    if (issues & kAnimationPropertyContested) add(L"目标属性还有别的写者(其中一个是死的)");
    return text;
}

} // namespace miaodesk::content
