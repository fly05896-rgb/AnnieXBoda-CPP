#include "AnnieXMedia/utils/cookie_handler.hpp"
#include "AnnieXMedia/logging.hpp"
#include <filesystem>

namespace AnnieXMedia::utils {

std::string resolve_raw_cookie_url(const std::string& url) {
    return url;
}

void fetch_and_store_cookies() {
    std::error_code ec;
    if (std::filesystem::exists("AnnieXMedia/assets/cookies.txt", ec)) {
        std::filesystem::remove("AnnieXMedia/assets/cookies.txt", ec);
    }
    LOGGER("AnnieXMedia.utils.cookie_handler").info("✅ Cookie-Free YouTube Innertube Engine (VISIONOS / IPv4) Active.");
}

} // namespace AnnieXMedia::utils
