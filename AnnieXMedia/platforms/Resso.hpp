#pragma once
#include "AnnieXMedia/platforms/Youtube.hpp"
#include <regex>
#include <string>

namespace AnnieXMedia::platforms {
class RessoAPI {
public:
    bool valid(const std::string& link) const {
        return std::regex_search(link, std::regex("^https://m\\.resso\\.com/.+"));
    }
    TrackDetails track(const std::string& url) { return YouTube.track(url, false); }
};
extern RessoAPI Resso;
} // namespace AnnieXMedia::platforms
