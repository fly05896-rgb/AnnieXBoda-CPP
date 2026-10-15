#include "AnnieXMedia/utils/cookie_handler.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/logging.hpp"
#include "config.hpp"
#include <filesystem>
#include <fstream>

namespace AnnieXMedia::utils {

std::string resolve_raw_cookie_url(const std::string& url) {
    if (url.find("pastebin.com/") != std::string::npos && url.find("/raw/") == std::string::npos) {
        auto pos = url.find_last_of('/');
        if (pos != std::string::npos) {
            return "https://pastebin.com/raw/" + url.substr(pos + 1);
        }
    }
    if (url.find("batbin.me/") != std::string::npos && url.find("/api/v2/paste/") == std::string::npos) {
        auto pos = url.find_last_of('/');
        if (pos != std::string::npos) {
            return "https://batbin.me/api/v2/paste/" + url.substr(pos + 1);
        }
    }
    return url;
}

void fetch_and_store_cookies() {
    if (config::COOKIE_URL.empty()) {
        LOGGER("AnnieXMedia.utils.cookie_handler").warning("⚠️ COOKIE_URL not set in env.");
        return;
    }
    std::string raw_url = resolve_raw_cookie_url(config::COOKIE_URL);
    std::filesystem::create_directories("AnnieXMedia/assets");
    auto [out, rc] = core::run_shell_command(
        "curl -fsSL -m 15 -A \"annie-cookie-fetcher/1.0\" \"" + raw_url + "\" -o AnnieXMedia/assets/cookies.txt"
    );
    if (rc == 0 && std::filesystem::exists("AnnieXMedia/assets/cookies.txt")) {
        LOGGER("AnnieXMedia.utils.cookie_handler").info("✅ YouTube Cookies Loaded Successfully.");
    } else {
        LOGGER("AnnieXMedia.utils.cookie_handler").warning("⚠️ Failed to fetch cookies from COOKIE_URL.");
    }
}

} // namespace AnnieXMedia::utils
