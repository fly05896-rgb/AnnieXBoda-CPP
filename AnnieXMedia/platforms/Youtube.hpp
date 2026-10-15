#pragma once
#include <string>
#include <vector>

namespace AnnieXMedia::platforms {

struct TrackDetails {
    std::string title;
    std::string link;
    std::string vidid;
    std::string duration_min;
    int duration_sec = 0;
    std::string thumb;
    std::string file_path;
};

class YouTubeAPI {
public:
    std::string base = "https://www.youtube.com/watch?v=";
    std::string listbase = "https://youtube.com/playlist?list=";

    YouTubeAPI() = default;

    bool exists(const std::string& link) const;
    TrackDetails track(const std::string& query_or_link, bool is_videoid = false);
    std::string get_direct_link(const std::string& link, bool prefer_audio = true);
    std::string download(const std::string& query_or_link, bool is_video = false, bool is_videoid = false);
    std::vector<TrackDetails> playlist(const std::string& link, int limit = 30);
};

extern YouTubeAPI YouTube;

} // namespace AnnieXMedia::platforms
