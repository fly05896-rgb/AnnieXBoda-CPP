#include "AnnieXMedia/platforms/Youtube.hpp"
#include "AnnieXMedia/core/dir.hpp"
#include "AnnieXMedia/core/git.hpp"
#include "AnnieXMedia/logging.hpp"
#include "AnnieXMedia/utils/formatters.hpp"
#include "config.hpp"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace AnnieXMedia::platforms {

YouTubeAPI YouTube;

namespace {

constexpr int64_t CACHE_TTL_SEC = 3600;

std::mutex g_yt_cache_mtx;
std::unordered_map<std::string, std::pair<std::string, int64_t>> g_url_cache;
std::unordered_map<std::string, std::pair<TrackDetails, int64_t>> g_meta_cache;

int64_t now_epoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string shell_quote(const std::string& arg) {
    std::string out = "'";
    for (char c : arg) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
}

bool is_valid_vidid(const std::string& s) {
    if (s.size() != 11) return false;
    for (unsigned char c : s) {
        if (!std::isalnum(c) && c != '-' && c != '_') return false;
    }
    return true;
}

std::string extract_vid(const std::string& link, bool is_videoid = false) {
    if (is_videoid && is_valid_vidid(link)) {
        return link;
    }
    std::string s = link;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();

    if (s.size() == 13 && s.ends_with("_v")) {
        s = s.substr(0, 11);
    }
    if (is_valid_vidid(s) && s.rfind("http", 0) != 0 && s.find(' ') == std::string::npos) {
        return s;
    }
    auto vpos = s.find("v=");
    if (vpos != std::string::npos) {
        std::string sub = s.substr(vpos + 2);
        auto amp = sub.find_first_of("&#? ");
        if (amp != std::string::npos) sub = sub.substr(0, amp);
        if (is_valid_vidid(sub)) return sub;
    }
    for (const char* prefix : {"youtu.be/", "/shorts/", "/live/"}) {
        auto ppos = s.find(prefix);
        if (ppos != std::string::npos) {
            std::string sub = s.substr(ppos + std::char_traits<char>::length(prefix));
            auto qpos = sub.find_first_of("?&# ");
            if (qpos != std::string::npos) sub = sub.substr(0, qpos);
            if (is_valid_vidid(sub)) return sub;
        }
    }
    return "";
}

std::string sec_to_min_str(int seconds) {
    if (seconds <= 0) return "0:00";
    int m = seconds / 60;
    int s = seconds % 60;
    int h = m / 60;
    m %= 60;
    char buf[32];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    }
    return buf;
}

int parse_duration_to_sec(const std::string& dur) {
    if (dur.empty() || dur == "Live") return 0;
    std::vector<int> parts;
    std::stringstream ss(dur);
    std::string item;
    while (std::getline(ss, item, ':')) {
        try {
            parts.push_back(std::stoi(item));
        } catch (...) {
            return 0;
        }
    }
    int total = 0;
    int mult = 1;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        total += (*it) * mult;
        mult *= 60;
    }
    return total;
}

struct JsonVal {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonVal> arr;
    std::map<std::string, JsonVal> obj;

    const JsonVal& get(const std::string& k) const {
        static const JsonVal null_v;
        if (type != Object) return null_v;
        auto it = obj.find(k);
        return (it != obj.end()) ? it->second : null_v;
    }
    std::string as_str(const std::string& def = "") const {
        if (type == String) return str;
        if (type == Number) return std::to_string(static_cast<int64_t>(num));
        return def;
    }
    int64_t as_int(int64_t def = 0) const {
        if (type == Number) return static_cast<int64_t>(num);
        if (type == String) {
            try { return std::stoll(str); } catch (...) {}
        }
        return def;
    }
    bool as_bool(bool def = false) const {
        if (type == Bool) return b;
        return def;
    }
};

class JsonParser {
    const std::string& s_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) {
            ++pos_;
        }
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::string parse_string() {
        if (pos_ >= s_.size() || s_[pos_] != '"') return "";
        ++pos_;
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return out;
            if (c == '\\' && pos_ < s_.size()) {
                char esc = s_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 <= s_.size()) {
                            uint32_t cp = 0;
                            try { cp = static_cast<uint32_t>(std::stoul(s_.substr(pos_, 4), nullptr, 16)); } catch (...) {}
                            pos_ += 4;
                            if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                                uint32_t low = 0;
                                try { low = static_cast<uint32_t>(std::stoul(s_.substr(pos_ + 2, 4), nullptr, 16)); } catch (...) {}
                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    pos_ += 6;
                                    cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                                }
                            }
                            append_utf8(out, cp);
                        }
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    JsonVal parse_value() {
        skip_ws();
        if (pos_ >= s_.size()) return {};
        char c = s_[pos_];
        if (c == '"') {
            JsonVal v;
            v.type = JsonVal::String;
            v.str = parse_string();
            return v;
        }
        if (c == '{') {
            ++pos_;
            JsonVal v;
            v.type = JsonVal::Object;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return v; }
            while (pos_ < s_.size()) {
                skip_ws();
                std::string k = parse_string();
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ':') ++pos_;
                v.obj[std::move(k)] = parse_value();
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
                if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; break; }
                break;
            }
            return v;
        }
        if (c == '[') {
            ++pos_;
            JsonVal v;
            v.type = JsonVal::Array;
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return v; }
            while (pos_ < s_.size()) {
                v.arr.push_back(parse_value());
                skip_ws();
                if (pos_ < s_.size() && s_[pos_] == ',') { ++pos_; continue; }
                if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; break; }
                break;
            }
            return v;
        }
        if (s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            JsonVal v; v.type = JsonVal::Bool; v.b = true; return v;
        }
        if (s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            JsonVal v; v.type = JsonVal::Bool; v.b = false; return v;
        }
        if (s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return {};
        }
        size_t start = pos_;
        while (pos_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[pos_])) ||
               s_[pos_] == '-' || s_[pos_] == '+' || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
        }
        if (pos_ > start) {
            JsonVal v;
            v.type = JsonVal::Number;
            try { v.num = std::stod(s_.substr(start, pos_ - start)); } catch (...) { v.num = 0.0; }
            return v;
        }
        ++pos_;
        return {};
    }
};

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

std::optional<TrackDetails> innertube_player(const std::string& vid, const std::string& base_url) {
    if (!is_valid_vidid(vid)) return std::nullopt;
    int64_t now = now_epoch();
    {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        auto it = g_meta_cache.find(vid);
        if (it != g_meta_cache.end() && it->second.second > now) {
            auto a_it = g_url_cache.find(vid + ":a");
            if (a_it != g_url_cache.end() && a_it->second.second > now) {
                return it->second.first;
            }
        }
    }

    std::string payload =
        "{\"context\":{\"client\":{"
        "\"clientName\":\"VISIONOS\","
        "\"clientVersion\":\"1.02\","
        "\"deviceMake\":\"Apple\","
        "\"deviceModel\":\"RealityDevice17,1\","
        "\"osName\":\"visionOS\","
        "\"osVersion\":\"26.5.23O471\","
        "\"hl\":\"en\","
        "\"timeZone\":\"UTC\","
        "\"utcOffsetMinutes\":0"
        "}},\"videoId\":\"" + json_escape(vid) + "\","
        "\"contentCheckOk\":true,\"racyCheckOk\":true}";

    std::string cmd =
        "curl -4 -sS --max-time 4 --connect-timeout 2 "
        "-X POST 'https://www.youtube.com/youtubei/v1/player?prettyPrint=false' "
        "-H 'Content-Type: application/json' "
        "-H 'User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15' "
        "-H 'X-YouTube-Client-Name: 101' "
        "-H 'X-YouTube-Client-Version: 1.02' "
        "-H 'Origin: https://www.youtube.com' "
        "--data-raw " + shell_quote(payload) + " 2>/dev/null";

    auto [out, rc] = core::run_shell_command(cmd);
    if (rc != 0 || out.empty()) return std::nullopt;

    JsonVal root = JsonParser(out).parse_value();
    const JsonVal& vd = root.get("videoDetails");
    std::string title = vd.get("title").as_str();
    if (title.empty()) return std::nullopt;

    std::string v_id = vd.get("videoId").as_str(vid);
    int secs = static_cast<int>(vd.get("lengthSeconds").as_int(0));
    bool is_live = vd.get("isLive").as_bool(false) || (vd.get("isLiveContent").as_bool(false) && secs == 0);
    std::string duration_min = is_live ? "Live" : sec_to_min_str(secs);

    const JsonVal& sd = root.get("streamingData");
    std::string cached_audio_url;
    std::string cached_video_url;

    auto scan_formats = [&](const JsonVal& arr_val) {
        if (arr_val.type != JsonVal::Array) return;
        for (const auto& fmt : arr_val.arr) {
            std::string f_url = fmt.get("url").as_str();
            if (f_url.empty()) continue;
            std::string itag = fmt.get("itag").as_str();
            std::string mime = fmt.get("mimeType").as_str();
            if (itag == "18") {
                cached_video_url = f_url;
                if (cached_audio_url.empty()) cached_audio_url = f_url;
            } else if (mime.find("audio") != std::string::npos) {
                cached_audio_url = f_url;
            }
        }
    };
    scan_formats(sd.get("formats"));
    scan_formats(sd.get("adaptiveFormats"));

    TrackDetails td;
    td.title = title;
    td.vidid = v_id;
    td.link = base_url + v_id;
    td.duration_sec = secs;
    td.duration_min = duration_min;
    td.thumb = "https://i.ytimg.com/vi/" + v_id + "/hqdefault.jpg";

    {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        g_meta_cache[v_id] = {td, now + CACHE_TTL_SEC};
        if (!cached_video_url.empty()) {
            g_url_cache[v_id + ":v"] = {cached_video_url, now + CACHE_TTL_SEC};
        }
        if (!cached_audio_url.empty()) {
            g_url_cache[v_id + ":a"] = {cached_audio_url, now + CACHE_TTL_SEC};
        }
    }

    return td;
}

std::vector<TrackDetails> innertube_search(const std::string& query, int limit, const std::string& base_url) {
    std::vector<TrackDetails> results;
    if (query.empty()) return results;

    std::string payload =
        "{\"context\":{\"client\":{"
        "\"clientName\":\"WEB\","
        "\"clientVersion\":\"2.20260708.00.00\","
        "\"hl\":\"ar\","
        "\"gl\":\"EG\","
        "\"timeZone\":\"UTC\","
        "\"utcOffsetMinutes\":0"
        "}},\"query\":\"" + json_escape(query) + "\","
        "\"params\":\"EgIQAfABAQ==\"}";

    std::string cmd =
        "curl -4 -sS --max-time 4 --connect-timeout 2 "
        "-X POST 'https://www.youtube.com/youtubei/v1/search?prettyPrint=false' "
        "-H 'Content-Type: application/json' "
        "-H 'User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36' "
        "-H 'X-YouTube-Client-Name: 1' "
        "-H 'X-YouTube-Client-Version: 2.20260708.00.00' "
        "-H 'Origin: https://www.youtube.com' "
        "--data-raw " + shell_quote(payload) + " 2>/dev/null";

    auto [out, rc] = core::run_shell_command(cmd);
    if (rc != 0 || out.empty()) return results;

    JsonVal root = JsonParser(out).parse_value();
    const JsonVal& sections = root.get("contents")
        .get("twoColumnSearchResultsRenderer")
        .get("primaryContents")
        .get("sectionListRenderer")
        .get("contents");

    if (sections.type != JsonVal::Array) return results;

    int64_t now = now_epoch();
    for (const auto& sec : sections.arr) {
        const JsonVal& items = sec.get("itemSectionRenderer").get("contents");
        if (items.type != JsonVal::Array) continue;
        for (const auto& item : items.arr) {
            const JsonVal& vr = item.get("videoRenderer");
            if (vr.type != JsonVal::Object) continue;
            std::string v_id = vr.get("videoId").as_str();
            if (v_id.empty()) continue;

            std::string title;
            const JsonVal& runs = vr.get("title").get("runs");
            if (runs.type == JsonVal::Array) {
                for (const auto& r : runs.arr) {
                    title += r.get("text").as_str();
                }
            }
            if (title.empty()) title = "Unknown";

            std::string dur_text = vr.get("lengthText").get("simpleText").as_str();
            if (dur_text.empty()) {
                dur_text = vr.get("lengthText")
                    .get("accessibility")
                    .get("accessibilityData")
                    .get("label")
                    .as_str("Live");
            }

            TrackDetails td;
            td.title = title;
            td.vidid = v_id;
            td.link = base_url + v_id;
            td.duration_min = dur_text;
            td.duration_sec = parse_duration_to_sec(dur_text);
            td.thumb = "https://i.ytimg.com/vi/" + v_id + "/hqdefault.jpg";

            {
                std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
                g_meta_cache[v_id] = {td, now + CACHE_TTL_SEC};
            }
            results.push_back(std::move(td));
            if (static_cast<int>(results.size()) >= limit) return results;
        }
    }
    return results;
}

} // namespace

bool YouTubeAPI::exists(const std::string& link) const {
    static const std::regex yt_re(
        R"((https?://)?(www\.|m\.|music\.)?(youtube\.com/(watch\?v=|shorts/|live/|playlist\?list=)|youtu\.be/)([A-Za-z0-9_-]{11}|PL[A-Za-z0-9_-]+|OLAK5uy_[A-Za-z0-9_-]+)([&?][^\s]*)?)"
    );
    return std::regex_search(link, yt_re);
}

TrackDetails YouTubeAPI::track(const std::string& query_or_link, bool is_videoid) {
    std::string vid = extract_vid(query_or_link, is_videoid);

    if (is_valid_vidid(vid)) {
        LOGGER("AnnieXMedia.platforms.Youtube").info("⚡ Fast Innertube Player lookup (cookie-free IPv4) for videoId: " + vid);
        if (auto info = innertube_player(vid, base)) {
            LOGGER("AnnieXMedia.platforms.Youtube").info(
                "✅ YouTube track found (Innertube Player): title=[" + info->title + "] id=[" + info->vidid + "] duration=[" + info->duration_min + "]"
            );
            return *info;
        }
    }

    std::string query = is_valid_vidid(vid) ? vid : query_or_link;
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.front()))) query.erase(query.begin());
    while (!query.empty() && std::isspace(static_cast<unsigned char>(query.back()))) query.pop_back();

    LOGGER("AnnieXMedia.platforms.Youtube").info("🔎 Fast Innertube Search (cookie-free IPv4) for query: " + query);
    auto results = innertube_search(query, 1, base);
    if (!results.empty()) {
        TrackDetails first = results.front();
        if (is_valid_vidid(first.vidid)) {
            if (auto full_info = innertube_player(first.vidid, base)) {
                first = *full_info;
            }
        }
        LOGGER("AnnieXMedia.platforms.Youtube").info(
            "✅ YouTube track found: title=[" + first.title + "] id=[" + first.vidid + "] duration=[" + first.duration_min + "]"
        );
        return first;
    }

    std::string target = is_valid_vidid(vid) ? (base + vid) : ("ytsearch1:" + query);
    std::string cmd =
        "yt-dlp --no-cookies --force-ipv4 --source-address 0.0.0.0 --no-warnings --no-check-certificates --flat-playlist "
        "--extractor-args \"youtube:player_client=visionos,android;player_skip=webpage,configs,initial_data,js;fetch_pot=never\" "
        "--print \"%(id)s\t%(title)s\t%(duration)s\t%(webpage_url)s\t%(thumbnail)s\" " +
        shell_quote(target) + " 2>&1";

    auto [out, rc] = core::run_shell_command(cmd);
    if (!out.empty()) {
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
            std::getline(lss, td.link, '\t');
            std::getline(lss, td.thumb, '\t');
            if (!td.vidid.empty() && !td.title.empty()) {
                try { td.duration_sec = static_cast<int>(std::stod(dur_s)); } catch (...) { td.duration_sec = 0; }
                td.duration_min = td.duration_sec > 0 ? sec_to_min_str(td.duration_sec) : "Live";
                td.link = is_valid_vidid(td.vidid) ? (base + td.vidid) : target;
                td.thumb = "https://i.ytimg.com/vi/" + td.vidid + "/hqdefault.jpg";
                return td;
            }
        }
    }

    std::string err_msg = "❌ لم يتم العثور على نتائج في يوتيوب لـ: " + query;
    LOGGER("AnnieXMedia.platforms.Youtube").error(err_msg);
    throw std::runtime_error(err_msg);
}

std::string YouTubeAPI::get_direct_link(const std::string& link, bool prefer_audio) {
    std::string vid = extract_vid(link, false);
    if (!is_valid_vidid(vid) && !exists(link) && link.rfind("http", 0) != 0) {
        auto td = track(link, false);
        vid = td.vidid;
    }

    std::string target_url = is_valid_vidid(vid) ? (base + vid) : link;
    bool is_video = !prefer_audio;
    std::string cache_key = (is_valid_vidid(vid) ? vid : target_url) + (is_video ? ":v" : ":a");
    int64_t now = now_epoch();

    {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        auto it = g_url_cache.find(cache_key);
        if (it != g_url_cache.end() && it->second.second > now && !it->second.first.empty()) {
            LOGGER("AnnieXMedia.platforms.Youtube").info("⚡ Using cached direct YouTube stream URL for " + cache_key);
            return it->second.first;
        }
    }

    if (is_valid_vidid(vid)) {
        (void)innertube_player(vid, base);
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        auto it = g_url_cache.find(cache_key);
        if (it != g_url_cache.end() && it->second.second > now && !it->second.first.empty()) {
            LOGGER("AnnieXMedia.platforms.Youtube").info(
                "✅ Extracted direct YouTube URL via Innertube VISIONOS (cookie-free, " +
                std::to_string(it->second.first.size()) + " chars)"
            );
            return it->second.first;
        }
    }

    std::string media_format = is_video
        ? "best[ext=mp4][height<=720]/best[height<=720]/best"
        : "bestaudio[ext=m4a]/bestaudio/best";

    auto extract_first_http_url = [prefer_audio](const std::string& output) -> std::string {
        std::stringstream ss(output);
        std::string line;
        std::string chosen;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.rfind("https://", 0) == 0 || line.rfind("http://", 0) == 0) {
                chosen = line;
                if (prefer_audio) break;
            }
        }
        return chosen;
    };

    std::string fast_cmd =
        "yt-dlp --no-cookies --force-ipv4 --source-address 0.0.0.0 --no-warnings --no-check-certificates "
        "--no-playlist --socket-timeout 5 --retries 2 --cache-dir /tmp/ytdlp_cache "
        "--extractor-args \"youtube:player_client=visionos,android;player_skip=webpage,configs,initial_data,js;skip=hls,dash,translated_subs;formats=missing_pot;fetch_pot=never\" "
        "-g -f \"" + media_format + "\" " + shell_quote(target_url) + " 2>&1";

    LOGGER("AnnieXMedia.platforms.Youtube").info("🔗 Pulling direct YouTube stream URL (cookie-free fast_opts): " + fast_cmd);
    auto [fast_out, fast_rc] = core::run_shell_command(fast_cmd);
    std::string direct_url = extract_first_http_url(fast_out);
    if (!direct_url.empty()) {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        g_url_cache[cache_key] = {direct_url, now + CACHE_TTL_SEC};
        LOGGER("AnnieXMedia.platforms.Youtube").info(
            "✅ Successfully extracted direct YouTube URL via fast_opts (" + std::to_string(direct_url.size()) + " chars)"
        );
        return direct_url;
    }

    std::string fb_cmd =
        "yt-dlp --no-cookies --force-ipv4 --source-address 0.0.0.0 --no-warnings --no-check-certificates "
        "--no-playlist --cache-dir /tmp/ytdlp_cache "
        "--remote-components ejs:github "
        "--extractor-args \"youtube:player_client=tv_samsung,web_embedded;player_skip=webpage,initial_data;skip=translated_subs;fetch_pot=never\" "
        "-g -f \"" + media_format + "\" " + shell_quote(target_url) + " 2>&1";

    LOGGER("AnnieXMedia.platforms.Youtube").info("🔄 Trying fallback_opts (tv_samsung,web_embedded cookie-free): " + fb_cmd);
    auto [fb_out, fb_rc] = core::run_shell_command(fb_cmd);
    direct_url = extract_first_http_url(fb_out);
    if (!direct_url.empty()) {
        std::lock_guard<std::mutex> lk(g_yt_cache_mtx);
        g_url_cache[cache_key] = {direct_url, now + CACHE_TTL_SEC};
        LOGGER("AnnieXMedia.platforms.Youtube").info(
            "✅ Successfully extracted direct YouTube URL via fallback_opts (" + std::to_string(direct_url.size()) + " chars)"
        );
        return direct_url;
    }

    std::string full_reason =
        "❌ تعذر استخراج الرابط المباشر من يوتيوب (بدون كوكيز)!\n"
        "• الرابط المطلوب: " + target_url + "\n"
        "• رد محرك الاستخراج:\n" + (fb_out.empty() ? fast_out : fb_out);

    LOGGER("AnnieXMedia.platforms.Youtube").error(full_reason);
    throw std::runtime_error(full_reason);
}

std::string YouTubeAPI::download(const std::string& query_or_link, bool is_video, bool is_videoid) {
    std::string vid = extract_vid(query_or_link, is_videoid);
    std::string target = is_valid_vidid(vid) ? (base + vid) : query_or_link;
    if (!is_valid_vidid(vid) && !exists(target) && target.rfind("http", 0) != 0) {
        auto td = track(target, false);
        target = td.link;
    }
    return get_direct_link(target, !is_video);
}

std::vector<TrackDetails> YouTubeAPI::playlist(const std::string& link, int limit) {
    std::vector<TrackDetails> list;
    std::string target = link;
    if (target.find('&') != std::string::npos && target.find("list=") != std::string::npos) {
        auto lpos = target.find("list=");
        std::string list_id = target.substr(lpos + 5);
        auto amp = list_id.find('&');
        if (amp != std::string::npos) list_id = list_id.substr(0, amp);
        target = listbase + list_id;
    }

    std::string cmd =
        "yt-dlp --no-cookies --force-ipv4 --no-warnings --no-check-certificates --flat-playlist "
        "--playlist-end " + std::to_string(limit > 0 ? limit : 25) + " "
        "--extractor-args \"youtubetab:skip=webpage\" "
        "--print \"%(id)s\t%(title)s\t%(duration)s\" " + shell_quote(target) + " 2>&1";

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
            td.duration_min = sec_to_min_str(td.duration_sec);
            td.link = base + td.vidid;
            td.thumb = "https://i.ytimg.com/vi/" + td.vidid + "/hqdefault.jpg";
            list.push_back(td);
        }
    } else {
        LOGGER("AnnieXMedia.platforms.Youtube").error("❌ YouTube playlist error:\n" + out);
    }
    return list;
}

} // namespace AnnieXMedia::platforms
