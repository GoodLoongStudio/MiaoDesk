#pragma once

#include <string>

#include "miaodesk/MiaoContentDataBinding.h"

namespace miaodesk::desktop {

class TodayTaskContentProvider {
public:
    static bool Capture(content::ContentDataValues* values, std::wstring* error = nullptr);
    static bool SelfTest();
};

} // namespace miaodesk::desktop
