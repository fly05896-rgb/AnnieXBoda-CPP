#pragma once
#include <string>

namespace AnnieXMedia::platforms {
class CarbonAPI {
public:
    std::string generate(const std::string& text, int64_t user_id);
};
extern CarbonAPI Carbon;
} // namespace AnnieXMedia::platforms
