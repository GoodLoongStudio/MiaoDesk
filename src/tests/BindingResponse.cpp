// BindingResponse math properties. Every member must be a pure, finite function on
// [0, 1] that pins the endpoints, or a binding could stall the scene runtime. The
// Elastic curve is the one with real failure history: an earlier formulation used
// 1 - e^(-kt)(1 + cos(wt))/2, which never exceeds 1 because (1 + cos) is never
// negative, so it was a critically damped approach wearing an elastic's name.
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using miaodesk::content::BindingResponse;
using miaodesk::content::BindingResponseKey;
using miaodesk::content::ParseBindingResponse;
using miaodesk::content::kBindingResponseCount;

int failures = 0;
void Check(bool c, const std::string& w) {
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", w.c_str());
    if (!c) ++failures;
}

// A mirrored copy of the curve in MiaoSceneRuntime.cpp::ApplyBindingResponse, used
// here to probe the math over the whole domain. The end-to-end test in
// MiaoSceneRuntimeTest drives the real implementation.
double Response(BindingResponse r, double t) {
    switch (r) {
    case BindingResponse::Linear: return t;
    case BindingResponse::Square: return t * t;
    case BindingResponse::Cube: return t * t * t;
    case BindingResponse::SquareRoot: return t <= 0 ? 0 : std::sqrt(t);
    case BindingResponse::SmoothStep: return t * t * (3.0 - 2.0 * t);
    case BindingResponse::Elastic: {
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        const double k = 6.0, w = 3.0 * 3.14159265358979323846;
        return 1.0 - std::exp(-k * t) * (std::cos(w * t) + (k / w) * std::sin(w * t));
    }
    case BindingResponse::Threshold: return t >= 0.5 ? 1.0 : 0.0;
    case BindingResponse::Invert: return 1.0 - t;
    }
    return t;
}

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("BindingResponse\n");

    std::printf("\n1. 每个响应在 [0,1] 上全有限且有界(Elastic 允许过冲)\n");
    {
        const std::vector<BindingResponse> all = {
            BindingResponse::Linear, BindingResponse::Square, BindingResponse::Cube,
            BindingResponse::SquareRoot, BindingResponse::SmoothStep, BindingResponse::Elastic,
            BindingResponse::Threshold, BindingResponse::Invert};
        Check(all.size() == kBindingResponseCount, "closed set size matches kBindingResponseCount");
        for (auto r : all) {
            bool finite = true, monotone = true, bounded = true;
            double prev = -1e9, maxV = -1e9, minV = 1e9;
            for (int i = 0; i <= 1000; ++i) {
                const double t = i / 1000.0;
                const double v = Response(r, t);
                if (!std::isfinite(v)) finite = false;
                if (v < prev - 1e-9) monotone = false;
                prev = v;
                maxV = std::max(maxV, v); minV = std::min(minV, v);
                if (v < -1.0 || v > 2.0) bounded = false;
            }
            const char* n = miaodesk::content::BindingResponseKey(r);
            Check(finite, std::string("finite over [0,1]: ") + n);
            Check(bounded, std::string("bounded in [-1,2]: ") + n);
            if (r != BindingResponse::Elastic && r != BindingResponse::Invert)
                Check(monotone, std::string("monotone increasing: ") + n);
            if (r == BindingResponse::Invert)
                Check(!monotone, std::string("invert is monotone decreasing: ") + n);
        }
    }

    std::printf("\n2. 端点固定:0 -> 0,1 -> 1(Elastic 也如此)\n");
    for (auto r : {BindingResponse::Linear, BindingResponse::Square, BindingResponse::Cube,
                   BindingResponse::SquareRoot, BindingResponse::SmoothStep,
                   BindingResponse::Elastic, BindingResponse::Threshold, BindingResponse::Invert}) {
        if (r == BindingResponse::Invert) {
            Check(std::abs(Response(r, 0.0) - 1.0) < 1e-12 && std::abs(Response(r, 1.0)) < 1e-12,
                  "invert endpoints pinned: 0->1, 1->0");
        } else {
            Check(std::abs(Response(r, 0.0)) < 1e-12 && std::abs(Response(r, 1.0) - 1.0) < 1e-12,
                  std::string("endpoints pinned: ") + miaodesk::content::BindingResponseKey(r));
        }
    }

    std::printf("\n3. Elastic 必须真的过冲(否则没有弹性)\n");
    {
        double peak = 0;
        for (int i = 0; i <= 1000; ++i) peak = std::max(peak, Response(BindingResponse::Elastic, i / 1000.0));
        Check(peak > 1.05, "elastic overshoots above 1.05 (peak " + std::to_string(peak) + ")");
        // 有意义的下冲:过冲之后必须回落到 1 以下,否则它是单调逼近不是弹性
        bool crossedBackBelow = false;
        bool wasAbove = false;
        for (int i = 0; i <= 2000; ++i) {
            const double v = Response(BindingResponse::Elastic, i / 2000.0);
            if (v > 1.02) wasAbove = true;
            if (wasAbove && v < 0.99) crossedBackBelow = true;
        }
        Check(crossedBackBelow, "elastic crosses back below 1 after overshooting");
    }

    std::printf("\n4. Threshold 是阶跃,不是斜坡\n");
    {
        Check(Response(BindingResponse::Threshold, 0.49) == 0.0, "0.49 -> 0");
        Check(Response(BindingResponse::Threshold, 0.50) == 1.0, "0.50 -> 1");
        Check(Response(BindingResponse::Threshold, 0.99) == 1.0, "0.99 -> 1");
    }

    std::printf("\n5. SquareRoot / Square 的排序关系\n");
    {
        Check(Response(BindingResponse::SquareRoot, 0.25) > 0.25, "sqrt lifts mid values");
        Check(Response(BindingResponse::Square, 0.25) < 0.25, "square suppresses mid values");
        Check(Response(BindingResponse::SmoothStep, 0.5) == 0.5, "smoothstep is symmetric at 0.5");
    }

    std::printf("\n6. 序列化 key 往返\n");
    {
        const std::vector<BindingResponse> all = {
            BindingResponse::Linear, BindingResponse::Square, BindingResponse::Cube,
            BindingResponse::SquareRoot, BindingResponse::SmoothStep, BindingResponse::Elastic,
            BindingResponse::Threshold, BindingResponse::Invert};
        bool ok = true;
        for (auto r : all) {
            BindingResponse back;
            if (!miaodesk::content::ParseBindingResponse(miaodesk::content::BindingResponseKey(r), &back) || back != r) ok = false;
        }
        Check(ok, "every response round-trips through its key");
        BindingResponse tmp;
        Check(!miaodesk::content::ParseBindingResponse("wobble", &tmp), "unknown key rejected");
        Check(!miaodesk::content::ParseBindingResponse("", &tmp), "empty key rejected");
        Check(!miaodesk::content::ParseBindingResponse("Linear", &tmp), "keys are lowercase only");
    }

    std::printf("\n7. Linear 必须与旧行为逐位一致\n");
    {
        bool identical = true;
        for (int i = -5; i <= 105; ++i) {
            const double v = i / 10.0;
            if (Response(BindingResponse::Linear, v) != v) identical = false;
        }
        Check(identical, "Linear is the identity, so existing packages are unaffected");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
