#pragma once

#include <string>

#include "whaledesk/WhaleContentDataBinding.h"

namespace whaledesk::desktop {

class TodayTaskContentProvider {
public:
    static bool Capture(content::ContentDataValues* values, std::wstring* error = nullptr);
    static bool SelfTest();
};

} // namespace whaledesk::desktop
