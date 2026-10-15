#include "AnnieXMedia/platforms/Carbon.hpp"
#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/core/git.hpp"

namespace AnnieXMedia::platforms {
CarbonAPI Carbon;

std::string CarbonAPI::generate(const std::string& text, int64_t user_id) {
    (void)text;
    std::string out = core::CACHE_DIR + "/carbon_" + std::to_string(user_id) + ".png";
    return out;
}
} // namespace AnnieXMedia::platforms
