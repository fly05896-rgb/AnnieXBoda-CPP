#pragma once
#include "AnnieXMedia/platforms/Youtube.hpp"
#include <regex>
#include <string>

namespace AnnieXMedia::platforms {
class SoundAPI {
public:
    bool valid(const std::string& link) const {
        return std::regex_search(link, std::regex("^https?://(soundcloud\\.com|on\\.soundcloud\\.com)/.+"));
    }
    TrackDetails download(const std::string& url) {
        auto td = YouTube.track(url, false);
        td.file_path = YouTube.download(url, false, false);
        return td;
    }
};
extern SoundAPI SoundCloud;
} // namespace AnnieXMedia::platforms
