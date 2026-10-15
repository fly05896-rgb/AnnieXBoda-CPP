#pragma once
#include "AnnieXMedia/platforms/Youtube.hpp"
#include <regex>
#include <string>

namespace AnnieXMedia::platforms {
class AppleAPI {
public:
    bool valid(const std::string& link) const {
        return std::regex_search(link, std::regex("^https://music\\.apple\\.com/.+"));
    }
    TrackDetails track(const std::string& url) { return YouTube.track(url, false); }
};
extern AppleAPI Apple;
} // namespace AnnieXMedia::platforms
