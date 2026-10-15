#pragma once
#include "AnnieXMedia/platforms/Youtube.hpp"
#include <regex>
#include <string>

namespace AnnieXMedia::platforms {
class SpotifyAPI {
public:
    bool valid(const std::string& link) const {
        return std::regex_search(link, std::regex("^https://open\\.spotify\\.com/.+"));
    }
    TrackDetails track(const std::string& link) { return YouTube.track(link, false); }
};
extern SpotifyAPI Spotify;
} // namespace AnnieXMedia::platforms
