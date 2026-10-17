#include "AnnieXMedia/platforms/Youtube.hpp"
#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/logging.hpp"
#include "AnnieXMedia/utils/formatters.hpp"
#include "config.hpp"

#include <cctype>
#include <filesystem>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace AnnieXMedia::platforms {

YouTubeAPI YouTube;

static std::string shell_quote(const std::string& arg) {
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}

static bool is_valid_vidid(const std::string& s) {
    if (s.size() != 11) return false;
    for (unsigned char c : s) {
        if (!std::isalnum(c) && c != '-' && c != '_') return false;
    }
    return true;
}

static std::string cookie_arg() {
    if (std::filesystem::exists("AnnieXMedia/assets/cookies.txt") &&
        std::filesystem::file_size("AnnieXMedia/assets/cookies.txt") > 32) {
        return "--cookies \"AnnieXMedia/assets/cookies.txt\" ";
    }
    return "";
}

bool YouTubeAPI::exists(const std::string& link) const {
    std::regex yt_re("(https?://)?(www\\.|m\\.|music\\.)?(youtube\\.com/(watch\\?v=|shorts/|live/|playlist\\?list=)|youtu\\.be/)");
    return std::regex_search(link, yt_re);
}

TrackDetails YouTubeAPI::track(const std::string& query_or_link, bool is_videoid) {
    std::string query = query_or_link;
    auto amp = query.find('&');
    if (amp != std::string::npos && query.rfind("http", 0) == 0) {
        query = query.substr(0, amp);
    }

    std::string target;
    std::string mode_flags = "--no-playlist ";
    if (is_videoid || is_valid_vidid(query)) {
        target = base + query;
    } else if (exists(query) || query.rfind("http://", 0) == 0 || query.rfind("https://", 0) == 0) {
        target = query;
    } else {
        target = "ytsearch1:" + query;
        mode_flags = "--flat-playlist ";
    }

    std::string cmd = "yt-dlp --no-warnings --no-check-certificates " + mode_flags + cookie_arg() +
                      "--print \"%(id)s\t%(title)s\t%(duration)s\t%(webpage_url)s\t%(thumbnail)s\" " +
                      shell_quote(target) + " 2>&1";

    LOGGER("AnnieXMedia.platforms.Youtube").info("🔎 Running YouTube track lookup: " + cmd);
    auto [out, rc] = core::run_shell_command(cmd);

    TrackDetails td;
    if (!out.empty()) {
        std::stringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find('\t') == std::string::npos) continue;

            std::stringstream lss(line);
            std::string dur_s;
            std::getline(lss, td.vidid, '\t');
            std::getline(lss, td.title, '\t');
            std::getline(lss, dur_s, '\t');
            std::getline(lss, td.link, '\t');
            std::getline(lss, td.thumb, '\t');

            if (!td.vidid.empty() && !td.title.empty()) {
                try { td.duration_sec = static_cast<int>(std::stod(dur_s)); } catch (...) { td.duration_sec = 0; }
                td.duration_min = td.duration_sec > 0 ? utils::formatters::seconds_to_min(td.duration_sec) : "Live";
                if (td.link.empty() || td.link == "NA" || !exists(td.link)) {
                    td.link = is_valid_vidid(td.vidid) ? (base + td.vidid) : target;
                }
                if (td.thumb.empty() || td.thumb == "NA") {
                    td.thumb = is_valid_vidid(td.vidid)
                        ? ("https://i.ytimg.com/vi/" + td.vidid + "/hqdefault.jpg")
                        : config::YOUTUBE_IMG_URL;
                }
                LOGGER("AnnieXMedia.platforms.Youtube").info(
                    "✅ YouTube track found: title=[" + td.title + "] id=[" + td.vidid + "] duration=[" + td.duration_min + "]"
                );
                return td;
            }
        }
    }

    std::string err_msg = "❌ فشل البحث في يوتيوب عبر yt-dlp (exit code=" + std::to_string(rc) + "):\n" +
                          (out.empty() ? "لم يُرجع yt-dlp أي نتائج." : out);
    LOGGER("AnnieXMedia.platforms.Youtube").error(err_msg);
    throw std::runtime_error(err_msg);
}

// سحب الرابط المباشر من يوتيوب عبر yt-dlp -g بنفس طريقة ملفات ntgcalls الرسمية (examples/python/utils.py -> get_youtube_stream)
std::string YouTubeAPI::get_direct_link(const std::string& link, bool prefer_audio) {
    std::string target = link;
    if (is_valid_vidid(target)) {
        target = base + target;
    } else if (!exists(target) && target.rfind("http", 0) != 0) {
        auto td = track(target, false);
        target = td.link;
    }

    std::string fmt = prefer_audio
        ? "bestaudio[ext=m4a]/bestaudio/best"
        : "best[height<=?720]/bestvideo[height<=?720]+bestaudio/best";

    std::string cookies = cookie_arg();
    std::string cmd = "yt-dlp --no-warnings --no-check-certificates --no-playlist " + cookies +
                      "-g -f \"" + fmt + "\" " + shell_quote(target) + " 2>&1";

    LOGGER("AnnieXMedia.platforms.Youtube").info("🔗 Pulling direct YouTube stream URL (yt-dlp -g): " + cmd);
    auto [out, rc] = core::run_shell_command(cmd);

    std::string direct_url;
    if (!out.empty()) {
        std::stringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.rfind("https://", 0) == 0 || line.rfind("http://", 0) == 0) {
                direct_url = line;
                if (prefer_audio) break;
            }
        }
    }

    if (!direct_url.empty()) {
        LOGGER("AnnieXMedia.platforms.Youtube").info(
            "✅ Successfully extracted direct YouTube URL (" + std::to_string(direct_url.size()) + " chars): " +
            direct_url.substr(0, 80) + "..."
        );
        return direct_url;
    }

    std::string cookie_status = cookies.empty()
        ? "⚠️ ملاحظة: ملف الكوكيز غير مفعل (COOKIE_URL غير مضبوط في السكرتس)."
        : "ℹ️ ملاحظة: تم استخدام ملف الكوكيز AnnieXMedia/assets/cookies.txt.";

    std::string full_reason =
        "❌ رفض يوتيوب إعطاء الرابط المباشر عبر (yt-dlp -g)!\n"
        "• الرابط المطلوب: " + target + "\n"
        "• كود الخروج: " + std::to_string(rc) + "\n"
        "• " + cookie_status + "\n"
        "• رد yt-dlp التفصيلي:\n" + (out.empty() ? "(فارغ)" : out);

    LOGGER("AnnieXMedia.platforms.Youtube").error(full_reason);
    throw std::runtime_error(full_reason);
}

std::string YouTubeAPI::download(const std::string& query_or_link, bool is_video, bool is_videoid) {
    std::string target = is_videoid ? (base + query_or_link) : query_or_link;
    if (is_valid_vidid(target)) {
        target = base + target;
    } else if (!exists(target) && target.rfind("http", 0) != 0) {
        auto td = track(target, false);
        target = td.link;
    }
    return get_direct_link(target, !is_video);
}

std::vector<TrackDetails> YouTubeAPI::playlist(const std::string& link, int limit) {
    std::vector<TrackDetails> list;
    std::string cmd = "yt-dlp --no-warnings --no-check-certificates --flat-playlist --playlist-end " +
                      std::to_string(limit) + " " + cookie_arg() +
                      "--print \"%(id)s\t%(title)s\t%(duration)s\" " + shell_quote(link) + " 2>&1";
    auto [out, rc] = core::run_shell_command(cmd);
    if (rc == 0 && !out.empty()) {
        std::stringstream ss(out);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find('\t') == std::string::npos) continue;
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
    } else {
        LOGGER("AnnieXMedia.platforms.Youtube").error("❌ YouTube playlist error:\n" + out);
    }
    return list;
}

} // namespace AnnieXMedia::platforms
