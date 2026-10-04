#include "miaodesk/MiaoIniReadLimit.h"

namespace miaodesk::ini_read {

const char* ExplainTruncatedRead() noexcept {
    return "读取配置文件时内容超出缓冲区，已截断；这份配置可能不完整，"
           "请检查它的值是否被写长过";
}

} // namespace miaodesk::ini_read
