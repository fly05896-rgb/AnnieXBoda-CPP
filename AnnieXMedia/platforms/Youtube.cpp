#include "AnnieXMedia/platforms/Youtube.hpp"
#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/logging.hpp"
#include "AnnieXMedia/utils/formatters.hpp"
#include "config.hpp"
#include <filesystem>
#include <regex>
#include <sstream>

namespace AnnieXMedia::platforms {

YouTubeAPI YouTube;

bool YouTubeAPI::exists(const std::string& link) const {
    std::regex yt_re("(https?://)?(www\\.|m\\.|music\\.)?(youtube\\.com/(watch\\?v=|shorts/|live/|playlist\\?list=)|youtu\\.be/)");
    return std::regex_search(link, yt_re);
}

static std::string cookie_arg() {
    if (std::filesystem::exists("AnnieXMedia/assets/cookies.txt")) {
        return "--cookies \"AnnieXMedia/assets/cookies.txt\" ";
    }
    return "";
}

TrackDetails YouTubeAPI::track(const std::string& query_or_link, bool is_videoid) {
    std::string target = query_or_link;
    if (is_videoid) {
        target = base + query_or_link;
    } else if (!exists(query_or_link) && query_or_link.rfind("http", 0) != 0) {
        target = "ytsearch1:" + query_or_link;
    }

    std::string cmd = "yt-dlp --quiet --no-warnings --no-playlist " + cookie_arg() +
                      "--print \"%(id)s\t%(title)s\t%(duration)s\t%(webpage_url)s\t%(thumbnail)s\" \"" + target + "\"";
    auto [out, rc] = core::run_shell_command(cmd);
    TrackDetails td;
    if (rc == 0 && !out.empty()) {
        std::stringstream ss(out);
        std::string line;
        if (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::stringstream lss(line);
            std::string dur_s;
            std::getline(lss, td.vidid, '\t');
            std::getline(lss, td.title, '\t');
            std::getline(lss, dur_s, '\t');
            std::getline(lss, td.link, '\t');
            std::getline(lss, td.thumb, '\t');
            try { td.duration_sec = std::stoi(dur_s); } catch (...) { td.duration_sec = 0; }
            td.duration_min = td.duration_sec > 0 ? utils::formatters::seconds_to_min(td.duration_sec) : "Live";
        }
    }
    if (td.title.empty()) {
        td.title = query_or_link;
        td.vidid = is_videoid ? query_or_link : "youtube";
        td.link = is_videoid ? (base + query_or_link) : query_or_link;
        td.duration_min = "03:30";
        td.duration_sec = 210;
        td.thumb = config::YOUTUBE_IMG_URL;
    }
    return td;
}

std::string YouTubeAPI::get_direct_link(const std::string& link, bool prefer_audio) {
    std::string fmt = prefer_audio ? "bestaudio/best" : "best[height<=?720]/best";
    std::string cmd = "yt-dlp --quiet --no-warnings --no-playlist " + cookie_arg() +
                      "-g -f \"" + fmt + "\" \"" + link + "\"";
    auto [out, rc] = core::run_shell_command(cmd);
    if (rc == 0 && !out.empty()) {
        std::stringstream ss(out);
        std::string first_line;
        std::getline(ss, first_line);
        while (!first_line.empty() && (first_line.back() == '\r' || first_line.back() == '\n')) first_line.pop_back();
        if (!first_line.empty()) return first_line;
    }
    return link;
}

std::string YouTubeAPI::download(const std::string& query_or_link, bool is_video, bool is_videoid) {
    std::string target = is_videoid ? (base + query_or_link) : query_or_link;
    if (!exists(target) && target.rfind("http", 0) != 0) {
        target = "ytsearch1:" + target;
    }
    std::string ext = is_video ? "mp4" : "m4a";
    std::string fmt = is_video ? "bestvideo[height<=?720][ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best"
                               : "bestaudio[ext=m4a]/bestaudio/best";
    std::string out_tmpl = core::DOWNLOAD_DIR + "/%(id)s." + ext;
    std::string cmd = "yt-dlp --quiet --no-warnings --no-playlist " + cookie_arg() +
                      "-f \"" + fmt + "\" -o \"" + out_tmpl + "\" --print after_move:filepath \"" + target + "\"";
    auto [out, rc] = core::run_shell_command(cmd);
    if (rc == 0 && !out.empty()) {
        std::stringstream ss(out);
        std::string path;
        std::getline(ss, path);
        while (!path.empty() && (path.back() == '\r' || path.back() == '\n')) path.pop_back();
        if (std::filesystem::exists(path)) return path;
    }
    // Fallback to direct stream link
    return get_direct_link(target, !is_video);
}

std::vector<TrackDetails> YouTubeAPI::playlist(const std::string& link, int limit) {
    std::vector<TrackDetails> list;
    std::string cmd = "yt-dlp --quiet --no-warnings --flat-playlist --playlist-end " + std::to_string(limit) +
                      " " + cookie_arg() + "--print \"%(id)s\t%(title)s\t%(duration)s\" \"" + link + "\"";
    auto [out, rc] = core::run_shell_command(cmd);
    if (rc == 0 && !out.empty()) {
        std::stringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            TrackDetails td;
            std::stringstream lss(line);
            std::string dur_s;
            std::getline(lss, td.vidid, '\t');
            std::getline(lss, td.title, '\t');
            std::getline(lss, dur_s, '\t');
            try { td.duration_sec = std::stoi(dur_s); } catch (...) { td.duration_sec = 180; }
            td.duration_min = utils::formatters::seconds_to_min(td.duration_sec);
            td.link = base + td.vidid;
            list.push_back(td);
        }
    }
    return list;
}

} // namespace AnnieXMedia::platforms
