#pragma once
#include "AnnieXMedia/platforms/Youtube.hpp"
#include <string>

namespace AnnieXMedia::platforms {
class SongDownloaderAPI {
public:
    bool force_high_quality = false;
    int playlist_limit = 10;

    void set_limit(int limit) { playlist_limit = limit; }
    void open_limit() { playlist_limit = 0; }
    void reset_limit() { playlist_limit = 10; }
    void enable_quality() { force_high_quality = true; }
    void disable_quality() { force_high_quality = false; }

    TrackDetails get_info(const std::string& query) { return YouTube.track(query, false); }
    std::string download_media(const std::string& link, bool is_video) { return YouTube.download(link, is_video, false); }
};
extern SongDownloaderAPI SongDownloader;
} // namespace AnnieXMedia::platforms
